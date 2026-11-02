--  Ada.Containers.Doubly_Linked_Lists.adb
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

with Ada.Finalization;
with Ada.Streams;
with Ada.Unchecked_Conversion;
with Ada.Unchecked_Deallocation;

package body Ada.Containers.Doubly_Linked_Lists is

   --  The operations that take the container only through a cursor (or as
   --  an in parameter) still have to record that tampering is prohibited:
   --  the variable view of the container is obtained by converting the
   --  access-to-constant value designating it.
   type Constant_List_Access is access constant List;

   function To_Variable is new Ada.Unchecked_Conversion
     (Constant_List_Access, List_Access);

   function Var (Container : List) return List_Access is
     (To_Variable (Container'Unchecked_Access));

   procedure Free is new Ada.Unchecked_Deallocation
     (Node_Array, Node_Array_Access);
   procedure Free is new Ada.Unchecked_Deallocation
     (Element_Type, Element_Access);

   ---------------------------------------------------------------------
   --  Tampering
   ---------------------------------------------------------------------

   procedure Check_Cursors (Container : List) is
   begin
      if Container.Busy > 0 or else Container.Lock > 0 then
         raise Program_Error with "attempt to tamper with cursors";
      end if;
   end Check_Cursors;

   procedure Check_Elements (Container : List) is
   begin
      null;  --  never prohibited for a definite element type
   end Check_Elements;

   type Busy_Guard (Container : List_Access) is
     new Ada.Finalization.Limited_Controlled with null record;
   overriding procedure Initialize (Guard : in out Busy_Guard);
   overriding procedure Finalize (Guard : in out Busy_Guard);

   type Lock_Guard (Container : List_Access) is
     new Ada.Finalization.Limited_Controlled with null record;
   overriding procedure Initialize (Guard : in out Lock_Guard);
   overriding procedure Finalize (Guard : in out Lock_Guard);

   overriding procedure Initialize (Guard : in out Busy_Guard) is
   begin
      Guard.Container.Busy := Guard.Container.Busy + 1;
   end Initialize;

   overriding procedure Finalize (Guard : in out Busy_Guard) is
   begin
      Guard.Container.Busy := Guard.Container.Busy - 1;
   end Finalize;

   overriding procedure Initialize (Guard : in out Lock_Guard) is
   begin
      Guard.Container.Lock := Guard.Container.Lock + 1;
   end Initialize;

   overriding procedure Finalize (Guard : in out Lock_Guard) is
   begin
      Guard.Container.Lock := Guard.Container.Lock - 1;
   end Finalize;

   overriding procedure Adjust (Control : in out Reference_Control) is
   begin
      if Control.Container /= null then
         Control.Container.Lock := Control.Container.Lock + 1;
      end if;
   end Adjust;

   overriding procedure Finalize (Control : in out Reference_Control) is
   begin
      if Control.Container /= null then
         Control.Container.Lock := Control.Container.Lock - 1;
         Control.Container := null;
      end if;
   end Finalize;

   --  Records that tampering with elements is prohibited (by a reference
   --  object about to be created) and returns the container's access.
   function Lock (Container : List) return List_Access is
      C : constant List_Access := Var (Container);
   begin
      C.Lock := C.Lock + 1;
      return C;
   end Lock;

   ---------------------------------------------------------------------
   --  Checks
   ---------------------------------------------------------------------

   function Valid (Container : List; Node : Count_Type) return Boolean is
     (Node in 1 .. Container.High and then Container.Nodes (Node).Used);

   --  Position designates an element of Container
   procedure Check_Position (Container : List; Position : Cursor) is
   begin
      if Position.Container = null then
         raise Constraint_Error with "cursor has no element";
      elsif Position.Container /= Var (Container)
        or else not Valid (Container, Position.Node)
      then
         raise Program_Error with "cursor designates no element of the list";
      end if;
   end Check_Position;

   --  Before is No_Element or designates an element of Container
   procedure Check_Before (Container : List; Before : Cursor) is
   begin
      if Before.Container /= null
        and then (Before.Container /= Var (Container)
                  or else not Valid (Container, Before.Node))
      then
         raise Program_Error with "cursor designates no element of the list";
      end if;
   end Check_Before;

   procedure Check_Length (Container : List; Count : Count_Type) is
   begin
      if Container.Length > Count_Type'Last - Count then
         raise Constraint_Error with "list would be too long";
      end if;
   end Check_Length;

   ---------------------------------------------------------------------
   --  The node pool
   ---------------------------------------------------------------------

   --  Takes a node from the pool (growing it if need be); the node is
   --  not linked and its element is not set.
   procedure Allocate (Container : in out List; Node : out Count_Type) is
   begin
      if Container.Free /= 0 then
         Node := Container.Free;
         Container.Free := Container.Nodes (Node).Next;
      else
         if Container.Nodes = null
           or else Container.High = Container.Nodes'Length
         then
            declare
               Old : Node_Array_Access := Container.Nodes;
               N   : constant Count_Type :=
                 (if Old = null then 0 else Old'Length);
               New_Length : constant Count_Type :=
                 (if N >= Count_Type'Last / 2 then Count_Type'Last
                  else Count_Type'Max (8, 2 * N));
            begin
               if N = Count_Type'Last then
                  raise Capacity_Error with "list capacity exceeded";
               end if;
               Container.Nodes := new Node_Array (1 .. New_Length);
               if Old /= null then
                  Container.Nodes (1 .. Container.High) :=
                    Old (1 .. Container.High);
                  Free (Old);
               end if;
            end;
         end if;
         Container.High := Container.High + 1;
         Node := Container.High;
      end if;
      Container.Nodes (Node).Next := 0;
      Container.Nodes (Node).Prev := 0;
      Container.Nodes (Node).Used := True;
   end Allocate;

   --  Returns an unlinked node to the pool; the element is finalized
   --  (indefinite and unbounded forms) unless Keep_Element.
   procedure Deallocate (Container    : in out List;
                         Node         : Count_Type;
                         Keep_Element : Boolean := False) is
      N : Node_Type renames Container.Nodes (Node);
   begin
      if not Keep_Element then
         Free (N.Element);
      end if;
      N.Element := null;
      N.Used := False;
      N.Prev := 0;
      N.Next := Container.Free;
      Container.Free := Node;
   end Deallocate;

   --  Allocates a node holding a copy of New_Item (the container is not
   --  modified if the copy fails).
   procedure New_Node (Container : in out List;
                       New_Item  : Element_Type;
                       Node      : out Count_Type) is
      X : Element_Access := new Element_Type'(New_Item);
   begin
      begin
         Allocate (Container, Node);
      exception
         when others =>
            Free (X);
            raise;
      end;
      Container.Nodes (Node).Element := X;
   end New_Node;

   --  Links an unlinked node before Before (0: at the end).
   procedure Link (Container : in out List; Node, Before : Count_Type) is
      P : Count_Type;
   begin
      if Before = 0 then
         P := Container.Last;
         Container.Last := Node;
      else
         P := Container.Nodes (Before).Prev;
         Container.Nodes (Before).Prev := Node;
      end if;
      Container.Nodes (Node).Prev := P;
      Container.Nodes (Node).Next := Before;
      if P = 0 then
         Container.First := Node;
      else
         Container.Nodes (P).Next := Node;
      end if;
      Container.Length := Container.Length + 1;
   end Link;

   procedure Unlink (Container : in out List; Node : Count_Type) is
      P : constant Count_Type := Container.Nodes (Node).Prev;
      N : constant Count_Type := Container.Nodes (Node).Next;
   begin
      if P = 0 then
         Container.First := N;
      else
         Container.Nodes (P).Next := N;
      end if;
      if N = 0 then
         Container.Last := P;
      else
         Container.Nodes (N).Prev := P;
      end if;
      Container.Nodes (Node).Next := 0;
      Container.Nodes (Node).Prev := 0;
      Container.Length := Container.Length - 1;
   end Unlink;

   --  Moves the node Node of Source before Before of Target (different
   --  lists); returns the node in Target.  Target must have room.
   procedure Transfer (Target : in out List;
                       Before : Count_Type;
                       Source : in out List;
                       Node   : Count_Type;
                       Result : out Count_Type) is
   begin
      Allocate (Target, Result);
      Target.Nodes (Result).Element := Source.Nodes (Node).Element;
      Unlink (Source, Node);
      Deallocate (Source, Node, Keep_Element => True);
      Link (Target, Result, Before);
   end Transfer;

   --  Removes all the nodes and resets the pool.
   procedure Clear_All (Container : in out List) is
   begin
      if Container.Nodes /= null then
         for N in 1 .. Container.High loop
            Free (Container.Nodes (N).Element);
            Container.Nodes (N).Used := False;
         end loop;
      end if;
      Container.First := 0;
      Container.Last := 0;
      Container.Length := 0;
      Container.Free := 0;
      Container.High := 0;
   end Clear_All;

   --  Appends a copy of each element of Source to Target.
   procedure Append_All (Target : in out List; Source : List) is
      N    : Count_Type := Source.First;
      Node : Count_Type;
   begin
      while N /= 0 loop
         New_Node (Target, Source.Nodes (N).Element.all, Node);
         Link (Target, Node, 0);
         N := Source.Nodes (N).Next;
      end loop;
   end Append_All;

   ---------------------------------------------------------------------
   --  Controlled operations
   ---------------------------------------------------------------------

   --  Makes a deep copy, with the nodes compacted in list order.
   overriding procedure Adjust (Container : in out List) is
      Source : constant Node_Array_Access := Container.Nodes;
      L      : constant Count_Type := Container.Length;
      N      : Count_Type := Container.First;
      J      : Count_Type := 0;
   begin
      Container.Nodes := null;
      Container.First := 0;
      Container.Last := 0;
      Container.Length := 0;
      Container.Free := 0;
      Container.High := 0;
      Container.Busy := 0;
      Container.Lock := 0;
      if L = 0 then
         return;
      end if;
      Container.Nodes := new Node_Array (1 .. L);
      while N /= 0 loop
         J := J + 1;
         Container.Nodes (J) :=
           (Element => new Element_Type'(Source (N).Element.all),
            Next    => 0,
            Prev    => J - 1,
            Used    => True);
         if J > 1 then
            Container.Nodes (J - 1).Next := J;
         end if;
         Container.First := 1;
         Container.Last := J;
         Container.Length := J;
         Container.High := J;
         N := Source (N).Next;
      end loop;
   end Adjust;

   overriding procedure Finalize (Container : in out List) is
      Prohibited : constant Boolean :=
        Container.Busy > 0 or else Container.Lock > 0;
   begin
      --  (while tampering is prohibited (e.g. the target of an assignment):
      --  Program_Error, the container left unchanged)
      if Prohibited then
         raise Program_Error with "list finalized while tampering is prohibited";
      end if;
      Clear_All (Container);
      Free (Container.Nodes);
   end Finalize;

   ---------------------------------------------------------------------
   --  Streams
   ---------------------------------------------------------------------

   procedure Write
     (Stream    : not null access Ada.Streams.Root_Stream_Type'Class;
      Container : List)
   is
      N : Count_Type := Container.First;
   begin
      Count_Type'Base'Write (Stream, Container.Length);
      while N /= 0 loop
         Element_Type'Write (Stream, Container.Nodes (N).Element.all);
         N := Container.Nodes (N).Next;
      end loop;
   end Write;

   procedure Read
     (Stream    : not null access Ada.Streams.Root_Stream_Type'Class;
      Container : out List)
   is
      N    : Count_Type'Base;
      Node : Count_Type;
   begin
      Clear (Container);
      Count_Type'Base'Read (Stream, N);
      if N < 0 then
         raise Constraint_Error with "invalid list length in stream";
      end if;
      Check_Length (Container, N);
      for J in 1 .. N loop
         declare
            X : Element_Access := new Element_Type;
         begin
            Element_Type'Read (Stream, X.all);
            Allocate (Container, Node);
            Container.Nodes (Node).Element := X;
         exception
            when others =>
               Free (X);
               raise;
         end;
         Link (Container, Node, 0);
      end loop;
   end Read;

   procedure Write
     (Stream   : not null access Ada.Streams.Root_Stream_Type'Class;
      Position : Cursor) is
   begin
      raise Program_Error with "attempt to stream a list cursor";
   end Write;

   procedure Read
     (Stream   : not null access Ada.Streams.Root_Stream_Type'Class;
      Position : out Cursor) is
   begin
      raise Program_Error with "attempt to stream a list cursor";
   end Read;

   procedure Write
     (Stream : not null access Ada.Streams.Root_Stream_Type'Class;
      Item   : Constant_Reference_Type) is
   begin
      raise Program_Error with "attempt to stream a reference";
   end Write;

   procedure Read
     (Stream : not null access Ada.Streams.Root_Stream_Type'Class;
      Item   : out Constant_Reference_Type) is
   begin
      raise Program_Error with "attempt to stream a reference";
   end Read;

   procedure Write
     (Stream : not null access Ada.Streams.Root_Stream_Type'Class;
      Item   : Reference_Type) is
   begin
      raise Program_Error with "attempt to stream a reference";
   end Write;

   procedure Read
     (Stream : not null access Ada.Streams.Root_Stream_Type'Class;
      Item   : out Reference_Type) is
   begin
      raise Program_Error with "attempt to stream a reference";
   end Read;

   ---------------------------------------------------------------------
   --  Iterators
   ---------------------------------------------------------------------

   Max_Split : constant := 64;
   type Chunk_Starts is array (1 .. Max_Split + 1) of Count_Type;

   type Iterator is new Ada.Finalization.Limited_Controlled
     and List_Iterator_Interfaces.Parallel_Reversible_Iterator with
   record
      Container : List_Access;
      Start     : Count_Type := 0;     --  0: the whole list
      Chunks    : Natural := 0;        --  0: not split
      Starts    : Chunk_Starts := (others => 0);
   end record;

   overriding procedure Finalize (Object : in out Iterator);

   overriding function First (Object : Iterator) return Cursor;
   overriding function Next
     (Object : Iterator; Position : Cursor) return Cursor;
   overriding function Last (Object : Iterator) return Cursor;
   overriding function Previous
     (Object : Iterator; Position : Cursor) return Cursor;
   overriding function Is_Split (Object : Iterator) return Boolean;
   overriding procedure Split_Into_Chunks
     (Object     : in out Iterator;
      Max_Chunks : List_Iterator_Interfaces.Chunk_Index);
   overriding function Chunk_Count
     (Object : Iterator) return List_Iterator_Interfaces.Chunk_Index;
   overriding function First
     (Object : Iterator; Chunk : List_Iterator_Interfaces.Chunk_Index)
      return Cursor;
   overriding function Next
     (Object   : Iterator;
      Position : Cursor;
      Chunk    : List_Iterator_Interfaces.Chunk_Index) return Cursor;

   overriding procedure Finalize (Object : in out Iterator) is
   begin
      if Object.Container /= null then
         Object.Container.Busy := Object.Container.Busy - 1;
         Object.Container := null;
      end if;
   end Finalize;

   function Make_Cursor (Container : List_Access; Node : Count_Type)
      return Cursor is
     (if Node = 0 then No_Element else (Container, Node));

   overriding function First (Object : Iterator) return Cursor is
     (Make_Cursor (Object.Container,
                   (if Object.Start /= 0 then Object.Start
                    else Object.Container.First)));

   procedure Check_Iterator_Cursor (Object : Iterator; Position : Cursor) is
   begin
      if Position.Container /= null
        and then Position.Container /= Object.Container
      then
         raise Program_Error with "cursor designates another list";
      end if;
   end Check_Iterator_Cursor;

   overriding function Next
     (Object : Iterator; Position : Cursor) return Cursor is
   begin
      Check_Iterator_Cursor (Object, Position);
      return Next (Position);
   end Next;

   overriding function Last (Object : Iterator) return Cursor is
     (Make_Cursor (Object.Container,
                   (if Object.Start /= 0 then Object.Start
                    else Object.Container.Last)));

   overriding function Previous
     (Object : Iterator; Position : Cursor) return Cursor is
   begin
      Check_Iterator_Cursor (Object, Position);
      return Previous (Position);
   end Previous;

   overriding function Is_Split (Object : Iterator) return Boolean is
     (Object.Chunks > 0);

   overriding procedure Split_Into_Chunks
     (Object     : in out Iterator;
      Max_Chunks : List_Iterator_Interfaces.Chunk_Index)
   is
      C    : constant List_Access := Object.Container;
      K    : constant Count_Type :=
        Count_Type'Max (1, Count_Type'Min
          (C.Length, Count_Type'Min (Count_Type (Max_Chunks),
                                     Max_Split)));
      Size : constant Count_Type := (C.Length + K - 1) / K;
      N    : Count_Type := C.First;
      I    : Count_Type := 0;
   begin
      if Object.Chunks > 0 then
         raise Program_Error with "iterator already split";
      end if;
      Object.Chunks := 0;
      while N /= 0 loop
         if I mod Size = 0 then
            Object.Chunks := Object.Chunks + 1;
            Object.Starts (Object.Chunks) := N;
         end if;
         I := I + 1;
         N := C.Nodes (N).Next;
      end loop;
      if Object.Chunks = 0 then
         Object.Chunks := 1;
         Object.Starts (1) := 0;
      end if;
      Object.Starts (Object.Chunks + 1) := 0;
   end Split_Into_Chunks;

   procedure Check_Chunk (Object : Iterator; Chunk : Positive) is
   begin
      if Object.Chunks = 0 or else Chunk > Object.Chunks then
         raise Program_Error with "iterator not split or no such chunk";
      end if;
   end Check_Chunk;

   overriding function Chunk_Count
     (Object : Iterator) return List_Iterator_Interfaces.Chunk_Index is
   begin
      if Object.Chunks = 0 then
         raise Program_Error with "iterator not split";
      end if;
      return Object.Chunks;
   end Chunk_Count;

   overriding function First
     (Object : Iterator; Chunk : List_Iterator_Interfaces.Chunk_Index)
      return Cursor is
   begin
      Check_Chunk (Object, Chunk);
      return Make_Cursor (Object.Container, Object.Starts (Chunk));
   end First;

   overriding function Next
     (Object   : Iterator;
      Position : Cursor;
      Chunk    : List_Iterator_Interfaces.Chunk_Index) return Cursor
   is
      N : Count_Type;
   begin
      Check_Chunk (Object, Chunk);
      Check_Iterator_Cursor (Object, Position);
      if Position.Container = null then
         return No_Element;
      end if;
      N := Object.Container.Nodes (Position.Node).Next;
      if N = Object.Starts (Chunk + 1) then
         return No_Element;
      end if;
      return Make_Cursor (Object.Container, N);
   end Next;

   ---------------------------------------------------------------------
   --  Visible operations
   ---------------------------------------------------------------------

   function Has_Element (Position : Cursor) return Boolean is
     (Position.Container /= null
      and then Valid (Position.Container.all, Position.Node));

   function Has_Element (Container : List; Position : Cursor)
      return Boolean is
     (Position.Container = Var (Container)
      and then Valid (Container, Position.Node));

   function "=" (Left, Right : List) return Boolean is
      L : Count_Type := Left.First;
      R : Count_Type := Right.First;
   begin
      if Var (Left) = Var (Right) then
         return True;
      elsif Left.Length /= Right.Length then
         return False;
      end if;
      while L /= 0 loop
         if not (Left.Nodes (L).Element.all = Right.Nodes (R).Element.all) then
            return False;
         end if;
         L := Left.Nodes (L).Next;
         R := Right.Nodes (R).Next;
      end loop;
      return True;
   end "=";

   function Same_Object (Left, Right : List) return Boolean is
     (Var (Left) = Var (Right));

   function Tampering_With_Cursors_Prohibited
      (Container : List) return Boolean is
     (Container.Busy > 0 or else Container.Lock > 0);

   function Tampering_With_Elements_Prohibited
      (Container : List) return Boolean is
     (False);

   function Length (Container : List) return Count_Type is
     (Container.Length);

   function Is_Empty (Container : List) return Boolean is
     (Container.Length = 0);

   procedure Clear (Container : in out List) is
   begin
      Check_Cursors (Container);
      Clear_All (Container);
   end Clear;

   function Element (Position : Cursor) return Element_Type is
   begin
      if not Has_Element (Position) then
         raise Constraint_Error with "cursor has no element";
      end if;
      return Position.Container.Nodes (Position.Node).Element.all;
   end Element;

   function Element (Container : List;
                     Position  : Cursor) return Element_Type is
   begin
      Check_Position (Container, Position);
      return Container.Nodes (Position.Node).Element.all;
   end Element;

   procedure Replace_Element (Container : in out List;
                              Position  : in     Cursor;
                              New_item  : in     Element_Type) is
   begin
      Check_Elements (Container);
      Check_Position (Container, Position);
      declare
         N : Node_Type renames Container.Nodes (Position.Node);
         X : constant Element_Access := new Element_Type'(New_item);
      begin
         Free (N.Element);
         N.Element := X;
      end;
   end Replace_Element;

   procedure Query_Element
     (Position : in Cursor;
      Process  : not null access procedure (Element : in Element_Type)) is
   begin
      if not Has_Element (Position) then
         raise Constraint_Error with "cursor has no element";
      end if;
      declare
         Guard : Lock_Guard (Position.Container);
      begin
         Process (Position.Container.Nodes (Position.Node).Element.all);
      end;
   end Query_Element;

   procedure Query_Element
     (Container : in List;
      Position  : in Cursor;
      Process   : not null access procedure (Element : in Element_Type)) is
   begin
      Check_Position (Container, Position);
      declare
         Guard : Lock_Guard (Var (Container));
      begin
         Process (Container.Nodes (Position.Node).Element.all);
      end;
   end Query_Element;

   procedure Update_Element
     (Container : in out List;
      Position  : in     Cursor;
      Process   : not null access procedure
                      (Element : in out Element_Type)) is
   begin
      Check_Position (Container, Position);
      declare
         Guard : Lock_Guard (Var (Container));
      begin
         Process (Container.Nodes (Position.Node).Element.all);
      end;
   end Update_Element;

   function Constant_Reference (Container : aliased in List;
                                Position  : in Cursor)
      return Constant_Reference_Type is
   begin
      Check_Position (Container, Position);
      return (Element => Container.Nodes (Position.Node).Element,
              Control => (Ada.Finalization.Controlled with
                          Container => Lock (Container)));
   end Constant_Reference;

   function Reference (Container : aliased in out List;
                       Position  : in Cursor)
      return Reference_Type is
   begin
      Check_Position (Container, Position);
      return (Element => Container.Nodes (Position.Node).Element,
              Control => (Ada.Finalization.Controlled with
                          Container => Lock (Container)));
   end Reference;

   procedure Assign (Target : in out List; Source : in List) is
   begin
      if Var (Target) = Var (Source) then
         return;
      end if;
      Check_Cursors (Target);
      Clear_All (Target);
      Append_All (Target, Source);
   end Assign;

   function Copy (Source : List)
      return List is
   begin
      return Result : List do
         Append_All (Result, Source);
      end return;
   end Copy;

   procedure Move (Target : in out List;
                   Source : in out List) is
   begin
      if Var (Target) = Var (Source) then
         return;
      end if;
      Check_Cursors (Target);
      Check_Cursors (Source);
      Clear_All (Target);
      declare
         Nodes : constant Node_Array_Access := Target.Nodes;
      begin
         Target.Nodes := Source.Nodes;
         Target.First := Source.First;
         Target.Last := Source.Last;
         Target.Length := Source.Length;
         Target.Free := Source.Free;
         Target.High := Source.High;
         Source.Nodes := Nodes;
         Source.First := 0;
         Source.Last := 0;
         Source.Length := 0;
         Source.Free := 0;
         Source.High := 0;
      end;
   end Move;

   --  Inserts Count copies of New_Item before Before; Node is the first
   --  new node (0 if Count = 0).
   procedure Insert_Copies (Container : in out List;
                            Before    : Count_Type;
                            New_Item  : Element_Type;
                            Count     : Count_Type;
                            Node      : out Count_Type) is
      N : Count_Type;
   begin
      Node := 0;
      for J in 1 .. Count loop
         New_Node (Container, New_Item, N);
         Link (Container, N, Before);
         if J = 1 then
            Node := N;
         end if;
      end loop;
   end Insert_Copies;

   procedure Insert (Container : in out List;
                     Before    : in     Cursor;
                     New_Item  : in     Element_Type;
                     Count     : in     Count_Type := 1)
   is
      N : Count_Type;
   begin
      Check_Cursors (Container);
      Check_Before (Container, Before);
      Check_Length (Container, Count);
      Insert_Copies (Container, Before.Node, New_Item, Count, N);
   end Insert;

   procedure Insert (Container : in out List;
                     Before    : in     Cursor;
                     New_Item  : in     Element_Type;
                     Position  :    out Cursor;
                     Count     : in     Count_Type := 1)
   is
      N : Count_Type;
   begin
      Check_Cursors (Container);
      Check_Before (Container, Before);
      Check_Length (Container, Count);
      Insert_Copies (Container, Before.Node, New_Item, Count, N);
      Position := (if N = 0 then Before else (Var (Container), N));
   end Insert;

   procedure Insert (Container : in out List;
                     Before    : in     Cursor;
                     Position  :    out Cursor;
                     Count     : in     Count_Type := 1)
   is
      N, First : Count_Type := 0;
   begin
      Check_Cursors (Container);
      Check_Before (Container, Before);
      Check_Length (Container, Count);
      for J in 1 .. Count loop
         declare
            X : Element_Access := new Element_Type;
         begin
            Allocate (Container, N);
            Container.Nodes (N).Element := X;
         exception
            when others =>
               Free (X);
               raise;
         end;
         Link (Container, N, Before.Node);
         if J = 1 then
            First := N;
         end if;
      end loop;
      Position := (if First = 0 then Before else (Var (Container), First));
   end Insert;

   procedure Prepend (Container : in out List;
                      New_Item  : in     Element_Type;
                      Count     : in     Count_Type := 1)
   is
      N : Count_Type;
   begin
      Check_Cursors (Container);
      Check_Length (Container, Count);
      Insert_Copies (Container, Container.First, New_Item, Count, N);
   end Prepend;

   procedure Append (Container : in out List;
                     New_Item  : in     Element_Type) is
   begin
      Append (Container, New_Item, 1);
   end Append;

   procedure Append (Container : in out List;
                     New_Item  : in     Element_Type;
                     Count     : in     Count_Type)
   is
      N : Count_Type;
   begin
      Check_Cursors (Container);
      Check_Length (Container, Count);
      Insert_Copies (Container, 0, New_Item, Count, N);
   end Append;

   procedure Delete (Container : in out List;
                     Position  : in out Cursor;
                     Count     : in     Count_Type := 1)
   is
      N, Next : Count_Type;
   begin
      Check_Cursors (Container);
      Check_Position (Container, Position);
      N := Position.Node;
      for J in 1 .. Count loop
         exit when N = 0;
         Next := Container.Nodes (N).Next;
         Unlink (Container, N);
         Deallocate (Container, N);
         N := Next;
      end loop;
      Position := No_Element;
   end Delete;

   procedure Delete_First (Container : in out List;
                           Count     : in     Count_Type := 1)
   is
      N : Count_Type;
   begin
      Check_Cursors (Container);
      if Count >= Container.Length then
         Clear_All (Container);
         return;
      end if;
      for J in 1 .. Count loop
         N := Container.First;
         Unlink (Container, N);
         Deallocate (Container, N);
      end loop;
   end Delete_First;

   procedure Delete_Last (Container : in out List;
                          Count     : in     Count_Type := 1)
   is
      N : Count_Type;
   begin
      Check_Cursors (Container);
      if Count >= Container.Length then
         Clear_All (Container);
         return;
      end if;
      for J in 1 .. Count loop
         N := Container.Last;
         Unlink (Container, N);
         Deallocate (Container, N);
      end loop;
   end Delete_Last;

   procedure Reverse_Elements (Container : in out List) is
      N, T : Count_Type;
   begin
      Check_Cursors (Container);
      N := Container.First;
      while N /= 0 loop
         T := Container.Nodes (N).Next;
         Container.Nodes (N).Next := Container.Nodes (N).Prev;
         Container.Nodes (N).Prev := T;
         N := T;
      end loop;
      T := Container.First;
      Container.First := Container.Last;
      Container.Last := T;
   end Reverse_Elements;

   procedure Check_Two (Container : List; I, J : Cursor) is
   begin
      if I.Container = null or else J.Container = null then
         raise Constraint_Error with "cursor has no element";
      end if;
      Check_Position (Container, I);
      Check_Position (Container, J);
   end Check_Two;

   procedure Swap (Container : in out List;
                   I, J      : in     Cursor) is
   begin
      Check_Elements (Container);
      Check_Two (Container, I, J);
      if I.Node /= J.Node then
         declare
            T : constant Element_Access := Container.Nodes (I.Node).Element;
         begin
            Container.Nodes (I.Node).Element := Container.Nodes (J.Node).Element;
            Container.Nodes (J.Node).Element := T;
         end;
      end if;
   end Swap;

   procedure Swap_Links (Container : in out List;
                         I, J      : in     Cursor)
   is
      A, B, A_Next, B_Next : Count_Type;
   begin
      Check_Elements (Container);
      Check_Two (Container, I, J);
      A := I.Node;
      B := J.Node;
      if A = B then
         return;
      end if;
      A_Next := Container.Nodes (A).Next;
      B_Next := Container.Nodes (B).Next;
      if A_Next = B then
         Unlink (Container, B);
         Link (Container, B, A);
      elsif B_Next = A then
         Unlink (Container, A);
         Link (Container, A, B);
      else
         Unlink (Container, A);
         Link (Container, A, B_Next);
         Unlink (Container, B);
         Link (Container, B, A_Next);
      end if;
   end Swap_Links;

   procedure Splice (Target   : in out List;
                     Before   : in     Cursor;
                     Source   : in out List)
   is
      N : Count_Type;
   begin
      Check_Cursors (Target);
      Check_Cursors (Source);
      Check_Before (Target, Before);
      if Var (Target) = Var (Source) then
         return;
      end if;
      Check_Length (Target, Source.Length);
      if Target.Length = 0 then
         Move (Target, Source);
         return;
      end if;
      while Source.First /= 0 loop
         Transfer (Target, Before.Node, Source, Source.First, N);
      end loop;
   end Splice;

   procedure Splice (Target   : in out List;
                     Before   : in     Cursor;
                     Source   : in out List;
                     Position : in out Cursor)
   is
      N : Count_Type;
   begin
      Check_Cursors (Target);
      Check_Cursors (Source);
      Check_Position (Source, Position);
      Check_Before (Target, Before);
      if Var (Target) = Var (Source) then
         Splice (Target, Before, Position);
         return;
      end if;
      Check_Length (Target, 1);
      Transfer (Target, Before.Node, Source, Position.Node, N);
      Position := (Var (Target), N);
   end Splice;

   procedure Splice (Container: in out List;
                     Before   : in     Cursor;
                     Position : in     Cursor) is
   begin
      Check_Cursors (Container);
      Check_Position (Container, Position);
      Check_Before (Container, Before);
      if Position.Node = Before.Node
        or else Container.Nodes (Position.Node).Next = Before.Node
      then
         return;
      end if;
      Unlink (Container, Position.Node);
      Link (Container, Position.Node, Before.Node);
   end Splice;

   function First (Container : List) return Cursor is
     (Make_Cursor (Var (Container), Container.First));

   function First_Element (Container : List)
      return Element_Type is
   begin
      if Container.Length = 0 then
         raise Constraint_Error with "list is empty";
      end if;
      return Container.Nodes (Container.First).Element.all;
   end First_Element;

   function Last (Container : List) return Cursor is
     (Make_Cursor (Var (Container), Container.Last));

   function Last_Element (Container : List)
      return Element_Type is
   begin
      if Container.Length = 0 then
         raise Constraint_Error with "list is empty";
      end if;
      return Container.Nodes (Container.Last).Element.all;
   end Last_Element;

   function Next (Position : Cursor) return Cursor is
   begin
      if not Has_Element (Position) then
         return No_Element;
      end if;
      return Make_Cursor (Position.Container,
                          Position.Container.Nodes (Position.Node).Next);
   end Next;

   function Next (Container : List;
                  Position : Cursor) return Cursor is
   begin
      if Position.Container = null then
         return No_Element;
      end if;
      Check_Position (Container, Position);
      return Next (Position);
   end Next;

   procedure Next (Position : in out Cursor) is
   begin
      Position := Next (Position);
   end Next;

   procedure Next (Container : in     List;
                   Position  : in out Cursor) is
   begin
      Position := Next (Container, Position);
   end Next;

   function Previous (Position : Cursor) return Cursor is
   begin
      if not Has_Element (Position) then
         return No_Element;
      end if;
      return Make_Cursor (Position.Container,
                          Position.Container.Nodes (Position.Node).Prev);
   end Previous;

   function Previous (Container : List;
                      Position  : Cursor) return Cursor is
   begin
      if Position.Container = null then
         return No_Element;
      end if;
      Check_Position (Container, Position);
      return Previous (Position);
   end Previous;

   procedure Previous (Position : in out Cursor) is
   begin
      Position := Previous (Position);
   end Previous;

   procedure Previous (Container : in     List;
                       Position  : in out Cursor) is
   begin
      Position := Previous (Container, Position);
   end Previous;

   function Find (Container : List;
                  Item      : Element_Type;
                  Position  : Cursor := No_Element)
      return Cursor
   is
      N : Count_Type;
   begin
      if Position.Container /= null then
         Check_Position (Container, Position);
         N := Position.Node;
      else
         N := Container.First;
      end if;
      declare
         Guard : Lock_Guard (Var (Container));
      begin
         while N /= 0 loop
            if Container.Nodes (N).Element.all = Item then
               return (Var (Container), N);
            end if;
            N := Container.Nodes (N).Next;
         end loop;
      end;
      return No_Element;
   end Find;

   function Reverse_Find (Container : List;
                          Item      : Element_Type;
                          Position  : Cursor := No_Element)
      return Cursor
   is
      N : Count_Type;
   begin
      if Position.Container /= null then
         Check_Position (Container, Position);
         N := Position.Node;
      else
         N := Container.Last;
      end if;
      declare
         Guard : Lock_Guard (Var (Container));
      begin
         while N /= 0 loop
            if Container.Nodes (N).Element.all = Item then
               return (Var (Container), N);
            end if;
            N := Container.Nodes (N).Prev;
         end loop;
      end;
      return No_Element;
   end Reverse_Find;

   function Contains (Container : List;
                      Item      : Element_Type) return Boolean is
     (Find (Container, Item) /= No_Element);

   procedure  Iterate
     (Container : in List;
      Process   : not null access procedure (Position : in Cursor))
   is
      C     : constant List_Access := Var (Container);
      Guard : Busy_Guard (C);
      N     : Count_Type := Container.First;
   begin
      while N /= 0 loop
         Process ((C, N));
         N := Container.Nodes (N).Next;
      end loop;
   end Iterate;

   procedure Reverse_Iterate
     (Container : in List;
      Process   : not null access procedure (Position : in Cursor))
   is
      C     : constant List_Access := Var (Container);
      Guard : Busy_Guard (C);
      N     : Count_Type := Container.Last;
   begin
      while N /= 0 loop
         Process ((C, N));
         N := Container.Nodes (N).Prev;
      end loop;
   end Reverse_Iterate;

   function Iterate (Container : in List)
      return List_Iterator_Interfaces.Parallel_Reversible_Iterator'Class
   is
      C : constant List_Access := Var (Container);
   begin
      return It : Iterator do
         It.Container := C;
         C.Busy := C.Busy + 1;
      end return;
   end Iterate;

   function Iterate (Container : in List; Start : in Cursor)
      return List_Iterator_Interfaces.Reversible_Iterator'Class
   is
      C : constant List_Access := Var (Container);
   begin
      Check_Position (Container, Start);
      return It : Iterator do
         It.Container := C;
         It.Start := Start.Node;
         C.Busy := C.Busy + 1;
      end return;
   end Iterate;

   package body Generic_Sorting is

      function Is_Sorted (Container : List) return Boolean is
         N : Count_Type := Container.First;
         M : Count_Type;
      begin
         if N = 0 then
            return True;
         end if;
         loop
            M := Container.Nodes (N).Next;
            exit when M = 0;
            if Container.Nodes (M).Element.all < Container.Nodes (N).Element.all then
               return False;
            end if;
            N := M;
         end loop;
         return True;
      end Is_Sorted;

      procedure Sort (Container : in out List) is

         --  A stable merge sort of the chain of Length nodes starting at
         --  Head (linked through Next); returns the head of the sorted
         --  chain, whose last node has Next = 0.
         function Merge_Sort (Head : Count_Type; Length : Count_Type)
            return Count_Type
         is
            Left, Right, Mid, Tail, Result : Count_Type;
         begin
            if Length <= 1 then
               if Head /= 0 then
                  Container.Nodes (Head).Next := 0;
               end if;
               return Head;
            end if;
            Mid := Head;
            for J in 2 .. Length / 2 loop
               Mid := Container.Nodes (Mid).Next;
            end loop;
            Right := Container.Nodes (Mid).Next;
            Left := Merge_Sort (Head, Length / 2);
            Right := Merge_Sort (Right, Length - Length / 2);
            Result := 0;
            Tail := 0;
            while Left /= 0 or else Right /= 0 loop
               declare
                  Take : Count_Type;
               begin
                  if Left = 0 then
                     Take := Right;
                     Right := Container.Nodes (Right).Next;
                  elsif Right = 0 then
                     Take := Left;
                     Left := Container.Nodes (Left).Next;
                  elsif Container.Nodes (Right).Element.all <
                        Container.Nodes (Left).Element.all
                  then
                     Take := Right;
                     Right := Container.Nodes (Right).Next;
                  else
                     Take := Left;
                     Left := Container.Nodes (Left).Next;
                  end if;
                  if Tail = 0 then
                     Result := Take;
                  else
                     Container.Nodes (Tail).Next := Take;
                  end if;
                  Tail := Take;
               end;
            end loop;
            Container.Nodes (Tail).Next := 0;
            return Result;
         end Merge_Sort;

         N, P : Count_Type;
      begin
         Check_Cursors (Container);
         if Container.Length <= 1 then
            return;
         end if;
         declare
            Guard : Lock_Guard (Var (Container));
         begin
            Container.First := Merge_Sort (Container.First, Container.Length);
         end;
         --  Rebuild the back links.
         P := 0;
         N := Container.First;
         while N /= 0 loop
            Container.Nodes (N).Prev := P;
            P := N;
            N := Container.Nodes (N).Next;
         end loop;
         Container.Last := P;
      end Sort;

      procedure Merge (Target  : in out List;
                       Source  : in out List)
      is
         T, S, N : Count_Type;
      begin
         Check_Cursors (Target);
         Check_Cursors (Source);
         if Source.Length = 0 then
            return;
         elsif Var (Target) = Var (Source) then
            raise Program_Error with "Merge of a nonempty container with itself";
         end if;
         Check_Length (Target, Source.Length);
         T := Target.First;
         while Source.First /= 0 loop
            S := Source.First;
            while T /= 0
              and then not (Source.Nodes (S).Element.all <
                            Target.Nodes (T).Element.all)
            loop
               T := Target.Nodes (T).Next;
            end loop;
            Transfer (Target, T, Source, S, N);
         end loop;
      end Merge;

   end Generic_Sorting;

end Ada.Containers.Doubly_Linked_Lists;
