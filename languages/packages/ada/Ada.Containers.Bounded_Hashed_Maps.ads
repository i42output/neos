--  Ada.Containers.Bounded_Hashed_Maps.ads
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
--  package Ada.Containers.Bounded_Hashed_Maps in the Ada Reference Manual, 202x Edition, Draft 25 (A.18.21, A.18.5, A.18.4),
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
--  this is the specification of Containers.Hashed_Maps with the changes that
--  A.18.21 describes; the pragma Preelaborable_Initialization of type
--  Map is replaced by the aspect Preelaborable_Initialization =>
--  Key_Type'Preelaborable_Initialization and
--  Element_Type'Preelaborable_Initialization (the Ada 2022 form for a type
--  whose initialization depends on a formal type);
--  the implementation-defined default of the Capacity parameter of Empty is
--  10;
--  the Pre, Post, Default_Initial_Condition, Stable_Properties, Global,
--  Nonblocking and Use_Formal aspects are omitted (the body makes the checks
--  of the preconditions, raising the same exceptions), as is the aspect
--  Iterator_View of type Map (not supported by GNAT 13), and the nested
--  package Stable is omitted; private with clauses for Ada.Finalization
--  and Ada.Streams are added; the private part is neos's.
--  (See languages/packages/ada/NOTICE.)

with Ada.Iterator_Interfaces;
private with Ada.Finalization;
private with Ada.Streams;
generic
   type Key_Type is private;
   type Element_Type is private;
   with function Hash (Key : Key_Type) return Hash_Type;
   with function Equivalent_Keys (Left, Right : Key_Type)
      return Boolean;
   with function "=" (Left, Right : Element_Type)
      return Boolean is <>;
package Ada.Containers.Bounded_Hashed_Maps
   with Pure, Remote_Types is

   type Map (Capacity : Count_Type;
             Modulus  : Hash_Type) is tagged private
      with Constant_Indexing => Constant_Reference,
           Variable_Indexing => Reference,
           Default_Iterator  => Iterate,
           Iterator_Element  => Element_Type,
           Aggregate         => (Empty     => Empty,
                                 Add_Named => Insert),
           Preelaborable_Initialization
                             => Key_Type'Preelaborable_Initialization
                                and Element_Type'Preelaborable_Initialization;

   type Cursor is private;
   pragma Preelaborable_Initialization(Cursor);

   Empty_Map : constant Map;

   No_Element : constant Cursor;

   function Has_Element (Position : Cursor) return Boolean;

   function Has_Element (Container : Map; Position : Cursor)
      return Boolean;

   package Map_Iterator_Interfaces is new
       Ada.Iterator_Interfaces (Cursor, Has_Element);

   function "=" (Left, Right : Map) return Boolean;

   function Tampering_With_Cursors_Prohibited
      (Container : Map) return Boolean;

   function Tampering_With_Elements_Prohibited
      (Container : Map) return Boolean;

   function Empty (Capacity : Count_Type := 10)
      return Map;

   function Capacity (Container : Map) return Count_Type;

   procedure Reserve_Capacity (Container : in out Map;
                               Capacity  : in     Count_Type);

   function Default_Modulus (Capacity : Count_Type) return Hash_Type;

   function Length (Container : Map) return Count_Type;

   function Is_Empty (Container : Map) return Boolean;

   procedure Clear (Container : in out Map);

   function Key (Position : Cursor) return Key_Type;

   function Key (Container : Map;
                 Position : Cursor) return Key_Type;

   function Element (Position : Cursor) return Element_Type;

   function Element (Container : Map;
                     Position  : Cursor) return Element_Type;

   procedure Replace_Element (Container : in out Map;
                              Position  : in     Cursor;
                              New_item  : in     Element_Type);

   procedure Query_Element
     (Position : in Cursor;
      Process  : not null access procedure (Key     : in Key_Type;
                                            Element : in Element_Type));

   procedure Query_Element
     (Container : in Map;
      Position  : in Cursor;
      Process   : not null access procedure (Key     : in Key_Type;
                                             Element : in Element_Type));

   procedure Update_Element
     (Container : in out Map;
      Position  : in     Cursor;
      Process   : not null access procedure
                      (Key     : in     Key_Type;
                       Element : in out Element_Type));

   type Constant_Reference_Type
         (Element : not null access constant Element_Type) is private
      with Implicit_Dereference => Element;

   type Reference_Type (Element : not null access Element_Type) is private
      with Implicit_Dereference => Element;

   function Constant_Reference (Container : aliased in Map;
                                Position  : in Cursor)
      return Constant_Reference_Type;

   function Reference (Container : aliased in out Map;
                       Position  : in Cursor)
      return Reference_Type;

   function Constant_Reference (Container : aliased in Map;
                                Key       : in Key_Type)
      return Constant_Reference_Type;

   function Reference (Container : aliased in out Map;
                       Key       : in Key_Type)
      return Reference_Type;

   procedure Assign (Target : in out Map; Source : in Map);

   function Copy (Source   : Map;
                  Capacity : Count_Type := 0;
                  Modulus  : Hash_Type := 0) return Map;

   procedure Move (Target : in out Map;
                   Source : in out Map);

   procedure Insert (Container : in out Map;
                     Key       : in     Key_Type;
                     New_Item  : in     Element_Type;
                     Position  :    out Cursor;
                     Inserted  :    out Boolean);

   procedure Insert (Container : in out Map;
                     Key       : in     Key_Type;
                     Position  :    out Cursor;
                     Inserted  :    out Boolean);

   procedure Insert (Container : in out Map;
                     Key       : in     Key_Type;
                     New_Item  : in     Element_Type);

   procedure Include (Container : in out Map;
                      Key       : in     Key_Type;
                      New_Item  : in     Element_Type);

   procedure Replace (Container : in out Map;
                      Key       : in     Key_Type;
                      New_Item  : in     Element_Type);

   procedure Exclude (Container : in out Map;
                      Key       : in     Key_Type);

   procedure Delete (Container : in out Map;
                     Key       : in     Key_Type);

   procedure Delete (Container : in out Map;
                     Position  : in out Cursor);

   function First (Container : Map) return Cursor;

   function Next (Position : Cursor) return Cursor;

   function Next (Container : Map;
                  Position : Cursor) return Cursor;

   procedure Next (Position : in out Cursor);

   procedure Next (Container : in     Map;
                   Position  : in out Cursor);

   function Find (Container : Map;
                  Key       : Key_Type)
      return Cursor;

   function Element (Container : Map;
                     Key       : Key_Type)
      return Element_Type;

   function Contains (Container : Map;
                      Key       : Key_Type) return Boolean;

   function Equivalent_Keys (Left, Right : Cursor)
      return Boolean;

   function Equivalent_Keys (Left  : Cursor;
                             Right : Key_Type)
      return Boolean;

   function Equivalent_Keys (Left  : Key_Type;
                             Right : Cursor)
      return Boolean;

   procedure Iterate
     (Container : in Map;
      Process   : not null access procedure (Position : in Cursor))
      with Allows_Exit;

   function Iterate (Container : in Map)
      return Map_Iterator_Interfaces.Parallel_Iterator'Class;

