--  Ada.Execution_Time.Group_Budgets.adb
--
--  Copyright (c) 2026 Leigh Johnston.  All Rights Reserved.
--
--  Redistribution and use in source and binary forms, with or without
--  modification, are permitted provided that the following conditions are
--  met:
--
--     * Redistributions of source code must retain the above copyright
--       notice, this list of conditions and the following disclaimer.
--
--     * Redistributions in binary form must reproduce the above copyright
--       notice, this list of conditions and the following disclaimer in the
--       documentation and/or other materials provided with the distribution.
--
--     * Neither the name of the copyright holder nor the names of its
--       contributors may be used to endorse or promote products derived from
--       this software without specific prior written permission.
--
--  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
--  IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
--  THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
--  PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
--  CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
--  EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
--  PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
--  PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
--  LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
--  NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
--  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

--  A group's budget is a runtime execution-time event (keyed by the group's
--  address): its members' execution time since they were added or the budget
--  was last loaded reaching the budget then (the execution time of a member
--  removed (or terminated) stays counted). A server task (of priority
--  Interrupt_Priority'Last), which exists while budgets with handlers are not
--  exhausted, waits for the events and executes the handlers of the budgets
--  that are exhausted. The runtime detects an exhaustion at the running
--  task's next dispatching point or poll (at most 100 microseconds apart
--  while it executes a loop), with the accuracy of the execution time clocks.

with Interfaces;
with Ada.Unchecked_Conversion;
with Ada.Unchecked_Deallocation;
with System.Address_To_Access_Conversions;

