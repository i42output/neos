--  Standard.ads
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
--  package Standard in the Ada Reference Manual, 202x Edition, Draft 25 (A.1, J.5, J.6),
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
--  the predefined integer and floating point types' ranges and digits are
--  neos's (Short_Short_Integer, Short_Integer, Long_Integer, Long_Long_Integer,
--  Short_Float, Long_Float and Long_Long_Float are neos's choices of the
--  additional predefined types that the Reference Manual permits); Duration's
--  delta, range and small are neos's; types Character, Wide_Character and
--  Wide_Wide_Character, the universal and root numeric types and the operators
--  predefined for them are built into the compiler (they are not declared here);
--  package ASCII (J.5) and Numeric_Error (J.6) are declared here; the predefined
--  operators are implicitly declared, so their declarations in comments are
--  omitted.
--  (See languages/packages/ada/NOTICE.)

package Standard is
   pragma Pure (Standard);

   type Boolean is (False, True);

   --  The integer type root_integer and the corresponding universal type
   --  universal_integer are predefined (built in).

   type Integer is range -(2 ** 31) .. +(2 ** 31 - 1);

   subtype Natural  is Integer range 0 .. Integer'Last;
   subtype Positive is Integer range 1 .. Integer'Last;

   type Short_Short_Integer is range -(2 ** 7) .. +(2 ** 7 - 1);
   type Short_Integer is range -(2 ** 15) .. +(2 ** 15 - 1);
   type Long_Integer is range -(2 ** 63) .. +(2 ** 63 - 1);
   type Long_Long_Integer is range -(2 ** 63) .. +(2 ** 63 - 1);

   --  The floating point type root_real and the corresponding universal type
   --  universal_real are predefined (built in).

   type Short_Float is digits 6
     range -16#0.FFFF_FF#E+32 .. 16#0.FFFF_FF#E+32;
   type Float is digits 6
     range -16#0.FFFF_FF#E+32 .. 16#0.FFFF_FF#E+32;
   type Long_Float is digits 15
     range -16#0.FFFF_FFFF_FFFF_F8#E+256 .. 16#0.FFFF_FFFF_FFFF_F8#E+256;
   type Long_Long_Float is digits 15
     range -16#0.FFFF_FFFF_FFFF_F8#E+256 .. 16#0.FFFF_FFFF_FFFF_F8#E+256;

   --  The types universal_fixed and universal_access are predefined (built
   --  in), as are the multiplying operators of universal_fixed and the
   --  equality operators of universal_access.

   --  The types Character (ISO 8859-1), Wide_Character (the BMP of ISO/IEC
   --  10646) and Wide_Wide_Character (ISO/IEC 10646) are predefined (built
   --  in): their character literals are the characters of their positions.

   package ASCII is

      --  Control characters:

      NUL   : constant Character := Character'Val (16#00#);
      SOH   : constant Character := Character'Val (16#01#);
      STX   : constant Character := Character'Val (16#02#);
      ETX   : constant Character := Character'Val (16#03#);
      EOT   : constant Character := Character'Val (16#04#);
      ENQ   : constant Character := Character'Val (16#05#);
      ACK   : constant Character := Character'Val (16#06#);
      BEL   : constant Character := Character'Val (16#07#);
      BS    : constant Character := Character'Val (16#08#);
      HT    : constant Character := Character'Val (16#09#);
      LF    : constant Character := Character'Val (16#0A#);
      VT    : constant Character := Character'Val (16#0B#);
      FF    : constant Character := Character'Val (16#0C#);
      CR    : constant Character := Character'Val (16#0D#);
      SO    : constant Character := Character'Val (16#0E#);
      SI    : constant Character := Character'Val (16#0F#);
      DLE   : constant Character := Character'Val (16#10#);
      DC1   : constant Character := Character'Val (16#11#);
      DC2   : constant Character := Character'Val (16#12#);
      DC3   : constant Character := Character'Val (16#13#);
      DC4   : constant Character := Character'Val (16#14#);
      NAK   : constant Character := Character'Val (16#15#);
      SYN   : constant Character := Character'Val (16#16#);
      ETB   : constant Character := Character'Val (16#17#);
      CAN   : constant Character := Character'Val (16#18#);
      EM    : constant Character := Character'Val (16#19#);
      SUB   : constant Character := Character'Val (16#1A#);
      ESC   : constant Character := Character'Val (16#1B#);
      FS    : constant Character := Character'Val (16#1C#);
      GS    : constant Character := Character'Val (16#1D#);
      RS    : constant Character := Character'Val (16#1E#);
      US    : constant Character := Character'Val (16#1F#);
      DEL   : constant Character := Character'Val (16#7F#);

      --  Other characters:

      Exclam   : constant Character := '!';
      Quotation : constant Character := '"';
      Sharp    : constant Character := '#';
      Dollar   : constant Character := '$';
      Percent  : constant Character := '%';
      Ampersand : constant Character := '&';
      Colon    : constant Character := ':';
      Semicolon : constant Character := ';';
      Query    : constant Character := '?';
      At_Sign  : constant Character := '@';
      L_Bracket : constant Character := '[';
      Back_Slash : constant Character := '\';
      R_Bracket : constant Character := ']';
      Circumflex : constant Character := '^';
      Underline : constant Character := '_';
      Grave    : constant Character := '`';
      L_Brace  : constant Character := '{';
      Bar      : constant Character := '|';
      R_Brace  : constant Character := '}';
      Tilde    : constant Character := '~';

      --  Lower case letters:

      LC_A : constant Character := 'a';
      LC_B : constant Character := 'b';
      LC_C : constant Character := 'c';
      LC_D : constant Character := 'd';
      LC_E : constant Character := 'e';
      LC_F : constant Character := 'f';
      LC_G : constant Character := 'g';
      LC_H : constant Character := 'h';
      LC_I : constant Character := 'i';
      LC_J : constant Character := 'j';
      LC_K : constant Character := 'k';
      LC_L : constant Character := 'l';
      LC_M : constant Character := 'm';
      LC_N : constant Character := 'n';
      LC_O : constant Character := 'o';
      LC_P : constant Character := 'p';
      LC_Q : constant Character := 'q';
      LC_R : constant Character := 'r';
      LC_S : constant Character := 's';
      LC_T : constant Character := 't';
      LC_U : constant Character := 'u';
      LC_V : constant Character := 'v';
      LC_W : constant Character := 'w';
      LC_X : constant Character := 'x';
      LC_Y : constant Character := 'y';
      LC_Z : constant Character := 'z';

   end ASCII;

   --  Predefined string types:

   type String is array (Positive range <>) of Character
      with Pack;

   type Wide_String is array (Positive range <>) of Wide_Character
      with Pack;

   type Wide_Wide_String is array (Positive range <>)
      of Wide_Wide_Character
         with Pack;

   type Duration is delta 0.000000001
     range -((2 ** 63) * 0.000000001) .. +((2 ** 63 - 1) * 0.000000001);
   for Duration'Small use 0.000000001;

   --  The predefined exceptions:

   Constraint_Error : exception;
   Program_Error    : exception;
   Storage_Error    : exception;
   Tasking_Error    : exception;

   Numeric_Error : exception renames Constraint_Error;

end Standard;
