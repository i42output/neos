--  Ada.Strings.UTF_Encoding.Wide_Strings.adb
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

package body Ada.Strings.UTF_Encoding.Wide_Strings is

   --  Through Wide_Wide_Strings

   package WW renames Ada.Strings.UTF_Encoding.Wide_Wide_Strings;

   function Wide_Wide (Item : Wide_String) return Wide_Wide_String is
      --  (the surrogates and 16#FFFE# and 16#FFFF# are invalid characters)
      Result : Wide_Wide_String (1 .. Item'Length);
   begin
      for I in Item'Range loop
         case Wide_Character'Pos (Item (I)) is
            when 16#D800# .. 16#DFFF# | 16#FFFE# | 16#FFFF# =>
               raise Encoding_Error;
            when others =>
               Result (I - Item'First + 1) :=
                 Wide_Wide_Character'Val (Wide_Character'Pos (Item (I)));
         end case;
      end loop;
      return Result;
   end Wide_Wide;

   function Wide (Item : Wide_Wide_String) return Wide_String is
      Result : Wide_String (1 .. Item'Length);
   begin
      for I in Item'Range loop
         if Wide_Wide_Character'Pos (Item (I)) > 16#FFFF# then
            raise Encoding_Error;
         end if;
         Result (I - Item'First + 1) :=
           Wide_Character'Val (Wide_Wide_Character'Pos (Item (I)));
      end loop;
      return Result;
   end Wide;

   function Encode (Item          : Wide_String;
                    Output_Scheme : Encoding_Scheme;
                    Output_BOM    : Boolean  := False) return UTF_String is
   begin
      return WW.Encode (Wide_Wide (Item), Output_Scheme, Output_BOM);
   end Encode;

   function Encode (Item       : Wide_String;
                    Output_BOM : Boolean  := False) return UTF_8_String is
   begin
      return WW.Encode (Wide_Wide (Item), Output_BOM);
   end Encode;

   function Encode (Item       : Wide_String;
                    Output_BOM : Boolean  := False)
      return UTF_16_Wide_String is
   begin
      return WW.Encode (Wide_Wide (Item), Output_BOM);
   end Encode;

   function Decode (Item         : UTF_String;
                    Input_Scheme : Encoding_Scheme) return Wide_String is
   begin
      return Wide (WW.Decode (Item, Input_Scheme));
   end Decode;

   function Decode (Item : UTF_8_String) return Wide_String is
   begin
      return Wide (WW.Decode (Item));
   end Decode;

   function Decode (Item : UTF_16_Wide_String) return Wide_String is
   begin
      return Wide (WW.Decode (Item));
   end Decode;

end Ada.Strings.UTF_Encoding.Wide_Strings;
