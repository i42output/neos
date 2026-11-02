--  Ada.Strings.Text_Buffers.Bounded.adb
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

with Ada.Strings.UTF_Encoding.Wide_Wide_Strings;

package body Ada.Strings.Text_Buffers.Bounded is

   function Text_Truncated (Buffer : in Buffer_Type) return Boolean is
   begin
      return Buffer.Truncated;
   end Text_Truncated;

   procedure Store (Buffer : in out Buffer_Type; Text : Text_Array) is
      --  (the characters beyond Max_Characters are discarded)
      Count : Text_Buffer_Count := Text'Length;
   begin
      if Count > Buffer.Max_Characters - Buffer.Last then
         Count := Buffer.Max_Characters - Buffer.Last;
         Buffer.Truncated := True;
      end if;
      Buffer.Data (Buffer.Last + 1 .. Buffer.Last + Count) :=
        Text (Text'First .. Text'First + Count - 1);
      Buffer.Last := Buffer.Last + Count;
   end Store;

   function Contents (Buffer : Buffer_Type) return Text_Array is
   begin
      return Buffer.Data (1 .. Buffer.Last);
   end Contents;

   procedure Reset (Buffer : in out Buffer_Type) is
      --  (to the initial state: empty, no indentation, not truncated)
   begin
      Buffer.Last := 0;
      Buffer.Truncated := False;
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

end Ada.Strings.Text_Buffers.Bounded;
