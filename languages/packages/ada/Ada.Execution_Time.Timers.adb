--  Ada.Execution_Time.Timers.adb
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

--  A timer's expiry is a runtime execution-time event (keyed by the timer's
--  address): its task's execution time reaching the timer's time. A server
--  task (of priority Interrupt_Priority'Last), which exists while timers are
--  set, waits for the events and executes the handlers of the timers that
--  expire. The runtime detects an expiry at the running task's next
--  dispatching point or poll (at most 100 microseconds apart while it executes
--  a loop), with the accuracy of the execution time clocks.

with Interfaces;
with Ada.Unchecked_Conversion;
with System.Address_To_Access_Conversions;

package body Ada.Execution_Time.Timers is

   use type Ada.Task_Identification.Task_Id;
   use type Interfaces.Integer_64;
   use type System.Address;

   subtype Integer_64 is Interfaces.Integer_64;

   Kind : constant := 1;   --  (the runtime's kind of events: timers')

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
   --  (the key of an expired event; 0: none can expire; -1: a deadlock)
   function Wait (Kind : Integer_64) return Integer_64;
   pragma Import (Neos, Wait, "ada::cpu_event_wait");
   function Live (Kind : Integer_64) return Integer_64;
   pragma Import (Neos, Live, "ada::cpu_event_live");
   procedure Preempt;
   pragma Import (Neos, Preempt, "ada::preempt");

   package Conversions is new System.Address_To_Access_Conversions (Timer);
   subtype Timer_Access is Conversions.Object_Pointer;

   function To_Address is new Ada.Unchecked_Conversion (Integer_64, System.Address);

   Billion : constant := 1_000_000_000;

   --  (an execution time as a number of nanoseconds)
   function Nanoseconds_Of (TS : Time_Span) return Integer_64 is
      Nanosecond : constant Duration := 1.0E-9;
   begin
      return Integer_64 (To_Duration (TS) / Nanosecond);
   end Nanoseconds_Of;

   function To_Time_Span (N : Integer_64) return Time_Span is
   begin
      return Seconds (Integer (N / Billion)) + Nanoseconds (Integer (N rem Billion));
   end To_Time_Span;

   protected Queue with Priority => System.Any_Priority'Last is
      --  (TM set: its handler, its event armed to expire when its task's
      --  execution time reaches At_Time)
      procedure Set (TM : Timer_Access; At_Time : Integer_64; Handler : Timer_Handler);
      procedure Clear (TM : Timer_Access; Cancelled : out Boolean);
      --  (the timer of an expired event, cleared (D.14.1(17/2)), and its
      --  handler (null: it is no longer set))
      procedure Take (Key : System.Address; TM : out Timer_Access; Handler : out Timer_Handler);
      --  (a server task is needed and none exists (one is then taken to exist))
      procedure Need_Server (Start : out Boolean);
      --  (the server task can end: no timer can expire)
      procedure End_Server (Done : out Boolean);
      --  (the server task has ended (a deadlock))
      procedure Stop;
   private
      Running : Boolean := False;
   end Queue;

   protected body Queue is

      procedure Set (TM : Timer_Access; At_Time : Integer_64; Handler : Timer_Handler) is
      begin
         if not TM.Created then
            Create (TM.all'Address, Kind);
            Member (TM.all'Address, TM.T.all, 1);
            TM.Created := True;
         end if;
         TM.Handler := Handler;
         --  (the event's member's base is 0: its limit is the execution time)
         Arm (TM.all'Address, At_Time, 1);
      end Set;

      procedure Clear (TM : Timer_Access; Cancelled : out Boolean) is
      begin
         Cancelled := TM.Handler /= null;
         TM.Handler := null;
         if TM.Created then
            Arm (TM.all'Address, 0, 0);
         end if;
      end Clear;

      procedure Take (Key : System.Address; TM : out Timer_Access; Handler : out Timer_Handler) is
      begin
         TM := Conversions.To_Pointer (Key);
         Handler := TM.Handler;
         TM.Handler := null;
      end Take;

      procedure Need_Server (Start : out Boolean) is
      begin
         Start := not Running;
         Running := True;
      end Need_Server;

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

   end Queue;

   task type Server with Interrupt_Priority => System.Interrupt_Priority'Last;
   type Server_Access is access Server;

   task body Server is
      Key : Integer_64;
      TM : Timer_Access;
      Handler : Timer_Handler;
      Done : Boolean;
   begin
      loop
         Key := Wait (Kind);
         --  (none can expire: the server ends unless one is set meanwhile; a
         --  deadlock (no task can run): it ends)
         if Key = 0 then
            Queue.End_Server (Done);
            exit when Done;
         elsif Key = -1 then
            Queue.Stop;
            exit;
         else
            Queue.Take (To_Address (Key), TM, Handler);
            if Handler /= null then
               begin
                  Handler.all (TM.all);
               exception
                  when others =>
                     null;   --  (D.14.1(24/2))
               end;
            end if;
         end if;
      end loop;
   end Server;

   procedure Check (TM : Timer) is
   begin
      if TM.T.all = Ada.Task_Identification.Null_Task_Id then
         raise Program_Error;
      end if;
      if Ada.Task_Identification.Is_Terminated (TM.T.all) then
         raise Tasking_Error;
      end if;
   end Check;

   procedure Set (TM : in out Timer; At_Time : Integer_64; Handler : Timer_Handler) is
      Cancelled, Start : Boolean;
      New_Server : Server_Access;
   begin
      Check (TM);
      if Handler = null then
         Queue.Clear (Conversions.To_Pointer (TM'Address), Cancelled);
         return;
      end if;
      Queue.Set (Conversions.To_Pointer (TM'Address), At_Time, Handler);
      Queue.Need_Server (Start);
      if Start then
         New_Server := new Server;
      end if;
      --  (it may have expired already: its handler is executed now)
      Preempt;
   end Set;

   procedure Set_Handler (TM      : in out Timer;
                          In_Time : in Time_Span;
                          Handler : in Timer_Handler) is
   begin
      Check (TM);
      Set (TM, Integer_64 (Clock (TM.T.all)) + Integer_64'Max (Nanoseconds_Of (In_Time), 0), Handler);
   end Set_Handler;

   procedure Set_Handler (TM      : in out Timer;
                          At_Time : in CPU_Time;
                          Handler : in Timer_Handler) is
   begin
      Set (TM, Integer_64 (At_Time), Handler);
   end Set_Handler;

   function Current_Handler (TM : Timer) return Timer_Handler is
   begin
      Check (TM);
      return TM.Handler;
   end Current_Handler;

   procedure Cancel_Handler (TM        : in out Timer;
                             Cancelled :    out Boolean) is
   begin
      Check (TM);
      Queue.Clear (Conversions.To_Pointer (TM'Address), Cancelled);
   end Cancel_Handler;

   function Time_Remaining (TM : Timer) return Time_Span is
   begin
      Check (TM);
      if TM.Handler = null then
         return Time_Span_Zero;
      end if;
      return To_Time_Span (Integer_64'Max (Limit_Of (TM'Address) - Consumed (TM'Address), 0));
   end Time_Remaining;

   overriding procedure Finalize (TM : in out Timer) is
      Cancelled : Boolean;
   begin
      if TM.Created then
         Queue.Clear (Conversions.To_Pointer (TM'Address), Cancelled);
         Delete (TM'Address);
         TM.Created := False;
      end if;
   end Finalize;

end Ada.Execution_Time.Timers;
