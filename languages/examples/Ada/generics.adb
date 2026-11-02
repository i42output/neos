--  Generics and subprograms: generic counters (state per instance), nested subprograms that see their
--  enclosing frame, access-to-subprogram values and a generic composition

with Ada.Text_IO; use Ada.Text_IO;

procedure Generics is

   generic
      Start : Integer := 0;
      Step  : Integer := 1;
   package Counter is
      function Next return Integer;
   end Counter;

   package body Counter is
      Count : Integer := Start;
      function Next return Integer is
      begin
         Count := Count + Step;
         return Count;
      end Next;
   end Counter;

   package C1 is new Counter;
   package C2 is new Counter (Start => 100, Step => 10);

   type Integer_Function is access function (X : Integer) return Integer;

   function Square (X : Integer) return Integer is (X * X);
   function Double (X : Integer) return Integer is (X * 2);
   function Increment (X : Integer) return Integer is (X + 1);

   generic
      with function F (X : Integer) return Integer;
      with function G (X : Integer) return Integer;
   function Compose (X : Integer) return Integer;

   function Compose (X : Integer) return Integer is (F (G (X)));

   function Increment_Then_Double is new Compose (F => Double, G => Increment);

   type Integer_Array is array (Positive range <>) of Integer;

   function Map (F : not null Integer_Function; Values : Integer_Array) return Integer_Array is
      Result : Integer_Array (Values'Range);
   begin
      for I in Values'Range loop
         Result (I) := F (Values (I));
      end loop;
      return Result;
   end Map;

   procedure Put (Values : Integer_Array) is
   begin
      for V of Values loop
         Put (Integer'Image (V));
      end loop;
      New_Line;
   end Put;

   --  (a nested subprogram updating its enclosing subprogram's variable)
   function Sum (Values : Integer_Array) return Integer is
      Total : Integer := 0;
      procedure Add (V : Integer) is
      begin
         Total := Total + V;
      end Add;
   begin
      for V of Values loop
         Add (V);
      end loop;
      return Total;
   end Sum;

   Functions : constant array (1 .. 3) of Integer_Function := (Square'Access, Double'Access, Increment'Access);

begin
   Put_Line (Integer'Image (C1.Next) & Integer'Image (C1.Next) & Integer'Image (C2.Next) & Integer'Image (C1.Next) &
             Integer'Image (C2.Next));
   for F of Functions loop
      Put (Map (F, (1, 2, 3)));
   end loop;
   Put_Line (Integer'Image (Sum ((1, 2, 3, 4))) & Integer'Image (Increment_Then_Double (20)));
end Generics;
