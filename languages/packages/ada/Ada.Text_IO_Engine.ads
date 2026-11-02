--  Ada.Text_IO_Engine.ads
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

--  The text files of Ada.Text_IO, Ada.Wide_Text_IO and Ada.Wide_Wide_Text_IO
--  (their File_Type designates a Text_File): file management, line, column
--  and page management, and the input and output of characters (as code
--  points: the external files of Text_IO are Latin-1, those of the wide
--  packages UTF-8).
--
--  The external representation: a line terminator is a line feed; a page
--  terminator is a form feed (recognized only in files that are not the
--  standard input, output or error); the file terminator is the end of the
--  file (a page terminator before it is implied).

with Ada.Streams;
with Interfaces;
with Ada.Native_IO;

private package Ada.Text_IO_Engine is

   use type Interfaces.Integer_64;

   subtype Int is Interfaces.Integer_64;

   type File_Mode is (In_File, Out_File, Append_File);
   type Encoding_Kind is (Latin_1, UTF_8);

   Count_Last : constant := Integer'Last;    --  (Count'Last)
   Field_Last : constant := 255;             --  (Field'Last)

   End_Of_Input : constant Int := -1;        --  (the code at the end)
   Line_Feed    : constant Int := 10;
   Form_Feed    : constant Int := 12;

   type Text_File;
   type File_Access is access all Text_File;

   --  The stream of a text file (Text_IO.Text_Streams): it reads and writes
   --  the external file's bytes directly.
   type Text_Stream is new Ada.Streams.Root_Stream_Type with record
      File : File_Access;
   end record;

   overriding procedure Read
     (Stream : in out Text_Stream;
      Item   : out Ada.Streams.Stream_Element_Array;
      Last   : out Ada.Streams.Stream_Element_Offset);

   overriding procedure Write
     (Stream : in out Text_Stream;
      Item   : Ada.Streams.Stream_Element_Array);

   Buffer_Size : constant := 4096;

   type Input_Buffer is record
      Data   : String (1 .. Buffer_Size);
      First  : Positive := 1;   --  (the next byte)
      Last   : Natural := 0;    --  (the last byte read)
      At_End : Boolean := False;
   end record;
   type Buffer_Access is access Input_Buffer;

   type String_Access is access String;

   type Text_File is limited record
      Handle      : Int := Native_IO.No_Handle;
      Is_Open     : Boolean := False;
      Is_Standard : Boolean := False;  --  (a standard file: never freed)
      Temporary   : Boolean := False;  --  (deleted when closed)
      Mode        : File_Mode := In_File;
      Encoding    : Encoding_Kind := Latin_1;
      Regular     : Boolean := True;   --  (page terminators recognized)
      Name        : String_Access;
      Form        : String_Access;
      Buffer      : Buffer_Access;
      Col         : Int := 1;
      Line        : Int := 1;
      Page        : Int := 1;
      Line_Length : Int := 0;
      Page_Length : Int := 0;
      Stream      : aliased Text_Stream;
      Next        : File_Access;       --  (the list of open files)
   end record;

   --  File management (Status_Error, Mode_Error ... as the Reference Manual
   --  specifies for Text_IO)

   procedure Create
     (File     : in out File_Access;
      Mode     : File_Mode;
      Name     : String;
      Form     : String;
      Encoding : Encoding_Kind);

   procedure Open
     (File     : in out File_Access;
      Mode     : File_Mode;
      Name     : String;
      Form     : String;
      Encoding : Encoding_Kind);

   procedure Close (File : in out File_Access);
   procedure Delete (File : in out File_Access);
   procedure Reset (File : File_Access; Mode : File_Mode);
   procedure Reset (File : File_Access);

   function Mode (File : File_Access) return File_Mode;
   function Name (File : File_Access) return String;
   function Form (File : File_Access) return String;
   function Is_Open (File : File_Access) return Boolean;

   --  A standard file (0: input, 1: output, 2: error).
   function Standard_File (Handle : Int; Encoding : Encoding_Kind)
      return File_Access;

   procedure Check_Open (File : File_Access);     --  (Status_Error)
   procedure Check_Input (File : File_Access);    --  (and Mode_Error)
   procedure Check_Output (File : File_Access);   --  (and Mode_Error)

   procedure Flush (File : File_Access);

   procedure Set_Line_Length (File : File_Access; To : Int);
   procedure Set_Page_Length (File : File_Access; To : Int);
   function Line_Length (File : File_Access) return Int;
   function Page_Length (File : File_Access) return Int;

   procedure New_Line (File : File_Access; Spacing : Int);
   procedure Skip_Line (File : File_Access; Spacing : Int);
   function End_Of_Line (File : File_Access) return Boolean;
   procedure New_Page (File : File_Access);
   procedure Skip_Page (File : File_Access);
   function End_Of_Page (File : File_Access) return Boolean;
   function End_Of_File (File : File_Access) return Boolean;
   procedure Set_Col (File : File_Access; To : Int);
   procedure Set_Line (File : File_Access; To : Int);
   function Col (File : File_Access) return Int;
   function Line (File : File_Access) return Int;
   function Page (File : File_Access) return Int;

   --  Characters (as code points)

   function Get (File : File_Access) return Int;
   --  (after skipping line and page terminators)
   procedure Put (File : File_Access; Code : Int);
   procedure Look_Ahead
     (File        : File_Access;
      Code        : out Int;
      End_Of_Line : out Boolean);
   function Get_Immediate (File : File_Access) return Int;

   --  Output of text: Text is the external representation of Count
   --  characters (Latin-1 or UTF-8), a line terminator follows if Line_End.
   procedure Put_Text
     (File     : File_Access;
      Text     : String;
      Count    : Natural;
      Line_End : Boolean := False);

   --  Output of the image of an item (a number or enumeration literal) of
   --  Count characters (A.10.6(8)).
   procedure Put_Item (File : File_Access; Text : String; Count : Natural);

   --  Input of a line (Latin-1 files): reads characters into Item until the
   --  end of a line (then skipped: Ended) or of the file (Ended) or of Item
   --  (End_Error is not propagated).
   procedure Get_Line_Part
     (File  : File_Access;
      Item  : out String;
      Last  : out Natural;
      Ended : out Boolean);

   --  The same for any file, as code points.
   procedure Get_Line_Part
     (File  : File_Access;
      Item  : out Wide_Wide_String;
      Last  : out Natural;
      Ended : out Boolean);

   --  Input of items: the next character (without reading it: End_Of_Input
   --  at the end of the file), the reading of the next character (Col is
   --  increased), the skipping of blanks and line and page terminators
   --  (End_Error at the end of the file).
   function Peek (File : File_Access) return Int;
   procedure Skip (File : File_Access);
   procedure Skip_Blanks (File : File_Access);

   --  UTF-8
   procedure Encode (Code : Int; Into : in out String; Last : in out Natural);
   --  (appends the encoding of Code to Into (Last + 1 ..))
   function Decode (Text : String) return Wide_Wide_String;
   function Encode (Text : Wide_Wide_String) return String;

end Ada.Text_IO_Engine;
