with Ada.Text_IO;
with Ada.Strings;
with Ada.Strings.Fixed;
procedure Range_Benchmark is
   function F (L, M : Natural) return Long_Long_Integer is
      Count : Long_Long_Integer := 0;
   begin
      for I in 0 .. L - 1 loop
         for J in 0 .. M - 1 loop
            Count := Count + 1;
         end loop;
      end loop;
      return Count;
   end F;
begin
   Ada.Text_IO.Put_Line (Ada.Strings.Fixed.Trim (Long_Long_Integer'Image (F (99_999 + 1, 9_999 + 1)), Ada.Strings.Left));
end Range_Benchmark;