package body Ada.Execution_Time.Group_Budgets is

   use type Ada.Task_Identification.Task_Id;
   use type System.Address;
   use type Interfaces.Integer_64;

   subtype Integer_64 is Interfaces.Integer_64;

   Kind : constant := 2;   --  (the runtime's kind of events: group budgets')

   procedure Create (Key : System.Address; Kind : Integer_64);
   pragma Import (Neos, Create, "ada::cpu_event_create");
   procedure Delete (Key : System.Address);
   pragma Import (Neos, Delete, "ada::cpu_event_delete");
   procedure Member (Key : System.Address; T : Ada.Task_Identification.Task_Id; Add : Integer_64);
   pragma Import (Neos, Member, "ada::cpu_event_member");
   procedure Arm (Key : System.Address; Limit : Integer_64; Armed : Integer_64);
   pragma Import (Neos, Arm, "ada::cpu_event_arm");
   function Consumed (Key : System.Address) return Integer_64;
   pragma Import (Neos, Consumed, "ada::cpu_event_consumed");
   function Limit_Of (Key : System.Address) return Integer_64;
   pragma Import (Neos, Limit_Of, "ada::cpu_event_limit");
   procedure Rebase (Key : System.Address);
   pragma Import (Neos, Rebase, "ada::cpu_event_rebase");
   --  (the key of an expired event; 0: none can expire; -1: a deadlock)
   function Wait (Kind : Integer_64) return Integer_64;
   pragma Import (Neos, Wait, "ada::cpu_event_wait");
   function Live (Kind : Integer_64) return Integer_64;
   pragma Import (Neos, Live, "ada::cpu_event_live");
   procedure Preempt;
   pragma Import (Neos, Preempt, "ada::preempt");

   package Conversions is new System.Address_To_Access_Conversions (Group_Budget);
   subtype Group_Budget_Access is Conversions.Object_Pointer;

   function To_Address is new Ada.Unchecked_Conversion (Integer_64, System.Address);

   Billion : constant := 1_000_000_000;

   function Nanoseconds_Of (TS : Time_Span) return Integer_64 is
      Nanosecond : constant Duration := 1.0E-9;
   begin
      return Integer_64 (To_Duration (TS) / Nanosecond);
   end Nanoseconds_Of;

   function To_Time_Span (N : Integer_64) return Time_Span is
   begin
      return Seconds (Integer (N / Billion)) + Nanoseconds (Integer (N rem Billion));
   end To_Time_Span;


   --  (a member task and the address of its group)
   type Membership;
   type Membership_Access is access Membership;
   type Membership is record
      T     : Ada.Task_Identification.Task_Id;
      Group : System.Address;
      Next  : Membership_Access;
   end record;

   procedure Free is new Ada.Unchecked_Deallocation (Membership, Membership_Access);

   Memberships : Membership_Access;

   procedure Check (T : Ada.Task_Identification.Task_Id) is
   begin
      if T = Ada.Task_Identification.Null_Task_Id then
         raise Program_Error;
      end if;
      if Ada.Task_Identification.Is_Terminated (T) then
         raise Tasking_Error;
      end if;
   end Check;

   --  (the membership of a task that has not terminated (a task is removed
   --  from its group when it terminates (D.14.2(32/3))), or null)
   function Membership_Of (T : Ada.Task_Identification.Task_Id) return Membership_Access is
      M : Membership_Access := Memberships;
   begin
      while M /= null loop
         if M.T = T and then not Ada.Task_Identification.Is_Terminated (M.T) then
            return M;
         end if;
         M := M.Next;
      end loop;
      return null;
   end Membership_Of;

   --  (removes the memberships of terminated tasks, and of the tasks of a
   --  group, or of a task)
   procedure Remove (Group : System.Address := System.Null_Address;
                     T     : Ada.Task_Identification.Task_Id :=
                               Ada.Task_Identification.Null_Task_Id) is
      M    : Membership_Access := Memberships;
      Prev : Membership_Access;
      Next : Membership_Access;
   begin
      while M /= null loop
         Next := M.Next;
         if M.Group = Group or else (T /= Ada.Task_Identification.Null_Task_Id and then M.T = T)
           or else Ada.Task_Identification.Is_Terminated (M.T)
         then
            if Prev = null then
               Memberships := Next;
            else
               Prev.Next := Next;
            end if;
            Free (M);
         else
            Prev := M;
         end if;
         M := Next;
      end loop;
   end Remove;

   --  (a group's runtime event, created when it is first needed)
   procedure Ensure (GB : Group_Budget_Access) is
   begin
      if not GB.Created then
         Create (GB.all'Address, Kind);
         GB.Created := True;
      end if;
   end Ensure;

   --  (a group's remaining budget (nanoseconds))
   function Remaining (GB : Group_Budget) return Integer_64 is
   begin
      if not GB.Created then
         return 0;
      end if;
      return Integer_64'Max (Limit_Of (GB'Address) - Consumed (GB'Address), 0);
   end Remaining;

   --  (a group's event armed while its budget is not exhausted and its
   --  handler is set (its limit kept))
   procedure Rearm (GB : Group_Budget_Access) is
      Armed : constant Boolean := GB.Handler /= null and then Remaining (GB.all) > 0;
   begin
      if GB.Created then
         Arm (GB.all'Address, Limit_Of (GB.all'Address), Boolean'Pos (Armed));
      end if;
   end Rearm;

   --  (the operations on groups: atomic with respect to each other)
   protected Queue with Priority => System.Any_Priority'Last is
      procedure Add_Task (GB : Group_Budget_Access; T : Ada.Task_Identification.Task_Id; Done : out Boolean);
      procedure Remove_Task (GB : Group_Budget_Access; T : Ada.Task_Identification.Task_Id; Done : out Boolean);
      function Is_Member (GB : System.Address; T : Ada.Task_Identification.Task_Id) return Boolean;
      function Is_A_Group_Member (T : Ada.Task_Identification.Task_Id) return Boolean;
      function Members (GB : System.Address) return Task_Array;
      --  (a group's budget loaded with Budget (nanoseconds); Exhausted: it
      --  was not and now is, the handler then)
      procedure Load (GB : Group_Budget_Access; Budget : Integer_64;
                      Exhausted : out Boolean; Handler : out Group_Budget_Handler; Start : out Boolean);
      procedure Set_Handler (GB : Group_Budget_Access; Handler : Group_Budget_Handler; Start : out Boolean);
      procedure Cancel_Handler (GB : Group_Budget_Access; Cancelled : out Boolean);
      --  (the group of an exhausted budget, and its handler (null: none))
      procedure Take (Key : System.Address; GB : out Group_Budget_Access; Handler : out Group_Budget_Handler);
      procedure End_Server (Done : out Boolean);
      procedure Stop;
      procedure Finalize (GB : Group_Budget_Access);
   private
      Running : Boolean := False;
   end Queue;

   protected body Queue is

      procedure Add_Task (GB : Group_Budget_Access; T : Ada.Task_Identification.Task_Id; Done : out Boolean) is
      begin
         Done := Membership_Of (T) = null;
         if Done then
            Remove;
            Memberships := new Membership'(T, GB.all'Address, Memberships);
            Ensure (GB);
            Member (GB.all'Address, T, 1);
         end if;
      end Add_Task;

      procedure Remove_Task (GB : Group_Budget_Access; T : Ada.Task_Identification.Task_Id; Done : out Boolean) is
         M : constant Membership_Access := Membership_Of (T);
      begin
         Done := M /= null and then M.Group = GB.all'Address;
         if Done then
            Remove (T => T);
            Member (GB.all'Address, T, 0);
         end if;
      end Remove_Task;

      function Is_Member (GB : System.Address; T : Ada.Task_Identification.Task_Id) return Boolean is
         M : constant Membership_Access := Membership_Of (T);
      begin
         return M /= null and then M.Group = GB;
      end Is_Member;

      function Is_A_Group_Member (T : Ada.Task_Identification.Task_Id) return Boolean is
      begin
         return Membership_Of (T) /= null;
      end Is_A_Group_Member;

      function Members (GB : System.Address) return Task_Array is
         Count : Natural := 0;
         M     : Membership_Access := Memberships;
      begin
         while M /= null loop
            if M.Group = GB and then not Ada.Task_Identification.Is_Terminated (M.T) then
               Count := Count + 1;
            end if;
            M := M.Next;
         end loop;
         return Result : Task_Array (1 .. Count) do
            Count := 0;
            M := Memberships;
            while M /= null loop
               if M.Group = GB and then not Ada.Task_Identification.Is_Terminated (M.T) then
                  Count := Count + 1;
                  Result (Count) := M.T;
               end if;
               M := M.Next;
            end loop;
         end return;
      end Members;

      --  (a server task is needed (a budget with a handler that is not
      --  exhausted) and none exists (one is then taken to exist))
      procedure Need_Server (GB : Group_Budget_Access; Start : out Boolean) is
      begin
         Start := GB.Handler /= null and then Remaining (GB.all) > 0 and then not Running;
         if Start then
            Running := True;
         end if;
      end Need_Server;

      procedure Load (GB : Group_Budget_Access; Budget : Integer_64;
                      Exhausted : out Boolean; Handler : out Group_Budget_Handler; Start : out Boolean) is
         Was : constant Integer_64 := Remaining (GB.all);
      begin
         Ensure (GB);
         Rebase (GB.all'Address);
         Arm (GB.all'Address, Budget, 0);
         Rearm (GB);
         Exhausted := Was > 0 and then Budget <= 0;
         Handler := GB.Handler;
         Need_Server (GB, Start);
      end Load;

      procedure Set_Handler (GB : Group_Budget_Access; Handler : Group_Budget_Handler; Start : out Boolean) is
      begin
         GB.Handler := Handler;
         Rearm (GB);
         Need_Server (GB, Start);
      end Set_Handler;

      procedure Cancel_Handler (GB : Group_Budget_Access; Cancelled : out Boolean) is
      begin
         Cancelled := GB.Handler /= null;
         GB.Handler := null;
         Rearm (GB);
      end Cancel_Handler;

      procedure Take (Key : System.Address; GB : out Group_Budget_Access; Handler : out Group_Budget_Handler) is
      begin
         GB := Conversions.To_Pointer (Key);
         Handler := GB.Handler;
      end Take;

      procedure End_Server (Done : out Boolean) is
      begin
         Done := Live (Kind) = 0;
         if Done then
            Running := False;
         end if;
      end End_Server;

      procedure Stop is
      begin
         Running := False;
      end Stop;

      procedure Finalize (GB : Group_Budget_Access) is
      begin
         Remove (Group => GB.all'Address);
         if GB.Created then
            Delete (GB.all'Address);
            GB.Created := False;
         end if;
      end Finalize;

   end Queue;

   task type Server with Interrupt_Priority => System.Interrupt_Priority'Last;
   type Server_Access is access Server;

   task body Server is
      Key : Integer_64;
      GB : Group_Budget_Access;
      Handler : Group_Budget_Handler;
      Done : Boolean;
   begin
      loop
         Key := Wait (Kind);
         --  (none can expire: the server ends unless one is armed meanwhile; a
         --  deadlock (no task can run): it ends)
         if Key = 0 then
            Queue.End_Server (Done);
            exit when Done;
         elsif Key = -1 then
            Queue.Stop;
            exit;
         else
            Queue.Take (To_Address (Key), GB, Handler);
            if Handler /= null then
               begin
                  Handler.all (GB.all);
               exception
                  when others =>
                     null;   --  (D.14.2(34/2))
               end;
            end if;
         end if;
      end loop;
   end Server;

   function Access_Of (GB : Group_Budget) return Group_Budget_Access is
   begin
      return Conversions.To_Pointer (GB'Address);
   end Access_Of;

   procedure Add_Task (GB : in out Group_Budget;
                       T  : in Ada.Task_Identification.Task_Id) is
      Done : Boolean;
   begin
      Check (T);
      Queue.Add_Task (Access_Of (GB), T, Done);
      if not Done then
         raise Group_Budget_Error;
      end if;
   end Add_Task;

   procedure Remove_Task (GB: in out Group_Budget;
                          T  : in Ada.Task_Identification.Task_Id) is
      Done : Boolean;
   begin
      Check (T);
      Queue.Remove_Task (Access_Of (GB), T, Done);
      if not Done then
         raise Group_Budget_Error;
      end if;
   end Remove_Task;

   function Is_Member (GB : Group_Budget;
                       T : Ada.Task_Identification.Task_Id) return Boolean is
   begin
      Check (T);
      return Queue.Is_Member (GB'Address, T);
   end Is_Member;

   function Is_A_Group_Member
      (T : Ada.Task_Identification.Task_Id) return Boolean is
   begin
      Check (T);
      return Queue.Is_A_Group_Member (T);
   end Is_A_Group_Member;

   function Members (GB : Group_Budget) return Task_Array is
   begin
      return Queue.Members (GB'Address);
   end Members;

   --  (a budget loaded: the handler executed if it is exhausted by that (its
   --  exception has no effect (D.14.2(34/2))))
   procedure Load (GB : in out Group_Budget; Budget : Integer_64) is
      Exhausted, Start : Boolean;
      Handler : Group_Budget_Handler;
      New_Server : Server_Access;
   begin
      Queue.Load (Access_Of (GB), Budget, Exhausted, Handler, Start);
      if Start then
         New_Server := new Server;
      end if;
      if Exhausted and then Handler /= null then
         begin
            Handler.all (GB);
         exception
            when others =>
               null;
         end;
      end if;
   end Load;

   procedure Replenish (GB : in out Group_Budget; To : in Time_Span) is
   begin
      if To <= Time_Span_Zero then
         raise Group_Budget_Error;
      end if;
      Load (GB, Nanoseconds_Of (To));
   end Replenish;

   procedure Add (GB : in out Group_Budget; Interval : in Time_Span) is
   begin
      if Interval = Time_Span_Zero then
         return;
      end if;
      Load (GB, Integer_64'Max (Remaining (GB) + Nanoseconds_Of (Interval), 0));
   end Add;

   function Budget_Has_Expired (GB : Group_Budget) return Boolean is
   begin
      return Remaining (GB) = 0;
   end Budget_Has_Expired;

   function Budget_Remaining (GB : Group_Budget) return Time_Span is
   begin
      return To_Time_Span (Remaining (GB));
   end Budget_Remaining;

   procedure Set_Handler (GB      : in out Group_Budget;
                          Handler : in Group_Budget_Handler) is
      Start : Boolean;
      New_Server : Server_Access;
   begin
      Queue.Set_Handler (Access_Of (GB), Handler, Start);
      if Start then
         New_Server := new Server;
      end if;
   end Set_Handler;

   function Current_Handler (GB : Group_Budget)
      return Group_Budget_Handler is
   begin
      return GB.Handler;
   end Current_Handler;

   procedure Cancel_Handler (GB        : in out Group_Budget;
                             Cancelled : out Boolean) is
   begin
      Queue.Cancel_Handler (Access_Of (GB), Cancelled);
   end Cancel_Handler;

   overriding procedure Finalize (GB : in out Group_Budget) is
   begin
      Queue.Finalize (Access_Of (GB));
   end Finalize;

end Ada.Execution_Time.Group_Budgets;
