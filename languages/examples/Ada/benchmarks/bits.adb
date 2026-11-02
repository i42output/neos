--  integer hashing with 32-bit modular operations
with Ada.Text_IO;
with Interfaces; use Interfaces;
procedure Bits is
   function Hash (N : Natural) return Unsigned_32 is
      H : Unsigned_32 := 2166136261;
   begin
      for I in 0 .. N - 1 loop
         H := H xor (Unsigned_32 (I) and 16#FF#);
         H := H * 16777619;
         H := Shift_Left (H, 5) xor Shift_Right (H, 27) xor Unsigned_32 (I);
      end loop;
      return H;
   end Hash;
begin
   Ada.Text_IO.Put_Line (Unsigned_32'Image (Hash (20_000_000)));
end Bits;
