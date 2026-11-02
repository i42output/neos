--  Ada.Task_Identification.adb
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

package body Ada.Task_Identification is

   function Current return Task_Id;
   pragma Import (Neos, Current, "ada::current_task");
   procedure Abort_It (T : Task_Id);
   pragma Import (Neos, Abort_It, "ada::task_abort");
   function Terminated (T : Task_Id) return Task_Id;
   pragma Import (Neos, Terminated, "ada::task_terminated");
   function Callable (T : Task_Id) return Task_Id;
   pragma Import (Neos, Callable, "ada::task_callable");

   function Image (T : Task_Id) return String is
      Number : constant String := Task_Id'Image (T);
   begin
      if T = Null_Task_Id then
         return "";
      end if;
      return "task" & Number (Number'First + 1 .. Number'Last);
   end Image;

   function Current_Task return Task_Id is
   begin
      return Current;
   end Current_Task;

   function Environment_Task return Task_Id is
   begin
      return 1;
   end Environment_Task;

   procedure Abort_Task (T : in Task_Id) is
   begin
      if T = Null_Task_Id then
         raise Program_Error;
      end if;
      Abort_It (T);
   end Abort_Task;

   function Is_Terminated (T : Task_Id) return Boolean is
   begin
      if T = Null_Task_Id then
         raise Program_Error;
      end if;
      return Terminated (T) /= 0;
   end Is_Terminated;

   function Is_Callable (T : Task_Id) return Boolean is
   begin
      if T = Null_Task_Id then
         raise Program_Error;
      end if;
      return Callable (T) /= 0;
   end Is_Callable;

   function Activation_Is_Complete (T : Task_Id) return Boolean is
   begin
      if T = Null_Task_Id then
         raise Program_Error;
      end if;
      return True;
   end Activation_Is_Complete;

end Ada.Task_Identification;
