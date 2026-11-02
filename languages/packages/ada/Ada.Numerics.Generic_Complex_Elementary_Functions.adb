--  Ada.Numerics.Generic_Complex_Elementary_Functions.adb
--
--  Copyright (c) 2026 Leigh Johnston.  All Rights Reserved.
--
--  Redistribution and use in source and binary forms, with or without
--  modification, are permitted provided that the following conditions are
--  met:
--
--     * Redistributions of source code must retain the above copyright
--       notice, this list of conditions and the following disclaimer.
--
--     * Redistributions in binary form must reproduce the above copyright
--       notice, this list of conditions and the following disclaimer in the
--       documentation and/or other materials provided with the distribution.
--
--     * Neither the name of the copyright holder nor the names of its
--       contributors may be used to endorse or promote products derived from
--       this software without specific prior written permission.
--
--  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
--  IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
--  THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
--  PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
--  CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
--  EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
--  PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
--  PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
--  LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
--  NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
--  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

--  The inverse trigonometric and hyperbolic functions use W. Kahan's
--  formulas ("Branch Cuts for Complex Elementary Functions", 1987), which
--  place the branch cuts as the Reference Manual requires (G.1.2(13-25)) and
--  respect the signs of zero components on the cuts. Expressions such as
--  1.0 - X are computed by components (not by promoting 1.0 to Complex) so
--  that the sign of a zero imaginary component is kept.

with Ada.Numerics.Generic_Elementary_Functions;

