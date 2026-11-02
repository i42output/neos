--  Ada.Strings.Text_Buffers.adb
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

package body Ada.Strings.Text_Buffers is

   function Current_Indent (
      Buffer : Root_Buffer_Type) return Text_Buffer_Count is
   begin
      return Buffer.Indentation;
   end Current_Indent;

   procedure Increase_Indent (
      Buffer : in out Root_Buffer_Type;
      Amount : in     Text_Buffer_Count := Standard_Indent) is
   begin
      Buffer.Indentation := Buffer.Indentation + Amount;
   end Increase_Indent;

   procedure Decrease_Indent (
      Buffer : in out Root_Buffer_Type;
      Amount : in     Text_Buffer_Count := Standard_Indent) is
   begin
      if Amount > Buffer.Indentation then
         raise Constraint_Error;
      end if;
      Buffer.Indentation := Buffer.Indentation - Amount;
   end Decrease_Indent;

   function Indent (Buffer : in out Root_Buffer_Type'Class)
     return Text_Array is
   begin
      if Buffer.At_Line_Start then
         Buffer.At_Line_Start := False;
         return (1 .. Buffer.Indentation => ' ');
      else
         return (1 .. 0 => ' ');
      end if;
   end Indent;

   function To_Text (Item : Wide_Wide_String) return Text_Array is
      Result : Text_Array (1 .. Item'Length);
   begin
      for I in Result'Range loop
         Result (I) := Item (Item'First + Integer (I) - 1);
      end loop;
      return Result;
   end To_Text;

   function To_Wide_Wide_String (Text : Text_Array) return Wide_Wide_String is
      Result : Wide_Wide_String (1 .. Text'Length);
   begin
      for I in Result'Range loop
         Result (I) := Text (Text'First + Text_Buffer_Count (I) - 1);
      end loop;
      return Result;
   end To_Wide_Wide_String;

   function Encodable (Text : Text_Array) return Wide_Wide_String is
      --  Text, with U+FFFD for the code points that have no UTF encoding
      Result : Wide_Wide_String := To_Wide_Wide_String (Text);
   begin
      for I in Result'Range loop
         case Wide_Wide_Character'Pos (Result (I)) is
            when 16#D800# .. 16#DFFF# | 16#11_0000# .. Integer'Last =>
               Result (I) := Wide_Wide_Character'Val (16#FFFD#);
            when others =>
               null;
         end case;
      end loop;
      return Result;
   end Encodable;

   function To_UTF_8 (Text : Text_Array) return UTF_Encoding.UTF_8_String is
   begin
      return UTF_Encoding.Wide_Wide_Strings.Encode (Encodable (Text));
   end To_UTF_8;

   function To_UTF_16 (Text : Text_Array)
     return UTF_Encoding.UTF_16_Wide_String is
   begin
      return UTF_Encoding.Wide_Wide_Strings.Encode (Encodable (Text));
   end To_UTF_16;

   function To_String (Text : Text_Array) return String is
      Result : String (1 .. Text'Length);
   begin
      for I in Text'Range loop
         if Wide_Wide_Character'Pos (Text (I)) > 16#FF# then
            return To_UTF_8 (Text);
         end if;
         Result (Integer (I - Text'First) + 1) :=
           Character'Val (Wide_Wide_Character'Pos (Text (I)));
      end loop;
      return Result;
   end To_String;

   function To_Wide_String (Text : Text_Array) return Wide_String is
      Result : Wide_String (1 .. Text'Length);
   begin
      for I in Text'Range loop
         if Wide_Wide_Character'Pos (Text (I)) > 16#FFFF# then
            return To_UTF_16 (Text);
         end if;
         Result (Integer (I - Text'First) + 1) :=
           Wide_Character'Val (Wide_Wide_Character'Pos (Text (I)));
      end loop;
      return Result;
   end To_Wide_String;

end Ada.Strings.Text_Buffers;
