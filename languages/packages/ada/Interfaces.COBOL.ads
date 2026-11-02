--  Interfaces.COBOL.ads
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
--  package Interfaces.COBOL in the Ada Reference Manual, 202x Edition, Draft 25 (B.4),
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
--  The implementation-defined values and types are neos's (COBOL_Character is
--  derived from Character and Ada_To_COBOL and COBOL_To_Ada are identity
--  mappings). Line breaks in the declarations of Ada_To_COBOL and COBOL_To_Ada
--  are moved. The private part is neos's.
--  (See languages/packages/ada/NOTICE.)

package Interfaces.COBOL
   with Preelaborate, Nonblocking is

-- Types and operations for internal data representations

   type Floating      is digits 6;
   type Long_Floating is digits 15;

   type Binary      is range -2 ** 31 .. 2 ** 31 - 1;
   type Long_Binary is range -2 ** 63 .. 2 ** 63 - 1;

   Max_Digits_Binary      : constant := 9;
   Max_Digits_Long_Binary : constant := 18;

   type Decimal_Element  is mod 16;
   type Packed_Decimal is array (Positive range <>) of Decimal_Element
      with Pack;

   type COBOL_Character is new Character;

   Ada_To_COBOL : array (Character) of COBOL_Character :=
      (COBOL_Character'Val (0), COBOL_Character'Val (1), COBOL_Character'Val (2), COBOL_Character'Val (3),
       COBOL_Character'Val (4), COBOL_Character'Val (5), COBOL_Character'Val (6), COBOL_Character'Val (7),
       COBOL_Character'Val (8), COBOL_Character'Val (9), COBOL_Character'Val (10), COBOL_Character'Val (11),
       COBOL_Character'Val (12), COBOL_Character'Val (13), COBOL_Character'Val (14), COBOL_Character'Val (15),
       COBOL_Character'Val (16), COBOL_Character'Val (17), COBOL_Character'Val (18), COBOL_Character'Val (19),
       COBOL_Character'Val (20), COBOL_Character'Val (21), COBOL_Character'Val (22), COBOL_Character'Val (23),
       COBOL_Character'Val (24), COBOL_Character'Val (25), COBOL_Character'Val (26), COBOL_Character'Val (27),
       COBOL_Character'Val (28), COBOL_Character'Val (29), COBOL_Character'Val (30), COBOL_Character'Val (31),
       COBOL_Character'Val (32), COBOL_Character'Val (33), COBOL_Character'Val (34), COBOL_Character'Val (35),
       COBOL_Character'Val (36), COBOL_Character'Val (37), COBOL_Character'Val (38), COBOL_Character'Val (39),
       COBOL_Character'Val (40), COBOL_Character'Val (41), COBOL_Character'Val (42), COBOL_Character'Val (43),
       COBOL_Character'Val (44), COBOL_Character'Val (45), COBOL_Character'Val (46), COBOL_Character'Val (47),
       COBOL_Character'Val (48), COBOL_Character'Val (49), COBOL_Character'Val (50), COBOL_Character'Val (51),
       COBOL_Character'Val (52), COBOL_Character'Val (53), COBOL_Character'Val (54), COBOL_Character'Val (55),
       COBOL_Character'Val (56), COBOL_Character'Val (57), COBOL_Character'Val (58), COBOL_Character'Val (59),
       COBOL_Character'Val (60), COBOL_Character'Val (61), COBOL_Character'Val (62), COBOL_Character'Val (63),
       COBOL_Character'Val (64), COBOL_Character'Val (65), COBOL_Character'Val (66), COBOL_Character'Val (67),
       COBOL_Character'Val (68), COBOL_Character'Val (69), COBOL_Character'Val (70), COBOL_Character'Val (71),
       COBOL_Character'Val (72), COBOL_Character'Val (73), COBOL_Character'Val (74), COBOL_Character'Val (75),
       COBOL_Character'Val (76), COBOL_Character'Val (77), COBOL_Character'Val (78), COBOL_Character'Val (79),
       COBOL_Character'Val (80), COBOL_Character'Val (81), COBOL_Character'Val (82), COBOL_Character'Val (83),
       COBOL_Character'Val (84), COBOL_Character'Val (85), COBOL_Character'Val (86), COBOL_Character'Val (87),
       COBOL_Character'Val (88), COBOL_Character'Val (89), COBOL_Character'Val (90), COBOL_Character'Val (91),
       COBOL_Character'Val (92), COBOL_Character'Val (93), COBOL_Character'Val (94), COBOL_Character'Val (95),
       COBOL_Character'Val (96), COBOL_Character'Val (97), COBOL_Character'Val (98), COBOL_Character'Val (99),
       COBOL_Character'Val (100), COBOL_Character'Val (101), COBOL_Character'Val (102), COBOL_Character'Val (103),
       COBOL_Character'Val (104), COBOL_Character'Val (105), COBOL_Character'Val (106), COBOL_Character'Val (107),
       COBOL_Character'Val (108), COBOL_Character'Val (109), COBOL_Character'Val (110), COBOL_Character'Val (111),
       COBOL_Character'Val (112), COBOL_Character'Val (113), COBOL_Character'Val (114), COBOL_Character'Val (115),
       COBOL_Character'Val (116), COBOL_Character'Val (117), COBOL_Character'Val (118), COBOL_Character'Val (119),
       COBOL_Character'Val (120), COBOL_Character'Val (121), COBOL_Character'Val (122), COBOL_Character'Val (123),
       COBOL_Character'Val (124), COBOL_Character'Val (125), COBOL_Character'Val (126), COBOL_Character'Val (127),
       COBOL_Character'Val (128), COBOL_Character'Val (129), COBOL_Character'Val (130), COBOL_Character'Val (131),
       COBOL_Character'Val (132), COBOL_Character'Val (133), COBOL_Character'Val (134), COBOL_Character'Val (135),
       COBOL_Character'Val (136), COBOL_Character'Val (137), COBOL_Character'Val (138), COBOL_Character'Val (139),
       COBOL_Character'Val (140), COBOL_Character'Val (141), COBOL_Character'Val (142), COBOL_Character'Val (143),
       COBOL_Character'Val (144), COBOL_Character'Val (145), COBOL_Character'Val (146), COBOL_Character'Val (147),
       COBOL_Character'Val (148), COBOL_Character'Val (149), COBOL_Character'Val (150), COBOL_Character'Val (151),
       COBOL_Character'Val (152), COBOL_Character'Val (153), COBOL_Character'Val (154), COBOL_Character'Val (155),
       COBOL_Character'Val (156), COBOL_Character'Val (157), COBOL_Character'Val (158), COBOL_Character'Val (159),
       COBOL_Character'Val (160), COBOL_Character'Val (161), COBOL_Character'Val (162), COBOL_Character'Val (163),
       COBOL_Character'Val (164), COBOL_Character'Val (165), COBOL_Character'Val (166), COBOL_Character'Val (167),
       COBOL_Character'Val (168), COBOL_Character'Val (169), COBOL_Character'Val (170), COBOL_Character'Val (171),
       COBOL_Character'Val (172), COBOL_Character'Val (173), COBOL_Character'Val (174), COBOL_Character'Val (175),
       COBOL_Character'Val (176), COBOL_Character'Val (177), COBOL_Character'Val (178), COBOL_Character'Val (179),
       COBOL_Character'Val (180), COBOL_Character'Val (181), COBOL_Character'Val (182), COBOL_Character'Val (183),
       COBOL_Character'Val (184), COBOL_Character'Val (185), COBOL_Character'Val (186), COBOL_Character'Val (187),
       COBOL_Character'Val (188), COBOL_Character'Val (189), COBOL_Character'Val (190), COBOL_Character'Val (191),
       COBOL_Character'Val (192), COBOL_Character'Val (193), COBOL_Character'Val (194), COBOL_Character'Val (195),
       COBOL_Character'Val (196), COBOL_Character'Val (197), COBOL_Character'Val (198), COBOL_Character'Val (199),
       COBOL_Character'Val (200), COBOL_Character'Val (201), COBOL_Character'Val (202), COBOL_Character'Val (203),
       COBOL_Character'Val (204), COBOL_Character'Val (205), COBOL_Character'Val (206), COBOL_Character'Val (207),
       COBOL_Character'Val (208), COBOL_Character'Val (209), COBOL_Character'Val (210), COBOL_Character'Val (211),
       COBOL_Character'Val (212), COBOL_Character'Val (213), COBOL_Character'Val (214), COBOL_Character'Val (215),
       COBOL_Character'Val (216), COBOL_Character'Val (217), COBOL_Character'Val (218), COBOL_Character'Val (219),
       COBOL_Character'Val (220), COBOL_Character'Val (221), COBOL_Character'Val (222), COBOL_Character'Val (223),
       COBOL_Character'Val (224), COBOL_Character'Val (225), COBOL_Character'Val (226), COBOL_Character'Val (227),
       COBOL_Character'Val (228), COBOL_Character'Val (229), COBOL_Character'Val (230), COBOL_Character'Val (231),
       COBOL_Character'Val (232), COBOL_Character'Val (233), COBOL_Character'Val (234), COBOL_Character'Val (235),
       COBOL_Character'Val (236), COBOL_Character'Val (237), COBOL_Character'Val (238), COBOL_Character'Val (239),
       COBOL_Character'Val (240), COBOL_Character'Val (241), COBOL_Character'Val (242), COBOL_Character'Val (243),
       COBOL_Character'Val (244), COBOL_Character'Val (245), COBOL_Character'Val (246), COBOL_Character'Val (247),
       COBOL_Character'Val (248), COBOL_Character'Val (249), COBOL_Character'Val (250), COBOL_Character'Val (251),
       COBOL_Character'Val (252), COBOL_Character'Val (253), COBOL_Character'Val (254), COBOL_Character'Val (255));

   COBOL_To_Ada : array (COBOL_Character) of Character :=
      (Character'Val (0), Character'Val (1), Character'Val (2), Character'Val (3),
       Character'Val (4), Character'Val (5), Character'Val (6), Character'Val (7),
       Character'Val (8), Character'Val (9), Character'Val (10), Character'Val (11),
       Character'Val (12), Character'Val (13), Character'Val (14), Character'Val (15),
       Character'Val (16), Character'Val (17), Character'Val (18), Character'Val (19),
       Character'Val (20), Character'Val (21), Character'Val (22), Character'Val (23),
       Character'Val (24), Character'Val (25), Character'Val (26), Character'Val (27),
       Character'Val (28), Character'Val (29), Character'Val (30), Character'Val (31),
       Character'Val (32), Character'Val (33), Character'Val (34), Character'Val (35),
       Character'Val (36), Character'Val (37), Character'Val (38), Character'Val (39),
       Character'Val (40), Character'Val (41), Character'Val (42), Character'Val (43),
       Character'Val (44), Character'Val (45), Character'Val (46), Character'Val (47),
       Character'Val (48), Character'Val (49), Character'Val (50), Character'Val (51),
       Character'Val (52), Character'Val (53), Character'Val (54), Character'Val (55),
       Character'Val (56), Character'Val (57), Character'Val (58), Character'Val (59),
       Character'Val (60), Character'Val (61), Character'Val (62), Character'Val (63),
       Character'Val (64), Character'Val (65), Character'Val (66), Character'Val (67),
       Character'Val (68), Character'Val (69), Character'Val (70), Character'Val (71),
       Character'Val (72), Character'Val (73), Character'Val (74), Character'Val (75),
       Character'Val (76), Character'Val (77), Character'Val (78), Character'Val (79),
       Character'Val (80), Character'Val (81), Character'Val (82), Character'Val (83),
       Character'Val (84), Character'Val (85), Character'Val (86), Character'Val (87),
       Character'Val (88), Character'Val (89), Character'Val (90), Character'Val (91),
       Character'Val (92), Character'Val (93), Character'Val (94), Character'Val (95),
       Character'Val (96), Character'Val (97), Character'Val (98), Character'Val (99),
       Character'Val (100), Character'Val (101), Character'Val (102), Character'Val (103),
       Character'Val (104), Character'Val (105), Character'Val (106), Character'Val (107),
       Character'Val (108), Character'Val (109), Character'Val (110), Character'Val (111),
       Character'Val (112), Character'Val (113), Character'Val (114), Character'Val (115),
       Character'Val (116), Character'Val (117), Character'Val (118), Character'Val (119),
       Character'Val (120), Character'Val (121), Character'Val (122), Character'Val (123),
       Character'Val (124), Character'Val (125), Character'Val (126), Character'Val (127),
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
       Character'Val (224), Character'Val (225), Character'Val (226), Character'Val (227),
       Character'Val (228), Character'Val (229), Character'Val (230), Character'Val (231),
       Character'Val (232), Character'Val (233), Character'Val (234), Character'Val (235),
       Character'Val (236), Character'Val (237), Character'Val (238), Character'Val (239),
       Character'Val (240), Character'Val (241), Character'Val (242), Character'Val (243),
       Character'Val (244), Character'Val (245), Character'Val (246), Character'Val (247),
       Character'Val (248), Character'Val (249), Character'Val (250), Character'Val (251),
       Character'Val (252), Character'Val (253), Character'Val (254), Character'Val (255));

   type Alphanumeric is array (Positive range <>) of COBOL_Character
      with Pack;

   function To_COBOL (Item : in String) return Alphanumeric;
   function To_Ada   (Item : in Alphanumeric) return String;

   procedure To_COBOL (Item       : in String;
                       Target     : out Alphanumeric;
                       Last       : out Natural);

   procedure To_Ada (Item     : in Alphanumeric;
                     Target   : out String;
                     Last     : out Natural);

   type Numeric is array (Positive range <>) of COBOL_Character
      with Pack;

