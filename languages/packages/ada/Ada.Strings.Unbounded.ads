--  Ada.Strings.Unbounded.ads
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
--  package Ada.Strings.Unbounded in the Ada Reference Manual, 202x Edition, Draft 25 (A.4.5),
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
--  Ada.Streams are added for it; re-indented to 3 spaces, the declarations the
--  RM text wraps rejoined.
--  (See languages/packages/ada/NOTICE.)

with Ada.Strings.Maps;
with Ada.Finalization;
with Ada.Streams;
package Ada.Strings.Unbounded
   with Preelaborate, Nonblocking is

   type Unbounded_String is private;
   pragma Preelaborable_Initialization(Unbounded_String);

   Null_Unbounded_String : constant Unbounded_String;

   function Length (Source : in Unbounded_String) return Natural;

   type String_Access is access all String;
   procedure Free (X : in out String_Access);

   -- Conversion, Concatenation, and Selection functions

   function To_Unbounded_String (Source : in String)
      return Unbounded_String;

   function To_Unbounded_String (Length : in Natural)
      return Unbounded_String;

   function To_String (Source : in Unbounded_String) return String;

   procedure Set_Unbounded_String
     (Target :    out Unbounded_String;
      Source : in     String);

   procedure Append (Source   : in out Unbounded_String;
                     New_Item : in Unbounded_String);

   procedure Append (Source   : in out Unbounded_String;
                     New_Item : in String);

   procedure Append (Source   : in out Unbounded_String;
                     New_Item : in Character);

   function "&" (Left, Right : in Unbounded_String)
      return Unbounded_String;

   function "&" (Left : in Unbounded_String; Right : in String)
      return Unbounded_String;

   function "&" (Left : in String; Right : in Unbounded_String)
      return Unbounded_String;

   function "&" (Left : in Unbounded_String; Right : in Character)
      return Unbounded_String;

   function "&" (Left : in Character; Right : in Unbounded_String)
      return Unbounded_String;

   function Element (Source : in Unbounded_String;
                     Index  : in Positive)
      return Character;

   procedure Replace_Element (Source : in out Unbounded_String;
                              Index  : in Positive;
                              By     : in Character);

   function Slice (Source : in Unbounded_String;
                   Low    : in Positive;
                   High   : in Natural)
      return String;

   function Unbounded_Slice
      (Source : in Unbounded_String;
       Low    : in Positive;
       High   : in Natural)
          return Unbounded_String;

   procedure Unbounded_Slice
      (Source : in     Unbounded_String;
       Target :    out Unbounded_String;
       Low    : in     Positive;
       High   : in     Natural);

   function "="  (Left, Right : in Unbounded_String) return Boolean;

   function "="  (Left : in Unbounded_String; Right : in String)
     return Boolean;

   function "="  (Left : in String; Right : in Unbounded_String)
     return Boolean;

   function "<"  (Left, Right : in Unbounded_String) return Boolean;

   function "<"  (Left : in Unbounded_String; Right : in String)
     return Boolean;

   function "<"  (Left : in String; Right : in Unbounded_String)
     return Boolean;

   function "<=" (Left, Right : in Unbounded_String) return Boolean;

   function "<="  (Left : in Unbounded_String; Right : in String)
     return Boolean;

   function "<="  (Left : in String; Right : in Unbounded_String)
     return Boolean;

   function ">"  (Left, Right : in Unbounded_String) return Boolean;

   function ">"  (Left : in Unbounded_String; Right : in String)
     return Boolean;

   function ">"  (Left : in String; Right : in Unbounded_String)
     return Boolean;

   function ">=" (Left, Right : in Unbounded_String) return Boolean;

   function ">="  (Left : in Unbounded_String; Right : in String)
     return Boolean;

   function ">="  (Left : in String; Right : in Unbounded_String)
     return Boolean;

   -- Search subprograms

   function Index (Source  : in Unbounded_String;
                   Pattern : in String;
                   From    : in Positive;
                   Going   : in Direction := Forward;
                   Mapping : in Maps.Character_Mapping := Maps.Identity)
      return Natural;

   function Index (Source  : in Unbounded_String;
                   Pattern : in String;
                   From    : in Positive;
                   Going   : in Direction := Forward;
                   Mapping : in Maps.Character_Mapping_Function)
      return Natural;

   function Index (Source   : in Unbounded_String;
                   Pattern  : in String;
                   Going    : in Direction := Forward;
                   Mapping  : in Maps.Character_Mapping := Maps.Identity)
      return Natural;

   function Index (Source   : in Unbounded_String;
                   Pattern  : in String;
                   Going    : in Direction := Forward;
                   Mapping  : in Maps.Character_Mapping_Function)
      return Natural;

   function Index (Source  : in Unbounded_String;
                   Set     : in Maps.Character_Set;
                   From    : in Positive;
                   Test    : in Membership := Inside;
                   Going    : in Direction := Forward)
      return Natural;

   function Index (Source : in Unbounded_String;
                   Set    : in Maps.Character_Set;
                   Test   : in Membership := Inside;
                   Going  : in Direction  := Forward) return Natural;

   function Index_Non_Blank (Source : in Unbounded_String;
                             From   : in Positive;
                             Going  : in Direction := Forward)
      return Natural;

   function Index_Non_Blank (Source : in Unbounded_String;
                             Going  : in Direction := Forward)
      return Natural;

   function Count (Source   : in Unbounded_String;
                   Pattern  : in String;
                   Mapping  : in Maps.Character_Mapping := Maps.Identity)
      return Natural;

   function Count (Source   : in Unbounded_String;
                   Pattern  : in String;
                   Mapping  : in Maps.Character_Mapping_Function)
      return Natural;

   function Count (Source   : in Unbounded_String;
                   Set      : in Maps.Character_Set)
      return Natural;

   procedure Find_Token (Source : in Unbounded_String;
                         Set    : in Maps.Character_Set;
                         From   : in Positive;
                         Test   : in Membership;
                         First  : out Positive;
                         Last   : out Natural);

   procedure Find_Token (Source : in Unbounded_String;
                         Set    : in Maps.Character_Set;
                         Test   : in Membership;
                         First  : out Positive;
                         Last   : out Natural);

   -- String translation subprograms

   function Translate (Source  : in Unbounded_String;
                       Mapping : in Maps.Character_Mapping)
      return Unbounded_String;

   procedure Translate (Source  : in out Unbounded_String;
                        Mapping : in Maps.Character_Mapping);

   function Translate (Source  : in Unbounded_String;
                       Mapping : in Maps.Character_Mapping_Function)
      return Unbounded_String;

   procedure Translate (Source  : in out Unbounded_String;
                        Mapping : in Maps.Character_Mapping_Function);

   -- String transformation subprograms

   function Replace_Slice (Source   : in Unbounded_String;
                           Low      : in Positive;
                           High     : in Natural;
                           By       : in String)
      return Unbounded_String;

   procedure Replace_Slice (Source   : in out Unbounded_String;
                            Low      : in Positive;
                            High     : in Natural;
                            By       : in String);

   function Insert (Source   : in Unbounded_String;
                    Before   : in Positive;
                    New_Item : in String)
      return Unbounded_String;

   procedure Insert (Source   : in out Unbounded_String;
                     Before   : in Positive;
                     New_Item : in String);

   function Overwrite (Source    : in Unbounded_String;
                       Position  : in Positive;
                       New_Item  : in String)
      return Unbounded_String;

   procedure Overwrite (Source    : in out Unbounded_String;
                        Position  : in Positive;
                        New_Item  : in String);

   function Delete (Source  : in Unbounded_String;
                    From    : in Positive;
                    Through : in Natural)
      return Unbounded_String;

   procedure Delete (Source  : in out Unbounded_String;
                     From    : in Positive;
                     Through : in Natural);

   function Trim (Source : in Unbounded_String;
                  Side   : in Trim_End)
      return Unbounded_String;

   procedure Trim (Source : in out Unbounded_String;
                   Side   : in Trim_End);

   function Trim (Source : in Unbounded_String;
                  Left   : in Maps.Character_Set;
                  Right  : in Maps.Character_Set)
      return Unbounded_String;

   procedure Trim (Source : in out Unbounded_String;
                   Left   : in Maps.Character_Set;
                   Right  : in Maps.Character_Set);

   function Head (Source : in Unbounded_String;
                  Count  : in Natural;
                  Pad    : in Character := Space)
      return Unbounded_String;

   procedure Head (Source : in out Unbounded_String;
                   Count  : in Natural;
                   Pad    : in Character := Space);

   function Tail (Source : in Unbounded_String;
                  Count  : in Natural;
                  Pad    : in Character := Space)
      return Unbounded_String;

   procedure Tail (Source : in out Unbounded_String;
                   Count  : in Natural;
                   Pad    : in Character := Space);

   function "*" (Left  : in Natural;
                 Right : in Character)
      return Unbounded_String;

   function "*" (Left  : in Natural;
                 Right : in String)
      return Unbounded_String;

   function "*" (Left  : in Natural;
                 Right : in Unbounded_String)
      return Unbounded_String;

private
   type Unbounded_String is new Ada.Finalization.Controlled with record
      Reference : String_Access := null;  -- the buffer (lower bound 1)
      Last      : Natural := 0;           -- the string is Reference (1 .. Last)
   end record;

   overriding procedure Adjust (Object : in out Unbounded_String);
   overriding procedure Finalize (Object : in out Unbounded_String);

   procedure Read
     (Stream : not null access Ada.Streams.Root_Stream_Type'Class;
      Item   : out Unbounded_String);
   procedure Write
     (Stream : not null access Ada.Streams.Root_Stream_Type'Class;
      Item   : in Unbounded_String);
   for Unbounded_String'Read use Read;
   for Unbounded_String'Write use Write;
   -- (as the String that the Unbounded_String represents)

   Null_Unbounded_String : constant Unbounded_String :=
     (Ada.Finalization.Controlled with Reference => null, Last => 0);

end Ada.Strings.Unbounded;
