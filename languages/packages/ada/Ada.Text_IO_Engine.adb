--  Ada.Text_IO_Engine.adb
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

with Ada.Finalization;
with Ada.IO_Exceptions;
with Ada.Unchecked_Deallocation;

package body Ada.Text_IO_Engine is

   use type Ada.Streams.Stream_Element_Offset;

   Status_Error : exception renames IO_Exceptions.Status_Error;
   Mode_Error   : exception renames IO_Exceptions.Mode_Error;
   Use_Error    : exception renames IO_Exceptions.Use_Error;
   End_Error    : exception renames IO_Exceptions.End_Error;
   Layout_Error : exception renames IO_Exceptions.Layout_Error;

   Tab : constant Int := 9;

   procedure Free is new Ada.Unchecked_Deallocation (Text_File, File_Access);
   procedure Free is new Ada.Unchecked_Deallocation (String, String_Access);
   procedure Free is
      new Ada.Unchecked_Deallocation (Input_Buffer, Buffer_Access);

   Open_Files            : File_Access;  --  (those that are not standard)
   Standard_Files        : File_Access;  --  (the standard files)
   Standard_Input_Buffer : Buffer_Access;

   ---------------------------------------------------------------------------
   --  The input buffer

   --  Whether at least Need bytes are available in File's buffer (reading
   --  more as needed).
   function Fill (File : File_Access; Need : Positive) return Boolean is
      B     : Input_Buffer renames File.Buffer.all;
      Count : Int;
   begin
      while B.Last - B.First + 1 < Need loop
         if B.At_End then
            return False;
         end if;
         if B.First > 1 then
            declare
               Available : constant Natural := B.Last - B.First + 1;
            begin
               B.Data (1 .. Available) := B.Data (B.First .. B.Last);
               B.First := 1;
               B.Last := Available;
            end;
         end if;
         Count := Native_IO.Read
                    (File.Handle, B.Data (B.Last + 1)'Address,
                     Int (Buffer_Size - B.Last));
         if Count = 0 then
            B.At_End := True;
            return False;
         end if;
         B.Last := B.Last + Natural (Count);
      end loop;
      return True;
   end Fill;

   --  The byte at Offset from the next one (-1 at the end of the file).
   function Peek_Byte (File : File_Access; Offset : Natural := 0) return Int is
   begin
      if Fill (File, Offset + 1) then
         return Character'Pos (File.Buffer.Data (File.Buffer.First + Offset));
      else
         return -1;
      end if;
   end Peek_Byte;

   procedure Consume (File : File_Access; Length : Natural) is
   begin
      File.Buffer.First := File.Buffer.First + Length;
   end Consume;

   --  The next character (its code point and the length of its encoding):
   --  End_Of_Input at the end of the file; an invalid UTF-8 sequence is read
   --  as the code point of its first byte.
   procedure Next_Code
     (File : File_Access; Code : out Int; Length : out Natural)
   is
      First : constant Int := Peek_Byte (File);
      Count : Natural;
      Min   : Int;
      Byte  : Int;
   begin
      Code := First;
      Length := 1;
      if First < 0 then
         Code := End_Of_Input;
         Length := 0;
         return;
      elsif File.Encoding = Latin_1 or else First < 16#80# then
         return;
      elsif First in 16#C2# .. 16#DF# then
         Count := 2;
         Code := First - 16#C0#;
         Min := 16#80#;
      elsif First in 16#E0# .. 16#EF# then
         Count := 3;
         Code := First - 16#E0#;
         Min := 16#800#;
      elsif First in 16#F0# .. 16#F4# then
         Count := 4;
         Code := First - 16#F0#;
         Min := 16#1_0000#;
      else
         return;
      end if;
      for K in 1 .. Count - 1 loop
         Byte := Peek_Byte (File, K);
         if Byte not in 16#80# .. 16#BF# then
            Code := First;
            return;
         end if;
         Code := Code * 64 + (Byte - 16#80#);
      end loop;
      if Code < Min or else Code > 16#10_FFFF# then
         Code := First;
         return;
      end if;
      Length := Count;
   end Next_Code;

   --  After the reading of a line terminator: the page terminator that
   --  follows it is read too.
   procedure Line_Terminator_Read (File : File_Access) is
      Byte : Int := 0;
   begin
      File.Col := 1;
      if File.Regular then
         Byte := Peek_Byte (File);
      end if;
      if Byte = Form_Feed then
         Consume (File, 1);
         File.Page := File.Page + 1;
         File.Line := 1;
      elsif File.Regular and then Byte = -1 then
         --  (the file terminator: preceded by a page terminator (implied),
         --  skipped with the line terminator (A.10(7), A.10.5(19)))
         File.Page := File.Page + 1;
         File.Line := 1;
      else
         File.Line := File.Line + 1;
      end if;
   end Line_Terminator_Read;

   procedure Page_Terminator_Read (File : File_Access) is
   begin
      File.Page := File.Page + 1;
      File.Line := 1;
      File.Col := 1;
   end Page_Terminator_Read;

   ---------------------------------------------------------------------------
   --  Output

   procedure Write (File : File_Access; Text : String; Line_Feeds : Int := 0)
   is
   begin
      Native_IO.Write (File.Handle, Text, Line_Feeds);
   end Write;

   procedure Write_Spaces (File : File_Access; Count : Int) is
      Spaces : constant String (1 .. 256) := (others => ' ');
      Rest   : Int := Count;
   begin
      while Rest > 0 loop
         declare
            Part : constant Natural := Natural (Int'Min (Rest, Spaces'Length));
         begin
            Write (File, Spaces (1 .. Part));
            Rest := Rest - Int (Part);
         end;
      end loop;
   end Write_Spaces;

   --  The line and page terminators written when an output file is closed
   --  or reset (unless the current page is already terminated; the page
   --  terminator before the file terminator is implied).
   procedure Terminate_Output (File : File_Access) is
   begin
      if File.Col /= 1 or else (File.Line = 1 and then File.Page = 1) then
         New_Line (File, 1);
      end if;
   end Terminate_Output;

   ---------------------------------------------------------------------------
   --  UTF-8

   procedure Encode (Code : Int; Into : in out String; Last : in out Natural)
   is
      procedure Add (Byte : Int) is
      begin
         Last := Last + 1;
         Into (Last) := Character'Val (Byte);
      end Add;
   begin
      if Code < 16#80# then
         Add (Code);
      elsif Code < 16#800# then
         Add (16#C0# + Code / 64);
         Add (16#80# + Code mod 64);
      elsif Code < 16#1_0000# then
         Add (16#E0# + Code / 4096);
         Add (16#80# + (Code / 64) mod 64);
         Add (16#80# + Code mod 64);
      else
         Add (16#F0# + (Code / 262144) mod 8);
         Add (16#80# + (Code / 4096) mod 64);
         Add (16#80# + (Code / 64) mod 64);
         Add (16#80# + Code mod 64);
      end if;
   end Encode;

   function Encode (Text : Wide_Wide_String) return String is
   begin
      return Native_IO.Encode (Text);
   end Encode;

   function Decode (Text : String) return Wide_Wide_String is
   begin
      return Native_IO.Decode (Text);
   end Decode;

   ---------------------------------------------------------------------------
   --  File management

   procedure Link (File : File_Access) is
   begin
      File.Next := Open_Files;
      Open_Files := File;
   end Link;

   procedure Unlink (File : File_Access) is
      Previous : File_Access;
      Current  : File_Access := Open_Files;
   begin
      while Current /= null loop
         if Current = File then
            if Previous = null then
               Open_Files := Current.Next;
            else
               Previous.Next := Current.Next;
            end if;
            return;
         end if;
         Previous := Current;
         Current := Current.Next;
      end loop;
   end Unlink;

   procedure Check_Open (File : File_Access) is
   begin
      if File = null or else not File.Is_Open then
         raise Status_Error;
      end if;
   end Check_Open;

   procedure Check_Input (File : File_Access) is
   begin
      Check_Open (File);
      if File.Mode /= In_File then
         raise Mode_Error;
      end if;
   end Check_Input;

   procedure Check_Output (File : File_Access) is
   begin
      Check_Open (File);
      if File.Mode = In_File then
         raise Mode_Error;
      end if;
   end Check_Output;

   procedure Initialize
     (File      : in out File_Access;
      Handle    : Int;
      Mode      : File_Mode;
      Form      : String;
      Encoding  : Encoding_Kind;
      Temporary : Boolean) is
   begin
      File := new Text_File;
      File.Handle := Handle;
      File.Mode := Mode;
      File.Encoding := Encoding;
      File.Temporary := Temporary;
      File.Form := new String'(Form);
      File.Name := new String'(Native_IO.Name (Handle));
      File.Buffer := new Input_Buffer;
      File.Stream.File := File;
      File.Is_Open := True;
      Link (File);
   end Initialize;

   procedure Create
     (File     : in out File_Access;
      Mode     : File_Mode;
      Name     : String;
      Form     : String;
      Encoding : Encoding_Kind)
   is
      Handle : Int;
   begin
      if File /= null and then File.Is_Open then
         raise Status_Error;
      end if;
      if Name = "" then
         Handle := Native_IO.Open_Temporary;
      else
         Handle := Native_IO.Open
                     (Name,
                      (if Mode = In_File then Native_IO.Read_Write_Create
                       else Native_IO.Write));
      end if;
      Initialize (File, Handle, Mode, Form, Encoding, Name = "");
   end Create;

   procedure Open
     (File     : in out File_Access;
      Mode     : File_Mode;
      Name     : String;
      Form     : String;
      Encoding : Encoding_Kind)
   is
      Handle : Int;
   begin
      if File /= null and then File.Is_Open then
         raise Status_Error;
      end if;
      Handle := Native_IO.Open
                  (Name,
                   (case Mode is
                       when In_File     => Native_IO.Read,
                       when Out_File    => Native_IO.Write,
                       when Append_File => Native_IO.Append),
                   Must_Exist => True);
      Initialize (File, Handle, Mode, Form, Encoding, False);
   end Open;

   --  The file is closed (its native file already closed).
   procedure Release (File : in out File_Access) is
   begin
      File.Is_Open := False;
      if File.Is_Standard then
         File := null;
         return;
      end if;
      Unlink (File);
      Free (File.Name);
      Free (File.Form);
      Free (File.Buffer);
      Free (File);
   end Release;

   procedure Close (File : in out File_Access) is
   begin
      Check_Open (File);
      begin
         if File.Mode /= In_File and then not File.Is_Standard then
            Terminate_Output (File);
         end if;
      exception
         when others =>
            Native_IO.Close (File.Handle);
            Release (File);
            raise;
      end;
      begin
         Native_IO.Close (File.Handle);
      exception
         when others =>
            Release (File);
            raise;
      end;
      Release (File);
   end Close;

   procedure Delete (File : in out File_Access) is
   begin
      Check_Open (File);
      if File.Is_Standard then
         raise Use_Error;
      end if;
      declare
         Name      : constant String := File.Name.all;
         Temporary : constant Boolean := File.Temporary;
      begin
         Close (File);
         if not Temporary then
            Native_IO.Delete (Name);
         end if;
      end;
   end Delete;

   procedure Reset (File : File_Access; Mode : File_Mode) is
   begin
      Check_Open (File);
      if File.Is_Standard then
         raise Use_Error;
      end if;
      if File.Mode /= In_File then
         Terminate_Output (File);
      end if;
      Native_IO.Reopen
        (File.Handle,
         (case Mode is
             when In_File     => Native_IO.Read,
             when Out_File    => Native_IO.Write,
             when Append_File => Native_IO.Append));
      File.Mode := Mode;
      File.Col := 1;
      File.Line := 1;
      File.Page := 1;
      File.Line_Length := 0;
      File.Page_Length := 0;
      File.Buffer.First := 1;
      File.Buffer.Last := 0;
      File.Buffer.At_End := False;
   exception
      when IO_Exceptions.Use_Error | IO_Exceptions.Device_Error |
           IO_Exceptions.Name_Error =>
         if File.Is_Open and then not File.Is_Standard then
            declare
               Closed : File_Access := File;
            begin
               Release (Closed);  --  (the native file is closed)
            end;
         end if;
         raise Use_Error;
   end Reset;

   procedure Reset (File : File_Access) is
   begin
      Check_Open (File);
      Reset (File, File.Mode);
   end Reset;

   function Mode (File : File_Access) return File_Mode is
   begin
      Check_Open (File);
      return File.Mode;
   end Mode;

   function Name (File : File_Access) return String is
   begin
      Check_Open (File);
      return File.Name.all;
   end Name;

   function Form (File : File_Access) return String is
   begin
      Check_Open (File);
      return File.Form.all;
   end Form;

   function Is_Open (File : File_Access) return Boolean is
   begin
      return File /= null and then File.Is_Open;
   end Is_Open;

   function Standard_File (Handle : Int; Encoding : Encoding_Kind)
      return File_Access
   is
      File : constant File_Access := new Text_File;
   begin
      File.Handle := Handle;
      File.Is_Standard := True;
      File.Mode := (if Handle = Native_IO.Standard_Input_Handle then In_File
                    else Out_File);
      File.Encoding := Encoding;
      File.Regular := False;
      File.Form := new String'("");
      File.Name := new String'(Native_IO.Name (Handle));
      if Handle = Native_IO.Standard_Input_Handle then
         if Standard_Input_Buffer = null then
            Standard_Input_Buffer := new Input_Buffer;
         end if;
         File.Buffer := Standard_Input_Buffer;
      else
         File.Buffer := new Input_Buffer;
      end if;
      File.Stream.File := File;
      File.Is_Open := True;
      File.Next := Standard_Files;
      Standard_Files := File;
      return File;
   end Standard_File;

   procedure Flush (File : File_Access) is
   begin
      Check_Output (File);
      Native_IO.Flush (File.Handle);
   end Flush;

   ---------------------------------------------------------------------------
   --  Line and page lengths

   procedure Set_Line_Length (File : File_Access; To : Int) is
   begin
      Check_Output (File);
      File.Line_Length := To;
   end Set_Line_Length;

   procedure Set_Page_Length (File : File_Access; To : Int) is
   begin
      Check_Output (File);
      File.Page_Length := To;
   end Set_Page_Length;

   function Line_Length (File : File_Access) return Int is
   begin
      Check_Output (File);
      return File.Line_Length;
   end Line_Length;

   function Page_Length (File : File_Access) return Int is
   begin
      Check_Output (File);
      return File.Page_Length;
   end Page_Length;

   ---------------------------------------------------------------------------
   --  Columns, lines and pages

   procedure New_Line (File : File_Access; Spacing : Int) is
   begin
      Check_Output (File);
      if File.Page_Length = 0 then
         Write (File, "", Line_Feeds => Spacing);
         File.Line := File.Line + Spacing;
         File.Col := 1;
      else
         for K in 1 .. Spacing loop
            if File.Line >= File.Page_Length then
               Write (File, (1 => ASCII.LF, 2 => ASCII.FF));
               File.Page := File.Page + 1;
               File.Line := 1;
            else
               Write (File, "", Line_Feeds => 1);
               File.Line := File.Line + 1;
            end if;
            File.Col := 1;
         end loop;
      end if;
   end New_Line;

   procedure Skip_Line (File : File_Access; Spacing : Int) is
      Code   : Int;
      Length : Natural;
   begin
      Check_Input (File);
      for K in 1 .. Spacing loop
         loop
            Next_Code (File, Code, Length);
            if Code = End_Of_Input then
               raise End_Error;
            end if;
            Consume (File, Length);
            exit when Code = Line_Feed;
         end loop;
         Line_Terminator_Read (File);
      end loop;
   end Skip_Line;

   function End_Of_Line (File : File_Access) return Boolean is
      Byte : Int;
   begin
      Check_Input (File);
      Byte := Peek_Byte (File);
      return Byte = -1 or else Byte = Line_Feed;
   end End_Of_Line;

   procedure New_Page (File : File_Access) is
   begin
      Check_Output (File);
      if File.Col /= 1 or else File.Line = 1 then
         Write (File, (1 => ASCII.LF, 2 => ASCII.FF));
      else
         Write (File, (1 => ASCII.FF));
      end if;
      Page_Terminator_Read (File);
   end New_Page;

   procedure Skip_Page (File : File_Access) is
      Byte : Int;
   begin
      Check_Input (File);
      if Peek_Byte (File) = -1 then
         raise End_Error;
      end if;
      loop
         Byte := Peek_Byte (File);
         exit when Byte = -1;
         Consume (File, 1);
         if Byte = Line_Feed and then File.Regular
           and then Peek_Byte (File) = Form_Feed
         then
            Consume (File, 1);
            exit;
         end if;
         exit when Byte = Form_Feed and then File.Regular;
      end loop;
      Page_Terminator_Read (File);
   end Skip_Page;

   function End_Of_Page (File : File_Access) return Boolean is
      Byte : Int;
   begin
      Check_Input (File);
      Byte := Peek_Byte (File);
      if Byte = -1 then
         return True;
      elsif Byte /= Line_Feed then
         return False;
      end if;
      Byte := Peek_Byte (File, 1);
      return Byte = -1 or else (Byte = Form_Feed and then File.Regular);
   end End_Of_Page;

   function End_Of_File (File : File_Access) return Boolean is
      Byte : Int;
   begin
      Check_Input (File);
      Byte := Peek_Byte (File);
      if Byte = -1 then
         return True;
      elsif Byte /= Line_Feed then
         return False;
      end if;
      Byte := Peek_Byte (File, 1);
      if Byte = -1 then
         return True;
      elsif Byte /= Form_Feed or else not File.Regular then
         return False;
      end if;
      return Peek_Byte (File, 2) = -1;
   end End_Of_File;

   procedure Set_Col (File : File_Access; To : Int) is
      Code   : Int;
      Length : Natural;
   begin
      Check_Open (File);
      if File.Mode /= In_File then
         if File.Line_Length /= 0 and then To > File.Line_Length then
            raise Layout_Error;
         end if;
         if To > File.Col then
            Write_Spaces (File, To - File.Col);
         elsif To < File.Col then
            New_Line (File, 1);
            Write_Spaces (File, To - 1);
         end if;
         File.Col := To;
      elsif To /= File.Col then
         loop
            Next_Code (File, Code, Length);
            if Code = End_Of_Input then
               raise End_Error;
            elsif Code = Line_Feed then
               Consume (File, Length);
               File.Col := 1;
               File.Line := File.Line + 1;
            elsif Code = Form_Feed and then File.Regular then
               Consume (File, Length);
               Page_Terminator_Read (File);
            elsif File.Col = To then
               exit;
            else
               Consume (File, Length);
               File.Col := File.Col + 1;
            end if;
         end loop;
      end if;
   end Set_Col;

   procedure Set_Line (File : File_Access; To : Int) is
   begin
      Check_Open (File);
      if File.Mode /= In_File then
         if File.Page_Length /= 0 and then To > File.Page_Length then
            raise Layout_Error;
         end if;
         if To > File.Line then
            New_Line (File, To - File.Line);
         elsif To < File.Line then
            New_Page (File);
            if To > 1 then
               New_Line (File, To - 1);
            end if;
         end if;
      else
         while File.Line /= To loop
            Skip_Line (File, 1);
         end loop;
      end if;
   end Set_Line;

   function Col (File : File_Access) return Int is
   begin
      Check_Open (File);
      if File.Col > Count_Last then
         raise Layout_Error;
      end if;
      return File.Col;
   end Col;

   function Line (File : File_Access) return Int is
   begin
      Check_Open (File);
      if File.Line > Count_Last then
         raise Layout_Error;
      end if;
      return File.Line;
   end Line;

   function Page (File : File_Access) return Int is
   begin
      Check_Open (File);
      if File.Page > Count_Last then
         raise Layout_Error;
      end if;
      return File.Page;
   end Page;

   ---------------------------------------------------------------------------
   --  Characters

   function Get (File : File_Access) return Int is
      Code   : Int;
      Length : Natural;
   begin
      Check_Input (File);
      loop
         Next_Code (File, Code, Length);
         if Code = End_Of_Input then
            raise End_Error;
         end if;
         Consume (File, Length);
         if Code = Line_Feed then
            Line_Terminator_Read (File);
         elsif Code = Form_Feed and then File.Regular then
            Page_Terminator_Read (File);
         else
            File.Col := File.Col + 1;
            return Code;
         end if;
      end loop;
   end Get;

   procedure Put (File : File_Access; Code : Int) is
      Text : String (1 .. 4);
      Last : Natural := 0;
   begin
      Check_Output (File);
      if File.Line_Length /= 0 and then File.Col > File.Line_Length then
         New_Line (File, 1);
      end if;
      if File.Encoding = Latin_1 then
         Text (1) := Character'Val (Code);
         Last := 1;
      else
         Encode (Code, Text, Last);
      end if;
      Write (File, Text (1 .. Last));
      File.Col := File.Col + 1;
   end Put;

   procedure Look_Ahead
     (File        : File_Access;
      Code        : out Int;
      End_Of_Line : out Boolean)
   is
      Length : Natural;
   begin
      Check_Input (File);
      Next_Code (File, Code, Length);
      End_Of_Line := Code = End_Of_Input or else Code = Line_Feed
        or else (Code = Form_Feed and then File.Regular);
      if End_Of_Line then
         Code := 0;
      end if;
   end Look_Ahead;

   function Get_Immediate (File : File_Access) return Int is
      Code   : Int;
      Length : Natural;
   begin
      Check_Input (File);
      Next_Code (File, Code, Length);
      if Code = End_Of_Input then
         raise End_Error;
      end if;
      Consume (File, Length);
      return Code;
   end Get_Immediate;

   --  The index after Count characters of Text from From.
   function Advance
     (File : File_Access; Text : String; From : Positive; Count : Natural)
      return Positive
   is
      I : Positive := From;
   begin
      if File.Encoding = Latin_1 then
         return From + Count;
      end if;
      for K in 1 .. Count loop
         I := I + 1;
         while I <= Text'Last
           and then Character'Pos (Text (I)) in 16#80# .. 16#BF#
         loop
            I := I + 1;
         end loop;
      end loop;
      return I;
   end Advance;

   procedure Put_Text
     (File     : File_Access;
      Text     : String;
      Count    : Natural;
      Line_End : Boolean := False) is
   begin
      Check_Output (File);
      if File.Line_Length = 0 then
         if Line_End and then File.Page_Length = 0 then
            Write (File, Text, Line_Feeds => 1);
            File.Line := File.Line + 1;
            File.Col := 1;
            return;
         end if;
         if Text'Length > 0 then
            Write (File, Text);
         end if;
         File.Col := File.Col + Int (Count);
      else
         declare
            I    : Positive := Text'First;
            J    : Positive;
            Rest : Int := Int (Count);
            Part : Int;
         begin
            while Rest > 0 loop
               if File.Col > File.Line_Length then
                  New_Line (File, 1);
               end if;
               Part := Int'Min (Rest, File.Line_Length - File.Col + 1);
               J := Advance (File, Text, I, Natural (Part));
               Write (File, Text (I .. J - 1));
               File.Col := File.Col + Part;
               Rest := Rest - Part;
               I := J;
            end loop;
         end;
      end if;
      if Line_End then
         New_Line (File, 1);
      end if;
   end Put_Text;

   procedure Put_Item (File : File_Access; Text : String; Count : Natural) is
   begin
      Check_Output (File);
      if File.Line_Length /= 0 then
         if Int (Count) > File.Line_Length then
            raise Layout_Error;
         elsif File.Col + Int (Count) - 1 > File.Line_Length then
            New_Line (File, 1);
         end if;
      end if;
      Write (File, Text);
      File.Col := File.Col + Int (Count);
   end Put_Item;

   procedure Get_Line_Part
     (File  : File_Access;
      Item  : out String;
      Last  : out Natural;
      Ended : out Boolean)
   is
      B : Input_Buffer renames File.Buffer.all;
   begin
      Check_Input (File);
      Last := Item'First - 1;
      Ended := False;
      while Last < Item'Last loop
         if B.First > B.Last and then not Fill (File, 1) then
            Ended := True;
            return;
         end if;
         declare
            Stop  : constant Natural :=
               Natural'Min (B.Last, B.First + (Item'Last - Last) - 1);
            Count : Natural;
         begin
            for P in B.First .. Stop loop
               if B.Data (P) = ASCII.LF then
                  Count := P - B.First;
                  Item (Last + 1 .. Last + Count) := B.Data (B.First .. P - 1);
                  Last := Last + Count;
                  B.First := P + 1;
                  Line_Terminator_Read (File);
                  Ended := True;
                  return;
               end if;
            end loop;
            Count := Stop - B.First + 1;
            Item (Last + 1 .. Last + Count) := B.Data (B.First .. Stop);
            Last := Last + Count;
            File.Col := File.Col + Int (Count);
            B.First := Stop + 1;
         end;
      end loop;
   end Get_Line_Part;

   procedure Get_Line_Part
     (File  : File_Access;
      Item  : out Wide_Wide_String;
      Last  : out Natural;
      Ended : out Boolean)
   is
      Code   : Int;
      Length : Natural;
   begin
      Check_Input (File);
      Last := Item'First - 1;
      Ended := False;
      while Last < Item'Last loop
         Next_Code (File, Code, Length);
         if Code = End_Of_Input then
            Ended := True;
            return;
         end if;
         Consume (File, Length);
         if Code = Line_Feed then
            Line_Terminator_Read (File);
            Ended := True;
            return;
         end if;
         Last := Last + 1;
         Item (Last) := Wide_Wide_Character'Val (Code);
         File.Col := File.Col + 1;
      end loop;
   end Get_Line_Part;

   function Peek (File : File_Access) return Int is
      Code   : Int;
      Length : Natural;
   begin
      Next_Code (File, Code, Length);
      return Code;
   end Peek;

   procedure Skip (File : File_Access) is
      Code   : Int;
      Length : Natural;
   begin
      Next_Code (File, Code, Length);
      Consume (File, Length);
      File.Col := File.Col + 1;
   end Skip;

   procedure Skip_Blanks (File : File_Access) is
      Code   : Int;
      Length : Natural;
   begin
      loop
         Next_Code (File, Code, Length);
         if Code = End_Of_Input then
            raise End_Error;
         elsif Code = Character'Pos (' ') or else Code = Tab then
            Consume (File, Length);
            File.Col := File.Col + 1;
         elsif Code = Line_Feed then
            Consume (File, Length);
            Line_Terminator_Read (File);
         elsif Code = Form_Feed and then File.Regular then
            Consume (File, Length);
            Page_Terminator_Read (File);
         else
            exit;
         end if;
      end loop;
   end Skip_Blanks;

   ---------------------------------------------------------------------------
   --  Streams

   overriding procedure Read
     (Stream : in out Text_Stream;
      Item   : out Ada.Streams.Stream_Element_Array;
      Last   : out Ada.Streams.Stream_Element_Offset)
   is
      File : constant File_Access := Stream.File;
   begin
      Check_Input (File);
      Last := Item'First - 1;
      while Last < Item'Last loop
         exit when File.Buffer.First > File.Buffer.Last
           and then not Fill (File, 1);
         declare
            B     : Input_Buffer renames File.Buffer.all;
            Count : constant Natural :=
               Natural (Ada.Streams.Stream_Element_Offset'Min
                          (Ada.Streams.Stream_Element_Offset
                             (B.Last - B.First + 1), Item'Last - Last));
         begin
            for K in 0 .. Count - 1 loop
               Item (Last + 1 + Ada.Streams.Stream_Element_Offset (K)) :=
                  Character'Pos (B.Data (B.First + K));
            end loop;
            B.First := B.First + Count;
            Last := Last + Ada.Streams.Stream_Element_Offset (Count);
         end;
      end loop;
   end Read;

   overriding procedure Write
     (Stream : in out Text_Stream;
      Item   : Ada.Streams.Stream_Element_Array)
   is
      File : constant File_Access := Stream.File;
   begin
      Check_Output (File);
      if Item'Length > 0 then
         Native_IO.Write (File.Handle, Item'Address, Item'Length);
      end if;
   end Write;

   ---------------------------------------------------------------------------
   --  The files still open when the program ends are closed.

   type Finalizer is new Ada.Finalization.Limited_Controlled with null record;
   overriding procedure Finalize (Object : in out Finalizer);

   overriding procedure Finalize (Object : in out Finalizer) is
      File : File_Access := Standard_Files;
   begin
      --  The last line of the standard output and error is terminated.
      while File /= null loop
         if File.Mode /= In_File and then File.Col /= 1 then
            begin
               Write (File, "", Line_Feeds => 1);
               File.Col := 1;
            exception
               when others =>
                  null;
            end;
         end if;
         File := File.Next;
      end loop;
      while Open_Files /= null loop
         File := Open_Files;
         begin
            Close (File);
         exception
            when others =>
               if File /= null and then File.Is_Open then
                  Release (File);
               end if;
         end;
      end loop;
   end Finalize;

   The_Finalizer : Finalizer;

end Ada.Text_IO_Engine;
