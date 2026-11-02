--  Ada.Decimal.adb
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

package body Ada.Decimal is

   procedure Divide (Dividend  : in Dividend_Type;
                     Divisor   : in Divisor_Type;
                     Quotient  : out Quotient_Type;
                     Remainder : out Remainder_Type) is

      --  The remainder is computed exactly with the values as integers (each
      --  value as a number of its type's delta): Dividend - Divisor *
      --  Quotient as a number of 10.0 ** (-S), S being the larger of the
      --  scale of Divisor * Quotient and that of Remainder_Type, the
      --  remainder's magnitude (less than Divisor * Quotient_Type'Delta) being
      --  representable in 64 bits when the result is a value of
      --  Remainder_Type. (The products are computed modulo 2 ** 64.)

      type Int is range -2 ** 63 .. 2 ** 63 - 1;
      type Bits is mod 2 ** 64;

      Dividend_Unit  : constant Dividend_Type := Dividend_Type'Delta;
      Divisor_Unit   : constant Divisor_Type := Divisor_Type'Delta;
      Quotient_Unit  : constant Quotient_Type := Quotient_Type'Delta;
      Remainder_Unit : constant Remainder_Type := Remainder_Type'Delta;

      function Power_Of_Ten (N : Natural) return Bits is
         Result : Bits := 1;
      begin
         for I in 1 .. N loop
            Result := Result * 10;
         end loop;
         return Result;
      end Power_Of_Ten;

      function To_Int (B : Bits) return Int is
      begin
         if B >= 2 ** 63 then
            return -Int (not B) - 1;
         end if;
         return Int (B);
      end To_Int;

      Q : constant Quotient_Type := Quotient_Type (Dividend / Divisor);
      D_Int : constant Int := Int (Dividend / Dividend_Unit);
      V_Int : constant Int := Int (Divisor / Divisor_Unit);
      Q_Int : constant Int := Int (Q / Quotient_Unit);
      Product_Scale : constant Integer := Divisor_Type'Scale + Quotient_Type'Scale;
      S : constant Integer := Integer'Max (Remainder_Type'Scale, Product_Scale);
      P : constant Bits := Bits'Mod (V_Int) * Bits'Mod (Q_Int) * Power_Of_Ten (S - Product_Scale);
      D_Part, Fraction : Int;
      A, R : Int;
      Billion : constant := 1_000_000_000;
   begin
      Quotient := Q;
      --  (Dividend as a number of 10.0 ** (-S): its integral part and the sign
      --  of its fractional part)
      if S >= Dividend_Type'Scale then
         D_Part := To_Int (Bits'Mod (D_Int) * Power_Of_Ten (S - Dividend_Type'Scale));
         Fraction := 0;
      else
         D_Part := D_Int / Int (Power_Of_Ten (Dividend_Type'Scale - S));
         Fraction := D_Int rem Int (Power_Of_Ten (Dividend_Type'Scale - S));
      end if;
      --  (the remainder as a number of 10.0 ** (-S), truncated)
      A := To_Int (Bits'Mod (D_Part) - P);
      if A > 0 and then Fraction < 0 then
         A := A - 1;
      elsif A < 0 and then Fraction > 0 then
         A := A + 1;
      end if;
      --  (as a number of Remainder_Type'Delta, truncated)
      R := A / Int (Power_Of_Ten (S - Remainder_Type'Scale));
      Remainder := (Remainder_Unit * Integer (R / Billion)) * Billion
                     + Remainder_Unit * Integer (R rem Billion);
   end Divide;

end Ada.Decimal;
