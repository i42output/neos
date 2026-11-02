--  Ada.Containers.Unbounded_Synchronized_Queues.adb
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

package body Ada.Containers.Unbounded_Synchronized_Queues is

   package body Implementation is

      procedure Free is new Ada.Unchecked_Deallocation (Node, Node_Access);

      function Length (Container : List) return Count_Type is
        (Container.Length);

      function Peak (Container : List) return Count_Type is
        (Container.Peak);

      procedure Append (Container : in out List;
                        New_Item  : Queue_Interfaces.Element_Type)
      is
         X : constant Node_Access :=
           new Node'(Element => New_Item, Next => null);
      begin
         if Container.Last = null then
            Container.First := X;
         else
            Container.Last.Next := X;
         end if;
         Container.Last := X;
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
         end if;
         Container.Length := Container.Length - 1;
         Free (X);
      end Remove_First;

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

      function Current_Use return Count_Type is
        (Implementation.Length (Items));

      function Peak_Use return Count_Type is
        (Implementation.Peak (Items));

   end Queue;

end Ada.Containers.Unbounded_Synchronized_Queues;
