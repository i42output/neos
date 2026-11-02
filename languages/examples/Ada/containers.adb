--  Containers: vectors (sorting, cursors), ordered and hashed maps, sets, and a doubly linked list

with Ada.Text_IO; use Ada.Text_IO;
with Ada.Containers; use Ada.Containers;
with Ada.Containers.Vectors;
with Ada.Containers.Ordered_Maps;
with Ada.Containers.Indefinite_Hashed_Maps;
with Ada.Containers.Ordered_Sets;
with Ada.Containers.Doubly_Linked_Lists;
with Ada.Strings.Hash;

procedure Containers is

   package Integer_Vectors is new Ada.Containers.Vectors (Positive, Integer);
   package Sorting is new Integer_Vectors.Generic_Sorting;
   package Squares is new Ada.Containers.Ordered_Maps (Integer, Integer);
   package Counts is new Ada.Containers.Indefinite_Hashed_Maps (String, Natural, Ada.Strings.Hash, "=");
   package Integer_Sets is new Ada.Containers.Ordered_Sets (Integer);
   package Integer_Lists is new Ada.Containers.Doubly_Linked_Lists (Integer);

   type Integer_Array is array (Positive range <>) of Integer;

   V : Integer_Vectors.Vector;
   M : Squares.Map;
   C : Counts.Map;
   S, T : Integer_Sets.Set;
   L : Integer_Lists.List;

   procedure Count (Word : String) is
   begin
      if C.Contains (Word) then
         C.Replace (Word, C.Element (Word) + 1);
      else
         C.Insert (Word, 1);
      end if;
   end Count;

begin
   for X of Integer_Array'(5, 3, 9, 1, 7) loop
      V.Append (X);
   end loop;
   Sorting.Sort (V);
   for X of V loop
      Put (Integer'Image (X));
   end loop;
   Put_Line (" (length" & Count_Type'Image (V.Length) & ", first" & Integer'Image (V.First_Element) & ")");

   for I in 1 .. 5 loop
      M.Insert (I, I * I);
   end loop;
   for Position in M.Iterate loop
      Put (Integer'Image (Squares.Key (Position)) & " =>" & Integer'Image (Squares.Element (Position)));
   end loop;
   New_Line;

   Count ("apple");
   Count ("pear");
   Count ("apple");
   Put_Line ("apple" & Natural'Image (C.Element ("apple")) & ", pear" & Natural'Image (C.Element ("pear")) &
             ", contains plum: " & Boolean'Image (C.Contains ("plum")));

   for X in 1 .. 6 loop
      S.Include (X);
      T.Include (X * 2);
   end loop;
   for X of Integer_Sets.Intersection (S, T) loop
      Put (Integer'Image (X));
   end loop;
   Put_Line (" |" & Count_Type'Image (Integer_Sets.Union (S, T).Length));

   L.Append (2);
   L.Prepend (1);
   L.Append (3);
   L.Reverse_Elements;
   for X of L loop
      Put (Integer'Image (X));
   end loop;
   New_Line;
end Containers;
