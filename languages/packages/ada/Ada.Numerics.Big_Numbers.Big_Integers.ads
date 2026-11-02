--  Ada.Numerics.Big_Numbers.Big_Integers.ads
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
--  package Ada.Numerics.Big_Numbers.Big_Integers in the Ada Reference Manual, 202x Edition, Draft 25 (A.5.6),
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
--  the private part is neos's (with a with clause for Ada.Finalization, which
--  it uses); in the predicate of Big_Natural, the draft's "Big_Natural => 0"
--  (a typographical error) is corrected to "Big_Natural >= 0". The layout of
--  the declarations (line breaks and alignment) is adjusted.
--  (See languages/packages/ada/NOTICE.)

with Ada.Strings.Text_Buffers;
with Ada.Finalization;
package Ada.Numerics.Big_Numbers.Big_Integers
   with Preelaborate, Nonblocking, Global => null is

   type Big_Integer is private
     with Integer_Literal => From_String,
          Put_Image => Put_Image;

   function Is_Valid (Arg : Big_Integer) return Boolean
      with Convention => Intrinsic;

   subtype Valid_Big_Integer is Big_Integer
      with Dynamic_Predicate => Is_Valid (Valid_Big_Integer),
           Predicate_Failure => (raise Program_Error);

   function "=" (L, R : Valid_Big_Integer) return Boolean;
   function "<" (L, R : Valid_Big_Integer) return Boolean;
   function "<=" (L, R : Valid_Big_Integer) return Boolean;
   function ">" (L, R : Valid_Big_Integer) return Boolean;
   function ">=" (L, R : Valid_Big_Integer) return Boolean;

   function To_Big_Integer (Arg : Integer) return Valid_Big_Integer;

   subtype Big_Positive is Big_Integer
      with Dynamic_Predicate => (if Is_Valid (Big_Positive) then Big_Positive > 0),
           Predicate_Failure => (raise Constraint_Error);

   subtype Big_Natural is Big_Integer
      with Dynamic_Predicate => (if Is_Valid (Big_Natural) then Big_Natural >= 0),
           Predicate_Failure => (raise Constraint_Error);

   function In_Range
     (Arg, Low, High : Valid_Big_Integer) return Boolean is
     (Low <= Arg and Arg <= High);

   function To_Integer (Arg : Valid_Big_Integer) return Integer
      with Pre => In_Range (Arg,
                            Low  => To_Big_Integer (Integer'First),
                            High => To_Big_Integer (Integer'Last))
                   or else raise Constraint_Error;

   generic
      type Int is range <>;
   package Signed_Conversions is
      function To_Big_Integer (Arg : Int) return Valid_Big_Integer;
      function From_Big_Integer (Arg : Valid_Big_Integer) return Int
         with Pre => In_Range (Arg,
                               Low  => To_Big_Integer (Int'First),
                               High => To_Big_Integer (Int'Last))
                      or else raise Constraint_Error;
   end Signed_Conversions;

   generic
      type Int is mod <>;
   package Unsigned_Conversions is
      function To_Big_Integer (Arg : Int) return Valid_Big_Integer;
      function From_Big_Integer (Arg : Valid_Big_Integer) return Int
         with Pre => In_Range (Arg,
                               Low  => To_Big_Integer (Int'First),
                               High => To_Big_Integer (Int'Last))
                      or else raise Constraint_Error;
   end Unsigned_Conversions;

   function To_String (Arg : Valid_Big_Integer;
                       Width : Field := 0;
                       Base  : Number_Base := 10) return String
      with Post => To_String'Result'First = 1;

   function From_String (Arg : String) return Valid_Big_Integer;

   procedure Put_Image
     (Buffer : in out Ada.Strings.Text_Buffers.Root_Buffer_Type'Class;
      Arg    : in Valid_Big_Integer);

   function "+" (L : Valid_Big_Integer) return Valid_Big_Integer;
   function "-" (L : Valid_Big_Integer) return Valid_Big_Integer;
   function "abs" (L : Valid_Big_Integer) return Valid_Big_Integer;
   function "+" (L, R : Valid_Big_Integer) return Valid_Big_Integer;
   function "-" (L, R : Valid_Big_Integer) return Valid_Big_Integer;
   function "*" (L, R : Valid_Big_Integer) return Valid_Big_Integer;
   function "/" (L, R : Valid_Big_Integer) return Valid_Big_Integer;
   function "mod" (L, R : Valid_Big_Integer) return Valid_Big_Integer;
   function "rem" (L, R : Valid_Big_Integer) return Valid_Big_Integer;
   function "**" (L : Valid_Big_Integer; R : Natural)
      return Valid_Big_Integer;
   function Min (L, R : Valid_Big_Integer) return Valid_Big_Integer;
   function Max (L, R : Valid_Big_Integer) return Valid_Big_Integer;

   function Greatest_Common_Divisor
     (L, R : Valid_Big_Integer) return Big_Positive
     with Pre => (L /= 0 and R /= 0) or else raise Constraint_Error;

private

   --  A value is a sign and a magnitude: the magnitude's digits (base
   --  2**32) are least significant first, with no zero digits at the end;
   --  zero has no digits (M.Value is null). A default-initialized value is
   --  not valid.

   type Digit is mod 2 ** 32;

   type Digits_Array is array (Integer range <>) of Digit;

   type Digits_Access is access Digits_Array;

   --  (The controlled part is a component, so that Big_Integer is not
   --  tagged and its operations are not dispatching operations, whose
   --  profiles could not use the subtype Valid_Big_Integer.)
   type Magnitude is new Ada.Finalization.Controlled with record
      Value : Digits_Access;
   end record;

   overriding procedure Adjust (Object : in out Magnitude);
   overriding procedure Finalize (Object : in out Magnitude);

   type Big_Integer is record
      Valid    : Boolean := False;
      Negative : Boolean := False;
      M        : Magnitude;
   end record;

end Ada.Numerics.Big_Numbers.Big_Integers;
