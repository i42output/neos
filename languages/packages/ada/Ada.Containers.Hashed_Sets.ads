--  Ada.Containers.Hashed_Sets.ads
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
--  package Ada.Containers.Hashed_Sets in the Ada Reference Manual, 202x Edition, Draft 25 (A.18.8, A.18.7),
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
--  the procedure Delete (Container, Item) that A.18.7 describes, missing from
--  the declaration in A.18.8, is added; the package Generic_Keys, whose
--  visible part declares a private type Reference_Type, is given a private
--  part (neos's);
--  the implementation-defined default of the Capacity parameter of Empty is
--  10;
--  the Pre, Post, Default_Initial_Condition, Stable_Properties, Global,
--  Nonblocking and Use_Formal aspects are omitted (the body makes the checks
--  of the preconditions, raising the same exceptions), as is the aspect
--  Iterator_View of type Set (not supported by GNAT 13), and the nested
--  package Stable is omitted; private with clauses for Ada.Finalization
--  and Ada.Streams are added; the private part is neos's.
--  (See languages/packages/ada/NOTICE.)

with Ada.Iterator_Interfaces;
private with Ada.Finalization;
private with Ada.Streams;
generic
   type Element_Type is private;
   with function Hash (Element : Element_Type) return Hash_Type;
   with function Equivalent_Elements (Left, Right : Element_Type)
                 return Boolean;
   with function "=" (Left, Right : Element_Type) return Boolean is <>;
package Ada.Containers.Hashed_Sets
   with Preelaborate, Remote_Types is

   type Set is tagged private
      with Constant_Indexing => Constant_Reference,
           Default_Iterator  => Iterate,
           Iterator_Element  => Element_Type,
           Aggregate         => (Empty       => Empty,
                                 Add_Unnamed => Include);
   pragma Preelaborable_Initialization(Set);

   type Cursor is private;
   pragma Preelaborable_Initialization(Cursor);

   Empty_Set : constant Set;

   No_Element : constant Cursor;

   function Has_Element (Position : Cursor) return Boolean;

   function Has_Element (Container : Set; Position : Cursor)
      return Boolean;

   package Set_Iterator_Interfaces is new
       Ada.Iterator_Interfaces (Cursor, Has_Element);

   function "=" (Left, Right : Set) return Boolean;

   function Equivalent_Sets (Left, Right : Set) return Boolean;

   function Tampering_With_Cursors_Prohibited
      (Container : Set) return Boolean;

   function Empty (Capacity : Count_Type := 10)
      return Set;

   function To_Set (New_Item : Element_Type) return Set;

   function Capacity (Container : Set) return Count_Type;

   procedure Reserve_Capacity (Container : in out Set;
                               Capacity  : in     Count_Type);

   function Length (Container : Set) return Count_Type;

   function Is_Empty (Container : Set) return Boolean;

   procedure Clear (Container : in out Set);

   function Element (Position : Cursor) return Element_Type;

   function Element (Container : Set;
                     Position  : Cursor) return Element_Type;

   procedure Replace_Element (Container : in out Set;
                              Position  : in     Cursor;
                              New_item  : in     Element_Type);

   procedure Query_Element
     (Position : in Cursor;
      Process  : not null access procedure (Element : in Element_Type));

   procedure Query_Element
     (Container : in Set;
      Position  : in Cursor;
      Process   : not null access procedure (Element : in Element_Type));

   type Constant_Reference_Type
         (Element : not null access constant Element_Type) is private
      with Implicit_Dereference => Element;

   function Constant_Reference (Container : aliased in Set;
                                Position  : in Cursor)
      return Constant_Reference_Type;

   procedure Assign (Target : in out Set; Source : in Set);

   function Copy (Source : Set; Capacity : Count_Type := 0)
      return Set;

   procedure Move (Target : in out Set;
                   Source : in out Set);

   procedure Insert (Container : in out Set;
                     New_Item  : in     Element_Type;
                     Position  :    out Cursor;
                     Inserted  :    out Boolean);

   procedure Insert (Container : in out Set;
                     New_Item  : in     Element_Type);

   procedure Include (Container : in out Set;
                      New_Item  : in     Element_Type);

   procedure Replace (Container : in out Set;
                      New_Item  : in     Element_Type);

   procedure Exclude (Container : in out Set;
                      Item      : in     Element_Type);

   procedure Delete (Container : in out Set;
                     Item      : in     Element_Type);

   procedure Delete (Container : in out Set;
                     Position  : in out Cursor);

   procedure Union (Target : in out Set;
                    Source : in     Set);

   function Union (Left, Right : Set) return Set;

   function "or" (Left, Right : Set) return Set renames Union;

   procedure Intersection (Target : in out Set;
                           Source : in     Set);

   function Intersection (Left, Right : Set) return Set;

   function "and" (Left, Right : Set) return Set renames Intersection;

   procedure Difference (Target : in out Set;
                         Source : in     Set);

   function Difference (Left, Right : Set) return Set;

   function "-" (Left, Right : Set) return Set renames Difference;

   procedure Symmetric_Difference (Target : in out Set;
                                   Source : in     Set);

   function Symmetric_Difference (Left, Right : Set) return Set;

   function "xor" (Left, Right : Set) return Set
     renames Symmetric_Difference;

   function Overlap (Left, Right : Set) return Boolean;

   function Is_Subset (Subset : Set;
                       Of_Set : Set) return Boolean;

   function First (Container : Set) return Cursor;

   function Next (Position : Cursor) return Cursor;

   function Next (Container : Set;
                  Position : Cursor) return Cursor;

   procedure Next (Position : in out Cursor);

   procedure Next (Container : in     Set;
                   Position  : in out Cursor);

   function Find (Container : Set;
                  Item      : Element_Type)
      return Cursor;

   function Contains (Container : Set;
                      Item      : Element_Type) return Boolean;

   function Equivalent_Elements (Left, Right : Cursor)
      return Boolean;

   function Equivalent_Elements (Left  : Cursor;
                                 Right : Element_Type)
      return Boolean;

   function Equivalent_Elements (Left  : Element_Type;
                                 Right : Cursor)
      return Boolean;

   procedure Iterate
     (Container : in Set;
      Process   : not null access procedure (Position : in Cursor))
      with Allows_Exit;

   function Iterate (Container : in Set)
      return Set_Iterator_Interfaces.Parallel_Iterator'Class;

   generic
      type Key_Type (<>) is private;
      with function Key (Element : Element_Type) return Key_Type;
      with function Hash (Key : Key_Type) return Hash_Type;
      with function Equivalent_Keys (Left, Right : Key_Type)
                                     return Boolean;
   package Generic_Keys is

      function Key (Position : Cursor) return Key_Type;

      function Key (Container : Set;
                    Position : Cursor) return Key_Type;

      function Element (Container : Set;
                        Key       : Key_Type)
        return Element_Type;

      procedure Replace (Container : in out Set;
                         Key       : in     Key_Type;
                         New_Item  : in     Element_Type);

      procedure Exclude (Container : in out Set;
                         Key       : in     Key_Type);

      procedure Delete (Container : in out Set;
                        Key       : in     Key_Type);

      function Find (Container : Set;
                     Key       : Key_Type)
         return Cursor;

      function Contains (Container : Set;
                         Key       : Key_Type)
         return Boolean;

      procedure Update_Element_Preserving_Key
        (Container : in out Set;
         Position  : in     Cursor;
         Process   : not null access procedure
                         (Element : in out Element_Type));

      type Reference_Type
            (Element : not null access Element_Type) is private
         with Implicit_Dereference => Element;

      function Reference_Preserving_Key (Container : aliased in out Set;
                                         Position  : in Cursor)
         return Reference_Type;

      function Constant_Reference (Container : aliased in Set;
                                   Key       : in Key_Type)
         return Constant_Reference_Type;

      function Reference_Preserving_Key (Container : aliased in out Set;
                                         Key       : in Key_Type)
         return Reference_Type;

   private

      type Keys_Set_Access is access all Set;
      type Saved_Key_Access is access Key_Type;

      --  While a reference object exists, tampering with the cursors of
      --  Container.all is prohibited; on finalization the key of the element
      --  of Node is checked against the saved one (the bounded form saves
      --  the element, from which the key is computed).
      type Key_Control is new Ada.Finalization.Controlled with record
         Container : Keys_Set_Access;
         Node      : Count_Type := 0;
         Saved     : Saved_Key_Access;
      end record;

      overriding procedure Adjust (Control : in out Key_Control);
      overriding procedure Finalize (Control : in out Key_Control);

      type Reference_Type
            (Element : not null access Element_Type) is record
         Control : Key_Control := raise Program_Error;
      end record;

      procedure Write
        (Stream : not null access Ada.Streams.Root_Stream_Type'Class;
         Item   : Reference_Type);
      for Reference_Type'Write use Write;

      procedure Read
        (Stream : not null access Ada.Streams.Root_Stream_Type'Class;
         Item   : out Reference_Type);
      for Reference_Type'Read use Read;

   end Generic_Keys;

private

   --  The nodes of a set live in an array (a pool) and are chained, by
   --  index, from the buckets of the hash table; Hash caches the hash value
   --  of the element.  Free nodes are chained through Next from Free; nodes
   --  High + 1 .. the end of the array have never been used.
   type Element_Access is access Element_Type;

   type Node_Type is record
      Element : Element_Access;
      Hash    : Hash_Type := 0;
      Next    : Count_Type := 0;
      Used    : Boolean := False;
   end record;

   type Node_Array is array (Count_Type range <>) of Node_Type;

   --  Bucket J holds the nodes whose hash value is J - 1 modulo the length
   --  of the bucket array.
   type Bucket_Array is array (Hash_Type range <>) of Count_Type;

   type Node_Array_Access is access Node_Array;
   type Bucket_Array_Access is access Bucket_Array;

   --  Busy counts the reasons for which tampering with cursors is
   --  prohibited (iterations); Lock counts the reasons for which tampering
   --  with elements is prohibited (Query_Element, Update_Element and
   --  references).
   type Set is new Ada.Finalization.Controlled with record
      Nodes   : Node_Array_Access;
      Buckets : Bucket_Array_Access;
      Length  : Count_Type := 0;
      Free    : Count_Type := 0;
      High    : Count_Type := 0;
      Busy    : Natural := 0;
      Lock    : Natural := 0;
   end record;

   overriding procedure Adjust (Container : in out Set);
   overriding procedure Finalize (Container : in out Set);

   procedure Write
     (Stream    : not null access Ada.Streams.Root_Stream_Type'Class;
      Container : Set);
   for Set'Write use Write;

   procedure Read
     (Stream    : not null access Ada.Streams.Root_Stream_Type'Class;
      Container : out Set);
   for Set'Read use Read;

   type Set_Access is access all Set;

   type Cursor is record
      Container : Set_Access;
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
   --  with the cursors of Container.all is prohibited.
   type Reference_Control is new Ada.Finalization.Controlled with record
      Container : Set_Access;
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

   Empty_Set : constant Set :=
     (Ada.Finalization.Controlled with others => <>);

   No_Element : constant Cursor := (Container => null, Node => 0);
end Ada.Containers.Hashed_Sets;
