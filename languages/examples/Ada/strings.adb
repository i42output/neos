--  Strings: fixed strings and slices, Ada.Strings.Fixed and Unbounded, character maps, searching, case,
--  and splitting words into a sorted container

with Ada.Text_IO; use Ada.Text_IO;
with Ada.Strings; use Ada.Strings;
with Ada.Strings.Fixed; use Ada.Strings.Fixed;
with Ada.Strings.Maps.Constants; use Ada.Strings.Maps;
with Ada.Strings.Unbounded; use Ada.Strings.Unbounded;
with Ada.Characters.Handling; use Ada.Characters.Handling;
with Ada.Containers.Indefinite_Ordered_Sets;

procedure Strings is

   package Word_Sets is new Ada.Containers.Indefinite_Ordered_Sets (String);

   Text : constant String := "The quick brown fox jumps over the lazy dog";
   Words : Word_Sets.Set;
   First : Positive := Text'First;
   Last : Natural;
   Joined : Unbounded_String;

begin
   --  (the words: each between spaces)
   while First <= Text'Last loop
      Find_Token (Text (First .. Text'Last), Constants.Letter_Set, Inside, First, Last);
      exit when Last = 0;
      Words.Include (To_Lower (Text (First .. Last)));
      First := Last + 1;
   end loop;
   for W of Words loop
      if Length (Joined) > 0 then
         Append (Joined, ",");
      end if;
      Append (Joined, W);
   end loop;
   Put_Line (To_String (Joined));

   Put_Line (Natural'Image (Index (Text, "brown")) & " " & Text (Index (Text, "fox") - 6 .. Index (Text, "fox") - 2));
   Put_Line (Translate (Text, Constants.Upper_Case_Map));
   Put_Line (Natural'Image (Count (Text, "o")) & " " & Trim ("   padded   ", Both) & "|" & Head ("abc", 6, '*') & "|" &
             Tail ("abc", 6, '.'));

   --  (a date's fields)
   declare
      Date : constant String := "2026-10-25";
      Year : constant Natural := Natural'Value (Date (1 .. 4));
      Month : constant Natural := Natural'Value (Date (6 .. 7));
      Day : constant Natural := Natural'Value (Date (9 .. 10));
      function Two (N : Natural) return String is (Tail (Trim (Natural'Image (N), Left), 2, '0'));
   begin
      Put_Line (Two (Day) & "/" & Two (Month) & "/" & Trim (Natural'Image (Year), Left));
   end;

   Put_Line (3 * "abc" & " " & Replace_Slice ("hello world", 1, 5, "HELLO") & " " & Overwrite ("abcdef", 3, "XY"));
   declare
      Reversed : String (Text'Range);
   begin
      for I in Text'Range loop
         Reversed (Text'Last - I + Text'First) := Text (I);
      end loop;
      Put_Line (Reversed);
   end;
end Strings;
