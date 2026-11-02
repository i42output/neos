--  Integer types: signed ranges with overflow checks, modular (unsigned, wrapping) types with bitwise operations
--  and shifts, conversions, and Interfaces' sized types

with Ada.Text_IO; use Ada.Text_IO;
with Interfaces; use Interfaces;

procedure Integers is

   type Byte is mod 2 ** 8;
   type Word is mod 2 ** 32;
   type Percent is range 0 .. 100;

   --  (FNV-1a: wrapping 32-bit arithmetic)
   function Hash (N : Natural) return Word is
      H : Word := 2166136261;
   begin
      for I in 0 .. N - 1 loop
         H := H xor (Word (I) and 16#FF#);
         H := H * 16777619;
         H := Word (Shift_Left (Unsigned_32 (H), 5) xor Shift_Right (Unsigned_32 (H), 27)) xor Word (I);
      end loop;
      return H;
   end Hash;

   B : Byte := 250;
   P : Percent := 99;
   Big : constant Long_Long_Integer := Long_Long_Integer (Integer'Last) * 4;

begin
   B := B + 10;
   Put_Line (Byte'Image (B) & Byte'Image (not B) & Byte'Image (B xor 16#FF#) & Byte'Image (-B));
   Put_Line (Word'Image (Word'Last) & Word'Image (Word'Last + 1) & " " & Word'Image (Hash (1000)) & Word'Image (Hash (10) mod 7));
   Put_Line (Unsigned_32'Image (Shift_Left (1, 31)) & Unsigned_32'Image (Rotate_Right (16#0000_00FF#, 4)) &
             Integer_32'Image (Integer_32'First) & Unsigned_64'Image (Unsigned_64'Last));
   Put_Line (Long_Long_Integer'Image (Big) & Integer'Image (Integer'Last) & Integer'Image ((-7) / 2) & Integer'Image ((-7) mod 2) &
             Integer'Image ((-7) rem 2) & Integer'Image (2 ** 30) & Integer'Image (abs (-5)));
   begin
      P := P + 2;
      Put_Line (Percent'Image (P));
   exception
      when Constraint_Error =>
         Put_Line ("overflow: " & Percent'Image (P) & " + 2 is out of range");
   end;
   begin
      Put_Line (Integer'Image (Integer'Last + Integer (P)));
   exception
      when Constraint_Error =>
         Put_Line ("overflow: Integer'Last + 99");
   end;
   Put_Line (Integer'Image (Integer (Float'Rounding (2.5))) & Integer'Image (Integer (2.5)) & Integer'Image (Integer (-2.5)) &
             Integer'Image (Integer (Float'Truncation (-3.7))) & Integer'Image (Integer'Value ("16#FF#")) &
             Integer'Image (Character'Pos ('A')));
end Integers;
