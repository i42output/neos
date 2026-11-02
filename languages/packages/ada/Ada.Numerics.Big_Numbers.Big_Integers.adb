--  Ada.Numerics.Big_Numbers.Big_Integers.adb
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

--  Magnitudes are arrays of digits base 2**32 (least significant first);
--  multiplication is the schoolbook method and division is Knuth's
--  algorithm D (The Art of Computer Programming, vol. 2, 4.3.1).
--
--  From_String accepts what Text_IO.Integer_IO.Get would read from the whole
--  string (leading and trailing blanks aside): an optional sign, then a
--  decimal or based integer literal (with underscores and a nonnegative
--  exponent allowed as in an Ada literal); anything else raises
--  Constraint_Error.

with Ada.Unchecked_Deallocation;

package body Ada.Numerics.Big_Numbers.Big_Integers is

   type Double is mod 2 ** 64;
   type Long is range -2 ** 63 .. 2 ** 63 - 1;

   Radix : constant := 2 ** 32;

   procedure Free is new Ada.Unchecked_Deallocation (Digits_Array, Digits_Access);

   Empty : constant Digits_Array (0 .. -1) := (others => 0);

   ------------------------
   -- Controlled objects --
   ------------------------

   overriding procedure Adjust (Object : in out Magnitude) is
   begin
      if Object.Value /= null then
         Object.Value := new Digits_Array'(Object.Value.all);
      end if;
   end Adjust;

   overriding procedure Finalize (Object : in out Magnitude) is
   begin
      Free (Object.Value);
   end Finalize;

   ----------------
   -- Magnitudes --
   ----------------

   --  A without its zero digits at the end, indexed from 0
   function Normalized (A : Digits_Array) return Digits_Array is
      L : Integer := A'Last;
   begin
      while L >= A'First and then A (L) = 0 loop
         L := L - 1;
      end loop;
      declare
         Result : constant Digits_Array (0 .. L - A'First) := A (A'First .. L);
      begin
         return Result;
      end;
   end Normalized;

   function Magnitude_Of (X : Big_Integer) return Digits_Array is
   begin
      if X.M.Value = null then
         return Empty;
      end if;
      return X.M.Value.all;
   end Magnitude_Of;

   function Make (Negative : Boolean; M : Digits_Array) return Big_Integer is
      N : constant Digits_Array := Normalized (M);
   begin
      if N'Length = 0 then
         return (Valid => True, Negative => False,
                 M => (Ada.Finalization.Controlled with Value => null));
      end if;
      return (Valid => True, Negative => Negative,
              M => (Ada.Finalization.Controlled with
                      Value => new Digits_Array'(N)));
   end Make;

   function Is_Zero (X : Big_Integer) return Boolean is
   begin
      return X.M.Value = null;
   end Is_Zero;

   --  -1, 0 or 1 as A < B, A = B or A > B (normalized magnitudes)
   function Compare (A, B : Digits_Array) return Integer is
   begin
      if A'Length /= B'Length then
         return (if A'Length < B'Length then -1 else 1);
      end if;
      for I in reverse 0 .. A'Length - 1 loop
         if A (A'First + I) /= B (B'First + I) then
            return (if A (A'First + I) < B (B'First + I) then -1 else 1);
         end if;
      end loop;
      return 0;
   end Compare;

   function Add (A, B : Digits_Array) return Digits_Array is
      N      : constant Natural := Natural'Max (A'Length, B'Length);
      Result : Digits_Array (0 .. N);
      Carry  : Double := 0;
      T      : Double;
   begin
      for I in 0 .. N - 1 loop
         T := Carry;
         if I < A'Length then
            T := T + Double (A (A'First + I));
         end if;
         if I < B'Length then
            T := T + Double (B (B'First + I));
         end if;
         Result (I) := Digit (T mod Radix);
         Carry := T / Radix;
      end loop;
      Result (N) := Digit (Carry);
      return Result;
   end Add;

   --  A - B (A >= B)
   function Subtract (A, B : Digits_Array) return Digits_Array is
      Result : Digits_Array (0 .. A'Length - 1);
      Borrow : Long := 0;
      T      : Long;
   begin
      for I in 0 .. A'Length - 1 loop
         T := Long (A (A'First + I)) - Borrow;
         if I < B'Length then
            T := T - Long (B (B'First + I));
         end if;
         if T < 0 then
            T := T + Radix;
            Borrow := 1;
         else
            Borrow := 0;
         end if;
         Result (I) := Digit (T);
      end loop;
      return Result;
   end Subtract;

   function Multiply (A, B : Digits_Array) return Digits_Array is
      Result : Digits_Array (0 .. A'Length + B'Length - 1) := (others => 0);
      Carry  : Double;
      T      : Double;
   begin
      for I in 0 .. A'Length - 1 loop
         Carry := 0;
         for J in 0 .. B'Length - 1 loop
            T := Double (A (A'First + I)) * Double (B (B'First + J))
                   + Double (Result (I + J)) + Carry;
            Result (I + J) := Digit (T mod Radix);
            Carry := T / Radix;
         end loop;
         Result (I + B'Length) := Digit (Carry);
      end loop;
      return Result;
   end Multiply;

   --  Q := A / D, Remainder := A mod D (Q'Range = 0 .. A'Length - 1)
   procedure Divide_Digit (A         : Digits_Array;
                           D         : Digit;
                           Q         : out Digits_Array;
                           Remainder : out Digit)
   is
      R : Double := 0;
      T : Double;
   begin
      for I in reverse 0 .. A'Length - 1 loop
         T := R * Radix + Double (A (A'First + I));
         Q (I) := Digit (T / Double (D));
         R := T mod Double (D);
      end loop;
      Remainder := Digit (R);
   end Divide_Digit;

   function Floor_Shift (T : Long) return Long is
   begin
      if T >= 0 then
         return T / Radix;
      else
         return -((-(T + 1)) / Radix) - 1;
      end if;
   end Floor_Shift;

   --  Q := U / V, R := U mod V (normalized U >= V > 0, V'Length >= 2;
   --  Q'Range = 0 .. U'Length - V'Length, R'Range = 0 .. V'Length - 1)
   procedure Divide_Long (U, V : Digits_Array;
                          Q    : out Digits_Array;
                          R    : out Digits_Array)
   is
      N   : constant Natural := V'Length;
      M   : constant Natural := U'Length - N;
      Un  : Digits_Array (0 .. M + N);
      Vn  : Digits_Array (0 .. N - 1);
      S   : Natural := 0;
      Top : Digit := V (V'First + N - 1);
   begin
      --  normalize: shift so that the divisor's top digit's high bit is set
      while Top < 2 ** 31 loop
         Top := Top * 2;
         S := S + 1;
      end loop;
      for I in reverse 1 .. N - 1 loop
         Vn (I) := Digit ((Double (V (V'First + I)) * 2 ** S
                             + Double (V (V'First + I - 1)) / 2 ** (32 - S))
                            mod Radix);
      end loop;
      Vn (0) := Digit ((Double (V (V'First)) * 2 ** S) mod Radix);
      Un (M + N) := Digit (Double (U (U'First + M + N - 1)) / 2 ** (32 - S));
      for I in reverse 1 .. M + N - 1 loop
         Un (I) := Digit ((Double (U (U'First + I)) * 2 ** S
                             + Double (U (U'First + I - 1)) / 2 ** (32 - S))
                            mod Radix);
      end loop;
      Un (0) := Digit ((Double (U (U'First)) * 2 ** S) mod Radix);

      for J in reverse 0 .. M loop
         declare
            Num  : constant Double :=
              Double (Un (J + N)) * Radix + Double (Un (J + N - 1));
            Qhat : Double := Num / Double (Vn (N - 1));
            Rhat : Double := Num mod Double (Vn (N - 1));
            P    : Double;
            K, T : Long;
            C    : Double;
         begin
            loop
               exit when Qhat < Radix
                 and then Qhat * Double (Vn (N - 2))
                            <= Rhat * Radix + Double (Un (J + N - 2));
               Qhat := Qhat - 1;
               Rhat := Rhat + Double (Vn (N - 1));
               exit when Rhat >= Radix;
            end loop;
            --  multiply and subtract
            K := 0;
            for I in 0 .. N - 1 loop
               P := Qhat * Double (Vn (I));
               T := Long (Un (I + J)) - K - Long (P mod Radix);
               Un (I + J) := Digit (T mod Radix);
               K := Long (P / Radix) - Floor_Shift (T);
            end loop;
            T := Long (Un (J + N)) - K;
            Un (J + N) := Digit (T mod Radix);
            if T < 0 then
               --  (subtracted too much: add back)
               Qhat := Qhat - 1;
               C := 0;
               for I in 0 .. N - 1 loop
                  P := Double (Un (I + J)) + Double (Vn (I)) + C;
                  Un (I + J) := Digit (P mod Radix);
                  C := P / Radix;
               end loop;
               Un (J + N) := Un (J + N) + Digit (C mod Radix);
            end if;
            Q (J) := Digit (Qhat);
         end;
      end loop;
      --  unnormalize the remainder
      for I in 0 .. N - 2 loop
         R (I) := Digit ((Double (Un (I)) / 2 ** S
                            + Double (Un (I + 1)) * 2 ** (32 - S)) mod Radix);
      end loop;
      R (N - 1) := Digit (Double (Un (N - 1)) / 2 ** S);
   end Divide_Long;

   --  Quotient and remainder of the magnitudes (B nonzero)
   procedure Divide (A, B      : Digits_Array;
                     Quotient  : out Digits_Access;
                     Remainder : out Digits_Access) is
   begin
      if Compare (A, B) < 0 then
         Quotient := new Digits_Array'(Empty);
         Remainder := new Digits_Array'(A);
      elsif B'Length = 1 then
         declare
            Q : Digits_Array (0 .. A'Length - 1);
            R : Digit;
         begin
            Divide_Digit (A, B (B'First), Q, R);
            Quotient := new Digits_Array'(Q);
            Remainder := new Digits_Array'(0 => R);
         end;
      else
         declare
            Q : Digits_Array (0 .. A'Length - B'Length);
            R : Digits_Array (0 .. B'Length - 1);
         begin
            Divide_Long (A, B, Q, R);
            Quotient := new Digits_Array'(Q);
            Remainder := new Digits_Array'(R);
         end;
      end if;
   end Divide;

   --------------------
   -- Long integers --
   --------------------

   function From_Long (V : Long) return Big_Integer is
      M : Double;
   begin
      if V < 0 then
         M := Double (-(V + 1)) + 1;
      else
         M := Double (V);
      end if;
      return Make (V < 0, (0 => Digit (M mod Radix), 1 => Digit (M / Radix)));
   end From_Long;

   function From_Double (D : Double) return Big_Integer is
   begin
      return Make (False, (0 => Digit (D mod Radix), 1 => Digit (D / Radix)));
   end From_Double;

   --  The magnitude of X (at most 2**64 - 1)
   function To_Double (X : Big_Integer) return Double is
      M : constant Digits_Array := Magnitude_Of (X);
      D : Double := 0;
   begin
      for I in reverse M'Range loop
         D := D * Radix + Double (M (I));
      end loop;
      return D;
   end To_Double;

   --  X (in Long's range)
   function To_Long (X : Big_Integer) return Long is
      D : constant Double := To_Double (X);
   begin
      if not X.Negative then
         return Long (D);
      elsif D = 2 ** 63 then
         return Long'First;
      else
         return -Long (D);
      end if;
   end To_Long;

   --------------
   -- Is_Valid --
   --------------

   function Is_Valid (Arg : Big_Integer) return Boolean is
   begin
      return Arg.Valid;
   end Is_Valid;

   -----------------
   -- Comparisons --
   -----------------

   --  -1, 0 or 1 as L < R, L = R or L > R
   function Compare (L, R : Big_Integer) return Integer is
   begin
      if L.Negative /= R.Negative then
         return (if L.Negative then -1 else 1);
      elsif L.Negative then
         return Compare (Magnitude_Of (R), Magnitude_Of (L));
      else
         return Compare (Magnitude_Of (L), Magnitude_Of (R));
      end if;
   end Compare;

   function "=" (L, R : Valid_Big_Integer) return Boolean is
   begin
      return Compare (L, R) = 0;
   end "=";

   function "<" (L, R : Valid_Big_Integer) return Boolean is
   begin
      return Compare (L, R) < 0;
   end "<";

   function "<=" (L, R : Valid_Big_Integer) return Boolean is
   begin
      return Compare (L, R) <= 0;
   end "<=";

   function ">" (L, R : Valid_Big_Integer) return Boolean is
   begin
      return Compare (L, R) > 0;
   end ">";

   function ">=" (L, R : Valid_Big_Integer) return Boolean is
   begin
      return Compare (L, R) >= 0;
   end ">=";

   -----------------
   -- Conversions --
   -----------------

   function To_Big_Integer (Arg : Integer) return Valid_Big_Integer is
   begin
      return From_Long (Long (Arg));
   end To_Big_Integer;

   function To_Integer (Arg : Valid_Big_Integer) return Integer is
   begin
      if Compare (Arg, From_Long (Long (Integer'First))) < 0
        or else Compare (Arg, From_Long (Long (Integer'Last))) > 0
      then
         raise Constraint_Error;
      end if;
      return Integer (To_Long (Arg));
   end To_Integer;

   package body Signed_Conversions is

      function To_Big_Integer (Arg : Int) return Valid_Big_Integer is
      begin
         return From_Long (Long (Arg));
      end To_Big_Integer;

      function From_Big_Integer (Arg : Valid_Big_Integer) return Int is
      begin
         if Compare (Arg, From_Long (Long (Int'First))) < 0
           or else Compare (Arg, From_Long (Long (Int'Last))) > 0
         then
            raise Constraint_Error;
         end if;
         return Int (To_Long (Arg));
      end From_Big_Integer;

   end Signed_Conversions;

   package body Unsigned_Conversions is

      function To_Big_Integer (Arg : Int) return Valid_Big_Integer is
      begin
         return From_Double (Double (Arg));
      end To_Big_Integer;

      function From_Big_Integer (Arg : Valid_Big_Integer) return Int is
      begin
         if Arg.Negative
           or else Compare (Arg, From_Double (Double (Int'Last))) > 0
         then
            raise Constraint_Error;
         end if;
         return Int (To_Double (Arg));
      end From_Big_Integer;

   end Unsigned_Conversions;

   ---------------
   -- To_String --
   ---------------

   Digit_Image : constant String := "0123456789ABCDEF";

   function To_String (Arg : Valid_Big_Integer;
                       Width : Field := 0;
                       Base  : Number_Base := 10) return String
   is
      M      : constant Digits_Array := Magnitude_Of (Arg);
      Buffer : String (1 .. 32 * M'Length + 1);
      First  : Natural := Buffer'Last + 1;
      Chunk  : Double := Double (Base);
      Size   : Positive := 1;  --  (digits per chunk: Chunk = Base**Size)
   begin
      while Chunk * Double (Base) < Radix loop
         Chunk := Chunk * Double (Base);
         Size := Size + 1;
      end loop;
      declare
         Work : Digits_Array := M;
         Used : Natural := M'Length;
         Q    : Digits_Array (0 .. M'Length - 1);
         R    : Digit;
      begin
         while Used > 0 loop
            Divide_Digit (Work (0 .. Used - 1), Digit (Chunk), Q (0 .. Used - 1), R);
            Work (0 .. Used - 1) := Q (0 .. Used - 1);
            while Used > 0 and then Work (Used - 1) = 0 loop
               Used := Used - 1;
            end loop;
            for I in 1 .. Size loop
               exit when Used = 0 and then R = 0;
               First := First - 1;
               Buffer (First) := Digit_Image (Integer (R mod Digit (Base)) + 1);
               R := R / Digit (Base);
            end loop;
         end loop;
      end;
      if First > Buffer'Last then
         First := First - 1;
         Buffer (First) := '0';
      end if;
      declare
         Numeral : constant String := Buffer (First .. Buffer'Last);
         Base_Image : constant String := Integer'Image (Base);
         Literal : constant String :=
           (if Base = 10 then Numeral
            else Base_Image (2 .. Base_Image'Last) & '#' & Numeral & '#');
         Signed : constant String :=
           (if Arg.Negative then '-' & Literal else Literal);
         Pad : constant Natural := Integer'Max (0, Width - Signed'Length);
         Result : constant String (1 .. Pad + Signed'Length) :=
           (1 .. Pad => ' ') & Signed;
      begin
         return Result;
      end;
   end To_String;

   -----------------
   -- From_String --
   -----------------

   function From_String (Arg : String) return Valid_Big_Integer is
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

      --  The value of the numeral (digits of Base, with single underscores
      --  between them) that starts at I; I := the index after it
      function Numeral (Base : Positive) return Big_Integer is
         Result : Big_Integer := From_Long (0);
         B      : constant Big_Integer := From_Long (Long (Base));
         Seen   : Boolean := False;  --  (a digit before an underscore)
         Count  : Natural := 0;
         D      : Integer;
      begin
         while I <= Last loop
            if Arg (I) = '_' then
               if not Seen or else I = Last or else Value_Of (Arg (I + 1)) >= Base
               then
                  raise Constraint_Error;
               end if;
               Seen := False;
            else
               D := Value_Of (Arg (I));
               exit when D >= Base;
               Result := Result * B + From_Long (Long (D));
               Seen := True;
               Count := Count + 1;
            end if;
            I := I + 1;
         end loop;
         if Count = 0 then
            raise Constraint_Error;
         end if;
         return Result;
      end Numeral;

      Negative : Boolean := False;
      Base     : Positive := 10;
      Value    : Big_Integer;
      Exponent : Big_Integer;
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
      Value := Numeral (10);
      if I <= Last and then (Arg (I) = '#' or else Arg (I) = ':') then
         declare
            Delimiter : constant Character := Arg (I);
         begin
            if Compare (Value, From_Long (2)) < 0
              or else Compare (Value, From_Long (16)) > 0
            then
               raise Constraint_Error;
            end if;
            Base := Positive (To_Long (Value));
            I := I + 1;
            Value := Numeral (Base);
            if I > Last or else Arg (I) /= Delimiter then
               raise Constraint_Error;
            end if;
            I := I + 1;
         end;
      end if;
      if I <= Last and then (Arg (I) = 'E' or else Arg (I) = 'e') then
         I := I + 1;
         if I <= Last and then Arg (I) = '+' then
            I := I + 1;
         end if;
         Exponent := Numeral (10);
         if Compare (Exponent, From_Long (Long (Natural'Last))) > 0 then
            raise Constraint_Error;
         end if;
         Value := Value * From_Long (Long (Base)) ** Natural (To_Long (Exponent));
      end if;
      if I <= Last then
         raise Constraint_Error;
      end if;
      if Negative then
         return -Value;
      end if;
      return Value;
   end From_String;

   ---------------
   -- Put_Image --
   ---------------

   procedure Put_Image
     (Buffer : in out Ada.Strings.Text_Buffers.Root_Buffer_Type'Class;
      Arg    : in Valid_Big_Integer) is
   begin
      if Arg.Negative then
         Buffer.Put (To_String (Arg));
      else
         Buffer.Put (' ' & To_String (Arg));
      end if;
   end Put_Image;

   ---------------
   -- Operators --
   ---------------

   function "+" (L : Valid_Big_Integer) return Valid_Big_Integer is
   begin
      return L;
   end "+";

   function "-" (L : Valid_Big_Integer) return Valid_Big_Integer is
   begin
      return Make (not L.Negative, Magnitude_Of (L));
   end "-";

   function "abs" (L : Valid_Big_Integer) return Valid_Big_Integer is
   begin
      return Make (False, Magnitude_Of (L));
   end "abs";

   --  The sum of two signed magnitudes
   function Sum (A_Negative : Boolean; A : Digits_Array;
                 B_Negative : Boolean; B : Digits_Array) return Big_Integer is
   begin
      if A_Negative = B_Negative then
         return Make (A_Negative, Add (A, B));
      elsif Compare (A, B) >= 0 then
         return Make (A_Negative, Subtract (A, B));
      else
         return Make (B_Negative, Subtract (B, A));
      end if;
   end Sum;

   function "+" (L, R : Valid_Big_Integer) return Valid_Big_Integer is
   begin
      return Sum (L.Negative, Magnitude_Of (L), R.Negative, Magnitude_Of (R));
   end "+";

   function "-" (L, R : Valid_Big_Integer) return Valid_Big_Integer is
   begin
      return Sum (L.Negative, Magnitude_Of (L), not R.Negative, Magnitude_Of (R));
   end "-";

   function "*" (L, R : Valid_Big_Integer) return Valid_Big_Integer is
   begin
      return Make (L.Negative /= R.Negative,
                   Multiply (Magnitude_Of (L), Magnitude_Of (R)));
   end "*";

   --  The quotient (truncated) and remainder of L and R (R nonzero)
   procedure Divide (L, R : Big_Integer; Q, Remainder : out Big_Integer) is
      QM, RM : Digits_Access;
   begin
      if Is_Zero (R) then
         raise Constraint_Error;
      end if;
      Divide (Magnitude_Of (L), Magnitude_Of (R), QM, RM);
      Q := Make (L.Negative /= R.Negative, QM.all);
      Remainder := Make (L.Negative, RM.all);
      Free (QM);
      Free (RM);
   end Divide;

   function "/" (L, R : Valid_Big_Integer) return Valid_Big_Integer is
      Q, Remainder : Big_Integer;
   begin
      Divide (L, R, Q, Remainder);
      return Q;
   end "/";

   function "rem" (L, R : Valid_Big_Integer) return Valid_Big_Integer is
      Q, Remainder : Big_Integer;
   begin
      Divide (L, R, Q, Remainder);
      return Remainder;
   end "rem";

   function "mod" (L, R : Valid_Big_Integer) return Valid_Big_Integer is
      Q, Remainder : Big_Integer;
   begin
      Divide (L, R, Q, Remainder);
      if not Is_Zero (Remainder) and then Remainder.Negative /= R.Negative then
         return Remainder + R;
      end if;
      return Remainder;
   end "mod";

   function "**" (L : Valid_Big_Integer; R : Natural)
     return Valid_Big_Integer
   is
      Result : Big_Integer := From_Long (1);
      Factor : Big_Integer := L;
      N      : Natural := R;
   begin
      while N > 0 loop
         if N mod 2 = 1 then
            Result := Result * Factor;
         end if;
         N := N / 2;
         if N > 0 then
            Factor := Factor * Factor;
         end if;
      end loop;
      return Result;
   end "**";

   function Min (L, R : Valid_Big_Integer) return Valid_Big_Integer is
   begin
      return (if Compare (L, R) <= 0 then L else R);
   end Min;

   function Max (L, R : Valid_Big_Integer) return Valid_Big_Integer is
   begin
      return (if Compare (L, R) >= 0 then L else R);
   end Max;

   function Greatest_Common_Divisor
     (L, R : Valid_Big_Integer) return Big_Positive
   is
      A : Big_Integer := abs L;
      B : Big_Integer := abs R;
      Q, T : Big_Integer;
   begin
      if Is_Zero (A) or else Is_Zero (B) then
         raise Constraint_Error;
      end if;
      while not Is_Zero (B) loop
         Divide (A, B, Q, T);
         A := B;
         B := T;
      end loop;
      return A;
   end Greatest_Common_Divisor;

end Ada.Numerics.Big_Numbers.Big_Integers;
