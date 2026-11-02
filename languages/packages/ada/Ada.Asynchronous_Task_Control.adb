--  Ada.Asynchronous_Task_Control.adb
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

--  A held task does not run until it is continued, unless it inherits a
--  priority (a rendezvous: D.11(4/2)); the runtime's scheduler does this
--  (see the Ada runtime's tasking).


package body Ada.Asynchronous_Task_Control is

   use Ada.Task_Identification;

   --  (a held task does not run until it is continued: the runtime's (see D.11))
   procedure Set_Held (T : Task_Id; Held : Integer);
   pragma Import (Neos, Set_Held, "ada::task_hold");
   function Held (T : Task_Id) return Integer;
   pragma Import (Neos, Held, "ada::task_held");

   procedure Check (T : Task_Id) is
   begin
      if T = Null_Task_Id then
         raise Program_Error;
      end if;
      if Is_Terminated (T) then
         raise Tasking_Error;
      end if;
   end Check;

   procedure Hold (T : in Ada.Task_Identification.Task_Id) is
   begin
      Check (T);
      Set_Held (T, 1);
   end Hold;

   procedure Continue (T : in Ada.Task_Identification.Task_Id) is
   begin
      Check (T);
      Set_Held (T, 0);
   end Continue;

   function Is_Held (T : Ada.Task_Identification.Task_Id)
    return Boolean is
   begin
      Check (T);
      return Held (T) /= 0;
   end Is_Held;

end Ada.Asynchronous_Task_Control;
