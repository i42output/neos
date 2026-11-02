--  Ada.Task_Termination.adb
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

--  The handlers are the runtime's: it calls a terminating task's handler (its
--  specific handler, else the fallback handler of the nearest of its masters'
--  tasks that has one) as the task's last action (C.7.3(14/2)). A handler is
--  an access-to-protected-procedure value: the address of its descriptor
--  (its procedure, static link and object), which the runtime keeps.

with Ada.Unchecked_Conversion;

package body Ada.Task_Termination is

   use Ada.Task_Identification;

   type Address_Value is range -2 ** 63 .. 2 ** 63 - 1;
   function To_Value is new Ada.Unchecked_Conversion (Termination_Handler, Address_Value);
   function To_Handler is new Ada.Unchecked_Conversion (Address_Value, Termination_Handler);

   --  (a task's handler: its specific one (Kind 0) or its dependents' fallback (1))
   procedure Set (T : Task_Id; Kind : Integer; Handler : Address_Value);
   pragma Import (Neos, Set, "ada::termination_handler_set");
   function Get (T : Task_Id; Kind : Integer) return Address_Value;
   pragma Import (Neos, Get, "ada::termination_handler");

   procedure Set_Dependents_Fallback_Handler
     (Handler: in Termination_Handler) is
   begin
      Set (Current_Task, 1, To_Value (Handler));
   end Set_Dependents_Fallback_Handler;

   function Current_Task_Fallback_Handler return Termination_Handler is
   begin
      return To_Handler (Get (Current_Task, 1));
   end Current_Task_Fallback_Handler;

   procedure Set_Specific_Handler
     (T       : in Ada.Task_Identification.Task_Id;
      Handler : in Termination_Handler) is
   begin
      if T = Null_Task_Id then
         raise Program_Error;
      end if;
      if Is_Terminated (T) then
         raise Tasking_Error;
      end if;
      Set (T, 0, To_Value (Handler));
   end Set_Specific_Handler;

   function Specific_Handler (T : Ada.Task_Identification.Task_Id)
      return Termination_Handler is
   begin
      if T = Null_Task_Id then
         raise Program_Error;
      end if;
      if Is_Terminated (T) then
         raise Tasking_Error;
      end if;
      return To_Handler (Get (T, 0));
   end Specific_Handler;

end Ada.Task_Termination;
