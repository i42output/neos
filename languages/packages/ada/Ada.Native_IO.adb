--  Ada.Native_IO.adb
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

package body Ada.Native_IO is



   --  The handles of the files open (Opened (H) for handle H): those still
   --  open are closed when the program ends.
   type Flags is array (Int range <>) of Boolean;
   type Flags_Access is access Flags;
   procedure Free is new Ada.Unchecked_Deallocation (Flags, Flags_Access);
   Opened : Flags_Access;

   procedure Register (Handle : Int; Open : Boolean) is
   begin
      if Opened = null or else Handle > Opened'Last then
         if not Open then
            return;
         end if;
         declare
            Size     : Int := (if Opened = null then 64 else Opened'Length);
            New_Flags : Flags_Access;
         begin
            while Size <= Handle loop
               Size := Size * 2;
            end loop;
            New_Flags := new Flags'(0 .. Size - 1 => False);
            if Opened /= null then
               New_Flags (Opened'Range) := Opened.all;
               Free (Opened);
            end if;
            Opened := New_Flags;
         end;
      end if;
      if Handle >= Opened'First then
         Opened (Handle) := Open;
      end if;
   end Register;

   type Finalizer is new Ada.Finalization.Limited_Controlled with null record;
   overriding procedure Finalize (Object : in out Finalizer);

   function IO_Open (Name : System.Address; Length : Int; Mode : Int)
      return Int;
   pragma Import (Neos, IO_Open, "ada::io_open");

   function IO_Open_Temp (Mode : Int) return Int;
   pragma Import (Neos, IO_Open_Temp, "ada::io_open_temp");

   function IO_Reopen (Handle : Int; Mode : Int) return Int;
   pragma Import (Neos, IO_Reopen, "ada::io_reopen");

   function IO_Close (Handle : Int) return Int;
   pragma Import (Neos, IO_Close, "ada::io_close");

   function IO_Delete (Name : System.Address; Length : Int) return Int;
   pragma Import (Neos, IO_Delete, "ada::io_delete");

   function IO_Name (Handle : Int; Buffer : System.Address; Length : Int)
      return Int;
   pragma Import (Neos, IO_Name, "ada::io_name");

   function IO_Read (Handle : Int; Buffer : System.Address; Length : Int)
      return Int;
   pragma Import (Neos, IO_Read, "ada::io_read");

   function IO_Write
     (Handle : Int; Data : System.Address; Length : Int; Line_Feeds : Int)
      return Int;
   pragma Import (Neos, IO_Write, "ada::io_write");

   function IO_Flush (Handle : Int) return Int;
   pragma Import (Neos, IO_Flush, "ada::io_flush");

   function IO_At_End (Handle : Int) return Int;
   pragma Import (Neos, IO_At_End, "ada::io_at_end");

   function IO_Seek (Handle : Int; Position : Int) return Int;
   pragma Import (Neos, IO_Seek, "ada::io_seek");

   function IO_Size (Handle : Int) return Int;
   pragma Import (Neos, IO_Size, "ada::io_size");

   function IO_Float_Digits
     (X : Long_Float; Buffer : System.Address; Length : Int) return Int;
   pragma Import (Neos, IO_Float_Digits, "ada::io_float_digits");

   function IO_Float_Value (Text : System.Address; Length : Int)
      return Long_Float;
   pragma Import (Neos, IO_Float_Value, "ada::io_float_value");

   --  Raise the exception of an error code.
   procedure Check (Result : Int) is
   begin
      if Result >= 0 then
         return;
      elsif Result = -1 then
         raise IO_Exceptions.Name_Error;
      elsif Result = -2 then
         raise IO_Exceptions.Use_Error;
      else
         raise IO_Exceptions.Device_Error;
      end if;
   end Check;

   function Open
     (Name : String; Mode : Open_Mode; Must_Exist : Boolean := False)
      return Int
   is
      Result : constant Int :=
         IO_Open (Name'Address, Name'Length,
                  Open_Mode'Pos (Mode) + (if Must_Exist then 8 else 0));
   begin
      Check (Result);
      Register (Result, True);
      return Result;
   end Open;

   function Open_Temporary return Int is
      Result : constant Int :=
         IO_Open_Temp (Open_Mode'Pos (Read_Write_Create));
   begin
      Check (Result);
      Register (Result, True);
      return Result;
   end Open_Temporary;

   procedure Reopen (Handle : Int; Mode : Open_Mode) is
      Result : constant Int := IO_Reopen (Handle, Open_Mode'Pos (Mode));
   begin
      if Result < 0 then
         Register (Handle, False);  --  (the file is closed)
      end if;
      Check (Result);
   end Reopen;

   procedure Close (Handle : Int) is
      Result : constant Int := IO_Close (Handle);
   begin
      Register (Handle, False);
      Check (Result);
   end Close;

   procedure Delete (Name : String) is
   begin
      Check (IO_Delete (Name'Address, Name'Length));
   end Delete;

   function Name (Handle : Int) return String is
      Buffer : String (1 .. 1024);
      Length : constant Int := IO_Name (Handle, Buffer'Address, Buffer'Length);
   begin
      Check (Length);
      if Length <= Buffer'Length then
         return Buffer (1 .. Natural (Length));
      end if;
      declare
         Long_Buffer : String (1 .. Natural (Length));
      begin
         Check (IO_Name (Handle, Long_Buffer'Address, Long_Buffer'Length));
         return Long_Buffer;
      end;
   end Name;

   function Read
     (Handle : Int; Buffer : System.Address; Length : Int) return Int
   is
      Result : constant Int := IO_Read (Handle, Buffer, Length);
   begin
      Check (Result);
      return Result;
   end Read;

   procedure Write
     (Handle     : Int;
      Data       : System.Address;
      Length     : Int;
      Line_Feeds : Int := 0) is
   begin
      Check (IO_Write (Handle, Data, Length, Line_Feeds));
   end Write;

   procedure Write (Handle : Int; Data : String; Line_Feeds : Int := 0) is
   begin
      Check (IO_Write (Handle, Data'Address, Data'Length, Line_Feeds));
   end Write;

   procedure Flush (Handle : Int) is
   begin
      Check (IO_Flush (Handle));
   end Flush;

   function At_End (Handle : Int) return Boolean is
      Result : constant Int := IO_At_End (Handle);
   begin
      Check (Result);
      return Result = 1;
   end At_End;

   procedure Seek (Handle : Int; Position : Int) is
   begin
      Check (IO_Seek (Handle, Position));
   end Seek;

   function Size (Handle : Int) return Int is
      Result : constant Int := IO_Size (Handle);
   begin
      Check (Result);
      return Result;
   end Size;

   function Float_Digits (X : Long_Float) return String is
      Buffer : String (1 .. 840);
      Length : constant Int :=
         IO_Float_Digits (X, Buffer'Address, Buffer'Length);
   begin
      case Length is
         when -1     => return "NaN";
         when -2     => return "+Inf";
         when -3     => return "-Inf";
         when others => return Buffer (1 .. Natural (Length));
      end case;
   end Float_Digits;

   function Float_Value (Text : String) return Long_Float is
   begin
      return IO_Float_Value (Text'Address, Text'Length);
   end Float_Value;

   function Encode (Item : Wide_Wide_String) return String is
      Result : String (1 .. 4 * Item'Length);
      Last   : Natural := 0;
      Code   : Natural;

      procedure Add (Byte : Natural) is
      begin
         Last := Last + 1;
         Result (Last) := Character'Val (Byte);
      end Add;
   begin
      for C of Item loop
         Code := Wide_Wide_Character'Pos (C);
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
      end loop;
      return Result (1 .. Last);
   end Encode;

   function Encode (Item : Wide_String) return String is
      Wide_Wide : Wide_Wide_String (Item'Range);
   begin
      for I in Item'Range loop
         Wide_Wide (I) :=
            Wide_Wide_Character'Val (Wide_Character'Pos (Item (I)));
      end loop;
      return Encode (Wide_Wide);
   end Encode;

   function Decode (Item : String) return Wide_Wide_String is
      Result : Wide_Wide_String (1 .. Item'Length);
      Last   : Natural := 0;
      I      : Positive := Item'First;
      First  : Natural;
      Code   : Natural;
      Count  : Natural;
      Min    : Natural;
      Valid  : Boolean;
   begin
      while I <= Item'Last loop
         First := Character'Pos (Item (I));
         Code := First;
         Count := 1;
         Min := 0;
         if First in 16#C2# .. 16#DF# then
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
         end if;
         Valid := Count > 1 and then I + Count - 1 <= Item'Last;
         if Valid then
            for K in 1 .. Count - 1 loop
               if Character'Pos (Item (I + K)) not in 16#80# .. 16#BF# then
                  Valid := False;
                  exit;
               end if;
               Code := Code * 64 + Character'Pos (Item (I + K)) - 16#80#;
            end loop;
            Valid := Valid and then Code >= Min and then Code <= 16#10_FFFF#;
         end if;
         if not Valid then
            Code := First;
            Count := 1;
         end if;
         Last := Last + 1;
         Result (Last) := Wide_Wide_Character'Val (Code);
         I := I + Count;
      end loop;
      return Result (1 .. Last);
   end Decode;

   function Decode (Item : String) return Wide_String is
      Wide_Wide : constant Wide_Wide_String := Decode (Item);
      Result    : Wide_String (1 .. Wide_Wide'Length);
      Code      : Natural;
   begin
      for I in Wide_Wide'Range loop
         Code := Wide_Wide_Character'Pos (Wide_Wide (I));
         Result (I) :=
            Wide_Character'Val (if Code > 16#FFFF# then 16#FFFD# else Code);
      end loop;
      return Result;
   end Decode;

   overriding procedure Finalize (Object : in out Finalizer) is
      Ignored : Int;
   begin
      if Opened /= null then
         for Handle in Opened'Range loop
            if Opened (Handle) then
               Ignored := IO_Close (Handle);
            end if;
         end loop;
         Free (Opened);
      end if;
      Ignored := IO_Flush (Standard_Output_Handle);
      Ignored := IO_Flush (Standard_Error_Handle);
   end Finalize;

   The_Finalizer : Finalizer;

end Ada.Native_IO;