private

   --  The nodes of a map live in an array (a pool) and are chained, by
   --  index, from the buckets of the hash table; Hash caches the hash value
   --  of the key.  Free nodes are chained through Next from Free; nodes
   --  High + 1 .. the end of the array have never been used.
   type Node_Type is record
      Key     : Key_Type;
      Element : aliased Element_Type;
      Hash    : Hash_Type := 0;
      Next    : Count_Type := 0;
      Used    : Boolean := False;
   end record;

   type Node_Array is array (Count_Type range <>) of Node_Type;

   --  Bucket J holds the nodes whose hash value is J - 1 modulo the length
   --  of the bucket array.
   type Bucket_Array is array (Hash_Type range <>) of Count_Type;

   --  Busy counts the reasons for which tampering with cursors is
   --  prohibited (iterations); Lock counts the reasons for which tampering
   --  with elements is prohibited (Query_Element, Update_Element and
   --  references).
   type Map (Capacity : Count_Type;
             Modulus  : Hash_Type) is tagged record
      Nodes   : Node_Array (1 .. Capacity);
      Buckets : Bucket_Array (1 .. Modulus) := (others => 0);
      Length  : Count_Type := 0;
      Free    : Count_Type := 0;
      High    : Count_Type := 0;
      Busy    : Natural := 0;
      Lock    : Natural := 0;
   end record;

   procedure Write
     (Stream    : not null access Ada.Streams.Root_Stream_Type'Class;
      Container : Map);
   for Map'Write use Write;

   procedure Read
     (Stream    : not null access Ada.Streams.Root_Stream_Type'Class;
      Container : out Map);
   for Map'Read use Read;

   type Map_Access is access all Map;
   for Map_Access'Storage_Size use 0;

   type Cursor is record
      Container : Map_Access;
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
      Container : Map_Access;
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

   Empty_Map : constant Map := (Capacity => 0, Modulus => 0, others => <>);

   No_Element : constant Cursor := (Container => null, Node => 0);
end Ada.Containers.Bounded_Hashed_Maps;
