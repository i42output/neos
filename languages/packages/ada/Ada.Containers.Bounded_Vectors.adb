--  Ada.Containers.Bounded_Vectors.adb
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
with Ada.Containers.Generic_Sort;
with Interfaces;

package body Ada.Containers.Bounded_Vectors is

   use type Interfaces.Integer_64;
   subtype Int is Interfaces.Integer_64;

   --  The operations that take the container only through a cursor (or as
   --  an in parameter) still have to record that tampering is prohibited:
   --  the variable view of the container is obtained by converting the
   --  access-to-constant value designating it.
   type Constant_Vector_Access is access constant Vector;
   for Constant_Vector_Access'Storage_Size use 0;

   function To_Variable is new Ada.Unchecked_Conversion
     (Constant_Vector_Access, Vector_Access);

   function Var (Container : Vector) return Vector_Access is
     (To_Variable (Container'Unchecked_Access));

   ---------------------------------------------------------------------
   --  Index arithmetic (done in 64 bits so that it cannot overflow)
   ---------------------------------------------------------------------

   First_Int : constant Int := Int (Index_Type'First);

   --  The position (1 = first) of an index.
   function Pos (Index : Extended_Index) return Int is
     (Int (Index) - First_Int + 1);

   --  The index of a position.
   function Idx (Position : Count_Type) return Extended_Index is
     (Extended_Index (First_Int + Int (Position) - 1));

   function Max_Length return Count_Type is
      F : constant Int := First_Int;
      L : constant Int := Int (Index_Type'Last);
   begin
      if L < F then
         return 0;
      elsif F < 0 and then L > Int'Last + F then
         return Count_Type'Last;
      elsif L - F >= Int (Count_Type'Last) then
         return Count_Type'Last;
      else
         return Count_Type (L - F + 1);
      end if;
   end Max_Length;

   Max : constant Count_Type := Max_Length;

   ---------------------------------------------------------------------
   --  Tampering
   ---------------------------------------------------------------------

   procedure Check_Cursors (Container : Vector) is
   begin
      if Container.Busy > 0 or else Container.Lock > 0 then
         raise Program_Error with "attempt to tamper with cursors";
      end if;
   end Check_Cursors;

   procedure Check_Elements (Container : Vector) is
   begin
      null;  --  never prohibited for a definite element type
   end Check_Elements;

   --  Objects of these types prohibit tampering while they exist.
   type Busy_Guard (Container : Vector_Access) is
     new Ada.Finalization.Limited_Controlled with null record;
   overriding procedure Initialize (Guard : in out Busy_Guard);
   overriding procedure Finalize (Guard : in out Busy_Guard);

   type Lock_Guard (Container : Vector_Access) is
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
   function Lock (Container : Vector) return Vector_Access is
      C : constant Vector_Access := Var (Container);
   begin
      C.Lock := C.Lock + 1;
      return C;
   end Lock;

   ---------------------------------------------------------------------
   --  Checks
   ---------------------------------------------------------------------

   --  Index in First_Index (Container) .. Last_Index (Container)
   procedure Check_Index (Container : Vector; Index : Extended_Index) is
      P : constant Int := Pos (Index);
   begin
      if P < 1 or else P > Int (Container.Last) then
         raise Constraint_Error with "index out of range";
      end if;
   end Check_Index;

   --  Index in First_Index (Container) .. Last_Index (Container) + 1
   procedure Check_Before (Container : Vector; Index : Extended_Index) is
      P : constant Int := Pos (Index);
   begin
      if P < 1 or else P > Int (Container.Last) + 1 then
         raise Constraint_Error with "index out of range";
      end if;
   end Check_Before;

   --  Position designates an element of Container
   procedure Check_Position (Container : Vector; Position : Cursor) is
   begin
      if Position.Container = null then
         raise Constraint_Error with "cursor has no element";
      elsif Position.Container /= Var (Container)
        or else Position.Index not in 1 .. Container.Last
      then
         raise Program_Error with "cursor designates no element of the vector";
      end if;
   end Check_Position;

   --  Before is No_Element or designates an element of Container
   procedure Check_Before (Container : Vector; Before : Cursor) is
   begin
      if Before.Container /= null
        and then (Before.Container /= Var (Container)
                  or else Before.Index not in 1 .. Container.Last)
      then
         raise Program_Error with "cursor designates no element of the vector";
      end if;
   end Check_Before;

   --  Length (Container) <= Maximum_Length - Count
   procedure Check_Length (Container : Vector; Count : Count_Type) is
   begin
      if Container.Last > Max - Count then
         raise Constraint_Error with "vector would be too long";
      end if;
      if Container.Last > Container.Capacity - Count then
         raise Capacity_Error with "vector capacity exceeded";
      end if;
   end Check_Length;

   ---------------------------------------------------------------------
   --  Storage
   ---------------------------------------------------------------------

   function Cap (Container : Vector) return Count_Type is
     (Container.Capacity);

   --  Ensures that the capacity is at least N, growing geometrically.
   procedure Grow (Container : in out Vector; N : Count_Type) is
   begin
      if N > Container.Capacity then
         raise Capacity_Error with "vector capacity exceeded";
      end if;
   end Grow;

   --  Opens a gap of Count empty elements at position P (1 .. Last + 1);
   --  the capacity must suffice.
   procedure Open_Gap (Container : in out Vector; P, Count : Count_Type) is
      L : constant Count_Type := Container.Last;
   begin
      if P <= L then
         Container.Elements (P + Count .. L + Count) :=
           Container.Elements (P .. L);
      end if;
      Container.Last := L + Count;
   end Open_Gap;

   ---------------------------------------------------------------------
   --  Controlled operations
   ---------------------------------------------------------------------

   ---------------------------------------------------------------------
   --  Streams
   ---------------------------------------------------------------------

   procedure Write
     (Stream    : not null access Ada.Streams.Root_Stream_Type'Class;
      Container : Vector) is
   begin
      Count_Type'Base'Write (Stream, Container.Last);
      for P in 1 .. Container.Last loop
         Element_Type'Write (Stream, Container.Elements (P));
      end loop;
   end Write;

   procedure Read
     (Stream    : not null access Ada.Streams.Root_Stream_Type'Class;
      Container : out Vector)
   is
      N : Count_Type'Base;
   begin
      Clear (Container);
      Count_Type'Base'Read (Stream, N);
      if N < 0 then
         raise Constraint_Error with "invalid vector length in stream";
      end if;
      Check_Length (Container, N);
      Grow (Container, N);
      for P in 1 .. N loop
         Element_Type'Read (Stream, Container.Elements (P));
         Container.Last := P;
      end loop;
   end Read;

   procedure Write
     (Stream   : not null access Ada.Streams.Root_Stream_Type'Class;
      Position : Cursor) is
   begin
      raise Program_Error with "attempt to stream a vector cursor";
   end Write;

   procedure Read
     (Stream   : not null access Ada.Streams.Root_Stream_Type'Class;
      Position : out Cursor) is
   begin
      raise Program_Error with "attempt to stream a vector cursor";
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

   type Iterator is new Ada.Finalization.Limited_Controlled
     and Vector_Iterator_Interfaces.Parallel_Reversible_Iterator with
   record
      Container   : Vector_Access;
      Start       : Count_Type := 0;     --  0: the whole vector
      Chunks      : Natural := 0;        --  0: not split
      Chunk_Size  : Count_Type := 0;
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
     (Object : in out Iterator; Max_Chunks : Vector_Iterator_Interfaces.Chunk_Index);
   overriding function Chunk_Count
     (Object : Iterator) return Vector_Iterator_Interfaces.Chunk_Index;
   overriding function First
     (Object : Iterator; Chunk : Vector_Iterator_Interfaces.Chunk_Index)
      return Cursor;
   overriding function Next
     (Object   : Iterator;
      Position : Cursor;
      Chunk    : Vector_Iterator_Interfaces.Chunk_Index) return Cursor;

   overriding procedure Finalize (Object : in out Iterator) is
   begin
      if Object.Container /= null then
         Object.Container.Busy := Object.Container.Busy - 1;
         Object.Container := null;
      end if;
   end Finalize;

   overriding function First (Object : Iterator) return Cursor is
   begin
      if Object.Start /= 0 then
         return (Object.Container, Object.Start);
      elsif Object.Container.Last = 0 then
         return No_Element;
      else
         return (Object.Container, 1);
      end if;
   end First;

   procedure Check_Iterator_Cursor (Object : Iterator; Position : Cursor) is
   begin
      if Position.Container /= null
        and then Position.Container /= Object.Container
      then
         raise Program_Error with "cursor designates another vector";
      end if;
   end Check_Iterator_Cursor;

   overriding function Next
     (Object : Iterator; Position : Cursor) return Cursor is
   begin
      Check_Iterator_Cursor (Object, Position);
      return Next (Position);
   end Next;

   overriding function Last (Object : Iterator) return Cursor is
   begin
      if Object.Start /= 0 then
         return (Object.Container, Object.Start);
      elsif Object.Container.Last = 0 then
         return No_Element;
      else
         return (Object.Container, Object.Container.Last);
      end if;
   end Last;

   overriding function Previous
     (Object : Iterator; Position : Cursor) return Cursor is
   begin
      Check_Iterator_Cursor (Object, Position);
      return Previous (Position);
   end Previous;

   overriding function Is_Split (Object : Iterator) return Boolean is
     (Object.Chunks > 0);

   overriding procedure Split_Into_Chunks
     (Object : in out Iterator; Max_Chunks : Vector_Iterator_Interfaces.Chunk_Index)
   is
      L : constant Count_Type := Object.Container.Last;
      N : Count_Type;
   begin
      if Object.Chunks > 0 then
         raise Program_Error with "iterator already split";
      end if;
      N := Count_Type'Max (1, Count_Type'Min (L, Count_Type (Max_Chunks)));
      Object.Chunk_Size := (L + N - 1) / N;
      if Object.Chunk_Size = 0 then
         Object.Chunks := 1;
      else
         Object.Chunks := Natural ((L + Object.Chunk_Size - 1) / Object.Chunk_Size);
      end if;
   end Split_Into_Chunks;

   procedure Check_Chunk (Object : Iterator; Chunk : Positive) is
   begin
      if Object.Chunks = 0 or else Chunk > Object.Chunks then
         raise Program_Error with "iterator not split or no such chunk";
      end if;
   end Check_Chunk;

   overriding function Chunk_Count
     (Object : Iterator) return Vector_Iterator_Interfaces.Chunk_Index is
   begin
      if Object.Chunks = 0 then
         raise Program_Error with "iterator not split";
      end if;
      return Object.Chunks;
   end Chunk_Count;

   overriding function First
     (Object : Iterator; Chunk : Vector_Iterator_Interfaces.Chunk_Index)
      return Cursor
   is
      P : Count_Type;
   begin
      Check_Chunk (Object, Chunk);
      P := Count_Type (Chunk - 1) * Object.Chunk_Size + 1;
      if Object.Chunk_Size = 0 or else P > Object.Container.Last then
         return No_Element;
      end if;
      return (Object.Container, P);
   end First;

   overriding function Next
     (Object   : Iterator;
      Position : Cursor;
      Chunk    : Vector_Iterator_Interfaces.Chunk_Index) return Cursor
   is
      Chunk_Last : Count_Type;
   begin
      Check_Chunk (Object, Chunk);
      Check_Iterator_Cursor (Object, Position);
      if Position.Container = null then
         return No_Element;
      end if;
      Chunk_Last := Count_Type'Min
        (Count_Type (Chunk) * Object.Chunk_Size, Object.Container.Last);
      if Position.Index >= Chunk_Last then
         return No_Element;
      end if;
      return (Object.Container, Position.Index + 1);
   end Next;

   ---------------------------------------------------------------------
   --  Visible operations
   ---------------------------------------------------------------------

   function Has_Element (Position : Cursor) return Boolean is
     (Position.Container /= null
      and then Position.Index in 1 .. Position.Container.Last);

   function Has_Element (Container : Vector; Position : Cursor)
      return Boolean is
     (Position.Container = Var (Container)
      and then Position.Index in 1 .. Container.Last);

   function "=" (Left, Right : Vector) return Boolean is
   begin
      if Var (Left) = Var (Right) then
         return True;
      elsif Left.Last /= Right.Last then
         return False;
      end if;
      for P in 1 .. Left.Last loop
         if not (Left.Elements (P) = Right.Elements (P)) then
            return False;
         end if;
      end loop;
      return True;
   end "=";

   function Tampering_With_Cursors_Prohibited
      (Container : Vector) return Boolean is
     (Container.Busy > 0 or else Container.Lock > 0);

   function Tampering_With_Elements_Prohibited
      (Container : Vector) return Boolean is
     (False);

   function Maximum_Length return Count_Type is (Max);

   function Empty (Capacity : Count_Type := 10) return Vector is
   begin
      if Capacity > Max then
         raise Constraint_Error with "capacity exceeds the maximum length";
      end if;
      return (Capacity => Capacity, others => <>);
   end Empty;

   function To_Vector (Length : Count_Type) return Vector is
   begin
      if Length > Max then
         raise Constraint_Error with "length exceeds the maximum length";
      end if;
      return Result : Vector (Length) do
         Result.Last := Length;
      end return;
   end To_Vector;

   function To_Vector
     (New_Item : Element_Type;
      Length   : Count_Type) return Vector is
   begin
      if Length > Max then
         raise Constraint_Error with "length exceeds the maximum length";
      end if;
      return Result : Vector (Length) do
         Append (Result, New_Item, Length);
      end return;
   end To_Vector;

   function "&" (Left, Right : Vector) return Vector is
   begin
      if Left.Last > Max - Right.Last then
         raise Constraint_Error with "vector would be too long";
      end if;
      return Result : Vector (Left.Last + Right.Last) do
         Append (Result, Left);
         Append (Result, Right);
      end return;
   end "&";

   function "&" (Left  : Vector;
                 Right : Element_Type) return Vector is
   begin
      if Left.Last > Max - 1 then
         raise Constraint_Error with "vector would be too long";
      end if;
      return Result : Vector (Left.Last + 1) do
         Append (Result, Left);
         Append (Result, Right);
      end return;
   end "&";

   function "&" (Left  : Element_Type;
                 Right : Vector) return Vector is
   begin
      if Right.Last > Max - 1 then
         raise Constraint_Error with "vector would be too long";
      end if;
      return Result : Vector (Right.Last + 1) do
         Append (Result, Left);
         Append (Result, Right);
      end return;
   end "&";

   function "&" (Left, Right  : Element_Type) return Vector is
   begin
      if Max < 2 then
         raise Constraint_Error with "vector would be too long";
      end if;
      return Result : Vector (2) do
         Append (Result, Left);
         Append (Result, Right);
      end return;
   end "&";

   function Capacity (Container : Vector) return Count_Type is
     (Cap (Container));

   procedure Reserve_Capacity (Container : in out Vector;
                               Capacity  : in     Count_Type)
   is
   begin
      if Capacity > Container.Capacity then
         raise Capacity_Error with "vector capacity exceeded";
      end if;
   end Reserve_Capacity;

   function Length (Container : Vector) return Count_Type is
     (Container.Last);

   procedure Set_Length (Container : in out Vector;
                         Length    : in     Count_Type) is
   begin
      Check_Cursors (Container);
      if Length > Max then
         raise Constraint_Error with "length exceeds the maximum length";
      end if;
      if Length > Cap (Container) then
         Reserve_Capacity (Container, Length);
      end if;
      Container.Last := Length;
   end Set_Length;

   function Is_Empty (Container : Vector) return Boolean is
     (Container.Last = 0);

   procedure Clear (Container : in out Vector) is
   begin
      Check_Cursors (Container);
      Container.Last := 0;
   end Clear;

   function To_Cursor (Container : Vector;
                       Index     : Extended_Index) return Cursor
   is
      P : constant Int := Pos (Index);
   begin
      if P < 1 or else P > Int (Container.Last) then
         return No_Element;
      end if;
      return (Var (Container), Count_Type (P));
   end To_Cursor;

   function To_Index (Position  : Cursor) return Extended_Index is
   begin
      if Position.Container = null then
         return No_Index;
      end if;
      return Idx (Position.Index);
   end To_Index;

   function To_Index (Container : Vector;
                      Position  : Cursor) return Extended_Index is
   begin
      if Position.Container = null then
         return No_Index;
      end if;
      Check_Position (Container, Position);
      return Idx (Position.Index);
   end To_Index;

   function Element (Container : Vector;
                     Index     : Index_Type)
      return Element_Type is
   begin
      Check_Index (Container, Index);
      return Container.Elements (Count_Type (Pos (Index)));
   end Element;

   function Element (Position : Cursor) return Element_Type is
   begin
      if not Has_Element (Position) then
         raise Constraint_Error with "cursor has no element";
      end if;
      return Position.Container.Elements (Position.Index);
   end Element;

   function Element (Container : Vector;
                     Position  : Cursor) return Element_Type is
   begin
      Check_Position (Container, Position);
      return Container.Elements (Position.Index);
   end Element;

   procedure Replace_At (Container : in out Vector;
                         P         : Count_Type;
                         New_Item  : Element_Type) is
   begin
      Container.Elements (P) := New_Item;
   end Replace_At;

   procedure Replace_Element (Container : in out Vector;
                              Index     : in     Index_Type;
                              New_Item  : in     Element_Type) is
   begin
      Check_Elements (Container);
      Check_Index (Container, Index);
      Replace_At (Container, Count_Type (Pos (Index)), New_Item);
   end Replace_Element;

   procedure Replace_Element (Container : in out Vector;
                              Position  : in     Cursor;
                              New_item  : in     Element_Type) is
   begin
      Check_Elements (Container);
      Check_Position (Container, Position);
      Replace_At (Container, Position.Index, New_item);
   end Replace_Element;

   procedure Query_Element
     (Container : in Vector;
      Index     : in Index_Type;
      Process   : not null access procedure (Element : in Element_Type)) is
   begin
      Check_Index (Container, Index);
      declare
         Guard : Lock_Guard (Var (Container));
      begin
         Process (Container.Elements (Count_Type (Pos (Index))));
      end;
   end Query_Element;

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
         Process (Position.Container.Elements (Position.Index));
      end;
   end Query_Element;

   procedure Query_Element
     (Container : in Vector;
      Position  : in Cursor;
      Process   : not null access procedure (Element : in Element_Type)) is
   begin
      Check_Position (Container, Position);
      declare
         Guard : Lock_Guard (Var (Container));
      begin
         Process (Container.Elements (Position.Index));
      end;
   end Query_Element;

   procedure Update_Element
     (Container : in out Vector;
      Index     : in     Index_Type;
      Process   : not null access procedure
                      (Element : in out Element_Type)) is
   begin
      Check_Index (Container, Index);
      declare
         Guard : Lock_Guard (Var (Container));
      begin
         Process (Container.Elements (Count_Type (Pos (Index))));
      end;
   end Update_Element;

   procedure Update_Element
     (Container : in out Vector;
      Position  : in     Cursor;
      Process   : not null access procedure
                      (Element : in out Element_Type)) is
   begin
      Check_Position (Container, Position);
      declare
         Guard : Lock_Guard (Var (Container));
      begin
         Process (Container.Elements (Position.Index));
      end;
   end Update_Element;

   function Constant_Reference (Container : aliased in Vector;
                                Index     : in Index_Type)
      return Constant_Reference_Type
   is
   begin
      Check_Index (Container, Index);
      return (Element => Container.Elements (Count_Type (Pos (Index)))'Access,
              Control => (Ada.Finalization.Controlled with
                          Container => Lock (Container)));
   end Constant_Reference;

   function Reference (Container : aliased in out Vector;
                       Index     : in Index_Type)
      return Reference_Type
   is
   begin
      Check_Index (Container, Index);
      return (Element => Container.Elements (Count_Type (Pos (Index)))'Access,
              Control => (Ada.Finalization.Controlled with
                          Container => Lock (Container)));
   end Reference;

   function Constant_Reference (Container : aliased in Vector;
                                Position  : in Cursor)
      return Constant_Reference_Type
   is
   begin
      Check_Position (Container, Position);
      return (Element => Container.Elements (Position.Index)'Access,
              Control => (Ada.Finalization.Controlled with
                          Container => Lock (Container)));
   end Constant_Reference;

   function Reference (Container : aliased in out Vector;
                       Position  : in Cursor)
      return Reference_Type
   is
   begin
      Check_Position (Container, Position);
      return (Element => Container.Elements (Position.Index)'Access,
              Control => (Ada.Finalization.Controlled with
                          Container => Lock (Container)));
   end Reference;

   procedure Assign (Target : in out Vector; Source : in Vector) is
   begin
      if Var (Target) = Var (Source) then
         return;
      end if;
      Check_Cursors (Target);
      if Source.Last > Cap (Target) then
         Reserve_Capacity (Target, Source.Last);
      end if;
      Target.Elements (1 .. Source.Last) := Source.Elements (1 .. Source.Last);
      Target.Last := Source.Last;
   end Assign;

   function Copy (Source : Vector; Capacity : Count_Type := 0)
      return Vector
   is
      C : constant Count_Type :=
        (if Capacity = 0 then Source.Last else Capacity);
   begin
      if Capacity /= 0 and then Capacity < Source.Last then
         raise Capacity_Error with "capacity less than the length";
      end if;
      return Result : Vector (C) do
         Assign (Result, Source);
      end return;
   end Copy;

   procedure Move (Target : in out Vector;
                   Source : in out Vector) is
   begin
      if Var (Target) = Var (Source) then
         return;
      end if;
      Check_Cursors (Target);
      Check_Cursors (Source);
      Reserve_Capacity (Target, Source.Last);
      Target.Elements (1 .. Source.Last) := Source.Elements (1 .. Source.Last);
      Target.Last := Source.Last;
      Source.Last := 0;
   end Move;

   --  Inserts the elements of New_Item at position P; the checks are done.
   procedure Insert_At (Container : in out Vector;
                        P         : Count_Type;
                        New_Item  : Vector) is
      N : constant Count_Type := New_Item.Last;
   begin
      if N = 0 then
         return;
      end if;
      if Var (Container) = Var (New_Item) then
         declare
            Copy : constant Vector := New_Item;
         begin
            Insert_At (Container, P, Copy);
         end;
         return;
      end if;
      Grow (Container, Container.Last + N);
      Open_Gap (Container, P, N);
      Container.Elements (P .. P + N - 1) := New_Item.Elements (1 .. N);
   end Insert_At;

   --  Inserts Count copies of New_Item at position P; the checks are done.
   procedure Insert_At (Container : in out Vector;
                        P         : Count_Type;
                        New_Item  : Element_Type;
                        Count     : Count_Type) is
   begin
      if Count = 0 then
         return;
      end if;
      Grow (Container, Container.Last + Count);
      Open_Gap (Container, P, Count);
      for Q in P .. P + Count - 1 loop
         Container.Elements (Q) := New_Item;
      end loop;
   end Insert_At;

   function Before_Position (Container : Vector; Before : Cursor)
      return Count_Type is
     (if Before.Container = null then Container.Last + 1 else Before.Index);

   procedure Insert (Container : in out Vector;
                     Before    : in     Extended_Index;
                     New_Item  : in     Vector) is
   begin
      Check_Cursors (Container);
      Check_Before (Container, Before);
      Check_Length (Container, New_Item.Last);
      Insert_At (Container, Count_Type (Pos (Before)), New_Item);
   end Insert;

   procedure Insert (Container : in out Vector;
                     Before    : in     Cursor;
                     New_Item  : in     Vector) is
   begin
      Check_Cursors (Container);
      Check_Before (Container, Before);
      Check_Length (Container, New_Item.Last);
      Insert_At (Container, Before_Position (Container, Before), New_Item);
   end Insert;

   procedure Insert (Container : in out Vector;
                     Before    : in     Cursor;
                     New_Item  : in     Vector;
                     Position  :    out Cursor)
   is
      P : Count_Type;
   begin
      Check_Cursors (Container);
      Check_Before (Container, Before);
      Check_Length (Container, New_Item.Last);
      P := Before_Position (Container, Before);
      Insert_At (Container, P, New_Item);
      Position := (if P <= Container.Last then (Var (Container), P)
                   else No_Element);
   end Insert;

   procedure Insert (Container : in out Vector;
                     Before    : in     Extended_Index;
                     New_Item  : in     Element_Type;
                     Count     : in     Count_Type := 1) is
   begin
      Check_Cursors (Container);
      Check_Before (Container, Before);
      Check_Length (Container, Count);
      Insert_At (Container, Count_Type (Pos (Before)), New_Item, Count);
   end Insert;

   procedure Insert (Container : in out Vector;
                     Before    : in     Cursor;
                     New_Item  : in     Element_Type;
                     Count     : in     Count_Type := 1) is
   begin
      Check_Cursors (Container);
      Check_Before (Container, Before);
      Check_Length (Container, Count);
      Insert_At (Container, Before_Position (Container, Before), New_Item,
                 Count);
   end Insert;

   procedure Insert (Container : in out Vector;
                     Before    : in     Cursor;
                     New_Item  : in     Element_Type;
                     Position  :    out Cursor;
                     Count     : in     Count_Type := 1)
   is
      P : Count_Type;
   begin
      Check_Cursors (Container);
      Check_Before (Container, Before);
      Check_Length (Container, Count);
      P := Before_Position (Container, Before);
      Insert_At (Container, P, New_Item, Count);
      Position := (if P <= Container.Last then (Var (Container), P)
                   else No_Element);
   end Insert;

   --  Inserts Count default-initialized elements at position P.
   procedure Insert_Default (Container : in out Vector;
                             P, Count  : Count_Type) is
      Default : Element_Type;
      pragma Warnings (Off, Default);
   begin
      if Count = 0 then
         return;
      end if;
      Grow (Container, Container.Last + Count);
      Open_Gap (Container, P, Count);
      for Q in P .. P + Count - 1 loop
         Container.Elements (Q) := Default;
      end loop;
   end Insert_Default;

   procedure Insert (Container : in out Vector;
                     Before    : in     Extended_Index;
                     Count     : in     Count_Type := 1) is
   begin
      Check_Cursors (Container);
      Check_Before (Container, Before);
      Check_Length (Container, Count);
      Insert_Default (Container, Count_Type (Pos (Before)), Count);
   end Insert;

   procedure Insert (Container : in out Vector;
                     Before    : in     Cursor;
                     Position  :    out Cursor;
                     Count     : in     Count_Type := 1)
   is
      P : Count_Type;
   begin
      Check_Cursors (Container);
      Check_Before (Container, Before);
      Check_Length (Container, Count);
      P := Before_Position (Container, Before);
      Insert_Default (Container, P, Count);
      Position := (if P <= Container.Last then (Var (Container), P)
                   else No_Element);
   end Insert;

   procedure Prepend (Container : in out Vector;
                      New_Item  : in     Vector) is
   begin
      Check_Cursors (Container);
      Check_Length (Container, New_Item.Last);
      Insert_At (Container, 1, New_Item);
   end Prepend;

   procedure Prepend (Container : in out Vector;
                      New_Item  : in     Element_Type;
                      Count     : in     Count_Type := 1) is
   begin
      Check_Cursors (Container);
      Check_Length (Container, Count);
      Insert_At (Container, 1, New_Item, Count);
   end Prepend;

   procedure Append (Container : in out Vector;
                     New_Item  : in     Vector) is
   begin
      Check_Cursors (Container);
      Check_Length (Container, New_Item.Last);
      Insert_At (Container, Container.Last + 1, New_Item);
   end Append;

   procedure Append (Container : in out Vector;
                     New_Item  : in     Element_Type;
                     Count     : in     Count_Type := 1) is
   begin
      Check_Cursors (Container);
      Check_Length (Container, Count);
      Insert_At (Container, Container.Last + 1, New_Item, Count);
   end Append;

   procedure Append_One (Container : in out Vector;
                         New_Item  : in     Element_Type) is
   begin
      Check_Cursors (Container);
      Check_Length (Container, 1);
      Insert_At (Container, Container.Last + 1, New_Item, 1);
   end Append_One;

   procedure Insert_Space (Container : in out Vector;
                           Before    : in     Extended_Index;
                           Count     : in     Count_Type := 1) is
   begin
      Check_Cursors (Container);
      Check_Before (Container, Before);
      Check_Length (Container, Count);
      if Count > 0 then
         Grow (Container, Container.Last + Count);
         Open_Gap (Container, Count_Type (Pos (Before)), Count);
      end if;
   end Insert_Space;

   procedure Insert_Space (Container : in out Vector;
                           Before    : in     Cursor;
                           Position  :    out Cursor;
                           Count     : in     Count_Type := 1)
   is
      P : Count_Type;
   begin
      Check_Cursors (Container);
      Check_Before (Container, Before);
      Check_Length (Container, Count);
      P := Before_Position (Container, Before);
      if Count > 0 then
         Grow (Container, Container.Last + Count);
         Open_Gap (Container, P, Count);
      end if;
      Position := (if P <= Container.Last then (Var (Container), P)
                   else No_Element);
   end Insert_Space;

   --  Deletes up to Count elements starting at position P (1 .. Last + 1).
   procedure Delete_At (Container : in out Vector; P, Count : Count_Type) is
      L : constant Count_Type := Container.Last;
      N : Count_Type;
   begin
      if Count = 0 or else P > L then
         return;
      end if;
      N := Count_Type'Min (Count, L - P + 1);
      Container.Elements (P .. L - N) := Container.Elements (P + N .. L);
      Container.Last := L - N;
   end Delete_At;

   procedure Delete (Container : in out Vector;
                     Index     : in     Extended_Index;
                     Count     : in     Count_Type := 1) is
   begin
      Check_Cursors (Container);
      Check_Before (Container, Index);
      Delete_At (Container, Count_Type (Pos (Index)), Count);
   end Delete;

   procedure Delete (Container : in out Vector;
                     Position  : in out Cursor;
                     Count     : in     Count_Type := 1) is
   begin
      Check_Cursors (Container);
      Check_Position (Container, Position);
      Delete_At (Container, Position.Index, Count);
      Position := No_Element;
   end Delete;

   procedure Delete_First (Container : in out Vector;
                           Count     : in     Count_Type := 1) is
   begin
      Check_Cursors (Container);
      Delete_At (Container, 1, Count);
   end Delete_First;

   procedure Delete_Last (Container : in out Vector;
                          Count     : in     Count_Type := 1) is
   begin
      Check_Cursors (Container);
      if Container.Last <= Count then
         Clear (Container);
      else
         Delete_At (Container, Container.Last - Count + 1, Count);
      end if;
   end Delete_Last;

   procedure Swap_At (Container : in out Vector; I, J : Count_Type) is
   begin
      if I /= J then
         declare
            T : constant Element_Type := Container.Elements (I);
         begin
            Container.Elements (I) := Container.Elements (J);
            Container.Elements (J) := T;
         end;
      end if;
   end Swap_At;

   procedure Reverse_Elements (Container : in out Vector) is
      I : Count_Type := 1;
      J : Count_Type := Container.Last;
   begin
      Check_Cursors (Container);
      while I < J loop
         Swap_At (Container, I, J);
         I := I + 1;
         J := J - 1;
      end loop;
   end Reverse_Elements;

   procedure Swap (Container : in out Vector;
                   I, J      : in     Index_Type) is
   begin
      Check_Elements (Container);
      Check_Index (Container, I);
      Check_Index (Container, J);
      Swap_At (Container, Count_Type (Pos (I)), Count_Type (Pos (J)));
   end Swap;

   procedure Swap (Container : in out Vector;
                   I, J      : in     Cursor) is
   begin
      Check_Elements (Container);
      if I.Container = null or else J.Container = null then
         raise Constraint_Error with "cursor has no element";
      end if;
      Check_Position (Container, I);
      Check_Position (Container, J);
      Swap_At (Container, I.Index, J.Index);
   end Swap;

   function First_Index (Container : Vector) return Index_Type is
     (Index_Type'First);

   function First (Container : Vector) return Cursor is
     (if Container.Last = 0 then No_Element else (Var (Container), 1));

   function First_Element (Container : Vector)
      return Element_Type is
   begin
      if Container.Last = 0 then
         raise Constraint_Error with "vector is empty";
      end if;
      return Container.Elements (1);
   end First_Element;

   function Last_Index (Container : Vector) return Extended_Index is
     (Idx (Container.Last));

   function Last (Container : Vector) return Cursor is
     (if Container.Last = 0 then No_Element
      else (Var (Container), Container.Last));

   function Last_Element (Container : Vector)
      return Element_Type is
   begin
      if Container.Last = 0 then
         raise Constraint_Error with "vector is empty";
      end if;
      return Container.Elements (Container.Last);
   end Last_Element;

   function Next (Position : Cursor) return Cursor is
   begin
      if Position.Container = null
        or else Position.Index >= Position.Container.Last
      then
         return No_Element;
      end if;
      return (Position.Container, Position.Index + 1);
   end Next;

   function Next (Container : Vector; Position : Cursor) return Cursor is
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

   procedure Next (Container : in     Vector;
                   Position  : in out Cursor) is
   begin
      Position := Next (Container, Position);
   end Next;

   function Previous (Position : Cursor) return Cursor is
   begin
      if Position.Container = null
        or else Position.Index <= 1
        or else Position.Index > Position.Container.Last
      then
         return No_Element;
      end if;
      return (Position.Container, Position.Index - 1);
   end Previous;

   function Previous (Container : Vector;
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

   procedure Previous (Container : in     Vector;
                       Position  : in out Cursor) is
   begin
      Position := Previous (Container, Position);
   end Previous;

   --  The position of the first element equal to Item at or after From
   --  (0 if none); the search prohibits tampering with elements.
   function Search (Container : Vector; Item : Element_Type;
                    From : Count_Type) return Count_Type
   is
      Guard : Lock_Guard (Var (Container));
   begin
      for P in From .. Container.Last loop
         if Container.Elements (P) = Item then
            return P;
         end if;
      end loop;
      return 0;
   end Search;

   --  Likewise searching backwards from From.
   function Reverse_Search (Container : Vector; Item : Element_Type;
                            From : Count_Type) return Count_Type
   is
      Guard : Lock_Guard (Var (Container));
   begin
      for P in reverse 1 .. Count_Type'Min (From, Container.Last) loop
         if Container.Elements (P) = Item then
            return P;
         end if;
      end loop;
      return 0;
   end Reverse_Search;

   function Find_Index (Container : Vector;
                        Item      : Element_Type;
                        Index     : Index_Type := Index_Type'First)
      return Extended_Index
   is
      From : constant Int := Pos (Index);
      P    : Count_Type;
   begin
      if From > Int (Container.Last) then
         return No_Index;
      end if;
      P := Search (Container, Item, Count_Type (From));
      return (if P = 0 then No_Index else Idx (P));
   end Find_Index;

   function Find (Container : Vector;
                  Item      : Element_Type;
                  Position  : Cursor := No_Element)
      return Cursor
   is
      P : Count_Type;
   begin
      if Position.Container /= null then
         Check_Position (Container, Position);
      end if;
      P := Search (Container, Item,
                   (if Position.Container = null then 1 else Position.Index));
      return (if P = 0 then No_Element else (Var (Container), P));
   end Find;

   function Reverse_Find_Index (Container : Vector;
                                Item      : Element_Type;
                                Index     : Index_Type := Index_Type'Last)
      return Extended_Index
   is
      From : constant Int := Pos (Index);
      P    : Count_Type;
   begin
      P := Reverse_Search
        (Container, Item,
         (if From > Int (Container.Last) then Container.Last
          else Count_Type (From)));
      return (if P = 0 then No_Index else Idx (P));
   end Reverse_Find_Index;

   function Reverse_Find (Container : Vector;
                          Item      : Element_Type;
                          Position  : Cursor := No_Element)
      return Cursor
   is
      P : Count_Type;
   begin
      if Position.Container /= null then
         Check_Position (Container, Position);
      end if;
      P := Reverse_Search
        (Container, Item,
         (if Position.Container = null then Container.Last
          else Position.Index));
      return (if P = 0 then No_Element else (Var (Container), P));
   end Reverse_Find;

   function Contains (Container : Vector;
                      Item      : Element_Type) return Boolean is
     (Search (Container, Item, 1) /= 0);

   procedure Iterate
     (Container : in Vector;
      Process   : not null access procedure (Position : in Cursor))
   is
      C     : constant Vector_Access := Var (Container);
      Guard : Busy_Guard (C);
   begin
      for P in 1 .. Container.Last loop
         Process ((C, P));
      end loop;
   end Iterate;

   procedure Reverse_Iterate
     (Container : in Vector;
      Process   : not null access procedure (Position : in Cursor))
   is
      C     : constant Vector_Access := Var (Container);
      Guard : Busy_Guard (C);
   begin
      for P in reverse 1 .. Container.Last loop
         Process ((C, P));
      end loop;
   end Reverse_Iterate;

   function Iterate (Container : in Vector)
      return Vector_Iterator_Interfaces.Parallel_Reversible_Iterator'Class
   is
      C : constant Vector_Access := Var (Container);
   begin
      return It : Iterator do
         It.Container := C;
         C.Busy := C.Busy + 1;
      end return;
   end Iterate;

   function Iterate (Container : in Vector; Start : in Cursor)
      return Vector_Iterator_Interfaces.Reversible_Iterator'Class
   is
      C : constant Vector_Access := Var (Container);
   begin
      Check_Position (Container, Start);
      return It : Iterator do
         It.Container := C;
         It.Start := Start.Index;
         C.Busy := C.Busy + 1;
      end return;
   end Iterate;

   package body Generic_Sorting is

      function Is_Sorted (Container : Vector) return Boolean is
      begin
         for P in 2 .. Container.Last loop
            if Container.Elements (P) < Container.Elements (P - 1) then
               return False;
            end if;
         end loop;
         return True;
      end Is_Sorted;

      procedure Sort (Container : in out Vector) is
         C : constant Vector_Access := Var (Container);

         function Before (Left, Right : Count_Type) return Boolean is
           (C.Elements (Left) < C.Elements (Right));

         procedure Swap (Left, Right : Count_Type) is
         begin
            Swap_At (C.all, Left, Right);
         end Swap;

         procedure Sort_Positions is new Ada.Containers.Generic_Sort
           (Count_Type, Before, Swap);
      begin
         Check_Elements (Container);
         if Container.Last > 1 then
            Sort_Positions (1, Container.Last);
         end if;
      end Sort;

      procedure Merge (Target  : in out Vector;
                       Source  : in out Vector)
      is
         I, J, K : Count_Type;
      begin
         Check_Cursors (Target);
         Check_Cursors (Source);
         if Source.Last = 0 then
            return;
         elsif Var (Target) = Var (Source) then
            raise Program_Error with "Merge of a nonempty container with itself";
         end if;
         Check_Length (Target, Source.Last);
         if Source.Last = 0 then
            return;
         end if;
         Grow (Target, Target.Last + Source.Last);
         I := Target.Last;
         J := Source.Last;
         K := I + J;
         --  Merge from the back so that the elements of Target are not
         --  overwritten before they are moved.
         while J > 0 loop
            if I > 0 and then
              Source.Elements (J) < Target.Elements (I)
            then
               Target.Elements (K) := Target.Elements (I);
               I := I - 1;
            else
               Target.Elements (K) := Source.Elements (J);
               J := J - 1;
            end if;
            K := K - 1;
         end loop;
         Target.Last := Target.Last + Source.Last;
         Source.Last := 0;
      end Merge;

   end Generic_Sorting;


   procedure Insert_Vector (Container : in out Vector;
                            Before    : in     Extended_Index;
                            New_Item  : in     Vector) is
   begin
      Insert (Container, Before, New_Item);
   end Insert_Vector;

   procedure Insert_Vector (Container : in out Vector;
                            Before    : in     Cursor;
                            New_Item  : in     Vector) is
   begin
      Insert (Container, Before, New_Item);
   end Insert_Vector;

   procedure Insert_Vector (Container : in out Vector;
                            Before    : in     Cursor;
                            New_Item  : in     Vector;
                            Position  :    out Cursor) is
   begin
      Insert (Container, Before, New_Item, Position);
   end Insert_Vector;

   procedure Prepend_Vector (Container : in out Vector;
                             New_Item  : in     Vector) is
   begin
      Prepend (Container, New_Item);
   end Prepend_Vector;

   procedure Append_Vector (Container : in out Vector;
                            New_Item  : in     Vector) is
   begin
      Append (Container, New_Item);
   end Append_Vector;

end Ada.Containers.Bounded_Vectors;
