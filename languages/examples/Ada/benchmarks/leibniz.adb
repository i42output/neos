with Ada.Text_IO;
procedure Leibniz is
   Total : Long_Float := 0.0;
   Sign : Long_Float := 1.0;
begin
   for K in 0 .. 30_000_000 - 1 loop
      Total := Total + Sign / Long_Float (2 * K + 1);
      Sign := -Sign;
   end loop;
   Ada.Text_IO.Put_Line (Long_Float'Image (4.0 * Total));
end Leibniz;
