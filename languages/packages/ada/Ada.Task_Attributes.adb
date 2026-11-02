--  Ada.Task_Attributes.adb
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

--  A task's attribute is created when it is first referenced or set (until
--  then its value is Initial_Value); the attributes of terminated tasks are
--  finalized (and their storage reclaimed) when another task's attribute is
--  created, and when the instance is finalized.

with Ada.Unchecked_Deallocation;

package body Ada.Task_Attributes is

   type Node;
   type Node_Access is access Node;
   type Node is record
      Id    : Task_Id;
      Value : aliased Attribute;
      Next  : Node_Access;
   end record;

   procedure Free is new Ada.Unchecked_Deallocation (Node, Node_Access);

   --  (the tasks' attributes that have been created)
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

   --  (T's attribute, created if need be)
   function Attribute_Of (T : Task_Id) return Node_Access is
      N : Node_Access := Find (T);
      Previous, Next : Node_Access;
   begin
      if N = null then
         --  (the attributes of terminated tasks)
         N := List;
         while N /= null loop
            Next := N.Next;
            if Is_Terminated (N.Id) then
               if Previous = null then
                  List := Next;
               else
                  Previous.Next := Next;
               end if;
               Free (N);
            else
               Previous := N;
            end if;
            N := Next;
         end loop;
         N := new Node'(Id => T, Value => Initial_Value, Next => List);
         List := N;
      end if;
      return N;
   end Attribute_Of;

   function Value(T : Task_Id := Current_Task)
     return Attribute is
      N : Node_Access;
   begin
      Check (T);
      N := Find (T);
      if N = null then
         return Initial_Value;
      end if;
      return N.Value;
   end Value;

   function Reference(T : Task_Id := Current_Task)
     return Attribute_Handle is
   begin
      Check (T);
      return Attribute_Of (T).Value'Access;
   end Reference;

   procedure Set_Value(Val : in Attribute;
                       T : in Task_Id := Current_Task) is
   begin
      Check (T);
      Attribute_Of (T).Value := Val;
   end Set_Value;

   procedure Reinitialize(T : in Task_Id := Current_Task) is
      N : Node_Access;
   begin
      Check (T);
      N := Find (T);
      if N /= null then
         N.Value := Initial_Value;
      end if;
   end Reinitialize;

end Ada.Task_Attributes;
