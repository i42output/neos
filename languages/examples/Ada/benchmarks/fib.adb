with Ada.Text_IO;
procedure Fib is
   function Fib (N : Integer) return Integer is (if N < 2 then N else Fib (N - 1) + Fib (N - 2));
begin
   Ada.Text_IO.Put_Line (Integer'Image (Fib (30)));
end Fib;
