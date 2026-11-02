--  Ada.Strings.Wide_Maps.ads
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
--  package Ada.Strings.Wide_Maps in the Ada Reference Manual, 202x Edition, Draft 25 (A.4.7),
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
--  the private part is neos's, and with clauses for Ada.Finalization and
--  Ada.Streams are added for it; re-indented to 3 spaces, the declarations the RM
--  text wraps rejoined.
--  (See languages/packages/ada/NOTICE.)

with Ada.Finalization;
with Ada.Streams;
package Ada.Strings.Wide_Maps
   with Preelaborate, Nonblocking is

   -- Representation for a set of Wide_Character values:
   type Wide_Character_Set is private;
   pragma Preelaborable_Initialization(Wide_Character_Set);

   Null_Set : constant Wide_Character_Set;

   type Wide_Character_Range is
     record
         Low  : Wide_Character;
         High : Wide_Character;
     end record;
   -- Represents Wide_Character range Low..High

   type Wide_Character_Ranges is array (Positive range <>)
      of Wide_Character_Range;

   function To_Set    (Ranges : in Wide_Character_Ranges)
      return Wide_Character_Set;

   function To_Set    (Span   : in Wide_Character_Range)
      return Wide_Character_Set;

   function To_Ranges (Set    : in Wide_Character_Set)
      return Wide_Character_Ranges;

   function "="   (Left, Right : in Wide_Character_Set) return Boolean;

   function "not" (Right : in Wide_Character_Set)
      return Wide_Character_Set;
   function "and" (Left, Right : in Wide_Character_Set)
      return Wide_Character_Set;
   function "or"  (Left, Right : in Wide_Character_Set)
      return Wide_Character_Set;
   function "xor" (Left, Right : in Wide_Character_Set)
      return Wide_Character_Set;
   function "-"   (Left, Right : in Wide_Character_Set)
      return Wide_Character_Set;

   function Is_In (Element : in Wide_Character;
                   Set     : in Wide_Character_Set)
      return Boolean;

   function Is_Subset (Elements : in Wide_Character_Set;
                       Set      : in Wide_Character_Set)
      return Boolean;

   function "<=" (Left  : in Wide_Character_Set;
                  Right : in Wide_Character_Set)
      return Boolean renames Is_Subset;

   -- Alternative representation for a set of Wide_Character values:
   subtype Wide_Character_Sequence is Wide_String;

   function To_Set (Sequence  : in Wide_Character_Sequence)
      return Wide_Character_Set;

   function To_Set (Singleton : in Wide_Character)
      return Wide_Character_Set;

   function To_Sequence (Set  : in Wide_Character_Set)
      return Wide_Character_Sequence;

   -- Representation for a Wide_Character to Wide_Character mapping:
   type Wide_Character_Mapping is private;
   pragma Preelaborable_Initialization(Wide_Character_Mapping);

   function Value (Map     : in Wide_Character_Mapping;
                   Element : in Wide_Character)
      return Wide_Character;

   Identity : constant Wide_Character_Mapping;

   function To_Mapping (From, To : in Wide_Character_Sequence)
      return Wide_Character_Mapping;

   function To_Domain (Map : in Wide_Character_Mapping)
      return Wide_Character_Sequence;

   function To_Range  (Map : in Wide_Character_Mapping)
      return Wide_Character_Sequence;

   type Wide_Character_Mapping_Function is
      access function (From : in Wide_Character) return Wide_Character;

private
   type Wide_Character_Ranges_Access is
     access Wide_Character_Ranges;

   type Wide_Character_Set is new Ada.Finalization.Controlled with record
      Ranges : Wide_Character_Ranges_Access := null;
      -- the ranges of the set, in increasing order, disjoint and not adjacent
      -- (null for the empty set)
   end record;

   overriding procedure Adjust (Object : in out Wide_Character_Set);
   overriding procedure Finalize (Object : in out Wide_Character_Set);

   procedure Read
     (Stream : not null access Ada.Streams.Root_Stream_Type'Class;
      Item   : out Wide_Character_Set);
   procedure Write
     (Stream : not null access Ada.Streams.Root_Stream_Type'Class;
      Item   : in Wide_Character_Set);
   for Wide_Character_Set'Read use Read;
   for Wide_Character_Set'Write use Write;
   -- (as its Wide_Character_Ranges)

   Null_Set : constant Wide_Character_Set :=
     (Ada.Finalization.Controlled with Ranges => null);

   type Wide_Character_Sequence_Access is
     access Wide_Character_Sequence;

   type Wide_Character_Mapping is new Ada.Finalization.Controlled with
   record
      Domain  : Wide_Character_Sequence_Access := null;
      Targets : Wide_Character_Sequence_Access := null;
      -- the characters that do not map to themselves, in increasing order,
      -- and the characters they map to (null for none)
   end record;

   overriding procedure Adjust (Object : in out Wide_Character_Mapping);
   overriding procedure Finalize
     (Object : in out Wide_Character_Mapping);

   procedure Read
     (Stream : not null access Ada.Streams.Root_Stream_Type'Class;
      Item   : out Wide_Character_Mapping);
   procedure Write
     (Stream : not null access Ada.Streams.Root_Stream_Type'Class;
      Item   : in Wide_Character_Mapping);
   for Wide_Character_Mapping'Read use Read;
   for Wide_Character_Mapping'Write use Write;
   -- (as its domain and range)

   Identity : constant Wide_Character_Mapping :=
     (Ada.Finalization.Controlled with Domain => null, Targets => null);
end Ada.Strings.Wide_Maps;
