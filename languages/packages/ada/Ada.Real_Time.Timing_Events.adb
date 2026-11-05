--  Ada.Real_Time.Timing_Events.adb
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

--  The set events are queued by time, and in the order in which they were
--  set for the same time. A server task (of priority Interrupt_Priority'Last,
--  as if the clock's interrupt executed the handlers (D.15(25/2))), which
--  exists while events are queued, waits until the first event's time (or
--  until the queue changes) and then executes the handlers of the events whose
--  time has come. (A
--  program's library-level tasks are awaited when its main subprogram
--  completes: a program with an event that is still set ends after its
--  handler is executed.)

with Interfaces;
with System;
with Ada.Unchecked_Conversion;
with Ada.Unchecked_Deallocation;

package body Ada.Real_Time.Timing_Events is

   use type Interfaces.Integer_64;

   --  (the ceiling priority of a handler's protected object; whether the
   --  locking policy is Ceiling_Locking)
   function To_Value is new Ada.Unchecked_Conversion (Timing_Event_Handler, Interfaces.Integer_64);
   function Ceiling (Handler : Interfaces.Integer_64) return Interfaces.Integer_64;
   pragma Import (Neos, Ceiling, "ada::handler_ceiling");
   function Ceiling_Locking return Interfaces.Integer_64;
   pragma Import (Neos, Ceiling_Locking, "ada::ceiling_locking");

   type Event_Access is access all Timing_Event;

   Interrupt_Priority_Last : constant := System.Interrupt_Priority'Last;

   type Node;
   type Node_Access is access Node;
   type Node is record
      Event : Event_Access;
      Next  : Node_Access;
   end record;

   procedure Free is new Ada.Unchecked_Deallocation (Node, Node_Access);

   protected Queue with Priority => System.Any_Priority'Last is
      --  (Event set: to execute Handler at At_Time)
      procedure Set (Event : Event_Access; At_Time : Time; Handler : Timing_Event_Handler);
      --  (Event cleared; Cancelled: whether it was set)
      procedure Clear (Event : Event_Access; Cancelled : out Boolean);
      --  (the time of the first event (Found: whether there is one))
      procedure First_Time (At_Time : out Time; Found : out Boolean);
      --  (the first event, if its time has come (else null), cleared, and its handler)
      procedure Take_Due (Event : out Event_Access; Handler : out Timing_Event_Handler);
      --  (a server task is needed and none exists (one is then taken to exist))
      procedure Need_Server (Start : out Boolean);
      --  (the server task can end: no event is queued (one is then taken not to exist))
      procedure End_Server (Done : out Boolean);
      --  (the queue has changed since the last call)
      entry Changed;
   private
      First   : Node_Access;
      Change  : Boolean := False;
      Running : Boolean := False;
   end Queue;

   protected body Queue is

      procedure Remove (Event : Event_Access) is
         N : Node_Access := First;
         Previous : Node_Access;
      begin
         while N /= null loop
            if N.Event = Event then
               if Previous = null then
                  First := N.Next;
               else
                  Previous.Next := N.Next;
               end if;
               Free (N);
               return;
            end if;
            Previous := N;
            N := N.Next;
         end loop;
      end Remove;

      procedure Set (Event : Event_Access; At_Time : Time; Handler : Timing_Event_Handler) is
         N : Node_Access := First;
         Previous : Node_Access;
      begin
         if Event.Handler /= null then
            Remove (Event);
         end if;
         Event.Handler := Handler;
         Event.At_Time := At_Time;
         while N /= null and then N.Event.At_Time <= At_Time loop
            Previous := N;
            N := N.Next;
         end loop;
         if Previous = null then
            First := new Node'(Event => Event, Next => First);
         else
            Previous.Next := new Node'(Event => Event, Next => N);
         end if;
         Change := True;
      end Set;

      procedure Clear (Event : Event_Access; Cancelled : out Boolean) is
      begin
         Cancelled := Event.Handler /= null;
         if Cancelled then
            Remove (Event);
            Event.Handler := null;
            Change := True;
         end if;
      end Clear;

      procedure First_Time (At_Time : out Time; Found : out Boolean) is
      begin
         Found := First /= null;
         if Found then
            At_Time := First.Event.At_Time;
         else
            At_Time := Time_First;
         end if;
      end First_Time;

      procedure Take_Due (Event : out Event_Access; Handler : out Timing_Event_Handler) is
         N : Node_Access := First;
      begin
         Event := null;
         Handler := null;
         if N /= null and then N.Event.At_Time <= Clock then
            Event := N.Event;
            Handler := Event.Handler;
            Event.Handler := null;
            First := N.Next;
            Free (N);
         end if;
      end Take_Due;

      procedure Need_Server (Start : out Boolean) is
      begin
         Start := not Running;
         Running := True;
      end Need_Server;

      procedure End_Server (Done : out Boolean) is
      begin
         Done := First = null;
         if Done then
            Running := False;
         end if;
      end End_Server;

      entry Changed when Change is
      begin
         Change := False;
      end Changed;

   end Queue;

   task type Server with Interrupt_Priority => System.Interrupt_Priority'Last;
   type Server_Access is access Server;

   task body Server is
      Event : Event_Access;
      Handler : Timing_Event_Handler;
      At_Time : Time;
      Found, Done : Boolean;
   begin
      loop
         Queue.First_Time (At_Time, Found);
         if Found then
            select
               Queue.Changed;
            or
               delay until At_Time;
            end select;
            loop
               Queue.Take_Due (Event, Handler);
               exit when Event = null;
               begin
                  Handler.all (Event.all);
               exception
                  when others =>
                     null;   --  (D.15(21))
               end;
            end loop;
         else
            Queue.End_Server (Done);
            exit when Done;
         end if;
      end loop;
   end Server;

   procedure Set_Handler (Event   : in out Timing_Event;
                          At_Time : in Time;
                          Handler : in Timing_Event_Handler) is
      Cancelled, Start : Boolean;
      New_Server : Server_Access;
   begin
      if Handler = null then
         Queue.Clear (Event'Unchecked_Access, Cancelled);
         return;
      end if;
      --  (under Ceiling_Locking the handler's ceiling is Interrupt_Priority'Last
      --  (D.15(14/2)))
      if Ceiling_Locking /= 0 and then Ceiling (To_Value (Handler)) /= Interrupt_Priority_Last then
         raise Program_Error;
      end if;
      Queue.Set (Event'Unchecked_Access, At_Time, Handler);
      Queue.Need_Server (Start);
      if Start then
         New_Server := new Server;
      end if;
   end Set_Handler;

   procedure Set_Handler (Event   : in out Timing_Event;
                          In_Time : in Time_Span;
                          Handler : in Timing_Event_Handler) is
   begin
      Set_Handler (Event, Clock + In_Time, Handler);
   end Set_Handler;

   function Current_Handler (Event : Timing_Event)
        return Timing_Event_Handler is
   begin
      return Event.Handler;
   end Current_Handler;

   procedure Cancel_Handler (Event     : in out Timing_Event;
                             Cancelled : out Boolean) is
   begin
      Queue.Clear (Event'Unchecked_Access, Cancelled);
   end Cancel_Handler;

   function Time_Of_Event (Event : Timing_Event) return Time is
   begin
      if Event.Handler = null then
         return Time_First;
      end if;
      return Event.At_Time;
   end Time_Of_Event;

   overriding procedure Finalize (Event : in out Timing_Event) is
      Cancelled : Boolean;
   begin
      if Event.Handler /= null then
         Queue.Clear (Event'Unchecked_Access, Cancelled);
      end if;
   end Finalize;

end Ada.Real_Time.Timing_Events;
