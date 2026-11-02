--  Ada.Binary_IO_Engine.adb
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
with System.Storage_Elements;

package body Ada.Binary_IO_Engine is

   use type Ada.Streams.Stream_Element_Offset;

   procedure Free is new Ada.Unchecked_Deallocation (Binary_File, File_Access);
   procedure Free is new Ada.Unchecked_Deallocation (String, String_Access);

   Open_Files : File_Access;

   procedure Check_Open (File : File_Access) is
   begin
      if File = null or else not File.Is_Open then
         raise IO_Exceptions.Status_Error;
      end if;
   end Check_Open;

   procedure Check_Readable (File : File_Access) is
   begin
      Check_Open (File);
      if not File.Kind.Readable then
         raise IO_Exceptions.Mode_Error;
      end if;
   end Check_Readable;

   procedure Check_Writable (File : File_Access) is
   begin
      Check_Open (File);
      if not File.Kind.Writable then
         raise IO_Exceptions.Mode_Error;
      end if;
   end Check_Writable;

   procedure Initialize
     (File      : in out File_Access;
      Handle    : Int;
      Kind      : Access_Kind;
      Form      : String;
      Temporary : Boolean) is
   begin
      File := new Binary_File;
      File.Handle := Handle;
      File.Kind := Kind;
      File.Temporary := Temporary;
      File.Form := new String'(Form);
      File.Name := new String'(Native_IO.Name (Handle));
      File.Stream.File := File;
      File.Native_Position := 0;
      File.Position := (if Kind.Appending then Native_IO.Size (Handle) else 0);
      File.Is_Open := True;
      File.Next := Open_Files;
      Open_Files := File;
   end Initialize;

   procedure Create
     (File : in out File_Access;
      Kind : Access_Kind;
      Name : String;
      Form : String)
   is
      Handle : Int;
   begin
      if File /= null and then File.Is_Open then
         raise IO_Exceptions.Status_Error;
      end if;
      if Name = "" then
         Handle := Native_IO.Open_Temporary;
      else
         Handle := Native_IO.Open (Name, Native_IO.Read_Write_Create);
      end if;
      Initialize (File, Handle, Kind, Form, Name = "");
   end Create;

   function Native_Mode (Kind : Access_Kind; Truncate : Boolean)
      return Native_IO.Open_Mode is
   begin
      if Truncate then
         return (if Kind.Readable then Native_IO.Read_Write_Create
                 else Native_IO.Write);
      elsif Kind.Writable then
         return Native_IO.Read_Write;
      else
         return Native_IO.Read;
      end if;
   end Native_Mode;

   procedure Open
     (File     : in out File_Access;
      Kind     : Access_Kind;
      Name     : String;
      Form     : String;
      Truncate : Boolean)
   is
      Handle : Int;
   begin
      if File /= null and then File.Is_Open then
         raise IO_Exceptions.Status_Error;
      end if;
      Handle := Native_IO.Open
                  (Name, Native_Mode (Kind, Truncate), Must_Exist => True);
      Initialize (File, Handle, Kind, Form, False);
   end Open;

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

   --  The file is closed (its native file already closed).
   procedure Release (File : in out File_Access) is
   begin
      File.Is_Open := False;
      Unlink (File);
      Free (File.Name);
      Free (File.Form);
      Free (File);
   end Release;

   procedure Close (File : in out File_Access) is
   begin
      Check_Open (File);
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

   procedure Reset
     (File : File_Access; Kind : Access_Kind; Truncate : Boolean) is
   begin
      Check_Open (File);
      begin
         Native_IO.Reopen (File.Handle, Native_Mode (Kind, Truncate));
      exception
         when others =>
            declare
               Closed : File_Access := File;
            begin
               Release (Closed);  --  (the native file is closed)
            end;
            raise IO_Exceptions.Use_Error;
      end;
      File.Kind := Kind;
      File.Native_Position := 0;
      File.Position :=
         (if Kind.Appending then Native_IO.Size (File.Handle) else 0);
   end Reset;

   procedure Set_Kind (File : File_Access; Kind : Access_Kind) is
   begin
      Check_Open (File);
      if (Kind.Readable and then not File.Kind.Readable)
        or else (Kind.Writable and then not File.Kind.Writable)
      then
         --  The native file is reopened with the access needed.
         declare
            Position : constant Int := File.Position;
         begin
            Native_IO.Flush (File.Handle);
            Native_IO.Reopen
              (File.Handle,
               Native_Mode ((Mode => 0, Readable => True, Writable => True,
                             Appending => False), False));
            File.Native_Position := 0;
            File.Position := Position;
         exception
            when others =>
               declare
                  Closed : File_Access := File;
               begin
                  Release (Closed);
               end;
               raise IO_Exceptions.Use_Error;
         end;
      end if;
      File.Kind := Kind;
      if Kind.Appending then
         File.Position := Native_IO.Size (File.Handle);
      end if;
   end Set_Kind;

   function Kind (File : File_Access) return Access_Kind is
   begin
      Check_Open (File);
      return File.Kind;
   end Kind;

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

   procedure Flush (File : File_Access) is
   begin
      Check_Writable (File);
      Native_IO.Flush (File.Handle);
   end Flush;

   --  The native file is positioned at the file's position.
   procedure Seek (File : File_Access) is
   begin
      if File.Native_Position /= File.Position then
         Native_IO.Seek (File.Handle, File.Position);
         File.Native_Position := File.Position;
      end if;
   end Seek;

   function Read
     (File : File_Access; Data : System.Address; Length : Int) return Int
   is
      Count : Int := 0;
      Part  : Int;
   begin
      Seek (File);
      --  (the native read can return fewer bytes than are available)
      while Count < Length loop
         Part := Native_IO.Read
                   (File.Handle,
                    System.Storage_Elements."+"
                      (Data, System.Storage_Elements.Storage_Offset (Count)),
                    Length - Count);
         exit when Part = 0;
         Count := Count + Part;
      end loop;
      File.Position := File.Position + Count;
      File.Native_Position := File.Position;
      return Count;
   end Read;

   procedure Write (File : File_Access; Data : System.Address; Length : Int)
   is
   begin
      Seek (File);
      Native_IO.Write (File.Handle, Data, Length);
      File.Position := File.Position + Length;
      File.Native_Position := File.Position;
   end Write;

   procedure Set_Position (File : File_Access; Position : Int) is
   begin
      Check_Open (File);
      File.Position := Position;
   end Set_Position;

   function Position (File : File_Access) return Int is
   begin
      Check_Open (File);
      return File.Position;
   end Position;

   function Size (File : File_Access) return Int is
   begin
      Check_Open (File);
      return Native_IO.Size (File.Handle);
   end Size;

   function End_Of_File (File : File_Access) return Boolean is
   begin
      Check_Open (File);
      Seek (File);
      return Native_IO.At_End (File.Handle);
   end End_Of_File;

   ---------------------------------------------------------------------------
   --  Streams

   overriding procedure Read
     (Stream : in out Binary_Stream;
      Item   : out Ada.Streams.Stream_Element_Array;
      Last   : out Ada.Streams.Stream_Element_Offset)
   is
      Count : Int;
   begin
      Check_Readable (Stream.File);
      if Item'Length = 0 then
         Last := Item'First - 1;
         return;
      end if;
      Count := Read (Stream.File, Item'Address, Item'Length);
      Last := Item'First + Ada.Streams.Stream_Element_Offset (Count) - 1;
   end Read;

   overriding procedure Write
     (Stream : in out Binary_Stream;
      Item   : Ada.Streams.Stream_Element_Array) is
   begin
      Check_Writable (Stream.File);
      if Item'Length > 0 then
         Write (Stream.File, Item'Address, Item'Length);
      end if;
   end Write;

   ---------------------------------------------------------------------------
   --  The files still open when the program ends are closed.

   type Finalizer is new Ada.Finalization.Limited_Controlled with null record;
   overriding procedure Finalize (Object : in out Finalizer);

   overriding procedure Finalize (Object : in out Finalizer) is
      File : File_Access;
   begin
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

end Ada.Binary_IO_Engine;
