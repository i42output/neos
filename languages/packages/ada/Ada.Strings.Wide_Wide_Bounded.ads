--  Ada.Strings.Wide_Wide_Bounded.ads
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
--  package Ada.Strings.Bounded in the Ada Reference Manual, 202x Edition, Draft 25 (A.4.4, A.4.8),
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
--  derived from the specification of Ada.Strings.Bounded by the substitutions
--  of RM A.4.8; the private part of Generic_Bounded_Length is neos's;
--  re-indented to 3 spaces, the declarations the RM text wraps rejoined.
--  (See languages/packages/ada/NOTICE.)

with Ada.Strings.Wide_Wide_Maps;
package Ada.Strings.Wide_Wide_Bounded
   with Preelaborate, Nonblocking is

   generic
      Max   : Positive;    -- Maximum length of a Bounded_String
   package Generic_Bounded_Length is

      Max_Length : constant Positive := Max;

      type Bounded_Wide_Wide_String is private;

      Null_Bounded_Wide_Wide_String : constant Bounded_Wide_Wide_String;

      subtype Length_Range is Natural range 0 .. Max_Length;

      function Length (Source : in Bounded_Wide_Wide_String) return Length_Range;

   -- Conversion, Concatenation, and Selection functions

      function To_Bounded_Wide_Wide_String (Source : in Wide_Wide_String;
                                  Drop   : in Truncation := Error)
         return Bounded_Wide_Wide_String;

      function To_Wide_Wide_String (Source : in Bounded_Wide_Wide_String) return Wide_Wide_String;

      procedure Set_Bounded_Wide_Wide_String
         (Target :    out Bounded_Wide_Wide_String;
          Source : in     Wide_Wide_String;
          Drop   : in     Truncation := Error);

      function Append (Left, Right : in Bounded_Wide_Wide_String;
                       Drop        : in Truncation  := Error)
         return Bounded_Wide_Wide_String;

      function Append (Left  : in Bounded_Wide_Wide_String;
                       Right : in Wide_Wide_String;
                       Drop  : in Truncation := Error)
         return Bounded_Wide_Wide_String;

      function Append (Left  : in Wide_Wide_String;
                       Right : in Bounded_Wide_Wide_String;
                       Drop  : in Truncation := Error)
         return Bounded_Wide_Wide_String;

      function Append (Left  : in Bounded_Wide_Wide_String;
                       Right : in Wide_Wide_Character;
                       Drop  : in Truncation := Error)
         return Bounded_Wide_Wide_String;

      function Append (Left  : in Wide_Wide_Character;
                       Right : in Bounded_Wide_Wide_String;
                       Drop  : in Truncation := Error)
         return Bounded_Wide_Wide_String;

      procedure Append (Source   : in out Bounded_Wide_Wide_String;
                        New_Item : in Bounded_Wide_Wide_String;
                        Drop     : in Truncation  := Error);

      procedure Append (Source   : in out Bounded_Wide_Wide_String;
                        New_Item : in Wide_Wide_String;
                        Drop     : in Truncation  := Error);

      procedure Append (Source   : in out Bounded_Wide_Wide_String;
                        New_Item : in Wide_Wide_Character;
                        Drop     : in Truncation  := Error);

      function "&" (Left, Right : in Bounded_Wide_Wide_String)
         return Bounded_Wide_Wide_String;

      function "&" (Left : in Bounded_Wide_Wide_String; Right : in Wide_Wide_String)
         return Bounded_Wide_Wide_String;

      function "&" (Left : in Wide_Wide_String; Right : in Bounded_Wide_Wide_String)
         return Bounded_Wide_Wide_String;

      function "&" (Left : in Bounded_Wide_Wide_String; Right : in Wide_Wide_Character)
         return Bounded_Wide_Wide_String;

      function "&" (Left : in Wide_Wide_Character; Right : in Bounded_Wide_Wide_String)
         return Bounded_Wide_Wide_String;

      function Element (Source : in Bounded_Wide_Wide_String;
                        Index  : in Positive)
         return Wide_Wide_Character;

      procedure Replace_Element (Source : in out Bounded_Wide_Wide_String;
                                 Index  : in Positive;
                                 By     : in Wide_Wide_Character);

      function Slice (Source : in Bounded_Wide_Wide_String;
                      Low    : in Positive;
                      High   : in Natural)
         return Wide_Wide_String;

      function Bounded_Slice
         (Source : in Bounded_Wide_Wide_String;
          Low    : in Positive;
          High   : in Natural)
             return Bounded_Wide_Wide_String;

      procedure Bounded_Slice
         (Source : in     Bounded_Wide_Wide_String;
          Target :    out Bounded_Wide_Wide_String;
          Low    : in     Positive;
          High   : in     Natural);

      function "="  (Left, Right : in Bounded_Wide_Wide_String) return Boolean;
      function "="  (Left : in Bounded_Wide_Wide_String; Right : in Wide_Wide_String)
        return Boolean;

      function "="  (Left : in Wide_Wide_String; Right : in Bounded_Wide_Wide_String)
        return Boolean;

      function "<"  (Left, Right : in Bounded_Wide_Wide_String) return Boolean;

      function "<"  (Left : in Bounded_Wide_Wide_String; Right : in Wide_Wide_String)
        return Boolean;

      function "<"  (Left : in Wide_Wide_String; Right : in Bounded_Wide_Wide_String)
        return Boolean;

      function "<=" (Left, Right : in Bounded_Wide_Wide_String) return Boolean;

      function "<="  (Left : in Bounded_Wide_Wide_String; Right : in Wide_Wide_String)
        return Boolean;

      function "<="  (Left : in Wide_Wide_String; Right : in Bounded_Wide_Wide_String)
        return Boolean;

      function ">"  (Left, Right : in Bounded_Wide_Wide_String) return Boolean;

      function ">"  (Left : in Bounded_Wide_Wide_String; Right : in Wide_Wide_String)
        return Boolean;

      function ">"  (Left : in Wide_Wide_String; Right : in Bounded_Wide_Wide_String)
        return Boolean;

      function ">=" (Left, Right : in Bounded_Wide_Wide_String) return Boolean;

      function ">="  (Left : in Bounded_Wide_Wide_String; Right : in Wide_Wide_String)
        return Boolean;

      function ">="  (Left : in Wide_Wide_String; Right : in Bounded_Wide_Wide_String)
        return Boolean;

   -- Search subprograms

      function Index (Source  : in Bounded_Wide_Wide_String;
                      Pattern : in Wide_Wide_String;
                      From    : in Positive;
                      Going   : in Direction := Forward;
                      Mapping : in Wide_Wide_Maps.Wide_Wide_Character_Mapping := Wide_Wide_Maps.Identity)
         return Natural;

      function Index (Source  : in Bounded_Wide_Wide_String;
                      Pattern : in Wide_Wide_String;
                      From    : in Positive;
                      Going   : in Direction := Forward;
                      Mapping : in Wide_Wide_Maps.Wide_Wide_Character_Mapping_Function)
         return Natural;

      function Index (Source   : in Bounded_Wide_Wide_String;
                      Pattern  : in Wide_Wide_String;
                      Going    : in Direction := Forward;
                      Mapping  : in Wide_Wide_Maps.Wide_Wide_Character_Mapping := Wide_Wide_Maps.Identity)
         return Natural;

      function Index (Source   : in Bounded_Wide_Wide_String;
                      Pattern  : in Wide_Wide_String;
                      Going    : in Direction := Forward;
                      Mapping  : in Wide_Wide_Maps.Wide_Wide_Character_Mapping_Function)
         return Natural;

      function Index (Source  : in Bounded_Wide_Wide_String;
                      Set     : in Wide_Wide_Maps.Wide_Wide_Character_Set;
                      From    : in Positive;
                      Test    : in Membership := Inside;
                      Going   : in Direction := Forward)
         return Natural;

      function Index (Source : in Bounded_Wide_Wide_String;
                      Set    : in Wide_Wide_Maps.Wide_Wide_Character_Set;
                      Test   : in Membership := Inside;
                      Going  : in Direction  := Forward)
         return Natural;

      function Index_Non_Blank (Source : in Bounded_Wide_Wide_String;
                                From   : in Positive;
                                Going  : in Direction := Forward)
         return Natural;

      function Index_Non_Blank (Source : in Bounded_Wide_Wide_String;
                                Going  : in Direction := Forward)
         return Natural;

      function Count (Source   : in Bounded_Wide_Wide_String;
                      Pattern  : in Wide_Wide_String;
                      Mapping  : in Wide_Wide_Maps.Wide_Wide_Character_Mapping := Wide_Wide_Maps.Identity)
         return Natural;

      function Count (Source   : in Bounded_Wide_Wide_String;
                      Pattern  : in Wide_Wide_String;
                      Mapping  : in Wide_Wide_Maps.Wide_Wide_Character_Mapping_Function)
         return Natural;

      function Count (Source   : in Bounded_Wide_Wide_String;
                      Set      : in Wide_Wide_Maps.Wide_Wide_Character_Set)
         return Natural;

      procedure Find_Token (Source : in Bounded_Wide_Wide_String;
                            Set    : in Wide_Wide_Maps.Wide_Wide_Character_Set;
                            From   : in Positive;
                            Test   : in Membership;
                            First  : out Positive;
                            Last   : out Natural);

      procedure Find_Token (Source : in Bounded_Wide_Wide_String;
                            Set    : in Wide_Wide_Maps.Wide_Wide_Character_Set;
                            Test   : in Membership;
                            First  : out Positive;
                            Last   : out Natural);

   -- String translation subprograms

      function Translate (Source  : in Bounded_Wide_Wide_String;
                          Mapping : in Wide_Wide_Maps.Wide_Wide_Character_Mapping)
         return Bounded_Wide_Wide_String;

      procedure Translate (Source  : in out Bounded_Wide_Wide_String;
                           Mapping : in Wide_Wide_Maps.Wide_Wide_Character_Mapping);

      function Translate (Source  : in Bounded_Wide_Wide_String;
                          Mapping : in Wide_Wide_Maps.Wide_Wide_Character_Mapping_Function)
         return Bounded_Wide_Wide_String;

      procedure Translate (Source  : in out Bounded_Wide_Wide_String;
                           Mapping : in Wide_Wide_Maps.Wide_Wide_Character_Mapping_Function);

   -- String transformation subprograms

      function Replace_Slice (Source   : in Bounded_Wide_Wide_String;
                              Low      : in Positive;
                              High     : in Natural;
                              By       : in Wide_Wide_String;
                              Drop     : in Truncation := Error)
         return Bounded_Wide_Wide_String;

      procedure Replace_Slice (Source   : in out Bounded_Wide_Wide_String;
                               Low      : in Positive;
                               High     : in Natural;
                               By       : in Wide_Wide_String;
                               Drop     : in Truncation := Error);

      function Insert (Source   : in Bounded_Wide_Wide_String;
                       Before   : in Positive;
                       New_Item : in Wide_Wide_String;
                       Drop     : in Truncation := Error)
         return Bounded_Wide_Wide_String;

      procedure Insert (Source   : in out Bounded_Wide_Wide_String;
                        Before   : in Positive;
                        New_Item : in Wide_Wide_String;
                        Drop     : in Truncation := Error);

      function Overwrite (Source    : in Bounded_Wide_Wide_String;
                          Position  : in Positive;
                          New_Item  : in Wide_Wide_String;
                          Drop      : in Truncation := Error)
         return Bounded_Wide_Wide_String;

      procedure Overwrite (Source    : in out Bounded_Wide_Wide_String;
                           Position  : in Positive;
                           New_Item  : in Wide_Wide_String;
                           Drop      : in Truncation := Error);

      function Delete (Source  : in Bounded_Wide_Wide_String;
                       From    : in Positive;
                       Through : in Natural)
         return Bounded_Wide_Wide_String;

      procedure Delete (Source  : in out Bounded_Wide_Wide_String;
                        From    : in Positive;
                        Through : in Natural);

   --String selector subprograms

      function Trim (Source : in Bounded_Wide_Wide_String;
                     Side   : in Trim_End)
         return Bounded_Wide_Wide_String;
      procedure Trim (Source : in out Bounded_Wide_Wide_String;
                      Side   : in Trim_End);

      function Trim (Source : in Bounded_Wide_Wide_String;
                     Left   : in Wide_Wide_Maps.Wide_Wide_Character_Set;
                     Right  : in Wide_Wide_Maps.Wide_Wide_Character_Set)
         return Bounded_Wide_Wide_String;

      procedure Trim (Source : in out Bounded_Wide_Wide_String;
                      Left   : in Wide_Wide_Maps.Wide_Wide_Character_Set;
                      Right  : in Wide_Wide_Maps.Wide_Wide_Character_Set);

      function Head (Source : in Bounded_Wide_Wide_String;
                     Count  : in Natural;
                     Pad    : in Wide_Wide_Character  := Wide_Wide_Space;
                     Drop   : in Truncation := Error)
         return Bounded_Wide_Wide_String;

      procedure Head (Source : in out Bounded_Wide_Wide_String;
                      Count  : in Natural;
                      Pad    : in Wide_Wide_Character  := Wide_Wide_Space;
                      Drop   : in Truncation := Error);

      function Tail (Source : in Bounded_Wide_Wide_String;
                     Count  : in Natural;
                     Pad    : in Wide_Wide_Character  := Wide_Wide_Space;
                     Drop   : in Truncation := Error)
         return Bounded_Wide_Wide_String;

      procedure Tail (Source : in out Bounded_Wide_Wide_String;
                      Count  : in Natural;
                      Pad    : in Wide_Wide_Character  := Wide_Wide_Space;
                      Drop   : in Truncation := Error);

   --String constructor subprograms

      function "*" (Left  : in Natural;
                    Right : in Wide_Wide_Character)
         return Bounded_Wide_Wide_String;

      function "*" (Left  : in Natural;
                    Right : in Wide_Wide_String)
         return Bounded_Wide_Wide_String;

      function "*" (Left  : in Natural;
                    Right : in Bounded_Wide_Wide_String)
         return Bounded_Wide_Wide_String;

      function Replicate (Count : in Natural;
                          Item  : in Wide_Wide_Character;
                          Drop  : in Truncation := Error)
         return Bounded_Wide_Wide_String;

      function Replicate (Count : in Natural;
                          Item  : in Wide_Wide_String;
                          Drop  : in Truncation := Error)
         return Bounded_Wide_Wide_String;

      function Replicate (Count : in Natural;
                          Item  : in Bounded_Wide_Wide_String;
                          Drop  : in Truncation := Error)
         return Bounded_Wide_Wide_String;

   private
      type Bounded_Wide_Wide_String is record
         Length : Length_Range := 0;
         Data   : Wide_Wide_String (1 .. Max_Length);  -- the string is Data (1 .. Length)
      end record;

      Null_Bounded_Wide_Wide_String : constant Bounded_Wide_Wide_String :=
        (Length => 0, Data => (others => Wide_Wide_Space));
   end Generic_Bounded_Length;

end Ada.Strings.Wide_Wide_Bounded;
