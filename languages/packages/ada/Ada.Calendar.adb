--  Ada.Calendar.adb
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

--  Calendar's time zone is the local time zone (as the host gives it for
--  each time; UTC if it does not). The clock is the host's real time clock,
--  whose resolution is the host's (typically a microsecond or better).

with Ada.Unchecked_Conversion;

package body Ada.Calendar is

   use type Interfaces.Integer_64;

   subtype Integer_64 is Interfaces.Integer_64;

   function Clock_Realtime return Integer_64;
   pragma Import (Neos, Clock_Realtime, "ada::clock_realtime");

   function UTC_Offset (Seconds : Integer_64) return Integer_64;
   pragma Import (Neos, UTC_Offset, "ada::utc_offset");

   function From_Duration is
     new Ada.Unchecked_Conversion (Duration, Integer_64);
   function From_Integer is
     new Ada.Unchecked_Conversion (Integer_64, Duration);

   function To_Nanoseconds (D : Duration) return Interfaces.Integer_64 is
   begin
      return From_Duration (D);
   end To_Nanoseconds;

   function To_Duration (N : Interfaces.Integer_64) return Duration is
   begin
      return From_Integer (N);
   end To_Duration;

   function I (T : Time) return Integer_64 is
   begin
      return Integer_64 (T);
   end I;

   --  Floor division and its remainder (0 .. Divisor - 1)
   function Floor_Div (N, Divisor : Integer_64) return Integer_64 is
   begin
      if N >= 0 then
         return N / Divisor;
      else
         return -((-(N + 1)) / Divisor) - 1;
      end if;
   end Floor_Div;

   ---------------------------
   -- The Gregorian calendar --
   ---------------------------

   --  (counted in 400 year eras of 146_097 days from 0000-03-01, so that the
   --  leap day is the last day of a year)

   Era_Days    : constant := 146_097;
   Origin_Days : constant := 785_212;  --  (0000-03-01 .. 2150-01-01)

   function Days_Of (Year, Month, Day : Integer) return Interfaces.Integer_64 is
      Y   : constant Integer_64 :=
        Integer_64 (Year) - (if Month <= 2 then 1 else 0);
      Era : constant Integer_64 := Floor_Div (Y, 400);
      YoE : constant Integer_64 := Y - Era * 400;  --  0 .. 399
      M   : constant Integer_64 :=
        Integer_64 (if Month > 2 then Month - 3 else Month + 9);  --  0 .. 11
      DoY : constant Integer_64 := (153 * M + 2) / 5 + Integer_64 (Day) - 1;
      DoE : constant Integer_64 := YoE * 365 + YoE / 4 - YoE / 100 + DoY;
   begin
      return Era * Era_Days + DoE - Origin_Days;
   end Days_Of;

   procedure Date_Of (Days  : Interfaces.Integer_64;
                      Year  : out Integer;
                      Month : out Integer;
                      Day   : out Integer)
   is
      Z   : constant Integer_64 := Days + Origin_Days;
      Era : constant Integer_64 := Floor_Div (Z, Era_Days);
      DoE : constant Integer_64 := Z - Era * Era_Days;  --  0 .. 146_096
      YoE : constant Integer_64 :=
        (DoE - DoE / 1460 + DoE / 36_524 - DoE / 146_096) / 365;
      DoY : constant Integer_64 := DoE - (365 * YoE + YoE / 4 - YoE / 100);
      MP  : constant Integer_64 := (5 * DoY + 2) / 153;  --  0 .. 11
      M   : constant Integer_64 := (if MP < 10 then MP + 3 else MP - 9);
   begin
      Day := Integer (DoY - (153 * MP + 2) / 5 + 1);
      Month := Integer (M);
      Year := Integer (YoE + Era * 400 + (if M <= 2 then 1 else 0));
   end Date_Of;

   function Is_Date (Year, Month, Day : Integer) return Boolean is
      Leap : constant Boolean :=
        Year mod 4 = 0 and then (Year mod 100 /= 0 or else Year mod 400 = 0);
      Last : Integer;
   begin
      case Month is
         when 1 | 3 | 5 | 7 | 8 | 10 | 12 => Last := 31;
         when 4 | 6 | 9 | 11 => Last := 30;
         when 2 => Last := (if Leap then 29 else 28);
         when others => return False;
      end case;
      return Day in 1 .. Last;
   end Is_Date;

   ------------------
   -- Local_Offset --
   ------------------

   function Local_Offset (Date : Time) return Integer is
      Offset : constant Integer_64 :=
        UTC_Offset (Floor_Div (I (Date), Nanoseconds_Per_Second)
                      + Unix_Origin / Nanoseconds_Per_Second);
   begin
      if Offset < -28 * 60 or else Offset > 28 * 60 then
         return Unknown;
      end if;
      return Integer (Offset);
   end Local_Offset;

   --  (UTC if it is not known)
   function Zone (Date : Time) return Integer is
      Offset : constant Integer := Local_Offset (Date);
   begin
      return (if Offset = Unknown then 0 else Offset);
   end Zone;

   ----------------
   -- Split_Time --
   ----------------

   procedure Split_Time (Date    : Time;
                         Offset  : Integer;
                         Year    : out Year_Number;
                         Month   : out Month_Number;
                         Day     : out Day_Number;
                         Seconds : out Interfaces.Integer_64)
   is
      Shift : constant Integer_64 :=
        Integer_64 (Offset) * 60 * Nanoseconds_Per_Second;
      Local : Integer_64;
      Days  : Integer_64;
      Y, M, D : Integer;
   begin
      if (Shift > 0 and then I (Date) > Integer_64'Last - Shift)
        or else (Shift < 0 and then I (Date) < Integer_64'First - Shift)
      then
         raise Time_Error;
      end if;
      Local := I (Date) + Shift;
      Days := Floor_Div (Local, Nanoseconds_Per_Day);
      Date_Of (Days, Y, M, D);
      if Y not in Year_Number then
         raise Time_Error;
      end if;
      Year := Y;
      Month := M;
      Day := D;
      Seconds := Local - Days * Nanoseconds_Per_Day;
   end Split_Time;

   ---------------
   -- Make_Time --
   ---------------

   function Make_Time (Year, Month, Day : Integer;
                       Nanoseconds      : Interfaces.Integer_64;
                       Offset           : Integer) return Time
   is
      Limit : constant := 106_000;  --  (days either side of the origin)
      Days  : Integer_64;
   begin
      if not Is_Date (Year, Month, Day) then
         raise Time_Error;
      end if;
      Days := Days_Of (Year, Month, Day);
      if abs Days > Limit then
         raise Time_Error;
      end if;
      return Time (Days * Nanoseconds_Per_Day + Nanoseconds
                     - Integer_64 (Offset) * 60 * Nanoseconds_Per_Second);
   exception
      when Constraint_Error =>
         raise Time_Error;
   end Make_Time;

   -----------
   -- Clock --
   -----------

   function Clock return Time is
   begin
      return Time (Clock_Realtime - Unix_Origin);
   end Clock;

   ----------------------------------
   -- Year, Month, Day and Seconds --
   ----------------------------------

   procedure Split (Date    : in Time;
                    Year    : out Year_Number;
                    Month   : out Month_Number;
                    Day     : out Day_Number;
                    Seconds : out Day_Duration)
   is
      N : Integer_64;
   begin
      Split_Time (Date, Zone (Date), Year, Month, Day, N);
      Seconds := To_Duration (N);
   end Split;

   function Year (Date : Time) return Year_Number is
      Y : Year_Number;
      M : Month_Number;
      D : Day_Number;
      S : Day_Duration;
   begin
      Split (Date, Y, M, D, S);
      return Y;
   end Year;

   function Month (Date : Time) return Month_Number is
      Y : Year_Number;
      M : Month_Number;
      D : Day_Number;
      S : Day_Duration;
   begin
      Split (Date, Y, M, D, S);
      return M;
   end Month;

   function Day (Date : Time) return Day_Number is
      Y : Year_Number;
      M : Month_Number;
      D : Day_Number;
      S : Day_Duration;
   begin
      Split (Date, Y, M, D, S);
      return D;
   end Day;

   function Seconds (Date : Time) return Day_Duration is
      Y : Year_Number;
      M : Month_Number;
      D : Day_Number;
      S : Day_Duration;
   begin
      Split (Date, Y, M, D, S);
      return S;
   end Seconds;

   -------------
   -- Time_Of --
   -------------

   --  (the local time: the offset is the one at the time that the date and
   --  time would be with the offset of that time, so that times in the
   --  hour that a change to summer time skips are taken as winter time)
   function Time_Of (Year    : Year_Number;
                     Month   : Month_Number;
                     Day     : Day_Number;
                     Seconds : Day_Duration := 0.0)
     return Time
   is
      N       : constant Integer_64 := To_Nanoseconds (Seconds);
      As_UTC  : constant Time := Make_Time (Year, Month, Day, N, 0);
      Guess   : constant Integer := Zone (As_UTC);
      Offset  : constant Integer :=
        Zone (Make_Time (Year, Month, Day, N, Guess));
   begin
      return Make_Time (Year, Month, Day, N, Offset);
   end Time_Of;

   ---------------
   -- Operators --
   ---------------

   function Add (T : Time; N : Integer_64) return Time is
   begin
      if (N > 0 and then I (T) > Integer_64'Last - N)
        or else (N < 0 and then I (T) < Integer_64'First - N)
      then
         raise Time_Error;
      end if;
      return Time (I (T) + N);
   end Add;

   function "+" (Left : Time; Right : Duration) return Time is
   begin
      return Add (Left, To_Nanoseconds (Right));
   end "+";

   function "+" (Left : Duration; Right : Time) return Time is
   begin
      return Add (Right, To_Nanoseconds (Left));
   end "+";

   function "-" (Left : Time; Right : Duration) return Time is
      N : constant Integer_64 := To_Nanoseconds (Right);
   begin
      if N = Integer_64'First then
         raise Time_Error;
      end if;
      return Add (Left, -N);
   end "-";

   function "-" (Left : Time; Right : Time) return Duration is
      L : constant Integer_64 := I (Left);
      R : constant Integer_64 := I (Right);
   begin
      if (R < 0 and then L > Integer_64'Last + R)
        or else (R > 0 and then L < Integer_64'First + R + 1)
      then
         raise Time_Error;
      end if;
      return To_Duration (L - R);
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

end Ada.Calendar;
