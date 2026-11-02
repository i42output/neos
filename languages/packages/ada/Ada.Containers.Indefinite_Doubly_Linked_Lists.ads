--  Ada.Containers.Indefinite_Doubly_Linked_Lists.ads
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
--  package Ada.Containers.Indefinite_Doubly_Linked_Lists in the Ada Reference Manual, 202x Edition, Draft 25 (A.18.12, A.18.3),
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
--  this is the specification of Containers.Doubly_Linked_Lists with the changes that
--  A.18.12 describes;
--  the procedure Append with a Count parameter defaulted to 1 is replaced by
--  two overloadings, one without Count and one with Count (not defaulted), so
--  that Append is a legal Add_Unnamed procedure of the aspect Aggregate (which
--  requires exactly two parameters, 4.3.5);
--  the Pre, Post, Default_Initial_Condition, Stable_Properties, Global,
--  Nonblocking and Use_Formal aspects are omitted (the body makes the checks
--  of the preconditions, raising the same exceptions), as is the aspect
--  Iterator_View of type List (not supported by GNAT 13), and the nested
--  package Stable is omitted; private with clauses for Ada.Finalization
--  and Ada.Streams are added; the private part is neos's.
--  (See languages/packages/ada/NOTICE.)

with Ada.Iterator_Interfaces;
private with Ada.Finalization;
private with Ada.Streams;
generic
   type Element_Type (<>) is private;
   with function "=" (Left, Right : Element_Type)
      return Boolean is <>;
package Ada.Containers.Indefinite_Doubly_Linked_Lists
   with Preelaborate, Remote_Types is

   type List is tagged private
      with Constant_Indexing => Constant_Reference,
           Variable_Indexing => Reference,
           Default_Iterator  => Iterate,
           Iterator_Element  => Element_Type,
           Aggregate         => (Empty       => Empty,
                                 Add_Unnamed => Append);
   pragma Preelaborable_Initialization(List);

   type Cursor is private;
   pragma Preelaborable_Initialization(Cursor);

   Empty_List : constant List;

   No_Element : constant Cursor;

   function Has_Element (Position : Cursor) return Boolean;

   function Has_Element (Container : List; Position : Cursor)
      return Boolean;

   package List_Iterator_Interfaces is new
       Ada.Iterator_Interfaces (Cursor, Has_Element);

   function "=" (Left, Right : List) return Boolean;

   function Same_Object (Left, Right : List) return Boolean;

   function Tampering_With_Cursors_Prohibited
      (Container : List) return Boolean;

   function Tampering_With_Elements_Prohibited
      (Container : List) return Boolean;

   function Empty return List
      is (Empty_List);

   function Length (Container : List) return Count_Type;

   function Is_Empty (Container : List) return Boolean;

   procedure Clear (Container : in out List);

   function Element (Position : Cursor) return Element_Type;

   function Element (Container : List;
                     Position  : Cursor) return Element_Type;

   procedure Replace_Element (Container : in out List;
                              Position  : in     Cursor;
                              New_item  : in     Element_Type);

   procedure Query_Element
     (Position : in Cursor;
      Process  : not null access procedure (Element : in Element_Type));

   procedure Query_Element
     (Container : in List;
      Position  : in Cursor;
      Process   : not null access procedure (Element : in Element_Type));

   procedure Update_Element
     (Container : in out List;
      Position  : in     Cursor;
      Process   : not null access procedure
                      (Element : in out Element_Type));

   type Constant_Reference_Type
         (Element : not null access constant Element_Type) is private
      with Implicit_Dereference => Element;

   type Reference_Type (Element : not null access Element_Type) is private
      with Implicit_Dereference => Element;

   function Constant_Reference (Container : aliased in List;
                                Position  : in Cursor)
      return Constant_Reference_Type;

   function Reference (Container : aliased in out List;
                       Position  : in Cursor)
      return Reference_Type;

   procedure Assign (Target : in out List; Source : in List);

   function Copy (Source : List)
      return List;

   procedure Move (Target : in out List;
                   Source : in out List);

   procedure Insert (Container : in out List;
                     Before    : in     Cursor;
                     New_Item  : in     Element_Type;
                     Count     : in     Count_Type := 1);

   procedure Insert (Container : in out List;
                     Before    : in     Cursor;
                     New_Item  : in     Element_Type;
                     Position  :    out Cursor;
                     Count     : in     Count_Type := 1);

   procedure Prepend (Container : in out List;
                      New_Item  : in     Element_Type;
                      Count     : in     Count_Type := 1);

   procedure Append (Container : in out List;
                     New_Item  : in     Element_Type);

   procedure Append (Container : in out List;
                     New_Item  : in     Element_Type;
                     Count     : in     Count_Type);

   procedure Delete (Container : in out List;
                     Position  : in out Cursor;
                     Count     : in     Count_Type := 1);

   procedure Delete_First (Container : in out List;
                           Count     : in     Count_Type := 1);

   procedure Delete_Last (Container : in out List;
                          Count     : in     Count_Type := 1);

   procedure Reverse_Elements (Container : in out List);

   procedure Swap (Container : in out List;
                   I, J      : in     Cursor);

   procedure Swap_Links (Container : in out List;
                         I, J      : in     Cursor);

   procedure Splice (Target   : in out List;
                     Before   : in     Cursor;
                     Source   : in out List);

   procedure Splice (Target   : in out List;
                     Before   : in     Cursor;
                     Source   : in out List;
                     Position : in out Cursor);

   procedure Splice (Container: in out List;
                     Before   : in     Cursor;
                     Position : in     Cursor);

   function First (Container : List) return Cursor;

   function First_Element (Container : List)
      return Element_Type;

   function Last (Container : List) return Cursor;

   function Last_Element (Container : List)
      return Element_Type;

   function Next (Position : Cursor) return Cursor;

   function Next (Container : List;
                  Position : Cursor) return Cursor;

   procedure Next (Position : in out Cursor);

   procedure Next (Container : in     List;
                   Position  : in out Cursor);

   function Previous (Position : Cursor) return Cursor;

   function Previous (Container : List;
                      Position  : Cursor) return Cursor;

   procedure Previous (Position : in out Cursor);

   procedure Previous (Container : in     List;
                       Position  : in out Cursor);

   function Find (Container : List;
                  Item      : Element_Type;
                  Position  : Cursor := No_Element)
      return Cursor;

   function Reverse_Find (Container : List;
                          Item      : Element_Type;
                          Position  : Cursor := No_Element)
      return Cursor;

   function Contains (Container : List;
                      Item      : Element_Type) return Boolean;

   procedure  Iterate
     (Container : in List;
      Process   : not null access procedure (Position : in Cursor))
      with Allows_Exit;

   procedure Reverse_Iterate
     (Container : in List;
      Process   : not null access procedure (Position : in Cursor))
      with Allows_Exit;

   function Iterate (Container : in List)
      return List_Iterator_Interfaces.Parallel_Reversible_Iterator'Class;

   function Iterate (Container : in List; Start : in Cursor)
      return List_Iterator_Interfaces.Reversible_Iterator'Class;

   generic
      with function "<" (Left, Right : Element_Type)
         return Boolean is <>;
   package Generic_Sorting is

      function Less_Element (Left, Right : Element_Type) return Boolean
         renames "<";

      function Is_Sorted (Container : List) return Boolean;

      procedure Sort (Container : in out List);

      procedure Merge (Target  : in out List;
                       Source  : in out List);

   end Generic_Sorting;

