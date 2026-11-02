--  Ada.Containers.Indefinite_Ordered_Maps.adb
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

package body Ada.Containers.Indefinite_Ordered_Maps is

   --  The operations that take the container only through a cursor (or as
   --  an in parameter) still have to record that tampering is prohibited:
   --  the variable view of the container is obtained by converting the
   --  access-to-constant value designating it.
   type Constant_Map_Access is access constant Map;

   function To_Variable is new Ada.Unchecked_Conversion
     (Constant_Map_Access, Map_Access);

   function Var (Container : Map) return Map_Access is
     (To_Variable (Container'Unchecked_Access));

   procedure Free is new Ada.Unchecked_Deallocation
     (Node_Array, Node_Array_Access);
   procedure Free is new Ada.Unchecked_Deallocation
     (Key_Type, Key_Access);
   procedure Free is new Ada.Unchecked_Deallocation
     (Element_Type, Element_Access);

   ---------------------------------------------------------------------
   --  Tampering
   ---------------------------------------------------------------------

   procedure Check_Cursors (Container : Map) is
   begin
      if Container.Busy > 0 or else Container.Lock > 0 then
         raise Program_Error with "attempt to tamper with cursors";
      end if;
   end Check_Cursors;

   procedure Check_Elements (Container : Map) is
   begin
      if Container.Lock > 0 then
         raise Program_Error with "attempt to tamper with elements";
      end if;
   end Check_Elements;

   type Busy_Guard (Container : Map_Access) is
     new Ada.Finalization.Limited_Controlled with null record;
   overriding procedure Initialize (Guard : in out Busy_Guard);
   overriding procedure Finalize (Guard : in out Busy_Guard);

   type Lock_Guard (Container : Map_Access) is
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
   function Lock (Container : Map) return Map_Access is
      C : constant Map_Access := Var (Container);
   begin
      C.Lock := C.Lock + 1;
      return C;
   end Lock;

   ---------------------------------------------------------------------
   --  Checks
   ---------------------------------------------------------------------

   function Valid (Container : Map; Node : Count_Type) return Boolean is
     (Node in 1 .. Container.High and then Container.Nodes (Node).Used);

   --  Position designates an element of Container
   procedure Check_Position (Container : Map; Position : Cursor) is
   begin
      if Position.Container = null then
         raise Constraint_Error with "cursor has no element";
      elsif Position.Container /= Var (Container)
        or else not Valid (Container, Position.Node)
      then
         raise Program_Error with "cursor designates no element of the map";
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

   function Is_Red (Container : Map; X : Count_Type) return Boolean is
     (X /= 0 and then Container.Nodes (X).Red);

   function Min_Node (Container : Map; X : Count_Type) return Count_Type is
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

   function Max_Node (Container : Map; X : Count_Type) return Count_Type is
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

   function Next_Node (Container : Map; X : Count_Type) return Count_Type is
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

   function Previous_Node (Container : Map; X : Count_Type)
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

   procedure Rotate_Left (Container : in out Map; X : Count_Type) is
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

   procedure Rotate_Right (Container : in out Map; X : Count_Type) is
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
   procedure Link_Node (Container : in out Map;
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
   procedure Transplant (Container : in out Map; U, V : Count_Type) is
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
   procedure Unlink_Node (Container : in out Map; Z : Count_Type) is
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
   procedure Allocate (Container : in out Map; Node : out Count_Type) is
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
   procedure Release (Container : in out Map; Node : Count_Type) is
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

   --  Finds the node whose key is equivalent to Key (Node, 0 if none) and
   --  otherwise the place where such a node would be linked.
   procedure Search (Container : Map;
                     Key       : Key_Type;
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
         if Key < Container.Nodes (N).Key.all then
            Parent := N;
            As_Left := True;
            N := Container.Nodes (N).Left;
         elsif Container.Nodes (N).Key.all < Key then
            Parent := N;
            As_Left := False;
            N := Container.Nodes (N).Right;
         else
            Node := N;
            return;
         end if;
      end loop;
   end Search;

   function Find_Node (Container : Map; Key : Key_Type) return Count_Type is
      Node, Parent : Count_Type;
      As_Left      : Boolean;
   begin
      Search (Container, Key, Node, Parent, As_Left);
      return Node;
   end Find_Node;

   --  Unlinks a node and returns it to the pool (finalizing its key and
   --  element in the unbounded and indefinite forms).
   procedure Deallocate (Container : in out Map; Node : Count_Type) is
   begin
      Unlink_Node (Container, Node);
      Free (Container.Nodes (Node).Key);
      Free (Container.Nodes (Node).Element);
      Release (Container, Node);
   end Deallocate;

   --  Inserts a new node for Key and a copy of New_Item at the place found
   --  by Search; the container is not modified if a copy fails.
   procedure Insert_New (Container : in out Map;
                         Key       : Key_Type;
                         New_Item  : Element_Type;
                         Parent    : Count_Type;
                         As_Left   : Boolean;
                         Node      : out Count_Type) is
      K : Key_Access := new Key_Type'(Key);
      E : Element_Access;
   begin
      if Container.Length = Count_Type'Last then
         raise Constraint_Error with "map would be too long";
      end if;
      begin
         E := new Element_Type'(New_Item);
         Allocate (Container, Node);
      exception
         when others =>
            Free (K);
            Free (E);
            raise;
      end;
      Container.Nodes (Node).Key := K;
      Container.Nodes (Node).Element := E;
      Link_Node (Container, Node, Parent, As_Left);
   end Insert_New;

   --  Removes all the nodes and resets the pool.
   procedure Clear_All (Container : in out Map) is
   begin
      for N in 1 .. Container.High loop
         Free (Container.Nodes (N).Key);
         Free (Container.Nodes (N).Element);
         Container.Nodes (N).Used := False;
      end loop;
      Container.Root := 0;
      Container.Length := 0;
      Container.Free := 0;
      Container.High := 0;
   end Clear_All;

   function First_Node (Container : Map) return Count_Type is
     (Min_Node (Container, Container.Root));

   function Last_Node (Container : Map) return Count_Type is
     (Max_Node (Container, Container.Root));

   --  Inserts a copy of each pair of Source into Target (empty).
   procedure Insert_All (Target : in out Map; Source : Map) is
      N    : Count_Type := First_Node (Source);
      Node : Count_Type;
      Last : Count_Type := 0;
   begin
      --  The keys come in order: each new node goes to the right of the
      --  previous one.
      while N /= 0 loop
         Insert_New (Target, Source.Nodes (N).Key.all,
                     Source.Nodes (N).Element.all, Last, False, Node);
         Last := Node;
         N := Next_Node (Source, N);
      end loop;
   end Insert_All;

   ---------------------------------------------------------------------
   --  Controlled operations
   ---------------------------------------------------------------------

   --  Makes a deep copy with the same layout.
   overriding procedure Adjust (Container : in out Map) is
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
         Container.Nodes (N).Key := null;
         Container.Nodes (N).Element := null;
      end loop;
      for N in 1 .. Container.High loop
         if Nodes (N).Used then
            Container.Nodes (N).Key := new Key_Type'(Nodes (N).Key.all);
            Container.Nodes (N).Element :=
              new Element_Type'(Nodes (N).Element.all);
         end if;
      end loop;
   end Adjust;

   overriding procedure Finalize (Container : in out Map) is
      Prohibited : constant Boolean :=
        Container.Busy > 0 or else Container.Lock > 0;
   begin
      --  (while tampering is prohibited (e.g. the target of an assignment):
      --  Program_Error, the container left unchanged)
      if Prohibited then
         raise Program_Error with "map finalized while tampering is prohibited";
      end if;
      if Container.Nodes /= null then
         for N in 1 .. Container.High loop
            Free (Container.Nodes (N).Key);
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
      Container : Map)
   is
      N : Count_Type := First_Node (Container);
   begin
      Count_Type'Base'Write (Stream, Container.Length);
      while N /= 0 loop
         Key_Type'Output (Stream, Container.Nodes (N).Key.all);
         Element_Type'Output (Stream, Container.Nodes (N).Element.all);
         N := Next_Node (Container, N);
      end loop;
   end Write;

   procedure Read
     (Stream    : not null access Ada.Streams.Root_Stream_Type'Class;
      Container : out Map)
   is
      N : Count_Type'Base;
   begin
      Clear (Container);
      Count_Type'Base'Read (Stream, N);
      if N < 0 then
         raise Constraint_Error with "invalid map length in stream";
      end if;
      for J in 1 .. N loop
         declare
            K : constant Key_Type := Key_Type'Input (Stream);
            E : constant Element_Type := Element_Type'Input (Stream);
         begin
            Include (Container, K, E);
         end;
      end loop;
   end Read;

   procedure Write
     (Stream   : not null access Ada.Streams.Root_Stream_Type'Class;
      Position : Cursor) is
   begin
      raise Program_Error with "attempt to stream a map cursor";
   end Write;

   procedure Read
     (Stream   : not null access Ada.Streams.Root_Stream_Type'Class;
      Position : out Cursor) is
   begin
      raise Program_Error with "attempt to stream a map cursor";
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
     and Map_Iterator_Interfaces.Parallel_Reversible_Iterator with
   record
      Container : Map_Access;
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
      Max_Chunks : Map_Iterator_Interfaces.Chunk_Index);
   overriding function Chunk_Count
     (Object : Iterator) return Map_Iterator_Interfaces.Chunk_Index;
   overriding function First
     (Object : Iterator; Chunk : Map_Iterator_Interfaces.Chunk_Index) return Cursor;
   overriding function Next
     (Object   : Iterator;
      Position : Cursor;
      Chunk    : Map_Iterator_Interfaces.Chunk_Index) return Cursor;

   overriding procedure Finalize (Object : in out Iterator) is
   begin
      if Object.Container /= null then
         Object.Container.Busy := Object.Container.Busy - 1;
         Object.Container := null;
      end if;
   end Finalize;

   function Make_Cursor (Container : Map_Access; Node : Count_Type)
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
         raise Program_Error with "cursor designates another map";
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
      Max_Chunks : Map_Iterator_Interfaces.Chunk_Index)
   is
      C    : constant Map_Access := Object.Container;
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
     (Object : Iterator) return Map_Iterator_Interfaces.Chunk_Index is
   begin
      if Object.Chunks = 0 then
         raise Program_Error with "iterator not split";
      end if;
      return Object.Chunks;
   end Chunk_Count;

   overriding function First
     (Object : Iterator; Chunk : Map_Iterator_Interfaces.Chunk_Index) return Cursor is
   begin
      Check_Chunk (Object, Chunk);
      return Make_Cursor (Object.Container, Object.Starts (Chunk));
   end First;

   overriding function Next
     (Object   : Iterator;
      Position : Cursor;
      Chunk    : Map_Iterator_Interfaces.Chunk_Index) return Cursor
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

   function Has_Element (Container : Map; Position : Cursor)
      return Boolean is
     (Position.Container = Var (Container)
      and then Valid (Container, Position.Node));

   function "=" (Left, Right : Map) return Boolean is
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
         if not Equivalent_Keys (Left.Nodes (N).Key.all, Right.Nodes (M).Key.all)
           or else not (Left.Nodes (N).Element.all = Right.Nodes (M).Element.all)
         then
            return False;
         end if;
         N := Next_Node (Left, N);
         M := Next_Node (Right, M);
      end loop;
      return True;
   end "=";

   function Tampering_With_Cursors_Prohibited
      (Container : Map) return Boolean is
     (Container.Busy > 0 or else Container.Lock > 0);

   function Tampering_With_Elements_Prohibited
      (Container : Map) return Boolean is
     (Container.Lock > 0);

   function Length (Container : Map) return Count_Type is
     (Container.Length);

   function Is_Empty (Container : Map) return Boolean is
     (Container.Length = 0);

   procedure Clear (Container : in out Map) is
   begin
      Check_Cursors (Container);
      Clear_All (Container);
   end Clear;

   function Key (Position : Cursor) return Key_Type is
   begin
      Check_Has_Element (Position);
      return Position.Container.Nodes (Position.Node).Key.all;
   end Key;

   function Key (Container : Map;
                 Position : Cursor) return Key_Type is
   begin
      Check_Position (Container, Position);
      return Container.Nodes (Position.Node).Key.all;
   end Key;

   function Element (Position : Cursor) return Element_Type is
   begin
      Check_Has_Element (Position);
      return Position.Container.Nodes (Position.Node).Element.all;
   end Element;

   function Element (Container : Map;
                     Position  : Cursor) return Element_Type is
   begin
      Check_Position (Container, Position);
      return Container.Nodes (Position.Node).Element.all;
   end Element;

   procedure Replace_Element (Container : in out Map;
                              Position  : in     Cursor;
                              New_item  : in     Element_Type) is
   begin
      Check_Elements (Container);
      Check_Position (Container, Position);
      declare
         X : constant Element_Access := new Element_Type'(New_item);
      begin
         Free (Container.Nodes (Position.Node).Element);
         Container.Nodes (Position.Node).Element := X;
      end;
   end Replace_Element;

   procedure Query_Element
     (Position : in Cursor;
      Process  : not null access procedure (Key     : in Key_Type;
                                            Element : in Element_Type)) is
   begin
      Check_Has_Element (Position);
      declare
         Guard : Lock_Guard (Position.Container);
         N     : Node_Type renames Position.Container.Nodes (Position.Node);
      begin
         Process (N.Key.all, N.Element.all);
      end;
   end Query_Element;

   procedure Query_Element
     (Container : in Map;
      Position  : in Cursor;
      Process   : not null access procedure (Key     : in Key_Type;
                                             Element : in Element_Type)) is
   begin
      Check_Position (Container, Position);
      declare
         Guard : Lock_Guard (Var (Container));
         N     : Node_Type renames Container.Nodes (Position.Node);
      begin
         Process (N.Key.all, N.Element.all);
      end;
   end Query_Element;

   procedure Update_Element
     (Container : in out Map;
      Position  : in     Cursor;
      Process   : not null access procedure
                      (Key     : in     Key_Type;
                       Element : in out Element_Type)) is
   begin
      Check_Position (Container, Position);
      declare
         Guard : Lock_Guard (Var (Container));
         N     : Node_Type renames Container.Nodes (Position.Node);
      begin
         Process (N.Key.all, N.Element.all);
      end;
   end Update_Element;

   function Constant_Reference (Container : aliased in Map;
                                Position  : in Cursor)
      return Constant_Reference_Type is
   begin
      Check_Position (Container, Position);
      return (Element => Container.Nodes (Position.Node).Element,
              Control => (Ada.Finalization.Controlled with
                          Container => Lock (Container)));
   end Constant_Reference;

   function Reference (Container : aliased in out Map;
                       Position  : in Cursor)
      return Reference_Type is
   begin
      Check_Position (Container, Position);
      return (Element => Container.Nodes (Position.Node).Element,
              Control => (Ada.Finalization.Controlled with
                          Container => Lock (Container)));
   end Reference;

   function Constant_Reference (Container : aliased in Map;
                                Key       : in Key_Type)
      return Constant_Reference_Type
   is
      N : constant Count_Type := Find_Node (Container, Key);
   begin
      if N = 0 then
         raise Constraint_Error with "key not in map";
      end if;
      return (Element => Container.Nodes (N).Element,
              Control => (Ada.Finalization.Controlled with
                          Container => Lock (Container)));
   end Constant_Reference;

   function Reference (Container : aliased in out Map;
                       Key       : in Key_Type)
      return Reference_Type
   is
      N : constant Count_Type := Find_Node (Container, Key);
   begin
      if N = 0 then
         raise Constraint_Error with "key not in map";
      end if;
      return (Element => Container.Nodes (N).Element,
              Control => (Ada.Finalization.Controlled with
                          Container => Lock (Container)));
   end Reference;

   procedure Assign (Target : in out Map; Source : in Map) is
   begin
      if Var (Target) = Var (Source) then
         return;
      end if;
      Check_Cursors (Target);
      Clear_All (Target);
      Insert_All (Target, Source);
   end Assign;

   function Copy (Source : Map)
      return Map is
   begin
      return Result : Map do
         Insert_All (Result, Source);
      end return;
   end Copy;

   procedure Move (Target : in out Map;
                   Source : in out Map) is
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

   procedure Insert (Container : in out Map;
                     Key       : in     Key_Type;
                     New_Item  : in     Element_Type;
                     Position  :    out Cursor;
                     Inserted  :    out Boolean)
   is
      N, Parent : Count_Type;
      As_Left   : Boolean;
   begin
      Check_Cursors (Container);
      Search (Container, Key, N, Parent, As_Left);
      if N /= 0 then
         Position := (Var (Container), N);
         Inserted := False;
         return;
      end if;
      Insert_New (Container, Key, New_Item, Parent, As_Left, N);
      Position := (Var (Container), N);
      Inserted := True;
   end Insert;

   procedure Insert (Container : in out Map;
                     Key       : in     Key_Type;
                     New_Item  : in     Element_Type)
   is
      Position : Cursor;
      Inserted : Boolean;
   begin
      Insert (Container, Key, New_Item, Position, Inserted);
      if not Inserted then
         raise Constraint_Error with "key already in map";
      end if;
   end Insert;

   procedure Replace_Node (Container : in out Map;
                           Node      : Count_Type;
                           Key       : Key_Type;
                           New_Item  : Element_Type) is
      K : Key_Access := new Key_Type'(Key);
      E : Element_Access;
   begin
      begin
         E := new Element_Type'(New_Item);
      exception
         when others =>
            Free (K);
            raise;
      end;
      Free (Container.Nodes (Node).Key);
      Free (Container.Nodes (Node).Element);
      Container.Nodes (Node).Key := K;
      Container.Nodes (Node).Element := E;
   end Replace_Node;

   procedure Include (Container : in out Map;
                      Key       : in     Key_Type;
                      New_Item  : in     Element_Type)
   is
      Position : Cursor;
      Inserted : Boolean;
   begin
      Insert (Container, Key, New_Item, Position, Inserted);
      if not Inserted then
         Replace_Node (Container, Position.Node, Key, New_Item);
      end if;
   end Include;

   procedure Replace (Container : in out Map;
                      Key       : in     Key_Type;
                      New_Item  : in     Element_Type)
   is
      N : Count_Type;
   begin
      Check_Elements (Container);
      N := Find_Node (Container, Key);
      if N = 0 then
         raise Constraint_Error with "key not in map";
      end if;
      Replace_Node (Container, N, Key, New_Item);
   end Replace;

   procedure Exclude (Container : in out Map;
                      Key       : in     Key_Type)
   is
      N : Count_Type;
   begin
      Check_Cursors (Container);
      N := Find_Node (Container, Key);
      if N /= 0 then
         Deallocate (Container, N);
      end if;
   end Exclude;

   procedure Delete (Container : in out Map;
                     Key       : in     Key_Type)
   is
      N : Count_Type;
   begin
      Check_Cursors (Container);
      N := Find_Node (Container, Key);
      if N = 0 then
         raise Constraint_Error with "key not in map";
      end if;
      Deallocate (Container, N);
   end Delete;

   procedure Delete (Container : in out Map;
                     Position  : in out Cursor) is
   begin
      Check_Cursors (Container);
      Check_Position (Container, Position);
      Deallocate (Container, Position.Node);
      Position := No_Element;
   end Delete;

   procedure Delete_First (Container : in out Map) is
   begin
      Check_Cursors (Container);
      if Container.Length > 0 then
         Deallocate (Container, First_Node (Container));
      end if;
   end Delete_First;

   procedure Delete_Last (Container : in out Map) is
   begin
      Check_Cursors (Container);
      if Container.Length > 0 then
         Deallocate (Container, Last_Node (Container));
      end if;
   end Delete_Last;

   function First (Container : Map) return Cursor is
     (Make_Cursor (Var (Container), First_Node (Container)));

   function First_Element (Container : Map)
      return Element_Type is
   begin
      if Container.Length = 0 then
         raise Constraint_Error with "map is empty";
      end if;
      return Container.Nodes (First_Node (Container)).Element.all;
   end First_Element;

   function First_Key (Container : Map)
      return Key_Type is
   begin
      if Container.Length = 0 then
         raise Constraint_Error with "map is empty";
      end if;
      return Container.Nodes (First_Node (Container)).Key.all;
   end First_Key;

   function Last (Container : Map) return Cursor is
     (Make_Cursor (Var (Container), Last_Node (Container)));

   function Last_Element (Container : Map)
      return Element_Type is
   begin
      if Container.Length = 0 then
         raise Constraint_Error with "map is empty";
      end if;
      return Container.Nodes (Last_Node (Container)).Element.all;
   end Last_Element;

   function Last_Key (Container : Map)
      return Key_Type is
   begin
      if Container.Length = 0 then
         raise Constraint_Error with "map is empty";
      end if;
      return Container.Nodes (Last_Node (Container)).Key.all;
   end Last_Key;

   function Next (Position : Cursor) return Cursor is
   begin
      if not Has_Element (Position) then
         return No_Element;
      end if;
      return Make_Cursor
        (Position.Container,
         Next_Node (Position.Container.all, Position.Node));
   end Next;

   function Next (Container : Map;
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

   procedure Next (Container : in     Map;
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

   function Previous (Container : Map;
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

   procedure Previous (Container : in     Map;
                       Position  : in out Cursor) is
   begin
      Position := Previous (Container, Position);
   end Previous;

   function Find (Container : Map;
                  Key       : Key_Type) return Cursor is
     (Make_Cursor (Var (Container), Find_Node (Container, Key)));

   function Element (Container : Map;
                     Key       : Key_Type) return Element_Type
   is
      N : constant Count_Type := Find_Node (Container, Key);
   begin
      if N = 0 then
         raise Constraint_Error with "key not in map";
      end if;
      return Container.Nodes (N).Element.all;
   end Element;

   function Floor (Container : Map;
                   Key       : Key_Type) return Cursor
   is
      N : Count_Type := Container.Root;
      R : Count_Type := 0;
   begin
      while N /= 0 loop
         if Key < Container.Nodes (N).Key.all then
            N := Container.Nodes (N).Left;
         else
            R := N;
            N := Container.Nodes (N).Right;
         end if;
      end loop;
      return Make_Cursor (Var (Container), R);
   end Floor;

   function Ceiling (Container : Map;
                     Key       : Key_Type) return Cursor
   is
      N : Count_Type := Container.Root;
      R : Count_Type := 0;
   begin
      while N /= 0 loop
         if Container.Nodes (N).Key.all < Key then
            N := Container.Nodes (N).Right;
         else
            R := N;
            N := Container.Nodes (N).Left;
         end if;
      end loop;
      return Make_Cursor (Var (Container), R);
   end Ceiling;

   function Contains (Container : Map;
                      Key       : Key_Type) return Boolean is
     (Find_Node (Container, Key) /= 0);

   function Key_Of (Position : Cursor) return Key_Type is
   begin
      Check_Has_Element (Position);
      return Position.Container.Nodes (Position.Node).Key.all;
   end Key_Of;

   function "<" (Left, Right : Cursor) return Boolean is
   begin
      Check_Has_Element (Left);
      Check_Has_Element (Right);
      return Key_Of (Left) < Key_Of (Right);
   end "<";

   function ">" (Left, Right : Cursor) return Boolean is
   begin
      Check_Has_Element (Left);
      Check_Has_Element (Right);
      return Key_Of (Right) < Key_Of (Left);
   end ">";

   function "<" (Left : Cursor; Right : Key_Type) return Boolean is
     (Key_Of (Left) < Right);

   function ">" (Left : Cursor; Right : Key_Type) return Boolean is
     (Right < Key_Of (Left));

   function "<" (Left : Key_Type; Right : Cursor) return Boolean is
     (Left < Key_Of (Right));

   function ">" (Left : Key_Type; Right : Cursor) return Boolean is
     (Key_Of (Right) < Left);

   procedure Iterate
     (Container : in Map;
      Process   : not null access procedure (Position : in Cursor))
   is
      C     : constant Map_Access := Var (Container);
      Guard : Busy_Guard (C);
      N     : Count_Type := First_Node (Container);
   begin
      while N /= 0 loop
         Process ((C, N));
         N := Next_Node (Container, N);
      end loop;
   end Iterate;

   procedure Reverse_Iterate
     (Container : in Map;
      Process   : not null access procedure (Position : in Cursor))
   is
      C     : constant Map_Access := Var (Container);
      Guard : Busy_Guard (C);
      N     : Count_Type := Last_Node (Container);
   begin
      while N /= 0 loop
         Process ((C, N));
         N := Previous_Node (Container, N);
      end loop;
   end Reverse_Iterate;

   function Iterate (Container : in Map)
      return Map_Iterator_Interfaces.Parallel_Reversible_Iterator'Class
   is
      C : constant Map_Access := Var (Container);
   begin
      return It : Iterator do
         It.Container := C;
         C.Busy := C.Busy + 1;
      end return;
   end Iterate;

   function Iterate (Container : in Map; Start : in Cursor)
      return Map_Iterator_Interfaces.Reversible_Iterator'Class
   is
      C : constant Map_Access := Var (Container);
   begin
      Check_Position (Container, Start);
      return It : Iterator do
         It.Container := C;
         It.Start := Start.Node;
         C.Busy := C.Busy + 1;
      end return;
   end Iterate;

end Ada.Containers.Indefinite_Ordered_Maps;
