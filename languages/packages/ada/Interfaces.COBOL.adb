--  Interfaces.COBOL.adb
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

package body Interfaces.COBOL is

   function To_COBOL (Item : in String) return Alphanumeric is
      Result : Alphanumeric (1 .. Item'Length);
   begin
      for J in Result'Range loop
         Result (J) := Ada_To_COBOL (Item (Item'First + J - 1));
      end loop;
      return Result;
   end To_COBOL;

   function To_Ada (Item : in Alphanumeric) return String is
      Result : String (1 .. Item'Length);
   begin
      for J in Result'Range loop
         Result (J) := COBOL_To_Ada (Item (Item'First + J - 1));
      end loop;
      return Result;
   end To_Ada;

   procedure To_COBOL (Item       : in String;
                       Target     : out Alphanumeric;
                       Last       : out Natural) is
   begin
      if Item'Length > Target'Length then
         raise Constraint_Error;
      end if;
      for J in 0 .. Item'Length - 1 loop
         Target (Target'First + J) := Ada_To_COBOL (Item (Item'First + J));
      end loop;
      if Item'Length = 0 then
         Last := 0;
      else
         Last := Target'First + Item'Length - 1;
      end if;
   end To_COBOL;

   procedure To_Ada (Item     : in Alphanumeric;
                     Target   : out String;
                     Last     : out Natural) is
   begin
      if Item'Length > Target'Length then
         raise Constraint_Error;
      end if;
      for J in 0 .. Item'Length - 1 loop
         Target (Target'First + J) := COBOL_To_Ada (Item (Item'First + J));
      end loop;
      if Item'Length = 0 then
         Last := 0;
      else
         Last := Target'First + Item'Length - 1;
      end if;
   end To_Ada;

   package body Decimal_Conversions is

      type Int is range -2 ** 63 .. 2 ** 63 - 1;
      type Bits is mod 2 ** 64;   --  (two's complement)

      Unit : constant Num := Num'Delta;

      --  (Item as a number of Num'Delta)
      function Scaled (Item : Num) return Int is
      begin
         return Int (Item / Unit);
      end Scaled;

      --  (the largest number of Num'Delta in a value of Num)
      function Largest return Int is
         Result : Int := 1;
      begin
         for I in 1 .. Num'Digits loop
            Result := Result * 10;
         end loop;
         return Result - 1;
      end Largest;

      --  (N * Num'Delta: Conversion_Error if it is not a value of Num)
      function To_Num (N : Int) return Num is
         Billion : constant := 1_000_000_000;
      begin
         if N > Largest or else N < -Largest then
            raise Conversion_Error;
         end if;
         return (Unit * Integer (N / Billion)) * Billion + Unit * Integer (N rem Billion);
      end To_Num;

      --  (a digit's value, or -1 if it is not a digit)
      function Digit (C : COBOL_Character) return Integer is
         A : constant Character := COBOL_To_Ada (C);
      begin
         if A in '0' .. '9' then
            return Character'Pos (A) - Character'Pos ('0');
         end if;
         return -1;
      end Digit;

      --  (an overpunched digit's value (-1 if it is not one) and sign)
      procedure Overpunched (C : COBOL_Character; Value : out Integer; Negative : out Boolean) is
         A : constant Character := COBOL_To_Ada (C);
      begin
         Negative := False;
         Value := -1;
         case A is
            when '0' .. '9' => Value := Character'Pos (A) - Character'Pos ('0');
            when '{' => Value := 0;
            when 'A' .. 'I' => Value := Character'Pos (A) - Character'Pos ('A') + 1;
            when '}' => Value := 0; Negative := True;
            when 'J' .. 'R' => Value := Character'Pos (A) - Character'Pos ('J') + 1; Negative := True;
            when others => null;
         end case;
      end Overpunched;

      function To_Character (Value : Integer) return COBOL_Character is
      begin
         return Ada_To_COBOL (Character'Val (Character'Pos ('0') + Value));
      end To_Character;

      --  (N * 10 + D: Conversion_Error if that is too large)
      procedure Accumulate (N : in out Int; D : Integer) is
      begin
         if N > (Int'Last - Int (D)) / 10 then
            raise Conversion_Error;
         end if;
         N := N * 10 + Int (D);
      end Accumulate;

      --  (the digits of Item (First .. Last), and whether they are all digits)
      procedure Digits_Of (Item : Numeric; First, Last : Integer; N : out Int; Valid : out Boolean) is
         D : Integer;
      begin
         N := 0;
         Valid := First <= Last;
         for I in First .. Last loop
            D := Digit (Item (I));
            if D < 0 then
               Valid := False;
               return;
            end if;
            Accumulate (N, D);
         end loop;
      end Digits_Of;

      --  (Item's value as a number of Num'Delta, and whether it is valid)
      procedure Decode (Item : Numeric; Format : Display_Format; N : out Int; Valid : out Boolean) is
         Negative : Boolean := False;
         Sign : Character;
         D : Integer;
      begin
         N := 0;
         Valid := False;
         if Item'Length = 0 then
            return;
         end if;
         case Format is
            when Unsigned_Display =>
               Digits_Of (Item, Item'First, Item'Last, N, Valid);
            when Leading_Separate_Display =>
               Sign := COBOL_To_Ada (Item (Item'First));
               Negative := Sign = '-';
               Digits_Of (Item, Item'First + 1, Item'Last, N, Valid);
               Valid := Valid and then (Sign = '+' or else Sign = '-');
            when Trailing_Separate_Display =>
               Sign := COBOL_To_Ada (Item (Item'Last));
               Negative := Sign = '-';
               Digits_Of (Item, Item'First, Item'Last - 1, N, Valid);
               Valid := Valid and then (Sign = '+' or else Sign = '-');
            when Leading_Nonseparate_Display =>
               Overpunched (Item (Item'First), D, Negative);
               if D < 0 then
                  return;
               end if;
               N := Int (D);
               Valid := True;
               for I in Item'First + 1 .. Item'Last loop
                  D := Digit (Item (I));
                  if D < 0 then
                     Valid := False;
                     return;
                  end if;
                  Accumulate (N, D);
               end loop;
            when Trailing_Nonseparate_Display =>
               Overpunched (Item (Item'Last), D, Negative);
               if D < 0 then
                  return;
               end if;
               if Item'Length = 1 then
                  N := 0;
                  Valid := True;
               else
                  Digits_Of (Item, Item'First, Item'Last - 1, N, Valid);
               end if;
               if Valid then
                  Accumulate (N, D);
               end if;
         end case;
         if Negative then
            N := -N;
         end if;
      end Decode;

      function Valid (Item   : in Numeric;
                      Format : in Display_Format) return Boolean is
         N : Int;
         Ok : Boolean;
      begin
         Decode (Item, Format, N, Ok);
         return Ok;
      exception
         when Conversion_Error =>
            return True;   --  (well formed, but too large for Num)
      end Valid;

      function Length (Format : in Display_Format) return Natural is
      begin
         case Format is
            when Leading_Separate_Display | Trailing_Separate_Display =>
               return Num'Digits + 1;
            when others =>
               return Num'Digits;
         end case;
      end Length;

      function To_Decimal (Item   : in Numeric;
                           Format : in Display_Format) return Num is
         N : Int;
         Ok : Boolean;
      begin
         Decode (Item, Format, N, Ok);
         if not Ok then
            raise Conversion_Error;
         end if;
         return To_Num (N);
      end To_Decimal;

      function To_Display (Item   : in Num;
                           Format : in Display_Format) return Numeric is
         N : Int := Scaled (Item);
         Negative : constant Boolean := N < 0;
         Result : Numeric (1 .. Length (Format));
         First, Last : Positive;
      begin
         if Negative and then Format = Unsigned_Display then
            raise Conversion_Error;
         end if;
         N := abs N;
         First := Result'First;
         Last := Result'Last;
         case Format is
            when Leading_Separate_Display =>
               Result (First) := Ada_To_COBOL (if Negative then '-' else '+');
               First := First + 1;
            when Trailing_Separate_Display =>
               Result (Last) := Ada_To_COBOL (if Negative then '-' else '+');
               Last := Last - 1;
            when others =>
               null;
         end case;
         for I in reverse First .. Last loop
            Result (I) := To_Character (Integer (N rem 10));
            N := N / 10;
         end loop;
         if Negative then
            declare
               At_Sign : constant Positive :=
                 (if Format = Leading_Nonseparate_Display then First else Last);
               D : constant Integer := Digit (Result (At_Sign));
            begin
               if Format = Leading_Nonseparate_Display or else Format = Trailing_Nonseparate_Display then
                  Result (At_Sign) := Ada_To_COBOL
                    (if D = 0 then '}' else Character'Val (Character'Pos ('J') + D - 1));
               end if;
            end;
         end if;
         return Result;
      end To_Display;

      --  Packed formats

      procedure Decode (Item : Packed_Decimal; Format : Packed_Format; N : out Int; Valid : out Boolean) is
         Sign : Decimal_Element;
      begin
         N := 0;
         Valid := False;
         if Item'Length < 2 then
            return;
         end if;
         Sign := Item (Item'Last);
         case Format is
            when Packed_Unsigned_Format =>
               if Sign /= 16#F# then
                  return;
               end if;
            when Packed_Signed_Format =>
               if Sign < 16#A# then
                  return;
               end if;
         end case;
         for I in Item'First .. Item'Last - 1 loop
            if Item (I) > 9 then
               return;
            end if;
         end loop;
         Valid := True;
         for I in Item'First .. Item'Last - 1 loop
            Accumulate (N, Integer (Item (I)));
         end loop;
         if Format = Packed_Signed_Format and then (Sign = 16#B# or else Sign = 16#D#) then
            N := -N;
         end if;
      end Decode;

      function Valid (Item   : in Packed_Decimal;
                      Format : in Packed_Format) return Boolean is
         N : Int;
         Ok : Boolean;
      begin
         Decode (Item, Format, N, Ok);
         return Ok;
      exception
         when Conversion_Error =>
            return True;
      end Valid;

      function Length (Format : in Packed_Format) return Natural is
      begin
         return Num'Digits + 1;
      end Length;

      function To_Decimal (Item   : in Packed_Decimal;
                           Format : in Packed_Format) return Num is
         N : Int;
         Ok : Boolean;
      begin
         Decode (Item, Format, N, Ok);
         if not Ok then
            raise Conversion_Error;
         end if;
         return To_Num (N);
      end To_Decimal;

      function To_Packed (Item   : in Num;
                          Format : in Packed_Format) return Packed_Decimal is
         N : Int := Scaled (Item);
         Result : Packed_Decimal (1 .. Length (Format));
      begin
         if N < 0 and then Format = Packed_Unsigned_Format then
            raise Conversion_Error;
         end if;
         if Format = Packed_Unsigned_Format then
            Result (Result'Last) := 16#F#;
         elsif N < 0 then
            Result (Result'Last) := 16#D#;
         else
            Result (Result'Last) := 16#C#;
         end if;
         N := abs N;
         for I in reverse Result'First .. Result'Last - 1 loop
            Result (I) := Decimal_Element (N rem 10);
            N := N / 10;
         end loop;
         return Result;
      end To_Packed;

      --  Binary formats

      procedure Decode (Item : Byte_Array; Format : Binary_Format; N : out Int; Valid : out Boolean) is
         Value : Bits := 0;
         Top : Bits;
      begin
         N := 0;
         Valid := Item'Length in 1 .. 8;
         if not Valid then
            return;
         end if;
         for J in 0 .. Item'Length - 1 loop
            if Format = High_Order_First_Binary then
               Value := Value * 256 + Bits (Item (Item'First + J));
            else
               Value := Value * 256 + Bits (Item (Item'Last - J));
            end if;
         end loop;
         --  (sign extension)
         Top := 2 ** (8 * Item'Length - 1);
         if Value >= Top then
            Value := Value - 2 * Top;   --  (modulo 2 ** 64)
         end if;
         if Value >= 2 ** 63 then
            N := -Int (not Value) - 1;
         else
            N := Int (Value);
         end if;
         Valid := abs N <= Largest;
      end Decode;

      function Valid (Item   : in Byte_Array;
                      Format : in Binary_Format) return Boolean is
         N : Int;
         Ok : Boolean;
      begin
         Decode (Item, Format, N, Ok);
         return Ok;
      end Valid;

      function Length (Format : in Binary_Format) return Natural is
         Limit : Int := 127;
         Result : Natural := 1;
      begin
         while Largest > Limit loop
            Result := Result + 1;
            Limit := Limit * 256 + 255;
         end loop;
         return Result;
      end Length;

      function To_Decimal (Item   : in Byte_Array;
                           Format : in Binary_Format) return Num is
         N : Int;
         Ok : Boolean;
      begin
         Decode (Item, Format, N, Ok);
         if not Ok then
            raise Conversion_Error;
         end if;
         return To_Num (N);
      end To_Decimal;

      function To_Binary (Item   : in Num;
                          Format : in Binary_Format) return Byte_Array is
         Value : Bits := Bits'Mod (Scaled (Item));
         Result : Byte_Array (1 .. Length (Format));
      begin
         for J in 0 .. Result'Length - 1 loop
            if Format = High_Order_First_Binary then
               Result (Result'Last - J) := Byte (Value mod 256);
            else
               Result (Result'First + J) := Byte (Value mod 256);
            end if;
            Value := Value / 256;
         end loop;
         return Result;
      end To_Binary;

      function To_Decimal (Item : in Binary) return Num is
      begin
         return To_Num (Int (Item));
      end To_Decimal;

      function To_Decimal (Item : in Long_Binary) return Num is
      begin
         return To_Num (Int (Item));
      end To_Decimal;

      function To_Binary (Item : in Num) return Binary is
         N : constant Int := Scaled (Item);
      begin
         if N not in Int (Binary'First) .. Int (Binary'Last) then
            raise Conversion_Error;
         end if;
         return Binary (N);
      end To_Binary;

      function To_Long_Binary (Item : in Num) return Long_Binary is
      begin
         return Long_Binary (Scaled (Item));
      end To_Long_Binary;

   end Decimal_Conversions;

end Interfaces.COBOL;
