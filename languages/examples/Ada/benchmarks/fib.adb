with Ada.Text_IO;
with Ada.Strings;
with Ada.Strings.Fixed;
procedure Fib is
   function Fib (N : Integer) return Integer is (if N < 2 then N else Fib (N - 1) + Fib (N - 2));
begin
   Ada.Text_IO.Put_Line (Ada.Strings.Fixed.Trim (Integer'Image (Fib (30)), Ada.Strings.Left));
end Fib;
