--  Ada.Wide_Characters.Handling.adb
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

with Ada.Wide_Wide_Characters.Handling;

package body Ada.Wide_Characters.Handling is

   --  Each function is that of Ada.Wide_Wide_Characters.Handling applied to
   --  the corresponding Wide_Wide_Character.

   package WW renames Ada.Wide_Wide_Characters.Handling;

   function Wide_Wide (Item : Wide_Character) return Wide_Wide_Character is
   begin
      return Wide_Wide_Character'Val (Wide_Character'Pos (Item));
   end Wide_Wide;

   function Wide (Item : Wide_Wide_Character; Default : Wide_Character)
     return Wide_Character is
      --  (the case mappings of a character of the BMP are in the BMP, but a
      --  few decomposition mappings are not: Default for those)
   begin
      if Wide_Wide_Character'Pos (Item) > Wide_Character'Pos (Wide_Character'Last)
      then
         return Default;
      end if;
      return Wide_Character'Val (Wide_Wide_Character'Pos (Item));
   end Wide;

   function Character_Set_Version return String is
   begin
      return WW.Character_Set_Version;
   end Character_Set_Version;

   function Is_Control (Item : Wide_Character) return Boolean is
   begin
      return WW.Is_Control (Wide_Wide (Item));
   end Is_Control;

   function Is_Letter (Item : Wide_Character) return Boolean is
   begin
      return WW.Is_Letter (Wide_Wide (Item));
   end Is_Letter;

   function Is_Lower (Item : Wide_Character) return Boolean is
   begin
      return WW.Is_Lower (Wide_Wide (Item));
   end Is_Lower;

   function Is_Upper (Item : Wide_Character) return Boolean is
   begin
      return WW.Is_Upper (Wide_Wide (Item));
   end Is_Upper;

   function Is_Basic (Item : Wide_Character) return Boolean is
   begin
      return WW.Is_Basic (Wide_Wide (Item));
   end Is_Basic;

   function Is_Digit (Item : Wide_Character) return Boolean is
   begin
      return WW.Is_Digit (Wide_Wide (Item));
   end Is_Digit;

   function Is_Hexadecimal_Digit (Item : Wide_Character) return Boolean is
   begin
      return WW.Is_Hexadecimal_Digit (Wide_Wide (Item));
   end Is_Hexadecimal_Digit;

   function Is_Alphanumeric (Item : Wide_Character) return Boolean is
   begin
      return WW.Is_Alphanumeric (Wide_Wide (Item));
   end Is_Alphanumeric;

   function Is_Special (Item : Wide_Character) return Boolean is
   begin
      return WW.Is_Special (Wide_Wide (Item));
   end Is_Special;

   function Is_Line_Terminator (Item : Wide_Character) return Boolean is
   begin
      return WW.Is_Line_Terminator (Wide_Wide (Item));
   end Is_Line_Terminator;

   function Is_Mark (Item : Wide_Character) return Boolean is
   begin
      return WW.Is_Mark (Wide_Wide (Item));
   end Is_Mark;

   function Is_Other_Format (Item : Wide_Character) return Boolean is
   begin
      return WW.Is_Other_Format (Wide_Wide (Item));
   end Is_Other_Format;

   function Is_Punctuation_Connector (Item : Wide_Character) return Boolean is
   begin
      return WW.Is_Punctuation_Connector (Wide_Wide (Item));
   end Is_Punctuation_Connector;

   function Is_Space (Item : Wide_Character) return Boolean is
   begin
      return WW.Is_Space (Wide_Wide (Item));
   end Is_Space;

   function Is_NFKC (Item : Wide_Character) return Boolean is
   begin
      return WW.Is_NFKC (Wide_Wide (Item));
   end Is_NFKC;

   function Is_Graphic (Item : Wide_Character) return Boolean is
   begin
      return WW.Is_Graphic (Wide_Wide (Item));
   end Is_Graphic;

   function To_Lower (Item : Wide_Character) return Wide_Character is
   begin
      return Wide (WW.To_Lower (Wide_Wide (Item)), Item);
   end To_Lower;

   function To_Upper (Item : Wide_Character) return Wide_Character is
   begin
      return Wide (WW.To_Upper (Wide_Wide (Item)), Item);
   end To_Upper;

   function To_Basic (Item : Wide_Character) return Wide_Character is
   begin
      return Wide (WW.To_Basic (Wide_Wide (Item)), Item);
   end To_Basic;

   function To_Lower (Item : Wide_String) return Wide_String is
      Result : Wide_String (1 .. Item'Length);
   begin
      for I in Item'Range loop
         Result (I - Item'First + 1) := To_Lower (Item (I));
      end loop;
      return Result;
   end To_Lower;

   function To_Upper (Item : Wide_String) return Wide_String is
      Result : Wide_String (1 .. Item'Length);
   begin
      for I in Item'Range loop
         Result (I - Item'First + 1) := To_Upper (Item (I));
      end loop;
      return Result;
   end To_Upper;

   function To_Basic (Item : Wide_String) return Wide_String is
      Result : Wide_String (1 .. Item'Length);
   begin
      for I in Item'Range loop
         Result (I - Item'First + 1) := To_Basic (Item (I));
      end loop;
      return Result;
   end To_Basic;

end Ada.Wide_Characters.Handling;
