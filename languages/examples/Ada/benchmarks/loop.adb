with Ada.Text_IO;
with Ada.Strings;
with Ada.Strings.Fixed;
procedure Loop_Benchmark is
   Total : Long_Long_Integer := 0;
begin
   for I in Long_Long_Integer range 0 .. 30_000_000 - 1 loop
      Total := Total + (I * I) mod 7;
   end loop;
   Ada.Text_IO.Put_Line (Ada.Strings.Fixed.Trim (Long_Long_Integer'Image (Total), Ada.Strings.Left));
end Loop_Benchmark;
