--  Ada.Strings.Maps.Constants.ads
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
--  package Ada.Strings.Maps.Constants in the Ada Reference Manual, 202x Edition, Draft 25 (A.4.6),
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
--  the private part is neos's; re-indented to 3 spaces.
--  (See languages/packages/ada/NOTICE.)

package Ada.Strings.Maps.Constants is
   pragma Pure(Constants);

   Control_Set           : constant Character_Set;
   Graphic_Set           : constant Character_Set;
   Letter_Set            : constant Character_Set;
   Lower_Set             : constant Character_Set;
   Upper_Set             : constant Character_Set;
   Basic_Set             : constant Character_Set;
   Decimal_Digit_Set     : constant Character_Set;
   Hexadecimal_Digit_Set : constant Character_Set;
   Alphanumeric_Set      : constant Character_Set;
   Special_Set           : constant Character_Set;
   ISO_646_Set           : constant Character_Set;

   Lower_Case_Map        : constant Character_Mapping;
     --Maps to lower case for letters, else identity
   Upper_Case_Map        : constant Character_Mapping;
     --Maps to upper case for letters, else identity
   Basic_Map             : constant Character_Mapping;
     --Maps to basic letter for letters, else identity

private
   Control_Set : constant Character_Set :=
     (Bits =>
        (Character'Val (0) .. Character'Val (31) |
          Character'Val (127) .. Character'Val (159)
         => True,
         others => False));

   Graphic_Set : constant Character_Set :=
     (Bits =>
        (' ' .. '~' | Character'Val (160) .. Character'Val (255)
         => True,
         others => False));

   Letter_Set : constant Character_Set :=
     (Bits =>
        ('A' .. 'Z' | 'a' .. 'z' |
          Character'Val (192) .. Character'Val (214) |
          Character'Val (216) .. Character'Val (246) |
          Character'Val (248) .. Character'Val (255)
         => True,
         others => False));

   Lower_Set : constant Character_Set :=
     (Bits =>
        ('a' .. 'z' | Character'Val (223) .. Character'Val (246) |
          Character'Val (248) .. Character'Val (255)
         => True,
         others => False));

   Upper_Set : constant Character_Set :=
     (Bits =>
        ('A' .. 'Z' | Character'Val (192) .. Character'Val (214) |
          Character'Val (216) .. Character'Val (222)
         => True,
         others => False));

   Basic_Set : constant Character_Set :=
     (Bits =>
        ('A' .. 'Z' | 'a' .. 'z' | Character'Val (198) |
          Character'Val (208) |
          Character'Val (222) .. Character'Val (223) |
          Character'Val (230) | Character'Val (240) |
          Character'Val (254)
         => True,
         others => False));

   Decimal_Digit_Set : constant Character_Set :=
     (Bits =>
        ('0' .. '9'
         => True,
         others => False));

   Hexadecimal_Digit_Set : constant Character_Set :=
     (Bits =>
        ('0' .. '9' | 'A' .. 'F' | 'a' .. 'f'
         => True,
         others => False));

   Alphanumeric_Set : constant Character_Set :=
     (Bits =>
        ('0' .. '9' | 'A' .. 'Z' | 'a' .. 'z' |
          Character'Val (192) .. Character'Val (214) |
          Character'Val (216) .. Character'Val (246) |
          Character'Val (248) .. Character'Val (255)
         => True,
         others => False));

   Special_Set : constant Character_Set :=
     (Bits =>
        (' ' .. '/' | ':' .. '@' | '[' .. '`' | '{' .. '~' |
          Character'Val (160) .. Character'Val (191) |
          Character'Val (215) | Character'Val (247)
         => True,
         others => False));

   ISO_646_Set : constant Character_Set :=
     (Bits =>
        (Character'Val (0) .. Character'Val (127)
         => True,
         others => False));

   Lower_Case_Map : constant Character_Mapping :=
     (Map =>
        (Character'Val (0), Character'Val (1), Character'Val (2), Character'Val (3),
         Character'Val (4), Character'Val (5), Character'Val (6), Character'Val (7),
         Character'Val (8), Character'Val (9), Character'Val (10), Character'Val (11),
         Character'Val (12), Character'Val (13), Character'Val (14), Character'Val (15),
         Character'Val (16), Character'Val (17), Character'Val (18), Character'Val (19),
         Character'Val (20), Character'Val (21), Character'Val (22), Character'Val (23),
         Character'Val (24), Character'Val (25), Character'Val (26), Character'Val (27),
         Character'Val (28), Character'Val (29), Character'Val (30), Character'Val (31),
         ' ', '!', '"', '#',
         '$', '%', '&', ''',
         '(', ')', '*', '+',
         ',', '-', '.', '/',
         '0', '1', '2', '3',
         '4', '5', '6', '7',
         '8', '9', ':', ';',
         '<', '=', '>', '?',
         '@', 'a', 'b', 'c',
         'd', 'e', 'f', 'g',
         'h', 'i', 'j', 'k',
         'l', 'm', 'n', 'o',
         'p', 'q', 'r', 's',
         't', 'u', 'v', 'w',
         'x', 'y', 'z', '[',
         '\', ']', '^', '_',
         '`', 'a', 'b', 'c',
         'd', 'e', 'f', 'g',
         'h', 'i', 'j', 'k',
         'l', 'm', 'n', 'o',
         'p', 'q', 'r', 's',
         't', 'u', 'v', 'w',
         'x', 'y', 'z', '{',
         '|', '}', '~', Character'Val (127),
         Character'Val (128), Character'Val (129), Character'Val (130), Character'Val (131),
         Character'Val (132), Character'Val (133), Character'Val (134), Character'Val (135),
         Character'Val (136), Character'Val (137), Character'Val (138), Character'Val (139),
         Character'Val (140), Character'Val (141), Character'Val (142), Character'Val (143),
         Character'Val (144), Character'Val (145), Character'Val (146), Character'Val (147),
         Character'Val (148), Character'Val (149), Character'Val (150), Character'Val (151),
         Character'Val (152), Character'Val (153), Character'Val (154), Character'Val (155),
         Character'Val (156), Character'Val (157), Character'Val (158), Character'Val (159),
         Character'Val (160), Character'Val (161), Character'Val (162), Character'Val (163),
         Character'Val (164), Character'Val (165), Character'Val (166), Character'Val (167),
         Character'Val (168), Character'Val (169), Character'Val (170), Character'Val (171),
         Character'Val (172), Character'Val (173), Character'Val (174), Character'Val (175),
         Character'Val (176), Character'Val (177), Character'Val (178), Character'Val (179),
         Character'Val (180), Character'Val (181), Character'Val (182), Character'Val (183),
         Character'Val (184), Character'Val (185), Character'Val (186), Character'Val (187),
         Character'Val (188), Character'Val (189), Character'Val (190), Character'Val (191),
         Character'Val (224), Character'Val (225), Character'Val (226), Character'Val (227),
         Character'Val (228), Character'Val (229), Character'Val (230), Character'Val (231),
         Character'Val (232), Character'Val (233), Character'Val (234), Character'Val (235),
         Character'Val (236), Character'Val (237), Character'Val (238), Character'Val (239),
         Character'Val (240), Character'Val (241), Character'Val (242), Character'Val (243),
         Character'Val (244), Character'Val (245), Character'Val (246), Character'Val (215),
         Character'Val (248), Character'Val (249), Character'Val (250), Character'Val (251),
         Character'Val (252), Character'Val (253), Character'Val (254), Character'Val (223),
         Character'Val (224), Character'Val (225), Character'Val (226), Character'Val (227),
         Character'Val (228), Character'Val (229), Character'Val (230), Character'Val (231),
         Character'Val (232), Character'Val (233), Character'Val (234), Character'Val (235),
         Character'Val (236), Character'Val (237), Character'Val (238), Character'Val (239),
         Character'Val (240), Character'Val (241), Character'Val (242), Character'Val (243),
         Character'Val (244), Character'Val (245), Character'Val (246), Character'Val (247),
         Character'Val (248), Character'Val (249), Character'Val (250), Character'Val (251),
         Character'Val (252), Character'Val (253), Character'Val (254), Character'Val (255)));

   Upper_Case_Map : constant Character_Mapping :=
     (Map =>
        (Character'Val (0), Character'Val (1), Character'Val (2), Character'Val (3),
         Character'Val (4), Character'Val (5), Character'Val (6), Character'Val (7),
         Character'Val (8), Character'Val (9), Character'Val (10), Character'Val (11),
         Character'Val (12), Character'Val (13), Character'Val (14), Character'Val (15),
         Character'Val (16), Character'Val (17), Character'Val (18), Character'Val (19),
         Character'Val (20), Character'Val (21), Character'Val (22), Character'Val (23),
         Character'Val (24), Character'Val (25), Character'Val (26), Character'Val (27),
         Character'Val (28), Character'Val (29), Character'Val (30), Character'Val (31),
         ' ', '!', '"', '#',
         '$', '%', '&', ''',
         '(', ')', '*', '+',
         ',', '-', '.', '/',
         '0', '1', '2', '3',
         '4', '5', '6', '7',
         '8', '9', ':', ';',
         '<', '=', '>', '?',
         '@', 'A', 'B', 'C',
         'D', 'E', 'F', 'G',
         'H', 'I', 'J', 'K',
         'L', 'M', 'N', 'O',
         'P', 'Q', 'R', 'S',
         'T', 'U', 'V', 'W',
         'X', 'Y', 'Z', '[',
         '\', ']', '^', '_',
         '`', 'A', 'B', 'C',
         'D', 'E', 'F', 'G',
         'H', 'I', 'J', 'K',
         'L', 'M', 'N', 'O',
         'P', 'Q', 'R', 'S',
         'T', 'U', 'V', 'W',
         'X', 'Y', 'Z', '{',
         '|', '}', '~', Character'Val (127),
         Character'Val (128), Character'Val (129), Character'Val (130), Character'Val (131),
         Character'Val (132), Character'Val (133), Character'Val (134), Character'Val (135),
         Character'Val (136), Character'Val (137), Character'Val (138), Character'Val (139),
         Character'Val (140), Character'Val (141), Character'Val (142), Character'Val (143),
         Character'Val (144), Character'Val (145), Character'Val (146), Character'Val (147),
         Character'Val (148), Character'Val (149), Character'Val (150), Character'Val (151),
         Character'Val (152), Character'Val (153), Character'Val (154), Character'Val (155),
         Character'Val (156), Character'Val (157), Character'Val (158), Character'Val (159),
         Character'Val (160), Character'Val (161), Character'Val (162), Character'Val (163),
         Character'Val (164), Character'Val (165), Character'Val (166), Character'Val (167),
         Character'Val (168), Character'Val (169), Character'Val (170), Character'Val (171),
         Character'Val (172), Character'Val (173), Character'Val (174), Character'Val (175),
         Character'Val (176), Character'Val (177), Character'Val (178), Character'Val (179),
         Character'Val (180), Character'Val (181), Character'Val (182), Character'Val (183),
         Character'Val (184), Character'Val (185), Character'Val (186), Character'Val (187),
         Character'Val (188), Character'Val (189), Character'Val (190), Character'Val (191),
         Character'Val (192), Character'Val (193), Character'Val (194), Character'Val (195),
         Character'Val (196), Character'Val (197), Character'Val (198), Character'Val (199),
         Character'Val (200), Character'Val (201), Character'Val (202), Character'Val (203),
         Character'Val (204), Character'Val (205), Character'Val (206), Character'Val (207),
         Character'Val (208), Character'Val (209), Character'Val (210), Character'Val (211),
         Character'Val (212), Character'Val (213), Character'Val (214), Character'Val (215),
         Character'Val (216), Character'Val (217), Character'Val (218), Character'Val (219),
         Character'Val (220), Character'Val (221), Character'Val (222), Character'Val (223),
         Character'Val (192), Character'Val (193), Character'Val (194), Character'Val (195),
         Character'Val (196), Character'Val (197), Character'Val (198), Character'Val (199),
         Character'Val (200), Character'Val (201), Character'Val (202), Character'Val (203),
         Character'Val (204), Character'Val (205), Character'Val (206), Character'Val (207),
         Character'Val (208), Character'Val (209), Character'Val (210), Character'Val (211),
         Character'Val (212), Character'Val (213), Character'Val (214), Character'Val (247),
         Character'Val (216), Character'Val (217), Character'Val (218), Character'Val (219),
         Character'Val (220), Character'Val (221), Character'Val (222), Character'Val (255)));

   Basic_Map : constant Character_Mapping :=
     (Map =>
        (Character'Val (0), Character'Val (1), Character'Val (2), Character'Val (3),
         Character'Val (4), Character'Val (5), Character'Val (6), Character'Val (7),
         Character'Val (8), Character'Val (9), Character'Val (10), Character'Val (11),
         Character'Val (12), Character'Val (13), Character'Val (14), Character'Val (15),
         Character'Val (16), Character'Val (17), Character'Val (18), Character'Val (19),
         Character'Val (20), Character'Val (21), Character'Val (22), Character'Val (23),
         Character'Val (24), Character'Val (25), Character'Val (26), Character'Val (27),
         Character'Val (28), Character'Val (29), Character'Val (30), Character'Val (31),
         ' ', '!', '"', '#',
         '$', '%', '&', ''',
         '(', ')', '*', '+',
         ',', '-', '.', '/',
         '0', '1', '2', '3',
         '4', '5', '6', '7',
         '8', '9', ':', ';',
         '<', '=', '>', '?',
         '@', 'A', 'B', 'C',
         'D', 'E', 'F', 'G',
         'H', 'I', 'J', 'K',
         'L', 'M', 'N', 'O',
         'P', 'Q', 'R', 'S',
         'T', 'U', 'V', 'W',
         'X', 'Y', 'Z', '[',
         '\', ']', '^', '_',
         '`', 'a', 'b', 'c',
         'd', 'e', 'f', 'g',
         'h', 'i', 'j', 'k',
         'l', 'm', 'n', 'o',
         'p', 'q', 'r', 's',
         't', 'u', 'v', 'w',
         'x', 'y', 'z', '{',
         '|', '}', '~', Character'Val (127),
         Character'Val (128), Character'Val (129), Character'Val (130), Character'Val (131),
         Character'Val (132), Character'Val (133), Character'Val (134), Character'Val (135),
         Character'Val (136), Character'Val (137), Character'Val (138), Character'Val (139),
         Character'Val (140), Character'Val (141), Character'Val (142), Character'Val (143),
         Character'Val (144), Character'Val (145), Character'Val (146), Character'Val (147),
         Character'Val (148), Character'Val (149), Character'Val (150), Character'Val (151),
         Character'Val (152), Character'Val (153), Character'Val (154), Character'Val (155),
         Character'Val (156), Character'Val (157), Character'Val (158), Character'Val (159),
         Character'Val (160), Character'Val (161), Character'Val (162), Character'Val (163),
         Character'Val (164), Character'Val (165), Character'Val (166), Character'Val (167),
         Character'Val (168), Character'Val (169), Character'Val (170), Character'Val (171),
         Character'Val (172), Character'Val (173), Character'Val (174), Character'Val (175),
         Character'Val (176), Character'Val (177), Character'Val (178), Character'Val (179),
         Character'Val (180), Character'Val (181), Character'Val (182), Character'Val (183),
         Character'Val (184), Character'Val (185), Character'Val (186), Character'Val (187),
         Character'Val (188), Character'Val (189), Character'Val (190), Character'Val (191),
         'A', 'A', 'A', 'A',
         'A', 'A', Character'Val (198), 'C',
         'E', 'E', 'E', 'E',
         'I', 'I', 'I', 'I',
         Character'Val (208), 'N', 'O', 'O',
         'O', 'O', 'O', Character'Val (215),
         'O', 'U', 'U', 'U',
         'U', 'Y', Character'Val (222), Character'Val (223),
         'a', 'a', 'a', 'a',
         'a', 'a', Character'Val (230), 'c',
         'e', 'e', 'e', 'e',
         'i', 'i', 'i', 'i',
         Character'Val (240), 'n', 'o', 'o',
         'o', 'o', 'o', Character'Val (247),
         'o', 'u', 'u', 'u',
         'u', 'y', Character'Val (254), 'y'));

end Ada.Strings.Maps.Constants;
