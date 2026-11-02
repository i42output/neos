--  Ada.Containers.Bounded_Multiway_Trees.adb
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

package body Ada.Containers.Bounded_Multiway_Trees is

   --  The operations that take the container only through a cursor (or as
   --  an in parameter) still have to record that tampering is prohibited:
   --  the variable view of the container is obtained by converting the
   --  access-to-constant value designating it.
   type Constant_Tree_Access is access constant Tree;
   for Constant_Tree_Access'Storage_Size use 0;

   function To_Variable is new Ada.Unchecked_Conversion
     (Constant_Tree_Access, Tree_Access);

   function Var (Container : Tree) return Tree_Access is
     (To_Variable (Container'Unchecked_Access));

   ---------------------------------------------------------------------
   --  Tampering
   ---------------------------------------------------------------------

   procedure Check_Cursors (Container : Tree) is
   begin
      if Container.Busy > 0 or else Container.Lock > 0 then
         raise Program_Error with "attempt to tamper with cursors";
      end if;
   end Check_Cursors;

   procedure Check_Elements (Container : Tree) is
   begin
      null;  --  never prohibited for a definite element type
   end Check_Elements;

   type Busy_Guard (Container : Tree_Access) is
     new Ada.Finalization.Limited_Controlled with null record;
   overriding procedure Initialize (Guard : in out Busy_Guard);
   overriding procedure Finalize (Guard : in out Busy_Guard);

   type Lock_Guard (Container : Tree_Access) is
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
   function Lock (Container : Tree) return Tree_Access is
      C : constant Tree_Access := Var (Container);
   begin
      C.Lock := C.Lock + 1;
      return C;
   end Lock;

   ---------------------------------------------------------------------
   --  Node links (the root's child links are components of the tree)
   ---------------------------------------------------------------------

   function Valid (Container : Tree; Node : Count_Type) return Boolean is
     (Node = Root_Node
      or else (Node in 1 .. Container.High
               and then Container.Nodes (Node).Used));

   function FC (Container : Tree; N : Count_Type) return Count_Type is
     (if N = Root_Node then Container.Root_First
      else Container.Nodes (N).First_Child);

   function LC (Container : Tree; N : Count_Type) return Count_Type is
     (if N = Root_Node then Container.Root_Last
      else Container.Nodes (N).Last_Child);

   procedure Set_FC (Container : in out Tree; N, Child : Count_Type) is
   begin
      if N = Root_Node then
         Container.Root_First := Child;
      else
         Container.Nodes (N).First_Child := Child;
      end if;
   end Set_FC;

   procedure Set_LC (Container : in out Tree; N, Child : Count_Type) is
   begin
      if N = Root_Node then
         Container.Root_Last := Child;
      else
         Container.Nodes (N).Last_Child := Child;
      end if;
   end Set_LC;

   function Parent_Of (Container : Tree; N : Count_Type) return Count_Type is
     (if N = Root_Node then 0 else Container.Nodes (N).Parent);

   --  Links the detached node Node as a child of Parent, before Before
   --  (0: as the last child).
   procedure Link_Child (Container : in out Tree;
                         Node      : Count_Type;
                         Parent    : Count_Type;
                         Before    : Count_Type)
   is
      P : Count_Type;
   begin
      Container.Nodes (Node).Parent := Parent;
      if Before = 0 then
         P := LC (Container, Parent);
         Set_LC (Container, Parent, Node);
      else
         P := Container.Nodes (Before).Prev;
         Container.Nodes (Before).Prev := Node;
      end if;
      Container.Nodes (Node).Prev := P;
      Container.Nodes (Node).Next := Before;
      if P = 0 then
         Set_FC (Container, Parent, Node);
      else
         Container.Nodes (P).Next := Node;
      end if;
   end Link_Child;

   --  Detaches Node (with its subtree) from its parent.
   procedure Unlink (Container : in out Tree; Node : Count_Type) is
      Parent : constant Count_Type := Container.Nodes (Node).Parent;
      P      : constant Count_Type := Container.Nodes (Node).Prev;
      N      : constant Count_Type := Container.Nodes (Node).Next;
   begin
      if P = 0 then
         Set_FC (Container, Parent, N);
      else
         Container.Nodes (P).Next := N;
      end if;
      if N = 0 then
         Set_LC (Container, Parent, P);
      else
         Container.Nodes (N).Prev := P;
      end if;
      Container.Nodes (Node).Parent := 0;
      Container.Nodes (Node).Next := 0;
      Container.Nodes (Node).Prev := 0;
   end Unlink;

   --  The node after N in depth-first (preorder) order within the subtree
   --  rooted at Top, or 0.
   function Next_Pre (Container : Tree; N, Top : Count_Type)
      return Count_Type
   is
      X : Count_Type := N;
   begin
      if FC (Container, X) /= 0 then
         return FC (Container, X);
      end if;
      loop
         if X = Top then
            return 0;
         elsif Container.Nodes (X).Next /= 0 then
            return Container.Nodes (X).Next;
         end if;
         X := Container.Nodes (X).Parent;
      end loop;
   end Next_Pre;

   --  The first node of the subtree rooted at Top in depth-first order,
   --  not counting the root node.
   function First_Pre (Container : Tree; Top : Count_Type) return Count_Type is
     (if Top = Root_Node then Container.Root_First else Top);

   function Is_Ancestor (Container : Tree; Ancestor, N : Count_Type)
      return Boolean
   is
      X : Count_Type := N;
   begin
      while X /= 0 loop
         if X = Ancestor then
            return True;
         end if;
         X := Parent_Of (Container, X);
      end loop;
      return False;
   end Is_Ancestor;

   function Subtree_Count (Container : Tree; Top : Count_Type)
      return Count_Type
   is
      N     : Count_Type := First_Pre (Container, Top);
      Count : Count_Type := (if Top = Root_Node then 1 else 0);
   begin
      while N /= 0 loop
         Count := Count + 1;
         N := Next_Pre (Container, N, Top);
      end loop;
      return Count;
   end Subtree_Count;

   ---------------------------------------------------------------------
   --  Checks
   ---------------------------------------------------------------------

   function Is_Element (Container : Tree; Node : Count_Type) return Boolean is
     (Node in 1 .. Container.High and then Container.Nodes (Node).Used);

   --  Position designates an element (not the root) of Container
   procedure Check_Element (Container : Tree; Position : Cursor) is
   begin
      if Position.Container = null then
         raise Constraint_Error with "cursor has no element";
      elsif Position.Container /= Var (Container)
        or else not Is_Element (Container, Position.Node)
      then
         raise Program_Error with "cursor designates no element of the tree";
      end if;
   end Check_Element;

   --  Position designates an element (not the root)
   procedure Check_Element (Position : Cursor) is
   begin
      if Position.Container = null then
         raise Constraint_Error with "cursor has no element";
      elsif not Is_Element (Position.Container.all, Position.Node) then
         raise Program_Error with "cursor designates no element";
      end if;
   end Check_Element;

   --  Meaningful_For (Container, Position)
   procedure Check_Meaningful (Container : Tree; Position : Cursor) is
   begin
      if Position.Container /= null
        and then (Position.Container /= Var (Container)
                  or else not Valid (Container, Position.Node))
      then
         raise Program_Error with "cursor designates no node of the tree";
      end if;
   end Check_Meaningful;

   --  Position /= No_Element and Meaningful_For (Container, Position)
   procedure Check_Node (Container : Tree; Position : Cursor) is
   begin
      if Position.Container = null then
         raise Constraint_Error with "cursor has no node";
      end if;
      Check_Meaningful (Container, Position);
   end Check_Node;

   --  The checks on Parent and Before of the insertion operations.
   procedure Check_Parent_Before (Container : Tree; Parent, Before : Cursor)
   is
   begin
      Check_Node (Container, Parent);
      Check_Meaningful (Container, Before);
      if Before.Container /= null
        and then (Before.Node = Root_Node
                  or else Container.Nodes (Before.Node).Parent /= Parent.Node)
      then
         raise Constraint_Error with "Before is not a child of Parent";
      end if;
   end Check_Parent_Before;

   procedure Check_Room (Container : Tree; Count : Count_Type) is
   begin
      if Container.Count > Root_Node - 1 - Count then
         raise Constraint_Error with "tree would be too large";
      end if;
      if Container.Count > Container.Capacity - Count then
         raise Capacity_Error with "tree capacity exceeded";
      end if;
   end Check_Room;

   ---------------------------------------------------------------------
   --  The node pool
   ---------------------------------------------------------------------

   --  Takes a detached node from the pool (growing it in the unbounded
   --  forms); its element is not set.
   procedure Allocate (Container : in out Tree; Node : out Count_Type) is
   begin
      if Container.Free /= 0 then
         Node := Container.Free;
         Container.Free := Container.Nodes (Node).Next;
      else
         if Container.High = Container.Capacity then
            raise Capacity_Error with "tree capacity exceeded";
         end if;
         Container.High := Container.High + 1;
         Node := Container.High;
      end if;
      Container.Nodes (Node) :=
        (Element     => Container.Nodes (Node).Element,
         Parent      => 0,
         First_Child => 0,
         Last_Child  => 0,
         Next        => 0,
         Prev        => 0,
         Used        => True);
      Container.Count := Container.Count + 1;
   end Allocate;

   --  Returns a detached childless node to the pool.
   procedure Release (Container : in out Tree; Node : Count_Type) is
   begin
      Container.Nodes (Node).Used := False;
      Container.Nodes (Node).Parent := 0;
      Container.Nodes (Node).First_Child := 0;
      Container.Nodes (Node).Last_Child := 0;
      Container.Nodes (Node).Prev := 0;
      Container.Nodes (Node).Next := Container.Free;
      Container.Free := Node;
      Container.Count := Container.Count - 1;
   end Release;

   --  Allocates a detached node holding a copy of New_Item (the container
   --  is not modified if the copy fails).
   procedure New_Node (Container : in out Tree;
                       New_Item  : Element_Type;
                       Node      : out Count_Type) is
   begin
      Allocate (Container, Node);
      begin
         Container.Nodes (Node).Element := New_Item;
      exception
         when others =>
            Release (Container, Node);
            raise;
      end;
   end New_Node;

   --  Frees the detached subtree rooted at Top (not the root node).
   procedure Free_Subtree (Container : in out Tree; Top : Count_Type) is
      N, P, S : Count_Type := Top;
   begin
      loop
         while Container.Nodes (N).First_Child /= 0 loop
            N := Container.Nodes (N).First_Child;
         end loop;
         if N = Top then
            Release (Container, N);
            return;
         end if;
         --  N is a leaf and the first child of its parent.
         P := Container.Nodes (N).Parent;
         S := Container.Nodes (N).Next;
         Container.Nodes (P).First_Child := S;
         if S = 0 then
            Container.Nodes (P).Last_Child := 0;
         else
            Container.Nodes (S).Prev := 0;
         end if;
         Release (Container, N);
         N := (if S /= 0 then S else P);
      end loop;
   end Free_Subtree;

   --  Frees all the children (and further descendants) of Parent.
   procedure Free_Children (Container : in out Tree; Parent : Count_Type) is
      C : Count_Type;
   begin
      loop
         C := FC (Container, Parent);
         exit when C = 0;
         Unlink (Container, C);
         Free_Subtree (Container, C);
      end loop;
   end Free_Children;

   --  Makes a detached copy, in Target, of the subtree rooted at Top of
   --  Source (Top is not the root node); Target and Source may be the same
   --  object.  Nothing is allocated in Target if a copy fails.
   procedure Copy_Nodes (Target : in out Tree;
                         Source : Tree_Access;
                         Top    : Count_Type;
                         Result : out Count_Type)
   is
      S, New_Cur, N : Count_Type;
   begin
      New_Node (Target, Source.Nodes (Top).Element, Result);
      S := Top;
      New_Cur := Result;
      begin
         Walk : loop
            if Source.Nodes (S).First_Child /= 0 then
               S := Source.Nodes (S).First_Child;
               New_Node (Target, Source.Nodes (S).Element, N);
               Link_Child (Target, N, New_Cur, 0);
               New_Cur := N;
            else
               loop
                  exit Walk when S = Top;
                  if Source.Nodes (S).Next /= 0 then
                     S := Source.Nodes (S).Next;
                     New_Node (Target, Source.Nodes (S).Element, N);
                     Link_Child (Target, N, Target.Nodes (New_Cur).Parent, 0);
                     New_Cur := N;
                     exit;
                  end if;
                  S := Source.Nodes (S).Parent;
                  New_Cur := Target.Nodes (New_Cur).Parent;
               end loop;
            end if;
         end loop Walk;
      exception
         when others =>
            Free_Subtree (Target, Result);
            raise;
      end;
   end Copy_Nodes;

   --  Appends copies of the children (and descendants) of Source's root to
   --  the root of Target.
   procedure Copy_Children (Target : in out Tree; Source : Tree_Access) is
      C : Count_Type := Source.Root_First;
      N : Count_Type;
   begin
      while C /= 0 loop
         Copy_Nodes (Target, Source, C, N);
         Link_Child (Target, N, Root_Node, 0);
         C := Source.Nodes (C).Next;
      end loop;
   end Copy_Children;

   --  Removes all the nodes and resets the pool.
   procedure Clear_All (Container : in out Tree) is
   begin
      for N in 1 .. Container.High loop
         Container.Nodes (N).Used := False;
      end loop;
      Container.Root_First := 0;
      Container.Root_Last := 0;
      Container.Count := 0;
      Container.Free := 0;
      Container.High := 0;
   end Clear_All;

   ---------------------------------------------------------------------
   --  Controlled operations
   ---------------------------------------------------------------------

   ---------------------------------------------------------------------
   --  Streams: the number of nodes (other than the root), then for each
   --  node in depth-first order its number of children and its element.
   ---------------------------------------------------------------------

   procedure Write
     (Stream    : not null access Ada.Streams.Root_Stream_Type'Class;
      Container : Tree)
   is
      N : Count_Type := Container.Root_First;
      K : Count_Type;
      C : Count_Type;
   begin
      Count_Type'Base'Write (Stream, Container.Count);
      K := 0;
      C := Container.Root_First;
      while C /= 0 loop
         K := K + 1;
         C := Container.Nodes (C).Next;
      end loop;
      Count_Type'Base'Write (Stream, K);
      while N /= 0 loop
         K := 0;
         C := Container.Nodes (N).First_Child;
         while C /= 0 loop
            K := K + 1;
            C := Container.Nodes (C).Next;
         end loop;
         Count_Type'Base'Write (Stream, K);
         Element_Type'Write (Stream, Container.Nodes (N).Element);
         N := Next_Pre (Container, N, Root_Node);
      end loop;
   end Write;

   procedure Read
     (Stream    : not null access Ada.Streams.Root_Stream_Type'Class;
      Container : out Tree)
   is
      function Read_Count return Count_Type is
         K : Count_Type'Base;
      begin
         Count_Type'Base'Read (Stream, K);
         if K < 0 then
            raise Constraint_Error with "invalid tree in stream";
         end if;
         return K;
      end Read_Count;

      --  Reads K nodes (with their subtrees) as children of Parent.
      procedure Read_Children (Parent : Count_Type; K : Count_Type) is
         Child_Count : Count_Type;
         N           : Count_Type;
      begin
         for J in 1 .. K loop
            Child_Count := Read_Count;
            Check_Room (Container, 1);
            Allocate (Container, N);
            begin
               Element_Type'Read (Stream, Container.Nodes (N).Element);
            exception
               when others =>
                  Release (Container, N);
                  raise;
            end;
            Link_Child (Container, N, Parent, 0);
            Read_Children (N, Child_Count);
         end loop;
      end Read_Children;

      Total : Count_Type;
   begin
      Clear (Container);
      Total := Read_Count;
      Read_Children (Root_Node, Read_Count);
      if Container.Count /= Total then
         raise Constraint_Error with "invalid tree in stream";
      end if;
   end Read;

   procedure Write
     (Stream   : not null access Ada.Streams.Root_Stream_Type'Class;
      Position : Cursor) is
   begin
      raise Program_Error with "attempt to stream a tree cursor";
   end Write;

   procedure Read
     (Stream   : not null access Ada.Streams.Root_Stream_Type'Class;
      Position : out Cursor) is
   begin
      raise Program_Error with "attempt to stream a tree cursor";
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

   --  An iterator over the elements of the subtree rooted at Top (in
   --  depth-first order), or over the children of Top.
   type Iterator is new Ada.Finalization.Limited_Controlled
     and Tree_Iterator_Interfaces.Parallel_Reversible_Iterator with
   record
      Container : Tree_Access;
      Top       : Count_Type := Root_Node;
      Children  : Boolean := False;
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
      Max_Chunks : Tree_Iterator_Interfaces.Chunk_Index);
   overriding function Chunk_Count
     (Object : Iterator) return Tree_Iterator_Interfaces.Chunk_Index;
   overriding function First
     (Object : Iterator; Chunk : Tree_Iterator_Interfaces.Chunk_Index)
      return Cursor;
   overriding function Next
     (Object   : Iterator;
      Position : Cursor;
      Chunk    : Tree_Iterator_Interfaces.Chunk_Index) return Cursor;

   overriding procedure Finalize (Object : in out Iterator) is
   begin
      if Object.Container /= null then
         Object.Container.Busy := Object.Container.Busy - 1;
         Object.Container := null;
      end if;
   end Finalize;

   function Make_Cursor (Container : Tree_Access; Node : Count_Type)
      return Cursor is
     (if Node = 0 then No_Element else (Container, Node));

   function First_Node (Object : Iterator) return Count_Type is
     (if Object.Children then FC (Object.Container.all, Object.Top)
      else First_Pre (Object.Container.all, Object.Top));

   function Next_Node (Object : Iterator; N : Count_Type) return Count_Type is
     (if Object.Children then Object.Container.Nodes (N).Next
      else Next_Pre (Object.Container.all, N, Object.Top));

   procedure Check_Iterator_Cursor (Object : Iterator; Position : Cursor) is
   begin
      if Position.Container /= null
        and then Position.Container /= Object.Container
      then
         raise Program_Error with "cursor designates another tree";
      end if;
   end Check_Iterator_Cursor;

   overriding function First (Object : Iterator) return Cursor is
     (Make_Cursor (Object.Container, First_Node (Object)));

   overriding function Next
     (Object : Iterator; Position : Cursor) return Cursor is
   begin
      Check_Iterator_Cursor (Object, Position);
      if not Has_Element (Position) then
         return No_Element;
      end if;
      return Make_Cursor (Object.Container, Next_Node (Object, Position.Node));
   end Next;

   overriding function Last (Object : Iterator) return Cursor is
   begin
      if not Object.Children then
         raise Program_Error with "subtree iterator is not reversible";
      end if;
      return Make_Cursor (Object.Container, LC (Object.Container.all, Object.Top));
   end Last;

   overriding function Previous
     (Object : Iterator; Position : Cursor) return Cursor is
   begin
      if not Object.Children then
         raise Program_Error with "subtree iterator is not reversible";
      end if;
      Check_Iterator_Cursor (Object, Position);
      if not Has_Element (Position) then
         return No_Element;
      end if;
      return Make_Cursor
        (Object.Container, Object.Container.Nodes (Position.Node).Prev);
   end Previous;

   overriding function Is_Split (Object : Iterator) return Boolean is
     (Object.Chunks > 0);

   overriding procedure Split_Into_Chunks
     (Object     : in out Iterator;
      Max_Chunks : Tree_Iterator_Interfaces.Chunk_Index)
   is
      Length : Count_Type := 0;
      N      : Count_Type := First_Node (Object);
      K, Size, I : Count_Type;
   begin
      if Object.Chunks > 0 then
         raise Program_Error with "iterator already split";
      end if;
      while N /= 0 loop
         Length := Length + 1;
         N := Next_Node (Object, N);
      end loop;
      K := Count_Type'Max (1, Count_Type'Min
             (Length, Count_Type'Min (Count_Type (Max_Chunks), Max_Split)));
      Size := (Length + K - 1) / K;
      N := First_Node (Object);
      I := 0;
      while N /= 0 loop
         if I mod Size = 0 then
            Object.Chunks := Object.Chunks + 1;
            Object.Starts (Object.Chunks) := N;
         end if;
         I := I + 1;
         N := Next_Node (Object, N);
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
     (Object : Iterator) return Tree_Iterator_Interfaces.Chunk_Index is
   begin
      if Object.Chunks = 0 then
         raise Program_Error with "iterator not split";
      end if;
      return Object.Chunks;
   end Chunk_Count;

   overriding function First
     (Object : Iterator; Chunk : Tree_Iterator_Interfaces.Chunk_Index)
      return Cursor is
   begin
      Check_Chunk (Object, Chunk);
      return Make_Cursor (Object.Container, Object.Starts (Chunk));
   end First;

   overriding function Next
     (Object   : Iterator;
      Position : Cursor;
      Chunk    : Tree_Iterator_Interfaces.Chunk_Index) return Cursor
   is
      N : Count_Type;
   begin
      Check_Chunk (Object, Chunk);
      Check_Iterator_Cursor (Object, Position);
      if not Has_Element (Position) then
         return No_Element;
      end if;
      N := Next_Node (Object, Position.Node);
      if N = Object.Starts (Chunk + 1) then
         return No_Element;
      end if;
      return Make_Cursor (Object.Container, N);
   end Next;

   function Make_Iterator (Container : Tree_Access;
                           Top       : Count_Type;
                           Children  : Boolean) return Iterator is
   begin
      return It : Iterator do
         It.Container := Container;
         It.Top := Top;
         It.Children := Children;
         Container.Busy := Container.Busy + 1;
      end return;
   end Make_Iterator;

   ---------------------------------------------------------------------
   --  Visible operations
   ---------------------------------------------------------------------

   function Has_Element (Position : Cursor) return Boolean is
     (Position.Container /= null
      and then Is_Element (Position.Container.all, Position.Node));

   function Has_Element (Container : Tree; Position : Cursor)
      return Boolean is
     (Position.Container = Var (Container)
      and then Is_Element (Container, Position.Node));

   --  Compares the subtrees rooted at L in Left and R in Right.
   function Equal_Nodes (Left : Tree; L : Count_Type;
                         Right : Tree; R : Count_Type) return Boolean
   is
      LN : Count_Type := L;
      RN : Count_Type := R;
   begin
      if (L = Root_Node) /= (R = Root_Node) then
         return False;
      end if;
      if L /= Root_Node
        and then not (Left.Nodes (L).Element = Right.Nodes (R).Element)
      then
         return False;
      end if;
      --  Walk both subtrees in step; the shapes must agree.
      loop
         declare
            LC1 : constant Count_Type := FC (Left, LN);
            RC1 : constant Count_Type := FC (Right, RN);
         begin
            if (LC1 = 0) /= (RC1 = 0) then
               return False;
            end if;
            if LC1 /= 0 then
               LN := LC1;
               RN := RC1;
            else
               loop
                  if LN = L then
                     return True;
                  end if;
                  if (Left.Nodes (LN).Next = 0) /= (Right.Nodes (RN).Next = 0)
                  then
                     return False;
                  end if;
                  if Left.Nodes (LN).Next /= 0 then
                     LN := Left.Nodes (LN).Next;
                     RN := Right.Nodes (RN).Next;
                     exit;
                  end if;
                  LN := Left.Nodes (LN).Parent;
                  RN := Right.Nodes (RN).Parent;
               end loop;
            end if;
            if not (Left.Nodes (LN).Element = Right.Nodes (RN).Element)
            then
               return False;
            end if;
         end;
      end loop;
   end Equal_Nodes;

   function Equal_Subtree (Left_Position : Cursor;
                           Right_Position: Cursor) return Boolean is
   begin
      if Left_Position.Container = null
        or else Right_Position.Container = null
      then
         raise Constraint_Error with "cursor has no node";
      end if;
      return Equal_Nodes (Left_Position.Container.all, Left_Position.Node,
                          Right_Position.Container.all, Right_Position.Node);
   end Equal_Subtree;

   function "=" (Left, Right : Tree) return Boolean is
   begin
      if Var (Left) = Var (Right) then
         return True;
      elsif Left.Count /= Right.Count then
         return False;
      end if;
      return Equal_Nodes (Left, Root_Node, Right, Root_Node);
   end "=";

   function Same_Object (Left, Right : Tree) return Boolean is
     (Var (Left) = Var (Right));

   function Tampering_With_Cursors_Prohibited
      (Container : Tree) return Boolean is
     (Container.Busy > 0 or else Container.Lock > 0);

   function Tampering_With_Elements_Prohibited
      (Container : Tree) return Boolean is
     (False);

   function Empty (Capacity : Count_Type := 10)
      return Tree is
   begin
      return (Capacity => Capacity, others => <>);
   end Empty;

   function Is_Empty (Container : Tree) return Boolean is
     (Container.Count = 0);

   function Node_Count (Container : Tree) return Count_Type is
     (Container.Count + 1);

   function Subtree_Node_Count (Position : Cursor) return Count_Type is
     (if Position.Container = null then 0
      else Subtree_Count (Position.Container.all, Position.Node));

   function Subtree_Node_Count (Container : Tree; Position : Cursor)
      return Count_Type is
   begin
      Check_Meaningful (Container, Position);
      return Subtree_Node_Count (Position);
   end Subtree_Node_Count;

   function Depth (Position : Cursor) return Count_Type is
      N : Count_Type;
      D : Count_Type := 0;
   begin
      if Position.Container = null then
         return 0;
      end if;
      N := Position.Node;
      while N /= 0 loop
         D := D + 1;
         N := Parent_Of (Position.Container.all, N);
      end loop;
      return D;
   end Depth;

   function Depth (Container : Tree; Position : Cursor)
      return Count_Type is
   begin
      Check_Meaningful (Container, Position);
      return Depth (Position);
   end Depth;

   function Is_Root (Position : Cursor) return Boolean is
     (Position.Container /= null and then Position.Node = Root_Node);

   function Is_Root (Container : Tree; Position : Cursor)
      return Boolean is
     (Position.Container = Var (Container) and then Position.Node = Root_Node);

   function Is_Leaf (Position : Cursor) return Boolean is
     (Position.Container /= null
      and then FC (Position.Container.all, Position.Node) = 0);

   function Is_Leaf (Container : Tree; Position : Cursor)
      return Boolean is
   begin
      Check_Meaningful (Container, Position);
      return Is_Leaf (Position);
   end Is_Leaf;

   function Is_Ancestor_Of (Container : Tree;
          Parent   : Cursor;
          Position : Cursor) return Boolean is
   begin
      Check_Meaningful (Container, Position);
      Check_Meaningful (Container, Parent);
      if Parent.Container = null or else Position.Container = null then
         return False;
      end if;
      return Is_Ancestor (Container, Parent.Node, Position.Node);
   end Is_Ancestor_Of;

   function Root (Container : Tree) return Cursor is
     ((Var (Container), Root_Node));

   procedure Clear (Container : in out Tree) is
   begin
      Check_Cursors (Container);
      Clear_All (Container);
   end Clear;

   function Element (Position : Cursor) return Element_Type is
   begin
      Check_Element (Position);
      return Position.Container.Nodes (Position.Node).Element;
   end Element;

   function Element (Container : Tree;
                     Position  : Cursor) return Element_Type is
   begin
      Check_Element (Container, Position);
      return Container.Nodes (Position.Node).Element;
   end Element;

   procedure Replace_Element (Container : in out Tree;
                              Position  : in     Cursor;
                              New_item  : in     Element_Type) is
   begin
      Check_Elements (Container);
      Check_Element (Container, Position);
      declare
      begin
         Container.Nodes (Position.Node).Element := New_item;
      end;
   end Replace_Element;

   procedure Query_Element
     (Position : in Cursor;
      Process  : not null access procedure (Element : in Element_Type)) is
   begin
      Check_Element (Position);
      declare
         Guard : Lock_Guard (Position.Container);
      begin
         Process (Position.Container.Nodes (Position.Node).Element);
      end;
   end Query_Element;

   procedure Query_Element
     (Container : in Tree;
      Position  : in Cursor;
      Process   : not null access procedure (Element : in Element_Type)) is
   begin
      Check_Element (Container, Position);
      declare
         Guard : Lock_Guard (Var (Container));
      begin
         Process (Container.Nodes (Position.Node).Element);
      end;
   end Query_Element;

   procedure Update_Element
     (Container : in out Tree;
      Position  : in     Cursor;
      Process   : not null access procedure
                      (Element : in out Element_Type)) is
   begin
      Check_Element (Container, Position);
      declare
         Guard : Lock_Guard (Var (Container));
      begin
         Process (Container.Nodes (Position.Node).Element);
      end;
   end Update_Element;

   function Constant_Reference (Container : aliased in Tree;
                                Position  : in Cursor)
      return Constant_Reference_Type is
   begin
      Check_Element (Container, Position);
      return (Element => Container.Nodes (Position.Node).Element'Access,
              Control => (Ada.Finalization.Controlled with
                          Container => Lock (Container)));
   end Constant_Reference;

   function Reference (Container : aliased in out Tree;
                       Position  : in Cursor)
      return Reference_Type is
   begin
      Check_Element (Container, Position);
      return (Element => Container.Nodes (Position.Node).Element'Access,
              Control => (Ada.Finalization.Controlled with
                          Container => Lock (Container)));
   end Reference;

   procedure Assign (Target : in out Tree; Source : in Tree) is
   begin
      if Var (Target) = Var (Source) then
         return;
      end if;
      Check_Cursors (Target);
      if Source.Count > Target.Capacity then
         raise Capacity_Error with "tree capacity exceeded";
      end if;
      Clear_All (Target);
      Copy_Children (Target, Var (Source));
   end Assign;

   function Copy (Source : Tree; Capacity : Count_Type := 0)
      return Tree
   is
      C : constant Count_Type :=
        (if Capacity = 0 then Source.Count else Capacity);
   begin
      if Capacity /= 0 and then Capacity < Source.Count then
         raise Capacity_Error with "capacity less than the node count";
      end if;
      return Result : Tree (C) do
         Copy_Children (Result, Var (Source));
      end return;
   end Copy;

   procedure Move (Target : in out Tree;
                   Source : in out Tree) is
   begin
      if Var (Target) = Var (Source) then
         return;
      end if;
      Check_Cursors (Target);
      Check_Cursors (Source);
      Assign (Target, Source);
      Clear_All (Source);
   end Move;

   procedure Delete_Leaf (Container : in out Tree;
                          Position  : in out Cursor) is
   begin
      Check_Cursors (Container);
      Check_Element (Container, Position);
      if Container.Nodes (Position.Node).First_Child /= 0 then
         raise Constraint_Error with "node is not a leaf";
      end if;
      Unlink (Container, Position.Node);
      Release (Container, Position.Node);
      Position := No_Element;
   end Delete_Leaf;

   procedure Delete_Subtree (Container : in out Tree;
                             Position  : in out Cursor) is
   begin
      Check_Cursors (Container);
      Check_Element (Container, Position);
      Unlink (Container, Position.Node);
      Free_Subtree (Container, Position.Node);
      Position := No_Element;
   end Delete_Subtree;

   procedure Swap (Container : in out Tree;
                   I, J      : in     Cursor) is
   begin
      Check_Cursors (Container);
      if I.Container = null or else J.Container = null then
         raise Constraint_Error with "cursor has no element";
      end if;
      Check_Element (Container, I);
      Check_Element (Container, J);
      if I.Node /= J.Node then
         declare
            T : constant Element_Type := Container.Nodes (I.Node).Element;
         begin
            Container.Nodes (I.Node).Element := Container.Nodes (J.Node).Element;
            Container.Nodes (J.Node).Element := T;
         end;
      end if;
   end Swap;

   --  The first node equal to Item in the subtree rooted at Top (in
   --  depth-first order), or 0.
   function Find_Node (Container : Tree; Top : Count_Type;
                       Item : Element_Type) return Count_Type
   is
      N     : Count_Type := First_Pre (Container, Top);
      Guard : Lock_Guard (Var (Container));
   begin
      while N /= 0 loop
         if Container.Nodes (N).Element = Item then
            return N;
         end if;
         N := Next_Pre (Container, N, Top);
      end loop;
      return 0;
   end Find_Node;

   function Find (Container : Tree;
                  Item      : Element_Type)
      return Cursor is
     (Make_Cursor (Var (Container), Find_Node (Container, Root_Node, Item)));

   function Find_In_Subtree (Position : Cursor;
                             Item     : Element_Type)
      return Cursor is
   begin
      if Position.Container = null then
         raise Constraint_Error with "cursor has no node";
      end if;
      return Make_Cursor
        (Position.Container,
         Find_Node (Position.Container.all, Position.Node, Item));
   end Find_In_Subtree;

   function Find_In_Subtree (Container : Tree;
                             Position  : Cursor;
                             Item      : Element_Type)
      return Cursor is
   begin
      Check_Node (Container, Position);
      return Find_In_Subtree (Position, Item);
   end Find_In_Subtree;

   function Ancestor_Find (Position : Cursor;
                           Item     : Element_Type)
      return Cursor
   is
      N : Count_Type;
   begin
      if Position.Container = null then
         raise Constraint_Error with "cursor has no node";
      end if;
      N := Position.Node;
      while N /= 0 and then N /= Root_Node loop
         if Position.Container.Nodes (N).Element = Item then
            return (Position.Container, N);
         end if;
         N := Position.Container.Nodes (N).Parent;
      end loop;
      return No_Element;
   end Ancestor_Find;

   function Ancestor_Find (Container : Tree;
                           Position  : Cursor;
                           Item      : Element_Type)
      return Cursor is
   begin
      Check_Node (Container, Position);
      return Ancestor_Find (Position, Item);
   end Ancestor_Find;

   function Contains (Container : Tree;
                      Item      : Element_Type) return Boolean is
     (Find_Node (Container, Root_Node, Item) /= 0);

   procedure Iterate_Nodes
     (C       : Tree_Access;
      Top     : Count_Type;
      Process : not null access procedure (Position : in Cursor))
   is
      Guard : Busy_Guard (C);
      N     : Count_Type := First_Pre (C.all, Top);
   begin
      while N /= 0 loop
         Process ((C, N));
         N := Next_Pre (C.all, N, Top);
      end loop;
   end Iterate_Nodes;

   procedure Iterate
     (Container : in Tree;
      Process   : not null access procedure (Position : in Cursor)) is
   begin
      Iterate_Nodes (Var (Container), Root_Node, Process);
   end Iterate;

   procedure Iterate_Subtree
     (Position  : in Cursor;
      Process   : not null access procedure (Position : in Cursor)) is
   begin
      if Position.Container = null then
         raise Constraint_Error with "cursor has no node";
      end if;
      Iterate_Nodes (Position.Container, Position.Node, Process);
   end Iterate_Subtree;

   procedure Iterate_Subtree
     (Container : in Tree;
      Position  : in Cursor;
      Process   : not null access procedure (Position : in Cursor)) is
   begin
      Check_Node (Container, Position);
      Iterate_Nodes (Position.Container, Position.Node, Process);
   end Iterate_Subtree;

   function Iterate (Container : in Tree)
      return Tree_Iterator_Interfaces.Parallel_Iterator'Class is
     (Make_Iterator (Var (Container), Root_Node, False));

   function Iterate_Subtree (Position : in Cursor)
      return Tree_Iterator_Interfaces.Parallel_Iterator'Class is
   begin
      if Position.Container = null then
         raise Constraint_Error with "cursor has no node";
      end if;
      return Make_Iterator (Position.Container, Position.Node, False);
   end Iterate_Subtree;

   function Iterate_Subtree (Container : in Tree; Position : in Cursor)
      return Tree_Iterator_Interfaces.Parallel_Iterator'Class is
   begin
      Check_Node (Container, Position);
      return Make_Iterator (Position.Container, Position.Node, False);
   end Iterate_Subtree;

   function Child_Count (Parent : Cursor) return Count_Type is
      N : Count_Type;
      K : Count_Type := 0;
   begin
      if Parent.Container = null then
         return 0;
      end if;
      N := FC (Parent.Container.all, Parent.Node);
      while N /= 0 loop
         K := K + 1;
         N := Parent.Container.Nodes (N).Next;
      end loop;
      return K;
   end Child_Count;

   function Child_Count (Container : Tree; Parent : Cursor)
      return Count_Type is
   begin
      Check_Meaningful (Container, Parent);
      return Child_Count (Parent);
   end Child_Count;

   function Child_Depth (Parent, Child : Cursor) return Count_Type is
      N : Count_Type;
      D : Count_Type := 0;
   begin
      if Parent.Container = null or else Child.Container = null then
         raise Constraint_Error with "cursor has no node";
      end if;
      N := Child.Node;
      while N /= Parent.Node loop
         if N = 0 or else Parent.Container /= Child.Container then
            raise Program_Error with "Parent is not an ancestor of Child";
         end if;
         D := D + 1;
         N := Parent_Of (Child.Container.all, N);
      end loop;
      return D;
   end Child_Depth;

   function Child_Depth (Container : Tree; Parent, Child : Cursor)
      return Count_Type is
   begin
      if Parent.Container = null or else Child.Container = null then
         raise Constraint_Error with "cursor has no node";
      end if;
      Check_Meaningful (Container, Parent);
      Check_Meaningful (Container, Child);
      return Child_Depth (Parent, Child);
   end Child_Depth;

   --  Inserts Count copies of New_Item; Node is the first new node.
   procedure Insert_Copies (Container : in out Tree;
                            Parent    : Count_Type;
                            Before    : Count_Type;
                            New_Item  : Element_Type;
                            Count     : Count_Type;
                            Node      : out Count_Type)
   is
      N : Count_Type;
   begin
      Node := 0;
      for J in 1 .. Count loop
         New_Node (Container, New_Item, N);
         Link_Child (Container, N, Parent, Before);
         if J = 1 then
            Node := N;
         end if;
      end loop;
   end Insert_Copies;

   procedure Insert_Child (Container : in out Tree;
                           Parent    : in     Cursor;
                           Before    : in     Cursor;
                           New_Item  : in     Element_Type;
                           Count     : in     Count_Type := 1)
   is
      N : Count_Type;
   begin
      Check_Cursors (Container);
      Check_Parent_Before (Container, Parent, Before);
      Check_Room (Container, Count);
      Insert_Copies (Container, Parent.Node, Before.Node, New_Item, Count, N);
   end Insert_Child;

   procedure Insert_Child (Container : in out Tree;
                           Parent    : in     Cursor;
                           Before    : in     Cursor;
                           New_Item  : in     Element_Type;
                           Position  :    out Cursor;
                           Count     : in     Count_Type := 1)
   is
      N : Count_Type;
   begin
      Check_Cursors (Container);
      Check_Parent_Before (Container, Parent, Before);
      Check_Room (Container, Count);
      Insert_Copies (Container, Parent.Node, Before.Node, New_Item, Count, N);
      Position := (if N = 0 then Before else (Var (Container), N));
   end Insert_Child;

   procedure Insert_Child (Container : in out Tree;
                           Parent    : in     Cursor;
                           Before    : in     Cursor;
                           Position  :    out Cursor;
                           Count     : in     Count_Type := 1)
   is
      N, First : Count_Type := 0;
   begin
      Check_Cursors (Container);
      Check_Parent_Before (Container, Parent, Before);
      Check_Room (Container, Count);
      for J in 1 .. Count loop
         declare
            Default : Element_Type;
         begin
            Allocate (Container, N);
            Container.Nodes (N).Element := Default;
         end;
         Link_Child (Container, N, Parent.Node, Before.Node);
         if J = 1 then
            First := N;
         end if;
      end loop;
      Position := (if First = 0 then Before else (Var (Container), First));
   end Insert_Child;

   procedure Prepend_Child (Container : in out Tree;
                            Parent    : in     Cursor;
                            New_Item  : in     Element_Type;
                            Count     : in     Count_Type := 1)
   is
      N : Count_Type;
   begin
      Check_Cursors (Container);
      Check_Node (Container, Parent);
      Check_Room (Container, Count);
      Insert_Copies (Container, Parent.Node, FC (Container, Parent.Node),
                     New_Item, Count, N);
   end Prepend_Child;

   procedure Append_Child (Container : in out Tree;
                           Parent    : in     Cursor;
                           New_Item  : in     Element_Type;
                           Count     : in     Count_Type := 1)
   is
      N : Count_Type;
   begin
      Check_Cursors (Container);
      Check_Node (Container, Parent);
      Check_Room (Container, Count);
      Insert_Copies (Container, Parent.Node, 0, New_Item, Count, N);
   end Append_Child;

   procedure Delete_Children (Container : in out Tree;
                              Parent    : in     Cursor) is
   begin
      Check_Cursors (Container);
      Check_Node (Container, Parent);
      Free_Children (Container, Parent.Node);
   end Delete_Children;

   --  Copies the subtree rooted at Top of Source as a child of Parent
   --  before Before in Target.
   procedure Copy_Into (Target : in out Tree;
                        Parent : Count_Type;
                        Before : Count_Type;
                        Source : Tree_Access;
                        Top    : Count_Type)
   is
      N : Count_Type;
   begin
      Check_Room (Target, Subtree_Count (Source.all, Top));
      Copy_Nodes (Target, Source, Top, N);
      Link_Child (Target, N, Parent, Before);
   end Copy_Into;

   procedure Copy_Subtree (Target   : in out Tree;
                           Parent   : in     Cursor;
                           Before   : in     Cursor;
                           Source   : in     Cursor) is
   begin
      Check_Cursors (Target);
      Check_Parent_Before (Target, Parent, Before);
      if Source.Container = null then
         return;
      elsif Source.Node = Root_Node then
         raise Constraint_Error with "Source designates a root node";
      end if;
      Copy_Into (Target, Parent.Node, Before.Node, Source.Container,
                 Source.Node);
   end Copy_Subtree;

   procedure Copy_Local_Subtree (Target   : in out Tree;
                                 Parent   : in     Cursor;
                                 Before   : in     Cursor;
                                 Source   : in     Cursor) is
   begin
      Check_Cursors (Target);
      Check_Parent_Before (Target, Parent, Before);
      Check_Meaningful (Target, Source);
      if Source.Container = null then
         return;
      elsif Source.Node = Root_Node then
         raise Constraint_Error with "Source designates a root node";
      end if;
      Copy_Into (Target, Parent.Node, Before.Node, Var (Target), Source.Node);
   end Copy_Local_Subtree;

   procedure Copy_Subtree (Target   : in out Tree;
                           Parent   : in     Cursor;
                           Before   : in     Cursor;
                           Source   : in     Tree;
                           Subtree  : in     Cursor) is
   begin
      Check_Cursors (Target);
      Check_Parent_Before (Target, Parent, Before);
      Check_Meaningful (Source, Subtree);
      if Subtree.Container = null then
         return;
      elsif Subtree.Node = Root_Node then
         raise Constraint_Error with "Subtree designates a root node";
      end if;
      Copy_Into (Target, Parent.Node, Before.Node, Var (Source), Subtree.Node);
   end Copy_Subtree;

   --  Moves the subtree rooted at Top of Source (another tree) to be a
   --  child of Parent before Before in Target; returns the new top.
   procedure Transfer (Target : in out Tree;
                       Parent : Count_Type;
                       Before : Count_Type;
                       Source : in out Tree;
                       Top    : Count_Type;
                       Result : out Count_Type) is
   begin
      Copy_Nodes (Target, Var (Source), Top, Result);
      Unlink (Source, Top);
      Free_Subtree (Source, Top);
      Link_Child (Target, Result, Parent, Before);
   end Transfer;

   procedure Splice_Subtree (Target   : in out Tree;
                             Parent   : in     Cursor;
                             Before   : in     Cursor;
                             Source   : in out Tree;
                             Position : in out Cursor)
   is
      N : Count_Type;
   begin
      Check_Cursors (Target);
      Check_Cursors (Source);
      Check_Parent_Before (Target, Parent, Before);
      Check_Element (Source, Position);
      if Var (Target) = Var (Source) then
         Splice_Subtree (Target, Parent, Before, Position);
         return;
      end if;
      Check_Room (Target, Subtree_Count (Source, Position.Node));
      Transfer (Target, Parent.Node, Before.Node, Source, Position.Node, N);
      Position := (Var (Target), N);
   end Splice_Subtree;

   procedure Splice_Subtree (Container: in out Tree;
                             Parent   : in     Cursor;
                             Before   : in     Cursor;
                             Position : in     Cursor) is
   begin
      Check_Cursors (Container);
      Check_Parent_Before (Container, Parent, Before);
      Check_Element (Container, Position);
      if Position.Node = Before.Node then
         return;
      elsif Is_Ancestor (Container, Position.Node, Parent.Node) then
         raise Constraint_Error with "Position is an ancestor of Parent";
      end if;
      Unlink (Container, Position.Node);
      Link_Child (Container, Position.Node, Parent.Node, Before.Node);
   end Splice_Subtree;

   procedure Splice_Children (Target          : in out Tree;
                              Target_Parent   : in     Cursor;
                              Before          : in     Cursor;
                              Source          : in out Tree;
                              Source_Parent   : in     Cursor)
   is
      C, N : Count_Type;
   begin
      Check_Cursors (Target);
      Check_Cursors (Source);
      Check_Parent_Before (Target, Target_Parent, Before);
      Check_Node (Source, Source_Parent);
      if Var (Target) = Var (Source) then
         Splice_Children (Target, Target_Parent, Before, Source_Parent);
         return;
      end if;
      Check_Room (Target, Subtree_Count (Source, Source_Parent.Node) -
                    (if Source_Parent.Node = Root_Node then 1 else 1));
      loop
         C := FC (Source, Source_Parent.Node);
         exit when C = 0;
         Transfer (Target, Target_Parent.Node, Before.Node, Source, C, N);
      end loop;
   end Splice_Children;

   procedure Splice_Children (Container       : in out Tree;
                              Target_Parent   : in     Cursor;
                              Before          : in     Cursor;
                              Source_Parent   : in     Cursor)
   is
      C : Count_Type;
   begin
      Check_Cursors (Container);
      Check_Parent_Before (Container, Target_Parent, Before);
      Check_Node (Container, Source_Parent);
      if Target_Parent.Node = Source_Parent.Node then
         return;
      elsif Is_Ancestor (Container, Source_Parent.Node, Target_Parent.Node) then
         raise Constraint_Error with
           "Source_Parent is an ancestor of Target_Parent";
      end if;
      loop
         C := FC (Container, Source_Parent.Node);
         exit when C = 0;
         Unlink (Container, C);
         Link_Child (Container, C, Target_Parent.Node, Before.Node);
      end loop;
   end Splice_Children;

   function Parent (Position : Cursor) return Cursor is
     (if Position.Container = null then No_Element
      else Make_Cursor (Position.Container,
                        Parent_Of (Position.Container.all, Position.Node)));

   function Parent (Container : Tree;
                    Position  : Cursor) return Cursor is
   begin
      Check_Meaningful (Container, Position);
      return Parent (Position);
   end Parent;

   function First_Child (Parent : Cursor) return Cursor is
   begin
      if Parent.Container = null then
         raise Constraint_Error with "cursor has no node";
      end if;
      return Make_Cursor (Parent.Container,
                          FC (Parent.Container.all, Parent.Node));
   end First_Child;

   function First_Child (Container : Tree;
                         Parent    : Cursor) return Cursor is
   begin
      Check_Node (Container, Parent);
      return First_Child (Parent);
   end First_Child;

   function First_Child_Element (Parent : Cursor) return Element_Type is
      C : constant Cursor := First_Child (Parent);
   begin
      if C.Container = null then
         raise Constraint_Error with "node has no children";
      end if;
      return C.Container.Nodes (C.Node).Element;
   end First_Child_Element;

   function First_Child_Element (Container : Tree;
                                 Parent    : Cursor) return Element_Type is
   begin
      Check_Node (Container, Parent);
      return First_Child_Element (Parent);
   end First_Child_Element;

   function Last_Child (Parent : Cursor) return Cursor is
   begin
      if Parent.Container = null then
         raise Constraint_Error with "cursor has no node";
      end if;
      return Make_Cursor (Parent.Container,
                          LC (Parent.Container.all, Parent.Node));
   end Last_Child;

   function Last_Child (Container : Tree;
                        Parent    : Cursor) return Cursor is
   begin
      Check_Node (Container, Parent);
      return Last_Child (Parent);
   end Last_Child;

   function Last_Child_Element (Parent : Cursor) return Element_Type is
      C : constant Cursor := Last_Child (Parent);
   begin
      if C.Container = null then
         raise Constraint_Error with "node has no children";
      end if;
      return C.Container.Nodes (C.Node).Element;
   end Last_Child_Element;

   function Last_Child_Element (Container : Tree;
                                Parent    : Cursor) return Element_Type is
   begin
      Check_Node (Container, Parent);
      return Last_Child_Element (Parent);
   end Last_Child_Element;

   function Next_Sibling (Position : Cursor) return Cursor is
     (if Position.Container = null or else Position.Node = Root_Node
      then No_Element
      else Make_Cursor (Position.Container,
                        Position.Container.Nodes (Position.Node).Next));

   function Next_Sibling (Container : Tree;
                          Position  : Cursor) return Cursor is
   begin
      Check_Meaningful (Container, Position);
      return Next_Sibling (Position);
   end Next_Sibling;

   procedure Next_Sibling (Position : in out Cursor) is
   begin
      Position := Next_Sibling (Position);
   end Next_Sibling;

   procedure Next_Sibling (Container : in     Tree;
                           Position  : in out Cursor) is
   begin
      Position := Next_Sibling (Container, Position);
   end Next_Sibling;

   function Previous_Sibling (Position : Cursor) return Cursor is
     (if Position.Container = null or else Position.Node = Root_Node
      then No_Element
      else Make_Cursor (Position.Container,
                        Position.Container.Nodes (Position.Node).Prev));

   function Previous_Sibling (Container : Tree;
                              Position  : Cursor) return Cursor is
   begin
      Check_Meaningful (Container, Position);
      return Previous_Sibling (Position);
   end Previous_Sibling;

   procedure Previous_Sibling (Position : in out Cursor) is
   begin
      Position := Previous_Sibling (Position);
   end Previous_Sibling;

   procedure Previous_Sibling (Container : in     Tree;
                                  Position  : in out Cursor) is
   begin
      Position := Previous_Sibling (Container, Position);
   end Previous_Sibling;

   procedure Iterate_Children_Of
     (C       : Tree_Access;
      Parent  : Count_Type;
      Forward : Boolean;
      Process : not null access procedure (Position : in Cursor))
   is
      Guard : Busy_Guard (C);
      N     : Count_Type :=
        (if Forward then FC (C.all, Parent) else LC (C.all, Parent));
   begin
      while N /= 0 loop
         Process ((C, N));
         N := (if Forward then C.Nodes (N).Next else C.Nodes (N).Prev);
      end loop;
   end Iterate_Children_Of;

   procedure Iterate_Children
     (Parent  : in Cursor;
      Process : not null access procedure (Position : in Cursor)) is
   begin
      if Parent.Container = null then
         raise Constraint_Error with "cursor has no node";
      end if;
      Iterate_Children_Of (Parent.Container, Parent.Node, True, Process);
   end Iterate_Children;

   procedure Iterate_Children
     (Container : in Tree;
      Parent    : in Cursor;
      Process   : not null access procedure (Position : in Cursor)) is
   begin
      Check_Node (Container, Parent);
      Iterate_Children_Of (Parent.Container, Parent.Node, True, Process);
   end Iterate_Children;

   procedure Reverse_Iterate_Children
     (Parent  : in Cursor;
      Process : not null access procedure (Position : in Cursor)) is
   begin
      if Parent.Container = null then
         raise Constraint_Error with "cursor has no node";
      end if;
      Iterate_Children_Of (Parent.Container, Parent.Node, False, Process);
   end Reverse_Iterate_Children;

   procedure Reverse_Iterate_Children
     (Container : in Tree;
      Parent    : in Cursor;
      Process   : not null access procedure (Position : in Cursor)) is
   begin
      Check_Node (Container, Parent);
      Iterate_Children_Of (Parent.Container, Parent.Node, False, Process);
   end Reverse_Iterate_Children;

   function Iterate_Children (Container : in Tree; Parent : in Cursor)
      return Tree_Iterator_Interfaces.Parallel_Reversible_Iterator'Class is
   begin
      Check_Node (Container, Parent);
      return Make_Iterator (Parent.Container, Parent.Node, True);
   end Iterate_Children;

end Ada.Containers.Bounded_Multiway_Trees;