package body Ada.Numerics.Generic_Complex_Elementary_Functions is

   subtype R is Real'Base;

   package Elementary is new Ada.Numerics.Generic_Elementary_Functions (R);

   Half_Pi : constant := Ada.Numerics.Pi / 2.0;

   --  beyond this magnitude of the imaginary component, Tan's real component
   --  is negligible against its imaginary one (which rounds to 1.0)
   Large : constant R := 0.35 * R (R'Machine_Mantissa) + 1.0;

   --  below this magnitude, Arctanh (X) is X to machine precision
   Small : constant R := 2.0 ** (-(R'Machine_Mantissa / 2));

   function Cartesian (Re, Im : R) return Complex is
   begin
      return Complex'(Re => Re, Im => Im);
   end Cartesian;

   function Is_Zero (X : Complex) return Boolean is
   begin
      return X.Re = 0.0 and then X.Im = 0.0;
   end Is_Zero;

   --  Atan2, but zero (rather than Argument_Error) at the origin
   function Angle (Y, X : R) return R is
   begin
      if X = 0.0 and then Y = 0.0 then
         return 0.0;
      end if;
      return Elementary.Arctan (Y, X);
   end Angle;

   ----------
   -- Sqrt --
   ----------

   function Sqrt (X : Complex) return Complex is
      A : constant R := X.Re;
      B : constant R := X.Im;
      T : R;
   begin
      if Is_Zero (X) then
         return X;
      elsif B = 0.0 then
         if A > 0.0 then
            return Cartesian (Elementary.Sqrt (A), B);
         else
            return Cartesian (0.0, R'Copy_Sign (Elementary.Sqrt (-A), B));
         end if;
      end if;
      if abs A > R'Last / 4.0 or else abs B > R'Last / 4.0 then
         --  (scaled: the modulus of a large X would overflow)
         T := 2.0 * Elementary.Sqrt
           (abs (A / 4.0) / 2.0 + Modulus (Cartesian (A / 4.0, B / 4.0)) / 2.0);
      else
         T := Elementary.Sqrt (abs A / 2.0 + Modulus (X) / 2.0);
      end if;
      if A >= 0.0 then
         return Cartesian (T, B / (2.0 * T));
      else
         return Cartesian (abs B / (2.0 * T), R'Copy_Sign (T, B));
      end if;
   end Sqrt;

   ---------
   -- Log --
   ---------

   function Log (X : Complex) return Complex is
   begin
      if Is_Zero (X) then
         raise Constraint_Error;
      end if;
      return Cartesian (Elementary.Log (Modulus (X)), Argument (X));
   end Log;

   ---------
   -- Exp --
   ---------

   function Exp (X : Complex) return Complex is
      E : R;
   begin
      if Is_Zero (X) then
         return Cartesian (1.0, 0.0);
      end if;
      E := Elementary.Exp (X.Re);
      if X.Im = 0.0 then
         return Cartesian (E, X.Im);
      end if;
      return Cartesian (E * Elementary.Cos (X.Im), E * Elementary.Sin (X.Im));
   end Exp;

   function Exp (X : Imaginary) return Complex is
      Y : constant R := Im (X);
   begin
      if Y = 0.0 then
         return Cartesian (1.0, Y);
      end if;
      return Cartesian (Elementary.Cos (Y), Elementary.Sin (Y));
   end Exp;

   ----------
   -- "**" --
   ----------

   function "**" (Left : Complex; Right : Complex) return Complex is
   begin
      if Is_Zero (Left) then
         if Right.Re = 0.0 then
            raise Ada.Numerics.Argument_Error;
         elsif Right.Re < 0.0 then
            raise Constraint_Error;
         end if;
         return Left;
      elsif Is_Zero (Right) then
         return Cartesian (1.0, 0.0);
      elsif Right.Re = 1.0 and then Right.Im = 0.0 then
         return Left;
      elsif Left.Re = 1.0 and then Left.Im = 0.0 then
         return Cartesian (1.0, 0.0);
      end if;
      return Exp (Right * Log (Left));
   end "**";

   function "**" (Left : Complex; Right : Real'Base) return Complex is
   begin
      if Is_Zero (Left) then
         if Right = 0.0 then
            raise Ada.Numerics.Argument_Error;
         elsif Right < 0.0 then
            raise Constraint_Error;
         end if;
         return Left;
      elsif Right = 0.0 then
         return Cartesian (1.0, 0.0);
      elsif Right = 1.0 then
         return Left;
      elsif Left.Re = 1.0 and then Left.Im = 0.0 then
         return Cartesian (1.0, 0.0);
      end if;
      return Compose_From_Polar
               (Elementary."**" (Modulus (Left), Right),
                Argument (Left) * Right);
   end "**";

   function "**" (Left : Real'Base; Right : Complex) return Complex is
   begin
      if Left = 0.0 then
         if Right.Re = 0.0 then
            raise Ada.Numerics.Argument_Error;
         elsif Right.Re < 0.0 then
            raise Constraint_Error;
         end if;
         return Cartesian (0.0, 0.0);
      elsif Is_Zero (Right) or else Left = 1.0 then
         return Cartesian (1.0, 0.0);
      elsif Right.Re = 1.0 and then Right.Im = 0.0 then
         return Cartesian (Left, 0.0);
      elsif Left > 0.0 then
         return Exp (Right * Elementary.Log (Left));
      end if;
      return Exp (Right * Log (Cartesian (Left, 0.0)));
   end "**";

   ---------
   -- Sin --
   ---------

   function Sin (X : Complex) return Complex is
   begin
      if Is_Zero (X) then
         return X;
      end if;
      return Cartesian (Elementary.Sin (X.Re) * Elementary.Cosh (X.Im),
                        Elementary.Cos (X.Re) * Elementary.Sinh (X.Im));
   end Sin;

   ---------
   -- Cos --
   ---------

   function Cos (X : Complex) return Complex is
   begin
      if Is_Zero (X) then
         return Cartesian (1.0, 0.0);
      end if;
      return Cartesian (Elementary.Cos (X.Re) * Elementary.Cosh (X.Im),
                        -(Elementary.Sin (X.Re) * Elementary.Sinh (X.Im)));
   end Cos;

   ---------
   -- Tan --
   ---------

   --  Tan (A + iB) = (Sin (2A) + i Sinh (2B)) / (Cos (2A) + Cosh (2B))
   --                = (Sin A Cos A + i Sinh B Cosh B) / (Cos**2 A + Sinh**2 B)
   --  (the second form has no cancellation in the denominator)
   function Tan (X : Complex) return Complex is
      A  : constant R := X.Re;
      B  : constant R := X.Im;
      SA, CA, SB, D : R;
   begin
      if Is_Zero (X) then
         return X;
      elsif B = 0.0 then
         return Cartesian (Elementary.Tan (A), B);
      elsif abs B > Large then
         return Cartesian
                  (2.0 * Elementary.Sin (2.0 * A)
                       * Elementary.Exp (-2.0 * abs B),
                   R'Copy_Sign (1.0, B));
      end if;
      SA := Elementary.Sin (A);
      CA := Elementary.Cos (A);
      SB := Elementary.Sinh (B);
      D := CA * CA + SB * SB;
      return Cartesian (SA * CA / D, SB * Elementary.Cosh (B) / D);
   end Tan;

   ---------
   -- Cot --
   ---------

   --  Cot (A + iB) = (Sin (2A) - i Sinh (2B)) / (Cosh (2B) - Cos (2A))
   --                = (Sin A Cos A - i Sinh B Cosh B) / (Sin**2 A + Sinh**2 B)
   function Cot (X : Complex) return Complex is
      A  : constant R := X.Re;
      B  : constant R := X.Im;
      SA, CA, SB, D : R;
   begin
      if Is_Zero (X) then
         raise Constraint_Error;
      elsif B = 0.0 then
         return Cartesian (Elementary.Cot (A), -B);
      elsif abs B > Large then
         return Cartesian
                  (2.0 * Elementary.Sin (2.0 * A)
                       * Elementary.Exp (-2.0 * abs B),
                   -R'Copy_Sign (1.0, B));
      end if;
      SA := Elementary.Sin (A);
      CA := Elementary.Cos (A);
      SB := Elementary.Sinh (B);
      D := SA * SA + SB * SB;
      return Cartesian (SA * CA / D, -(SB * Elementary.Cosh (B) / D));
   end Cot;

   ------------
   -- Arcsin --
   ------------

   function Arcsin (X : Complex) return Complex is
      S1, S2 : Complex;
   begin
      if Is_Zero (X) then
         return X;
      elsif X.Im = 0.0 and then abs X.Re <= 1.0 then
         return Cartesian (Elementary.Arcsin (X.Re), X.Im);
      end if;
      S1 := Sqrt (Cartesian (1.0 - X.Re, -X.Im));
      S2 := Sqrt (Cartesian (1.0 + X.Re, X.Im));
      return Cartesian
               (Angle (X.Re, S1.Re * S2.Re - S1.Im * S2.Im),
                Elementary.Arcsinh (S1.Re * S2.Im - S1.Im * S2.Re));
   end Arcsin;

   ------------
   -- Arccos --
   ------------

   function Arccos (X : Complex) return Complex is
      S1, S2 : Complex;
   begin
      if X.Re = 1.0 and then X.Im = 0.0 then
         return Cartesian (0.0, 0.0);
      elsif X.Im = 0.0 and then abs X.Re <= 1.0 then
         return Cartesian (Elementary.Arccos (X.Re), -X.Im);
      end if;
      S1 := Sqrt (Cartesian (1.0 - X.Re, -X.Im));
      S2 := Sqrt (Cartesian (1.0 + X.Re, X.Im));
      return Cartesian
               (2.0 * Angle (S1.Re, S2.Re),
                Elementary.Arcsinh (S2.Re * S1.Im - S2.Im * S1.Re));
   end Arccos;

   -------------
   -- Arctanh --
   -------------

   function Arctanh (X : Complex) return Complex is
      L1, L2 : Complex;
   begin
      if Is_Zero (X) then
         return X;
      elsif X.Im = 0.0 and then abs X.Re = 1.0 then
         raise Constraint_Error;
      elsif X.Im = 0.0 and then abs X.Re < 1.0 then
         return Cartesian (Elementary.Arctanh (X.Re), X.Im);
      elsif abs X.Re < Small and then abs X.Im < Small then
         return X;
      end if;
      L1 := Log (Cartesian (1.0 + X.Re, X.Im));
      L2 := Log (Cartesian (1.0 - X.Re, -X.Im));
      return Cartesian ((L1.Re - L2.Re) / 2.0, (L1.Im - L2.Im) / 2.0);
   end Arctanh;

   ------------
   -- Arctan --
   ------------

   --  Arctan (X) = -i Arctanh (iX)
   function Arctan (X : Complex) return Complex is
      W : Complex;
   begin
      if Is_Zero (X) then
         return X;
      end if;
      W := Arctanh (Cartesian (-X.Im, X.Re));
      return Cartesian (W.Im, -W.Re);
   end Arctan;

   ------------
   -- Arccot --
   ------------

   --  Arccot (X) = Pi / 2 - Arctan (X)
   function Arccot (X : Complex) return Complex is
      W : Complex;
   begin
      if Is_Zero (X) then
         return Cartesian (Half_Pi, -X.Im);
      end if;
      W := Arctan (X);
      return Cartesian (Half_Pi - W.Re, -W.Im);
   end Arccot;

   ----------
   -- Sinh --
   ----------

   function Sinh (X : Complex) return Complex is
   begin
      if Is_Zero (X) then
         return X;
      end if;
      return Cartesian (Elementary.Sinh (X.Re) * Elementary.Cos (X.Im),
                        Elementary.Cosh (X.Re) * Elementary.Sin (X.Im));
   end Sinh;

   ----------
   -- Cosh --
   ----------

   function Cosh (X : Complex) return Complex is
   begin
      if Is_Zero (X) then
         return Cartesian (1.0, 0.0);
      end if;
      return Cartesian (Elementary.Cosh (X.Re) * Elementary.Cos (X.Im),
                        Elementary.Sinh (X.Re) * Elementary.Sin (X.Im));
   end Cosh;

   ----------
   -- Tanh --
   ----------

   --  Tanh (X) = -i Tan (iX)
   function Tanh (X : Complex) return Complex is
      T : Complex;
   begin
      if Is_Zero (X) then
         return X;
      end if;
      T := Tan (Cartesian (-X.Im, X.Re));
      return Cartesian (T.Im, -T.Re);
   end Tanh;

   ----------
   -- Coth --
   ----------

   --  Coth (X) = i Cot (iX)
   function Coth (X : Complex) return Complex is
      T : Complex;
   begin
      if Is_Zero (X) then
         raise Constraint_Error;
      end if;
      T := Cot (Cartesian (-X.Im, X.Re));
      return Cartesian (-T.Im, T.Re);
   end Coth;

   -------------
   -- Arcsinh --
   -------------

   --  Arcsinh (X) = -i Arcsin (iX)
   function Arcsinh (X : Complex) return Complex is
      W : Complex;
   begin
      if Is_Zero (X) then
         return X;
      end if;
      W := Arcsin (Cartesian (-X.Im, X.Re));
      return Cartesian (W.Im, -W.Re);
   end Arcsinh;

   -------------
   -- Arccosh --
   -------------

   function Arccosh (X : Complex) return Complex is
      S1, S2 : Complex;
   begin
      if X.Re = 1.0 and then X.Im = 0.0 then
         return Cartesian (0.0, 0.0);
      elsif X.Im = 0.0 and then X.Re > 1.0 then
         return Cartesian (Elementary.Arccosh (X.Re), X.Im);
      end if;
      S1 := Sqrt (Cartesian (X.Re - 1.0, X.Im));
      S2 := Sqrt (Cartesian (X.Re + 1.0, X.Im));
      return Cartesian
               (Elementary.Arcsinh (S1.Re * S2.Re + S1.Im * S2.Im),
                2.0 * Angle (S1.Im, S2.Re));
   end Arccosh;

   -------------
   -- Arccoth --
   -------------

   --  Arccoth (X) = (Log (X + 1) - Log (X - 1)) / 2: the analytic
   --  continuation of the real function, whose branch cut is the real axis
   --  between -1.0 and 1.0 (G.1.2(19)); so its imaginary component is in
   --  -Pi / 2 .. Pi / 2, not 0.0 .. Pi as G.1.2(24) says (the two cannot both
   --  hold: this is the one that agrees with G.1.2(19-20)). At
   --  zero, it is -i Pi / 2 with the sign of the imaginary component inverted
   --  (as Arctanh (1 / X) would be).
   function Arccoth (X : Complex) return Complex is
      L1, L2 : Complex;
   begin
      if X.Im = 0.0 and then abs X.Re = 1.0 then
         raise Constraint_Error;
      elsif X.Im = 0.0 and then abs X.Re > 1.0 then
         return Cartesian (Elementary.Arccoth (X.Re), -X.Im);
      end if;
      L1 := Log (Cartesian (X.Re + 1.0, X.Im));
      L2 := Log (Cartesian (X.Re - 1.0, X.Im));
      return Cartesian ((L1.Re - L2.Re) / 2.0, (L1.Im - L2.Im) / 2.0);
   end Arccoth;

end Ada.Numerics.Generic_Complex_Elementary_Functions;
