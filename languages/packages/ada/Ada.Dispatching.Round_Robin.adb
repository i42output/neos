--  Ada.Dispatching.Round_Robin.adb
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

--  The quanta are the runtime's (its scheduler accounts each task's execution
--  time against its budget, see D.2.5): any positive quantum is supported, as
--  set; a budget's exhaustion is detected at the running task's next
--  dispatching point or poll (at most 100 microseconds apart while it executes
--  a loop), with the accuracy of the execution time clocks (D.14).

with Interfaces;

package body Ada.Dispatching.Round_Robin is

   use type Interfaces.Integer_64;

   Round_Robin_Within_Priorities : constant := 3;   --  (the runtime's policy)

   function Policy (Pri : System.Any_Priority) return Interfaces.Integer_64;
   pragma Import (Neos, Policy, "ada::policy");
   procedure Set (Low, High : System.Any_Priority; Quantum : Ada.Real_Time.Time_Span);
   pragma Import (Neos, Set, "ada::quantum_set");
   function Get (Pri : System.Any_Priority) return Ada.Real_Time.Time_Span;
   pragma Import (Neos, Get, "ada::quantum");

   --  (Dispatching_Policy_Error unless each priority in the range is covered by
   --  Round_Robin_Within_Priorities (D.2.5(9/2)))
   procedure Check (Low, High : System.Priority) is
   begin
      for P in Low .. High loop
         if not Is_Round_Robin (P) then
            raise Dispatching_Policy_Error;
         end if;
      end loop;
   end Check;

   procedure Set_Quantum (Pri     : in System.Priority;
                          Quantum : in Ada.Real_Time.Time_Span) is
   begin
      Set_Quantum (Pri, Pri, Quantum);
   end Set_Quantum;

   procedure Set_Quantum (Low, High : in System.Priority;
                          Quantum   : in Ada.Real_Time.Time_Span) is
   begin
      Check (Low, High);
      Set (Low, High, Quantum);
   end Set_Quantum;

   function Actual_Quantum (Pri : System.Priority)
              return Ada.Real_Time.Time_Span is
   begin
      Check (Pri, Pri);
      return Get (Pri);
   end Actual_Quantum;

   function Is_Round_Robin (Pri : System.Priority) return Boolean is
   begin
      return Policy (Pri) = Round_Robin_Within_Priorities;
   end Is_Round_Robin;

end Ada.Dispatching.Round_Robin;
