with Ada.Text_IO;
procedure Collatz is
   type Value is range 0 .. 2 ** 63 - 1;
   function Steps (Start : Value) return Natural is
      N : Value := Start;
      S : Natural := 0;
   begin
      while N /= 1 loop
         N := (if N mod 2 = 0 then N / 2 else 3 * N + 1);
         S := S + 1;
      end loop;
      return S;
   end Steps;
   Best : Natural := 0;
   Arg : Value := 0;
begin
   for I in Value range 1 .. 1_000_000 - 1 loop
      declare
         K : constant Natural := Steps (I);
      begin
         if K > Best then
            Best := K;
            Arg := I;
         end if;
      end;
   end loop;
   Ada.Text_IO.Put_Line (Value'Image (Arg) & Natural'Image (Best));
end Collatz;
