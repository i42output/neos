--  Ada.Strings.Text_Buffers.Unbounded.adb
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

with Ada.Unchecked_Deallocation;
with Ada.Strings.UTF_Encoding.Wide_Wide_Strings;

package body Ada.Strings.Text_Buffers.Unbounded is

   procedure Free is new Ada.Unchecked_Deallocation (Text_Array, Text_Access);

   overriding procedure Adjust (Object : in out Text_Storage) is
   begin
      if Object.Data /= null then
         Object.Data := new Text_Array'(Object.Data (1 .. Object.Last));
      end if;
   end Adjust;

   overriding procedure Finalize (Object : in out Text_Storage) is
   begin
      Free (Object.Data);
      Object.Last := 0;
   end Finalize;

   procedure Read
     (Stream : not null access Ada.Streams.Root_Stream_Type'Class;
      Item   : out Text_Storage) is
      Text : constant Text_Array := Text_Array'Input (Stream);
   begin
      Free (Item.Data);
      Item.Last := Text'Length;
      if Text'Length /= 0 then
         Item.Data := new Text_Array (1 .. Text'Length);
         Item.Data.all := Text;
      end if;
   end Read;

   procedure Write
     (Stream : not null access Ada.Streams.Root_Stream_Type'Class;
      Item   : in Text_Storage) is
   begin
      if Item.Data = null then
         Text_Array'Output (Stream, (1 .. 0 => ' '));
      else
         Text_Array'Output (Stream, Item.Data (1 .. Item.Last));
      end if;
   end Write;

   procedure Store (Buffer : in out Buffer_Type; Text : Text_Array) is
      S        : Text_Storage renames Buffer.Text;
      New_Last : constant Text_Buffer_Count := S.Last + Text'Length;
   begin
      if Text'Length = 0 then
         return;
      end if;
      if S.Data = null or else New_Last > S.Data'Last then
         declare
            Capacity : Text_Buffer_Count := 64;
            Old      : Text_Access := S.Data;
         begin
            if Old /= null then
               if Old'Last >= Text_Buffer_Count'Last / 2 then
                  Capacity := Text_Buffer_Count'Last;
               else
                  Capacity := 2 * Old'Last;
               end if;
            end if;
            Capacity := Text_Buffer_Count'Max (Capacity, New_Last);
            S.Data := new Text_Array (1 .. Capacity);
            if Old /= null then
               S.Data (1 .. S.Last) := Old (1 .. S.Last);
               Free (Old);
            end if;
         end;
      end if;
      S.Data (S.Last + 1 .. New_Last) := Text;
      S.Last := New_Last;
   end Store;

   function Contents (Buffer : Buffer_Type) return Text_Array is
   begin
      if Buffer.Text.Data = null then
         return (1 .. 0 => ' ');
      end if;
      return Buffer.Text.Data (1 .. Buffer.Text.Last);
   end Contents;

   procedure Reset (Buffer : in out Buffer_Type) is
      --  (to the initial state: empty, no indentation)
   begin
      Free (Buffer.Text.Data);
      Buffer.Text.Last := 0;
      Buffer.Indentation := 0;
      Buffer.At_Line_Start := True;
   end Reset;

   procedure Store_Indent (Buffer : in out Buffer_Type) is
   begin
      Store (Buffer, Indent (Buffer));
   end Store_Indent;

   overriding procedure Put (
      Buffer : in out Buffer_Type;
      Item   : in     String) is
      Text : Text_Array (1 .. Item'Length);
   begin
      if Item'Length = 0 then
         return;
      end if;
      for I in Text'Range loop
         Text (I) := Wide_Wide_Character'Val
           (Character'Pos (Item (Item'First + Integer (I) - 1)));
      end loop;
      Store_Indent (Buffer);
      Store (Buffer, Text);
   end Put;

   overriding procedure Wide_Put (
      Buffer : in out Buffer_Type;
      Item   : in     Wide_String) is
      Text : Text_Array (1 .. Item'Length);
   begin
      if Item'Length = 0 then
         return;
      end if;
      for I in Text'Range loop
         Text (I) := Wide_Wide_Character'Val
           (Wide_Character'Pos (Item (Item'First + Integer (I) - 1)));
      end loop;
      Store_Indent (Buffer);
      Store (Buffer, Text);
   end Wide_Put;

   overriding procedure Wide_Wide_Put (
      Buffer : in out Buffer_Type;
      Item   : in     Wide_Wide_String) is
   begin
      if Item'Length = 0 then
         return;
      end if;
      Store_Indent (Buffer);
      Store (Buffer, To_Text (Item));
   end Wide_Wide_Put;

   overriding procedure Put_UTF_8 (
      Buffer : in out Buffer_Type;
      Item   : in     UTF_Encoding.UTF_8_String) is
   begin
      Wide_Wide_Put (Buffer, UTF_Encoding.Wide_Wide_Strings.Decode (Item));
   end Put_UTF_8;

   overriding procedure Wide_Put_UTF_16 (
      Buffer : in out Buffer_Type;
      Item   : in     UTF_Encoding.UTF_16_Wide_String) is
   begin
      Wide_Wide_Put (Buffer, UTF_Encoding.Wide_Wide_Strings.Decode (Item));
   end Wide_Put_UTF_16;

   overriding procedure New_Line (Buffer : in out Buffer_Type) is
   begin
      Store (Buffer, (1 .. New_Line_Count => Line_Terminator));
      Buffer.At_Line_Start := True;
   end New_Line;

   function Get (
      Buffer : in out Buffer_Type)
      return String is
      Result : constant String := To_String (Contents (Buffer));
   begin
      Reset (Buffer);
      return Result;
   end Get;

   function Wide_Get (
      Buffer : in out Buffer_Type)
      return Wide_String is
      Result : constant Wide_String := To_Wide_String (Contents (Buffer));
   begin
      Reset (Buffer);
      return Result;
   end Wide_Get;

   function Wide_Wide_Get (
      Buffer : in out Buffer_Type)
      return Wide_Wide_String is
      Result : constant Wide_Wide_String :=
        To_Wide_Wide_String (Contents (Buffer));
   begin
      Reset (Buffer);
      return Result;
   end Wide_Wide_Get;

   function Get_UTF_8 (
      Buffer : in out Buffer_Type)
      return UTF_Encoding.UTF_8_String is
      Result : constant UTF_Encoding.UTF_8_String :=
        To_UTF_8 (Contents (Buffer));
   begin
      Reset (Buffer);
      return Result;
   end Get_UTF_8;

   function Wide_Get_UTF_16 (
      Buffer : in out Buffer_Type)
      return UTF_Encoding.UTF_16_Wide_String is
      Result : constant UTF_Encoding.UTF_16_Wide_String :=
        To_UTF_16 (Contents (Buffer));
   begin
      Reset (Buffer);
      return Result;
   end Wide_Get_UTF_16;

end Ada.Strings.Text_Buffers.Unbounded;
