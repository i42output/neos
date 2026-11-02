--  Ada.Text_IO_Numbers.adb
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

with Ada.IO_Exceptions;
with Ada.Native_IO;

package body Ada.Text_IO_Numbers is

   use type Interfaces.Integer_64;
   use type Interfaces.Unsigned_64;

   Data_Error   : exception renames IO_Exceptions.Data_Error;
   End_Error    : exception renames IO_Exceptions.End_Error;
   Layout_Error : exception renames IO_Exceptions.Layout_Error;

   Space : constant Int := 32;
   Tab   : constant Int := 9;

   ---------------------------------------------------------------------------
   --  Scanning

   type Scan_State is
     (Start,          --  nothing yet
      Sign,           --  a sign
      Numeral,        --  digits (the last a digit)
      Numeral_Line,   --  digits and a low line
      Point,          --  digits and a point
      Lead_Point,     --  [sign] point
      Fraction,       --  digits after the point (the last a digit)
      Fraction_Line,
      Based_Start,    --  base and #
      Based,          --  base # extended digits
      Based_Line,
      Based_Point,    --  base # extended digits .
      Based_Lead,     --  base # .
      Based_Fraction, --  base # [extended digits] . extended digits
      Based_Fraction_Line,
      Based_End,      --  base # ... #
      Exponent_Start, --  ... E
      Exponent_Sign,  --  ... E sign
      Exponent,       --  ... E [sign] digits
      Exponent_Line,
      Identifier,     --  letters, digits (the last not a low line)
      Identifier_Line,
      Character_Start,  --  '
      Character_Graphic,  --  ' graphic character
      Character_End,  --  ' graphic character '
      Failed);

   type Scanner is record
      Kind      : Literal_Kind;
      State     : Scan_State := Start;
      Delimiter : Int := 0;  --  (# or :)
   end record;

   function Is_Digit (C : Int) return Boolean is
     (C in Character'Pos ('0') .. Character'Pos ('9'));

   function Is_Extended_Digit (C : Int) return Boolean is
     (Is_Digit (C) or else C in Character'Pos ('A') .. Character'Pos ('F')
        or else C in Character'Pos ('a') .. Character'Pos ('f'));

   --  The characters of identifiers: letters (and, beyond Latin-1, any
   --  character that is not punctuation or a symbol of the blocks of
   --  U+2000 .. U+2BFF and U+3000 .. U+3003).
   function Is_Letter (C : Int) return Boolean is
     (C in Character'Pos ('A') .. Character'Pos ('Z')
        or else C in Character'Pos ('a') .. Character'Pos ('z')
        or else C = 16#AA# or else C = 16#B5# or else C = 16#BA#
        or else (C in 16#C0# .. 16#FF# and then C /= 16#D7#
                   and then C /= 16#F7#)
        or else (C > 16#FF# and then C not in 16#2000# .. 16#2BFF#
                   and then C not in 16#3000# .. 16#3003#
                   and then C not in 16#D800# .. 16#DFFF#
                   and then C <= 16#10_FFFF#));

   function Is_Graphic (C : Int) return Boolean is
     (C in 32 .. 126 or else C in 160 .. 16#7FFF_FFFF#);

   function Is_Exponent_Letter (C : Int) return Boolean is
     (C = Character'Pos ('E') or else C = Character'Pos ('e'));

   --  Whether C continues the literal (S.State is then updated).
   function Accept_Character (S : in out Scanner; C : Int) return Boolean
   is
      Real  : constant Boolean := S.Kind = Real_Literal;
      Next  : Scan_State := Failed;
   begin
      case S.State is
         when Start =>
            if S.Kind = Enumeration_Literal then
               if Is_Letter (C) then
                  Next := Identifier;
               elsif C = Character'Pos (''') then
                  Next := Character_Start;
               end if;
            elsif Is_Digit (C) then
               Next := Numeral;
            elsif C = Character'Pos ('+')
              or else (C = Character'Pos ('-')
                         and then S.Kind /= Unsigned_Literal)
            then
               Next := Sign;
            elsif Real and then C = Character'Pos ('.') then
               Next := Lead_Point;
            end if;
         when Sign =>
            if Is_Digit (C) then
               Next := Numeral;
            elsif Real and then C = Character'Pos ('.') then
               Next := Lead_Point;
            end if;
         when Numeral =>
            if Is_Digit (C) then
               Next := Numeral;
            elsif C = Character'Pos ('_') then
               Next := Numeral_Line;
            elsif C = Character'Pos ('#') or else C = Character'Pos (':') then
               S.Delimiter := C;
               Next := Based_Start;
            elsif Real and then C = Character'Pos ('.') then
               Next := Point;
            elsif Is_Exponent_Letter (C) then
               Next := Exponent_Start;
            end if;
         when Numeral_Line =>
            if Is_Digit (C) then
               Next := Numeral;
            end if;
         when Point =>
            if Is_Digit (C) then
               Next := Fraction;
            elsif Is_Exponent_Letter (C) then
               Next := Exponent_Start;
            end if;
         when Lead_Point =>
            if Is_Digit (C) then
               Next := Fraction;
            end if;
         when Fraction =>
            if Is_Digit (C) then
               Next := Fraction;
            elsif C = Character'Pos ('_') then
               Next := Fraction_Line;
            elsif Is_Exponent_Letter (C) then
               Next := Exponent_Start;
            end if;
         when Fraction_Line =>
            if Is_Digit (C) then
               Next := Fraction;
            end if;
         when Based_Start =>
            if Is_Extended_Digit (C) then
               Next := Based;
            elsif Real and then C = Character'Pos ('.') then
               Next := Based_Lead;
            end if;
         when Based =>
            if Is_Extended_Digit (C) then
               Next := Based;
            elsif C = Character'Pos ('_') then
               Next := Based_Line;
            elsif Real and then C = Character'Pos ('.') then
               Next := Based_Point;
            elsif C = S.Delimiter then
               Next := Based_End;
            end if;
         when Based_Line =>
            if Is_Extended_Digit (C) then
               Next := Based;
            end if;
         when Based_Point =>
            if Is_Extended_Digit (C) then
               Next := Based_Fraction;
            elsif C = S.Delimiter then
               Next := Based_End;
            end if;
         when Based_Lead =>
            if Is_Extended_Digit (C) then
               Next := Based_Fraction;
            end if;
         when Based_Fraction =>
            if Is_Extended_Digit (C) then
               Next := Based_Fraction;
            elsif C = Character'Pos ('_') then
               Next := Based_Fraction_Line;
            elsif C = S.Delimiter then
               Next := Based_End;
            end if;
         when Based_Fraction_Line =>
            if Is_Extended_Digit (C) then
               Next := Based_Fraction;
            end if;
         when Based_End =>
            if Is_Exponent_Letter (C) then
               Next := Exponent_Start;
            end if;
         when Exponent_Start =>
            if Is_Digit (C) then
               Next := Exponent;
            elsif C = Character'Pos ('+') or else C = Character'Pos ('-') then
               Next := Exponent_Sign;
            end if;
         when Exponent_Sign =>
            if Is_Digit (C) then
               Next := Exponent;
            end if;
         when Exponent =>
            if Is_Digit (C) then
               Next := Exponent;
            elsif C = Character'Pos ('_') then
               Next := Exponent_Line;
            end if;
         when Exponent_Line =>
            if Is_Digit (C) then
               Next := Exponent;
            end if;
         when Identifier =>
            if Is_Letter (C) or else Is_Digit (C) then
               Next := Identifier;
            elsif C = Character'Pos ('_') then
               Next := Identifier_Line;
            end if;
         when Identifier_Line =>
            if Is_Letter (C) or else Is_Digit (C) then
               Next := Identifier;
            end if;
         when Character_Start =>
            if Is_Graphic (C) then
               Next := Character_Graphic;
            end if;
         when Character_Graphic =>
            if C = Character'Pos (''') then
               Next := Character_End;
            end if;
         when Character_End | Failed =>
            null;
      end case;
      if Next = Failed then
         return False;
      end if;
      S.State := Next;
      return True;
   end Accept_Character;

   --  Whether the characters accepted form a literal.
   function Complete (S : Scanner) return Boolean is
   begin
      case S.Kind is
         when Signed_Literal | Unsigned_Literal =>
            return S.State in Numeral | Based_End | Exponent;
         when Real_Literal =>
            return S.State in
                     Numeral | Point | Fraction | Based_End | Exponent;
         when Enumeration_Literal =>
            return S.State in Identifier | Character_End;
      end case;
   end Complete;

   function Is_Blank (C : Int) return Boolean is (C = Space or else C = Tab);

   procedure Get_Literal
     (From  : Wide_Wide_String;
      Kind  : Literal_Kind;
      First : out Positive;
      Last  : out Natural)
   is
      S : Scanner;
      I : Natural := From'First;
   begin
      S.Kind := Kind;
      while I <= From'Last
        and then Is_Blank (Wide_Wide_Character'Pos (From (I)))
      loop
         I := I + 1;
      end loop;
      if I > From'Last then
         raise End_Error;
      end if;
      First := I;
      Last := I - 1;
      while Last < From'Last
        and then Accept_Character
                   (S, Wide_Wide_Character'Pos (From (Last + 1)))
      loop
         Last := Last + 1;
      end loop;
      if not Complete (S) then
         raise Data_Error;
      end if;
   end Get_Literal;

   function Get_Literal
     (File  : Text_IO_Engine.File_Access;
      Kind  : Literal_Kind;
      Width : Natural := 0) return Wide_Wide_String
   is
      use Text_IO_Engine;
      Buffer : Wide_Wide_String (1 .. 512);
      Last   : Natural := 0;
      Long   : Boolean := False;  --  (more characters than Buffer holds)
      S      : Scanner;
      C      : Int;

      procedure Add is
      begin
         if Last < Buffer'Last then
            Last := Last + 1;
            Buffer (Last) := Wide_Wide_Character'Val (C);
         else
            Long := True;
         end if;
         Skip (File);
      end Add;
   begin
      Check_Input (File);
      S.Kind := Kind;
      if Width = 0 then
         Skip_Blanks (File);
         loop
            C := Peek (File);
            exit when C = End_Of_Input or else C = Line_Feed
              or else not Accept_Character (S, C);
            Add;
         end loop;
         if Long or else not Complete (S) then
            raise Data_Error;
         end if;
         return Buffer (1 .. Last);
      end if;
      if Peek (File) = End_Of_Input then
         raise End_Error;
      end if;
      for K in 1 .. Width loop
         C := Peek (File);
         exit when C = End_Of_Input or else C = Line_Feed;
         Add;
      end loop;
      if Long then
         raise Data_Error;
      end if;
      declare
         First : Positive;
         Stop  : Natural;
      begin
         begin
            Get_Literal (Buffer (1 .. Last), Kind, First, Stop);
         exception
            when End_Error =>  --  (only blanks)
               raise Data_Error;
         end;
         for I in Stop + 1 .. Last loop
            if not Is_Blank (Wide_Wide_Character'Pos (Buffer (I))) then
               raise Data_Error;
            end if;
         end loop;
         return Buffer (First .. Stop);
      end;
   end Get_Literal;

   function To_Wide_Wide (Item : String) return Wide_Wide_String is
      Result : Wide_Wide_String (Item'Range);
   begin
      for I in Item'Range loop
         Result (I) := Wide_Wide_Character'Val (Character'Pos (Item (I)));
      end loop;
      return Result;
   end To_Wide_Wide;

   function To_Wide_Wide (Item : Wide_String) return Wide_Wide_String is
      Result : Wide_Wide_String (Item'Range);
   begin
      for I in Item'Range loop
         Result (I) :=
            Wide_Wide_Character'Val (Wide_Character'Pos (Item (I)));
      end loop;
      return Result;
   end To_Wide_Wide;

   function To_String (Item : Wide_Wide_String) return String is
      Result : String (Item'Range);
      Code   : Natural;
   begin
      for I in Item'Range loop
         Code := Wide_Wide_Character'Pos (Item (I));
         Result (I) := (if Code < 256 then Character'Val (Code)
                        else Character'Val (0));
      end loop;
      return Result;
   end To_String;

   ---------------------------------------------------------------------------
   --  The parts of a (well formed) numeric literal

   type Literal_Parts is record
      Negative       : Boolean := False;
      Base           : Natural := 10;
      Mantissa_First : Positive := 1;   --  (the digits, with low lines and
      Mantissa_Last  : Natural := 0;    --  a point)
      Exponent       : Int := 0;
   end record;

   function Digit_Value (C : Character) return Natural is
   begin
      case C is
         when '0' .. '9' => return Character'Pos (C) - Character'Pos ('0');
         when 'A' .. 'F' =>
            return Character'Pos (C) - Character'Pos ('A') + 10;
         when 'a' .. 'f' =>
            return Character'Pos (C) - Character'Pos ('a') + 10;
         when others     => return 99;
      end case;
   end Digit_Value;

   function Parts (Literal : String) return Literal_Parts is
      P : Literal_Parts;
      I : Natural := Literal'First;
      B : Natural := 0;
   begin
      if I <= Literal'Last and then Literal (I) in '+' | '-' then
         P.Negative := Literal (I) = '-';
         I := I + 1;
      end if;
      P.Mantissa_First := I;
      while I <= Literal'Last and then Literal (I) in '0' .. '9' | '_' | '.'
      loop
         if Literal (I) in '0' .. '9' and then B < 100 then
            B := B * 10 + Digit_Value (Literal (I));
         end if;
         I := I + 1;
      end loop;
      P.Mantissa_Last := I - 1;
      if I <= Literal'Last and then Literal (I) in '#' | ':' then
         if B not in 2 .. 16 then
            raise Data_Error;
         end if;
         P.Base := B;
         I := I + 1;
         P.Mantissa_First := I;
         while Literal (I) not in '#' | ':' loop
            if Literal (I) /= '_' and then Literal (I) /= '.'
              and then Digit_Value (Literal (I)) >= B
            then
               raise Data_Error;
            end if;
            I := I + 1;
         end loop;
         P.Mantissa_Last := I - 1;
         I := I + 1;
      end if;
      if I <= Literal'Last then  --  E [sign] digits
         I := I + 1;
         declare
            Negative : Boolean := False;
         begin
            if Literal (I) in '+' | '-' then
               Negative := Literal (I) = '-';
               I := I + 1;
            end if;
            while I <= Literal'Last loop
               if Literal (I) /= '_' and then P.Exponent < 1_000_000_000 then
                  P.Exponent :=
                     P.Exponent * 10 + Int (Digit_Value (Literal (I)));
               end if;
               I := I + 1;
            end loop;
            if Negative then
               P.Exponent := -P.Exponent;
            end if;
         end;
      end if;
      return P;
   end Parts;

   procedure Integer_Value
     (Literal  : String;
      Value    : out Uns;
      Negative : out Boolean)
   is
      P     : constant Literal_Parts := Parts (Literal);
      Base  : constant Uns := Uns (P.Base);
      Limit : constant Uns := Uns'Last / Base;
      D     : Uns;
   begin
      Negative := P.Negative;
      Value := 0;
      for I in P.Mantissa_First .. P.Mantissa_Last loop
         if Literal (I) /= '_' then
            D := Uns (Digit_Value (Literal (I)));
            if Value > Limit or else Value * Base > Uns'Last - D then
               raise Data_Error;
            end if;
            Value := Value * Base + D;
         end if;
      end loop;
      if Value /= 0 then
         if P.Exponent < 0 then
            raise Data_Error;
         end if;
         for K in 1 .. P.Exponent loop
            if Value > Limit then
               raise Data_Error;
            end if;
            Value := Value * Base;
         end loop;
      end if;
   end Integer_Value;

   function Image (Value : Int) return String is
      S : constant String := Int'Image (Value);
   begin
      return (if S (S'First) = ' ' then S (S'First + 1 .. S'Last) else S);
   end Image;

   function Digits_Image (Value : Uns; Base : Positive) return String;

   function Real_Value (Literal : String) return Long_Float is
      P : constant Literal_Parts := Parts (Literal);
   begin
      if P.Base = 10 then
         declare
            Text : String (1 .. Literal'Length + 24);
            Last : Natural := 0;
         begin
            if P.Negative then
               Last := 1;
               Text (1) := '-';
            end if;
            for I in P.Mantissa_First .. P.Mantissa_Last loop
               if Literal (I) /= '_' then
                  Last := Last + 1;
                  Text (Last) := Literal (I);
               end if;
            end loop;
            declare
               E : constant String := "e" & Image (P.Exponent);
            begin
               Text (Last + 1 .. Last + E'Length) := E;
               Last := Last + E'Length;
            end;
            return Native_IO.Float_Value (Text (1 .. Last));
         end;
      end if;
      declare
         Bits       : constant array (2 .. 16) of Natural :=
            (2 => 1, 4 => 2, 8 => 3, 16 => 4, others => 0);
         Mantissa   : Uns := 0;
         Sticky     : Boolean := False;
         Scale      : Int := 0;  --  (the exponent of the last digit)
         After      : Boolean := False;
         Value      : Long_Float := 0.0;
         Base       : constant Long_Float := Long_Float (P.Base);
         D          : Natural;
      begin
         for I in P.Mantissa_First .. P.Mantissa_Last loop
            if Literal (I) = '.' then
               After := True;
            elsif Literal (I) /= '_' then
               D := Digit_Value (Literal (I));
               if Bits (P.Base) /= 0 then
                  if Mantissa < 2 ** 56 then
                     Mantissa := Mantissa * Uns (P.Base) + Uns (D);
                     if After then
                        Scale := Scale - 1;
                     end if;
                  else
                     Sticky := Sticky or else D /= 0;
                     if not After then
                        Scale := Scale + 1;
                     end if;
                  end if;
               else
                  Value := Value * Base + Long_Float (D);
                  if After then
                     Scale := Scale - 1;
                  end if;
               end if;
            end if;
         end loop;
         if Bits (P.Base) = 0 then
            Scale := Scale + P.Exponent;
            if Scale > 4000 then
               Scale := 4000;
            elsif Scale < -4000 then
               Scale := -4000;
            end if;
            if Scale >= 0 then
               Value := Value * Base ** Natural (Scale);
            else
               Value := Value / Base ** Natural (-Scale);
            end if;
            return (if P.Negative then -Value else Value);
         end if;
         if Sticky then
            Mantissa := Mantissa * 2 + 1;  --  (a bit below the last digit)
            Scale := Scale * Int (Bits (P.Base)) - 1;
         else
            Scale := Scale * Int (Bits (P.Base));
         end if;
         Scale := Scale + P.Exponent * Int (Bits (P.Base));
         return Native_IO.Float_Value
                  ((if P.Negative then "-0x" else "0x")
                   & Digits_Image (Mantissa, 16) & "p" & Image (Scale));
      end;
   end Real_Value;

   ---------------------------------------------------------------------------
   --  Arithmetic on natural numbers of any size, represented by their
   --  decimal images (without leading zeros; "" for zero)

   function Trim_Zeros (N : String) return String is
   begin
      for I in N'Range loop
         if N (I) /= '0' then
            return N (I .. N'Last);
         end if;
      end loop;
      return "";
   end Trim_Zeros;

   function Big (Value : Uns) return String is
      S : constant String := Uns'Image (Value);
   begin
      return Trim_Zeros (S (S'First + 1 .. S'Last));
   end Big;

   --  N * Factor (Factor <= 2 ** 59)
   function Multiply (N : String; Factor : Int) return String is
      Result : String (1 .. N'Length + 19);
      Carry  : Int := 0;
      J      : Natural := Result'Last;
      T      : Int;
   begin
      for I in reverse N'Range loop
         T := Int (Digit_Value (N (I))) * Factor + Carry;
         Result (J) := Character'Val (Character'Pos ('0') + T mod 10);
         Carry := T / 10;
         J := J - 1;
      end loop;
      while Carry > 0 loop
         Result (J) := Character'Val (Character'Pos ('0') + Carry mod 10);
         Carry := Carry / 10;
         J := J - 1;
      end loop;
      return Trim_Zeros (Result (J + 1 .. Result'Last));
   end Multiply;

   --  N / Divisor and its remainder (Divisor <= 2 ** 59)
   procedure Divide
     (N         : String;
      Divisor   : Int;
      Quotient  : out String;  --  (of N's length)
      Remainder : out Int) is
   begin
      Remainder := 0;
      for I in N'Range loop
         Remainder := Remainder * 10 + Int (Digit_Value (N (I)));
         Quotient (Quotient'First + I - N'First) :=
            Character'Val (Character'Pos ('0') + Remainder / Divisor);
         Remainder := Remainder mod Divisor;
      end loop;
   end Divide;

   function To_Uns (N : String) return Uns is
      Value : Uns := 0;
   begin
      if N'Length > 20 then
         raise Data_Error;
      end if;
      for C of N loop
         if Value > (Uns'Last - 9) / 10 then
            raise Data_Error;
         end if;
         Value := Value * 10 + Uns (Digit_Value (C));
      end loop;
      return Value;
   end To_Uns;

   --  N rounded (or truncated) to its first Keep digits; Carry if a digit
   --  is added (999 -> 1000).
   function Round
     (N : String; Keep : Natural; Truncate : Boolean := False) return String
   is
      Result : String (1 .. Keep);
   begin
      if Keep >= N'Length then
         Result (1 .. N'Length) := N;
         Result (N'Length + 1 .. Keep) := (others => '0');
         return Result;
      end if;
      Result := N (N'First .. N'First + Keep - 1);
      if Truncate or else N (N'First + Keep) < '5' then
         return Result;
      end if;
      for I in reverse Result'Range loop
         if Result (I) = '9' then
            Result (I) := '0';
         else
            Result (I) := Character'Succ (Result (I));
            return Result;
         end if;
      end loop;
      return '1' & Result;
   end Round;

   ---------------------------------------------------------------------------
   --  Fixed point

   function Describe (Small : Long_Float) return Small_Description is
      D : Small_Description;
      Y : Long_Float;
      P : Long_Float;
   begin
      D.Approximation := Small;
      if Long_Float'Fraction (Small) = 0.5 then
         D.Kind := Power_Of_Two;
         D.Exponent := Long_Float'Exponent (Small) - 1;
         return D;
      end if;
      for J in 0 .. 18 loop
         Y := Small * 10.0 ** J;
         P := Long_Float'Rounding (Y);
         if P >= 1.0 and then P < 2.0 ** 59
           and then abs (Y - P) <= Y * 1.0E-14
         then
            D.Kind := Decimal_Fraction;
            D.Factor := Int (P);
            D.Exponent := J;
            return D;
         end if;
      end loop;
      return D;
   end Describe;

   function Decimal_Small (Scale : Integer) return Small_Description is
   begin
      if Scale >= 0 then
         return (Kind => Decimal_Fraction, Factor => 1, Exponent => Scale,
                 Approximation => 10.0 ** (-Scale));
      else
         return (Kind => Decimal_Fraction, Factor => 10 ** (-Scale),
                 Exponent => 0, Approximation => 10.0 ** (-Scale));
      end if;
   end Decimal_Small;

   function Fixed_Value
     (Literal  : String;
      Small    : Small_Description;
      Truncate : Boolean) return Int
   is
      P : constant Literal_Parts := Parts (Literal);

      --  The magnitude of the literal's value: Digits * 10 ** Scale.
      function Literal_Digits return String is
         Result : String (1 .. Literal'Length);
         Last   : Natural := 0;
      begin
         for I in P.Mantissa_First .. P.Mantissa_Last loop
            if Literal (I) in '0' .. '9' then
               Last := Last + 1;
               Result (Last) := Literal (I);
            end if;
         end loop;
         return Trim_Zeros (Result (1 .. Last));
      end Literal_Digits;

      function Literal_Scale return Int is
         Scale : Int := P.Exponent;
         After : Boolean := False;
      begin
         for I in P.Mantissa_First .. P.Mantissa_Last loop
            if Literal (I) = '.' then
               After := True;
            elsif After and then Literal (I) in '0' .. '9' then
               Scale := Scale - 1;
            end if;
         end loop;
         return Scale;
      end Literal_Scale;

      --  N * 10 ** Scale, rounded or truncated, as an integer.
      function Shift (N : String; Scale : Int) return Uns is
      begin
         if N'Length = 0 then
            return 0;
         elsif Scale >= 0 then
            if Int (N'Length) + Scale > 20 then
               raise Data_Error;
            end if;
            return To_Uns (N & (1 .. Natural (Scale) => '0'));
         elsif -Scale > Int (N'Length) then
            return 0;
         else
            declare
               Keep : constant Natural := N'Length - Natural (-Scale);
               R    : constant String := Round ('0' & N, Keep + 1, Truncate);
            begin
               return To_Uns (R);
            end;
         end if;
      end Shift;

      Magnitude : Uns;
   begin
      if P.Base /= 10 or else Small.Kind = Other then
         declare
            V : constant Long_Float := Real_Value (Literal);
            M : constant Long_Float := V / Small.Approximation;
         begin
            if abs M >= 2.0 ** 63 then
               raise Data_Error;
            end if;
            return Int (Long_Float'Rounding (M));  --  (even if Truncate)
         end;
      end if;
      declare
         N     : constant String := Literal_Digits;
         Scale : constant Int := Literal_Scale;
      begin
         if Small.Kind = Power_Of_Two and then Small.Exponent <= 0 then
            --  N * 10 ** Scale * 2 ** (-Exponent)
            declare
               Product : String (1 .. N'Length + 1 + (-Small.Exponent) / 3);
               Last    : Natural := N'Length;
               Rest    : Natural := -Small.Exponent;
            begin
               Product (1 .. Last) := N;
               while Rest > 0 loop
                  declare
                     Step : constant Natural := Natural'Min (Rest, 30);
                     M    : constant String :=
                        Multiply (Product (1 .. Last), 2 ** Step);
                  begin
                     Last := M'Length;
                     Product (1 .. Last) := M;
                     Rest := Rest - Step;
                  end;
               end loop;
               Magnitude := Shift (Product (1 .. Last), Scale);
            end;
         else
            --  N * 10 ** (Scale + J) / P (Small = P * 10 ** (-J))
            declare
               Divisor : constant Int :=
                  (if Small.Kind = Power_Of_Two then 2 ** Small.Exponent
                   else Small.Factor);
               T       : constant Int :=
                  Scale + (if Small.Kind = Power_Of_Two then 0
                           else Int (Small.Exponent));
            begin
               if N'Length = 0 then
                  Magnitude := 0;
               elsif T >= 0 then
                  if Int (N'Length) + T > 40 then
                     raise Data_Error;
                  end if;
                  declare
                     Dividend  : constant String :=
                        N & (1 .. Natural (T) => '0');
                     Quotient  : String (Dividend'Range);
                     Remainder : Int;
                  begin
                     Divide (Dividend, Divisor, Quotient, Remainder);
                     Magnitude := To_Uns (Trim_Zeros (Quotient));
                     if not Truncate and then 2 * Remainder >= Divisor then
                        Magnitude := Magnitude + 1;
                     end if;
                  end;
               elsif -T > Int (N'Length) then
                  Magnitude := 0;
               else
                  declare
                     Quotient  : String (N'Range);
                     Remainder : Int;
                  begin
                     Divide (N, Divisor, Quotient, Remainder);
                     Magnitude := Shift (Trim_Zeros (Quotient), T);
                  end;
               end if;
            end;
         end if;
      end;
      if P.Negative then
         if Magnitude > 2 ** 63 then
            raise Data_Error;
         end if;
         return (if Magnitude = 2 ** 63 then Int'First else -Int (Magnitude));
      elsif Magnitude >= 2 ** 63 then
         raise Data_Error;
      end if;
      return Int (Magnitude);
   end Fixed_Value;

   ---------------------------------------------------------------------------
   --  Images

   Hex : constant String := "0123456789ABCDEF";

   function Digits_Image (Value : Uns; Base : Positive) return String is
      Buffer : String (1 .. 64);
      First  : Positive := Buffer'Last + 1;
      V      : Uns := Value;
   begin
      loop
         First := First - 1;
         Buffer (First) := Hex (Natural (V mod Uns (Base)) + 1);
         V := V / Uns (Base);
         exit when V = 0;
      end loop;
      return Buffer (First .. Buffer'Last);
   end Digits_Image;

   function Unsigned_Image (Value : Uns; Base : Positive) return String is
   begin
      if Base = 10 then
         return Digits_Image (Value, 10);
      end if;
      return Image (Int (Base)) & "#" & Digits_Image (Value, Base) & "#";
   end Unsigned_Image;

   function Integer_Image (Value : Int; Base : Positive) return String is
   begin
      if Value >= 0 then
         return Unsigned_Image (Uns (Value), Base);
      elsif Value = Int'First then
         return "-" & Unsigned_Image (2 ** 63, Base);
      else
         return "-" & Unsigned_Image (Uns (-Value), Base);
      end if;
   end Integer_Image;

   --  The image of the value N * 10 ** Scale (N a decimal image without
   --  leading zeros) in the format Fore . Aft [E Exp]; Max_Digits: the
   --  significant digits beyond which those output are zeros (0: none).
   function Decimal_Image
     (Negative   : Boolean;
      N          : String;
      Scale      : Int;
      Fore       : Natural;
      Aft        : Natural;
      Exp        : Natural;
      Max_Digits : Natural) return String
   is
      Fraction_Digits : constant Positive := Natural'Max (Aft, 1);

      --  N rounded to Keep digits (Keep may exceed Max_Digits)
      function Round_To (Keep : Natural) return String is
      begin
         if Max_Digits = 0 or else Keep <= Max_Digits then
            return Round (N, Keep);
         end if;
         declare
            R : constant String := Round (N, Max_Digits);
         begin
            return R & (1 .. Keep - Max_Digits => '0');
         end;
      end Round_To;

      function Pad (Integer_Part : String) return String is
         Sign : constant String := (if Negative then "-" else "");
         Text : constant String := Sign & Integer_Part;
      begin
         if Text'Length >= Fore then
            return Text;
         end if;
         return (1 .. Fore - Text'Length => ' ') & Text;
      end Pad;

      function Exponent_Image (E : Int) return String is
         Digits_Image : constant String := Image (abs E);
         Sign         : constant Character := (if E < 0 then '-' else '+');
      begin
         if Digits_Image'Length + 1 >= Exp then
            return Sign & Digits_Image;
         end if;
         return Sign & (1 .. Exp - 1 - Digits_Image'Length => '0')
                & Digits_Image;
      end Exponent_Image;
   begin
      if Exp > 0 then
         if N'Length = 0 then
            return Pad ("0") & "." & (1 .. Fraction_Digits => '0') & "E"
                   & Exponent_Image (0);
         end if;
         declare
            R : constant String := Round_To (1 + Fraction_Digits);
            E : Int := Int (N'Length) - 1 + Scale;
         begin
            if R'Length > 1 + Fraction_Digits then
               E := E + 1;  --  (rounded up to a power of ten)
            end if;
            return Pad (R (R'First .. R'First)) & "."
                   & R (R'First + 1 .. R'First + Fraction_Digits) & "E"
                   & Exponent_Image (E);
         end;
      end if;
      declare
         --  The number of digits of N that are at least 10 ** (-Aft)
         Keep : constant Int := Int (N'Length) + Scale + Int (Fraction_Digits);
      begin
         if N'Length = 0 or else Keep < 0
           or else (Keep = 0 and then N (N'First) < '5')
         then
            return Pad ("0") & "." & (1 .. Fraction_Digits => '0');
         elsif Keep = 0 then
            return Pad ("0") & "." & (1 .. Fraction_Digits - 1 => '0') & "1";
         end if;
         declare
            R : constant String := Round_To (Natural (Keep));
         begin
            if R'Length <= Fraction_Digits then
               return Pad ("0") & "."
                      & (1 .. Fraction_Digits - R'Length => '0') & R;
            end if;
            return Pad (R (R'First .. R'Last - Fraction_Digits)) & "."
                   & R (R'Last - Fraction_Digits + 1 .. R'Last);
         end;
      end;
   end Decimal_Image;

   function Float_Image
     (Item       : Long_Float;
      Fore       : Natural;
      Aft        : Natural;
      Exp        : Natural;
      Max_Digits : Positive) return String
   is
      D : constant String := Native_IO.Float_Digits (Item);
   begin
      if D (D'First) not in '+' | '-' or else D'Length < 3
        or else D (D'First + 1) in 'I' | 'N'
      then
         --  Infinities and NaNs: their name, filled with asterisks to
         --  the length of the format.
         declare
            Name   : constant String :=
               (if D (D'First) = 'N' then "NaN" else D);
            Length : constant Natural :=
               Fore + 1 + Natural'Max (Aft, 1)
               + (if Exp > 0 then 1 + Exp else 0);
         begin
            if Name'Length >= Length then
               return Name;
            end if;
            return Name & (1 .. Length - Name'Length => '*');
         end;
      end if;
      declare
         E     : Natural := D'Last;
         Scale : Int := 0;
      begin
         while D (E) /= 'e' loop
            E := E - 1;
         end loop;
         declare
            Negative : Boolean := False;
         begin
            for I in E + 1 .. D'Last loop
               if D (I) = '-' then
                  Negative := True;
               else
                  Scale := Scale * 10 + Int (Digit_Value (D (I)));
               end if;
            end loop;
            if Negative then
               Scale := -Scale;
            end if;
         end;
         --  0.digits * 10 ** Scale = digits * 10 ** (Scale - length)
         return Decimal_Image
                  (Negative   => D (D'First) = '-',
                   N          => D (D'First + 1 .. E - 1),
                   Scale      => Scale - Int (E - 1 - D'First),
                   Fore       => Fore,
                   Aft        => Aft,
                   Exp        => Exp,
                   Max_Digits => Max_Digits);
      end;
   end Float_Image;

   function Fixed_Image
     (Mantissa : Int;
      Small    : Small_Description;
      Fore     : Natural;
      Aft      : Natural;
      Exp      : Natural) return String
   is
      Negative  : constant Boolean := Mantissa < 0;
      Magnitude : constant Uns :=
         (if Mantissa = Int'First then 2 ** 63
          elsif Negative then Uns (-Mantissa) else Uns (Mantissa));
   begin
      case Small.Kind is
         when Power_Of_Two =>
            declare
               N    : String (1 .. 21 + abs Small.Exponent);
               Last : Natural;
               Rest : Natural := abs Small.Exponent;
               Factor : constant Int := (if Small.Exponent < 0 then 5 else 2);
            begin
               declare
                  M : constant String := Big (Magnitude);
               begin
                  Last := M'Length;
                  N (1 .. Last) := M;
               end;
               --  M * 2 ** E = M * 5 ** (-E) * 10 ** E when E < 0
               while Rest > 0 loop
                  declare
                     Step : constant Natural := Natural'Min (Rest, 20);
                     P    : constant String :=
                        Multiply (N (1 .. Last), Factor ** Step);
                  begin
                     Last := P'Length;
                     N (1 .. Last) := P;
                     Rest := Rest - Step;
                  end;
               end loop;
               return Decimal_Image
                        (Negative, N (1 .. Last),
                         Int (Integer'Min (Small.Exponent, 0)),
                         Fore, Aft, Exp, 0);
            end;
         when Decimal_Fraction =>
            return Decimal_Image
                     (Negative, Multiply (Big (Magnitude), Small.Factor),
                      Int (-Small.Exponent), Fore, Aft, Exp, 0);
         when Other =>
            return Float_Image
                     (Long_Float (Mantissa) * Small.Approximation,
                      Fore, Aft, Exp, 15);
      end case;
   end Fixed_Image;

   function Enumeration_Image
     (Image : String; Width : Natural; Lower : Boolean) return String
   is
      Result : String (1 .. Natural'Max (Image'Length, Width)) :=
         (others => ' ');
   begin
      Result (1 .. Image'Length) := Image;
      if Lower and then Image'Length > 0 and then Image (Image'First) /= '''
      then
         for I in 1 .. Image'Length loop
            if Result (I) in 'A' .. 'Z'
              or else (Character'Pos (Result (I)) in 16#C0# .. 16#DE#
                         and then Character'Pos (Result (I)) /= 16#D7#)
            then
               Result (I) := Character'Val (Character'Pos (Result (I)) + 32);
            end if;
         end loop;
      end if;
      return Result;
   end Enumeration_Image;

   procedure Put_Image
     (File   : Text_IO_Engine.File_Access;
      Image  : String;
      Length : Natural) is
   begin
      Text_IO_Engine.Put_Item (File, Image, Length);
   end Put_Image;

   procedure Put_Image
     (To    : out String;
      Image : String;
      Left  : Boolean := False) is
   begin
      if Image'Length > To'Length then
         raise Layout_Error;
      end if;
      if Left then
         To (To'First .. To'First + Image'Length - 1) := Image;
         To (To'First + Image'Length .. To'Last) := (others => ' ');
      else
         To (To'First .. To'Last - Image'Length) := (others => ' ');
         To (To'Last - Image'Length + 1 .. To'Last) := Image;
      end if;
   end Put_Image;

end Ada.Text_IO_Numbers;
