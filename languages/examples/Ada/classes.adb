--  Tagged types: operators, primitive operations, inheritance, dispatching and class-wide types

with Ada.Text_IO; use Ada.Text_IO;
with Ada.Numerics.Elementary_Functions; use Ada.Numerics.Elementary_Functions;

procedure Classes is

   type Vector is record
      X, Y : Float;
   end record;

   function "+" (A, B : Vector) return Vector is (A.X + B.X, A.Y + B.Y);
   function Length (V : Vector) return Float is (Sqrt (V.X ** 2 + V.Y ** 2));
   function Image (V : Vector) return String is
     ("(" & Integer'Image (Integer (V.X)) & "," & Integer'Image (Integer (V.Y)) & " )");

   package Animals is
      type Animal is tagged record
         Name : access constant String;
      end record;
      function Sound (A : Animal) return String is ("...");
      function Speak (A : Animal'Class) return String is (A.Name.all & " says " & A.Sound);

      type Dog is new Animal with null record;
      overriding function Sound (D : Dog) return String is ("woof");
      function Fetch (D : Dog) return String is (D.Name.all & " fetches the ball");

      type Cat is new Animal with record
         Lives : Natural := 9;
      end record;
      overriding function Sound (C : Cat) return String is ("meow");
   end Animals;
   use Animals;

   A : constant Vector := (3.0, 4.0);
   B : constant Vector := (1.0, 2.0);
   Rex : constant Dog := (Name => new String'("Rex"));
   Tom : constant Cat := (Name => new String'("Tom"), Lives => 9);
   Pets : constant array (1 .. 2) of access constant Animal'Class := (Rex'Unchecked_Access, Tom'Unchecked_Access);

begin
   Put_Line (Image (A + B) & Float'Image (Length (A)) & " " & Boolean'Image (A = (3.0, 4.0)));
   for P of Pets loop
      Put_Line (Speak (P.all));
   end loop;
   Put_Line (Rex.Fetch);
   Put_Line (Boolean'Image (Pets (1).all in Dog'Class) & " " & Boolean'Image (Pets (2).all in Dog'Class) &
             Natural'Image (Cat (Pets (2).all).Lives));
end Classes;
