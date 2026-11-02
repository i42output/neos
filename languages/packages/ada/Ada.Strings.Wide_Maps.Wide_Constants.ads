--  Ada.Strings.Wide_Maps.Wide_Constants.ads
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
--  package Ada.Strings.Maps.Constants in the Ada Reference Manual, 202x Edition, Draft 25 (A.4.6, A.4.7),
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
--  derived from the specification of Ada.Strings.Maps.Constants by the
--  substitutions of RM A.4.7, with the additional declarations and pragma
--  Preelaborate (for pragma Pure) that RM A.4.7 gives; the private part is
--  neos's; re-indented to 3 spaces.
--  (See languages/packages/ada/NOTICE.)

package Ada.Strings.Wide_Maps.Wide_Constants is
   pragma Preelaborate(Wide_Constants);

   Control_Set           : constant Wide_Character_Set;
   Graphic_Set           : constant Wide_Character_Set;
   Letter_Set            : constant Wide_Character_Set;
   Lower_Set             : constant Wide_Character_Set;
   Upper_Set             : constant Wide_Character_Set;
   Basic_Set             : constant Wide_Character_Set;
   Decimal_Digit_Set     : constant Wide_Character_Set;
   Hexadecimal_Digit_Set : constant Wide_Character_Set;
   Alphanumeric_Set      : constant Wide_Character_Set;
   Special_Set           : constant Wide_Character_Set;
   ISO_646_Set           : constant Wide_Character_Set;

   Lower_Case_Map        : constant Wide_Character_Mapping;
     --Maps to lower case for letters, else identity
   Upper_Case_Map        : constant Wide_Character_Mapping;
     --Maps to upper case for letters, else identity
   Basic_Map             : constant Wide_Character_Mapping;
     --Maps to basic letter for letters, else identity

   Character_Set : constant Wide_Maps.Wide_Character_Set;
   --Contains each Wide_Character value WC such that
   --Characters.Conversions.Is_Character(WC) is True

