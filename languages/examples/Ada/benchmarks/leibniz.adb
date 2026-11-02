with Ada.Text_IO;
with Ada.Long_Float_Text_IO;
procedure Leibniz is
   Total : Long_Float := 0.0;
   Sign : Long_Float := 1.0;
begin
   for K in 0 .. 30_000_000 - 1 loop
      Total := Total + Sign / Long_Float (2 * K + 1);
      Sign := -Sign;
   end loop;
   Ada.Long_Float_Text_IO.Put (4.0 * Total, Fore => 1, Aft => 13, Exp => 0);
   Ada.Text_IO.New_Line;
end Leibniz;
