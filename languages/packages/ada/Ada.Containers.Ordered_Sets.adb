--  Ada.Containers.Ordered_Sets.adb
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

package body Ada.Containers.Ordered_Sets is

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

   ---------------------------------------------------------------------
   --  Red-black tree (nodes linked by index; 0 is the null node)
   ---------------------------------------------------------------------

   function Is_Red (Container : Set; X : Count_Type) return Boolean is
     (X /= 0 and then Container.Nodes (X).Red);

   function Min_Node (Container : Set; X : Count_Type) return Count_Type is
      N : Count_Type := X;
   begin
      if N = 0 then
         return 0;
      end if;
      while Container.Nodes (N).Left /= 0 loop
         N := Container.Nodes (N).Left;
      end loop;
      return N;
   end Min_Node;

   function Max_Node (Container : Set; X : Count_Type) return Count_Type is
      N : Count_Type := X;
   begin
      if N = 0 then
         return 0;
      end if;
      while Container.Nodes (N).Right /= 0 loop
         N := Container.Nodes (N).Right;
      end loop;
      return N;
   end Max_Node;

   function Next_Node (Container : Set; X : Count_Type) return Count_Type is
      N : Count_Type := X;
      P : Count_Type;
   begin
      if Container.Nodes (N).Right /= 0 then
         return Min_Node (Container, Container.Nodes (N).Right);
      end if;
      P := Container.Nodes (N).Parent;
      while P /= 0 and then Container.Nodes (P).Right = N loop
         N := P;
         P := Container.Nodes (P).Parent;
      end loop;
      return P;
   end Next_Node;

   function Previous_Node (Container : Set; X : Count_Type)
      return Count_Type
   is
      N : Count_Type := X;
      P : Count_Type;
   begin
      if Container.Nodes (N).Left /= 0 then
         return Max_Node (Container, Container.Nodes (N).Left);
      end if;
      P := Container.Nodes (N).Parent;
      while P /= 0 and then Container.Nodes (P).Left = N loop
         N := P;
         P := Container.Nodes (P).Parent;
      end loop;
      return P;
   end Previous_Node;

   procedure Rotate_Left (Container : in out Set; X : Count_Type) is
      Y : constant Count_Type := Container.Nodes (X).Right;
      P : constant Count_Type := Container.Nodes (X).Parent;
      B : constant Count_Type := Container.Nodes (Y).Left;
   begin
      Container.Nodes (X).Right := B;
      if B /= 0 then
         Container.Nodes (B).Parent := X;
      end if;
      Container.Nodes (Y).Parent := P;
      if P = 0 then
         Container.Root := Y;
      elsif Container.Nodes (P).Left = X then
         Container.Nodes (P).Left := Y;
      else
         Container.Nodes (P).Right := Y;
      end if;
      Container.Nodes (Y).Left := X;
      Container.Nodes (X).Parent := Y;
   end Rotate_Left;

   procedure Rotate_Right (Container : in out Set; X : Count_Type) is
      Y : constant Count_Type := Container.Nodes (X).Left;
      P : constant Count_Type := Container.Nodes (X).Parent;
      B : constant Count_Type := Container.Nodes (Y).Right;
   begin
      Container.Nodes (X).Left := B;
      if B /= 0 then
         Container.Nodes (B).Parent := X;
      end if;
      Container.Nodes (Y).Parent := P;
      if P = 0 then
         Container.Root := Y;
      elsif Container.Nodes (P).Right = X then
         Container.Nodes (P).Right := Y;
      else
         Container.Nodes (P).Left := Y;
      end if;
      Container.Nodes (Y).Right := X;
      Container.Nodes (X).Parent := Y;
   end Rotate_Right;

   --  Links the unlinked node Z as a leaf below Parent (0: as the root),
   --  on the left if As_Left, and rebalances.
   procedure Link_Node (Container : in out Set;
                        Z         : Count_Type;
                        Parent    : Count_Type;
                        As_Left   : Boolean)
   is
      X : Count_Type := Z;
      P, G, U : Count_Type;
   begin
      Container.Nodes (Z).Parent := Parent;
      Container.Nodes (Z).Left := 0;
      Container.Nodes (Z).Right := 0;
      Container.Nodes (Z).Red := True;
      if Parent = 0 then
         Container.Root := Z;
      elsif As_Left then
         Container.Nodes (Parent).Left := Z;
      else
         Container.Nodes (Parent).Right := Z;
      end if;
      Container.Length := Container.Length + 1;

      while X /= Container.Root
        and then Is_Red (Container, Container.Nodes (X).Parent)
      loop
         P := Container.Nodes (X).Parent;
         G := Container.Nodes (P).Parent;
         if P = Container.Nodes (G).Left then
            U := Container.Nodes (G).Right;
            if Is_Red (Container, U) then
               Container.Nodes (P).Red := False;
               Container.Nodes (U).Red := False;
               Container.Nodes (G).Red := True;
               X := G;
            else
               if X = Container.Nodes (P).Right then
                  X := P;
                  Rotate_Left (Container, X);
                  P := Container.Nodes (X).Parent;
               end if;
               Container.Nodes (P).Red := False;
               Container.Nodes (G).Red := True;
               Rotate_Right (Container, G);
            end if;
         else
            U := Container.Nodes (G).Left;
            if Is_Red (Container, U) then
               Container.Nodes (P).Red := False;
               Container.Nodes (U).Red := False;
               Container.Nodes (G).Red := True;
               X := G;
            else
               if X = Container.Nodes (P).Left then
                  X := P;
                  Rotate_Right (Container, X);
                  P := Container.Nodes (X).Parent;
               end if;
               Container.Nodes (P).Red := False;
               Container.Nodes (G).Red := True;
               Rotate_Left (Container, G);
            end if;
         end if;
      end loop;
      Container.Nodes (Container.Root).Red := False;
   end Link_Node;

   --  Replaces the subtree rooted at U by the one rooted at V.
   procedure Transplant (Container : in out Set; U, V : Count_Type) is
      P : constant Count_Type := Container.Nodes (U).Parent;
   begin
      if P = 0 then
         Container.Root := V;
      elsif Container.Nodes (P).Left = U then
         Container.Nodes (P).Left := V;
      else
         Container.Nodes (P).Right := V;
      end if;
      if V /= 0 then
         Container.Nodes (V).Parent := P;
      end if;
   end Transplant;

   --  Unlinks node Z from the tree and rebalances; the other nodes keep
   --  their identity.
   procedure Unlink_Node (Container : in out Set; Z : Count_Type) is
      Y        : Count_Type := Z;
      Y_Red    : Boolean := Container.Nodes (Z).Red;
      X, XP, W : Count_Type;
   begin
      if Container.Nodes (Z).Left = 0 then
         X := Container.Nodes (Z).Right;
         XP := Container.Nodes (Z).Parent;
         Transplant (Container, Z, X);
      elsif Container.Nodes (Z).Right = 0 then
         X := Container.Nodes (Z).Left;
         XP := Container.Nodes (Z).Parent;
         Transplant (Container, Z, X);
      else
         Y := Min_Node (Container, Container.Nodes (Z).Right);
         Y_Red := Container.Nodes (Y).Red;
         X := Container.Nodes (Y).Right;
         if Container.Nodes (Y).Parent = Z then
            XP := Y;
         else
            XP := Container.Nodes (Y).Parent;
            Transplant (Container, Y, X);
            Container.Nodes (Y).Right := Container.Nodes (Z).Right;
            Container.Nodes (Container.Nodes (Y).Right).Parent := Y;
         end if;
         Transplant (Container, Z, Y);
         Container.Nodes (Y).Left := Container.Nodes (Z).Left;
         Container.Nodes (Container.Nodes (Y).Left).Parent := Y;
         Container.Nodes (Y).Red := Container.Nodes (Z).Red;
      end if;
      Container.Length := Container.Length - 1;
      Container.Nodes (Z).Parent := 0;
      Container.Nodes (Z).Left := 0;
      Container.Nodes (Z).Right := 0;

      if Y_Red then
         return;
      end if;
      while X /= Container.Root and then not Is_Red (Container, X) loop
         if X = Container.Nodes (XP).Left then
            W := Container.Nodes (XP).Right;
            if Is_Red (Container, W) then
               Container.Nodes (W).Red := False;
               Container.Nodes (XP).Red := True;
               Rotate_Left (Container, XP);
               W := Container.Nodes (XP).Right;
            end if;
            if not Is_Red (Container, Container.Nodes (W).Left)
              and then not Is_Red (Container, Container.Nodes (W).Right)
            then
               Container.Nodes (W).Red := True;
               X := XP;
               XP := Container.Nodes (X).Parent;
            else
               if not Is_Red (Container, Container.Nodes (W).Right) then
                  Container.Nodes (Container.Nodes (W).Left).Red := False;
                  Container.Nodes (W).Red := True;
                  Rotate_Right (Container, W);
                  W := Container.Nodes (XP).Right;
               end if;
               Container.Nodes (W).Red := Container.Nodes (XP).Red;
               Container.Nodes (XP).Red := False;
               if Container.Nodes (W).Right /= 0 then
                  Container.Nodes (Container.Nodes (W).Right).Red := False;
               end if;
               Rotate_Left (Container, XP);
               X := Container.Root;
            end if;
         else
            W := Container.Nodes (XP).Left;
            if Is_Red (Container, W) then
               Container.Nodes (W).Red := False;
               Container.Nodes (XP).Red := True;
               Rotate_Right (Container, XP);
               W := Container.Nodes (XP).Left;
            end if;
            if not Is_Red (Container, Container.Nodes (W).Right)
              and then not Is_Red (Container, Container.Nodes (W).Left)
            then
               Container.Nodes (W).Red := True;
               X := XP;
               XP := Container.Nodes (X).Parent;
            else
               if not Is_Red (Container, Container.Nodes (W).Left) then
                  Container.Nodes (Container.Nodes (W).Right).Red := False;
                  Container.Nodes (W).Red := True;
                  Rotate_Left (Container, W);
                  W := Container.Nodes (XP).Left;
               end if;
               Container.Nodes (W).Red := Container.Nodes (XP).Red;
               Container.Nodes (XP).Red := False;
               if Container.Nodes (W).Left /= 0 then
                  Container.Nodes (Container.Nodes (W).Left).Red := False;
               end if;
               Rotate_Right (Container, XP);
               X := Container.Root;
            end if;
         end if;
      end loop;
      if X /= 0 then
         Container.Nodes (X).Red := False;
      end if;
   end Unlink_Node;

   --  Takes a node from the pool (growing it in the unbounded forms); the
   --  node is not linked and its key and element are not set.
   procedure Allocate (Container : in out Set; Node : out Count_Type) is
   begin
      if Container.Free /= 0 then
         Node := Container.Free;
         Container.Free := Container.Nodes (Node).Right;
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
                  raise Capacity_Error with "container capacity exceeded";
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
      Container.Nodes (Node).Parent := 0;
      Container.Nodes (Node).Left := 0;
      Container.Nodes (Node).Right := 0;
      Container.Nodes (Node).Used := True;
   end Allocate;

   --  Returns an unlinked node to the pool.
   procedure Release (Container : in out Set; Node : Count_Type) is
   begin
      Container.Nodes (Node).Used := False;
      Container.Nodes (Node).Parent := 0;
      Container.Nodes (Node).Left := 0;
      Container.Nodes (Node).Right := Container.Free;
      Container.Free := Node;
   end Release;

   ---------------------------------------------------------------------
   --  Searching
   ---------------------------------------------------------------------

   --  Finds the node whose element is equivalent to Item (Node, 0 if none)
   --  and otherwise the place where such a node would be linked.
   procedure Search (Container : Set;
                     Item      : Element_Type;
                     Node      : out Count_Type;
                     Parent    : out Count_Type;
                     As_Left   : out Boolean)
   is
      N : Count_Type := Container.Root;
   begin
      Node := 0;
      Parent := 0;
      As_Left := True;
      while N /= 0 loop
         if Item < Container.Nodes (N).Element.all then
            Parent := N;
            As_Left := True;
            N := Container.Nodes (N).Left;
         elsif Container.Nodes (N).Element.all < Item then
            Parent := N;
            As_Left := False;
            N := Container.Nodes (N).Right;
         else
            Node := N;
            return;
         end if;
      end loop;
   end Search;

   function Find_Node (Container : Set; Item : Element_Type)
      return Count_Type
   is
      Node, Parent : Count_Type;
      As_Left      : Boolean;
   begin
      Search (Container, Item, Node, Parent, As_Left);
      return Node;
   end Find_Node;

   --  Unlinks a node and returns it to the pool (finalizing its element
   --  in the unbounded and indefinite forms).
   procedure Deallocate (Container : in out Set; Node : Count_Type) is
   begin
      Unlink_Node (Container, Node);
      Free (Container.Nodes (Node).Element);
      Release (Container, Node);
   end Deallocate;

   --  Inserts a new node holding a copy of New_Item at the place found by
   --  Search; the container is not modified if the copy fails.
   procedure Insert_New (Container : in out Set;
                         New_Item  : Element_Type;
                         Parent    : Count_Type;
                         As_Left   : Boolean;
                         Node      : out Count_Type) is
      E : Element_Access := new Element_Type'(New_Item);
   begin
      if Container.Length = Count_Type'Last then
         Free (E);
         raise Constraint_Error with "set would be too long";
      end if;
      begin
         Allocate (Container, Node);
      exception
         when others =>
            Free (E);
            raise;
      end;
      Container.Nodes (Node).Element := E;
      Link_Node (Container, Node, Parent, As_Left);
   end Insert_New;

   --  Inserts a copy of Item if no equivalent element is present.
   procedure Insert_If_Absent (Container : in out Set; Item : Element_Type)
   is
      N, Parent : Count_Type;
      As_Left   : Boolean;
   begin
      Search (Container, Item, N, Parent, As_Left);
      if N = 0 then
         Insert_New (Container, Item, Parent, As_Left, N);
      end if;
   end Insert_If_Absent;

   --  Removes all the nodes and resets the pool.
   procedure Clear_All (Container : in out Set) is
   begin
      for N in 1 .. Container.High loop
         Free (Container.Nodes (N).Element);
         Container.Nodes (N).Used := False;
      end loop;
      Container.Root := 0;
      Container.Length := 0;
      Container.Free := 0;
      Container.High := 0;
   end Clear_All;

   function First_Node (Container : Set) return Count_Type is
     (Min_Node (Container, Container.Root));

   function Last_Node (Container : Set) return Count_Type is
     (Max_Node (Container, Container.Root));

   --  Appends a copy of Item, which is greater than all the elements of
   --  Target; Last is the last node of Target (0 if empty) and is updated.
   procedure Append_Node (Target : in out Set;
                          Item   : Element_Type;
                          Last   : in out Count_Type)
   is
      Node : Count_Type;
   begin
      Insert_New (Target, Item, Last, False, Node);
      Last := Node;
   end Append_Node;

   --  Inserts a copy of each element of Source into Target (empty).
   procedure Insert_All (Target : in out Set; Source : Set) is
      N    : Count_Type := First_Node (Source);
      Last : Count_Type := 0;
   begin
      while N /= 0 loop
         Append_Node (Target, Source.Nodes (N).Element.all, Last);
         N := Next_Node (Source, N);
      end loop;
   end Insert_All;

   ---------------------------------------------------------------------
   --  Controlled operations
   ---------------------------------------------------------------------

   --  Makes a deep copy with the same layout.
   overriding procedure Adjust (Container : in out Set) is
      Nodes : constant Node_Array_Access := Container.Nodes;
   begin
      Container.Busy := 0;
      Container.Lock := 0;
      Container.Nodes := null;
      if Nodes = null then
         Container.Root := 0;
         Container.Length := 0;
         Container.Free := 0;
         Container.High := 0;
         return;
      end if;
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
      Container.Root := 0;
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
     and Set_Iterator_Interfaces.Parallel_Reversible_Iterator with
   record
      Container : Set_Access;
      Start     : Count_Type := 0;     --  0: the whole container
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
      Max_Chunks : Set_Iterator_Interfaces.Chunk_Index);
   overriding function Chunk_Count
     (Object : Iterator) return Set_Iterator_Interfaces.Chunk_Index;
   overriding function First
     (Object : Iterator; Chunk : Set_Iterator_Interfaces.Chunk_Index) return Cursor;
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
     (Make_Cursor (Object.Container,
                   (if Object.Start /= 0 then Object.Start
                    else First_Node (Object.Container.all))));

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

   overriding function Last (Object : Iterator) return Cursor is
     (Make_Cursor (Object.Container,
                   (if Object.Start /= 0 then Object.Start
                    else Last_Node (Object.Container.all))));

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
     (Object : Iterator; Chunk : Set_Iterator_Interfaces.Chunk_Index) return Cursor is
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

   function Equivalent_Elements (Left, Right : Element_Type) return Boolean is
     (not (Left < Right) and then not (Right < Left));

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
      M := First_Node (Right);
      while N /= 0 loop
         if not (Left.Nodes (N).Element.all = Right.Nodes (M).Element.all) then
            return False;
         end if;
         N := Next_Node (Left, N);
         M := Next_Node (Right, M);
      end loop;
      return True;
   end "=";

   function Equivalent_Sets (Left, Right : Set) return Boolean is
      N, M : Count_Type;
   begin
      if Var (Left) = Var (Right) then
         return True;
      elsif Left.Length /= Right.Length then
         return False;
      end if;
      N := First_Node (Left);
      M := First_Node (Right);
      while N /= 0 loop
         if not Equivalent_Elements
           (Left.Nodes (N).Element.all, Right.Nodes (M).Element.all)
         then
            return False;
         end if;
         N := Next_Node (Left, N);
         M := Next_Node (Right, M);
      end loop;
      return True;
   end Equivalent_Sets;

   function Tampering_With_Cursors_Prohibited
      (Container : Set) return Boolean is
     (Container.Busy > 0 or else Container.Lock > 0);

   function To_Set (New_Item : Element_Type) return Set is
   begin
      return Result : Set do
         Insert (Result, New_Item);
      end return;
   end To_Set;

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
   --  if the new element is not equivalent to the old one; Program_Error
   --  if another element is equivalent to New_Item.
   procedure Replace_Node (Container : in out Set;
                           Node      : Count_Type;
                           New_Item  : Element_Type)
   is
      N, Parent : Count_Type;
      As_Left   : Boolean;
   begin
      if Equivalent_Elements (Container.Nodes (Node).Element.all, New_Item) then
         declare
            X : constant Element_Access := new Element_Type'(New_Item);
         begin
            Free (Container.Nodes (Node).Element);
            Container.Nodes (Node).Element := X;
         end;
         return;
      end if;
      Search (Container, New_Item, N, Parent, As_Left);
      if N /= 0 then
         raise Program_Error with "an equivalent element is already in the set";
      end if;
      declare
         X : constant Element_Access := new Element_Type'(New_Item);
      begin
         Unlink_Node (Container, Node);
         Free (Container.Nodes (Node).Element);
         Container.Nodes (Node).Element := X;
      end;
      --  The place found above may have been changed by the unlinking.
      Search (Container, New_Item, N, Parent, As_Left);
      Link_Node (Container, Node, Parent, As_Left);
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
      Insert_All (Target, Source);
   end Assign;

   function Copy (Source : Set) return Set is
   begin
      return Result : Set do
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
         Nodes : constant Node_Array_Access := Target.Nodes;
      begin
         Target.Nodes := Source.Nodes;
         Target.Root := Source.Root;
         Target.Length := Source.Length;
         Target.Free := Source.Free;
         Target.High := Source.High;
         Source.Nodes := Nodes;
         Source.Root := 0;
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
      N, Parent : Count_Type;
      As_Left   : Boolean;
   begin
      Check_Cursors (Container);
      Search (Container, New_Item, N, Parent, As_Left);
      if N /= 0 then
         Position := (Var (Container), N);
         Inserted := False;
         return;
      end if;
      Insert_New (Container, New_Item, Parent, As_Left, N);
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

   procedure Delete_First (Container : in out Set) is
   begin
      Check_Cursors (Container);
      if Container.Length > 0 then
         Deallocate (Container, First_Node (Container));
      end if;
   end Delete_First;

   procedure Delete_Last (Container : in out Set) is
   begin
      Check_Cursors (Container);
      if Container.Length > 0 then
         Deallocate (Container, Last_Node (Container));
      end if;
   end Delete_Last;

   ---------------------------------------------------------------------
   --  Set operations
   ---------------------------------------------------------------------

   procedure Union (Target : in out Set;
                    Source : in     Set)
   is
      N : Count_Type;
   begin
      Check_Cursors (Target);
      if Var (Target) = Var (Source) then
         return;
      end if;
      N := First_Node (Source);
      while N /= 0 loop
         Insert_If_Absent (Target, Source.Nodes (N).Element.all);
         N := Next_Node (Source, N);
      end loop;
   end Union;

   --  The kinds of merge of two ordered sets.
   type Operation is (Op_Union, Op_Intersection, Op_Difference,
                      Op_Symmetric_Difference);

   --  Walks Left and Right in order, calling Emit for each element that
   --  belongs to the result of Op (Count only counts them).
   generic
      with procedure Emit (Item : Element_Type);
   procedure Merge_Walk (Left, Right : Set; Op : Operation);

   procedure Merge_Walk (Left, Right : Set; Op : Operation) is
      L : Count_Type := First_Node (Left);
      R : Count_Type := First_Node (Right);
   begin
      while L /= 0 or else R /= 0 loop
         if R = 0
           or else (L /= 0
                    and then Left.Nodes (L).Element.all < Right.Nodes (R).Element.all)
         then
            if Op /= Op_Intersection then
               Emit (Left.Nodes (L).Element.all);
            end if;
            L := Next_Node (Left, L);
         elsif L = 0
           or else Right.Nodes (R).Element.all < Left.Nodes (L).Element.all
         then
            if Op in Op_Union | Op_Symmetric_Difference then
               Emit (Right.Nodes (R).Element.all);
            end if;
            R := Next_Node (Right, R);
         else
            if Op in Op_Union | Op_Intersection then
               Emit (Left.Nodes (L).Element.all);
            end if;
            L := Next_Node (Left, L);
            R := Next_Node (Right, R);
         end if;
      end loop;
   end Merge_Walk;

   function Merge (Left, Right : Set; Op : Operation) return Set is
      Count : Count_Type := 0;
      procedure Count_One (Item : Element_Type) is
      begin
         Count := Count + 1;
      end Count_One;
      procedure Count_All is new Merge_Walk (Count_One);
   begin
      Count_All (Left, Right, Op);
      return Result : Set do
         declare
            Last : Count_Type := 0;
            procedure Add (Item : Element_Type) is
            begin
               Append_Node (Result, Item, Last);
            end Add;
            procedure Add_All is new Merge_Walk (Add);
         begin
            Add_All (Left, Right, Op);
         end;
      end return;
   end Merge;

   function Union (Left, Right : Set) return Set is
     (Merge (Left, Right, Op_Union));

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
         if Find_Node (Source, Target.Nodes (N).Element.all) = 0 then
            Deallocate (Target, N);
         end if;
         N := M;
      end loop;
   end Intersection;

   function Intersection (Left, Right : Set) return Set is
     (Merge (Left, Right, Op_Intersection));

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
         if Find_Node (Source, Target.Nodes (N).Element.all) /= 0 then
            Deallocate (Target, N);
         end if;
         N := M;
      end loop;
   end Difference;

   function Difference (Left, Right : Set) return Set is
     (Merge (Left, Right, Op_Difference));

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
         T := Find_Node (Target, Source.Nodes (N).Element.all);
         if T /= 0 then
            Deallocate (Target, T);
         else
            Insert_If_Absent (Target, Source.Nodes (N).Element.all);
         end if;
         N := Next_Node (Source, N);
      end loop;
   end Symmetric_Difference;

   function Symmetric_Difference (Left, Right : Set) return Set is
     (Merge (Left, Right, Op_Symmetric_Difference));

   function Overlap (Left, Right : Set) return Boolean is
      N : Count_Type := First_Node (Left);
   begin
      while N /= 0 loop
         if Find_Node (Right, Left.Nodes (N).Element.all) /= 0 then
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
         if Find_Node (Of_Set, Subset.Nodes (N).Element.all) = 0 then
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

   function First_Element (Container : Set)
      return Element_Type is
   begin
      if Container.Length = 0 then
         raise Constraint_Error with "set is empty";
      end if;
      return Container.Nodes (First_Node (Container)).Element.all;
   end First_Element;

   function Last (Container : Set) return Cursor is
     (Make_Cursor (Var (Container), Last_Node (Container)));

   function Last_Element (Container : Set)
      return Element_Type is
   begin
      if Container.Length = 0 then
         raise Constraint_Error with "set is empty";
      end if;
      return Container.Nodes (Last_Node (Container)).Element.all;
   end Last_Element;

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

   function Previous (Position : Cursor) return Cursor is
   begin
      if not Has_Element (Position) then
         return No_Element;
      end if;
      return Make_Cursor
        (Position.Container,
         Previous_Node (Position.Container.all, Position.Node));
   end Previous;

   function Previous (Container : Set;
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

   procedure Previous (Container : in     Set;
                       Position  : in out Cursor) is
   begin
      Position := Previous (Container, Position);
   end Previous;

   function Find (Container : Set;
                  Item      : Element_Type) return Cursor is
     (Make_Cursor (Var (Container), Find_Node (Container, Item)));

   function Floor (Container : Set;
                   Item      : Element_Type) return Cursor
   is
      N : Count_Type := Container.Root;
      R : Count_Type := 0;
   begin
      while N /= 0 loop
         if Item < Container.Nodes (N).Element.all then
            N := Container.Nodes (N).Left;
         else
            R := N;
            N := Container.Nodes (N).Right;
         end if;
      end loop;
      return Make_Cursor (Var (Container), R);
   end Floor;

   function Ceiling (Container : Set;
                     Item      : Element_Type) return Cursor
   is
      N : Count_Type := Container.Root;
      R : Count_Type := 0;
   begin
      while N /= 0 loop
         if Container.Nodes (N).Element.all < Item then
            N := Container.Nodes (N).Right;
         else
            R := N;
            N := Container.Nodes (N).Left;
         end if;
      end loop;
      return Make_Cursor (Var (Container), R);
   end Ceiling;

   function Contains (Container : Set;
                      Item      : Element_Type) return Boolean is
     (Find_Node (Container, Item) /= 0);

   function Element_Of (Position : Cursor) return Element_Type is
   begin
      Check_Has_Element (Position);
      return Position.Container.Nodes (Position.Node).Element.all;
   end Element_Of;

   function "<" (Left, Right : Cursor) return Boolean is
   begin
      Check_Has_Element (Left);
      Check_Has_Element (Right);
      return Element_Of (Left) < Element_Of (Right);
   end "<";

   function ">" (Left, Right : Cursor) return Boolean is
   begin
      Check_Has_Element (Left);
      Check_Has_Element (Right);
      return Element_Of (Right) < Element_Of (Left);
   end ">";

   function "<" (Left : Cursor; Right : Element_Type) return Boolean is
     (Element_Of (Left) < Right);

   function ">" (Left : Cursor; Right : Element_Type) return Boolean is
     (Right < Element_Of (Left));

   function "<" (Left : Element_Type; Right : Cursor) return Boolean is
     (Left < Element_Of (Right));

   function ">" (Left : Element_Type; Right : Cursor) return Boolean is
     (Element_Of (Right) < Left);

   procedure  Iterate
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

   procedure Reverse_Iterate
     (Container : in Set;
      Process   : not null access procedure (Position : in Cursor))
   is
      C     : constant Set_Access := Var (Container);
      Guard : Busy_Guard (C);
      N     : Count_Type := Last_Node (Container);
   begin
      while N /= 0 loop
         Process ((C, N));
         N := Previous_Node (Container, N);
      end loop;
   end Reverse_Iterate;

   function Iterate (Container : in Set)
      return Set_Iterator_Interfaces.Parallel_Reversible_Iterator'Class
   is
      C : constant Set_Access := Var (Container);
   begin
      return It : Iterator do
         It.Container := C;
         C.Busy := C.Busy + 1;
      end return;
   end Iterate;

   function Iterate (Container : in Set; Start : in Cursor)
      return Set_Iterator_Interfaces.Reversible_Iterator'Class
   is
      C : constant Set_Access := Var (Container);
   begin
      Check_Position (Container, Start);
      return It : Iterator do
         It.Container := C;
         It.Start := Start.Node;
         C.Busy := C.Busy + 1;
      end return;
   end Iterate;

   ---------------------------------------------------------------------
   --  Generic_Keys
   ---------------------------------------------------------------------

   package body Generic_Keys is

      function Equivalent_Keys (Left, Right : Key_Type)
         return Boolean is
        (not (Left < Right) and then not (Right < Left));

      --  The node whose element has a key equivalent to K, or 0.
      function Find_Key (Container : Set; K : Key_Type) return Count_Type is
         N : Count_Type := Container.Root;
      begin
         while N /= 0 loop
            declare
               NK : constant Key_Type := Key (Container.Nodes (N).Element.all);
            begin
               if K < NK then
                  N := Container.Nodes (N).Left;
               elsif NK < K then
                  N := Container.Nodes (N).Right;
               else
                  return N;
               end if;
            end;
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

      function Floor (Container : Set;
                      Key       : Key_Type)
        return Cursor
      is
         N : Count_Type := Container.Root;
         R : Count_Type := 0;
      begin
         while N /= 0 loop
            if Key < Generic_Keys.Key (Container.Nodes (N).Element.all) then
               N := Container.Nodes (N).Left;
            else
               R := N;
               N := Container.Nodes (N).Right;
            end if;
         end loop;
         return Make_Cursor (Var (Container), R);
      end Floor;

      function Ceiling (Container : Set;
                        Key       : Key_Type)
        return Cursor
      is
         N : Count_Type := Container.Root;
         R : Count_Type := 0;
      begin
         while N /= 0 loop
            if Generic_Keys.Key (Container.Nodes (N).Element.all) < Key then
               N := Container.Nodes (N).Right;
            else
               R := N;
               N := Container.Nodes (N).Left;
            end if;
         end loop;
         return Make_Cursor (Var (Container), R);
      end Ceiling;

      function Contains (Container : Set;
                         Key       : Key_Type) return Boolean is
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

end Ada.Containers.Ordered_Sets;
