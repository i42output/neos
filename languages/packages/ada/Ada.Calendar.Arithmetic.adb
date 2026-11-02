--  Ada.Calendar.Arithmetic.adb
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

--  Leap seconds are not supported (Leap_Seconds is always 0: see
--  RM 9.6.1(89)); a day is 86_400 seconds.

package body Ada.Calendar.Arithmetic is

   use type Interfaces.Integer_64;

   subtype Integer_64 is Interfaces.Integer_64;

   --  Floor division by a day and its remainder
   procedure Split_Days (T : Time; Days, Rest : out Integer_64) is
      N : constant Integer_64 := Integer_64 (T);
   begin
      if N >= 0 then
         Days := N / Nanoseconds_Per_Day;
      else
         Days := -((-(N + 1)) / Nanoseconds_Per_Day) - 1;
      end if;
      Rest := N - Days * Nanoseconds_Per_Day;
   end Split_Days;

   procedure Difference (Left, Right : in Time;
                         Days : out Day_Count;
                         Seconds : out Duration;
                         Leap_Seconds : out Leap_Seconds_Count)
   is
      Left_Days, Left_Rest, Right_Days, Right_Rest : Integer_64;
      D, N : Integer_64;
   begin
      Split_Days (Left, Left_Days, Left_Rest);
      Split_Days (Right, Right_Days, Right_Rest);
      D := Left_Days - Right_Days;
      N := Left_Rest - Right_Rest;
      if D > 0 and then N < 0 then
         D := D - 1;
         N := N + Nanoseconds_Per_Day;
      elsif D < 0 and then N > 0 then
         D := D + 1;
         N := N - Nanoseconds_Per_Day;
      end if;
      Days := Day_Count (D);
      Seconds := To_Duration (N);
      Leap_Seconds := 0;
   end Difference;

   function Add (Left : Time; Right : Day_Count) return Time is
      Limit : constant := Integer_64'Last / Nanoseconds_Per_Day;
      N     : Integer_64;
      T     : constant Integer_64 := Integer_64 (Left);
   begin
      if abs Integer_64 (Right) > Limit then
         raise Time_Error;
      end if;
      N := Integer_64 (Right) * Nanoseconds_Per_Day;
      if (N > 0 and then T > Integer_64'Last - N)
        or else (N < 0 and then T < Integer_64'First - N)
      then
         raise Time_Error;
      end if;
      return Time (T + N);
   end Add;

   function "+" (Left : Time; Right : Day_Count) return Time is
   begin
      return Add (Left, Right);
   end "+";

   function "+" (Left : Day_Count; Right : Time) return Time is
   begin
      return Add (Right, Left);
   end "+";

   function "-" (Left : Time; Right : Day_Count) return Time is
   begin
      return Add (Left, -Right);
   end "-";

   function "-" (Left, Right : Time) return Day_Count is
      Days : Day_Count;
      Seconds : Duration;
      Leap_Seconds : Leap_Seconds_Count;
   begin
      Difference (Left, Right, Days, Seconds, Leap_Seconds);
      return Days;
   end "-";

end Ada.Calendar.Arithmetic;
