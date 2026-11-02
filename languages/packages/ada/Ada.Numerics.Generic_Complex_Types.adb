--  Ada.Numerics.Generic_Complex_Types.adb
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

--  Imaginary's full type is derived from Real'Base, so its predefined
--  operators are overridden by the ones declared in the specification with
--  the same profiles: here, values of Imaginary are always converted to
--  Real'Base before arithmetic.

with Ada.Numerics.Generic_Elementary_Functions;

package body Ada.Numerics.Generic_Complex_Types is

   subtype R is Real'Base;

   package Elementary is new Ada.Numerics.Generic_Elementary_Functions (R);

   function V (X : Imaginary) return R is
   begin
      return R (X);
   end V;

   function Cartesian (Re, Im : R) return Complex is
   begin
      return Complex'(Re => Re, Im => Im);
   end Cartesian;

   --------
   -- Re --
   --------

   function Re (X : Complex) return Real'Base is
   begin
      return X.Re;
   end Re;

   --------
   -- Im --
   --------

   function Im (X : Complex) return Real'Base is
   begin
      return X.Im;
   end Im;

   function Im (X : Imaginary) return Real'Base is
   begin
      return V (X);
   end Im;

   ------------
   -- Set_Re --
   ------------

   procedure Set_Re (X : in out Complex; Re : in Real'Base) is
   begin
      X.Re := Re;
   end Set_Re;

   ------------
   -- Set_Im --
   ------------

   procedure Set_Im (X : in out Complex; Im : in Real'Base) is
   begin
      X.Im := Im;
   end Set_Im;

   procedure Set_Im (X : out Imaginary; Im : in Real'Base) is
   begin
      X := Imaginary (Im);
   end Set_Im;

   ----------------------------
   -- Compose_From_Cartesian --
   ----------------------------

   function Compose_From_Cartesian (Re, Im : Real'Base) return Complex is
   begin
      return Cartesian (Re, Im);
   end Compose_From_Cartesian;

   function Compose_From_Cartesian (Re : Real'Base) return Complex is
   begin
      return Cartesian (Re, 0.0);
   end Compose_From_Cartesian;

   function Compose_From_Cartesian (Im : Imaginary) return Complex is
   begin
      return Cartesian (0.0, V (Im));
   end Compose_From_Cartesian;

   -------------
   -- Modulus --
   -------------

   --  (scaled, so that it overflows only if the result does)
   function Modulus (X : Complex) return Real'Base is
      A : constant R := abs X.Re;
      B : constant R := abs X.Im;
      Q : R;
   begin
      if A = 0.0 then
         return B;
      elsif B = 0.0 then
         return A;
      elsif A >= B then
         Q := B / A;
         return A * Elementary.Sqrt (1.0 + Q * Q);
      else
         Q := A / B;
         return B * Elementary.Sqrt (1.0 + Q * Q);
      end if;
   end Modulus;

   --------------
   -- Argument --
   --------------

   function Argument (X : Complex) return Real'Base is
   begin
      if X.Im = 0.0 and then X.Re >= 0.0 then
         return X.Im;
      end if;
      return Elementary.Arctan (X.Im, X.Re);
   end Argument;

   function Argument (X : Complex; Cycle : Real'Base) return Real'Base is
   begin
      if Cycle <= 0.0 then
         raise Ada.Numerics.Argument_Error;
      elsif X.Im = 0.0 and then X.Re >= 0.0 then
         return X.Im;
      end if;
      return Elementary.Arctan (X.Im, X.Re, Cycle);
   end Argument;

   ------------------------
   -- Compose_From_Polar --
   ------------------------

   function Compose_From_Polar (Modulus, Argument : Real'Base)
     return Complex is
   begin
      if Modulus = 0.0 then
         return Cartesian (0.0, 0.0);
      elsif Argument = 0.0 then
         if Modulus > 0.0 then
            return Cartesian (Modulus, Argument);
         else
            return Cartesian (Modulus, -Argument);
         end if;
      end if;
      return Cartesian (Modulus * Elementary.Cos (Argument),
                        Modulus * Elementary.Sin (Argument));
   end Compose_From_Polar;

   function Compose_From_Polar (Modulus, Argument, Cycle : Real'Base)
     return Complex is
   begin
      if Cycle <= 0.0 then
         raise Ada.Numerics.Argument_Error;
      elsif Modulus = 0.0 then
         return Cartesian (0.0, 0.0);
      elsif Argument = 0.0 then
         if Modulus > 0.0 then
            return Cartesian (Modulus, Argument);
         else
            return Cartesian (Modulus, -Argument);
         end if;
      end if;
      return Cartesian (Modulus * Elementary.Cos (Argument, Cycle),
                        Modulus * Elementary.Sin (Argument, Cycle));
   end Compose_From_Polar;

   ---------------------------------------
   -- Operators of Complex and Complex --
   ---------------------------------------

   function "+" (Right : Complex) return Complex is
   begin
      return Right;
   end "+";

   function "-" (Right : Complex) return Complex is
   begin
      return Cartesian (-Right.Re, -Right.Im);
   end "-";

   function Conjugate (X : Complex) return Complex is
   begin
      return Cartesian (X.Re, -X.Im);
   end Conjugate;

   function "+" (Left, Right : Complex) return Complex is
   begin
      return Cartesian (Left.Re + Right.Re, Left.Im + Right.Im);
   end "+";

   function "-" (Left, Right : Complex) return Complex is
   begin
      return Cartesian (Left.Re - Right.Re, Left.Im - Right.Im);
   end "-";

   function "*" (Left, Right : Complex) return Complex is
      Large : constant R := R'Last / 4.0;
   begin
      --  (a product of large components: computed from halves (then
      --  scaled back), as a component of the result may not overflow
      --  where its intermediate products do)
      if (abs Left.Re > 2.0 or else abs Left.Im > 2.0)
        and then (abs Right.Re > 2.0 or else abs Right.Im > 2.0)
        and then R'Max (abs Left.Re, abs Left.Im) >
                 Large / R'Max (abs Right.Re, abs Right.Im)
      then
         return Cartesian
           (4.0 * ((Left.Re / 2.0) * (Right.Re / 2.0)
                   - (Left.Im / 2.0) * (Right.Im / 2.0)),
            4.0 * ((Left.Re / 2.0) * (Right.Im / 2.0)
                   + (Left.Im / 2.0) * (Right.Re / 2.0)));
      end if;
      return Cartesian (Left.Re * Right.Re - Left.Im * Right.Im,
                        Left.Re * Right.Im + Left.Im * Right.Re);
   end "*";

   --  (Smith's algorithm, which avoids overflow in the intermediate results)
   function "/" (Left, Right : Complex) return Complex is
      Ratio, Denominator : R;
   begin
      if Right.Re = 0.0 and then Right.Im = 0.0 then
         raise Constraint_Error;
      elsif Right.Im = 0.0 then
         return Cartesian (Left.Re / Right.Re, Left.Im / Right.Re);
      elsif Right.Re = 0.0 then
         return Cartesian (Left.Im / Right.Im, -(Left.Re / Right.Im));
      elsif abs Right.Re >= abs Right.Im then
         Ratio := Right.Im / Right.Re;
         Denominator := Right.Re + Right.Im * Ratio;
         return Cartesian ((Left.Re + Left.Im * Ratio) / Denominator,
                           (Left.Im - Left.Re * Ratio) / Denominator);
      else
         Ratio := Right.Re / Right.Im;
         Denominator := Right.Re * Ratio + Right.Im;
         return Cartesian ((Left.Re * Ratio + Left.Im) / Denominator,
                           (Left.Im * Ratio - Left.Re) / Denominator);
      end if;
   end "/";

   function "**" (Left : Complex; Right : Integer) return Complex is
      Result : Complex := Cartesian (1.0, 0.0);
      Factor : Complex := Left;
      N      : Long_Long_Integer := abs Long_Long_Integer (Right);
   begin
      if Right = 0 then
         return Result;
      elsif Left.Re = 0.0 and then Left.Im = 0.0 then
         if Right < 0 then
            raise Constraint_Error;
         end if;
         return Cartesian (0.0, 0.0);
      elsif Right = 1 then
         return Left;
      end if;
      loop
         if N mod 2 = 1 then
            Result := Result * Factor;
         end if;
         N := N / 2;
         exit when N = 0;
         Factor := Factor * Factor;
      end loop;
      if Right < 0 then
         return R'(1.0) / Result;
      end if;
      return Result;
   end "**";

   -------------------------------------------
   -- Operators of Imaginary and Imaginary --
   -------------------------------------------

   function "+" (Right : Imaginary) return Imaginary is
   begin
      return Right;
   end "+";

   function "-" (Right : Imaginary) return Imaginary is
   begin
      return Imaginary (-V (Right));
   end "-";

   function "abs" (Right : Imaginary) return Real'Base is
   begin
      return abs V (Right);
   end "abs";

   function "+" (Left, Right : Imaginary) return Imaginary is
   begin
      return Imaginary (V (Left) + V (Right));
   end "+";

   function "-" (Left, Right : Imaginary) return Imaginary is
   begin
      return Imaginary (V (Left) - V (Right));
   end "-";

   function "*" (Left, Right : Imaginary) return Real'Base is
   begin
      return -(V (Left) * V (Right));
   end "*";

   function "/" (Left, Right : Imaginary) return Real'Base is
   begin
      if V (Right) = 0.0 then
         raise Constraint_Error;
      end if;
      return V (Left) / V (Right);
   end "/";

   function "**" (Left : Imaginary; Right : Integer) return Complex is
      X : constant R := V (Left);
      M : R;
   begin
      if Right = 0 then
         return Cartesian (1.0, 0.0);
      elsif X = 0.0 then
         if Right < 0 then
            raise Constraint_Error;
         end if;
         return Cartesian (0.0, 0.0);
      end if;
      M := X ** Right;
      case Right mod 4 is
         when 0 => return Cartesian (M, 0.0);
         when 1 => return Cartesian (0.0, M);
         when 2 => return Cartesian (-M, 0.0);
         when others => return Cartesian (0.0, -M);
      end case;
   end "**";

   function "<" (Left, Right : Imaginary) return Boolean is
   begin
      return V (Left) < V (Right);
   end "<";

   function "<=" (Left, Right : Imaginary) return Boolean is
   begin
      return V (Left) <= V (Right);
   end "<=";

   function ">" (Left, Right : Imaginary) return Boolean is
   begin
      return V (Left) > V (Right);
   end ">";

   function ">=" (Left, Right : Imaginary) return Boolean is
   begin
      return V (Left) >= V (Right);
   end ">=";

   -----------------------------------
   -- Operators of Complex and Real --
   -----------------------------------

   function "+" (Left : Complex; Right : Real'Base) return Complex is
   begin
      return Cartesian (Left.Re + Right, Left.Im);
   end "+";

   function "+" (Left : Real'Base; Right : Complex) return Complex is
   begin
      return Cartesian (Left + Right.Re, Right.Im);
   end "+";

   function "-" (Left : Complex; Right : Real'Base) return Complex is
   begin
      return Cartesian (Left.Re - Right, Left.Im);
   end "-";

   function "-" (Left : Real'Base; Right : Complex) return Complex is
   begin
      return Cartesian (Left - Right.Re, -Right.Im);
   end "-";

   function "*" (Left : Complex; Right : Real'Base) return Complex is
   begin
      return Cartesian (Left.Re * Right, Left.Im * Right);
   end "*";

   function "*" (Left : Real'Base; Right : Complex) return Complex is
   begin
      return Cartesian (Left * Right.Re, Left * Right.Im);
   end "*";

   function "/" (Left : Complex; Right : Real'Base) return Complex is
   begin
      if Right = 0.0 then
         raise Constraint_Error;
      end if;
      return Cartesian (Left.Re / Right, Left.Im / Right);
   end "/";

   function "/" (Left : Real'Base; Right : Complex) return Complex is
   begin
      return Cartesian (Left, 0.0) / Right;
   end "/";

   ----------------------------------------
   -- Operators of Complex and Imaginary --
   ----------------------------------------

   function "+" (Left : Complex; Right : Imaginary) return Complex is
   begin
      return Cartesian (Left.Re, Left.Im + V (Right));
   end "+";

   function "+" (Left : Imaginary; Right : Complex) return Complex is
   begin
      return Cartesian (Right.Re, V (Left) + Right.Im);
   end "+";

   function "-" (Left : Complex; Right : Imaginary) return Complex is
   begin
      return Cartesian (Left.Re, Left.Im - V (Right));
   end "-";

   function "-" (Left : Imaginary; Right : Complex) return Complex is
   begin
      return Cartesian (-Right.Re, V (Left) - Right.Im);
   end "-";

   function "*" (Left : Complex; Right : Imaginary) return Complex is
   begin
      return Cartesian (-(Left.Im * V (Right)), Left.Re * V (Right));
   end "*";

   function "*" (Left : Imaginary; Right : Complex) return Complex is
   begin
      return Cartesian (-(V (Left) * Right.Im), V (Left) * Right.Re);
   end "*";

   function "/" (Left : Complex; Right : Imaginary) return Complex is
      Y : constant R := V (Right);
   begin
      if Y = 0.0 then
         raise Constraint_Error;
      end if;
      return Cartesian (Left.Im / Y, -(Left.Re / Y));
   end "/";

   function "/" (Left : Imaginary; Right : Complex) return Complex is
   begin
      return Cartesian (0.0, V (Left)) / Right;
   end "/";

   -------------------------------------
   -- Operators of Imaginary and Real --
   -------------------------------------

   function "+" (Left : Imaginary; Right : Real'Base) return Complex is
   begin
      return Cartesian (Right, V (Left));
   end "+";

   function "+" (Left : Real'Base; Right : Imaginary) return Complex is
   begin
      return Cartesian (Left, V (Right));
   end "+";

   function "-" (Left : Imaginary; Right : Real'Base) return Complex is
   begin
      return Cartesian (-Right, V (Left));
   end "-";

   function "-" (Left : Real'Base; Right : Imaginary) return Complex is
   begin
      return Cartesian (Left, -V (Right));
   end "-";

   function "*" (Left : Imaginary; Right : Real'Base) return Imaginary is
   begin
      return Imaginary (V (Left) * Right);
   end "*";

   function "*" (Left : Real'Base; Right : Imaginary) return Imaginary is
   begin
      return Imaginary (Left * V (Right));
   end "*";

   function "/" (Left : Imaginary; Right : Real'Base) return Imaginary is
   begin
      if Right = 0.0 then
         raise Constraint_Error;
      end if;
      return Imaginary (V (Left) / Right);
   end "/";

   function "/" (Left : Real'Base; Right : Imaginary) return Imaginary is
      Y : constant R := V (Right);
   begin
      if Y = 0.0 then
         raise Constraint_Error;
      end if;
      return Imaginary (-(Left / Y));
   end "/";

end Ada.Numerics.Generic_Complex_Types;
