--  Ada.Containers.Hashed_Sets.adb
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

package body Ada.Containers.Hashed_Sets is

   --  The operations that take the container only through a cursor (or as
   --  an in parameter) still have to record that tampering is prohibited:
   --  the variable view of the container is obtained by converting the
   --  access-to-constant value designating it.
   type Constant_Set_Access is access constant Set;

   function To_Variable is new Ada.Unchecked_Conversion
     (Constant_Set_Access, Set_Access);

   function Var (Container : Set) return Set_Access is
     (To_Variable (Container'Unchecked_Access));

   procedure Free is new Ada.Unchecked_Deallocation
     (Node_Array, Node_Array_Access);
   procedure Free is new Ada.Unchecked_Deallocation
     (Element_Type, Element_Access);

   ---------------------------------------------------------------------
   --  Tampering
   ---------------------------------------------------------------------

   procedure Check_Cursors (Container : Set) is
   begin
      if Container.Busy > 0 or else Container.Lock > 0 then
         raise Program_Error with "attempt to tamper with cursors";
      end if;
   end Check_Cursors;

   type Busy_Guard (Container : Set_Access) is
     new Ada.Finalization.Limited_Controlled with null record;
   overriding procedure Initialize (Guard : in out Busy_Guard);
   overriding procedure Finalize (Guard : in out Busy_Guard);

   type Lock_Guard (Container : Set_Access) is
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
   function Lock (Container : Set) return Set_Access is
      C : constant Set_Access := Var (Container);
   begin
      C.Lock := C.Lock + 1;
      return C;
   end Lock;

   ---------------------------------------------------------------------
   --  Checks
   ---------------------------------------------------------------------

   function Valid (Container : Set; Node : Count_Type) return Boolean is
     (Node in 1 .. Container.High and then Container.Nodes (Node).Used);

   --  Position designates an element of Container
   procedure Check_Position (Container : Set; Position : Cursor) is
   begin
      if Position.Container = null then
         raise Constraint_Error with "cursor has no element";
      elsif Position.Container /= Var (Container)
        or else not Valid (Container, Position.Node)
      then
         raise Program_Error with "cursor designates no element of the set";
      end if;
   end Check_Position;

   procedure Check_Has_Element (Position : Cursor) is
   begin
      if Position.Container = null
        or else not Valid (Position.Container.all, Position.Node)
      then
         raise Constraint_Error with "cursor has no element";
      end if;
   end Check_Has_Element;

   procedure Free is new Ada.Unchecked_Deallocation
     (Bucket_Array, Bucket_Array_Access);

   ---------------------------------------------------------------------
   --  Hash table
   ---------------------------------------------------------------------

   --  The smallest of a table of primes (each about twice the last) at
   --  least N, else the smallest prime at least N.
   Primes : constant array (Positive range <>) of Hash_Type :=
     (53, 97, 193, 389, 769, 1543, 3079, 6151, 12289, 24593, 49157, 98317,
      196613, 393241, 786433, 1572869, 3145739, 6291469, 12582917, 25165843,
      50331653, 100663319, 201326611, 402653189, 805306457, 1610612741,
      3221225473, 4294967291);

   function Prime_At_Least (N : Hash_Type) return Hash_Type is
      P : Hash_Type := Hash_Type'Max (N, 3);
      D : Hash_Type;
      Is_Prime : Boolean;
   begin
      for Prime of Primes loop
         if Prime >= N then
            return Prime;
         end if;
      end loop;
      if P mod 2 = 0 then
         P := P + 1;
      end if;
      loop
         Is_Prime := True;
         D := 3;
         while D <= P / D loop
            if P mod D = 0 then
               Is_Prime := False;
               exit;
            end if;
            D := D + 2;
         end loop;
         exit when Is_Prime;
         P := P + 2;
      end loop;
      return P;
   end Prime_At_Least;

   function Bucket_Count (Container : Set) return Hash_Type is
     (if Container.Buckets = null then 0 else Container.Buckets'Length);

   function Bucket (Container : Set; H : Hash_Type) return Hash_Type is
     (H mod Bucket_Count (Container) + 1);

   --  The node with a key equivalent to Key (whose hash is H), or 0.
   function Find_Node (Container : Set; Key : Element_Type; H : Hash_Type)
      return Count_Type
   is
      N : Count_Type;
   begin
      if Container.Length = 0 then
         return 0;
      end if;
      N := Container.Buckets (Bucket (Container, H));
      while N /= 0 loop
         if Container.Nodes (N).Hash = H
           and then Equivalent_Elements (Container.Nodes (N).Element.all, Key)
         then
            return N;
         end if;
         N := Container.Nodes (N).Next;
      end loop;
      return 0;
   end Find_Node;

   function Find_Node (Container : Set; Key : Element_Type) return Count_Type is
     (if Container.Length = 0 then 0
      else Find_Node (Container, Key, Hash (Key)));

   --  Replaces the bucket array by one of Count buckets.
   procedure Rehash (Container : in out Set; Count : Hash_Type) is
      New_Buckets : constant Bucket_Array_Access :=
        new Bucket_Array'(1 .. Count => 0);
      B : Hash_Type;
   begin
      for N in 1 .. Container.High loop
         if Container.Nodes (N).Used then
            B := Container.Nodes (N).Hash mod Count + 1;
            Container.Nodes (N).Next := New_Buckets (B);
            New_Buckets (B) := N;
         end if;
      end loop;
      Free (Container.Buckets);
      Container.Buckets := New_Buckets;
   end Rehash;

   --  Ensures that the node pool has at least N nodes.
   procedure Reserve_Nodes (Container : in out Set; N : Count_Type) is
      Old : Node_Array_Access := Container.Nodes;
   begin
      if Old /= null and then Old'Length >= N then
         return;
      end if;
      Container.Nodes := new Node_Array (1 .. N);
      if Old /= null then
         Container.Nodes (1 .. Container.High) := Old (1 .. Container.High);
         Free (Old);
      end if;
   end Reserve_Nodes;

   --  Makes room for one more node (growing the table and the pool if
   --  need be).
   procedure Make_Room (Container : in out Set) is
   begin
      if Container.Length = Count_Type'Last then
         raise Constraint_Error with "set would be too long";
      end if;
      if Hash_Type (Container.Length) + 1 > Bucket_Count (Container) then
         Rehash (Container, Prime_At_Least
           (Hash_Type'Max (2 * Bucket_Count (Container),
                           Hash_Type (Container.Length) + 1)));
      end if;
      if Container.Free = 0
        and then (Container.Nodes = null
                  or else Container.High = Container.Nodes'Length)
      then
         Reserve_Nodes
           (Container,
            (if Container.High >= Count_Type'Last / 2 then Count_Type'Last
             else Count_Type'Max (8, 2 * Container.High)));
      end if;
   end Make_Room;

   --  Takes a node from the pool (Make_Room must have been called) and
   --  links it into the table with hash H; its key and element are set
   --  by the caller.
   procedure Allocate (Container : in out Set; H : Hash_Type;
                       Node : out Count_Type)
   is
      B : constant Hash_Type := Bucket (Container, H);
   begin
      if Container.Free /= 0 then
         Node := Container.Free;
         Container.Free := Container.Nodes (Node).Next;
      else
         Container.High := Container.High + 1;
         Node := Container.High;
      end if;
      Container.Nodes (Node).Hash := H;
      Container.Nodes (Node).Used := True;
      Container.Nodes (Node).Next := Container.Buckets (B);
      Container.Buckets (B) := Node;
      Container.Length := Container.Length + 1;
   end Allocate;

   --  Unlinks a node and returns it to the pool (finalizing its key and
   --  element in the unbounded and indefinite forms).
   procedure Deallocate (Container : in out Set; Node : Count_Type) is
      B : constant Hash_Type := Bucket (Container, Container.Nodes (Node).Hash);
      N : Count_Type := Container.Buckets (B);
   begin
      if N = Node then
         Container.Buckets (B) := Container.Nodes (Node).Next;
      else
         while Container.Nodes (N).Next /= Node loop
            N := Container.Nodes (N).Next;
         end loop;
         Container.Nodes (N).Next := Container.Nodes (Node).Next;
      end if;
      Free (Container.Nodes (Node).Element);
      Container.Nodes (Node).Used := False;
      Container.Nodes (Node).Next := Container.Free;
      Container.Free := Node;
      Container.Length := Container.Length - 1;
   end Deallocate;

   --  Removes all the nodes and resets the pool.
   procedure Clear_All (Container : in out Set) is
   begin
      for N in 1 .. Container.High loop
         Free (Container.Nodes (N).Element);
         Container.Nodes (N).Used := False;
      end loop;
      if Container.Buckets /= null then
         Container.Buckets.all := (others => 0);
      end if;
      Container.Length := 0;
      Container.Free := 0;
      Container.High := 0;
   end Clear_All;

   --  The first node in iteration order at or after bucket B, or 0.
   function First_From (Container : Set; B : Hash_Type) return Count_Type is
   begin
      for J in B .. Bucket_Count (Container) loop
         if Container.Buckets (J) /= 0 then
            return Container.Buckets (J);
         end if;
      end loop;
      return 0;
   end First_From;

   function First_Node (Container : Set) return Count_Type is
     (if Container.Length = 0 then 0 else First_From (Container, 1));

   function Next_Node (Container : Set; Node : Count_Type)
      return Count_Type is
   begin
      if Container.Nodes (Node).Next /= 0 then
         return Container.Nodes (Node).Next;
      end if;
      return First_From
        (Container, Bucket (Container, Container.Nodes (Node).Hash) + 1);
   end Next_Node;

   --  Inserts a new node holding a copy of New_Item (whose hash is H; no
   --  equivalent element is present); the container is not modified if
   --  the copy fails.
   procedure Insert_New (Container : in out Set;
                         New_Item  : Element_Type;
                         H         : Hash_Type;
                         Node      : out Count_Type) is
      E : Element_Access := new Element_Type'(New_Item);
   begin
      begin
         Make_Room (Container);
      exception
         when others =>
            Free (E);
            raise;
      end;
      Allocate (Container, H, Node);
      Container.Nodes (Node).Element := E;
   end Insert_New;

   --  Inserts a copy of Item if no equivalent element is present.
   procedure Insert_If_Absent (Container : in out Set; Item : Element_Type)
   is
      H : constant Hash_Type := Hash (Item);
      N : Count_Type :=
        (if Container.Length = 0 then 0 else Find_Node (Container, Item, H));
   begin
      if N = 0 then
         Insert_New (Container, Item, H, N);
      end if;
   end Insert_If_Absent;

   --  Inserts a copy of each element of Source into Target.
   procedure Insert_All (Target : in out Set; Source : Set) is
      N    : Count_Type := First_Node (Source);
      Node : Count_Type;
   begin
      while N /= 0 loop
         Insert_New (Target, Source.Nodes (N).Element.all,
                     Source.Nodes (N).Hash, Node);
         N := Next_Node (Source, N);
      end loop;
   end Insert_All;

   ---------------------------------------------------------------------
   --  Controlled operations
   ---------------------------------------------------------------------

   --  Makes a deep copy with the same layout.
   overriding procedure Adjust (Container : in out Set) is
      Nodes   : constant Node_Array_Access := Container.Nodes;
      Buckets : constant Bucket_Array_Access := Container.Buckets;
   begin
      Container.Busy := 0;
      Container.Lock := 0;
      Container.Nodes := null;
      Container.Buckets := null;
      if Nodes = null then
         Container.Length := 0;
         Container.Free := 0;
         Container.High := 0;
         return;
      end if;
      Container.Buckets := new Bucket_Array'(Buckets.all);
      Container.Nodes := new Node_Array (1 .. Nodes'Length);
      for N in 1 .. Container.High loop
         Container.Nodes (N) := Nodes (N);
         Container.Nodes (N).Element := null;
      end loop;
      for N in 1 .. Container.High loop
         if Nodes (N).Used then
            Container.Nodes (N).Element :=
              new Element_Type'(Nodes (N).Element.all);
         end if;
      end loop;
   end Adjust;

   overriding procedure Finalize (Container : in out Set) is
      Prohibited : constant Boolean :=
        Container.Busy > 0 or else Container.Lock > 0;
   begin
      --  (while tampering is prohibited (e.g. the target of an assignment):
      --  Program_Error, the container left unchanged)
      if Prohibited then
         raise Program_Error with "set finalized while tampering is prohibited";
      end if;
      if Container.Nodes /= null then
         for N in 1 .. Container.High loop
            Free (Container.Nodes (N).Element);
         end loop;
      end if;
      Free (Container.Nodes);
      Free (Container.Buckets);
      Container.Length := 0;
      Container.Free := 0;
      Container.High := 0;
   end Finalize;

   ---------------------------------------------------------------------
   --  Streams
   ---------------------------------------------------------------------

   procedure Write
     (Stream    : not null access Ada.Streams.Root_Stream_Type'Class;
      Container : Set)
   is
      N : Count_Type := First_Node (Container);
   begin
      Count_Type'Base'Write (Stream, Container.Length);
      while N /= 0 loop
         Element_Type'Write (Stream, Container.Nodes (N).Element.all);
         N := Next_Node (Container, N);
      end loop;
   end Write;

   procedure Read
     (Stream    : not null access Ada.Streams.Root_Stream_Type'Class;
      Container : out Set)
   is
      N : Count_Type'Base;
   begin
      Clear (Container);
      Count_Type'Base'Read (Stream, N);
      if N < 0 then
         raise Constraint_Error with "invalid set length in stream";
      end if;
      for J in 1 .. N loop
         declare
            E : Element_Type;
         begin
            Element_Type'Read (Stream, E);
            Include (Container, E);
         end;
      end loop;
   end Read;

   procedure Write
     (Stream   : not null access Ada.Streams.Root_Stream_Type'Class;
      Position : Cursor) is
   begin
      raise Program_Error with "attempt to stream a set cursor";
   end Write;

   procedure Read
     (Stream   : not null access Ada.Streams.Root_Stream_Type'Class;
      Position : out Cursor) is
   begin
      raise Program_Error with "attempt to stream a set cursor";
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

   ---------------------------------------------------------------------
   --  Iterators
   ---------------------------------------------------------------------

   Max_Split : constant := 64;
   type Chunk_Starts is array (1 .. Max_Split + 1) of Count_Type;

   type Iterator is new Ada.Finalization.Limited_Controlled
     and Set_Iterator_Interfaces.Parallel_Iterator with
   record
      Container : Set_Access;
      Chunks    : Natural := 0;        --  0: not split
      Starts    : Chunk_Starts := (others => 0);
   end record;

   overriding procedure Finalize (Object : in out Iterator);

   overriding function First (Object : Iterator) return Cursor;
   overriding function Next
     (Object : Iterator; Position : Cursor) return Cursor;
   overriding function Is_Split (Object : Iterator) return Boolean;
   overriding procedure Split_Into_Chunks
     (Object     : in out Iterator;
      Max_Chunks : Set_Iterator_Interfaces.Chunk_Index);
   overriding function Chunk_Count
     (Object : Iterator) return Set_Iterator_Interfaces.Chunk_Index;
   overriding function First
     (Object : Iterator; Chunk : Set_Iterator_Interfaces.Chunk_Index)
      return Cursor;
   overriding function Next
     (Object   : Iterator;
      Position : Cursor;
      Chunk    : Set_Iterator_Interfaces.Chunk_Index) return Cursor;

   overriding procedure Finalize (Object : in out Iterator) is
   begin
      if Object.Container /= null then
         Object.Container.Busy := Object.Container.Busy - 1;
         Object.Container := null;
      end if;
   end Finalize;

   function Make_Cursor (Container : Set_Access; Node : Count_Type)
      return Cursor is
     (if Node = 0 then No_Element else (Container, Node));

   overriding function First (Object : Iterator) return Cursor is
     (Make_Cursor (Object.Container, First_Node (Object.Container.all)));

   procedure Check_Iterator_Cursor (Object : Iterator; Position : Cursor) is
   begin
      if Position.Container /= null
        and then Position.Container /= Object.Container
      then
         raise Program_Error with "cursor designates another set";
      end if;
   end Check_Iterator_Cursor;

   overriding function Next
     (Object : Iterator; Position : Cursor) return Cursor is
   begin
      Check_Iterator_Cursor (Object, Position);
      return Next (Position);
   end Next;

   overriding function Is_Split (Object : Iterator) return Boolean is
     (Object.Chunks > 0);

   overriding procedure Split_Into_Chunks
     (Object     : in out Iterator;
      Max_Chunks : Set_Iterator_Interfaces.Chunk_Index)
   is
      C    : constant Set_Access := Object.Container;
      K    : constant Count_Type :=
        Count_Type'Max (1, Count_Type'Min
          (C.Length, Count_Type'Min (Count_Type (Max_Chunks), Max_Split)));
      Size : constant Count_Type := (C.Length + K - 1) / K;
      N    : Count_Type := First_Node (C.all);
      I    : Count_Type := 0;
   begin
      if Object.Chunks > 0 then
         raise Program_Error with "iterator already split";
      end if;
      while N /= 0 loop
         if I mod Size = 0 then
            Object.Chunks := Object.Chunks + 1;
            Object.Starts (Object.Chunks) := N;
         end if;
         I := I + 1;
         N := Next_Node (C.all, N);
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
     (Object : Iterator) return Set_Iterator_Interfaces.Chunk_Index is
   begin
      if Object.Chunks = 0 then
         raise Program_Error with "iterator not split";
      end if;
      return Object.Chunks;
   end Chunk_Count;

   overriding function First
     (Object : Iterator; Chunk : Set_Iterator_Interfaces.Chunk_Index)
      return Cursor is
   begin
      Check_Chunk (Object, Chunk);
      return Make_Cursor (Object.Container, Object.Starts (Chunk));
   end First;

   overriding function Next
     (Object   : Iterator;
      Position : Cursor;
      Chunk    : Set_Iterator_Interfaces.Chunk_Index) return Cursor
   is
      N : Count_Type;
   begin
      Check_Chunk (Object, Chunk);
      Check_Iterator_Cursor (Object, Position);
      if Position.Container = null then
         return No_Element;
      end if;
      N := Next_Node (Object.Container.all, Position.Node);
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

   function Has_Element (Container : Set; Position : Cursor)
      return Boolean is
     (Position.Container = Var (Container)
      and then Valid (Container, Position.Node));

   function "=" (Left, Right : Set) return Boolean is
      N, M : Count_Type;
   begin
      if Var (Left) = Var (Right) then
         return True;
      elsif Left.Length /= Right.Length then
         return False;
      end if;
      N := First_Node (Left);
      while N /= 0 loop
         M := Find_Node (Right, Left.Nodes (N).Element.all, Left.Nodes (N).Hash);
         if M = 0
           or else not (Left.Nodes (N).Element.all = Right.Nodes (M).Element.all)
         then
            return False;
         end if;
         N := Next_Node (Left, N);
      end loop;
      return True;
   end "=";

   function Equivalent_Sets (Left, Right : Set) return Boolean is
      N : Count_Type;
   begin
      if Var (Left) = Var (Right) then
         return True;
      elsif Left.Length /= Right.Length then
         return False;
      end if;
      N := First_Node (Left);
      while N /= 0 loop
         if Find_Node (Right, Left.Nodes (N).Element.all, Left.Nodes (N).Hash) = 0
         then
            return False;
         end if;
         N := Next_Node (Left, N);
      end loop;
      return True;
   end Equivalent_Sets;

   function Tampering_With_Cursors_Prohibited
      (Container : Set) return Boolean is
     (Container.Busy > 0 or else Container.Lock > 0);

   function Empty (Capacity : Count_Type := 10)
      return Set is
   begin
      return Result : Set do
         Reserve_Capacity (Result, Capacity);
      end return;
   end Empty;

   function To_Set (New_Item : Element_Type) return Set is
   begin
      return Result : Set do
         Insert (Result, New_Item);
      end return;
   end To_Set;

   function Capacity (Container : Set) return Count_Type is
     (Count_Type (Hash_Type'Min (Bucket_Count (Container),
                                 Hash_Type (Count_Type'Last))));

   procedure Reserve_Capacity (Container : in out Set;
                               Capacity  : in     Count_Type) is
   begin
      Check_Cursors (Container);
      if Hash_Type (Capacity) > Bucket_Count (Container) then
         Rehash (Container, Prime_At_Least (Hash_Type (Capacity)));
      end if;
      Reserve_Nodes (Container, Capacity);
   end Reserve_Capacity;

   function Length (Container : Set) return Count_Type is
     (Container.Length);

   function Is_Empty (Container : Set) return Boolean is
     (Container.Length = 0);

   procedure Clear (Container : in out Set) is
   begin
      Check_Cursors (Container);
      Clear_All (Container);
   end Clear;

   function Element (Position : Cursor) return Element_Type is
   begin
      Check_Has_Element (Position);
      return Position.Container.Nodes (Position.Node).Element.all;
   end Element;

   function Element (Container : Set;
                     Position  : Cursor) return Element_Type is
   begin
      Check_Position (Container, Position);
      return Container.Nodes (Position.Node).Element.all;
   end Element;

   --  Replaces the element of Node by a copy of New_Item, moving the node
   --  to another bucket if the new element is not equivalent to the old
   --  one; Program_Error if another element is equivalent to New_Item.
   procedure Replace_Node (Container : in out Set;
                           Node      : Count_Type;
                           New_Item  : Element_Type)
   is
      H : constant Hash_Type := Hash (New_Item);
      N : Count_Type;
   begin
      if Equivalent_Elements (Container.Nodes (Node).Element.all, New_Item) then
         declare
            X : constant Element_Access := new Element_Type'(New_Item);
         begin
            Free (Container.Nodes (Node).Element);
            Container.Nodes (Node).Element := X;
            Container.Nodes (Node).Hash := H;
         end;
         return;
      end if;
      N := Find_Node (Container, New_Item, H);
      if N /= 0 then
         raise Program_Error with "an equivalent element is already in the set";
      end if;
      declare
         X : constant Element_Access := new Element_Type'(New_Item);
         B : Hash_Type := Bucket (Container, Container.Nodes (Node).Hash);
      begin
         --  Unchain the node from its bucket ...
         N := Container.Buckets (B);
         if N = Node then
            Container.Buckets (B) := Container.Nodes (Node).Next;
         else
            while Container.Nodes (N).Next /= Node loop
               N := Container.Nodes (N).Next;
            end loop;
            Container.Nodes (N).Next := Container.Nodes (Node).Next;
         end if;
         Free (Container.Nodes (Node).Element);
         Container.Nodes (Node).Element := X;
         --  ... and chain it into the bucket of the new hash value.
         Container.Nodes (Node).Hash := H;
         B := Bucket (Container, H);
         Container.Nodes (Node).Next := Container.Buckets (B);
         Container.Buckets (B) := Node;
      end;
   end Replace_Node;

   procedure Replace_Element (Container : in out Set;
                              Position  : in     Cursor;
                              New_item  : in     Element_Type) is
   begin
      Check_Cursors (Container);
      Check_Position (Container, Position);
      Replace_Node (Container, Position.Node, New_item);
   end Replace_Element;

   procedure Query_Element
     (Position : in Cursor;
      Process  : not null access procedure (Element : in Element_Type)) is
   begin
      Check_Has_Element (Position);
      declare
         Guard : Lock_Guard (Position.Container);
      begin
         Process (Position.Container.Nodes (Position.Node).Element.all);
      end;
   end Query_Element;

   procedure Query_Element
     (Container : in Set;
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

   function Constant_Reference (Container : aliased in Set;
                                Position  : in Cursor)
      return Constant_Reference_Type is
   begin
      Check_Position (Container, Position);
      return (Element => Container.Nodes (Position.Node).Element,
              Control => (Ada.Finalization.Controlled with
                          Container => Lock (Container)));
   end Constant_Reference;

   procedure Assign (Target : in out Set; Source : in Set) is
   begin
      if Var (Target) = Var (Source) then
         return;
      end if;
      Check_Cursors (Target);
      Clear_All (Target);
      Reserve_Capacity (Target, Source.Length);
      Insert_All (Target, Source);
   end Assign;

   function Copy (Source : Set; Capacity : Count_Type := 0)
      return Set is
   begin
      if Capacity /= 0 and then Capacity < Source.Length then
         raise Capacity_Error with "capacity less than the length";
      end if;
      return Result : Set do
         Reserve_Capacity (Result, Count_Type'Max (Capacity, Source.Length));
         Insert_All (Result, Source);
      end return;
   end Copy;

   procedure Move (Target : in out Set;
                   Source : in out Set) is
   begin
      if Var (Target) = Var (Source) then
         return;
      end if;
      Check_Cursors (Target);
      Check_Cursors (Source);
      Clear_All (Target);
      declare
         Nodes   : constant Node_Array_Access := Target.Nodes;
         Buckets : constant Bucket_Array_Access := Target.Buckets;
      begin
         Target.Nodes := Source.Nodes;
         Target.Buckets := Source.Buckets;
         Target.Length := Source.Length;
         Target.Free := Source.Free;
         Target.High := Source.High;
         Source.Nodes := Nodes;
         Source.Buckets := Buckets;
         Source.Length := 0;
         Source.Free := 0;
         Source.High := 0;
      end;
   end Move;

   procedure Insert (Container : in out Set;
                     New_Item  : in     Element_Type;
                     Position  :    out Cursor;
                     Inserted  :    out Boolean)
   is
      H : Hash_Type;
      N : Count_Type;
   begin
      Check_Cursors (Container);
      H := Hash (New_Item);
      N := (if Container.Length = 0 then 0
            else Find_Node (Container, New_Item, H));
      if N /= 0 then
         Position := (Var (Container), N);
         Inserted := False;
         return;
      end if;
      Insert_New (Container, New_Item, H, N);
      Position := (Var (Container), N);
      Inserted := True;
   end Insert;

   procedure Insert (Container : in out Set;
                     New_Item  : in     Element_Type)
   is
      Position : Cursor;
      Inserted : Boolean;
   begin
      Insert (Container, New_Item, Position, Inserted);
      if not Inserted then
         raise Constraint_Error with "element already in set";
      end if;
   end Insert;

   procedure Replace_Equivalent (Container : in out Set;
                                 Node      : Count_Type;
                                 New_Item  : Element_Type) is
      X : constant Element_Access := new Element_Type'(New_Item);
   begin
      Free (Container.Nodes (Node).Element);
      Container.Nodes (Node).Element := X;
   end Replace_Equivalent;

   procedure Include (Container : in out Set;
                      New_Item  : in     Element_Type)
   is
      Position : Cursor;
      Inserted : Boolean;
   begin
      Insert (Container, New_Item, Position, Inserted);
      if not Inserted then
         Replace_Equivalent (Container, Position.Node, New_Item);
      end if;
   end Include;

   procedure Replace (Container : in out Set;
                      New_Item  : in     Element_Type)
   is
      N : Count_Type;
   begin
      Check_Cursors (Container);
      N := Find_Node (Container, New_Item);
      if N = 0 then
         raise Constraint_Error with "element not in set";
      end if;
      Replace_Equivalent (Container, N, New_Item);
   end Replace;

   procedure Exclude (Container : in out Set;
                      Item      : in     Element_Type)
   is
      N : Count_Type;
   begin
      Check_Cursors (Container);
      N := Find_Node (Container, Item);
      if N /= 0 then
         Deallocate (Container, N);
      end if;
   end Exclude;

   procedure Delete (Container : in out Set;
                     Item      : in     Element_Type)
   is
      N : Count_Type;
   begin
      Check_Cursors (Container);
      N := Find_Node (Container, Item);
      if N = 0 then
         raise Constraint_Error with "element not in set";
      end if;
      Deallocate (Container, N);
   end Delete;

   procedure Delete (Container : in out Set;
                     Position  : in out Cursor) is
   begin
      Check_Cursors (Container);
      Check_Position (Container, Position);
      Deallocate (Container, Position.Node);
      Position := No_Element;
   end Delete;

   ---------------------------------------------------------------------
   --  Set operations
   ---------------------------------------------------------------------

   function In_Set (Container : Set; Other : Set; Node : Count_Type)
      return Boolean is
     (Find_Node (Container, Other.Nodes (Node).Element.all,
                 Other.Nodes (Node).Hash) /= 0);

   procedure Union (Target : in out Set;
                    Source : in     Set)
   is
      N, M : Count_Type;
   begin
      Check_Cursors (Target);
      if Var (Target) = Var (Source) then
         return;
      end if;
      N := First_Node (Source);
      while N /= 0 loop
         if not In_Set (Target, Source, N) then
            Insert_New (Target, Source.Nodes (N).Element.all,
                        Source.Nodes (N).Hash, M);
         end if;
         N := Next_Node (Source, N);
      end loop;
   end Union;

   --  The kinds of combination of two sets.
   type Operation is (Op_Union, Op_Intersection, Op_Difference,
                      Op_Symmetric_Difference);

   --  Calls Emit for each node of Left or Right whose element belongs to
   --  the result of Op.
   generic
      with procedure Emit (From : Set; Node : Count_Type);
   procedure Combine_Walk (Left, Right : Set; Op : Operation);

   procedure Combine_Walk (Left, Right : Set; Op : Operation) is
      N : Count_Type := First_Node (Left);
   begin
      while N /= 0 loop
         if In_Set (Right, Left, N) = (Op in Op_Union | Op_Intersection)
           or else Op = Op_Union
         then
            Emit (Left, N);
         end if;
         N := Next_Node (Left, N);
      end loop;
      if Op in Op_Union | Op_Symmetric_Difference then
         N := First_Node (Right);
         while N /= 0 loop
            if not In_Set (Left, Right, N) then
               Emit (Right, N);
            end if;
            N := Next_Node (Right, N);
         end loop;
      end if;
   end Combine_Walk;

   function Combine (Left, Right : Set; Op : Operation) return Set is
      Count : Count_Type := 0;
      procedure Count_One (From : Set; Node : Count_Type) is
      begin
         Count := Count + 1;
      end Count_One;
      procedure Count_All is new Combine_Walk (Count_One);
   begin
      Count_All (Left, Right, Op);
      return Result : Set do
         Reserve_Capacity (Result, Count);
         declare
            procedure Add (From : Set; Node : Count_Type) is
               M : Count_Type;
            begin
               Insert_New (Result, From.Nodes (Node).Element.all,
                           From.Nodes (Node).Hash, M);
            end Add;
            procedure Add_All is new Combine_Walk (Add);
         begin
            Add_All (Left, Right, Op);
         end;
      end return;
   end Combine;

   function Union (Left, Right : Set) return Set is
     (Combine (Left, Right, Op_Union));

   procedure Intersection (Target : in out Set;
                           Source : in     Set)
   is
      N, M : Count_Type;
   begin
      Check_Cursors (Target);
      if Var (Target) = Var (Source) then
         return;
      end if;
      N := First_Node (Target);
      while N /= 0 loop
         M := Next_Node (Target, N);
         if not In_Set (Source, Target, N) then
            Deallocate (Target, N);
         end if;
         N := M;
      end loop;
   end Intersection;

   function Intersection (Left, Right : Set) return Set is
     (Combine (Left, Right, Op_Intersection));

   procedure Difference (Target : in out Set;
                         Source : in     Set)
   is
      N, M : Count_Type;
   begin
      Check_Cursors (Target);
      if Var (Target) = Var (Source) then
         Clear_All (Target);
         return;
      end if;
      N := First_Node (Target);
      while N /= 0 loop
         M := Next_Node (Target, N);
         if In_Set (Source, Target, N) then
            Deallocate (Target, N);
         end if;
         N := M;
      end loop;
   end Difference;

   function Difference (Left, Right : Set) return Set is
     (Combine (Left, Right, Op_Difference));

   procedure Symmetric_Difference (Target : in out Set;
                                   Source : in     Set)
   is
      N, T : Count_Type;
   begin
      Check_Cursors (Target);
      if Var (Target) = Var (Source) then
         Clear_All (Target);
         return;
      end if;
      N := First_Node (Source);
      while N /= 0 loop
         T := Find_Node (Target, Source.Nodes (N).Element.all,
                         Source.Nodes (N).Hash);
         if T /= 0 then
            Deallocate (Target, T);
         else
            Insert_New (Target, Source.Nodes (N).Element.all,
                        Source.Nodes (N).Hash, T);
         end if;
         N := Next_Node (Source, N);
      end loop;
   end Symmetric_Difference;

   function Symmetric_Difference (Left, Right : Set) return Set is
     (Combine (Left, Right, Op_Symmetric_Difference));

   function Overlap (Left, Right : Set) return Boolean is
      N : Count_Type := First_Node (Left);
   begin
      while N /= 0 loop
         if In_Set (Right, Left, N) then
            return True;
         end if;
         N := Next_Node (Left, N);
      end loop;
      return False;
   end Overlap;

   function Is_Subset (Subset : Set;
                       Of_Set : Set) return Boolean
   is
      N : Count_Type := First_Node (Subset);
   begin
      if Subset.Length > Of_Set.Length then
         return False;
      end if;
      while N /= 0 loop
         if not In_Set (Of_Set, Subset, N) then
            return False;
         end if;
         N := Next_Node (Subset, N);
      end loop;
      return True;
   end Is_Subset;

   ---------------------------------------------------------------------
   --  Navigation
   ---------------------------------------------------------------------

   function First (Container : Set) return Cursor is
     (Make_Cursor (Var (Container), First_Node (Container)));

   function Next (Position : Cursor) return Cursor is
   begin
      if not Has_Element (Position) then
         return No_Element;
      end if;
      return Make_Cursor
        (Position.Container,
         Next_Node (Position.Container.all, Position.Node));
   end Next;

   function Next (Container : Set;
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

   procedure Next (Container : in     Set;
                   Position  : in out Cursor) is
   begin
      Position := Next (Container, Position);
   end Next;

   function Find (Container : Set;
                  Item      : Element_Type)
      return Cursor is
     (Make_Cursor (Var (Container), Find_Node (Container, Item)));

   function Contains (Container : Set;
                      Item      : Element_Type) return Boolean is
     (Find_Node (Container, Item) /= 0);

   function Element_Of (Position : Cursor) return Element_Type is
   begin
      Check_Has_Element (Position);
      return Position.Container.Nodes (Position.Node).Element.all;
   end Element_Of;

   function Equivalent_Elements (Left, Right : Cursor)
      return Boolean is
   begin
      Check_Has_Element (Left);
      Check_Has_Element (Right);
      return Equivalent_Elements (Element_Of (Left), Element_Of (Right));
   end Equivalent_Elements;

   function Equivalent_Elements (Left  : Cursor;
                                 Right : Element_Type)
      return Boolean is
     (Equivalent_Elements (Element_Of (Left), Right));

   function Equivalent_Elements (Left  : Element_Type;
                                 Right : Cursor)
      return Boolean is
     (Equivalent_Elements (Left, Element_Of (Right)));

   procedure Iterate
     (Container : in Set;
      Process   : not null access procedure (Position : in Cursor))
   is
      C     : constant Set_Access := Var (Container);
      Guard : Busy_Guard (C);
      N     : Count_Type := First_Node (Container);
   begin
      while N /= 0 loop
         Process ((C, N));
         N := Next_Node (Container, N);
      end loop;
   end Iterate;

   function Iterate (Container : in Set)
      return Set_Iterator_Interfaces.Parallel_Iterator'Class
   is
      C : constant Set_Access := Var (Container);
   begin
      return It : Iterator do
         It.Container := C;
         C.Busy := C.Busy + 1;
      end return;
   end Iterate;

   ---------------------------------------------------------------------
   --  Generic_Keys
   ---------------------------------------------------------------------

   package body Generic_Keys is

      --  The node whose element has a key equivalent to K, or 0.
      function Find_Key (Container : Set; K : Key_Type) return Count_Type is
         H : Hash_Type;
         N : Count_Type;
      begin
         if Container.Length = 0 then
            return 0;
         end if;
         H := Hash (K);
         N := Container.Buckets (Bucket (Container, H));
         while N /= 0 loop
            if Container.Nodes (N).Hash = H
              and then Equivalent_Keys (Key (Container.Nodes (N).Element.all), K)
            then
               return N;
            end if;
            N := Container.Nodes (N).Next;
         end loop;
         return 0;
      end Find_Key;

      function Key (Position : Cursor) return Key_Type is
      begin
         Check_Has_Element (Position);
         return Key (Position.Container.Nodes (Position.Node).Element.all);
      end Key;

      function Key (Container : Set;
                    Position : Cursor) return Key_Type is
      begin
         Check_Position (Container, Position);
         return Key (Container.Nodes (Position.Node).Element.all);
      end Key;

      function Element (Container : Set;
                        Key       : Key_Type)
        return Element_Type
      is
         N : constant Count_Type := Find_Key (Container, Key);
      begin
         if N = 0 then
            raise Constraint_Error with "key not in set";
         end if;
         return Container.Nodes (N).Element.all;
      end Element;

      procedure Replace (Container : in out Set;
                         Key       : in     Key_Type;
                         New_Item  : in     Element_Type)
      is
         N : Count_Type;
      begin
         Check_Cursors (Container);
         N := Find_Key (Container, Key);
         if N = 0 then
            raise Constraint_Error with "key not in set";
         end if;
         Replace_Node (Container, N, New_Item);
      end Replace;

      procedure Exclude (Container : in out Set;
                         Key       : in     Key_Type)
      is
         N : Count_Type;
      begin
         Check_Cursors (Container);
         N := Find_Key (Container, Key);
         if N /= 0 then
            Deallocate (Container, N);
         end if;
      end Exclude;

      procedure Delete (Container : in out Set;
                        Key       : in     Key_Type)
      is
         N : Count_Type;
      begin
         Check_Cursors (Container);
         N := Find_Key (Container, Key);
         if N = 0 then
            raise Constraint_Error with "key not in set";
         end if;
         Deallocate (Container, N);
      end Delete;

      function Find (Container : Set;
                     Key       : Key_Type)
         return Cursor is
        (Make_Cursor (Var (Container), Find_Key (Container, Key)));

      function Contains (Container : Set;
                         Key       : Key_Type)
         return Boolean is
        (Find_Key (Container, Key) /= 0);

      procedure Update_Element_Preserving_Key
        (Container : in out Set;
         Position  : in     Cursor;
         Process   : not null access procedure
                         (Element : in out Element_Type))
      is
         Same : Boolean;
      begin
         Check_Position (Container, Position);
         declare
            K : constant Key_Type :=
              Key (Container.Nodes (Position.Node).Element.all);
         begin
            declare
               Guard : Lock_Guard (Var (Container));
            begin
               Process (Container.Nodes (Position.Node).Element.all);
            end;
            Same := Equivalent_Keys
              (K, Key (Container.Nodes (Position.Node).Element.all));
         end;
         if not Same then
            Deallocate (Container, Position.Node);
            raise Program_Error with "the key of the element was changed";
         end if;
      end Update_Element_Preserving_Key;

      procedure Free is new Ada.Unchecked_Deallocation
        (Key_Type, Saved_Key_Access);

      overriding procedure Adjust (Control : in out Key_Control) is
      begin
         if Control.Container /= null then
            Control.Container.Lock := Control.Container.Lock + 1;
            Control.Saved := new Key_Type'(Control.Saved.all);
         end if;
      end Adjust;

      overriding procedure Finalize (Control : in out Key_Control) is
         C    : constant Keys_Set_Access := Control.Container;
         Same : Boolean;
      begin
         if C = null then
            return;
         end if;
         Control.Container := null;
         C.Lock := C.Lock - 1;
         Same := Equivalent_Keys
           (Control.Saved.all, Key (C.Nodes (Control.Node).Element.all));
         Free (Control.Saved);
         if not Same then
            Deallocate (C.all, Control.Node);
            raise Program_Error with "the key of the element was changed";
         end if;
      end Finalize;

      function Make_Reference (Container : Set; Node : Count_Type)
         return Reference_Type
      is
         C : constant Set_Access := Var (Container);
      begin
         return R : constant Reference_Type :=
           (Element => C.Nodes (Node).Element,
            Control =>
              (Ada.Finalization.Controlled with
               Container => Keys_Set_Access (C),
               Node      => Node,
               Saved     => new Key_Type'(Key (C.Nodes (Node).Element.all))))
         do
            C.Lock := C.Lock + 1;
         end return;
      end Make_Reference;

      function Reference_Preserving_Key (Container : aliased in out Set;
                                         Position  : in Cursor)
         return Reference_Type is
      begin
         Check_Position (Container, Position);
         return Make_Reference (Container, Position.Node);
      end Reference_Preserving_Key;

      function Constant_Reference (Container : aliased in Set;
                                   Key       : in Key_Type)
         return Constant_Reference_Type
      is
         N : constant Count_Type := Find_Key (Container, Key);
      begin
         if N = 0 then
            raise Constraint_Error with "key not in set";
         end if;
         return (Element => Container.Nodes (N).Element,
                 Control => (Ada.Finalization.Controlled with
                          Container => Lock (Container)));
      end Constant_Reference;

      function Reference_Preserving_Key (Container : aliased in out Set;
                                         Key       : in Key_Type)
         return Reference_Type
      is
         N : constant Count_Type := Find_Key (Container, Key);
      begin
         if N = 0 then
            raise Constraint_Error with "key not in set";
         end if;
         return Make_Reference (Container, N);
      end Reference_Preserving_Key;

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

   end Generic_Keys;

end Ada.Containers.Hashed_Sets;
