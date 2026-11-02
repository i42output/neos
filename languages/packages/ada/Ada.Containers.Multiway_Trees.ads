--  Ada.Containers.Multiway_Trees.ads
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
--
--  This package specification is an altered copy of the specification of
--  package Ada.Containers.Multiway_Trees in the Ada Reference Manual, 202x Edition, Draft 25 (A.18.10),
--  the draft of ISO/IEC 8652:2023, which carries the following notices:
--
--    Ada Reference Manual - Language and Standard Libraries
--
--    Copyright © 1992, 1993, 1994, 1995, Intermetrics, Inc.
--
--    This copyright is assigned to the U.S. Government. All rights reserved.
--
--    This document may be copied, in whole or in part, in any form or by any means,
--    as is or with alterations, provided that (1) alterations are clearly marked as
--    alterations and (2) this copyright notice is included unmodified in any copy.
--    Compiled copies of standard library units and examples need not contain this
--    copyright notice so long as the notice is included in all copies of source
--    code and documentation.
--
--
--
--    Technical Corrigendum 1
--
--    Copyright © 2000, The MITRE Corporation. All Rights Reserved.
--
--    This document may be copied, in whole or in part, in any form or by any means,
--    as is, or with alterations, provided that (1) alterations are clearly marked
--    as alterations and (2) this copyright notice is included unmodified in any
--    copy. Any other use or distribution of this document is prohibited without the
--    prior express permission of MITRE.
--
--    You use this document on the condition that you indemnify and hold harmless
--    MITRE, its Board of Trustees, officers, agents, and employees, from any and
--    all liability or damages to yourself or your hardware or software, or third
--    parties, including attorneys' fees, court costs, and other related costs and
--    expenses, arising out of your use of this document irrespective of the cause
--    of said liability.
--
--    MITRE MAKES THIS DOCUMENT AVAILABLE ON AN "AS IS" BASIS AND MAKES NO WARRANTY,
--    EXPRESS OR IMPLIED, AS TO THE ACCURACY, CAPABILITY, EFFICIENCY
--    MERCHANTABILITY, OR FUNCTIONING OF THIS DOCUMENT. IN NO EVENT WILL MITRE BE
--    LIABLE FOR ANY GENERAL, CONSEQUENTIAL, INDIRECT, INCIDENTAL, EXEMPLARY, OR
--    SPECIAL DAMAGES, EVEN IF MITRE HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH
--    DAMAGES.
--
--
--
--    Amendment 1
--
--    Copyright © 2004, 2005, 2006, 2007, AXE Consultants. All Rights Reserved.
--
--    This document may be copied, in whole or in part, in any form or by any means,
--    as is, or with alterations, provided that (1) alterations are clearly marked
--    as alterations and (2) this copyright notice is included unmodified in any
--    copy. Any other use or distribution of this document is prohibited without the
--    prior express permission of AXE.
--
--    You use this document on the condition that you indemnify and hold harmless
--    AXE, its board, officers, agents, and employees, from any and all liability or
--    damages to yourself or your hardware or software, or third parties, including
--    attorneys' fees, court costs, and other related costs and expenses, arising
--    out of your use of this document irrespective of the cause of said liability.
--
--    AXE MAKES THIS DOCUMENT AVAILABLE ON AN "AS IS" BASIS AND MAKES NO WARRANTY,
--    EXPRESS OR IMPLIED, AS TO THE ACCURACY, CAPABILITY, EFFICIENCY
--    MERCHANTABILITY, OR FUNCTIONING OF THIS DOCUMENT. IN NO EVENT WILL AXE BE
--    LIABLE FOR ANY GENERAL, CONSEQUENTIAL, INDIRECT, INCIDENTAL, EXEMPLARY, OR
--    SPECIAL DAMAGES, EVEN IF AXE HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH
--    DAMAGES.
--
--    Third Edition
--
--    Copyright © 2008, 2009, 2010, 2011, 2012 AXE Consultants. All Rights Reserved.
--
--    This document may be copied, in whole or in part, in any form or by any means,
--    as is, or with alterations, provided that (1) alterations are clearly marked
--    as alterations and (2) this copyright notice is included unmodified in any
--    copy. Any other use or distribution of this document is prohibited without the
--    prior express permission of AXE.
--
--    You use this document on the condition that you indemnify and hold harmless
--    AXE, its board, officers, agents, and employees, from any and all liability or
--    damages to yourself or your hardware or software, or third parties, including
--    attorneys' fees, court costs, and other related costs and expenses, arising
--    out of your use of this document irrespective of the cause of said liability.
--
--    AXE MAKES THIS DOCUMENT AVAILABLE ON AN "AS IS" BASIS AND MAKES NO WARRANTY,
--    EXPRESS OR IMPLIED, AS TO THE ACCURACY, CAPABILITY, EFFICIENCY
--    MERCHANTABILITY, OR FUNCTIONING OF THIS DOCUMENT. IN NO EVENT WILL AXE BE
--    LIABLE FOR ANY GENERAL, CONSEQUENTIAL, INDIRECT, INCIDENTAL, EXEMPLARY, OR
--    SPECIAL DAMAGES, EVEN IF AXE HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH
--    DAMAGES.
--
--    Technical Corrigendum 1 for Ada 2012
--
--    Copyright © 2013, 2014, 2015, 2016 AXE Consultants. All Rights Reserved.
--
--    This document may be copied, in whole or in part, in any form or by any means,
--    as is, or with alterations, provided that (1) alterations are clearly marked
--    as alterations and (2) this copyright notice is included unmodified in any
--    copy. Any other use or distribution of this document is prohibited without the
--    prior express permission of AXE.
--
--    You use this document on the condition that you indemnify and hold harmless
--    AXE, its board, officers, agents, and employees, from any and all liability or
--    damages to yourself or your hardware or software, or third parties, including
--    attorneys' fees, court costs, and other related costs and expenses, arising
--    out of your use of this document irrespective of the cause of said liability.
--
--    AXE MAKES THIS DOCUMENT AVAILABLE ON AN "AS IS" BASIS AND MAKES NO WARRANTY,
--    EXPRESS OR IMPLIED, AS TO THE ACCURACY, CAPABILITY, EFFICIENCY
--    MERCHANTABILITY, OR FUNCTIONING OF THIS DOCUMENT. IN NO EVENT WILL AXE BE
--    LIABLE FOR ANY GENERAL, CONSEQUENTIAL, INDIRECT, INCIDENTAL, EXEMPLARY, OR
--    SPECIAL DAMAGES, EVEN IF AXE HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH
--    DAMAGES.
--
--    Ada 202x version
--
--    Copyright © 2016, 2017, 2018, 2019, 2020 AXE Consultants. All Rights Reserved.
--
--    This document may be copied, in whole or in part, in any form or by any means,
--    as is, or with alterations, provided that (1) alterations are clearly marked
--    as alterations and (2) this copyright notice is included unmodified in any
--    copy. Any other use or distribution of this document is prohibited without the
--    prior express permission of AXE.
--
--    You use this document on the condition that you indemnify and hold harmless
--    AXE, its board, officers, agents, and employees, from any and all liability or
--    damages to yourself or your hardware or software, or third parties, including
--    attorneys' fees, court costs, and other related costs and expenses, arising
--    out of your use of this document irrespective of the cause of said liability.
--
--    AXE MAKES THIS DOCUMENT AVAILABLE ON AN "AS IS" BASIS AND MAKES NO WARRANTY,
--    EXPRESS OR IMPLIED, AS TO THE ACCURACY, CAPABILITY, EFFICIENCY
--    MERCHANTABILITY, OR FUNCTIONING OF THIS DOCUMENT. IN NO EVENT WILL AXE BE
--    LIABLE FOR ANY GENERAL, CONSEQUENTIAL, INDIRECT, INCIDENTAL, EXEMPLARY, OR
--    SPECIAL DAMAGES, EVEN IF AXE HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH
--    DAMAGES.
--
--
--
--    Ada 2005 Consolidated Standard
--
--    Copyright © 2004, 2005, 2006, Ada-Europe.
--
--    This document may be copied, in whole or in part, in any form or by any means,
--    as is, or with alterations, provided that (1) alterations are clearly marked
--    as alterations and (2) this copyright notice is included unmodified in any
--    copy. Any other use or distribution of this document is prohibited without the
--    prior express permission of Ada-Europe.
--
--    You use this document on the condition that you indemnify and hold harmless
--    Ada-Europe and its Board from any and all liability or damages to yourself or
--    your hardware or software, or third parties, including attorneys' fees, court
--    costs, and other related costs and expenses, arising out of your use of this
--    document irrespective of the cause of said liability.
--
--    ADA-EUROPE MAKES THIS DOCUMENT AVAILABLE ON AN "AS IS" BASIS AND MAKES NO
--    WARRANTY, EXPRESS OR IMPLIED, AS TO THE ACCURACY, CAPABILITY, EFFICIENCY
--    MERCHANTABILITY, OR FUNCTIONING OF THIS DOCUMENT. IN NO EVENT WILL ADA-EUROPE
--    BE LIABLE FOR ANY GENERAL, CONSEQUENTIAL, INDIRECT, INCIDENTAL, EXEMPLARY, OR
--    SPECIAL DAMAGES, EVEN IF ADA-EUROPE HAS BEEN ADVISED OF THE POSSIBILITY OF
--    SUCH DAMAGES.
--
--
--  Alterations (neos):
--  the Pre, Post, Default_Initial_Condition, Stable_Properties, Global,
--  Nonblocking and Use_Formal aspects are omitted (the body makes the checks
--  of the preconditions, raising the same exceptions), as is the aspect
--  Iterator_View of type Tree (not supported by GNAT 13), and the nested
--  package Stable is omitted; private with clauses for Ada.Finalization
--  and Ada.Streams are added; the private part is neos's.
--  (See languages/packages/ada/NOTICE.)

