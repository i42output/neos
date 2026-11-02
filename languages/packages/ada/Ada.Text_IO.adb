--  Ada.Text_IO.adb
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

with Interfaces;
with Ada.Native_IO;
with Ada.Text_IO_Engine;
with Ada.Text_IO_Numbers;
pragma Elaborate_All (Ada.Text_IO_Engine);

package body Ada.Text_IO is

   package Engine renames Ada.Text_IO_Engine;
   package Numbers renames Ada.Text_IO_Numbers;

   use type Engine.File_Access;
   use type Engine.File_Mode;
   use type Interfaces.Integer_64;
   use type Interfaces.Unsigned_64;

   subtype Int is Interfaces.Integer_64;
   subtype Uns is Interfaces.Unsigned_64;

   function To_Engine (Mode : File_Mode) return Engine.File_Mode is
     (Engine.File_Mode'Val (File_Mode'Pos (Mode)));

   --  The standard files and the current default files

   Standard_In  : aliased File_Type :=
      (File => Engine.Standard_File
                 (Native_IO.Standard_Input_Handle, Engine.Latin_1));
   Standard_Out : aliased File_Type :=
      (File => Engine.Standard_File
                 (Native_IO.Standard_Output_Handle, Engine.Latin_1));
   Standard_Err : aliased File_Type :=
      (File => Engine.Standard_File
                 (Native_IO.Standard_Error_Handle, Engine.Latin_1));

   Current_In  : aliased File_Type := (File => Standard_In.File);
   Current_Out : aliased File_Type := (File => Standard_Out.File);
   Current_Err : aliased File_Type := (File => Standard_Err.File);

   --  (the files that are the current defaults (File_Access values
   --  designate them, not the current defaults later, A.10.3(22/1)))
   Current_In_Object  : File_Access := Standard_In'Access;
   Current_Out_Object : File_Access := Standard_Out'Access;
   Current_Err_Object : File_Access := Standard_Err'Access;

   ---------------------------------------------------------------------------
   --  File Management

   procedure Create (File : in out File_Type;
                     Mode : in File_Mode := Out_File;
                     Name : in String    := "";
                     Form : in String    := "") is
   begin
      Engine.Create (File.File, To_Engine (Mode), Name, Form, Engine.Latin_1);
   end Create;

   procedure Open   (File : in out File_Type;
                     Mode : in File_Mode;
                     Name : in String;
                     Form : in String := "") is
   begin
      Engine.Open (File.File, To_Engine (Mode), Name, Form, Engine.Latin_1);
   end Open;

   --  A file that is closed is no longer a current default file.
   procedure Forget (File : File_Type) is
   begin
      if Current_In.File = File.File then
         Current_In.File := Standard_In.File;
         Current_In_Object := Standard_In'Access;
      end if;
      if Current_Out.File = File.File then
         Current_Out.File := Standard_Out.File;
         Current_Out_Object := Standard_Out'Access;
      end if;
      if Current_Err.File = File.File then
         Current_Err.File := Standard_Err.File;
         Current_Err_Object := Standard_Err'Access;
      end if;
   end Forget;

   procedure Close  (File : in out File_Type) is
   begin
      Engine.Check_Open (File.File);
      Forget (File);
      Engine.Close (File.File);
   end Close;

   procedure Delete (File : in out File_Type) is
   begin
      Engine.Check_Open (File.File);
      Forget (File);
      Engine.Delete (File.File);
   end Delete;

   procedure Reset  (File : in out File_Type; Mode : in File_Mode) is
   begin
      Engine.Check_Open (File.File);
      --  (the current default input file other than In_File, or the
      --  current default output or error file In_File (A.10.2(4/2)))
      if (File.File = Current_In.File and then Mode /= In_File)
        or else ((File.File = Current_Out.File
                  or else File.File = Current_Err.File)
                 and then Mode = In_File)
      then
         raise Mode_Error;
      end if;
      Engine.Reset (File.File, To_Engine (Mode));
   end Reset;

   procedure Reset  (File : in out File_Type) is
   begin
      Engine.Reset (File.File);
   end Reset;

   function  Mode   (File : in File_Type) return File_Mode is
   begin
      return File_Mode'Val (Engine.File_Mode'Pos (Engine.Mode (File.File)));
   end Mode;

   function  Name   (File : in File_Type) return String is
   begin
      return Engine.Name (File.File);
   end Name;

   function  Form   (File : in File_Type) return String is
   begin
      return Engine.Form (File.File);
   end Form;

   function  Is_Open(File : in File_Type) return Boolean is
   begin
      return Engine.Is_Open (File.File);
   end Is_Open;

   ---------------------------------------------------------------------------
   --  Control of default input and output files

   procedure Set_Input (File : in File_Type) is
   begin
      Engine.Check_Input (File.File);
      Current_In.File := File.File;
      Current_In_Object := File'Unrestricted_Access;
   end Set_Input;

   procedure Set_Output(File : in File_Type) is
   begin
      Engine.Check_Output (File.File);
      Current_Out.File := File.File;
      Current_Out_Object := File'Unrestricted_Access;
   end Set_Output;

   procedure Set_Error (File : in File_Type) is
   begin
      Engine.Check_Output (File.File);
      Current_Err.File := File.File;
      Current_Err_Object := File'Unrestricted_Access;
   end Set_Error;

   function Standard_Input  return File_Type is
   begin
      return (File => Standard_In.File);
   end Standard_Input;

   function Standard_Output return File_Type is
   begin
      return (File => Standard_Out.File);
   end Standard_Output;

   function Standard_Error  return File_Type is
   begin
      return (File => Standard_Err.File);
   end Standard_Error;

   function Current_Input   return File_Type is
   begin
      return (File => Current_In.File);
   end Current_Input;

   function Current_Output  return File_Type is
   begin
      return (File => Current_Out.File);
   end Current_Output;

   function Current_Error   return File_Type is
   begin
      return (File => Current_Err.File);
   end Current_Error;

   function Standard_Input  return File_Access is
   begin
      return Standard_In'Access;
   end Standard_Input;

   function Standard_Output return File_Access is
   begin
      return Standard_Out'Access;
   end Standard_Output;

   function Standard_Error  return File_Access is
   begin
      return Standard_Err'Access;
   end Standard_Error;

   function Current_Input   return File_Access is
   begin
      return Current_In_Object;
   end Current_Input;

   function Current_Output  return File_Access is
   begin
      return Current_Out_Object;
   end Current_Output;

   function Current_Error   return File_Access is
   begin
      return Current_Err_Object;
   end Current_Error;

   ---------------------------------------------------------------------------
   --  Buffer control

   procedure Flush (File : in File_Type) is
   begin
      Engine.Flush (File.File);
   end Flush;

   procedure Flush is
   begin
      Engine.Flush (Current_Out.File);
   end Flush;

   ---------------------------------------------------------------------------
   --  Specification of line and page lengths

   procedure Set_Line_Length(File : in File_Type; To : in Count) is
   begin
      Engine.Set_Line_Length (File.File, Int (To));
   end Set_Line_Length;

   procedure Set_Line_Length(To   : in Count) is
   begin
      Engine.Set_Line_Length (Current_Out.File, Int (To));
   end Set_Line_Length;

   procedure Set_Page_Length(File : in File_Type; To : in Count) is
   begin
      Engine.Set_Page_Length (File.File, Int (To));
   end Set_Page_Length;

   procedure Set_Page_Length(To   : in Count) is
   begin
      Engine.Set_Page_Length (Current_Out.File, Int (To));
   end Set_Page_Length;

   function  Line_Length(File : in File_Type) return Count is
   begin
      return Count (Engine.Line_Length (File.File));
   end Line_Length;

   function  Line_Length return Count is
   begin
      return Count (Engine.Line_Length (Current_Out.File));
   end Line_Length;

   function  Page_Length(File : in File_Type) return Count is
   begin
      return Count (Engine.Page_Length (File.File));
   end Page_Length;

   function  Page_Length return Count is
   begin
      return Count (Engine.Page_Length (Current_Out.File));
   end Page_Length;

   ---------------------------------------------------------------------------
   --  Column, Line, and Page Control

   procedure New_Line   (File    : in File_Type;
                         Spacing : in Positive_Count := 1) is
   begin
      Engine.New_Line (File.File, Int (Spacing));
   end New_Line;

   procedure New_Line   (Spacing : in Positive_Count := 1) is
   begin
      Engine.New_Line (Current_Out.File, Int (Spacing));
   end New_Line;

   procedure Skip_Line  (File    : in File_Type;
                         Spacing : in Positive_Count := 1) is
   begin
      Engine.Skip_Line (File.File, Int (Spacing));
   end Skip_Line;

   procedure Skip_Line  (Spacing : in Positive_Count := 1) is
   begin
      Engine.Skip_Line (Current_In.File, Int (Spacing));
   end Skip_Line;

   function  End_Of_Line(File : in File_Type) return Boolean is
   begin
      return Engine.End_Of_Line (File.File);
   end End_Of_Line;

   function  End_Of_Line return Boolean is
   begin
      return Engine.End_Of_Line (Current_In.File);
   end End_Of_Line;

   procedure New_Page   (File : in File_Type) is
   begin
      Engine.New_Page (File.File);
   end New_Page;

   procedure New_Page is
   begin
      Engine.New_Page (Current_Out.File);
   end New_Page;

   procedure Skip_Page  (File : in File_Type) is
   begin
      Engine.Skip_Page (File.File);
   end Skip_Page;

   procedure Skip_Page is
   begin
      Engine.Skip_Page (Current_In.File);
   end Skip_Page;

   function  End_Of_Page(File : in File_Type) return Boolean is
   begin
      return Engine.End_Of_Page (File.File);
   end End_Of_Page;

   function  End_Of_Page return Boolean is
   begin
      return Engine.End_Of_Page (Current_In.File);
   end End_Of_Page;

   function  End_Of_File(File : in File_Type) return Boolean is
   begin
      return Engine.End_Of_File (File.File);
   end End_Of_File;

   function  End_Of_File return Boolean is
   begin
      return Engine.End_Of_File (Current_In.File);
   end End_Of_File;

   procedure Set_Col (File : in File_Type; To : in Positive_Count) is
   begin
      Engine.Set_Col (File.File, Int (To));
   end Set_Col;

   procedure Set_Col (To   : in Positive_Count) is
   begin
      Engine.Set_Col (Current_Out.File, Int (To));
   end Set_Col;

   procedure Set_Line(File : in File_Type; To : in Positive_Count) is
   begin
      Engine.Set_Line (File.File, Int (To));
   end Set_Line;

   procedure Set_Line(To   : in Positive_Count) is
   begin
      Engine.Set_Line (Current_Out.File, Int (To));
   end Set_Line;

   function Col (File : in File_Type) return Positive_Count is
   begin
      return Positive_Count (Engine.Col (File.File));
   end Col;

   function Col  return Positive_Count is
   begin
      return Positive_Count (Engine.Col (Current_Out.File));
   end Col;

   function Line(File : in File_Type) return Positive_Count is
   begin
      return Positive_Count (Engine.Line (File.File));
   end Line;

   function Line return Positive_Count is
   begin
      return Positive_Count (Engine.Line (Current_Out.File));
   end Line;

   function Page(File : in File_Type) return Positive_Count is
   begin
      return Positive_Count (Engine.Page (File.File));
   end Page;

   function Page return Positive_Count is
   begin
      return Positive_Count (Engine.Page (Current_Out.File));
   end Page;

   ---------------------------------------------------------------------------
   --  Character Input-Output

   procedure Get(File : in  File_Type; Item : out Character) is
   begin
      Item := Character'Val (Engine.Get (File.File));
   end Get;

   procedure Get(Item : out Character) is
   begin
      Get (Current_Input, Item);
   end Get;

   procedure Put(File : in  File_Type; Item : in Character) is
   begin
      Engine.Put (File.File, Character'Pos (Item));
   end Put;

   procedure Put(Item : in  Character) is
   begin
      Engine.Put (Current_Out.File, Character'Pos (Item));
   end Put;

   procedure Look_Ahead (File        : in  File_Type;
                         Item        : out Character;
                         End_Of_Line : out Boolean) is
      Code : Int;
   begin
      Engine.Look_Ahead (File.File, Code, End_Of_Line);
      Item := Character'Val (Code);
   end Look_Ahead;

   procedure Look_Ahead (Item        : out Character;
                         End_Of_Line : out Boolean) is
   begin
      Look_Ahead (Current_Input, Item, End_Of_Line);
   end Look_Ahead;

   procedure Get_Immediate(File      : in  File_Type;
                           Item      : out Character) is
   begin
      Item := Character'Val (Engine.Get_Immediate (File.File));
   end Get_Immediate;

   procedure Get_Immediate(Item      : out Character) is
   begin
      Get_Immediate (Current_Input, Item);
   end Get_Immediate;

   procedure Get_Immediate(File      : in  File_Type;
                           Item      : out Character;
                           Available : out Boolean) is
   begin
      Item := Character'Val (Engine.Get_Immediate (File.File));
      Available := True;
   end Get_Immediate;

   procedure Get_Immediate(Item      : out Character;
                           Available : out Boolean) is
   begin
      Get_Immediate (Current_Input, Item, Available);
   end Get_Immediate;

   ---------------------------------------------------------------------------
   --  String Input-Output

   procedure Get(File : in  File_Type; Item : out String) is
   begin
      Engine.Check_Input (File.File);
      for I in Item'Range loop
         Item (I) := Character'Val (Engine.Get (File.File));
      end loop;
   end Get;

   procedure Get(Item : out String) is
   begin
      Get (Current_Input, Item);
   end Get;

   procedure Put(File : in  File_Type; Item : in String) is
   begin
      Engine.Put_Text (File.File, Item, Item'Length);
   end Put;

   procedure Put(Item : in  String) is
   begin
      Engine.Put_Text (Current_Out.File, Item, Item'Length);
   end Put;

   procedure Get_Line(File : in  File_Type;
                      Item : out String;
                      Last : out Natural) is
      Ended : Boolean;
   begin
      Engine.Check_Input (File.File);
      if Item'Length = 0 then
         Last := Item'First - 1;
         return;
      end if;
      if Engine.Peek (File.File) = Engine.End_Of_Input then
         raise End_Error;
      end if;
      Engine.Get_Line_Part (File.File, Item, Last, Ended);
   end Get_Line;

   procedure Get_Line(Item : out String; Last : out Natural) is
   begin
      Get_Line (Current_Input, Item, Last);
   end Get_Line;

   function Get_Line(File : in  File_Type) return String is
      --  The rest of the line (from a buffer of Size characters).
      function Rest (Size : Positive) return String is
         Buffer : String (1 .. Size);
         Last   : Natural;
         Ended  : Boolean;
      begin
         Engine.Get_Line_Part (File.File, Buffer, Last, Ended);
         if Ended then
            return Buffer (1 .. Last);
         end if;
         return Buffer & Rest (Integer'Min (Size, Integer'Last / 2) * 2);
      end Rest;
   begin
      Engine.Check_Input (File.File);
      if Engine.Peek (File.File) = Engine.End_Of_Input then
         raise End_Error;
      end if;
      return Rest (256);
   end Get_Line;

   function Get_Line return String is
   begin
      return Get_Line (Current_Input);
   end Get_Line;

   procedure Put_Line(File : in  File_Type; Item : in String) is
   begin
      Engine.Put_Text (File.File, Item, Item'Length, Line_End => True);
   end Put_Line;

   procedure Put_Line(Item : in  String) is
   begin
      Engine.Put_Text
        (Current_Out.File, Item, Item'Length, Line_End => True);
   end Put_Line;

   ---------------------------------------------------------------------------
   --  Images filled to a width

   function Right (Image : String; Width : Natural) return String is
   begin
      if Image'Length >= Width then
         return Image;
      end if;
      return (1 .. Width - Image'Length => ' ') & Image;
   end Right;

   procedure Put_Image (File : File_Type; Image : String) is
   begin
      Numbers.Put_Image (File.File, Image, Image'Length);
   end Put_Image;

   --  The literal at the start of From: From (First .. Last).
   procedure Get_Literal
     (From  : String;
      Kind  : Numbers.Literal_Kind;
      First : out Positive;
      Last  : out Positive) is
   begin
      Numbers.Get_Literal (Numbers.To_Wide_Wide (From), Kind, First, Last);
   end Get_Literal;

   function Get_Literal
     (File  : File_Type;
      Kind  : Numbers.Literal_Kind;
      Width : Field := 0) return String is
   begin
      return Numbers.To_String (Numbers.Get_Literal (File.File, Kind, Width));
   end Get_Literal;

   ---------------------------------------------------------------------------
   --  Generic packages for Input-Output of Integer Types

   package body Integer_IO is

      procedure Convert (Literal : String; Item : out Num) is
         Value    : Uns;
         Negative : Boolean;
         V        : Int;
      begin
         Numbers.Integer_Value (Literal, Value, Negative);
         if Negative then
            if Value > 2 ** 63 then
               raise Data_Error;
            end if;
            V := (if Value = 2 ** 63 then Int'First else -Int (Value));
         else
            if Value > 2 ** 63 - 1 then
               raise Data_Error;
            end if;
            V := Int (Value);
         end if;
         if V < Int (Num'First) or else V > Int (Num'Last) then
            raise Data_Error;
         end if;
         Item := Num (V);
      end Convert;

      procedure Get(File  : in  File_Type;
                    Item  : out Num;
                    Width : in Field := 0) is
      begin
         Convert (Get_Literal (File, Numbers.Signed_Literal, Width), Item);
      end Get;

      procedure Get(Item  : out Num;
                    Width : in  Field := 0) is
      begin
         Get (Current_Input, Item, Width);
      end Get;

      procedure Put(File  : in File_Type;
                    Item  : in Num;
                    Width : in Field := Default_Width;
                    Base  : in Number_Base := Default_Base) is
      begin
         Put_Image
           (File, Right (Numbers.Integer_Image (Int (Item), Base), Width));
      end Put;

      procedure Put(Item  : in Num;
                    Width : in Field := Default_Width;
                    Base  : in Number_Base := Default_Base) is
      begin
         Put (Current_Output, Item, Width, Base);
      end Put;

      procedure Get(From : in  String;
                    Item : out Num;
                    Last : out Positive) is
         First : Positive;
         Stop  : Positive;
      begin
         Get_Literal (From, Numbers.Signed_Literal, First, Stop);
         Convert (From (First .. Stop), Item);
         Last := Stop;
      end Get;

      procedure Put(To   : out String;
                    Item : in Num;
                    Base : in Number_Base := Default_Base) is
      begin
         Numbers.Put_Image (To, Numbers.Integer_Image (Int (Item), Base));
      end Put;

   end Integer_IO;

   package body Modular_IO is

      procedure Convert (Literal : String; Item : out Num) is
         Value    : Uns;
         Negative : Boolean;
      begin
         Numbers.Integer_Value (Literal, Value, Negative);
         if (Negative and then Value /= 0) or else Value < Uns (Num'First)
           or else Value > Uns (Num'Last)
         then
            raise Data_Error;
         end if;
         Item := Num (Value);
      end Convert;

      procedure Get(File  : in  File_Type;
                    Item  : out Num;
                    Width : in Field := 0) is
      begin
         Convert (Get_Literal (File, Numbers.Unsigned_Literal, Width), Item);
      end Get;

      procedure Get(Item  : out Num;
                    Width : in  Field := 0) is
      begin
         Get (Current_Input, Item, Width);
      end Get;

      procedure Put(File  : in File_Type;
                    Item  : in Num;
                    Width : in Field := Default_Width;
                    Base  : in Number_Base := Default_Base) is
      begin
         Put_Image
           (File, Right (Numbers.Unsigned_Image (Uns (Item), Base), Width));
      end Put;

      procedure Put(Item  : in Num;
                    Width : in Field := Default_Width;
                    Base  : in Number_Base := Default_Base) is
      begin
         Put (Current_Output, Item, Width, Base);
      end Put;

      procedure Get(From : in  String;
                    Item : out Num;
                    Last : out Positive) is
         First : Positive;
         Stop  : Positive;
      begin
         Get_Literal (From, Numbers.Unsigned_Literal, First, Stop);
         Convert (From (First .. Stop), Item);
         Last := Stop;
      end Get;

      procedure Put(To   : out String;
                    Item : in Num;
                    Base : in Number_Base := Default_Base) is
      begin
         Numbers.Put_Image (To, Numbers.Unsigned_Image (Uns (Item), Base));
      end Put;

   end Modular_IO;

   ---------------------------------------------------------------------------
   --  Generic packages for Input-Output of Real Types

   package body Float_IO is

      --  The bounds of the base range and half the distance between the
      --  largest machine numbers (values are converted through Long_Float)
      Wide  : constant Boolean := Num'Base'Digits > Long_Float'Digits;
      Last  : constant Long_Float :=
         (if Wide then Long_Float'Last else Long_Float (Num'Base'Last));
      First : constant Long_Float :=
         (if Wide then Long_Float'First else Long_Float (Num'Base'First));
      Half  : constant Long_Float :=
         (if Wide then 0.0
          else (Last - Long_Float (Num'Base'Pred (Num'Base'Last))) / 2.0);

      procedure Convert (Literal : String; Item : out Num) is
         V : constant Long_Float := Numbers.Real_Value (Literal);
         X : Num'Base;
      begin
         if V > Last then  --  (rounded to Last if less than half beyond)
            if not (V - Last < Half) then
               raise Data_Error;
            end if;
            X := Num'Base (Last);
         elsif V < First then
            if not (First - V < Half) then
               raise Data_Error;
            end if;
            X := Num'Base (First);
         else
            X := Num'Base (V);
         end if;
         if X < Num'First or else X > Num'Last then
            raise Data_Error;
         end if;
         Item := X;
      end Convert;

      function Image
        (Item : Num; Fore, Aft, Exp : Natural) return String is
      begin
         return Numbers.Float_Image
                  (Long_Float (Item), Fore, Aft, Exp, 2 * Num'Digits);
      end Image;

      procedure Get(File  : in  File_Type;
                    Item  : out Num;
                    Width : in  Field := 0) is
      begin
         Convert (Get_Literal (File, Numbers.Real_Literal, Width), Item);
      end Get;

      procedure Get(Item  : out Num;
                    Width : in  Field := 0) is
      begin
         Get (Current_Input, Item, Width);
      end Get;

      procedure Put(File : in File_Type;
                    Item : in Num;
                    Fore : in Field := Default_Fore;
                    Aft  : in Field := Default_Aft;
                    Exp  : in Field := Default_Exp) is
      begin
         Put_Image (File, Image (Item, Fore, Aft, Exp));
      end Put;

      procedure Put(Item : in Num;
                    Fore : in Field := Default_Fore;
                    Aft  : in Field := Default_Aft;
                    Exp  : in Field := Default_Exp) is
      begin
         Put (Current_Output, Item, Fore, Aft, Exp);
      end Put;

      procedure Get(From : in String;
                    Item : out Num;
                    Last : out Positive) is
         First : Positive;
         Stop  : Positive;
      begin
         Get_Literal (From, Numbers.Real_Literal, First, Stop);
         Convert (From (First .. Stop), Item);
         Last := Stop;
      end Get;

      procedure Put(To   : out String;
                    Item : in Num;
                    Aft  : in Field := Default_Aft;
                    Exp  : in Field := Default_Exp) is
      begin
         Numbers.Put_Image (To, Image (Item, 0, Aft, Exp));
      end Put;

   end Float_IO;

   package body Fixed_IO is

      Small : constant Numbers.Small_Description :=
         Numbers.Describe (Long_Float (Num'Small));

      --  The value Mantissa * Num'Small (exactly, using only the fixed
      --  point operations of Num, by parts of 21 bits).
      function Mantissa (Item : Num'Base) return Int is
        (Int (Item / Num'Base'(Num'Small)));

      function Value (Mantissa : Int) return Num'Base is
         S     : constant Num'Base := Num'Small;
         High  : constant Int := Mantissa / 2 ** 42;
         Rest  : constant Int := Mantissa rem 2 ** 42;
         Mid   : constant Int := Rest / 2 ** 21;
         Low   : constant Int := Rest rem 2 ** 21;
      begin
         return ((S * Integer (High)) * 2 ** 21 + S * Integer (Mid))
                * 2 ** 21 + S * Integer (Low);
      end Value;

      First_Mantissa : constant Int := Mantissa (Num'First);
      Last_Mantissa  : constant Int := Mantissa (Num'Last);

      procedure Convert (Literal : String; Item : out Num) is
         M : constant Int :=
            Numbers.Fixed_Value (Literal, Small, Truncate => False);
      begin
         if M < First_Mantissa or else M > Last_Mantissa then
            raise Data_Error;
         end if;
         Item := Value (M);
      end Convert;

      function Image
        (Item : Num; Fore, Aft, Exp : Natural) return String is
      begin
         return Numbers.Fixed_Image (Mantissa (Item), Small, Fore, Aft, Exp);
      end Image;

      procedure Get(File  : in  File_Type;
                    Item  : out Num;
                    Width : in  Field := 0) is
      begin
         Convert (Get_Literal (File, Numbers.Real_Literal, Width), Item);
      end Get;

      procedure Get(Item  : out Num;
                    Width : in  Field := 0) is
      begin
         Get (Current_Input, Item, Width);
      end Get;

      procedure Put(File : in File_Type;
                    Item : in Num;
                    Fore : in Field := Default_Fore;
                    Aft  : in Field := Default_Aft;
                    Exp  : in Field := Default_Exp) is
      begin
         Put_Image (File, Image (Item, Fore, Aft, Exp));
      end Put;

      procedure Put(Item : in Num;
                    Fore : in Field := Default_Fore;
                    Aft  : in Field := Default_Aft;
                    Exp  : in Field := Default_Exp) is
      begin
         Put (Current_Output, Item, Fore, Aft, Exp);
      end Put;

      procedure Get(From : in  String;
                    Item : out Num;
                    Last : out Positive) is
         First : Positive;
         Stop  : Positive;
      begin
         Get_Literal (From, Numbers.Real_Literal, First, Stop);
         Convert (From (First .. Stop), Item);
         Last := Stop;
      end Get;

      procedure Put(To   : out String;
                    Item : in Num;
                    Aft  : in Field := Default_Aft;
                    Exp  : in Field := Default_Exp) is
      begin
         Numbers.Put_Image (To, Image (Item, 0, Aft, Exp));
      end Put;

   end Fixed_IO;

   package body Decimal_IO is

      Small : constant Numbers.Small_Description :=
         Numbers.Decimal_Small (Num'Scale);

      function Mantissa (Item : Num'Base) return Int is
        (Int (Item / Num'Base'(Num'Small)));

      function Value (Mantissa : Int) return Num'Base is
         S     : constant Num'Base := Num'Small;
         High  : constant Int := Mantissa / 2 ** 42;
         Rest  : constant Int := Mantissa rem 2 ** 42;
         Mid   : constant Int := Rest / 2 ** 21;
         Low   : constant Int := Rest rem 2 ** 21;
      begin
         return ((S * Integer (High)) * 2 ** 21 + S * Integer (Mid))
                * 2 ** 21 + S * Integer (Low);
      end Value;

      First_Mantissa : constant Int := Mantissa (Num'First);
      Last_Mantissa  : constant Int := Mantissa (Num'Last);

      procedure Convert (Literal : String; Item : out Num) is
         M : constant Int :=
            Numbers.Fixed_Value (Literal, Small, Truncate => True);
      begin
         if M < First_Mantissa or else M > Last_Mantissa then
            raise Data_Error;
         end if;
         Item := Value (M);
      end Convert;

      function Image
        (Item : Num; Fore, Aft, Exp : Natural) return String is
      begin
         return Numbers.Fixed_Image (Mantissa (Item), Small, Fore, Aft, Exp);
      end Image;

      procedure Get(File  : in  File_Type;
                    Item  : out Num;
                    Width : in  Field := 0) is
      begin
         Convert (Get_Literal (File, Numbers.Real_Literal, Width), Item);
      end Get;

      procedure Get(Item  : out Num;
                    Width : in  Field := 0) is
      begin
         Get (Current_Input, Item, Width);
      end Get;

      procedure Put(File : in File_Type;
                    Item : in Num;
                    Fore : in Field := Default_Fore;
                    Aft  : in Field := Default_Aft;
                    Exp  : in Field := Default_Exp) is
      begin
         Put_Image (File, Image (Item, Fore, Aft, Exp));
      end Put;

      procedure Put(Item : in Num;
                    Fore : in Field := Default_Fore;
                    Aft  : in Field := Default_Aft;
                    Exp  : in Field := Default_Exp) is
      begin
         Put (Current_Output, Item, Fore, Aft, Exp);
      end Put;

      procedure Get(From : in  String;
                    Item : out Num;
                    Last : out Positive) is
         First : Positive;
         Stop  : Positive;
      begin
         Get_Literal (From, Numbers.Real_Literal, First, Stop);
         Convert (From (First .. Stop), Item);
         Last := Stop;
      end Get;

      procedure Put(To   : out String;
                    Item : in Num;
                    Aft  : in Field := Default_Aft;
                    Exp  : in Field := Default_Exp) is
      begin
         Numbers.Put_Image (To, Image (Item, 0, Aft, Exp));
      end Put;

   end Decimal_IO;

   ---------------------------------------------------------------------------
   --  Generic package for Input-Output of Enumeration Types

   package body Enumeration_IO is

      procedure Convert (Literal : String; Item : out Enum) is
         V : Enum'Base;
      begin
         begin
            V := Enum'Base'Value (Literal);
         exception
            when Constraint_Error =>
               raise Data_Error;
         end;
         if V < Enum'First or else V > Enum'Last then
            raise Data_Error;
         end if;
         Item := V;
      end Convert;

      procedure Get(File : in  File_Type;
                    Item : out Enum) is
      begin
         Convert (Get_Literal (File, Numbers.Enumeration_Literal), Item);
      end Get;

      procedure Get(Item : out Enum) is
      begin
         Get (Current_Input, Item);
      end Get;

      procedure Put(File  : in File_Type;
                    Item  : in Enum;
                    Width : in Field    := Default_Width;
                    Set   : in Type_Set := Default_Setting) is
      begin
         Put_Image
           (File,
            Numbers.Enumeration_Image
              (Enum'Image (Item), Width, Set = Lower_Case));
      end Put;

      procedure Put(Item  : in Enum;
                    Width : in Field    := Default_Width;
                    Set   : in Type_Set := Default_Setting) is
      begin
         Put (Current_Output, Item, Width, Set);
      end Put;

      procedure Get(From : in  String;
                    Item : out Enum;
                    Last : out Positive) is
         First : Positive;
         Stop  : Positive;
      begin
         Get_Literal (From, Numbers.Enumeration_Literal, First, Stop);
         Convert (From (First .. Stop), Item);
         Last := Stop;
      end Get;

      procedure Put(To   : out String;
                    Item : in  Enum;
                    Set  : in  Type_Set := Default_Setting) is
      begin
         Numbers.Put_Image
           (To,
            Numbers.Enumeration_Image (Enum'Image (Item), 0, Set = Lower_Case),
            Left => True);
      end Put;

   end Enumeration_IO;

   ---------------------------------------------------------------------------
   --  File names that are wide strings (UTF-8 encoded externally)

   function To_Wide (Item : Wide_Wide_String) return Wide_String is
      Result : Wide_String (1 .. Item'Length);
      Code   : Natural;
   begin
      for I in Item'Range loop
         Code := Wide_Wide_Character'Pos (Item (I));
         Result (I - Item'First + 1) :=
            Wide_Character'Val (if Code > 16#FFFF# then 16#FFFD# else Code);
      end loop;
      return Result;
   end To_Wide;

   package body Wide_File_Names is

      procedure Create (File : in out File_Type;
                        Mode : in File_Mode := Out_File;
                        Name : in Wide_String := "";
                        Form : in Wide_String := "") is
      begin
         Text_IO.Create
           (File, Mode, Engine.Encode (Numbers.To_Wide_Wide (Name)),
            Engine.Encode (Numbers.To_Wide_Wide (Form)));
      end Create;

      procedure Open   (File : in out File_Type;
                        Mode : in File_Mode;
                        Name : in Wide_String;
                        Form : in Wide_String := "") is
      begin
         Text_IO.Open
           (File, Mode, Engine.Encode (Numbers.To_Wide_Wide (Name)),
            Engine.Encode (Numbers.To_Wide_Wide (Form)));
      end Open;

      function Name    (File : in File_Type) return Wide_String is
      begin
         return To_Wide (Engine.Decode (Text_IO.Name (File)));
      end Name;

      function Form    (File : in File_Type) return Wide_String is
      begin
         return To_Wide (Engine.Decode (Text_IO.Form (File)));
      end Form;

   end Wide_File_Names;

   package body Wide_Wide_File_Names is

      procedure Create (File : in out File_Type;
                        Mode : in File_Mode := Out_File;
                        Name : in Wide_Wide_String := "";
                        Form : in Wide_Wide_String := "") is
      begin
         Text_IO.Create
           (File, Mode, Engine.Encode (Name), Engine.Encode (Form));
      end Create;

      procedure Open   (File : in out File_Type;
                        Mode : in File_Mode;
                        Name : in Wide_Wide_String;
                        Form : in Wide_Wide_String := "") is
      begin
         Text_IO.Open
           (File, Mode, Engine.Encode (Name), Engine.Encode (Form));
      end Open;

      function Name    (File : in File_Type) return Wide_Wide_String is
      begin
         return Engine.Decode (Text_IO.Name (File));
      end Name;

      function Form    (File : in File_Type) return Wide_Wide_String is
      begin
         return Engine.Decode (Text_IO.Form (File));
      end Form;

   end Wide_Wide_File_Names;

end Ada.Text_IO;