private
   Control_Set : constant Wide_Character_Set :=
     (Ada.Finalization.Controlled with
        Ranges => new Wide_Character_Ranges'
          ((Wide_Character'Val (0), Wide_Character'Val (31)),
           (Wide_Character'Val (127), Wide_Character'Val (159))));

   Graphic_Set : constant Wide_Character_Set :=
     (Ada.Finalization.Controlled with
        Ranges => new Wide_Character_Ranges'
          ((' ', '~'), (Wide_Character'Val (160), Wide_Character'Val (255))));

   Letter_Set : constant Wide_Character_Set :=
     (Ada.Finalization.Controlled with
        Ranges => new Wide_Character_Ranges'
          (('A', 'Z'), ('a', 'z'),
           (Wide_Character'Val (192), Wide_Character'Val (214)),
           (Wide_Character'Val (216), Wide_Character'Val (246)),
           (Wide_Character'Val (248), Wide_Character'Val (255))));

   Lower_Set : constant Wide_Character_Set :=
     (Ada.Finalization.Controlled with
        Ranges => new Wide_Character_Ranges'
          (('a', 'z'), (Wide_Character'Val (223), Wide_Character'Val (246)),
           (Wide_Character'Val (248), Wide_Character'Val (255))));

   Upper_Set : constant Wide_Character_Set :=
     (Ada.Finalization.Controlled with
        Ranges => new Wide_Character_Ranges'
          (('A', 'Z'), (Wide_Character'Val (192), Wide_Character'Val (214)),
           (Wide_Character'Val (216), Wide_Character'Val (222))));

   Basic_Set : constant Wide_Character_Set :=
     (Ada.Finalization.Controlled with
        Ranges => new Wide_Character_Ranges'
          (('A', 'Z'), ('a', 'z'),
           (Wide_Character'Val (198), Wide_Character'Val (198)),
           (Wide_Character'Val (208), Wide_Character'Val (208)),
           (Wide_Character'Val (222), Wide_Character'Val (223)),
           (Wide_Character'Val (230), Wide_Character'Val (230)),
           (Wide_Character'Val (240), Wide_Character'Val (240)),
           (Wide_Character'Val (254), Wide_Character'Val (254))));

   Decimal_Digit_Set : constant Wide_Character_Set :=
     (Ada.Finalization.Controlled with
        Ranges => new Wide_Character_Ranges'
          (1 => ('0', '9')));

   Hexadecimal_Digit_Set : constant Wide_Character_Set :=
     (Ada.Finalization.Controlled with
        Ranges => new Wide_Character_Ranges'
          (('0', '9'), ('A', 'F'), ('a', 'f')));

   Alphanumeric_Set : constant Wide_Character_Set :=
     (Ada.Finalization.Controlled with
        Ranges => new Wide_Character_Ranges'
          (('0', '9'), ('A', 'Z'), ('a', 'z'),
           (Wide_Character'Val (192), Wide_Character'Val (214)),
           (Wide_Character'Val (216), Wide_Character'Val (246)),
           (Wide_Character'Val (248), Wide_Character'Val (255))));

   Special_Set : constant Wide_Character_Set :=
     (Ada.Finalization.Controlled with
        Ranges => new Wide_Character_Ranges'
          ((' ', '/'), (':', '@'), ('[', '`'), ('{', '~'),
           (Wide_Character'Val (160), Wide_Character'Val (191)),
           (Wide_Character'Val (215), Wide_Character'Val (215)),
           (Wide_Character'Val (247), Wide_Character'Val (247))));

   ISO_646_Set : constant Wide_Character_Set :=
     (Ada.Finalization.Controlled with
        Ranges => new Wide_Character_Ranges'
          (1 => (Wide_Character'Val (0), Wide_Character'Val (127))));

   Lower_Case_Map : constant Wide_Character_Mapping :=
     (Ada.Finalization.Controlled with
        Domain  => new Wide_Character_Sequence'
          ('A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M',
           'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z',
           Wide_Character'Val (192), Wide_Character'Val (193),
           Wide_Character'Val (194), Wide_Character'Val (195),
           Wide_Character'Val (196), Wide_Character'Val (197),
           Wide_Character'Val (198), Wide_Character'Val (199),
           Wide_Character'Val (200), Wide_Character'Val (201),
           Wide_Character'Val (202), Wide_Character'Val (203),
           Wide_Character'Val (204), Wide_Character'Val (205),
           Wide_Character'Val (206), Wide_Character'Val (207),
           Wide_Character'Val (208), Wide_Character'Val (209),
           Wide_Character'Val (210), Wide_Character'Val (211),
           Wide_Character'Val (212), Wide_Character'Val (213),
           Wide_Character'Val (214), Wide_Character'Val (216),
           Wide_Character'Val (217), Wide_Character'Val (218),
           Wide_Character'Val (219), Wide_Character'Val (220),
           Wide_Character'Val (221), Wide_Character'Val (222)),
        Targets => new Wide_Character_Sequence'
          ('a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm',
           'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z',
           Wide_Character'Val (224), Wide_Character'Val (225),
           Wide_Character'Val (226), Wide_Character'Val (227),
           Wide_Character'Val (228), Wide_Character'Val (229),
           Wide_Character'Val (230), Wide_Character'Val (231),
           Wide_Character'Val (232), Wide_Character'Val (233),
           Wide_Character'Val (234), Wide_Character'Val (235),
           Wide_Character'Val (236), Wide_Character'Val (237),
           Wide_Character'Val (238), Wide_Character'Val (239),
           Wide_Character'Val (240), Wide_Character'Val (241),
           Wide_Character'Val (242), Wide_Character'Val (243),
           Wide_Character'Val (244), Wide_Character'Val (245),
           Wide_Character'Val (246), Wide_Character'Val (248),
           Wide_Character'Val (249), Wide_Character'Val (250),
           Wide_Character'Val (251), Wide_Character'Val (252),
           Wide_Character'Val (253), Wide_Character'Val (254)));

   Upper_Case_Map : constant Wide_Character_Mapping :=
     (Ada.Finalization.Controlled with
        Domain  => new Wide_Character_Sequence'
          ('a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm',
           'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z',
           Wide_Character'Val (224), Wide_Character'Val (225),
           Wide_Character'Val (226), Wide_Character'Val (227),
           Wide_Character'Val (228), Wide_Character'Val (229),
           Wide_Character'Val (230), Wide_Character'Val (231),
           Wide_Character'Val (232), Wide_Character'Val (233),
           Wide_Character'Val (234), Wide_Character'Val (235),
           Wide_Character'Val (236), Wide_Character'Val (237),
           Wide_Character'Val (238), Wide_Character'Val (239),
           Wide_Character'Val (240), Wide_Character'Val (241),
           Wide_Character'Val (242), Wide_Character'Val (243),
           Wide_Character'Val (244), Wide_Character'Val (245),
           Wide_Character'Val (246), Wide_Character'Val (248),
           Wide_Character'Val (249), Wide_Character'Val (250),
           Wide_Character'Val (251), Wide_Character'Val (252),
           Wide_Character'Val (253), Wide_Character'Val (254)),
        Targets => new Wide_Character_Sequence'
          ('A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M',
           'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z',
           Wide_Character'Val (192), Wide_Character'Val (193),
           Wide_Character'Val (194), Wide_Character'Val (195),
           Wide_Character'Val (196), Wide_Character'Val (197),
           Wide_Character'Val (198), Wide_Character'Val (199),
           Wide_Character'Val (200), Wide_Character'Val (201),
           Wide_Character'Val (202), Wide_Character'Val (203),
           Wide_Character'Val (204), Wide_Character'Val (205),
           Wide_Character'Val (206), Wide_Character'Val (207),
           Wide_Character'Val (208), Wide_Character'Val (209),
           Wide_Character'Val (210), Wide_Character'Val (211),
           Wide_Character'Val (212), Wide_Character'Val (213),
           Wide_Character'Val (214), Wide_Character'Val (216),
           Wide_Character'Val (217), Wide_Character'Val (218),
           Wide_Character'Val (219), Wide_Character'Val (220),
           Wide_Character'Val (221), Wide_Character'Val (222)));

   Basic_Map : constant Wide_Character_Mapping :=
     (Ada.Finalization.Controlled with
        Domain  => new Wide_Character_Sequence'
          (Wide_Character'Val (192), Wide_Character'Val (193),
           Wide_Character'Val (194), Wide_Character'Val (195),
           Wide_Character'Val (196), Wide_Character'Val (197),
           Wide_Character'Val (199), Wide_Character'Val (200),
           Wide_Character'Val (201), Wide_Character'Val (202),
           Wide_Character'Val (203), Wide_Character'Val (204),
           Wide_Character'Val (205), Wide_Character'Val (206),
           Wide_Character'Val (207), Wide_Character'Val (209),
           Wide_Character'Val (210), Wide_Character'Val (211),
           Wide_Character'Val (212), Wide_Character'Val (213),
           Wide_Character'Val (214), Wide_Character'Val (216),
           Wide_Character'Val (217), Wide_Character'Val (218),
           Wide_Character'Val (219), Wide_Character'Val (220),
           Wide_Character'Val (221), Wide_Character'Val (224),
           Wide_Character'Val (225), Wide_Character'Val (226),
           Wide_Character'Val (227), Wide_Character'Val (228),
           Wide_Character'Val (229), Wide_Character'Val (231),
           Wide_Character'Val (232), Wide_Character'Val (233),
           Wide_Character'Val (234), Wide_Character'Val (235),
           Wide_Character'Val (236), Wide_Character'Val (237),
           Wide_Character'Val (238), Wide_Character'Val (239),
           Wide_Character'Val (241), Wide_Character'Val (242),
           Wide_Character'Val (243), Wide_Character'Val (244),
           Wide_Character'Val (245), Wide_Character'Val (246),
           Wide_Character'Val (248), Wide_Character'Val (249),
           Wide_Character'Val (250), Wide_Character'Val (251),
           Wide_Character'Val (252), Wide_Character'Val (253),
           Wide_Character'Val (255)),
        Targets => new Wide_Character_Sequence'
          ('A', 'A', 'A', 'A', 'A', 'A', 'C', 'E', 'E', 'E', 'E', 'I', 'I',
           'I', 'I', 'N', 'O', 'O', 'O', 'O', 'O', 'O', 'U', 'U', 'U', 'U',
           'Y', 'a', 'a', 'a', 'a', 'a', 'a', 'c', 'e', 'e', 'e', 'e', 'i',
           'i', 'i', 'i', 'n', 'o', 'o', 'o', 'o', 'o', 'o', 'u', 'u', 'u',
           'u', 'y', 'y'));

   Character_Set : constant Wide_Character_Set :=
     (Ada.Finalization.Controlled with
        Ranges => new Wide_Character_Ranges'
          (1 => (Wide_Character'Val (0), Wide_Character'Val (255))));

end Ada.Strings.Wide_Maps.Wide_Constants;
