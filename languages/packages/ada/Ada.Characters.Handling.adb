--  Ada.Characters.Handling.adb
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

package body Ada.Characters.Handling is

   --  The classifications of RM A.3.2, by character position (ISO 8859-1)

   function Pos (Item : Character) return Natural is
   begin
      return Character'Pos (Item);
   end Pos;

   function Is_Control (Item : in Character) return Boolean is
   begin
      return Pos (Item) <= 31 or else Pos (Item) in 127 .. 159;
   end Is_Control;

   function Is_Graphic (Item : in Character) return Boolean is
   begin
      return Pos (Item) in 32 .. 126 or else Pos (Item) >= 160;
   end Is_Graphic;

   function Is_Letter (Item : in Character) return Boolean is
   begin
      return Item in 'A' .. 'Z' or else Item in 'a' .. 'z'
        or else Pos (Item) in 192 .. 214 or else Pos (Item) in 216 .. 246
        or else Pos (Item) >= 248;
   end Is_Letter;

   function Is_Lower (Item : in Character) return Boolean is
   begin
      return Item in 'a' .. 'z' or else Pos (Item) in 223 .. 246
        or else Pos (Item) >= 248;
   end Is_Lower;

   function Is_Upper (Item : in Character) return Boolean is
   begin
      return Item in 'A' .. 'Z' or else Pos (Item) in 192 .. 214
        or else Pos (Item) in 216 .. 222;
   end Is_Upper;

   function Is_Basic (Item : in Character) return Boolean is
   begin
      case Pos (Item) is
         when 198 | 230 | 208 | 240 | 222 | 254 | 223 =>
            return True;
         when others =>
            return Item in 'A' .. 'Z' or else Item in 'a' .. 'z';
      end case;
   end Is_Basic;

   function Is_Digit (Item : in Character) return Boolean is
   begin
      return Item in '0' .. '9';
   end Is_Digit;

   function Is_Hexadecimal_Digit (Item : in Character) return Boolean is
   begin
      return Item in '0' .. '9' or else Item in 'A' .. 'F'
        or else Item in 'a' .. 'f';
   end Is_Hexadecimal_Digit;

   function Is_Alphanumeric (Item : in Character) return Boolean is
   begin
      return Is_Letter (Item) or else Is_Digit (Item);
   end Is_Alphanumeric;

   function Is_Special (Item : in Character) return Boolean is
   begin
      return Is_Graphic (Item) and then not Is_Alphanumeric (Item);
   end Is_Special;

   function Is_Line_Terminator (Item : in Character) return Boolean is
   begin
      return Pos (Item) in 10 .. 13 or else Pos (Item) = 133;
   end Is_Line_Terminator;

   function Is_Mark (Item : in Character) return Boolean is
   begin
      return False;  --  (no Character is a mark)
   end Is_Mark;

   function Is_Other_Format (Item : in Character) return Boolean is
   begin
      return Pos (Item) = 173;
   end Is_Other_Format;

   function Is_Punctuation_Connector (Item : in Character) return Boolean is
   begin
      return Item = '_';
   end Is_Punctuation_Connector;

   function Is_Space (Item : in Character) return Boolean is
   begin
      return Pos (Item) = 32 or else Pos (Item) = 160;
   end Is_Space;

   function Is_NFKC (Item : in Character) return Boolean is
   begin
      case Pos (Item) is
         when 160 | 168 | 170 | 175 | 178 | 179 | 180 | 181 | 184 | 185
            | 186 | 188 | 189 | 190 =>
            return False;
         when others =>
            return True;
      end case;
   end Is_NFKC;

   --  Conversions

   function To_Lower (Item : in Character) return Character is
   begin
      if Is_Upper (Item) then
         return Character'Val (Pos (Item) + 32);
      else
         return Item;
      end if;
   end To_Lower;

   function To_Upper (Item : in Character) return Character is
   begin
      --  (the lower case letters with position 223 and 255 have no upper
      --  case form in Character)
      if Is_Lower (Item) and then Pos (Item) /= 223 and then Pos (Item) /= 255
      then
         return Character'Val (Pos (Item) - 32);
      else
         return Item;
      end if;
   end To_Upper;

   function To_Basic (Item : in Character) return Character is
   begin
      case Pos (Item) is
         when 192 .. 197 => return 'A';
         when 199        => return 'C';
         when 200 .. 203 => return 'E';
         when 204 .. 207 => return 'I';
         when 209        => return 'N';
         when 210 .. 214 | 216 => return 'O';
         when 217 .. 220 => return 'U';
         when 221        => return 'Y';
         when 224 .. 229 => return 'a';
         when 231        => return 'c';
         when 232 .. 235 => return 'e';
         when 236 .. 239 => return 'i';
         when 241        => return 'n';
         when 242 .. 246 | 248 => return 'o';
         when 249 .. 252 => return 'u';
         when 253 | 255  => return 'y';
         when others     => return Item;
      end case;
   end To_Basic;

   function To_Lower (Item : in String) return String is
      Result : String (1 .. Item'Length);
   begin
      for I in Item'Range loop
         Result (I - Item'First + 1) := To_Lower (Item (I));
      end loop;
      return Result;
   end To_Lower;

   function To_Upper (Item : in String) return String is
      Result : String (1 .. Item'Length);
   begin
      for I in Item'Range loop
         Result (I - Item'First + 1) := To_Upper (Item (I));
      end loop;
      return Result;
   end To_Upper;

   function To_Basic (Item : in String) return String is
      Result : String (1 .. Item'Length);
   begin
      for I in Item'Range loop
         Result (I - Item'First + 1) := To_Basic (Item (I));
      end loop;
      return Result;
   end To_Basic;

   --  ISO 646

   function Is_ISO_646 (Item : in Character) return Boolean is
   begin
      return Item in ISO_646;
   end Is_ISO_646;

   function Is_ISO_646 (Item : in String) return Boolean is
   begin
      for I in Item'Range loop
         if Item (I) not in ISO_646 then
            return False;
         end if;
      end loop;
      return True;
   end Is_ISO_646;

   function To_ISO_646 (Item       : in Character;
                        Substitute : in ISO_646 := ' ')
     return ISO_646 is
   begin
      if Item in ISO_646 then
         return Item;
      else
         return Substitute;
      end if;
   end To_ISO_646;

   function To_ISO_646 (Item       : in String;
                        Substitute : in ISO_646 := ' ')
     return String is
      Result : String (1 .. Item'Length);
   begin
      for I in Item'Range loop
         Result (I - Item'First + 1) := To_ISO_646 (Item (I), Substitute);
      end loop;
      return Result;
   end To_ISO_646;

end Ada.Characters.Handling;
