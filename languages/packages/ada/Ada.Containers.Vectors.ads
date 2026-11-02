--  Ada.Containers.Vectors.ads
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
--  package Ada.Containers.Vectors in the Ada Reference Manual, 202x Edition, Draft 25 (A.18.2),
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
--  Iterator_View of type Vector (not supported by GNAT 13), and the nested
--  package Stable is omitted; the implementation-defined default of the
--  Capacity parameter of Empty is 10; private with clauses for
--  Ada.Finalization and Ada.Streams are added; the private part is neos's.
--  (See languages/packages/ada/NOTICE.)

with Ada.Iterator_Interfaces;
private with Ada.Finalization;
private with Ada.Streams;
generic
   type Index_Type is range <>;
   type Element_Type is private;
   with function "=" (Left, Right : Element_Type)
      return Boolean is <>;
package Ada.Containers.Vectors
   with Preelaborate, Remote_Types is

   subtype Extended_Index is
      Index_Type'Base range
         Index_Type'First-1 ..
         Index_Type'Min (Index_Type'Base'Last - 1, Index_Type'Last) + 1;
   No_Index : constant Extended_Index := Extended_Index'First;

   type Vector is tagged private
      with Constant_Indexing => Constant_Reference,
           Variable_Indexing => Reference,
           Default_Iterator  => Iterate,
           Iterator_Element  => Element_Type,
           Aggregate         => (Empty          => Empty,
                                 Add_Unnamed    => Append_One,
                                 New_Indexed    => New_Vector,
                                 Assign_Indexed => Replace_Element);
   pragma Preelaborable_Initialization(Vector);

   type Cursor is private;
   pragma Preelaborable_Initialization(Cursor);

   Empty_Vector : constant Vector;

   No_Element : constant Cursor;

   function Has_Element (Position : Cursor) return Boolean;

   function Has_Element (Container : Vector; Position : Cursor)
      return Boolean;

   package Vector_Iterator_Interfaces is new
       Ada.Iterator_Interfaces (Cursor, Has_Element);

   function "=" (Left, Right : Vector) return Boolean;

   function Tampering_With_Cursors_Prohibited
      (Container : Vector) return Boolean;

   function Tampering_With_Elements_Prohibited
      (Container : Vector) return Boolean;

   function Maximum_Length return Count_Type;

   function Empty (Capacity : Count_Type := 10)
      return Vector;

   function To_Vector (Length : Count_Type) return Vector;

   function To_Vector
     (New_Item : Element_Type;
      Length   : Count_Type) return Vector;

   function New_Vector (First, Last : Index_Type) return Vector is
        (To_Vector (Count_Type (Last - First + 1)))
     with Pre => First = Index_Type'First;

   function "&" (Left, Right : Vector) return Vector;

   function "&" (Left  : Vector;
                 Right : Element_Type) return Vector;

   function "&" (Left  : Element_Type;
                 Right : Vector) return Vector;

   function "&" (Left, Right  : Element_Type) return Vector;

   function Capacity (Container : Vector) return Count_Type;

   procedure Reserve_Capacity (Container : in out Vector;
                               Capacity  : in     Count_Type);

   function Length (Container : Vector) return Count_Type;

   procedure Set_Length (Container : in out Vector;
                         Length    : in     Count_Type);

   function Is_Empty (Container : Vector) return Boolean;

   procedure Clear (Container : in out Vector);

   function To_Cursor (Container : Vector;
                       Index     : Extended_Index) return Cursor;

   function To_Index (Position  : Cursor) return Extended_Index;

   function To_Index (Container : Vector;
                      Position  : Cursor) return Extended_Index;

   function Element (Container : Vector;
                     Index     : Index_Type)
      return Element_Type;

   function Element (Position : Cursor) return Element_Type;

   function Element (Container : Vector;
                     Position  : Cursor) return Element_Type;

   procedure Replace_Element (Container : in out Vector;
                              Index     : in     Index_Type;
                              New_Item  : in     Element_Type);

   procedure Replace_Element (Container : in out Vector;
                              Position  : in     Cursor;
                              New_item  : in     Element_Type);

   procedure Query_Element
     (Container : in Vector;
      Index     : in Index_Type;
      Process   : not null access procedure (Element : in Element_Type));

   procedure Query_Element
     (Position : in Cursor;
      Process  : not null access procedure (Element : in Element_Type));

   procedure Query_Element
     (Container : in Vector;
      Position  : in Cursor;
      Process   : not null access procedure (Element : in Element_Type));

   procedure Update_Element
     (Container : in out Vector;
      Index     : in     Index_Type;
      Process   : not null access procedure
                      (Element : in out Element_Type));

   procedure Update_Element
     (Container : in out Vector;
      Position  : in     Cursor;
      Process   : not null access procedure
                      (Element : in out Element_Type));

   type Constant_Reference_Type
         (Element : not null access constant Element_Type) is private
      with Implicit_Dereference => Element;

   type Reference_Type (Element : not null access Element_Type) is private
      with Implicit_Dereference => Element;

   function Constant_Reference (Container : aliased in Vector;
                                Index     : in Index_Type)
      return Constant_Reference_Type;

   function Reference (Container : aliased in out Vector;
                       Index     : in Index_Type)
      return Reference_Type;

   function Constant_Reference (Container : aliased in Vector;
                                Position  : in Cursor)
      return Constant_Reference_Type;

   function Reference (Container : aliased in out Vector;
                       Position  : in Cursor)
      return Reference_Type;

   procedure Assign (Target : in out Vector; Source : in Vector);

   function Copy (Source : Vector; Capacity : Count_Type := 0)
      return Vector;

   procedure Move (Target : in out Vector;
                   Source : in out Vector);

   procedure Insert (Container : in out Vector;
                     Before    : in     Extended_Index;
                     New_Item  : in     Vector);

   procedure Insert (Container : in out Vector;
                     Before    : in     Cursor;
                     New_Item  : in     Vector);

   procedure Insert (Container : in out Vector;
                     Before    : in     Cursor;
                     New_Item  : in     Vector;
                     Position  :    out Cursor);

   procedure Insert (Container : in out Vector;
                     Before    : in     Extended_Index;
                     New_Item  : in     Element_Type;
                     Count     : in     Count_Type := 1);

   procedure Insert (Container : in out Vector;
                     Before    : in     Cursor;
                     New_Item  : in     Element_Type;
                     Count     : in     Count_Type := 1);

   procedure Insert (Container : in out Vector;
                     Before    : in     Cursor;
                     New_Item  : in     Element_Type;
                     Position  :    out Cursor;
                     Count     : in     Count_Type := 1);

   procedure Insert (Container : in out Vector;
                     Before    : in     Extended_Index;
                     Count     : in     Count_Type := 1);

   procedure Insert (Container : in out Vector;
                     Before    : in     Cursor;
                     Position  :    out Cursor;
                     Count     : in     Count_Type := 1);

   procedure Prepend (Container : in out Vector;
                      New_Item  : in     Vector);

   procedure Prepend (Container : in out Vector;
                      New_Item  : in     Element_Type;
                      Count     : in     Count_Type := 1);

   procedure Append (Container : in out Vector;
                     New_Item  : in     Vector);

   --  (Ada 2022 names of the operations that insert a vector)
   procedure Insert_Vector (Container : in out Vector;
                            Before    : in     Extended_Index;
                            New_Item  : in     Vector);

   procedure Insert_Vector (Container : in out Vector;
                            Before    : in     Cursor;
                            New_Item  : in     Vector);

   procedure Insert_Vector (Container : in out Vector;
                            Before    : in     Cursor;
                            New_Item  : in     Vector;
                            Position  :    out Cursor);

   procedure Prepend_Vector (Container : in out Vector;
                             New_Item  : in     Vector);

   procedure Append_Vector (Container : in out Vector;
                            New_Item  : in     Vector);

   procedure Append (Container : in out Vector;
                     New_Item  : in     Element_Type;
                     Count     : in     Count_Type := 1);

   procedure Append_One (Container : in out Vector;
                         New_Item  : in     Element_Type);

   procedure Insert_Space (Container : in out Vector;
                           Before    : in     Extended_Index;
                           Count     : in     Count_Type := 1);

   procedure Insert_Space (Container : in out Vector;
                           Before    : in     Cursor;
                           Position  :    out Cursor;
                           Count     : in     Count_Type := 1);

   procedure Delete (Container : in out Vector;
                     Index     : in     Extended_Index;
                     Count     : in     Count_Type := 1);

   procedure Delete (Container : in out Vector;
                     Position  : in out Cursor;
                     Count     : in     Count_Type := 1);

   procedure Delete_First (Container : in out Vector;
                           Count     : in     Count_Type := 1);

   procedure Delete_Last (Container : in out Vector;
                          Count     : in     Count_Type := 1);

   procedure Reverse_Elements (Container : in out Vector);

   procedure Swap (Container : in out Vector;
                   I, J      : in     Index_Type);

   procedure Swap (Container : in out Vector;
                   I, J      : in     Cursor);

   function First_Index (Container : Vector) return Index_Type;

   function First (Container : Vector) return Cursor;

   function First_Element (Container : Vector)
      return Element_Type;

   function Last_Index (Container : Vector) return Extended_Index;

   function Last (Container : Vector) return Cursor;

   function Last_Element (Container : Vector)
      return Element_Type;

   function Next (Position : Cursor) return Cursor;

   function Next (Container : Vector; Position : Cursor) return Cursor;

   procedure Next (Position : in out Cursor);

   procedure Next (Container : in     Vector;
                   Position  : in out Cursor);

   function Previous (Position : Cursor) return Cursor;

   function Previous (Container : Vector;
                      Position  : Cursor) return Cursor;

   procedure Previous (Position : in out Cursor);

   procedure Previous (Container : in     Vector;
                       Position  : in out Cursor);

   function Find_Index (Container : Vector;
                        Item      : Element_Type;
                        Index     : Index_Type := Index_Type'First)
      return Extended_Index;

   function Find (Container : Vector;
                  Item      : Element_Type;
                  Position  : Cursor := No_Element)
      return Cursor;

   function Reverse_Find_Index (Container : Vector;
                                Item      : Element_Type;
                                Index     : Index_Type := Index_Type'Last)
      return Extended_Index;

   function Reverse_Find (Container : Vector;
                          Item      : Element_Type;
                          Position  : Cursor := No_Element)
      return Cursor;

   function Contains (Container : Vector;
                      Item      : Element_Type) return Boolean;

   procedure Iterate
     (Container : in Vector;
      Process   : not null access procedure (Position : in Cursor))
      with Allows_Exit;

   procedure Reverse_Iterate
     (Container : in Vector;
      Process   : not null access procedure (Position : in Cursor))
      with Allows_Exit;

   function Iterate (Container : in Vector)
      return Vector_Iterator_Interfaces.Parallel_Reversible_Iterator'Class;

   function Iterate (Container : in Vector; Start : in Cursor)
      return Vector_Iterator_Interfaces.Reversible_Iterator'Class;

   generic
      with function "<" (Left, Right : Element_Type)
         return Boolean is <>;
   package Generic_Sorting is

      function Is_Sorted (Container : Vector) return Boolean;

      procedure Sort (Container : in out Vector);

      procedure Merge (Target  : in out Vector;
                       Source  : in out Vector);

   end Generic_Sorting;

private

   type Element_Array is array (Count_Type range <>) of aliased Element_Type;

   type Element_Array_Access is access Element_Array;

   --  Elements (1 .. Last) hold the elements; the capacity is the length of
   --  the array (0 when Elements is null).  Busy counts the reasons for
   --  which tampering with cursors is prohibited (iterations); Lock counts
   --  the reasons for which tampering with elements is prohibited
   --  (Query_Element, Update_Element and references).
   type Vector is new Ada.Finalization.Controlled with record
      Elements : Element_Array_Access;
      Last     : Count_Type := 0;
      Busy     : Natural := 0;
      Lock     : Natural := 0;
   end record;

   overriding procedure Adjust (Container : in out Vector);
   overriding procedure Finalize (Container : in out Vector);

   procedure Write
     (Stream    : not null access Ada.Streams.Root_Stream_Type'Class;
      Container : Vector);
   for Vector'Write use Write;

   procedure Read
     (Stream    : not null access Ada.Streams.Root_Stream_Type'Class;
      Container : out Vector);
   for Vector'Read use Read;

   type Vector_Access is access all Vector;

   --  A cursor designates the element at position Index (1 = first) of
   --  Container.all.
   type Cursor is record
      Container : Vector_Access;
      Index     : Count_Type := 0;
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
      Container : Vector_Access;
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

   type Reference_Type
         (Element : not null access Element_Type) is record
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

   Empty_Vector : constant Vector :=
     (Ada.Finalization.Controlled with others => <>);

   No_Element : constant Cursor := (Container => null, Index => 0);
end Ada.Containers.Vectors;