private

   --  The nodes of a list live in an array (a pool); they are linked through
   --  their indices.  Free nodes are chained through Next from Free; nodes
   --  High + 1 .. the end of the array have never been used.
   type Element_Access is access Element_Type;

   type Node_Type is record
      Element : Element_Access;
      Next    : Count_Type := 0;
      Prev    : Count_Type := 0;
      Used    : Boolean := False;
   end record;

   type Node_Array is array (Count_Type range <>) of Node_Type;

   type Node_Array_Access is access Node_Array;

   --  Busy counts the reasons for which tampering with cursors is
   --  prohibited (iterations); Lock counts the reasons for which tampering
   --  with elements is prohibited (Query_Element, Update_Element and
   --  references).
   type List is new Ada.Finalization.Controlled with record
      Nodes  : Node_Array_Access;
      First  : Count_Type := 0;
      Last   : Count_Type := 0;
      Length : Count_Type := 0;
      Free   : Count_Type := 0;
      High   : Count_Type := 0;
      Busy   : Natural := 0;
      Lock   : Natural := 0;
   end record;

   overriding procedure Adjust (Container : in out List);
   overriding procedure Finalize (Container : in out List);

   procedure Write
     (Stream    : not null access Ada.Streams.Root_Stream_Type'Class;
      Container : List);
   for List'Write use Write;

   procedure Read
     (Stream    : not null access Ada.Streams.Root_Stream_Type'Class;
      Container : out List);
   for List'Read use Read;

   type List_Access is access all List;

   type Cursor is record
      Container : List_Access;
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
      Container : List_Access;
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

   Empty_List : constant List :=
     (Ada.Finalization.Controlled with others => <>);

   No_Element : constant Cursor := (Container => null, Node => 0);
end Ada.Containers.Indefinite_Doubly_Linked_Lists;
