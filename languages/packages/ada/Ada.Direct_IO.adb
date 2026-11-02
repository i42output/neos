--  Ada.Direct_IO.adb
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

package body Ada.Direct_IO is

   package Engine renames Ada.Binary_IO_Engine;

   use type Interfaces.Integer_64;

   subtype Int is Interfaces.Integer_64;

   --  An element occupies the bytes of the representation of an object of
   --  the subtype Element_Type (the element of index I is at the byte
   --  (I - 1) * Bytes).
   Bytes : constant Int :=
      Int'Max (Int (Element_Type'Max_Size_In_Storage_Elements), 1);

   function Kind_Of (Mode : File_Mode) return Engine.Access_Kind is
     (Mode      => File_Mode'Pos (Mode),
      Readable  => Mode /= Out_File,
      Writable  => Mode /= In_File,
      Appending => False);

   procedure Create(File : in out File_Type;
                    Mode : in File_Mode := Inout_File;
                    Name : in String := "";
                    Form : in String := "") is
   begin
      Engine.Create (File.File, Kind_Of (Mode), Name, Form);
   end Create;

   procedure Open  (File : in out File_Type;
                    Mode : in File_Mode;
                    Name : in String;
                    Form : in String := "") is
   begin
      Engine.Open (File.File, Kind_Of (Mode), Name, Form, Truncate => False);
   end Open;

   procedure Close (File : in out File_Type) is
   begin
      Engine.Close (File.File);
   end Close;

   procedure Delete(File : in out File_Type) is
   begin
      Engine.Delete (File.File);
   end Delete;

   procedure Reset (File : in out File_Type; Mode : in File_Mode) is
   begin
      Engine.Reset (File.File, Kind_Of (Mode), Truncate => False);
   end Reset;

   procedure Reset (File : in out File_Type) is
   begin
      Reset (File, Mode (File));
   end Reset;

   function Mode   (File : in File_Type) return File_Mode is
   begin
      return File_Mode'Val (Engine.Kind (File.File).Mode);
   end Mode;

   function Name   (File : in File_Type) return String is
   begin
      return Engine.Name (File.File);
   end Name;

   function Form   (File : in File_Type) return String is
   begin
      return Engine.Form (File.File);
   end Form;

   function Is_Open(File : in File_Type) return Boolean is
   begin
      return Engine.Is_Open (File.File);
   end Is_Open;

   procedure Flush (File : in File_Type) is
   begin
      Engine.Flush (File.File);
   end Flush;

   procedure Read (File : in File_Type; Item : out Element_Type;
                                        From : in Positive_Count) is
   begin
      Set_Index (File, From);
      Read (File, Item);
   end Read;

   procedure Read (File : in File_Type; Item : out Element_Type) is
      Buffer : Element_Type;  --  (of the largest size)
   begin
      Engine.Check_Readable (File.File);
      if Engine.Read (File.File, Buffer'Address, Bytes) /= Bytes then
         raise End_Error;
      end if;
      Item := Buffer;
   end Read;

   procedure Write(File : in File_Type; Item : in  Element_Type;
                                        To   : in Positive_Count) is
   begin
      Set_Index (File, To);
      Write (File, Item);
   end Write;

   procedure Write(File : in File_Type; Item : in Element_Type) is
      Buffer : constant Element_Type := Item;  --  (of the largest size)
   begin
      Engine.Check_Writable (File.File);
      Engine.Write (File.File, Buffer'Address, Bytes);
   end Write;

   procedure Set_Index(File : in File_Type; To : in Positive_Count) is
   begin
      Engine.Set_Position (File.File, (Int (To) - 1) * Bytes);
   end Set_Index;

   function Index(File : in File_Type) return Positive_Count is
   begin
      return Positive_Count (Engine.Position (File.File) / Bytes + 1);
   end Index;

   function Size (File : in File_Type) return Count is
   begin
      return Count (Engine.Size (File.File) / Bytes);
   end Size;

   function End_Of_File(File : in File_Type) return Boolean is
   begin
      Engine.Check_Readable (File.File);
      return Index (File) > Size (File);
   end End_Of_File;

   package body Wide_File_Names is

      procedure Create(File : in out File_Type;
                       Mode : in File_Mode := Inout_File;
                       Name : in Wide_String := "";
                       Form : in Wide_String := "") is
      begin
         Direct_IO.Create
           (File, Mode, Native_IO.Encode (Name), Native_IO.Encode (Form));
      end Create;

      procedure Open  (File : in out File_Type;
                       Mode : in File_Mode;
                       Name : in Wide_String;
                       Form : in Wide_String := "") is
      begin
         Direct_IO.Open
           (File, Mode, Native_IO.Encode (Name), Native_IO.Encode (Form));
      end Open;

      function Name   (File : in File_Type) return Wide_String is
      begin
         return Native_IO.Decode (Direct_IO.Name (File));
      end Name;

      function Form   (File : in File_Type) return Wide_String is
      begin
         return Native_IO.Decode (Direct_IO.Form (File));
      end Form;

   end Wide_File_Names;

   package body Wide_Wide_File_Names is

      procedure Create(File : in out File_Type;
                       Mode : in File_Mode := Inout_File;
                       Name : in Wide_Wide_String := "";
                       Form : in Wide_Wide_String := "") is
      begin
         Direct_IO.Create
           (File, Mode, Native_IO.Encode (Name), Native_IO.Encode (Form));
      end Create;

      procedure Open  (File : in out File_Type;
                       Mode : in File_Mode;
                       Name : in Wide_Wide_String;
                       Form : in Wide_Wide_String := "") is
      begin
         Direct_IO.Open
           (File, Mode, Native_IO.Encode (Name), Native_IO.Encode (Form));
      end Open;

      function Name   (File : in File_Type) return Wide_Wide_String is
      begin
         return Native_IO.Decode (Direct_IO.Name (File));
      end Name;

      function Form   (File : in File_Type) return Wide_Wide_String is
      begin
         return Native_IO.Decode (Direct_IO.Form (File));
      end Form;

   end Wide_Wide_File_Names;

end Ada.Direct_IO;
