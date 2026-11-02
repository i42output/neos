--  Ada.Numerics.Discrete_Random.adb
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

--  The generator is xoshiro256** (D. Blackman and S. Vigna, "Scrambled
--  linear pseudorandom number generators", 2018): its period is 2**256 - 1.
--  Its state is seeded from an Initiator (or from the time-dependent seed,
--  a native primitive that mixes the system's entropy source with the
--  clock, so that successive calls of the time-dependent Reset give different
--  states however close together they are) by the SplitMix64 generator.
--
--  Random maps the generator's values onto the result range without bias
--  (values beyond the largest multiple of the range's size are rejected), so
--  every value of the range is equally likely; a range of 2**64 values takes
--  the generator's values as they are.
--
--  Image gives the state as four groups of 16 hexadecimal digits separated by
--  minus signs (67 characters); Value accepts that form (with upper or lower
--  case digits, and leading and trailing spaces) and raises Constraint_Error
--  for any other string or for the all-zero state, which no generator has.

with Ada.Unchecked_Deallocation;
with Interfaces;

package body Ada.Numerics.Discrete_Random is

   function Random_Seed return Interfaces.Integer_64;
   pragma Import (Neos, Random_Seed, "ada::random_seed");

   type Signed is range -2 ** 63 .. 2 ** 63 - 1;

   procedure Free is new Ada.Unchecked_Deallocation (State, State_Access);

   function Rotate_Left (X : Word; K : Natural) return Word is
   begin
      return X * 2 ** K or X / 2 ** (64 - K);
   end Rotate_Left;

   --  The next value of the sequence (advancing the state)
   function Next (S : in out State) return Word is
      Result : constant Word := Rotate_Left (S.S1 * 5, 7) * 9;
      T      : constant Word := S.S1 * 2 ** 17;
   begin
      S.S2 := S.S2 xor S.S0;
      S.S3 := S.S3 xor S.S1;
      S.S1 := S.S1 xor S.S2;
      S.S0 := S.S0 xor S.S3;
      S.S2 := S.S2 xor T;
      S.S3 := Rotate_Left (S.S3, 45);
      return Result;
   end Next;

   --  A state from a seed (SplitMix64)
   function Seeded (Seed : Word) return State is
      X : Word := Seed;
      function Mix return Word is
         Z : Word;
      begin
         X := X + 16#9E37_79B9_7F4A_7C15#;
         Z := X;
         Z := (Z xor Z / 2 ** 30) * 16#BF58_476D_1CE4_E5B9#;
         Z := (Z xor Z / 2 ** 27) * 16#94D0_49BB_1331_11EB#;
         return Z xor Z / 2 ** 31;
      end Mix;
      S : State;
   begin
      S.S0 := Mix;
      S.S1 := Mix;
      S.S2 := Mix;
      S.S3 := Mix;
      if (S.S0 or S.S1 or S.S2 or S.S3) = 0 then
         S.S0 := 1;
      end if;
      return S;
   end Seeded;

   overriding procedure Initialize (Gen : in out Generator) is
   begin
      Gen.Current := new State;
   end Initialize;

   overriding procedure Finalize (Gen : in out Generator) is
   begin
      Free (Gen.Current);
   end Finalize;

   ------------
   -- Random --
   ------------

   --  A value in 0 .. Span (with equal probability)
   function Uniform (Gen : Generator; Span : Word) return Word is
      Count     : Word;
      Threshold : Word;
      X         : Word;
   begin
      if Span = Word'Last then
         return Next (Gen.Current.all);
      end if;
      Count := Span + 1;
      Threshold := (0 - Count) mod Count;  --  (2**64 mod Count)
      loop
         X := Next (Gen.Current.all);
         exit when X >= Threshold;
      end loop;
      return X mod Count;
   end Uniform;

   function Random
     (Gen   : Generator;
      First : Result_Subtype;
      Last  : Result_Subtype) return Result_Subtype
   is
      Low    : constant Word := Word'Mod (Result_Subtype'Pos (First));
      High   : constant Word := Word'Mod (Result_Subtype'Pos (Last));
      Offset : Word;
      V      : Word;
   begin
      if Last < First then
         raise Constraint_Error;
      end if;
      Offset := Uniform (Gen, High - Low);
      if Offset = 0 then
         return First;
      end if;
      V := Low + Offset;
      if Result_Subtype'Pos (First) < 0 and then V >= 2 ** 63 then
         --  (a negative position)
         return Result_Subtype'Val
           (-Signed (not V) - 1);
      else
         return Result_Subtype'Val (V);
      end if;
   end Random;

   function Random (Gen : Generator) return Result_Subtype is
   begin
      return Random (Gen, Result_Subtype'First, Result_Subtype'Last);
   end Random;

   -----------
   -- Reset --
   -----------

   procedure Reset (Gen : in Generator; Initiator : in Integer) is
   begin
      Gen.Current.all := Seeded (Word'Mod (Initiator));
   end Reset;

   procedure Reset (Gen : in Generator) is
   begin
      Gen.Current.all := Seeded (Word'Mod (Random_Seed));
   end Reset;

   procedure Save (Gen : in Generator; To_State : out State) is
   begin
      To_State := Gen.Current.all;
   end Save;

   procedure Reset (Gen : in Generator; From_State : in State) is
   begin
      Gen.Current.all := From_State;
   end Reset;

   -----------
   -- Image --
   -----------

   function Image (Of_State : State) return String is
      Digits_Of : constant String := "0123456789ABCDEF";
      Result    : String (1 .. Max_Image_Width) := (others => '-');
      procedure Put (W : Word; At_Index : Positive) is
         V : Word := W;
      begin
         for I in reverse At_Index .. At_Index + 15 loop
            Result (I) := Digits_Of (Integer (V mod 16) + 1);
            V := V / 16;
         end loop;
      end Put;
   begin
      Put (Of_State.S0, 1);
      Put (Of_State.S1, 18);
      Put (Of_State.S2, 35);
      Put (Of_State.S3, 52);
      return Result;
   end Image;

   -----------
   -- Value --
   -----------

   function Value (Coded_State : String) return State is
      First : Integer := Coded_State'First;
      Last  : Integer := Coded_State'Last;
      function Get (At_Index : Integer) return Word is
         V : Word := 0;
         C : Character;
      begin
         for I in At_Index .. At_Index + 15 loop
            C := Coded_State (I);
            case C is
               when '0' .. '9' =>
                  V := V * 16 + Word (Character'Pos (C) - Character'Pos ('0'));
               when 'A' .. 'F' =>
                  V := V * 16 + Word (Character'Pos (C) - Character'Pos ('A') + 10);
               when 'a' .. 'f' =>
                  V := V * 16 + Word (Character'Pos (C) - Character'Pos ('a') + 10);
               when others =>
                  raise Constraint_Error;
            end case;
         end loop;
         return V;
      end Get;
      S : State;
   begin
      while First <= Last and then Coded_State (First) = ' ' loop
         First := First + 1;
      end loop;
      while Last >= First and then Coded_State (Last) = ' ' loop
         Last := Last - 1;
      end loop;
      if Last - First + 1 /= Max_Image_Width
        or else Coded_State (First + 16) /= '-'
        or else Coded_State (First + 33) /= '-'
        or else Coded_State (First + 50) /= '-'
      then
         raise Constraint_Error;
      end if;
      S.S0 := Get (First);
      S.S1 := Get (First + 17);
      S.S2 := Get (First + 34);
      S.S3 := Get (First + 51);
      if (S.S0 or S.S1 or S.S2 or S.S3) = 0 then
         raise Constraint_Error;
      end if;
      return S;
   end Value;

begin
   if Result_Subtype'Last < Result_Subtype'First then
      raise Constraint_Error;  --  (A.5.2(39))
   end if;
end Ada.Numerics.Discrete_Random;
