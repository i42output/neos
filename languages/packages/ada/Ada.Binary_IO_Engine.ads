--  Ada.Binary_IO_Engine.ads
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

--  The files of Ada.Sequential_IO, Ada.Direct_IO and Ada.Streams.Stream_IO
--  (their File_Type designates a Binary_File): file management and the
--  transfer of bytes at the current position (in bytes from the start).

with Interfaces;
with System;
with Ada.Streams;
with Ada.Native_IO;

private package Ada.Binary_IO_Engine is
   pragma Preelaborate;

   use type Interfaces.Integer_64;

   subtype Int is Interfaces.Integer_64;

   type Binary_File;
   type File_Access is access all Binary_File;

   --  The stream of a file (Stream_IO.Stream): it reads and writes at the
   --  file's current position.
   type Binary_Stream is new Ada.Streams.Root_Stream_Type with record
      File : File_Access;
   end record;

   overriding procedure Read
     (Stream : in out Binary_Stream;
      Item   : out Ada.Streams.Stream_Element_Array;
      Last   : out Ada.Streams.Stream_Element_Offset);

   overriding procedure Write
     (Stream : in out Binary_Stream;
      Item   : Ada.Streams.Stream_Element_Array);

   type String_Access is access String;

   --  The access of a file: Mode is the 'Pos of the package's File_Mode;
   --  Readable and Writable say which transfers it allows; if Appending,
   --  writes are at the end of the file.
   type Access_Kind is record
      Mode      : Natural := 0;
      Readable  : Boolean := False;
      Writable  : Boolean := False;
      Appending : Boolean := False;
   end record;

   type Binary_File is limited record
      Handle          : Int := Native_IO.No_Handle;
      Is_Open         : Boolean := False;
      Kind            : Access_Kind;
      Temporary       : Boolean := False;
      Name            : String_Access;
      Form            : String_Access;
      Position        : Int := 0;   --  (of the next transfer)
      Native_Position : Int := -1;  --  (of the native file; -1: unknown)
      Stream          : aliased Binary_Stream;
      Next            : File_Access;  --  (the list of open files)
   end record;

   --  File management (Status_Error, Name_Error, Use_Error ... as A.8.2
   --  specifies). Create opens Name created or truncated (a temporary file
   --  if Name is null); Open an existing file, truncated if Truncate. The
   --  position is then that of the first byte (of the end if appending).

   procedure Create
     (File : in out File_Access;
      Kind : Access_Kind;
      Name : String;
      Form : String);

   procedure Open
     (File     : in out File_Access;
      Kind     : Access_Kind;
      Name     : String;
      Form     : String;
      Truncate : Boolean);

   procedure Close (File : in out File_Access);
   procedure Delete (File : in out File_Access);
   procedure Reset
     (File : File_Access; Kind : Access_Kind; Truncate : Boolean);
   procedure Set_Kind (File : File_Access; Kind : Access_Kind);
   --  (the access is changed; the position is unchanged unless appending)

   function Kind (File : File_Access) return Access_Kind;
   function Name (File : File_Access) return String;
   function Form (File : File_Access) return String;
   function Is_Open (File : File_Access) return Boolean;
   procedure Flush (File : File_Access);

   procedure Check_Open (File : File_Access);      --  (Status_Error)
   procedure Check_Readable (File : File_Access);  --  (and Mode_Error)
   procedure Check_Writable (File : File_Access);  --  (and Mode_Error)

   --  Transfers at the current position (which is advanced): Read returns
   --  the number of bytes read (fewer at the end of the file).
   function Read
     (File : File_Access; Data : System.Address; Length : Int) return Int;
   procedure Write (File : File_Access; Data : System.Address; Length : Int);

   procedure Set_Position (File : File_Access; Position : Int);
   function Position (File : File_Access) return Int;
   function Size (File : File_Access) return Int;
   function End_Of_File (File : File_Access) return Boolean;
   --  (whether the position is at or beyond the end)

end Ada.Binary_IO_Engine;
