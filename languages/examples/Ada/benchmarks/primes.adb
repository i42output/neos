with Ada.Text_IO;
with Ada.Strings;
with Ada.Strings.Fixed;
procedure Primes is
   function Is_Prime (N : Natural) return Boolean is
      D : Natural := 2;
   begin
      if N < 2 then
         return False;
      end if;
      while D * D <= N loop
         if N mod D = 0 then
            return False;
         end if;
         D := D + 1;
      end loop;
      return True;
   end Is_Prime;
   Count : Natural := 0;
begin
   for N in 0 .. 2_000_000 - 1 loop
      if Is_Prime (N) then
         Count := Count + 1;
      end if;
   end loop;
   Ada.Text_IO.Put_Line (Ada.Strings.Fixed.Trim (Natural'Image (Count), Ada.Strings.Left));
end Primes;
