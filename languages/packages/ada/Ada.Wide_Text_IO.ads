--  Ada.Wide_Text_IO.ads
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
--  package Ada.Text_IO in the Ada Reference Manual, 202x Edition, Draft 25 (A.10.1, A.11),
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
--  This is package Ada.Wide_Text_IO, whose specification RM A.11(2/2) defines as
--  that of Ada.Text_IO with Character and String replaced by Wide_Character and
--  Wide_String in the Get, Look_Ahead, Get_Immediate, Get_Line, Put and Put_Line
--  subprograms. The private part is neos's (the language does not specify
--  it); a private with clause of the private unit Ada.Text_IO_Engine, which
--  implements the files, is added. The implementation-defined bounds of Count
--  and Field are given (Integer'Last and 255). File_Type is not a controlled
--  type, contrary to A.10.1(86/2) ("needs finalization"): a file still open
--  when its object ceases to exist remains open until the end of the program,
--  when it is closed.
--  (See languages/packages/ada/NOTICE.)

with Ada.IO_Exceptions;
private with Ada.Text_IO_Engine;
package Ada.Wide_Text_IO is

   type File_Type is limited private;

   type File_Mode is (In_File, Out_File, Append_File);

   type Count is range 0 .. Integer'Last;
   subtype Positive_Count is Count range 1 .. Count'Last;
   Unbounded : constant Count := 0; -- line and page length

   subtype Field       is Integer range 0 .. 255;
   subtype Number_Base is Integer range 2 .. 16;

   type Type_Set is (Lower_Case, Upper_Case);

   -- File Management

   procedure Create (File : in out File_Type;
                     Mode : in File_Mode := Out_File;
                     Name : in String    := "";
                     Form : in String    := "");

   procedure Open   (File : in out File_Type;
                     Mode : in File_Mode;
                     Name : in String;
                     Form : in String := "");

   procedure Close  (File : in out File_Type);
   procedure Delete (File : in out File_Type);
   procedure Reset  (File : in out File_Type; Mode : in File_Mode);
   procedure Reset  (File : in out File_Type);

   function  Mode   (File : in File_Type) return File_Mode;
   function  Name   (File : in File_Type) return String;
   function  Form   (File : in File_Type) return String;

   function  Is_Open(File : in File_Type) return Boolean;

   -- Control of default input and output files

   procedure Set_Input (File : in File_Type);
   procedure Set_Output(File : in File_Type);
   procedure Set_Error (File : in File_Type);

   function Standard_Input  return File_Type;
   function Standard_Output return File_Type;
   function Standard_Error  return File_Type;

   function Current_Input   return File_Type;
   function Current_Output  return File_Type;
   function Current_Error   return File_Type;

   type File_Access is access constant File_Type;

   function Standard_Input  return File_Access;
   function Standard_Output return File_Access;
   function Standard_Error  return File_Access;

   function Current_Input   return File_Access;
   function Current_Output  return File_Access;
   function Current_Error   return File_Access;

   --Buffer control
   procedure Flush (File : in File_Type);
   procedure Flush;

   -- Specification of line and page lengths

   procedure Set_Line_Length(File : in File_Type; To : in Count);
   procedure Set_Line_Length(To   : in Count);

   procedure Set_Page_Length(File : in File_Type; To : in Count);
   procedure Set_Page_Length(To   : in Count);

   function  Line_Length(File : in File_Type) return Count;
   function  Line_Length return Count;

   function  Page_Length(File : in File_Type) return Count;
   function  Page_Length return Count;

   -- Column, Line, and Page Control

   procedure New_Line   (File    : in File_Type;
                         Spacing : in Positive_Count := 1);
   procedure New_Line   (Spacing : in Positive_Count := 1);

   procedure Skip_Line  (File    : in File_Type;
                         Spacing : in Positive_Count := 1);
   procedure Skip_Line  (Spacing : in Positive_Count := 1);

   function  End_Of_Line(File : in File_Type) return Boolean;
   function  End_Of_Line return Boolean;

   procedure New_Page   (File : in File_Type);
   procedure New_Page;

   procedure Skip_Page  (File : in File_Type);
   procedure Skip_Page;

   function  End_Of_Page(File : in File_Type) return Boolean;
   function  End_Of_Page return Boolean;

   function  End_Of_File(File : in File_Type) return Boolean;
   function  End_Of_File return Boolean;

   procedure Set_Col (File : in File_Type; To : in Positive_Count);
   procedure Set_Col (To   : in Positive_Count);

   procedure Set_Line(File : in File_Type; To : in Positive_Count);
   procedure Set_Line(To   : in Positive_Count);

   function Col (File : in File_Type) return Positive_Count;
   function Col  return Positive_Count;

   function Line(File : in File_Type) return Positive_Count;
   function Line return Positive_Count;

   function Page(File : in File_Type) return Positive_Count;
   function Page return Positive_Count;

   -- Character Input-Output

   procedure Get(File : in  File_Type; Item : out Wide_Character);
   procedure Get(Item : out Wide_Character);

   procedure Put(File : in  File_Type; Item : in Wide_Character);
   procedure Put(Item : in  Wide_Character);

   procedure Look_Ahead (File        : in  File_Type;
                         Item        : out Wide_Character;
                         End_Of_Line : out Boolean);
   procedure Look_Ahead (Item        : out Wide_Character;
                         End_Of_Line : out Boolean);

   procedure Get_Immediate(File      : in  File_Type;
                           Item      : out Wide_Character);
   procedure Get_Immediate(Item      : out Wide_Character);

   procedure Get_Immediate(File      : in  File_Type;
                           Item      : out Wide_Character;
                           Available : out Boolean);
   procedure Get_Immediate(Item      : out Wide_Character;
                           Available : out Boolean);

   -- String Input-Output

   procedure Get(File : in  File_Type; Item : out Wide_String);
   procedure Get(Item : out Wide_String);

   procedure Put(File : in  File_Type; Item : in Wide_String);
   procedure Put(Item : in  Wide_String);

   procedure Get_Line(File : in  File_Type;
                      Item : out Wide_String;
                      Last : out Natural);
   procedure Get_Line(Item : out Wide_String; Last : out Natural);

   function Get_Line(File : in  File_Type) return Wide_String;
   function Get_Line return Wide_String;

   procedure Put_Line(File : in  File_Type; Item : in Wide_String);
   procedure Put_Line(Item : in  Wide_String);

   -- Generic packages for Input-Output of Integer Types

   generic
      type Num is range <>;
   package Integer_IO is

      Default_Width : Field := Num'Width;
      Default_Base  : Number_Base := 10;

      procedure Get(File  : in  File_Type;
                    Item  : out Num;
                    Width : in Field := 0);
      procedure Get(Item  : out Num;
                    Width : in  Field := 0);

      procedure Put(File  : in File_Type;
                    Item  : in Num;
                    Width : in Field := Default_Width;
                    Base  : in Number_Base := Default_Base);
      procedure Put(Item  : in Num;
                    Width : in Field := Default_Width;
                    Base  : in Number_Base := Default_Base);
      procedure Get(From : in  Wide_String;
                    Item : out Num;
                    Last : out Positive)
         with Nonblocking;
      procedure Put(To   : out Wide_String;
                    Item : in Num;
                    Base : in Number_Base := Default_Base)
         with Nonblocking;

   end Integer_IO;

   generic
      type Num is mod <>;
   package Modular_IO is

      Default_Width : Field := Num'Width;
      Default_Base  : Number_Base := 10;

      procedure Get(File  : in  File_Type;
                    Item  : out Num;
                    Width : in Field := 0);
      procedure Get(Item  : out Num;
                    Width : in  Field := 0);

      procedure Put(File  : in File_Type;
                    Item  : in Num;
                    Width : in Field := Default_Width;
                    Base  : in Number_Base := Default_Base);
      procedure Put(Item  : in Num;
                    Width : in Field := Default_Width;
                    Base  : in Number_Base := Default_Base);
      procedure Get(From : in  Wide_String;
                    Item : out Num;
                    Last : out Positive)
         with Nonblocking;
      procedure Put(To   : out Wide_String;
                    Item : in Num;
                    Base : in Number_Base := Default_Base)
         with Nonblocking;

   end Modular_IO;

   -- Generic packages for Input-Output of Real Types

   generic
      type Num is digits <>;
   package Float_IO is

      Default_Fore : Field := 2;
      Default_Aft  : Field := Num'Digits-1;
      Default_Exp  : Field := 3;

      procedure Get(File  : in  File_Type;
                    Item  : out Num;
                    Width : in  Field := 0);
      procedure Get(Item  : out Num;
                    Width : in  Field := 0);

      procedure Put(File : in File_Type;
                    Item : in Num;
                    Fore : in Field := Default_Fore;
                    Aft  : in Field := Default_Aft;
                    Exp  : in Field := Default_Exp);
      procedure Put(Item : in Num;
                    Fore : in Field := Default_Fore;
                    Aft  : in Field := Default_Aft;
                    Exp  : in Field := Default_Exp);

      procedure Get(From : in Wide_String;
                    Item : out Num;
                    Last : out Positive)
         with Nonblocking;
      procedure Put(To   : out Wide_String;
                    Item : in Num;
                    Aft  : in Field := Default_Aft;
                    Exp  : in Field := Default_Exp)
         with Nonblocking;
   end Float_IO;

   generic
      type Num is delta <>;
   package Fixed_IO is

      Default_Fore : Field := Num'Fore;
      Default_Aft  : Field := Num'Aft;
      Default_Exp  : Field := 0;

      procedure Get(File  : in  File_Type;
                    Item  : out Num;
                    Width : in  Field := 0);
      procedure Get(Item  : out Num;
                    Width : in  Field := 0);

      procedure Put(File : in File_Type;
                    Item : in Num;
                    Fore : in Field := Default_Fore;
                    Aft  : in Field := Default_Aft;
                    Exp  : in Field := Default_Exp);
      procedure Put(Item : in Num;
                    Fore : in Field := Default_Fore;
                    Aft  : in Field := Default_Aft;
                    Exp  : in Field := Default_Exp);

      procedure Get(From : in  Wide_String;
                    Item : out Num;
                    Last : out Positive)
         with Nonblocking;
      procedure Put(To   : out Wide_String;
                    Item : in Num;
                    Aft  : in Field := Default_Aft;
                    Exp  : in Field := Default_Exp)
         with Nonblocking;
   end Fixed_IO;

   generic
      type Num is delta <> digits <>;
   package Decimal_IO is

      Default_Fore : Field := Num'Fore;
      Default_Aft  : Field := Num'Aft;
      Default_Exp  : Field := 0;

      procedure Get(File  : in  File_Type;
                    Item  : out Num;
                    Width : in  Field := 0);
      procedure Get(Item  : out Num;
                    Width : in  Field := 0);

      procedure Put(File : in File_Type;
                    Item : in Num;
                    Fore : in Field := Default_Fore;
                    Aft  : in Field := Default_Aft;
                    Exp  : in Field := Default_Exp);
      procedure Put(Item : in Num;
                    Fore : in Field := Default_Fore;
                    Aft  : in Field := Default_Aft;
                    Exp  : in Field := Default_Exp);

      procedure Get(From : in  Wide_String;
                    Item : out Num;
                    Last : out Positive)
         with Nonblocking;
      procedure Put(To   : out Wide_String;
                    Item : in Num;
                    Aft  : in Field := Default_Aft;
                    Exp  : in Field := Default_Exp)
         with Nonblocking;
   end Decimal_IO;

   -- Generic package for Input-Output of Enumeration Types

   generic
      type Enum is (<>);
   package Enumeration_IO is

      Default_Width   : Field := 0;
      Default_Setting : Type_Set := Upper_Case;

      procedure Get(File : in  File_Type;
                    Item : out Enum);
      procedure Get(Item : out Enum);

      procedure Put(File  : in File_Type;
                    Item  : in Enum;
                    Width : in Field    := Default_Width;
                    Set   : in Type_Set := Default_Setting);
      procedure Put(Item  : in Enum;
                    Width : in Field    := Default_Width;
                    Set   : in Type_Set := Default_Setting);

      procedure Get(From : in  Wide_String;
                    Item : out Enum;
                    Last : out Positive)
         with Nonblocking;
      procedure Put(To   : out Wide_String;
                    Item : in  Enum;
                    Set  : in  Type_Set := Default_Setting)
         with Nonblocking;
   end Enumeration_IO;

   -- Exceptions

   Status_Error : exception renames IO_Exceptions.Status_Error;
   Mode_Error   : exception renames IO_Exceptions.Mode_Error;
   Name_Error   : exception renames IO_Exceptions.Name_Error;
   Use_Error    : exception renames IO_Exceptions.Use_Error;
   Device_Error : exception renames IO_Exceptions.Device_Error;
   End_Error    : exception renames IO_Exceptions.End_Error;
   Data_Error   : exception renames IO_Exceptions.Data_Error;
   Layout_Error : exception renames IO_Exceptions.Layout_Error;

   package Wide_File_Names is

      -- File management

      procedure Create (File : in out File_Type;
                        Mode : in File_Mode := Out_File;
                        Name : in Wide_String := "";
                        Form : in Wide_String := "");

      procedure Open   (File : in out File_Type;
                        Mode : in File_Mode;
                        Name : in Wide_String;
                        Form : in Wide_String := "");

      function Name    (File : in File_Type) return Wide_String;

      function Form    (File : in File_Type) return Wide_String;

   end Wide_File_Names;

   package Wide_Wide_File_Names is

      -- File management

      procedure Create (File : in out File_Type;
                        Mode : in File_Mode := Out_File;
                        Name : in Wide_Wide_String := "";
                        Form : in Wide_Wide_String := "");

      procedure Open   (File : in out File_Type;
                        Mode : in File_Mode;
                        Name : in Wide_Wide_String;
                        Form : in Wide_Wide_String := "");

      function Name    (File : in File_Type) return Wide_Wide_String;

      function Form    (File : in File_Type) return Wide_Wide_String;

   end Wide_Wide_File_Names;

private

   --  A file object designates the state of an open file (null: closed),
   --  shared with Ada.Text_IO's.
   type File_Type is limited record
      File : Text_IO_Engine.File_Access;
   end record;

end Ada.Wide_Text_IO;
