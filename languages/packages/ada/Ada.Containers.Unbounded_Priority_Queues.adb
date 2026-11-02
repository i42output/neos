--  Ada.Containers.Unbounded_Priority_Queues.adb
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

with Ada.Unchecked_Deallocation;

package body Ada.Containers.Unbounded_Priority_Queues is

   package body Implementation is

      procedure Free is new Ada.Unchecked_Deallocation (Node, Node_Access);

      function Length (Container : List) return Count_Type is
        (Container.Length);

      function Peak (Container : List) return Count_Type is
        (Container.Peak);

      procedure Append (Container : in out List;
                        New_Item  : Queue_Interfaces.Element_Type)
      is
         P : constant Queue_Priority := Get_Priority (New_Item);
         --  The new node goes after the last node whose priority is not
         --  after P (searching from the back: new items usually go last).
         After : Node_Access := Container.Last;
         X     : Node_Access;
      begin
         while After /= null
           and then Before (P, Get_Priority (After.Element))
         loop
            After := After.Prev;
         end loop;
         X := new Node'(Element => New_Item, Next => null, Prev => After);
         if After = null then
            X.Next := Container.First;
            Container.First := X;
         else
            X.Next := After.Next;
            After.Next := X;
         end if;
         if X.Next = null then
            Container.Last := X;
         else
            X.Next.Prev := X;
         end if;
         Container.Length := Container.Length + 1;
         if Container.Length > Container.Peak then
            Container.Peak := Container.Length;
         end if;
      end Append;

      procedure Remove_First (Container : in out List;
                              Element   : out Queue_Interfaces.Element_Type)
      is
         X : Node_Access := Container.First;
      begin
         Element := X.Element;
         Container.First := X.Next;
         if Container.First = null then
            Container.Last := null;
         else
            Container.First.Prev := null;
         end if;
         Container.Length := Container.Length - 1;
         Free (X);
      end Remove_First;

      function First_Priority (Container : List) return Queue_Priority is
        (Get_Priority (Container.First.Element));

      overriding procedure Finalize (Container : in out List) is
         X : Node_Access;
      begin
         while Container.First /= null loop
            X := Container.First;
            Container.First := X.Next;
            Free (X);
         end loop;
         Container.Last := null;
         Container.Length := 0;
      end Finalize;

   end Implementation;

   protected body Queue is

      entry Enqueue (New_Item : in Queue_Interfaces.Element_Type)
        when True
      is
      begin
         Implementation.Append (Items, New_Item);
      end Enqueue;

      entry Dequeue (Element : out Queue_Interfaces.Element_Type)
        when Implementation.Length (Items) > 0
      is
      begin
         Implementation.Remove_First (Items, Element);
      end Dequeue;

      procedure Dequeue_Only_High_Priority
        (At_Least : in     Queue_Priority;
         Element  : in out Queue_Interfaces.Element_Type;
         Success  :    out Boolean) is
      begin
         Success := Implementation.Length (Items) > 0
           and then not Before (At_Least, Implementation.First_Priority (Items));
         if Success then
            Implementation.Remove_First (Items, Element);
         end if;
      end Dequeue_Only_High_Priority;

      function Current_Use return Count_Type is
        (Implementation.Length (Items));

      function Peak_Use return Count_Type is
        (Implementation.Peak (Items));

   end Queue;

end Ada.Containers.Unbounded_Priority_Queues;
