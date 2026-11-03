--  Ada.Numerics.Generic_Complex_Arrays.adb
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

--  Solve, Inverse and Determinant use LU decomposition with partial pivoting
--  (row interchanges), followed by forward and back substitution (no
--  iteration on the residuals is performed). A matrix that is singular (a
--  zero pivot) raises Constraint_Error (Solve and Inverse) or has the
--  determinant zero. Eigenvalues and Eigensystem use the cyclic Jacobi
--  method (for Hermitian matrices); Constraint_Error is raised if it does
--  not converge within 50 sweeps. Inner products are accumulated in Complex
--  (no extended precision is used); the inner product of two complex vectors
--  is that of their components (it does not conjugate either).

with Ada.Numerics.Generic_Elementary_Functions;

package body Ada.Numerics.Generic_Complex_Arrays is

   package Elementary is new Ada.Numerics.Generic_Elementary_Functions (Real'Base);

   Max_Sweeps : constant := 50;

   -- Complex_Vector selection, conversion and composition operations

   function Re (X : Complex_Vector) return Real_Vector is
   begin
      return Result : Real_Vector (X'Range) do
         for I in X'Range loop
            Result (I) := X (I).Re;
         end loop;
      end return;
   end Re;

   function Im (X : Complex_Vector) return Real_Vector is
   begin
      return Result : Real_Vector (X'Range) do
         for I in X'Range loop
            Result (I) := X (I).Im;
         end loop;
      end return;
   end Im;

   procedure Set_Re (X  : in out Complex_Vector;
                     Re : in     Real_Vector) is
   begin
      if X'Length /= Re'Length then
         raise Constraint_Error with "vectors of different lengths";
      end if;
      for I in X'Range loop
         X (I).Re := Re (I - X'First + Re'First);
      end loop;
   end Set_Re;

   procedure Set_Im (X  : in out Complex_Vector;
                     Im : in     Real_Vector) is
   begin
      if X'Length /= Im'Length then
         raise Constraint_Error with "vectors of different lengths";
      end if;
      for I in X'Range loop
         X (I).Im := Im (I - X'First + Im'First);
      end loop;
   end Set_Im;

   function Compose_From_Cartesian (Re : Real_Vector) return Complex_Vector is
   begin
      return Result : Complex_Vector (Re'Range) do
         for I in Re'Range loop
            Result (I) := (Re (I), 0.0);
         end loop;
      end return;
   end Compose_From_Cartesian;

   function Compose_From_Cartesian (Re, Im : Real_Vector) return Complex_Vector is
   begin
      if Re'Length /= Im'Length then
         raise Constraint_Error with "vectors of different lengths";
      end if;
      return Result : Complex_Vector (Re'Range) do
         for I in Re'Range loop
            declare
               L : Real'Base renames Re (I);
               R : Real'Base renames Im (I - Re'First + Im'First);
            begin
               Result (I) := (L, R);
            end;
         end loop;
      end return;
   end Compose_From_Cartesian;

   function Modulus (X : Complex_Vector) return Real_Vector is
   begin
      return Result : Real_Vector (X'Range) do
         for I in X'Range loop
            Result (I) := Complex_Types.Modulus (X (I));
         end loop;
      end return;
   end Modulus;

   function Argument (X : Complex_Vector) return Real_Vector is
   begin
      return Result : Real_Vector (X'Range) do
         for I in X'Range loop
            Result (I) := Complex_Types.Argument (X (I));
         end loop;
      end return;
   end Argument;

   function Argument (X : Complex_Vector;
                      Cycle : Real'Base) return Real_Vector is
   begin
      return Result : Real_Vector (X'Range) do
         for I in X'Range loop
            Result (I) := Complex_Types.Argument (X (I), Cycle);
         end loop;
      end return;
   end Argument;

   function Compose_From_Polar (Modulus, Argument : Real_Vector) return Complex_Vector is
   begin
      if Modulus'Length /= Argument'Length then
         raise Constraint_Error with "vectors of different lengths";
      end if;
      return Result : Complex_Vector (Modulus'Range) do
         for I in Modulus'Range loop
            declare
               L : Real'Base renames Modulus (I);
               R : Real'Base renames Argument (I - Modulus'First + Argument'First);
            begin
               Result (I) := Complex_Types.Compose_From_Polar (L, R);
            end;
         end loop;
      end return;
   end Compose_From_Polar;

   function Compose_From_Polar (Modulus, Argument : Real_Vector;
                                Cycle             : Real'Base) return Complex_Vector is
   begin
      if Modulus'Length /= Argument'Length then
         raise Constraint_Error with "vectors of different lengths";
      end if;
      return Result : Complex_Vector (Modulus'Range) do
         for I in Modulus'Range loop
            declare
               L : Real'Base renames Modulus (I);
               R : Real'Base renames Argument (I - Modulus'First + Argument'First);
            begin
               Result (I) := Complex_Types.Compose_From_Polar (L, R, Cycle);
            end;
         end loop;
      end return;
   end Compose_From_Polar;

   -- Complex_Vector arithmetic operations

   function "+" (Right : Complex_Vector) return Complex_Vector is
   begin
      return Result : Complex_Vector (Right'Range) do
         for I in Right'Range loop
            Result (I) := +Right (I);
         end loop;
      end return;
   end "+";

   function "-" (Right : Complex_Vector) return Complex_Vector is
   begin
      return Result : Complex_Vector (Right'Range) do
         for I in Right'Range loop
            Result (I) := -Right (I);
         end loop;
      end return;
   end "-";

   function Conjugate (X : Complex_Vector) return Complex_Vector is
   begin
      return Result : Complex_Vector (X'Range) do
         for I in X'Range loop
            Result (I) := Complex_Types.Conjugate (X (I));
         end loop;
      end return;
   end Conjugate;

   function "+" (Left, Right : Complex_Vector) return Complex_Vector is
   begin
      if Left'Length /= Right'Length then
         raise Constraint_Error with "vectors of different lengths";
      end if;
      return Result : Complex_Vector (Left'Range) do
         for I in Left'Range loop
            declare
               L : Complex renames Left (I);
               R : Complex renames Right (I - Left'First + Right'First);
            begin
               Result (I) := L + R;
            end;
         end loop;
      end return;
   end "+";

   function "-" (Left, Right : Complex_Vector) return Complex_Vector is
   begin
      if Left'Length /= Right'Length then
         raise Constraint_Error with "vectors of different lengths";
      end if;
      return Result : Complex_Vector (Left'Range) do
         for I in Left'Range loop
            declare
               L : Complex renames Left (I);
               R : Complex renames Right (I - Left'First + Right'First);
            begin
               Result (I) := L - R;
            end;
         end loop;
      end return;
   end "-";

   function "*" (Left, Right : Complex_Vector) return Complex is
      Sum : Complex := (0.0, 0.0);
   begin
      if Left'Length /= Right'Length then
         raise Constraint_Error with "vectors of different lengths";
      end if;
      for I in Left'Range loop
         Sum := Sum + Left (I) * Right (I - Left'First + Right'First);
      end loop;
      return Sum;
   end "*";

   function "abs" (Right : Complex_Vector) return Real'Base is
      Sum : Real'Base := 0.0;
   begin
      for I in Right'Range loop
         Sum := Sum + Right (I).Re * Right (I).Re + Right (I).Im * Right (I).Im;
      end loop;
      return Elementary.Sqrt (Sum);
   end "abs";

   -- Mixed Real_Vector and Complex_Vector arithmetic operations

   function "+" (Left : Real_Vector; Right : Complex_Vector) return Complex_Vector is
   begin
      if Left'Length /= Right'Length then
         raise Constraint_Error with "vectors of different lengths";
      end if;
      return Result : Complex_Vector (Left'Range) do
         for I in Left'Range loop
            declare
               L : Real'Base renames Left (I);
               R : Complex renames Right (I - Left'First + Right'First);
            begin
               Result (I) := L + R;
            end;
         end loop;
      end return;
   end "+";

   function "+" (Left : Complex_Vector; Right : Real_Vector) return Complex_Vector is
   begin
      if Left'Length /= Right'Length then
         raise Constraint_Error with "vectors of different lengths";
      end if;
      return Result : Complex_Vector (Left'Range) do
         for I in Left'Range loop
            declare
               L : Complex renames Left (I);
               R : Real'Base renames Right (I - Left'First + Right'First);
            begin
               Result (I) := L + R;
            end;
         end loop;
      end return;
   end "+";

   function "-" (Left : Real_Vector; Right : Complex_Vector) return Complex_Vector is
   begin
      if Left'Length /= Right'Length then
         raise Constraint_Error with "vectors of different lengths";
      end if;
      return Result : Complex_Vector (Left'Range) do
         for I in Left'Range loop
            declare
               L : Real'Base renames Left (I);
               R : Complex renames Right (I - Left'First + Right'First);
            begin
               Result (I) := L - R;
            end;
         end loop;
      end return;
   end "-";

   function "-" (Left : Complex_Vector; Right : Real_Vector) return Complex_Vector is
   begin
      if Left'Length /= Right'Length then
         raise Constraint_Error with "vectors of different lengths";
      end if;
      return Result : Complex_Vector (Left'Range) do
         for I in Left'Range loop
            declare
               L : Complex renames Left (I);
               R : Real'Base renames Right (I - Left'First + Right'First);
            begin
               Result (I) := L - R;
            end;
         end loop;
      end return;
   end "-";

   function "*" (Left  : Real_Vector;    Right : Complex_Vector) return Complex is
      Sum : Complex := (0.0, 0.0);
   begin
      if Left'Length /= Right'Length then
         raise Constraint_Error with "vectors of different lengths";
      end if;
      for I in Left'Range loop
         Sum := Sum + Left (I) * Right (I - Left'First + Right'First);
      end loop;
      return Sum;
   end "*";

   function "*" (Left  : Complex_Vector; Right : Real_Vector) return Complex is
      Sum : Complex := (0.0, 0.0);
   begin
      if Left'Length /= Right'Length then
         raise Constraint_Error with "vectors of different lengths";
      end if;
      for I in Left'Range loop
         Sum := Sum + Left (I) * Right (I - Left'First + Right'First);
      end loop;
      return Sum;
   end "*";

   -- Complex_Vector scaling operations

   function "*" (Left  : Complex;
                 Right : Complex_Vector) return Complex_Vector is
   begin
      return Result : Complex_Vector (Right'Range) do
         for I in Right'Range loop
            Result (I) := Left * Right (I);
         end loop;
      end return;
   end "*";

   function "*" (Left  : Complex_Vector;
                 Right : Complex) return Complex_Vector is
   begin
      return Result : Complex_Vector (Left'Range) do
         for I in Left'Range loop
            Result (I) := Left (I) * Right;
         end loop;
      end return;
   end "*";

   function "/" (Left  : Complex_Vector;
                 Right : Complex) return Complex_Vector is
   begin
      return Result : Complex_Vector (Left'Range) do
         for I in Left'Range loop
            Result (I) := Left (I) / Right;
         end loop;
      end return;
   end "/";

   function "*" (Left  : Real'Base;
                 Right : Complex_Vector) return Complex_Vector is
   begin
      return Result : Complex_Vector (Right'Range) do
         for I in Right'Range loop
            Result (I) := Left * Right (I);
         end loop;
      end return;
   end "*";

   function "*" (Left  : Complex_Vector;
                 Right : Real'Base) return Complex_Vector is
   begin
      return Result : Complex_Vector (Left'Range) do
         for I in Left'Range loop
            Result (I) := Left (I) * Right;
         end loop;
      end return;
   end "*";

   function "/" (Left  : Complex_Vector;
                 Right : Real'Base) return Complex_Vector is
   begin
      return Result : Complex_Vector (Left'Range) do
         for I in Left'Range loop
            Result (I) := Left (I) / Right;
         end loop;
      end return;
   end "/";

   -- Other Complex_Vector operations

   function Unit_Vector (Index : Integer;
                         Order : Positive;
                         First : Integer := 1) return Complex_Vector is
   begin
      if Index < First or else First > Integer'Last - Order + 1 or else Index > First + Order - 1 then
         raise Constraint_Error with "index not in the vector's range";
      end if;
      return Result : Complex_Vector (First .. First + Order - 1) := (others => (0.0, 0.0)) do
         Result (Index) := (1.0, 0.0);
      end return;
   end Unit_Vector;

   -- Complex_Matrix selection, conversion and composition operations

   function Re (X : Complex_Matrix) return Real_Matrix is
   begin
      return Result : Real_Matrix (X'Range (1), X'Range (2)) do
         for I in X'Range (1) loop
            for J in X'Range (2) loop
               Result (I, J) := X (I, J).Re;
            end loop;
         end loop;
      end return;
   end Re;

   function Im (X : Complex_Matrix) return Real_Matrix is
   begin
      return Result : Real_Matrix (X'Range (1), X'Range (2)) do
         for I in X'Range (1) loop
            for J in X'Range (2) loop
               Result (I, J) := X (I, J).Im;
            end loop;
         end loop;
      end return;
   end Im;

   procedure Set_Re (X  : in out Complex_Matrix;
                     Re : in     Real_Matrix) is
   begin
      if X'Length (1) /= Re'Length (1) or else X'Length (2) /= Re'Length (2) then
         raise Constraint_Error with "matrices of different shapes";
      end if;
      for I in X'Range (1) loop
         for J in X'Range (2) loop
            X (I, J).Re := Re (I - X'First (1) + Re'First (1), J - X'First (2) + Re'First (2));
         end loop;
      end loop;
   end Set_Re;

   procedure Set_Im (X  : in out Complex_Matrix;
                     Im : in     Real_Matrix) is
   begin
      if X'Length (1) /= Im'Length (1) or else X'Length (2) /= Im'Length (2) then
         raise Constraint_Error with "matrices of different shapes";
      end if;
      for I in X'Range (1) loop
         for J in X'Range (2) loop
            X (I, J).Im := Im (I - X'First (1) + Im'First (1), J - X'First (2) + Im'First (2));
         end loop;
      end loop;
   end Set_Im;

   function Compose_From_Cartesian (Re : Real_Matrix) return Complex_Matrix is
   begin
      return Result : Complex_Matrix (Re'Range (1), Re'Range (2)) do
         for I in Re'Range (1) loop
            for J in Re'Range (2) loop
               Result (I, J) := (Re (I, J), 0.0);
            end loop;
         end loop;
      end return;
   end Compose_From_Cartesian;

   function Compose_From_Cartesian (Re, Im : Real_Matrix) return Complex_Matrix is
   begin
      if Re'Length (1) /= Im'Length (1) or else Re'Length (2) /= Im'Length (2) then
         raise Constraint_Error with "matrices of different shapes";
      end if;
      return Result : Complex_Matrix (Re'Range (1), Re'Range (2)) do
         for I in Re'Range (1) loop
            for J in Re'Range (2) loop
               declare
                  L : Real'Base renames Re (I, J);
                  R : Real'Base renames Im (I - Re'First (1) + Im'First (1), J - Re'First (2) + Im'First (2));
               begin
                  Result (I, J) := (L, R);
               end;
            end loop;
         end loop;
      end return;
   end Compose_From_Cartesian;

   function Modulus (X : Complex_Matrix) return Real_Matrix is
   begin
      return Result : Real_Matrix (X'Range (1), X'Range (2)) do
         for I in X'Range (1) loop
            for J in X'Range (2) loop
               Result (I, J) := Complex_Types.Modulus (X (I, J));
            end loop;
         end loop;
      end return;
   end Modulus;

   function Argument (X : Complex_Matrix) return Real_Matrix is
   begin
      return Result : Real_Matrix (X'Range (1), X'Range (2)) do
         for I in X'Range (1) loop
            for J in X'Range (2) loop
               Result (I, J) := Complex_Types.Argument (X (I, J));
            end loop;
         end loop;
      end return;
   end Argument;

   function Argument (X : Complex_Matrix;
                      Cycle : Real'Base) return Real_Matrix is
   begin
      return Result : Real_Matrix (X'Range (1), X'Range (2)) do
         for I in X'Range (1) loop
            for J in X'Range (2) loop
               Result (I, J) := Complex_Types.Argument (X (I, J), Cycle);
            end loop;
         end loop;
      end return;
   end Argument;

   function Compose_From_Polar (Modulus, Argument : Real_Matrix) return Complex_Matrix is
   begin
      if Modulus'Length (1) /= Argument'Length (1) or else Modulus'Length (2) /= Argument'Length (2) then
         raise Constraint_Error with "matrices of different shapes";
      end if;
      return Result : Complex_Matrix (Modulus'Range (1), Modulus'Range (2)) do
         for I in Modulus'Range (1) loop
            for J in Modulus'Range (2) loop
               declare
                  L : Real'Base renames Modulus (I, J);
                  R : Real'Base renames Argument (I - Modulus'First (1) + Argument'First (1), J - Modulus'First (2) + Argument'First (2));
               begin
                  Result (I, J) := Complex_Types.Compose_From_Polar (L, R);
               end;
            end loop;
         end loop;
      end return;
   end Compose_From_Polar;

   function Compose_From_Polar (Modulus, Argument : Real_Matrix;
                                Cycle             : Real'Base) return Complex_Matrix is
   begin
      if Modulus'Length (1) /= Argument'Length (1) or else Modulus'Length (2) /= Argument'Length (2) then
         raise Constraint_Error with "matrices of different shapes";
      end if;
      return Result : Complex_Matrix (Modulus'Range (1), Modulus'Range (2)) do
         for I in Modulus'Range (1) loop
            for J in Modulus'Range (2) loop
               declare
                  L : Real'Base renames Modulus (I, J);
                  R : Real'Base renames Argument (I - Modulus'First (1) + Argument'First (1), J - Modulus'First (2) + Argument'First (2));
               begin
                  Result (I, J) := Complex_Types.Compose_From_Polar (L, R, Cycle);
               end;
            end loop;
         end loop;
      end return;
   end Compose_From_Polar;

   -- Complex_Matrix arithmetic operations

   function "+" (Right : Complex_Matrix) return Complex_Matrix is
   begin
      return Result : Complex_Matrix (Right'Range (1), Right'Range (2)) do
         for I in Right'Range (1) loop
            for J in Right'Range (2) loop
               Result (I, J) := +Right (I, J);
            end loop;
         end loop;
      end return;
   end "+";

   function "-" (Right : Complex_Matrix) return Complex_Matrix is
   begin
      return Result : Complex_Matrix (Right'Range (1), Right'Range (2)) do
         for I in Right'Range (1) loop
            for J in Right'Range (2) loop
               Result (I, J) := -Right (I, J);
            end loop;
         end loop;
      end return;
   end "-";

   function Conjugate (X : Complex_Matrix) return Complex_Matrix is
   begin
      return Result : Complex_Matrix (X'Range (1), X'Range (2)) do
         for I in X'Range (1) loop
            for J in X'Range (2) loop
               Result (I, J) := Complex_Types.Conjugate (X (I, J));
            end loop;
         end loop;
      end return;
   end Conjugate;

   function Transpose (X : Complex_Matrix) return Complex_Matrix is
   begin
      return Result : Complex_Matrix (X'Range (2), X'Range (1)) do
         for I in X'Range (1) loop
            for J in X'Range (2) loop
               Result (J, I) := X (I, J);
            end loop;
         end loop;
      end return;
   end Transpose;

   function "+" (Left, Right : Complex_Matrix) return Complex_Matrix is
   begin
      if Left'Length (1) /= Right'Length (1) or else Left'Length (2) /= Right'Length (2) then
         raise Constraint_Error with "matrices of different shapes";
      end if;
      return Result : Complex_Matrix (Left'Range (1), Left'Range (2)) do
         for I in Left'Range (1) loop
            for J in Left'Range (2) loop
               declare
                  L : Complex renames Left (I, J);
                  R : Complex renames Right (I - Left'First (1) + Right'First (1), J - Left'First (2) + Right'First (2));
               begin
                  Result (I, J) := L + R;
               end;
            end loop;
         end loop;
      end return;
   end "+";

   function "-" (Left, Right : Complex_Matrix) return Complex_Matrix is
   begin
      if Left'Length (1) /= Right'Length (1) or else Left'Length (2) /= Right'Length (2) then
         raise Constraint_Error with "matrices of different shapes";
      end if;
      return Result : Complex_Matrix (Left'Range (1), Left'Range (2)) do
         for I in Left'Range (1) loop
            for J in Left'Range (2) loop
               declare
                  L : Complex renames Left (I, J);
                  R : Complex renames Right (I - Left'First (1) + Right'First (1), J - Left'First (2) + Right'First (2));
               begin
                  Result (I, J) := L - R;
               end;
            end loop;
         end loop;
      end return;
   end "-";

   function "*" (Left, Right : Complex_Matrix) return Complex_Matrix is
   begin
      if Left'Length (2) /= Right'Length (1) then
         raise Constraint_Error with "matrices of incompatible shapes";
      end if;
      return Result : Complex_Matrix (Left'Range (1), Right'Range (2)) do
         for I in Left'Range (1) loop
            for J in Right'Range (2) loop
               declare
                  Sum : Complex := (0.0, 0.0);
               begin
                  for K in Left'Range (2) loop
                     Sum := Sum + Left (I, K) * Right (K - Left'First (2) + Right'First (1), J);
                  end loop;
                  Result (I, J) := Sum;
               end;
            end loop;
         end loop;
      end return;
   end "*";

   function "*" (Left, Right : Complex_Vector) return Complex_Matrix is
   begin
      return Result : Complex_Matrix (Left'Range, Right'Range) do
         for I in Left'Range loop
            for J in Right'Range loop
               Result (I, J) := Left (I) * Right (J);
            end loop;
         end loop;
      end return;
   end "*";

   function "*" (Left  : Complex_Vector;
                 Right : Complex_Matrix) return Complex_Vector is
   begin
      if Left'Length /= Right'Length (1) then
         raise Constraint_Error with "vector and matrix of incompatible shapes";
      end if;
      return Result : Complex_Vector (Right'Range (2)) do
         for J in Right'Range (2) loop
            declare
               Sum : Complex := (0.0, 0.0);
            begin
               for K in Left'Range loop
                  Sum := Sum + Left (K) * Right (K - Left'First + Right'First (1), J);
               end loop;
               Result (J) := Sum;
            end;
         end loop;
      end return;
   end "*";

   function "*" (Left  : Complex_Matrix;
                 Right : Complex_Vector) return Complex_Vector is
   begin
      if Left'Length (2) /= Right'Length then
         raise Constraint_Error with "matrix and vector of incompatible shapes";
      end if;
      return Result : Complex_Vector (Left'Range (1)) do
         for I in Left'Range (1) loop
            declare
               Sum : Complex := (0.0, 0.0);
            begin
               for K in Left'Range (2) loop
                  Sum := Sum + Left (I, K) * Right (K - Left'First (2) + Right'First);
               end loop;
               Result (I) := Sum;
            end;
         end loop;
      end return;
   end "*";

   -- Mixed Real_Matrix and Complex_Matrix arithmetic operations

   function "+" (Left : Real_Matrix; Right : Complex_Matrix) return Complex_Matrix is
   begin
      if Left'Length (1) /= Right'Length (1) or else Left'Length (2) /= Right'Length (2) then
         raise Constraint_Error with "matrices of different shapes";
      end if;
      return Result : Complex_Matrix (Left'Range (1), Left'Range (2)) do
         for I in Left'Range (1) loop
            for J in Left'Range (2) loop
               declare
                  L : Real'Base renames Left (I, J);
                  R : Complex renames Right (I - Left'First (1) + Right'First (1), J - Left'First (2) + Right'First (2));
               begin
                  Result (I, J) := L + R;
               end;
            end loop;
         end loop;
      end return;
   end "+";

   function "+" (Left : Complex_Matrix; Right : Real_Matrix) return Complex_Matrix is
   begin
      if Left'Length (1) /= Right'Length (1) or else Left'Length (2) /= Right'Length (2) then
         raise Constraint_Error with "matrices of different shapes";
      end if;
      return Result : Complex_Matrix (Left'Range (1), Left'Range (2)) do
         for I in Left'Range (1) loop
            for J in Left'Range (2) loop
               declare
                  L : Complex renames Left (I, J);
                  R : Real'Base renames Right (I - Left'First (1) + Right'First (1), J - Left'First (2) + Right'First (2));
               begin
                  Result (I, J) := L + R;
               end;
            end loop;
         end loop;
      end return;
   end "+";

   function "-" (Left : Real_Matrix; Right : Complex_Matrix) return Complex_Matrix is
   begin
      if Left'Length (1) /= Right'Length (1) or else Left'Length (2) /= Right'Length (2) then
         raise Constraint_Error with "matrices of different shapes";
      end if;
      return Result : Complex_Matrix (Left'Range (1), Left'Range (2)) do
         for I in Left'Range (1) loop
            for J in Left'Range (2) loop
               declare
                  L : Real'Base renames Left (I, J);
                  R : Complex renames Right (I - Left'First (1) + Right'First (1), J - Left'First (2) + Right'First (2));
               begin
                  Result (I, J) := L - R;
               end;
            end loop;
         end loop;
      end return;
   end "-";

   function "-" (Left : Complex_Matrix; Right : Real_Matrix) return Complex_Matrix is
   begin
      if Left'Length (1) /= Right'Length (1) or else Left'Length (2) /= Right'Length (2) then
         raise Constraint_Error with "matrices of different shapes";
      end if;
      return Result : Complex_Matrix (Left'Range (1), Left'Range (2)) do
         for I in Left'Range (1) loop
            for J in Left'Range (2) loop
               declare
                  L : Complex renames Left (I, J);
                  R : Real'Base renames Right (I - Left'First (1) + Right'First (1), J - Left'First (2) + Right'First (2));
               begin
                  Result (I, J) := L - R;
               end;
            end loop;
         end loop;
      end return;
   end "-";

   function "*" (Left  : Real_Matrix;
                 Right : Complex_Matrix) return Complex_Matrix is
   begin
      if Left'Length (2) /= Right'Length (1) then
         raise Constraint_Error with "matrices of incompatible shapes";
      end if;
      return Result : Complex_Matrix (Left'Range (1), Right'Range (2)) do
         for I in Left'Range (1) loop
            for J in Right'Range (2) loop
               declare
                  Sum : Complex := (0.0, 0.0);
               begin
                  for K in Left'Range (2) loop
                     Sum := Sum + Left (I, K) * Right (K - Left'First (2) + Right'First (1), J);
                  end loop;
                  Result (I, J) := Sum;
               end;
            end loop;
         end loop;
      end return;
   end "*";

   function "*" (Left  : Complex_Matrix;
                 Right : Real_Matrix) return Complex_Matrix is
   begin
      if Left'Length (2) /= Right'Length (1) then
         raise Constraint_Error with "matrices of incompatible shapes";
      end if;
      return Result : Complex_Matrix (Left'Range (1), Right'Range (2)) do
         for I in Left'Range (1) loop
            for J in Right'Range (2) loop
               declare
                  Sum : Complex := (0.0, 0.0);
               begin
                  for K in Left'Range (2) loop
                     Sum := Sum + Left (I, K) * Right (K - Left'First (2) + Right'First (1), J);
                  end loop;
                  Result (I, J) := Sum;
               end;
            end loop;
         end loop;
      end return;
   end "*";

   function "*" (Left  : Real_Vector;
                 Right : Complex_Vector) return Complex_Matrix is
   begin
      return Result : Complex_Matrix (Left'Range, Right'Range) do
         for I in Left'Range loop
            for J in Right'Range loop
               Result (I, J) := Left (I) * Right (J);
            end loop;
         end loop;
      end return;
   end "*";

   function "*" (Left  : Complex_Vector;
                 Right : Real_Vector) return Complex_Matrix is
   begin
      return Result : Complex_Matrix (Left'Range, Right'Range) do
         for I in Left'Range loop
            for J in Right'Range loop
               Result (I, J) := Left (I) * Right (J);
            end loop;
         end loop;
      end return;
   end "*";

   function "*" (Left  : Real_Vector;
                 Right : Complex_Matrix) return Complex_Vector is
   begin
      if Left'Length /= Right'Length (1) then
         raise Constraint_Error with "vector and matrix of incompatible shapes";
      end if;
      return Result : Complex_Vector (Right'Range (2)) do
         for J in Right'Range (2) loop
            declare
               Sum : Complex := (0.0, 0.0);
            begin
               for K in Left'Range loop
                  Sum := Sum + Left (K) * Right (K - Left'First + Right'First (1), J);
               end loop;
               Result (J) := Sum;
            end;
         end loop;
      end return;
   end "*";

   function "*" (Left  : Complex_Vector;
                 Right : Real_Matrix) return Complex_Vector is
   begin
      if Left'Length /= Right'Length (1) then
         raise Constraint_Error with "vector and matrix of incompatible shapes";
      end if;
      return Result : Complex_Vector (Right'Range (2)) do
         for J in Right'Range (2) loop
            declare
               Sum : Complex := (0.0, 0.0);
            begin
               for K in Left'Range loop
                  Sum := Sum + Left (K) * Right (K - Left'First + Right'First (1), J);
               end loop;
               Result (J) := Sum;
            end;
         end loop;
      end return;
   end "*";

   function "*" (Left  : Real_Matrix;
                 Right : Complex_Vector) return Complex_Vector is
   begin
      if Left'Length (2) /= Right'Length then
         raise Constraint_Error with "matrix and vector of incompatible shapes";
      end if;
      return Result : Complex_Vector (Left'Range (1)) do
         for I in Left'Range (1) loop
            declare
               Sum : Complex := (0.0, 0.0);
            begin
               for K in Left'Range (2) loop
                  Sum := Sum + Left (I, K) * Right (K - Left'First (2) + Right'First);
               end loop;
               Result (I) := Sum;
            end;
         end loop;
      end return;
   end "*";

   function "*" (Left  : Complex_Matrix;
                 Right : Real_Vector) return Complex_Vector is
   begin
      if Left'Length (2) /= Right'Length then
         raise Constraint_Error with "matrix and vector of incompatible shapes";
      end if;
      return Result : Complex_Vector (Left'Range (1)) do
         for I in Left'Range (1) loop
            declare
               Sum : Complex := (0.0, 0.0);
            begin
               for K in Left'Range (2) loop
                  Sum := Sum + Left (I, K) * Right (K - Left'First (2) + Right'First);
               end loop;
               Result (I) := Sum;
            end;
         end loop;
      end return;
   end "*";

   -- Complex_Matrix scaling operations

   function "*" (Left  : Complex;
                 Right : Complex_Matrix) return Complex_Matrix is
   begin
      return Result : Complex_Matrix (Right'Range (1), Right'Range (2)) do
         for I in Right'Range (1) loop
            for J in Right'Range (2) loop
               Result (I, J) := Left * Right (I, J);
            end loop;
         end loop;
      end return;
   end "*";

   function "*" (Left  : Complex_Matrix;
                 Right : Complex) return Complex_Matrix is
   begin
      return Result : Complex_Matrix (Left'Range (1), Left'Range (2)) do
         for I in Left'Range (1) loop
            for J in Left'Range (2) loop
               Result (I, J) := Left (I, J) * Right;
            end loop;
         end loop;
      end return;
   end "*";

   function "/" (Left  : Complex_Matrix;
                 Right : Complex) return Complex_Matrix is
   begin
      return Result : Complex_Matrix (Left'Range (1), Left'Range (2)) do
         for I in Left'Range (1) loop
            for J in Left'Range (2) loop
               Result (I, J) := Left (I, J) / Right;
            end loop;
         end loop;
      end return;
   end "/";

   function "*" (Left  : Real'Base;
                 Right : Complex_Matrix) return Complex_Matrix is
   begin
      return Result : Complex_Matrix (Right'Range (1), Right'Range (2)) do
         for I in Right'Range (1) loop
            for J in Right'Range (2) loop
               Result (I, J) := Left * Right (I, J);
            end loop;
         end loop;
      end return;
   end "*";

   function "*" (Left  : Complex_Matrix;
                 Right : Real'Base) return Complex_Matrix is
   begin
      return Result : Complex_Matrix (Left'Range (1), Left'Range (2)) do
         for I in Left'Range (1) loop
            for J in Left'Range (2) loop
               Result (I, J) := Left (I, J) * Right;
            end loop;
         end loop;
      end return;
   end "*";

   function "/" (Left  : Complex_Matrix;
                 Right : Real'Base) return Complex_Matrix is
   begin
      return Result : Complex_Matrix (Left'Range (1), Left'Range (2)) do
         for I in Left'Range (1) loop
            for J in Left'Range (2) loop
               Result (I, J) := Left (I, J) / Right;
            end loop;
         end loop;
      end return;
   end "/";

   -- Complex_Matrix inversion and related operations

   --  (the LU decomposition of a square matrix (rows interchanged by partial
   --  pivoting on the components' moduli), indexed from 1; Pivots (I) is the
   --  row of A that is row I of LU; Sign is the sign of that permutation;
   --  Singular is True if a pivot is zero)
   type Index_Vector is array (Positive range <>) of Positive;

   procedure Decompose (A        : Complex_Matrix;
                        LU       : out Complex_Matrix;
                        Pivots   : out Index_Vector;
                        Sign     : out Real'Base;
                        Singular : out Boolean) is
      N : constant Natural := A'Length (1);
   begin
      for I in 1 .. N loop
         Pivots (I) := I;
         for J in 1 .. N loop
            LU (I, J) := A (A'First (1) + I - 1, A'First (2) + J - 1);
         end loop;
      end loop;
      Sign := 1.0;
      Singular := False;
      for K in 1 .. N loop
         declare
            P   : Positive := K;
            Max : Real'Base := Complex_Types.Modulus (LU (K, K));
         begin
            for I in K + 1 .. N loop
               if Complex_Types.Modulus (LU (I, K)) > Max then
                  P := I;
                  Max := Complex_Types.Modulus (LU (I, K));
               end if;
            end loop;
            if Max = 0.0 then
               Singular := True;
               return;
            end if;
            if P /= K then
               for J in 1 .. N loop
                  declare
                     T : constant Complex := LU (K, J);
                  begin
                     LU (K, J) := LU (P, J);
                     LU (P, J) := T;
                  end;
               end loop;
               declare
                  T : constant Positive := Pivots (K);
               begin
                  Pivots (K) := Pivots (P);
                  Pivots (P) := T;
               end;
               Sign := -Sign;
            end if;
            for I in K + 1 .. N loop
               LU (I, K) := LU (I, K) / LU (K, K);
               if LU (I, K) /= (0.0, 0.0) then
                  for J in K + 1 .. N loop
                     LU (I, J) := LU (I, J) - LU (I, K) * LU (K, J);
                  end loop;
               end if;
            end loop;
         end;
      end loop;
   end Decompose;

   function Solve (A, X : Complex_Matrix) return Complex_Matrix is
      N        : constant Natural := A'Length (1);
      LU       : Complex_Matrix (1 .. N, 1 .. N);
      Pivots   : Index_Vector (1 .. N);
      Sign     : Real'Base;
      Singular : Boolean;
   begin
      if A'Length (2) /= N or else X'Length (1) /= N then
         raise Constraint_Error with "matrices of incompatible shapes";
      end if;
      Decompose (A, LU, Pivots, Sign, Singular);
      if Singular then
         raise Constraint_Error with "matrix is singular";
      end if;
      return Result : Complex_Matrix (A'Range (2), X'Range (2)) do
         for C in X'Range (2) loop
            declare
               Y : Complex_Vector (1 .. N);
            begin
               --  (forward substitution (L has a unit diagonal), then back)
               for I in 1 .. N loop
                  Y (I) := X (X'First (1) + Pivots (I) - 1, C);
                  for J in 1 .. I - 1 loop
                     Y (I) := Y (I) - LU (I, J) * Y (J);
                  end loop;
               end loop;
               for I in reverse 1 .. N loop
                  for J in I + 1 .. N loop
                     Y (I) := Y (I) - LU (I, J) * Y (J);
                  end loop;
                  Y (I) := Y (I) / LU (I, I);
               end loop;
               for I in 1 .. N loop
                  Result (A'First (2) + I - 1, C) := Y (I);
               end loop;
            end;
         end loop;
      end return;
   end Solve;

   function Solve (A : Complex_Matrix; X : Complex_Vector)
      return Complex_Vector is
      XM : Complex_Matrix (X'Range, 1 .. 1);
   begin
      if A'Length (1) /= X'Length then
         raise Constraint_Error with "matrix and vector of incompatible shapes";
      end if;
      for I in X'Range loop
         XM (I, 1) := X (I);
      end loop;
      declare
         YM : constant Complex_Matrix := Solve (A, XM);
      begin
         return Result : Complex_Vector (A'Range (2)) do
            for I in Result'Range loop
               Result (I) := YM (I, 1);
            end loop;
         end return;
      end;
   end Solve;

   function Inverse (A : Complex_Matrix) return Complex_Matrix is
   begin
      if A'Length (1) /= A'Length (2) then
         raise Constraint_Error with "matrix is not square";
      end if;
      return Solve (A, Unit_Matrix (A'Length (1), A'First (1), A'First (1)));
   end Inverse;

   function Determinant (A : Complex_Matrix) return Complex is
      N        : constant Natural := A'Length (1);
      LU       : Complex_Matrix (1 .. N, 1 .. N);
      Pivots   : Index_Vector (1 .. N);
      Sign     : Real'Base;
      Singular : Boolean;
   begin
      if A'Length (2) /= N then
         raise Constraint_Error with "matrix is not square";
      end if;
      Decompose (A, LU, Pivots, Sign, Singular);
      if Singular then
         return (0.0, 0.0);
      end if;
      return Result : Complex := (Sign, 0.0) do
         for I in 1 .. N loop
            Result := Result * LU (I, I);
         end loop;
      end return;
   end Determinant;

   -- Eigenvalues and vectors of a Hermitian matrix

   --  (the eigenvalues (sorted into order with the largest first) and the
   --  eigenvectors (Vectors' columns, in the same order) of the Hermitian
   --  matrix A (indexed from 1 in each), by the cyclic Jacobi method: each
   --  rotation is that of the real symmetric method preceded by the unitary
   --  scaling of a row and column that makes the component to be annihilated
   --  real)
   procedure Jacobi (A       : Complex_Matrix;
                     Values  : out Real_Vector;
                     Vectors : out Complex_Matrix) is
      N : constant Natural := A'Length (1);
      M : Complex_Matrix (1 .. N, 1 .. N);
      V : Complex_Matrix (1 .. N, 1 .. N) := Unit_Matrix (N);
   begin
      if A'Length (2) /= N then
         raise Constraint_Error with "matrix is not square";
      end if;
      for I in 1 .. N loop
         for J in 1 .. N loop
            M (I, J) := A (A'First (1) + I - 1, A'First (2) + J - 1);
         end loop;
      end loop;
      for I in 1 .. N loop
         for J in I .. N loop
            if M (I, J) /= Complex_Types.Conjugate (M (J, I)) then
               raise Argument_Error with "matrix is not Hermitian";
            end if;
         end loop;
      end loop;
      for Sweep in 1 .. Max_Sweeps + 1 loop
         declare
            Off : Real'Base := 0.0;
         begin
            for I in 1 .. N loop
               for J in I + 1 .. N loop
                  Off := Off + abs M (I, J).Re + abs M (I, J).Im;
               end loop;
            end loop;
            exit when Off = 0.0;
            if Sweep > Max_Sweeps then
               raise Constraint_Error with "eigenvalues did not converge";
            end if;
         end;
         for P in 1 .. N - 1 loop
            for Q in P + 1 .. N loop
               declare
                  MPP : constant Real'Base := M (P, P).Re;
                  MQQ : constant Real'Base := M (Q, Q).Re;
                  G   : constant Real'Base := Complex_Types.Modulus (M (P, Q));
                  G100 : constant Real'Base := 100.0 * G;
                  H   : constant Real'Base := MQQ - MPP;
                  T   : Real'Base;
               begin
                  if G = 0.0 then
                     M (P, Q) := (0.0, 0.0);
                     M (Q, P) := (0.0, 0.0);
                  elsif Sweep > 4 and then abs MPP + G100 = abs MPP and then abs MQQ + G100 = abs MQQ then
                     --  (negligible: zero)
                     M (P, Q) := (0.0, 0.0);
                     M (Q, P) := (0.0, 0.0);
                  else
                     --  (column Q scaled by the conjugate of M (P, Q)'s phase,
                     --  row Q by the phase: M (P, Q) and M (Q, P) are G)
                     declare
                        Phase : constant Complex := M (P, Q) / G;
                        Back  : constant Complex := Complex_Types.Conjugate (Phase);
                     begin
                        for K in 1 .. N loop
                           M (K, Q) := M (K, Q) * Back;
                           V (K, Q) := V (K, Q) * Back;
                        end loop;
                        for K in 1 .. N loop
                           M (Q, K) := M (Q, K) * Phase;
                        end loop;
                     end;
                     --  (the rotation that annihilates M (P, Q): its tangent T)
                     if abs H + G100 = abs H then
                        T := G / H;
                     else
                        declare
                           Theta : constant Real'Base := 0.5 * H / G;
                        begin
                           T := 1.0 / (abs Theta + Elementary.Sqrt (Theta * Theta + 1.0));
                           if Theta < 0.0 then
                              T := -T;
                           end if;
                        end;
                     end if;
                     declare
                        C : constant Real'Base := 1.0 / Elementary.Sqrt (T * T + 1.0);
                        S : constant Real'Base := T * C;
                     begin
                        for K in 1 .. N loop
                           declare
                              MKP : constant Complex := M (K, P);
                              MKQ : constant Complex := M (K, Q);
                              VKP : constant Complex := V (K, P);
                              VKQ : constant Complex := V (K, Q);
                           begin
                              M (K, P) := C * MKP - S * MKQ;
                              M (K, Q) := S * MKP + C * MKQ;
                              V (K, P) := C * VKP - S * VKQ;
                              V (K, Q) := S * VKP + C * VKQ;
                           end;
                        end loop;
                        for K in 1 .. N loop
                           declare
                              MPK : constant Complex := M (P, K);
                              MQK : constant Complex := M (Q, K);
                           begin
                              M (P, K) := C * MPK - S * MQK;
                              M (Q, K) := S * MPK + C * MQK;
                           end;
                        end loop;
                     end;
                     M (P, P) := (M (P, P).Re, 0.0);
                     M (Q, Q) := (M (Q, Q).Re, 0.0);
                     M (P, Q) := (0.0, 0.0);
                     M (Q, P) := (0.0, 0.0);
                  end if;
               end;
            end loop;
         end loop;
      end loop;
      --  (sorted into order with the largest first (a selection sort))
      for I in 1 .. N loop
         Values (I) := M (I, I).Re;
      end loop;
      for I in 1 .. N - 1 loop
         declare
            Max : Positive := I;
         begin
            for J in I + 1 .. N loop
               if Values (J) > Values (Max) then
                  Max := J;
               end if;
            end loop;
            if Max /= I then
               declare
                  T : constant Real'Base := Values (I);
               begin
                  Values (I) := Values (Max);
                  Values (Max) := T;
               end;
               for K in 1 .. N loop
                  declare
                     T : constant Complex := V (K, I);
                  begin
                     V (K, I) := V (K, Max);
                     V (K, Max) := T;
                  end;
               end loop;
            end if;
         end;
      end loop;
      Vectors := V;
   end Jacobi;

   function Eigenvalues (A : Complex_Matrix) return Real_Vector is
      N       : constant Natural := A'Length (1);
      Values  : Real_Vector (1 .. N);
      Vectors : Complex_Matrix (1 .. N, 1 .. N);
   begin
      Jacobi (A, Values, Vectors);
      return Result : Real_Vector (A'Range (1)) do
         Result := Values;
      end return;
   end Eigenvalues;

   procedure Eigensystem (A       : in  Complex_Matrix;
                          Values  : out Real_Vector;
                          Vectors : out Complex_Matrix) is
      N : constant Natural := A'Length (1);
      W : Real_Vector (1 .. N);
      V : Complex_Matrix (1 .. N, 1 .. N);
   begin
      if A'Length (2) /= N or else Values'First /= A'First (1) or else Values'Last /= A'Last (1) or else
        Vectors'First (1) /= A'First (1) or else Vectors'Last (1) /= A'Last (1) or else
        Vectors'First (2) /= A'First (2) or else Vectors'Last (2) /= A'Last (2)
      then
         raise Constraint_Error with "index ranges are not those of the matrix";
      end if;
      Jacobi (A, W, V);
      Values := W;
      Vectors := V;
   end Eigensystem;

   -- Other Complex_Matrix operations

   function Unit_Matrix (Order            : Positive;
                         First_1, First_2 : Integer := 1)
                                            return Complex_Matrix is
   begin
      if First_1 > Integer'Last - Order + 1 or else First_2 > Integer'Last - Order + 1 then
         raise Constraint_Error with "index range exceeds Integer'Last";
      end if;
      return Result : Complex_Matrix (First_1 .. First_1 + Order - 1, First_2 .. First_2 + Order - 1) :=
        (others => (others => (0.0, 0.0)))
      do
         for I in 0 .. Order - 1 loop
            Result (First_1 + I, First_2 + I) := (1.0, 0.0);
         end loop;
      end return;
   end Unit_Matrix;

end Ada.Numerics.Generic_Complex_Arrays;
