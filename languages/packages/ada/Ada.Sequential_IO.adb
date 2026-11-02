--  Ada.Sequential_IO.adb
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

package body Ada.Sequential_IO is

   package Engine renames Ada.Binary_IO_Engine;

   use type Interfaces.Integer_64;

   subtype Int is Interfaces.Integer_64;

   --  An element is written as the bytes of its representation, preceded,
   --  for an indefinite Element_Type, by their number (an Integer_64).

   function Kind_Of (Mode : File_Mode) return Engine.Access_Kind is
     (Mode      => File_Mode'Pos (Mode),
      Readable  => Mode = In_File,
      Writable  => Mode /= In_File,
      Appending => Mode = Append_File);

   procedure Create(File : in out File_Type;
                    Mode : in File_Mode := Out_File;
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
      Engine.Open
        (File.File, Kind_Of (Mode), Name, Form, Truncate => Mode = Out_File);
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
      Engine.Reset (File.File, Kind_Of (Mode), Truncate => Mode = Out_File);
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

   procedure Read  (File : in File_Type; Item : out Element_Type) is
      Size   : constant Int := Int ((Item'Size + 7) / 8);
      Length : Int := Size;
   begin
      Engine.Check_Readable (File.File);
      if not Element_Type'Definite then
         if Engine.Read (File.File, Length'Address, 8) /= 8 then
            raise End_Error;
         end if;
         if Length < 0 or else Length > Size then
            raise Data_Error;
         end if;
      end if;
      if Engine.Read (File.File, Item'Address, Length) /= Length then
         raise End_Error;
      end if;
   end Read;

   procedure Write (File : in File_Type; Item : in Element_Type) is
      Length : aliased constant Int := Int ((Item'Size + 7) / 8);
   begin
      Engine.Check_Writable (File.File);
      if not Element_Type'Definite then
         Engine.Write (File.File, Length'Address, 8);
      end if;
      Engine.Write (File.File, Item'Address, Length);
   end Write;

   function End_Of_File(File : in File_Type) return Boolean is
   begin
      Engine.Check_Readable (File.File);
      return Engine.End_Of_File (File.File);
   end End_Of_File;

   package body Wide_File_Names is

      procedure Create(File : in out File_Type;
                       Mode : in File_Mode := Out_File;
                       Name : in Wide_String := "";
                       Form : in Wide_String := "") is
      begin
         Sequential_IO.Create
           (File, Mode, Native_IO.Encode (Name), Native_IO.Encode (Form));
      end Create;

      procedure Open  (File : in out File_Type;
                       Mode : in File_Mode;
                       Name : in Wide_String;
                       Form : in Wide_String := "") is
      begin
         Sequential_IO.Open
           (File, Mode, Native_IO.Encode (Name), Native_IO.Encode (Form));
      end Open;

      function Name   (File : in File_Type) return Wide_String is
      begin
         return Native_IO.Decode (Sequential_IO.Name (File));
      end Name;

      function Form   (File : in File_Type) return Wide_String is
      begin
         return Native_IO.Decode (Sequential_IO.Form (File));
      end Form;

   end Wide_File_Names;

   package body Wide_Wide_File_Names is

      procedure Create(File : in out File_Type;
                       Mode : in File_Mode := Out_File;
                       Name : in Wide_Wide_String := "";
                       Form : in Wide_Wide_String := "") is
      begin
         Sequential_IO.Create
           (File, Mode, Native_IO.Encode (Name), Native_IO.Encode (Form));
      end Create;

      procedure Open  (File : in out File_Type;
                       Mode : in File_Mode;
                       Name : in Wide_Wide_String;
                       Form : in Wide_Wide_String := "") is
      begin
         Sequential_IO.Open
           (File, Mode, Native_IO.Encode (Name), Native_IO.Encode (Form));
      end Open;

      function Name   (File : in File_Type) return Wide_Wide_String is
      begin
         return Native_IO.Decode (Sequential_IO.Name (File));
      end Name;

      function Form   (File : in File_Type) return Wide_Wide_String is
      begin
         return Native_IO.Decode (Sequential_IO.Form (File));
      end Form;

   end Wide_Wide_File_Names;

end Ada.Sequential_IO;
