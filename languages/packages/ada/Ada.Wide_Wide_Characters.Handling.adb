--  Ada.Wide_Wide_Characters.Handling.adb
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

with Ada.Wide_Wide_Characters.Unicode;

package body Ada.Wide_Wide_Characters.Handling is

   --  The classifications of RM A.3.5, from the Unicode Character Database
   --  (see Ada.Wide_Wide_Characters.Unicode)

   package U renames Ada.Wide_Wide_Characters.Unicode;
   use type U.Category;
   use type U.Code_Point;

   function Code (Item : Wide_Wide_Character) return U.Code_Point is
   begin
      return U.Code_Point (Wide_Wide_Character'Pos (Item));
   end Code;

   function Class (Item : Wide_Wide_Character) return U.Category is
   begin
      return U.Category_Of (Code (Item));
   end Class;

   function Character_Set_Version return String is
   begin
      return U.Version;
   end Character_Set_Version;

   function Is_Control (Item : Wide_Wide_Character) return Boolean is
   begin
      return Class (Item) = U.Control;
   end Is_Control;

   function Is_Letter (Item : Wide_Wide_Character) return Boolean is
   begin
      case Class (Item) is
         when U.Upper | U.Lower | U.Other_Letter =>
            return True;
         when others =>
            return False;
      end case;
   end Is_Letter;

   function Is_Lower (Item : Wide_Wide_Character) return Boolean is
   begin
      return Class (Item) = U.Lower;
   end Is_Lower;

   function Is_Upper (Item : Wide_Wide_Character) return Boolean is
   begin
      return Class (Item) = U.Upper;
   end Is_Upper;

   function Is_Basic (Item : Wide_Wide_Character) return Boolean is
   begin
      return not U.In_Runs (U.Basic, Code (Item));
   end Is_Basic;

   function Is_Digit (Item : Wide_Wide_Character) return Boolean is
   begin
      return Class (Item) = U.Digit;
   end Is_Digit;

   function Is_Hexadecimal_Digit (Item : Wide_Wide_Character) return Boolean is
   begin
      return Is_Digit (Item) or else Item in 'A' .. 'F'
        or else Item in 'a' .. 'f';
   end Is_Hexadecimal_Digit;

   function Is_Alphanumeric (Item : Wide_Wide_Character) return Boolean is
   begin
      case Class (Item) is
         when U.Upper | U.Lower | U.Other_Letter | U.Digit =>
            return True;
         when others =>
            return False;
      end case;
   end Is_Alphanumeric;

   function Is_Special (Item : Wide_Wide_Character) return Boolean is
   begin
      return Is_Graphic (Item) and then not Is_Alphanumeric (Item);
   end Is_Special;

   function Is_Line_Terminator (Item : Wide_Wide_Character) return Boolean is
   begin
      return Class (Item) = U.Line_Separator
        or else Code (Item) in 10 .. 13 or else Code (Item) = 133;
   end Is_Line_Terminator;

   function Is_Mark (Item : Wide_Wide_Character) return Boolean is
   begin
      return Class (Item) = U.Mark;
   end Is_Mark;

   function Is_Other_Format (Item : Wide_Wide_Character) return Boolean is
   begin
      return Class (Item) = U.Format;
   end Is_Other_Format;

   function Is_Punctuation_Connector (Item : Wide_Wide_Character)
     return Boolean is
   begin
      return Class (Item) = U.Connector;
   end Is_Punctuation_Connector;

   function Is_Space (Item : Wide_Wide_Character) return Boolean is
   begin
      return Class (Item) = U.Space;
   end Is_Space;

   function Is_NFKC (Item : Wide_Wide_Character) return Boolean is
   begin
      return U.Is_NFKC (Code (Item));
   end Is_NFKC;

   function Is_Graphic (Item : Wide_Wide_Character) return Boolean is
      --  (RM 2.1: not other_control, other_private_use, other_surrogate or
      --  format_effector, and not at position 16#FFFE# or 16#FFFF# in its
      --  plane)
   begin
      case Class (Item) is
         when U.Control | U.Private_Or_Surrogate | U.Line_Separator =>
            return False;
         when others =>
            return Code (Item) mod 16#1_0000# < 16#FFFE#;
      end case;
   end Is_Graphic;

   function To_Lower (Item : Wide_Wide_Character) return Wide_Wide_Character is
   begin
      return Wide_Wide_Character'Val (U.Map (U.Lower_Case, Code (Item)));
   end To_Lower;

   function To_Upper (Item : Wide_Wide_Character) return Wide_Wide_Character is
   begin
      return Wide_Wide_Character'Val (U.Map (U.Upper_Case, Code (Item)));
   end To_Upper;

   function To_Basic (Item : Wide_Wide_Character) return Wide_Wide_Character is
   begin
      return Wide_Wide_Character'Val (U.Map (U.Basic, Code (Item)));
   end To_Basic;

   function To_Lower (Item : Wide_Wide_String) return Wide_Wide_String is
      Result : Wide_Wide_String (1 .. Item'Length);
   begin
      for I in Item'Range loop
         Result (I - Item'First + 1) := To_Lower (Item (I));
      end loop;
      return Result;
   end To_Lower;

   function To_Upper (Item : Wide_Wide_String) return Wide_Wide_String is
      Result : Wide_Wide_String (1 .. Item'Length);
   begin
      for I in Item'Range loop
         Result (I - Item'First + 1) := To_Upper (Item (I));
      end loop;
      return Result;
   end To_Upper;

   function To_Basic (Item : Wide_Wide_String) return Wide_Wide_String is
      Result : Wide_Wide_String (1 .. Item'Length);
   begin
      for I in Item'Range loop
         Result (I - Item'First + 1) := To_Basic (Item (I));
      end loop;
      return Result;
   end To_Basic;

end Ada.Wide_Wide_Characters.Handling;