with Ada.Iterator_Interfaces;
private with Ada.Finalization;
private with Ada.Streams;
generic
   type Element_Type is private;
   with function "=" (Left, Right : Element_Type) return Boolean is <>;
package Ada.Containers.Multiway_Trees
   with Preelaborate, Remote_Types is

   type Tree is tagged private
      with Constant_Indexing => Constant_Reference,
           Variable_Indexing => Reference,
           Default_Iterator  => Iterate,
           Iterator_Element  => Element_Type;
   pragma Preelaborable_Initialization(Tree);

   type Cursor is private;
   pragma Preelaborable_Initialization(Cursor);

   Empty_Tree : constant Tree;

   No_Element : constant Cursor;

   function Equal_Element (Left, Right : Element_Type)
      return Boolean renames "=";

   function Has_Element (Position : Cursor) return Boolean;

   function Has_Element (Container : Tree; Position : Cursor)
      return Boolean;

   package Tree_Iterator_Interfaces is new
      Ada.Iterator_Interfaces (Cursor, Has_Element);

   function Equal_Subtree (Left_Position : Cursor;
                           Right_Position: Cursor) return Boolean;

   function "=" (Left, Right : Tree) return Boolean;

   function Same_Object (Left, Right : Tree) return Boolean;

   function Tampering_With_Cursors_Prohibited
      (Container : Tree) return Boolean;

   function Tampering_With_Elements_Prohibited
      (Container : Tree) return Boolean;

   function Empty return Tree
      is (Empty_Tree);

   function Is_Empty (Container : Tree) return Boolean;

   function Node_Count (Container : Tree) return Count_Type;

   function Subtree_Node_Count (Position : Cursor) return Count_Type;

   function Subtree_Node_Count (Container : Tree; Position : Cursor)
      return Count_Type;

   function Depth (Position : Cursor) return Count_Type;

   function Depth (Container : Tree; Position : Cursor)
      return Count_Type;

   function Is_Root (Position : Cursor) return Boolean;

   function Is_Root (Container : Tree; Position : Cursor)
      return Boolean;

   function Is_Leaf (Position : Cursor) return Boolean;

   function Is_Leaf (Container : Tree; Position : Cursor)
      return Boolean;

   function Is_Ancestor_Of (Container : Tree;
          Parent   : Cursor;
          Position : Cursor) return Boolean;

   function Root (Container : Tree) return Cursor;

   function Meaningful_For
      (Container : Tree; Position : Cursor) return Boolean is
      (Position = No_Element or else
       Is_Root (Container, Position) or else
       Has_Element (Container, Position));

   procedure Clear (Container : in out Tree);

   function Element (Position : Cursor) return Element_Type;

   function Element (Container : Tree;
                     Position  : Cursor) return Element_Type;

   procedure Replace_Element (Container : in out Tree;
                              Position  : in     Cursor;
                              New_item  : in     Element_Type);

   procedure Query_Element
     (Position : in Cursor;
      Process  : not null access procedure (Element : in Element_Type));

   procedure Query_Element
     (Container : in Tree;
      Position  : in Cursor;
      Process   : not null access procedure (Element : in Element_Type));

   procedure Update_Element
     (Container : in out Tree;
      Position  : in     Cursor;
      Process   : not null access procedure
                      (Element : in out Element_Type));

   type Constant_Reference_Type
         (Element : not null access constant Element_Type) is private
      with Implicit_Dereference => Element;

   type Reference_Type (Element : not null access Element_Type) is private
      with Implicit_Dereference => Element;

   function Constant_Reference (Container : aliased in Tree;
                                Position  : in Cursor)
      return Constant_Reference_Type;

   function Reference (Container : aliased in out Tree;
                       Position  : in Cursor)
      return Reference_Type;

   procedure Assign (Target : in out Tree; Source : in Tree);

   function Copy (Source : Tree) return Tree;

   procedure Move (Target : in out Tree;
                   Source : in out Tree);

   procedure Delete_Leaf (Container : in out Tree;
                          Position  : in out Cursor);

   procedure Delete_Subtree (Container : in out Tree;
                             Position  : in out Cursor);

   procedure Swap (Container : in out Tree;
                   I, J      : in     Cursor);

   function Find (Container : Tree;
                  Item      : Element_Type)
      return Cursor;

   function Find_In_Subtree (Position : Cursor;
                             Item     : Element_Type)
      return Cursor;

   function Find_In_Subtree (Container : Tree;
                             Position  : Cursor;
                             Item      : Element_Type)
      return Cursor;

   function Ancestor_Find (Position : Cursor;
                           Item     : Element_Type)
      return Cursor;

   function Ancestor_Find (Container : Tree;
                           Position  : Cursor;
                           Item      : Element_Type)
      return Cursor;

   function Contains (Container : Tree;
                      Item      : Element_Type) return Boolean;

   procedure Iterate
     (Container : in Tree;
      Process   : not null access procedure (Position : in Cursor))
      with Allows_Exit;

   procedure Iterate_Subtree
     (Position  : in Cursor;
      Process   : not null access procedure (Position : in Cursor))
      with Allows_Exit;

   procedure Iterate_Subtree
     (Container : in Tree;
      Position  : in Cursor;
      Process   : not null access procedure (Position : in Cursor))
      with Allows_Exit;

   function Iterate (Container : in Tree)
      return Tree_Iterator_Interfaces.Parallel_Iterator'Class;

   function Iterate_Subtree (Position : in Cursor)
      return Tree_Iterator_Interfaces.Parallel_Iterator'Class;

   function Iterate_Subtree (Container : in Tree; Position : in Cursor)
      return Tree_Iterator_Interfaces.Parallel_Iterator'Class;

   function Child_Count (Parent : Cursor) return Count_Type;

   function Child_Count (Container : Tree; Parent : Cursor)
      return Count_Type;

   function Child_Depth (Parent, Child : Cursor) return Count_Type;

   function Child_Depth (Container : Tree; Parent, Child : Cursor)
      return Count_Type;

   procedure Insert_Child (Container : in out Tree;
                           Parent    : in     Cursor;
                           Before    : in     Cursor;
                           New_Item  : in     Element_Type;
                           Count     : in     Count_Type := 1);

   procedure Insert_Child (Container : in out Tree;
                           Parent    : in     Cursor;
                           Before    : in     Cursor;
                           New_Item  : in     Element_Type;
                           Position  :    out Cursor;
                           Count     : in     Count_Type := 1);

   procedure Insert_Child (Container : in out Tree;
                           Parent    : in     Cursor;
                           Before    : in     Cursor;
                           Position  :    out Cursor;
                           Count     : in     Count_Type := 1);

   procedure Prepend_Child (Container : in out Tree;
                            Parent    : in     Cursor;
                            New_Item  : in     Element_Type;
                            Count     : in     Count_Type := 1);

   procedure Append_Child (Container : in out Tree;
                           Parent    : in     Cursor;
                           New_Item  : in     Element_Type;
                           Count     : in     Count_Type := 1);

   procedure Delete_Children (Container : in out Tree;
                              Parent    : in     Cursor);

   procedure Copy_Subtree (Target   : in out Tree;
                           Parent   : in     Cursor;
                           Before   : in     Cursor;
                           Source   : in     Cursor);

   procedure Copy_Local_Subtree (Target   : in out Tree;
                                 Parent   : in     Cursor;
                                 Before   : in     Cursor;
                                 Source   : in     Cursor);

   procedure Copy_Subtree (Target   : in out Tree;
                           Parent   : in     Cursor;
                           Before   : in     Cursor;
                           Source   : in     Tree;
                           Subtree  : in     Cursor);

   procedure Splice_Subtree (Target   : in out Tree;
                             Parent   : in     Cursor;
                             Before   : in     Cursor;
                             Source   : in out Tree;
                             Position : in out Cursor);

   procedure Splice_Subtree (Container: in out Tree;
                             Parent   : in     Cursor;
                             Before   : in     Cursor;
                             Position : in     Cursor);

   procedure Splice_Children (Target          : in out Tree;
                              Target_Parent   : in     Cursor;
                              Before          : in     Cursor;
                              Source          : in out Tree;
                              Source_Parent   : in     Cursor);

   procedure Splice_Children (Container       : in out Tree;
                              Target_Parent   : in     Cursor;
                              Before          : in     Cursor;
                              Source_Parent   : in     Cursor);

   function Parent (Position : Cursor) return Cursor;

   function Parent (Container : Tree;
                    Position  : Cursor) return Cursor;

   function First_Child (Parent : Cursor) return Cursor;

   function First_Child (Container : Tree;
                         Parent    : Cursor) return Cursor;

   function First_Child_Element (Parent : Cursor) return Element_Type;

   function First_Child_Element (Container : Tree;
                                 Parent    : Cursor) return Element_Type;

   function Last_Child (Parent : Cursor) return Cursor;

   function Last_Child (Container : Tree;
                        Parent    : Cursor) return Cursor;

   function Last_Child_Element (Parent : Cursor) return Element_Type;

   function Last_Child_Element (Container : Tree;
                                Parent    : Cursor) return Element_Type;

   function Next_Sibling (Position : Cursor) return Cursor;

   function Next_Sibling (Container : Tree;
                          Position  : Cursor) return Cursor;

   procedure Next_Sibling (Position : in out Cursor);

   procedure Next_Sibling (Container : in     Tree;
                           Position  : in out Cursor);

   function Previous_Sibling (Position : Cursor) return Cursor;

   function Previous_Sibling (Container : Tree;
                              Position  : Cursor) return Cursor;

   procedure Previous_Sibling (Position : in out Cursor);

   procedure Previous_Sibling (Container : in     Tree;
                                  Position  : in out Cursor);

   procedure Iterate_Children
     (Parent  : in Cursor;
      Process : not null access procedure (Position : in Cursor))
      with Allows_Exit;

   procedure Iterate_Children
     (Container : in Tree;
      Parent    : in Cursor;
      Process   : not null access procedure (Position : in Cursor))
      with Allows_Exit;

   procedure Reverse_Iterate_Children
     (Parent  : in Cursor;
      Process : not null access procedure (Position : in Cursor))
      with Allows_Exit;

   procedure Reverse_Iterate_Children
     (Container : in Tree;
      Parent    : in Cursor;
      Process   : not null access procedure (Position : in Cursor))
      with Allows_Exit;

   function Iterate_Children (Container : in Tree; Parent : in Cursor)
      return Tree_Iterator_Interfaces.Parallel_Reversible_Iterator'Class;

