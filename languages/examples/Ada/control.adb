--  Control flow: loops (with exit and continue-like goto), named loops, while loops, case statements with ranges
--  and choices, if expressions and case expressions

with Ada.Text_IO; use Ada.Text_IO;

procedure Control is

   function Name (N : Integer) return String is
     (case N is
         when 0 => "zero",
         when 1 | 2 => "small",
         when 100 => "hundred",
         when others => "other");

   Total : Natural := 0;
   N : Positive := 27;
   Steps : Natural := 0;
   Count : Natural := 0;
   K : Natural := 0;
   Log : String (1 .. 12) := (others => ' ');
   Last : Natural := 0;

begin
   for I in 0 .. 99 loop
      if I mod 3 /= 0 then
         exit when I > 50;
         Total := Total + I;
      end if;
   end loop;
   Put_Line ("total" & Natural'Image (Total));

   while N /= 1 loop
      N := (if N mod 2 = 0 then N / 2 else 3 * N + 1);
      Steps := Steps + 1;
   end loop;
   Put_Line (Natural'Image (Steps));

   loop
      K := K + 2;
      exit when K >= 7;
   end loop;
   Put_Line (Natural'Image (K));

   Outer :
   for A in 0 .. 4 loop
      for B in 0 .. 4 loop
         exit when B > A;
         Count := Count + 1;
         exit Outer when Count = 12;
      end loop;
   end loop Outer;
   Put_Line ("pairs" & Natural'Image (Count));

   for I in 0 .. 3 loop
      Put_Line (Integer'Image (I) & " " & Name (I));
   end loop;
   Put_Line (Name (100) & " " & Name (-1));

   for I in 0 .. 5 loop
      case I mod 3 is
         when 0 =>
            Last := Last + 1;
            Log (Last) := 'a';
            Last := Last + 1;
            Log (Last) := 'b';
         when 1 =>
            Last := Last + 1;
            Log (Last) := 'b';
         when others =>
            if I > 3 then
               goto Continue;
            end if;
            Last := Last + 1;
            Log (Last) := 'c';
      end case;
      Last := Last + 1;
      Log (Last) := '|';
   <<Continue>>
   end loop;
   Put_Line (Log (1 .. Last));

   for C of String'("xyzq") loop
      Put (Integer'Image (case C is when 'x' | 'y' => 1, when 'z' => 2, when others => 0));
   end loop;
   New_Line;
   for I in reverse 1 .. 5 loop
      Put (Integer'Image (I));
   end loop;
   New_Line;
end Control;
