with Ada.Text_IO;
with Ada.Strings;
with Ada.Strings.Fixed;
procedure Mandel is
   function Mandelbrot (Size, Limit : Natural) return Natural is
      Inside : Natural := 0;
   begin
      for Y in 0 .. Size - 1 loop
         for X in 0 .. Size - 1 loop
            declare
               Cr : constant Long_Float := 2.0 * Long_Float (X) / Long_Float (Size) - 1.5;
               Ci : constant Long_Float := 2.0 * Long_Float (Y) / Long_Float (Size) - 1.0;
               Zr, Zi, T : Long_Float := 0.0;
               I : Natural := 0;
            begin
               while I < Limit and then Zr * Zr + Zi * Zi <= 4.0 loop
                  T := Zr * Zr - Zi * Zi + Cr;
                  Zi := 2.0 * Zr * Zi + Ci;
                  Zr := T;
                  I := I + 1;
               end loop;
               if I = Limit then
                  Inside := Inside + 1;
               end if;
            end;
         end loop;
      end loop;
      return Inside;
   end Mandelbrot;
begin
   Ada.Text_IO.Put_Line (Ada.Strings.Fixed.Trim (Natural'Image (Mandelbrot (600, 200)), Ada.Strings.Left));
end Mandel;
