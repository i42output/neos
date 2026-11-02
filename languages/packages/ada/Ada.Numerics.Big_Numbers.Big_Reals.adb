--  Ada.Numerics.Big_Numbers.Big_Reals.adb
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

--  To_String rounds to the nearest value of its format (a value exactly
--  halfway is rounded away from zero, as by Text_IO). From_String accepts
--  what Text_IO.Float_IO.Get would read from the whole string (leading and
--  trailing blanks aside): an optional sign and a decimal or based real or
--  integer literal (the integer part or the fractional part may be empty, as
--  in "5." and ".5"); anything else raises Constraint_Error. The value is
--  exact.
--
--  Float_Conversions.From_Big_Real rounds to the nearest machine number
--  (halfway values to even), except that a result in the range of
--  denormalized numbers may be rounded twice. Fixed_Conversions takes the
--  small of Num as the simplest fraction within 2.0**(-50) (relative) of its
--  Long_Float value (which is its exact value for the usual smalls: powers of
--  two and ten, and fractions with small denominators); From_Big_Real rounds
--  to the nearest multiple of the small (halfway away from zero).

package body Ada.Numerics.Big_Numbers.Big_Reals is

   package BI renames Big_Integers;

   subtype Big_Integer is BI.Big_Integer;

   function Big (N : Integer) return Big_Integer renames BI.To_Big_Integer;

   function Is_Valid (Arg : Big_Real) return Boolean is
   begin
      return BI.Is_Valid (Arg.Num) and then BI.Is_Valid (Arg.Den);
   end Is_Valid;

   --  N / D in lowest terms (D nonzero)
   function Fraction (N, D : Big_Integer) return Big_Real is
      G : Big_Integer;
   begin
      if D = 0 then
         raise Constraint_Error;
      elsif N = 0 then
         return (Num => Big (0), Den => Big (1));
      end if;
      G := BI.Greatest_Common_Divisor (N, D);
      if D < 0 then
         G := -G;
      end if;
      return (Num => N / G, Den => D / G);
   end Fraction;

   function "/" (Num, Den : Big_Integers.Valid_Big_Integer)
     return Valid_Big_Real is
   begin
      return Fraction (Num, Den);
   end "/";

   function Numerator
     (Arg : Valid_Big_Real) return Big_Integers.Valid_Big_Integer is
   begin
      return Arg.Num;
   end Numerator;

   function Denominator (Arg : Valid_Big_Real)
     return Big_Integers.Big_Positive is
   begin
      return Arg.Den;
   end Denominator;

   -----------------
   -- Comparisons --
   -----------------

   --  (the denominators are positive)
   function "=" (L, R : Valid_Big_Real) return Boolean is
   begin
      return L.Num = R.Num and then L.Den = R.Den;
   end "=";

   function "<" (L, R : Valid_Big_Real) return Boolean is
   begin
      return L.Num * R.Den < R.Num * L.Den;
   end "<";

   function "<=" (L, R : Valid_Big_Real) return Boolean is
   begin
      return L.Num * R.Den <= R.Num * L.Den;
   end "<=";

   function ">" (L, R : Valid_Big_Real) return Boolean is
   begin
      return L.Num * R.Den > R.Num * L.Den;
   end ">";

   function ">=" (L, R : Valid_Big_Real) return Boolean is
   begin
      return L.Num * R.Den >= R.Num * L.Den;
   end ">=";

   ---------------
   -- Operators --
   ---------------

   function "+" (L : Valid_Big_Real) return Valid_Big_Real is
   begin
      return L;
   end "+";

   function "-" (L : Valid_Big_Real) return Valid_Big_Real is
   begin
      return (Num => -L.Num, Den => L.Den);
   end "-";

   function "abs" (L : Valid_Big_Real) return Valid_Big_Real is
   begin
      return (Num => abs L.Num, Den => L.Den);
   end "abs";

   function "+" (L, R : Valid_Big_Real) return Valid_Big_Real is
   begin
      return Fraction (L.Num * R.Den + R.Num * L.Den, L.Den * R.Den);
   end "+";

   function "-" (L, R : Valid_Big_Real) return Valid_Big_Real is
   begin
      return Fraction (L.Num * R.Den - R.Num * L.Den, L.Den * R.Den);
   end "-";

   function "*" (L, R : Valid_Big_Real) return Valid_Big_Real is
   begin
      return Fraction (L.Num * R.Num, L.Den * R.Den);
   end "*";

   function "/" (L, R : Valid_Big_Real) return Valid_Big_Real is
   begin
      if R.Num = 0 then
         raise Constraint_Error;
      end if;
      return Fraction (L.Num * R.Den, L.Den * R.Num);
   end "/";

   function "**" (L : Valid_Big_Real; R : Integer) return Valid_Big_Real is
   begin
      if R >= 0 then
         return (Num => L.Num ** R, Den => L.Den ** R);
      elsif L.Num = 0 then
         raise Constraint_Error;
      else
         --  (-R might not be an Integer)
         return Fraction (L.Den ** (-(R + 1)) * L.Den,
                          L.Num ** (-(R + 1)) * L.Num);
      end if;
   end "**";

   function Min (L, R : Valid_Big_Real) return Valid_Big_Real is
   begin
      return (if L <= R then L else R);
   end Min;

   function Max (L, R : Valid_Big_Real) return Valid_Big_Real is
   begin
      return (if L >= R then L else R);
   end Max;

   ------------------
   -- Bit lengths --
   ------------------

   --  The number of bits of N (positive)
   function Bit_Length (N : Big_Integer) return Natural is
      Image : constant String := BI.To_String (N, Base => 16);  --  16#...#
      Top   : constant Character := Image (4);
      Bits  : Natural;
   begin
      case Top is
         when '1' => Bits := 1;
         when '2' .. '3' => Bits := 2;
         when '4' .. '7' => Bits := 3;
         when others => Bits := 4;
      end case;
      return 4 * (Image'Length - 5) + Bits;
   end Bit_Length;

   type Word is mod 2 ** 64;

   package Word_Conversions is new BI.Unsigned_Conversions (Word);

   -----------------------
   -- Float_Conversions --
   -----------------------

   package body Float_Conversions is

      subtype Base is Num'Base;

      function To_Big_Real (Arg : Num) return Valid_Big_Real is
         Chunk : constant := 30;
         X     : Base;
         F     : Base;
         E     : Integer;
         D     : Integer;
         M     : Big_Integer := Big (0);
         Bits  : Integer := 0;
      begin
         if Arg = 0.0 then
            return To_Real (0);
         end if;
         X := abs Base (Arg);
         E := Base'Exponent (X);
         F := Base'Fraction (X);  --  (X = F * 2**E, F in 0.5 .. 1.0)
         while F /= 0.0 loop
            F := Base'Scaling (F, Chunk);
            D := Integer (Base'Truncation (F));
            F := F - Base (D);
            M := M * Big (2) ** Chunk + Big (D);
            Bits := Bits + Chunk;
         end loop;
         if Arg < 0.0 then
            M := -M;
         end if;
         if E >= Bits then
            return Fraction (M * Big (2) ** (E - Bits), Big (1));
         else
            return Fraction (M, Big (2) ** (Bits - E));
         end if;
      end To_Big_Real;

      function From_Big_Real (Arg : Valid_Big_Real) return Num is
         P     : constant Integer := Base'Machine_Mantissa;
         N     : Big_Integer;
         D     : Big_Integer;
         Shift : Integer;
         Q, R  : Big_Integer;
         Result : Base;
      begin
         if not In_Range (Arg, To_Big_Real (Num'First), To_Big_Real (Num'Last))
         then
            raise Constraint_Error;
         elsif Arg.Num = 0 then
            return 0.0;
         end if;
         N := abs Arg.Num;
         D := Arg.Den;
         --  Q := N / D * 2**Shift, with P bits
         Shift := P - (Bit_Length (N) - Bit_Length (D));
         loop
            if Shift >= 0 then
               Q := N * Big (2) ** Shift / D;
               R := N * Big (2) ** Shift rem D;
            else
               Q := N / (D * Big (2) ** (-Shift));
               R := N rem (D * Big (2) ** (-Shift));
            end if;
            if Q < Big (2) ** (P - 1) then
               Shift := Shift + 1;
            elsif Q >= Big (2) ** P then
               Shift := Shift - 1;
            else
               exit;
            end if;
         end loop;
         --  round (halfway: to even)
         declare
            Divisor : constant Big_Integer :=
              (if Shift >= 0 then D else D * Big (2) ** (-Shift));
         begin
            if R * Big (2) > Divisor
              or else (R * Big (2) = Divisor and then Q rem Big (2) = Big (1))
            then
               Q := Q + Big (1);
               if Q = Big (2) ** P then
                  Q := Q / Big (2);
                  Shift := Shift - 1;
               end if;
            end if;
         end;
         Result := Base'Scaling (Base (Word_Conversions.From_Big_Integer (Q)),
                                 -Shift);
         if Arg.Num < 0 then
            Result := -Result;
         end if;
         return Num (Result);
      end From_Big_Real;

   end Float_Conversions;

   package Long_Float_Conversions is new Float_Conversions (Long_Float);

   -----------------------
   -- Fixed_Conversions --
   -----------------------

   --  The simplest fraction within X * 2.0**(-50) of X (positive)
   function Simplest (X : Long_Float) return Big_Real is
      Exact     : constant Big_Real := Long_Float_Conversions.To_Big_Real (X);
      Tolerance : constant Big_Real :=
        Long_Float_Conversions.To_Big_Real (X * 2.0 ** (-50));
      R         : Big_Real := Exact;
      H0, H1    : Big_Integer := Big (0);
      K0, K1    : Big_Integer := Big (1);
      A, T      : Big_Integer;
   begin
      H1 := Big (1);
      K1 := Big (0);
      --  (the convergents H1 / K1 of the continued fraction of X)
      loop
         A := R.Num / R.Den;  --  (the integer part)
         T := A * H1 + H0;
         H0 := H1;
         H1 := T;
         T := A * K1 + K0;
         K0 := K1;
         K1 := T;
         declare
            Convergent : constant Big_Real := Fraction (H1, K1);
         begin
            if abs (Convergent - Exact) <= Tolerance then
               return Convergent;
            end if;
         end;
         R := R - To_Big_Real (A);
         exit when R.Num = 0;
         R := Fraction (R.Den, R.Num);
      end loop;
      return Exact;
   end Simplest;

   package body Fixed_Conversions is

      function Small return Big_Real is
      begin
         return Simplest (Long_Float (Num'Small));
      end Small;

      function To_Big_Real (Arg : Num) return Valid_Big_Real is
         package LLI is new BI.Signed_Conversions (Long_Long_Integer);
         Count : constant Long_Long_Integer :=
           Long_Long_Integer (Arg / Num'(Num'Small));
      begin
         return To_Big_Real (LLI.To_Big_Integer (Count)) * Small;
      end To_Big_Real;

      function From_Big_Real (Arg : Valid_Big_Real) return Num is
         Scale : constant Big_Integer := Big (2) ** 15;

         --  I * Num'Small
         function To_Num (I : Big_Integer) return Num is
         begin
            if abs I < Scale then
               return Num'(Num'Small) * BI.To_Integer (I);
            end if;
            return To_Num (I / Scale) * Integer (2 ** 15)
                     + Num'(Num'Small) * BI.To_Integer (I rem Scale);
         end To_Num;

         Q : Big_Real;
         I : Big_Integer;
      begin
         if not In_Range (Arg, To_Big_Real (Num'First), To_Big_Real (Num'Last))
         then
            raise Constraint_Error;
         end if;
         Q := abs Arg / Small;
         I := (Q.Num * Big (2) + Q.Den) / (Q.Den * Big (2));
         if Arg.Num < 0 then
            I := -I;
         end if;
         return To_Num (I);
      end From_Big_Real;

   end Fixed_Conversions;

   ---------------
   -- To_String --
   ---------------

   --  The decimal digits of N (nonnegative), at least Width of them
   function Digits_Of (N : Big_Integer; Width : Natural) return String is
      Image : constant String := BI.To_String (N);
   begin
      if Image'Length >= Width then
         return Image;
      end if;
      return (1 .. Width - Image'Length => '0') & Image;
   end Digits_Of;

   --  N / D rounded to the nearest integer (halfway: away from zero), for
   --  N >= 0, D > 0
   function Rounded (N, D : Big_Integer) return Big_Integer is
   begin
      return (N * Big (2) + D) / (D * Big (2));
   end Rounded;

   function To_String (Arg  : Valid_Big_Real;
                       Fore : Field := 2;
                       Aft  : Field := 3;
                       Exp  : Field := 0) return String
   is
      A      : constant Positive := Integer'Max (Aft, 1);
      Ten_A  : constant Big_Integer := Big (10) ** A;
      N      : constant Big_Integer := abs Arg.Num;
      D      : constant Big_Integer := Arg.Den;
      Sign   : constant String := (if Arg.Num < 0 then "-" else "");

      function Padded (S : String) return String is
         Result : constant String :=
           (1 .. Integer'Max (0, Fore - S'Length) => ' ') & S;
      begin
         return Result;
      end Padded;
   begin
      if Exp = 0 then
         declare
            Scaled : constant Big_Integer := Rounded (N * Ten_A, D);
         begin
            return Padded (Sign & BI.To_String (Scaled / Ten_A)) & '.'
              & Digits_Of (Scaled rem Ten_A, A);
         end;
      end if;
      declare
         X      : Integer := 0;  --  (the exponent)
         Scaled : Big_Integer := Big (0);
      begin
         if N /= 0 then
            X := BI.To_String (N)'Length - BI.To_String (D)'Length;
            --  (adjust: 10**X <= N / D < 10**(X + 1))
            loop
               if X >= 0 and then N < D * Big (10) ** X then
                  X := X - 1;
               elsif X < 0 and then N * Big (10) ** (-X) < D then
                  X := X - 1;
               elsif X + 1 >= 0 and then N >= D * Big (10) ** (X + 1) then
                  X := X + 1;
               elsif X + 1 < 0 and then N * Big (10) ** (-(X + 1)) >= D then
                  X := X + 1;
               else
                  exit;
               end if;
            end loop;
            if X >= A then
               Scaled := Rounded (N, D * Big (10) ** (X - A));
            else
               Scaled := Rounded (N * Big (10) ** (A - X), D);
            end if;
            if Scaled = Ten_A * Big (10) then
               --  (rounded up to the next power of ten)
               Scaled := Ten_A;
               X := X + 1;
            end if;
         end if;
         declare
            Exponent : constant String := Integer'Image (abs X);
            Digits_X : constant String := Exponent (2 .. Exponent'Last);
            Exp_Sign : constant Character := (if X < 0 then '-' else '+');
            Zeros    : constant Natural :=
              Integer'Max (0, Exp - 1 - Digits_X'Length);
         begin
            return Padded (Sign & BI.To_String (Scaled / Ten_A)) & '.'
              & Digits_Of (Scaled rem Ten_A, A) & 'E' & Exp_Sign
              & (1 .. Zeros => '0') & Digits_X;
         end;
      end;
   end To_String;

   -----------------
   -- From_String --
   -----------------

   function From_String (Arg : String) return Valid_Big_Real is
      First : Integer := Arg'First;
      Last  : Integer := Arg'Last;
      I     : Integer;

      function Is_Blank (C : Character) return Boolean is
      begin
         return C = ' ' or else C = Character'Val (9);
      end Is_Blank;

      function Value_Of (C : Character) return Integer is
      begin
         case C is
            when '0' .. '9' => return Character'Pos (C) - Character'Pos ('0');
            when 'A' .. 'F' => return Character'Pos (C) - Character'Pos ('A') + 10;
            when 'a' .. 'f' => return Character'Pos (C) - Character'Pos ('a') + 10;
            when others => return 16;
         end case;
      end Value_Of;

      --  Accumulate the digits of Base (with single underscores between
      --  them) that start at I into Value; Count := how many
      procedure Numeral (Base  : Positive;
                         Value : in out Big_Integer;
                         Count : out Natural)
      is
         B    : constant Big_Integer := Big (Base);
         Seen : Boolean := False;
         D    : Integer;
      begin
         Count := 0;
         while I <= Last loop
            if Arg (I) = '_' then
               if not Seen or else I = Last
                 or else Value_Of (Arg (I + 1)) >= Base
               then
                  raise Constraint_Error;
               end if;
               Seen := False;
            else
               D := Value_Of (Arg (I));
               exit when D >= Base;
               Value := Value * B + Big (D);
               Seen := True;
               Count := Count + 1;
            end if;
            I := I + 1;
         end loop;
      end Numeral;

      --  The mantissa (integer and fractional parts) of Base at I: Value
      --  and the number of fractional digits
      procedure Mantissa (Base       : Positive;
                          Value      : out Big_Integer;
                          Fractional : out Natural)
      is
         Whole : Natural;
      begin
         Value := Big (0);
         Numeral (Base, Value, Whole);
         Fractional := 0;
         if I <= Last and then Arg (I) = '.' then
            I := I + 1;
            Numeral (Base, Value, Fractional);
         end if;
         if Whole = 0 and then Fractional = 0 then
            raise Constraint_Error;
         end if;
      end Mantissa;

      Negative   : Boolean := False;
      Base       : Positive := 10;
      Value      : Big_Integer;
      Fractional : Natural;
      Exponent   : Big_Integer := Big (0);
      Exp_Digits : Natural;
      Exp_Minus  : Boolean := False;
      Result     : Big_Real;
   begin
      while First <= Last and then Is_Blank (Arg (First)) loop
         First := First + 1;
      end loop;
      while Last >= First and then Is_Blank (Arg (Last)) loop
         Last := Last - 1;
      end loop;
      I := First;
      if I <= Last and then (Arg (I) = '+' or else Arg (I) = '-') then
         Negative := Arg (I) = '-';
         I := I + 1;
      end if;
      Mantissa (10, Value, Fractional);
      if I <= Last and then (Arg (I) = '#' or else Arg (I) = ':') then
         declare
            Delimiter : constant Character := Arg (I);
         begin
            if Fractional /= 0 or else Value < Big (2) or else Value > Big (16)
            then
               raise Constraint_Error;
            end if;
            Base := BI.To_Integer (Value);
            I := I + 1;
            Mantissa (Base, Value, Fractional);
            if I > Last or else Arg (I) /= Delimiter then
               raise Constraint_Error;
            end if;
            I := I + 1;
         end;
      end if;
      if I <= Last and then (Arg (I) = 'E' or else Arg (I) = 'e') then
         I := I + 1;
         if I <= Last and then (Arg (I) = '+' or else Arg (I) = '-') then
            Exp_Minus := Arg (I) = '-';
            I := I + 1;
         end if;
         Numeral (10, Exponent, Exp_Digits);
         if Exp_Digits = 0 or else Exponent > Big (Integer'Last / 2) then
            raise Constraint_Error;
         end if;
      end if;
      if I <= Last then
         raise Constraint_Error;
      end if;
      declare
         E : constant Integer :=
           (if Exp_Minus then -BI.To_Integer (Exponent)
            else BI.To_Integer (Exponent)) - Fractional;
      begin
         if E >= 0 then
            Result := Fraction (Value * Big (Base) ** E, Big (1));
         else
            Result := Fraction (Value, Big (Base) ** (-E));
         end if;
      end;
      if Negative then
         return -Result;
      end if;
      return Result;
   end From_String;

   function From_Quotient_String (Arg : String) return Valid_Big_Real is
   begin
      for I in Arg'Range loop
         if Arg (I) = '/' then
            return Fraction (BI.From_String (Arg (Arg'First .. I - 1)),
                             BI.From_String (Arg (I + 1 .. Arg'Last)));
         end if;
      end loop;
      raise Constraint_Error;
   end From_Quotient_String;

   procedure Put_Image
     (Buffer : in out Ada.Strings.Text_Buffers.Root_Buffer_Type'Class;
      Arg    : in Valid_Big_Real) is
   begin
      Buffer.Put (To_String (Arg));
   end Put_Image;

end Ada.Numerics.Big_Numbers.Big_Reals;
