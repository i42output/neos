--  Ada.Real_Time.adb
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

--  The operators' values are those of the integer operators on the numbers
--  of nanoseconds (RM D.8(27)): Constraint_Error if they overflow.

with Ada.Unchecked_Conversion;

package body Ada.Real_Time is

   use type Interfaces.Integer_64;

   subtype Integer_64 is Interfaces.Integer_64;

   function Clock_Monotonic return Integer_64;
   pragma Import (Neos, Clock_Monotonic, "ada::clock_monotonic");

   --  (Duration is a number of nanoseconds)
   function From_Duration is
     new Ada.Unchecked_Conversion (Duration, Integer_64);
   function From_Integer is
     new Ada.Unchecked_Conversion (Integer_64, Duration);

   function I (T : Time) return Integer_64 is
   begin
      return Integer_64 (T);
   end I;

   function I (T : Time_Span) return Integer_64 is
   begin
      return Integer_64 (T);
   end I;

   function Clock return Time is
   begin
      return Time (Clock_Monotonic);
   end Clock;

   function "+" (Left : Time; Right : Time_Span) return Time is
   begin
      return Time (I (Left) + I (Right));
   end "+";

   function "+" (Left : Time_Span; Right : Time) return Time is
   begin
      return Time (I (Left) + I (Right));
   end "+";

   function "-" (Left : Time; Right : Time_Span) return Time is
   begin
      return Time (I (Left) - I (Right));
   end "-";

   function "-" (Left : Time; Right : Time) return Time_Span is
   begin
      return Time_Span (I (Left) - I (Right));
   end "-";

   function "<" (Left, Right : Time) return Boolean is
   begin
      return I (Left) < I (Right);
   end "<";

   function "<=" (Left, Right : Time) return Boolean is
   begin
      return I (Left) <= I (Right);
   end "<=";

   function ">" (Left, Right : Time) return Boolean is
   begin
      return I (Left) > I (Right);
   end ">";

   function ">=" (Left, Right : Time) return Boolean is
   begin
      return I (Left) >= I (Right);
   end ">=";

   function "+" (Left, Right : Time_Span) return Time_Span is
   begin
      return Time_Span (I (Left) + I (Right));
   end "+";

   function "-" (Left, Right : Time_Span) return Time_Span is
   begin
      return Time_Span (I (Left) - I (Right));
   end "-";

   function "-" (Right : Time_Span) return Time_Span is
   begin
      return Time_Span (-I (Right));
   end "-";

   function "*" (Left : Time_Span; Right : Integer) return Time_Span is
   begin
      return Time_Span (I (Left) * Integer_64 (Right));
   end "*";

   function "*" (Left : Integer; Right : Time_Span) return Time_Span is
   begin
      return Time_Span (Integer_64 (Left) * I (Right));
   end "*";

   function "/" (Left, Right : Time_Span) return Integer is
   begin
      return Integer (I (Left) / I (Right));
   end "/";

   function "/" (Left : Time_Span; Right : Integer) return Time_Span is
   begin
      return Time_Span (I (Left) / Integer_64 (Right));
   end "/";

   function "abs" (Right : Time_Span) return Time_Span is
   begin
      return Time_Span (abs I (Right));
   end "abs";

   function "<" (Left, Right : Time_Span) return Boolean is
   begin
      return I (Left) < I (Right);
   end "<";

   function "<=" (Left, Right : Time_Span) return Boolean is
   begin
      return I (Left) <= I (Right);
   end "<=";

   function ">" (Left, Right : Time_Span) return Boolean is
   begin
      return I (Left) > I (Right);
   end ">";

   function ">=" (Left, Right : Time_Span) return Boolean is
   begin
      return I (Left) >= I (Right);
   end ">=";

   function To_Duration (TS : Time_Span) return Duration is
   begin
      if I (TS) = Integer_64'First then
         raise Constraint_Error;  --  (not a value of Duration)
      end if;
      return From_Integer (I (TS));
   end To_Duration;

   function To_Time_Span (D : Duration) return Time_Span is
   begin
      return Time_Span (From_Duration (D));
   end To_Time_Span;

   function Nanoseconds (NS : Integer) return Time_Span is
   begin
      return Time_Span (Integer_64 (NS));
   end Nanoseconds;

   function Microseconds (US : Integer) return Time_Span is
   begin
      return Time_Span (Integer_64 (US) * 1_000);
   end Microseconds;

   function Milliseconds (MS : Integer) return Time_Span is
   begin
      return Time_Span (Integer_64 (MS) * 1_000_000);
   end Milliseconds;

   function Seconds (S : Integer) return Time_Span is
   begin
      return Time_Span (Integer_64 (S) * 1_000_000_000);
   end Seconds;

   function Minutes (M : Integer) return Time_Span is
   begin
      return Time_Span (Integer_64 (M) * 60_000_000_000);
   end Minutes;

   procedure Split (T : in Time; SC : out Seconds_Count; TS : out Time_Span) is
      N : constant Integer_64 := I (T);
      S : Integer_64;
   begin
      if N >= 0 then
         S := N / 1_000_000_000;
      else
         S := -((-(N + 1)) / 1_000_000_000) - 1;
      end if;
      SC := Seconds_Count (S);
      TS := Time_Span (N - S * 1_000_000_000);
   end Split;

   --  (TS's whole seconds are added to SC first, so that the intermediate
   --  values overflow only if the result does)
   function Time_Of (SC : Seconds_Count; TS : Time_Span) return Time is
      S : constant Integer_64 := Integer_64 (SC) + I (TS) / 1_000_000_000;
   begin
      return Time (S * 1_000_000_000 + I (TS) rem 1_000_000_000);
   end Time_Of;

end Ada.Real_Time;
