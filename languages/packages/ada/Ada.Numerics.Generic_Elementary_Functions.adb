--  Ada.Numerics.Generic_Elementary_Functions.adb
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

--  The elementary functions are computed in Long_Float by the C library's
--  (native primitives); this body checks the Reference Manual's domain and
--  pole rules (A.5.1(19-35)) and gives its prescribed results (A.5.1(37-47))
--  itself. The functions with a Cycle parameter reduce X exactly (with
--  'Remainder) so that their results at the multiples of the quarter cycle
--  are exact.

package body Ada.Numerics.Generic_Elementary_Functions is

   subtype T is Float_Type'Base;

   function C_Sqrt (X : Long_Float) return Long_Float;
   pragma Import (Neos, C_Sqrt, "ada::sqrt");
   function C_Exp (X : Long_Float) return Long_Float;
   pragma Import (Neos, C_Exp, "ada::exp");
   function C_Log (X : Long_Float) return Long_Float;
   pragma Import (Neos, C_Log, "ada::log");
   function C_Pow (X, Y : Long_Float) return Long_Float;
   pragma Import (Neos, C_Pow, "ada::pow");
   function C_Sin (X : Long_Float) return Long_Float;
   pragma Import (Neos, C_Sin, "ada::sin");
   function C_Cos (X : Long_Float) return Long_Float;
   pragma Import (Neos, C_Cos, "ada::cos");
   function C_Tan (X : Long_Float) return Long_Float;
   pragma Import (Neos, C_Tan, "ada::tan");
   function C_Asin (X : Long_Float) return Long_Float;
   pragma Import (Neos, C_Asin, "ada::asin");
   function C_Acos (X : Long_Float) return Long_Float;
   pragma Import (Neos, C_Acos, "ada::acos");
   function C_Atan2 (Y, X : Long_Float) return Long_Float;
   pragma Import (Neos, C_Atan2, "ada::atan2");
   function C_Sinh (X : Long_Float) return Long_Float;
   pragma Import (Neos, C_Sinh, "ada::sinh");
   function C_Cosh (X : Long_Float) return Long_Float;
   pragma Import (Neos, C_Cosh, "ada::cosh");
   function C_Tanh (X : Long_Float) return Long_Float;
   pragma Import (Neos, C_Tanh, "ada::tanh");
   function C_Asinh (X : Long_Float) return Long_Float;
   pragma Import (Neos, C_Asinh, "ada::asinh");
   function C_Acosh (X : Long_Float) return Long_Float;
   pragma Import (Neos, C_Acosh, "ada::acosh");
   function C_Atanh (X : Long_Float) return Long_Float;
   pragma Import (Neos, C_Atanh, "ada::atanh");

   Two_Pi : constant := 2.0 * Ada.Numerics.Pi;
   Half_Pi : constant := Ada.Numerics.Pi / 2.0;

   function L (X : T) return Long_Float is
   begin
      return Long_Float (X);
   end L;

   --  The result: Constraint_Error if it overflowed (or is not a number)
   function R (X : Long_Float) return T is
   begin
      if not (X >= -Long_Float'Last and then X <= Long_Float'Last) then
         raise Constraint_Error;
      end if;
      if T'Digits <= Long_Float'Digits
        and then abs X > Long_Float (T'Last)
      then
         raise Constraint_Error;
      end if;
      return T (X);
   end R;

   procedure Check_Cycle (Cycle : T) is
   begin
      if Cycle <= 0.0 then
         raise Ada.Numerics.Argument_Error;
      end if;
   end Check_Cycle;

   --  X reduced exactly to -Cycle / 2 .. Cycle / 2
   function Reduce (X, Cycle : T) return T is
   begin
      return T'Remainder (X, Cycle);
   end Reduce;

   --  The angle in radians of an exactly reduced X
   function Radians (X, Cycle : T) return Long_Float is
   begin
      return Long_Float (X) / Long_Float (Cycle) * Two_Pi;
   end Radians;

   --  An angle in radians as a fraction of Cycle
   function In_Cycle (Angle : Long_Float; Cycle : T) return T is
   begin
      return R (Angle / Two_Pi * Long_Float (Cycle));
   end In_Cycle;

   ----------
   -- Sqrt --
   ----------

   function Sqrt (X : Float_Type'Base) return Float_Type'Base is
   begin
      if X < 0.0 then
         raise Ada.Numerics.Argument_Error;
      elsif X = 0.0 or else X = 1.0 then
         return X;
      end if;
      return R (C_Sqrt (L (X)));
   end Sqrt;

   ---------
   -- Log --
   ---------

   function Log (X : Float_Type'Base) return Float_Type'Base is
   begin
      if X < 0.0 then
         raise Ada.Numerics.Argument_Error;
      elsif X = 0.0 then
         raise Constraint_Error;
      elsif X = 1.0 then
         return 0.0;
      end if;
      return R (C_Log (L (X)));
   end Log;

   function Log (X, Base : Float_Type'Base) return Float_Type'Base is
   begin
      if X < 0.0 or else Base <= 0.0 or else Base = 1.0 then
         raise Ada.Numerics.Argument_Error;
      elsif X = 0.0 then
         raise Constraint_Error;
      elsif X = 1.0 then
         return 0.0;
      end if;
      return R (C_Log (L (X)) / C_Log (L (Base)));
   end Log;

   ---------
   -- Exp --
   ---------

   function Exp (X : Float_Type'Base) return Float_Type'Base is
   begin
      if X = 0.0 then
         return 1.0;
      end if;
      return R (C_Exp (L (X)));
   end Exp;

   ----------
   -- "**" --
   ----------

   function "**" (Left, Right : Float_Type'Base) return Float_Type'Base is
   begin
      if Left < 0.0 or else (Left = 0.0 and then Right = 0.0) then
         raise Ada.Numerics.Argument_Error;
      elsif Left = 0.0 and then Right < 0.0 then
         raise Constraint_Error;
      elsif Right = 0.0 or else Left = 1.0 then
         return 1.0;
      elsif Right = 1.0 then
         return Left;
      elsif Left = 0.0 then
         return 0.0;
      elsif Right = 0.5 then
         return R (C_Sqrt (L (Left)));
      end if;
      return R (C_Pow (L (Left), L (Right)));
   end "**";

   ---------
   -- Sin --
   ---------

   function Sin (X : Float_Type'Base) return Float_Type'Base is
   begin
      if X = 0.0 then
         return X;
      end if;
      return R (C_Sin (L (X)));
   end Sin;

   function Sin (X, Cycle : Float_Type'Base) return Float_Type'Base is
      Y : T;
   begin
      Check_Cycle (Cycle);
      if X = 0.0 then
         return X;
      end if;
      Y := Reduce (X, Cycle);
      if Y = 0.0 or else abs Y = Cycle / 2.0 then
         return 0.0;
      elsif Y = Cycle / 4.0 then
         return 1.0;
      elsif Y = -Cycle / 4.0 then
         return -1.0;
      end if;
      return R (C_Sin (Radians (Y, Cycle)));
   end Sin;

   ---------
   -- Cos --
   ---------

   function Cos (X : Float_Type'Base) return Float_Type'Base is
   begin
      if X = 0.0 then
         return 1.0;
      end if;
      return R (C_Cos (L (X)));
   end Cos;

   function Cos (X, Cycle : Float_Type'Base) return Float_Type'Base is
      Y : T;
   begin
      Check_Cycle (Cycle);
      Y := abs Reduce (X, Cycle);
      if Y = 0.0 then
         return 1.0;
      elsif Y = Cycle / 2.0 then
         return -1.0;
      elsif Y = Cycle / 4.0 then
         return 0.0;
      end if;
      return R (C_Cos (Radians (Y, Cycle)));
   end Cos;

   ---------
   -- Tan --
   ---------

   function Tan (X : Float_Type'Base) return Float_Type'Base is
   begin
      if X = 0.0 then
         return X;
      end if;
      return R (C_Tan (L (X)));
   end Tan;

   function Tan (X, Cycle : Float_Type'Base) return Float_Type'Base is
      Y : T;
   begin
      Check_Cycle (Cycle);
      if X = 0.0 then
         return X;
      end if;
      Y := Reduce (X, Cycle);
      if Y = 0.0 or else abs Y = Cycle / 2.0 then
         return 0.0;
      elsif abs Y = Cycle / 4.0 then
         raise Constraint_Error;
      end if;
      return R (C_Tan (Radians (Y, Cycle)));
   end Tan;

   ---------
   -- Cot --
   ---------

   function Cot (X : Float_Type'Base) return Float_Type'Base is
      A : Long_Float;
   begin
      if X = 0.0 then
         raise Constraint_Error;
      end if;
      A := L (X);
      return R (C_Cos (A) / C_Sin (A));
   end Cot;

   function Cot (X, Cycle : Float_Type'Base) return Float_Type'Base is
      Y : T;
      A : Long_Float;
   begin
      Check_Cycle (Cycle);
      Y := Reduce (X, Cycle);
      if Y = 0.0 or else abs Y = Cycle / 2.0 then
         raise Constraint_Error;
      elsif abs Y = Cycle / 4.0 then
         return 0.0;
      end if;
      A := Radians (Y, Cycle);
      return R (C_Cos (A) / C_Sin (A));
   end Cot;

   ------------
   -- Arcsin --
   ------------

   function Arcsin (X : Float_Type'Base) return Float_Type'Base is
   begin
      if abs X > 1.0 then
         raise Ada.Numerics.Argument_Error;
      elsif X = 0.0 then
         return X;
      elsif X = 1.0 then
         return R (Half_Pi);
      elsif X = -1.0 then
         return R (-Half_Pi);
      end if;
      return R (C_Asin (L (X)));
   end Arcsin;

   function Arcsin (X, Cycle : Float_Type'Base) return Float_Type'Base is
   begin
      Check_Cycle (Cycle);
      if abs X > 1.0 then
         raise Ada.Numerics.Argument_Error;
      elsif X = 0.0 then
         return X;
      elsif X = 1.0 then
         return Cycle / 4.0;
      elsif X = -1.0 then
         return -Cycle / 4.0;
      end if;
      return In_Cycle (C_Asin (L (X)), Cycle);
   end Arcsin;

   ------------
   -- Arccos --
   ------------

   function Arccos (X : Float_Type'Base) return Float_Type'Base is
   begin
      if abs X > 1.0 then
         raise Ada.Numerics.Argument_Error;
      elsif X = 1.0 then
         return 0.0;
      elsif X = 0.0 then
         return R (Half_Pi);
      elsif X = -1.0 then
         return R (Ada.Numerics.Pi);
      end if;
      return R (C_Acos (L (X)));
   end Arccos;

   function Arccos (X, Cycle : Float_Type'Base) return Float_Type'Base is
   begin
      Check_Cycle (Cycle);
      if abs X > 1.0 then
         raise Ada.Numerics.Argument_Error;
      elsif X = 1.0 then
         return 0.0;
      elsif X = 0.0 then
         return Cycle / 4.0;
      elsif X = -1.0 then
         return Cycle / 2.0;
      end if;
      return In_Cycle (C_Acos (L (X)), Cycle);
   end Arccos;

   ------------
   -- Arctan --
   ------------

   function Arctan
     (Y : Float_Type'Base;
      X : Float_Type'Base := 1.0) return Float_Type'Base is
   begin
      if X = 0.0 and then Y = 0.0 then
         raise Ada.Numerics.Argument_Error;
      elsif Y = 0.0 and then X > 0.0 then
         return Y;
      end if;
      return R (C_Atan2 (L (Y), L (X)));
   end Arctan;

   function Arctan
     (Y     : Float_Type'Base;
      X     : Float_Type'Base := 1.0;
      Cycle : Float_Type'Base) return Float_Type'Base is
   begin
      Check_Cycle (Cycle);
      if X = 0.0 and then Y = 0.0 then
         raise Ada.Numerics.Argument_Error;
      elsif Y = 0.0 then
         if X > 0.0 then
            return Y;
         else
            return T'Copy_Sign (Cycle / 2.0, Y);
         end if;
      elsif X = 0.0 then
         return T'Copy_Sign (Cycle / 4.0, Y);
      elsif abs Y = abs X then
         if X > 0.0 then
            return T'Copy_Sign (Cycle / 8.0, Y);
         else
            return T'Copy_Sign (3.0 * Cycle / 8.0, Y);
         end if;
      end if;
      return In_Cycle (C_Atan2 (L (Y), L (X)), Cycle);
   end Arctan;

   ------------
   -- Arccot --
   ------------

   function Arccot
     (X : Float_Type'Base;
      Y : Float_Type'Base := 1.0) return Float_Type'Base is
   begin
      return Arctan (Y => Y, X => X);
   end Arccot;

   function Arccot
     (X     : Float_Type'Base;
      Y     : Float_Type'Base := 1.0;
      Cycle : Float_Type'Base) return Float_Type'Base is
   begin
      return Arctan (Y => Y, X => X, Cycle => Cycle);
   end Arccot;

   ----------
   -- Sinh --
   ----------

   function Sinh (X : Float_Type'Base) return Float_Type'Base is
   begin
      if X = 0.0 then
         return X;
      end if;
      return R (C_Sinh (L (X)));
   end Sinh;

   ----------
   -- Cosh --
   ----------

   function Cosh (X : Float_Type'Base) return Float_Type'Base is
   begin
      if X = 0.0 then
         return 1.0;
      end if;
      return R (C_Cosh (L (X)));
   end Cosh;

   ----------
   -- Tanh --
   ----------

   function Tanh (X : Float_Type'Base) return Float_Type'Base is
   begin
      if X = 0.0 then
         return X;
      end if;
      return R (C_Tanh (L (X)));
   end Tanh;

   ----------
   -- Coth --
   ----------

   function Coth (X : Float_Type'Base) return Float_Type'Base is
   begin
      if X = 0.0 then
         raise Constraint_Error;
      end if;
      return R (1.0 / C_Tanh (L (X)));
   end Coth;

   -------------
   -- Arcsinh --
   -------------

   function Arcsinh (X : Float_Type'Base) return Float_Type'Base is
   begin
      if X = 0.0 then
         return X;
      end if;
      return R (C_Asinh (L (X)));
   end Arcsinh;

   -------------
   -- Arccosh --
   -------------

   function Arccosh (X : Float_Type'Base) return Float_Type'Base is
   begin
      if X < 1.0 then
         raise Ada.Numerics.Argument_Error;
      elsif X = 1.0 then
         return 0.0;
      end if;
      return R (C_Acosh (L (X)));
   end Arccosh;

   -------------
   -- Arctanh --
   -------------

   function Arctanh (X : Float_Type'Base) return Float_Type'Base is
   begin
      if abs X > 1.0 then
         raise Ada.Numerics.Argument_Error;
      elsif abs X = 1.0 then
         raise Constraint_Error;
      elsif X = 0.0 then
         return X;
      end if;
      return R (C_Atanh (L (X)));
   end Arctanh;

   -------------
   -- Arccoth --
   -------------

   function Arccoth (X : Float_Type'Base) return Float_Type'Base is
   begin
      if abs X < 1.0 then
         raise Ada.Numerics.Argument_Error;
      elsif abs X = 1.0 then
         raise Constraint_Error;
      end if;
      return R (C_Atanh (1.0 / L (X)));
   end Arccoth;

end Ada.Numerics.Generic_Elementary_Functions;
