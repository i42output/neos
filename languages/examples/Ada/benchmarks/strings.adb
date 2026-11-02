with Ada.Text_IO;
procedure Strings is
   Total : Natural := 0;
begin
   for I in 0 .. 300_000 - 1 loop
      declare
         T : constant String := "item" & Natural'Image (I);
      begin
         Total := Total + T'Length;
      end;
   end loop;
   Ada.Text_IO.Put_Line (Natural'Image (Total));
end Strings;
