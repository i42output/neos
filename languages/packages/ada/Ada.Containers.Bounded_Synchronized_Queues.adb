--  Ada.Containers.Bounded_Synchronized_Queues.adb
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


package body Ada.Containers.Bounded_Synchronized_Queues is

   package body Implementation is

      --  The index in Items of the element at position I (0 = first).
      function Index (Container : List; I : Count_Type) return Count_Type is
        ((Container.First - 1 + I) mod Container.Capacity + 1);

      procedure Append (Container : in out List;
                        New_Item  : Queue_Interfaces.Element_Type)
      is
      begin
         Container.Items (Index (Container, Container.Length)) := New_Item;
         Container.Length := Container.Length + 1;
         if Container.Length > Container.Peak then
            Container.Peak := Container.Length;
         end if;
      end Append;

      procedure Remove_First (Container : in out List;
                              Element   : out Queue_Interfaces.Element_Type)
      is
      begin
         Element := Container.Items (Container.First);
         Container.First := Container.First mod Container.Capacity + 1;
         Container.Length := Container.Length - 1;
      end Remove_First;

   end Implementation;

   protected body Queue is

      entry Enqueue (New_Item : in Queue_Interfaces.Element_Type)
        when Items.Length < Items.Capacity
      is
      begin
         Implementation.Append (Items, New_Item);
      end Enqueue;

      entry Dequeue (Element : out Queue_Interfaces.Element_Type)
        when Items.Length > 0
      is
      begin
         Implementation.Remove_First (Items, Element);
      end Dequeue;

      function Current_Use return Count_Type is
        (Items.Length);

      function Peak_Use return Count_Type is
        (Items.Peak);

   end Queue;

end Ada.Containers.Bounded_Synchronized_Queues;
