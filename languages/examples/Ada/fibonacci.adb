--  Fibonacci numbers: recursive, iterative and memoized (an expression function, a loop, and a cache in a nested package)

with Ada.Text_IO; use Ada.Text_IO;

procedure Fibonacci is

   type Natural_64 is range 0 .. 2 ** 63 - 1;

   function Fib (N : Natural_64) return Natural_64 is
     (if N < 2 then N else Fib (N - 1) + Fib (N - 2));

   function Fib_Iterative (N : Natural_64) return Natural_64 is
      A, B : Natural_64 := 0;
   begin
      B := 1;
      for I in 1 .. N loop
         declare
            Next : constant Natural_64 := A + B;
         begin
            A := B;
            B := Next;
         end;
      end loop;
      return A;
   end Fib_Iterative;

   package Memo is
      function Fib (N : Natural_64) return Natural_64;
   end Memo;

   package body Memo is
      Cache : array (Natural_64 range 0 .. 92) of Natural_64 := (0 => 0, 1 => 1, others => 0);

      function Fib (N : Natural_64) return Natural_64 is
      begin
         if N >= 2 and then Cache (N) = 0 then
            Cache (N) := Fib (N - 1) + Fib (N - 2);
         end if;
         return Cache (N);
      end Fib;
   end Memo;

begin
   for N in Natural_64 range 0 .. 20 loop
      Put (Natural_64'Image (Fib (N)));
   end loop;
   New_Line;
   Put_Line (Natural_64'Image (Fib_Iterative (90)) & Natural_64'Image (Memo.Fib (90)));
end Fibonacci;