private

   --  The nodes of a tree live in an array (a pool) and are linked by
   --  index; the root node has no element and is not in the pool: it is
   --  represented by the index Root_Node, its child links being components
   --  of the tree.  Free nodes are chained through Next from Free; nodes
   --  High + 1 .. the end of the array have never been used.
   Root_Node : constant Count_Type := Count_Type'Last;
   type Element_Access is access Element_Type;

   type Node_Type is record
      Element     : Element_Access;
      Parent      : Count_Type := 0;
      First_Child : Count_Type := 0;
      Last_Child  : Count_Type := 0;
      Next        : Count_Type := 0;
      Prev        : Count_Type := 0;
      Used        : Boolean := False;
   end record;

   type Node_Array is array (Count_Type range <>) of Node_Type;

   type Node_Array_Access is access Node_Array;

   --  Count is the number of nodes other than the root.  Busy counts the
   --  reasons for which tampering with cursors is prohibited (iterations);
   --  Lock counts the reasons for which tampering with elements is
   --  prohibited (Query_Element, Update_Element and references).
   type Tree is new Ada.Finalization.Controlled with record
      Nodes      : Node_Array_Access;
      Root_First : Count_Type := 0;
      Root_Last  : Count_Type := 0;
      Count      : Count_Type := 0;
      Free       : Count_Type := 0;
      High       : Count_Type := 0;
      Busy       : Natural := 0;
      Lock       : Natural := 0;
   end record;

   overriding procedure Adjust (Container : in out Tree);
   overriding procedure Finalize (Container : in out Tree);

   procedure Write
     (Stream    : not null access Ada.Streams.Root_Stream_Type'Class;
      Container : Tree);
   for Tree'Write use Write;

   procedure Read
     (Stream    : not null access Ada.Streams.Root_Stream_Type'Class;
      Container : out Tree);
   for Tree'Read use Read;

   type Tree_Access is access all Tree;

   type Cursor is record
      Container : Tree_Access;
      Node      : Count_Type := 0;
   end record;

   procedure Write
     (Stream   : not null access Ada.Streams.Root_Stream_Type'Class;
      Position : Cursor);
   for Cursor'Write use Write;

   procedure Read
     (Stream   : not null access Ada.Streams.Root_Stream_Type'Class;
      Position : out Cursor);
   for Cursor'Read use Read;

   --  The control part of a reference object: while it exists, tampering
   --  with the elements of Container.all is prohibited.
   type Reference_Control is new Ada.Finalization.Controlled with record
      Container : Tree_Access;
   end record;

   overriding procedure Adjust (Control : in out Reference_Control);
   overriding procedure Finalize (Control : in out Reference_Control);

   type Constant_Reference_Type
         (Element : not null access constant Element_Type) is record
      Control : Reference_Control := raise Program_Error;
   end record;

   procedure Write
     (Stream : not null access Ada.Streams.Root_Stream_Type'Class;
      Item   : Constant_Reference_Type);
   for Constant_Reference_Type'Write use Write;

   procedure Read
     (Stream : not null access Ada.Streams.Root_Stream_Type'Class;
      Item   : out Constant_Reference_Type);
   for Constant_Reference_Type'Read use Read;

   type Reference_Type (Element : not null access Element_Type) is record
      Control : Reference_Control := raise Program_Error;
   end record;

   procedure Write
     (Stream : not null access Ada.Streams.Root_Stream_Type'Class;
      Item   : Reference_Type);
   for Reference_Type'Write use Write;

   procedure Read
     (Stream : not null access Ada.Streams.Root_Stream_Type'Class;
      Item   : out Reference_Type);
   for Reference_Type'Read use Read;

   No_Element : constant Cursor := (Container => null, Node => 0);

   Empty_Tree : constant Tree :=
     (Ada.Finalization.Controlled with others => <>);

end Ada.Containers.Multiway_Trees;