-- Formats for COBOL data representations

   type Display_Format is private;

   Unsigned             : constant Display_Format;
   Leading_Separate     : constant Display_Format;
   Trailing_Separate    : constant Display_Format;
   Leading_Nonseparate  : constant Display_Format;
   Trailing_Nonseparate : constant Display_Format;

   type Binary_Format is private;

   High_Order_First  : constant Binary_Format;
   Low_Order_First   : constant Binary_Format;
   Native_Binary     : constant Binary_Format;

   type Packed_Format is private;

   Packed_Unsigned   : constant Packed_Format;
   Packed_Signed     : constant Packed_Format;

-- Types for external representation of COBOL binary data

   type Byte is mod 2**COBOL_Character'Size;
   type Byte_Array is array (Positive range <>) of Byte
      with Pack;

   Conversion_Error : exception;

   generic
      type Num is delta <> digits <>;
   package Decimal_Conversions is

      -- Display Formats: data values are represented as Numeric

      function Valid (Item   : in Numeric;
                      Format : in Display_Format) return Boolean;

      function Length (Format : in Display_Format) return Natural;

      function To_Decimal (Item   : in Numeric;
                           Format : in Display_Format) return Num;

      function To_Display (Item   : in Num;
                           Format : in Display_Format) return Numeric;

      -- Packed Formats: data values are represented as Packed_Decimal

      function Valid (Item   : in Packed_Decimal;
                      Format : in Packed_Format) return Boolean;

      function Length (Format : in Packed_Format) return Natural;

      function To_Decimal (Item   : in Packed_Decimal;
                           Format : in Packed_Format) return Num;

      function To_Packed (Item   : in Num;
                          Format : in Packed_Format) return Packed_Decimal;

      -- Binary Formats: external data values are represented as Byte_Array

      function Valid (Item   : in Byte_Array;
                      Format : in Binary_Format) return Boolean;

      function Length (Format : in Binary_Format) return Natural;
      function To_Decimal (Item   : in Byte_Array;
                           Format : in Binary_Format) return Num;

      function To_Binary (Item   : in Num;
                        Format : in Binary_Format) return Byte_Array;

      -- Internal Binary formats: data values are of type Binary or Long_Binary

      function To_Decimal (Item : in Binary)      return Num;
      function To_Decimal (Item : in Long_Binary) return Num;

      function To_Binary      (Item : in Num)  return Binary;
      function To_Long_Binary (Item : in Num)  return Long_Binary;

   end Decimal_Conversions;

