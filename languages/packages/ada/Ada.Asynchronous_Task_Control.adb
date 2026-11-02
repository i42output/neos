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

--  neos's tasks run in turn on one processor and a task is only switched at
--  the points where it blocks or yields (see Ada.Task_Identification), so a
--  held task can only be prevented from running when it holds itself: Hold
--  applied to the calling task blocks it until Continue is applied to it;
--  applied to another task, Hold makes it held (Is_Held) but does not stop it.

with Ada.Synchronous_Task_Control;

package body Ada.Asynchronous_Task_Control is

   use Ada.Task_Identification;

   type Node;
   type Node_Access is access Node;
   type Node is limited record
      Id      : Task_Id;
      Held    : Boolean := False;
      Release : Ada.Synchronous_Task_Control.Suspension_Object;
      Next    : Node_Access;
   end record;

   --  (the tasks that have been held)
   List : Node_Access;

   procedure Check (T : Task_Id) is
   begin
      if T = Null_Task_Id then
         raise Program_Error;
      end if;
      if Is_Terminated (T) then
         raise Tasking_Error;
      end if;
   end Check;

   function Find (T : Task_Id) return Node_Access is
      N : Node_Access := List;
   begin
      while N /= null loop
         if N.Id = T then
            return N;
         end if;
         N := N.Next;
      end loop;
      return null;
   end Find;

   procedure Hold (T : in Ada.Task_Identification.Task_Id) is
      N : Node_Access;
   begin
      Check (T);
      N := Find (T);
      if N = null then
         N := new Node;
         N.Id := T;
         N.Next := List;
         List := N;
      end if;
      if not N.Held then
         N.Held := True;
         Ada.Synchronous_Task_Control.Set_False (N.Release);
         if T = Current_Task then
            Ada.Synchronous_Task_Control.Suspend_Until_True (N.Release);
         end if;
      end if;
   end Hold;

   procedure Continue (T : in Ada.Task_Identification.Task_Id) is
      N : Node_Access;
   begin
      Check (T);
      N := Find (T);
      if N /= null and then N.Held then
         N.Held := False;
         Ada.Synchronous_Task_Control.Set_True (N.Release);
      end if;
   end Continue;

   function Is_Held (T : Ada.Task_Identification.Task_Id)
    return Boolean is
      N : Node_Access;
   begin
      Check (T);
      N := Find (T);
      return N /= null and then N.Held;
   end Is_Held;

end Ada.Asynchronous_Task_Control;
