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

with Ada.Unchecked_Deallocation;

package body Ada.Execution_Time.Group_Budgets is

   use type Ada.Task_Identification.Task_Id;
   use type System.Address;

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

   procedure Add_Task (GB : in out Group_Budget;
                       T  : in Ada.Task_Identification.Task_Id) is
   begin
      Check (T);
      if Membership_Of (T) /= null then
         raise Group_Budget_Error;
      end if;
      Remove;
      Memberships := new Membership'(T, GB'Address, Memberships);
   end Add_Task;

   procedure Remove_Task (GB: in out Group_Budget;
                          T  : in Ada.Task_Identification.Task_Id) is
   begin
      Check (T);
      if not Is_Member (GB, T) then
         raise Group_Budget_Error;
      end if;
      Remove (T => T);
   end Remove_Task;

   function Is_Member (GB : Group_Budget;
                       T : Ada.Task_Identification.Task_Id) return Boolean is
      M : Membership_Access;
   begin
      Check (T);
      M := Membership_Of (T);
      return M /= null and then M.Group = GB'Address;
   end Is_Member;

   function Is_A_Group_Member
      (T : Ada.Task_Identification.Task_Id) return Boolean is
   begin
      Check (T);
      return Membership_Of (T) /= null;
   end Is_A_Group_Member;

   function Members (GB : Group_Budget) return Task_Array is
      Count : Natural := 0;
      M     : Membership_Access := Memberships;
   begin
      while M /= null loop
         if M.Group = GB'Address and then not Ada.Task_Identification.Is_Terminated (M.T) then
            Count := Count + 1;
         end if;
         M := M.Next;
      end loop;
      return Result : Task_Array (1 .. Count) do
         Count := 0;
         M := Memberships;
         while M /= null loop
            if M.Group = GB'Address and then not Ada.Task_Identification.Is_Terminated (M.T) then
               Count := Count + 1;
               Result (Count) := M.T;
            end if;
            M := M.Next;
         end loop;
      end return;
   end Members;

   --  (the handler of an exhausted budget, if set; an exception that it
   --  propagates has no effect (D.14.2(34/2)))
   procedure Exhausted (GB : in out Group_Budget) is
   begin
      if GB.Handler /= null then
         GB.Handler.all (GB);
      end if;
   exception
      when others =>
         null;
   end Exhausted;

   procedure Replenish (GB : in out Group_Budget; To : in Time_Span) is
   begin
      if To <= Time_Span_Zero then
         raise Group_Budget_Error;
      end if;
      GB.Budget := To;
   end Replenish;

   procedure Add (GB : in out Group_Budget; Interval : in Time_Span) is
   begin
      if Interval = Time_Span_Zero then
         return;
      end if;
      if Interval < Time_Span_Zero and then GB.Budget <= -Interval then
         if GB.Budget > Time_Span_Zero then
            GB.Budget := Time_Span_Zero;
            Exhausted (GB);
         end if;
      else
         GB.Budget := GB.Budget + Interval;
      end if;
   end Add;

   function Budget_Has_Expired (GB : Group_Budget) return Boolean is
   begin
      return GB.Budget = Time_Span_Zero;
   end Budget_Has_Expired;

   function Budget_Remaining (GB : Group_Budget) return Time_Span is
   begin
      return GB.Budget;
   end Budget_Remaining;

   procedure Set_Handler (GB      : in out Group_Budget;
                          Handler : in Group_Budget_Handler) is
   begin
      GB.Handler := Handler;
   end Set_Handler;

   function Current_Handler (GB : Group_Budget)
      return Group_Budget_Handler is
   begin
      return GB.Handler;
   end Current_Handler;

   procedure Cancel_Handler (GB        : in out Group_Budget;
                             Cancelled : out Boolean) is
   begin
      Cancelled := GB.Handler /= null;
      GB.Handler := null;
   end Cancel_Handler;

   overriding procedure Finalize (GB : in out Group_Budget) is
   begin
      Remove (Group => GB'Address);
   end Finalize;

end Ada.Execution_Time.Group_Budgets;