private
   --  Display formats: digits are the characters '0' .. '9'; a separate sign
   --  is '+' or '-'; a nonseparate (overpunched) sign is carried by the
   --  leading or trailing digit: for a negative value that digit is '}' (0)
   --  or 'J' .. 'R' (1 .. 9), for a positive value it is the digit itself or
   --  '{' (0) or 'A' .. 'I' (1 .. 9).
   type Display_Format is
     (Unsigned_Display, Leading_Separate_Display, Trailing_Separate_Display,
      Leading_Nonseparate_Display, Trailing_Nonseparate_Display);
   Unsigned             : constant Display_Format := Unsigned_Display;
   Leading_Separate     : constant Display_Format := Leading_Separate_Display;
   Trailing_Separate    : constant Display_Format := Trailing_Separate_Display;
   Leading_Nonseparate  : constant Display_Format := Leading_Nonseparate_Display;
   Trailing_Nonseparate : constant Display_Format := Trailing_Nonseparate_Display;

   --  Binary formats: two's complement, in the minimal number of bytes that
   --  holds every value of the decimal type (at most 8).
   type Binary_Format is (High_Order_First_Binary, Low_Order_First_Binary);
   High_Order_First  : constant Binary_Format := High_Order_First_Binary;
   Low_Order_First   : constant Binary_Format := Low_Order_First_Binary;
   Native_Binary     : constant Binary_Format := Low_Order_First_Binary;

   --  Packed formats: one decimal digit per element, most significant first,
   --  then a sign element: 16#F# (unsigned); 16#C# (or 16#A#, 16#E#, 16#F#)
   --  positive or 16#D# (or 16#B#) negative (signed).
   type Packed_Format is (Packed_Unsigned_Format, Packed_Signed_Format);
   Packed_Unsigned   : constant Packed_Format := Packed_Unsigned_Format;
   Packed_Signed     : constant Packed_Format := Packed_Signed_Format;
end Interfaces.COBOL;
