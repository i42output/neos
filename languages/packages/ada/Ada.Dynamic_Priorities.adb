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

--  neos's dispatching does not depend on priorities (its tasks run in turn:
--  see Ada.Task_Identification): a task's base priority is a value that this
--  package keeps. A task whose priority has not been set has the default
--  priority (System.Default_Priority).

package body Ada.Dynamic_Priorities is

   use Ada.Task_Identification;

   type Node;
   type Node_Access is access Node;
   type Node is record
      Id       : Task_Id;
      Priority : System.Any_Priority;
      Next     : Node_Access;
   end record;

   --  (the tasks whose priorities have been set)
   List : Node_Access;

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

   procedure Set_Priority(Priority : in System.Any_Priority;
                          T : in Ada.Task_Identification.Task_Id :=
                          Ada.Task_Identification.Current_Task) is
      N : Node_Access;
   begin
      if T = Null_Task_Id then
         raise Program_Error;
      end if;
      if Is_Terminated (T) then
         return;
      end if;
      N := Find (T);
      if N = null then
         List := new Node'(Id => T, Priority => Priority, Next => List);
      else
         N.Priority := Priority;
      end if;
   end Set_Priority;

   function Get_Priority (T : Ada.Task_Identification.Task_Id :=
                          Ada.Task_Identification.Current_Task)
                          return System.Any_Priority is
      N : Node_Access;
   begin
      if T = Null_Task_Id then
         raise Program_Error;
      end if;
      if Is_Terminated (T) then
         raise Tasking_Error;
      end if;
      N := Find (T);
      if N = null then
         return System.Default_Priority;
      end if;
      return N.Priority;
   end Get_Priority;

end Ada.Dynamic_Priorities;
