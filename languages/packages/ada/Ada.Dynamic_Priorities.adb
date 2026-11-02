--  Ada.Dynamic_Priorities.adb
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

--  A task's base priority is the runtime's (its scheduler dispatches by
--  priority: see the Ada runtime's tasking). Setting it is a dispatching
--  point, and requeues the task's entry call (if it is blocked on one) by its
--  new priority under Priority_Queuing (D.4(10)).

package body Ada.Dynamic_Priorities is

   use Ada.Task_Identification;

   --  (a task's base priority: the runtime's (see D.1, D.5.1))
   procedure Set (T : Task_Id; Priority : System.Any_Priority);
   pragma Import (Neos, Set, "ada::set_priority");
   function Get (T : Task_Id) return System.Any_Priority;
   pragma Import (Neos, Get, "ada::get_priority");

   procedure Set_Priority(Priority : in System.Any_Priority;
                          T : in Ada.Task_Identification.Task_Id :=
                          Ada.Task_Identification.Current_Task) is
   begin
      if T = Null_Task_Id then
         raise Program_Error;
      end if;
      if Is_Terminated (T) then
         return;
      end if;
      Set (T, Priority);
   end Set_Priority;

   function Get_Priority (T : Ada.Task_Identification.Task_Id :=
                          Ada.Task_Identification.Current_Task)
                          return System.Any_Priority is
   begin
      if T = Null_Task_Id then
         raise Program_Error;
      end if;
      if Is_Terminated (T) then
         raise Tasking_Error;
      end if;
      return Get (T);
   end Get_Priority;

end Ada.Dynamic_Priorities;
