--  Ada.Calendar.Formatting.adb
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

--  Leap seconds are not supported (RM 9.6.1(89)): Split gives Leap_Second
--  False and Time_Of raises Time_Error if Leap_Second is True. Day_of_Week
--  is that of the date in Calendar's time zone (the local time zone), as are
--  Calendar's Year, Month and Day. The image of an elapsed time of 100 hours
--  or more has as many digits of hours as it needs (and Value accepts that
--  form). Value accepts exactly the forms that Image gives.

package body Ada.Calendar.Formatting is

   use type Interfaces.Integer_64;

   subtype Integer_64 is Interfaces.Integer_64;

   NS_Per_Minute : constant := 60 * Nanoseconds_Per_Second;
   NS_Per_Hour   : constant := 60 * NS_Per_Minute;

   procedure Split_Date (Date      : Time;
                         Time_Zone : Time_Zones.Time_Offset;
                         Year      : out Year_Number;
                         Month     : out Month_Number;
                         Day       : out Day_Number;
                         N         : out Integer_64) is
   begin
      Split_Time (Date, Integer (Time_Zone), Year, Month, Day, N);
   end Split_Date;

   -----------------
   -- Day_of_Week --
   -----------------

   function Day_of_Week (Date : Time) return Day_Name is
      Y    : Year_Number;
      M    : Month_Number;
      D    : Day_Number;
      S    : Day_Duration;
      Days : Integer_64;
   begin
      Ada.Calendar.Split (Date, Y, M, D, S);
      Days := Days_Of (Y, M, D);  --  (2150-01-01 was a Thursday)
      return Day_Name'Val ((Days mod 7 + 3) mod 7);
   end Day_of_Week;

   ---------------------------------
   -- Year, Month, Day, Hour, ... --
   ---------------------------------

   function Year (Date : Time;
                  Time_Zone : Time_Zones.Time_Offset := 0) return Year_Number
   is
      Y : Year_Number;
      M : Month_Number;
      D : Day_Number;
      N : Integer_64;
   begin
      Split_Date (Date, Time_Zone, Y, M, D, N);
      return Y;
   end Year;

   function Month (Date : Time;
                   Time_Zone : Time_Zones.Time_Offset := 0) return Month_Number
   is
      Y : Year_Number;
      M : Month_Number;
      D : Day_Number;
      N : Integer_64;
   begin
      Split_Date (Date, Time_Zone, Y, M, D, N);
      return M;
   end Month;

   function Day (Date : Time;
                 Time_Zone : Time_Zones.Time_Offset := 0) return Day_Number
   is
      Y : Year_Number;
      M : Month_Number;
      D : Day_Number;
      N : Integer_64;
   begin
      Split_Date (Date, Time_Zone, Y, M, D, N);
      return D;
   end Day;

   function Hour (Date : Time;
                  Time_Zone : Time_Zones.Time_Offset := 0) return Hour_Number
   is
      Y : Year_Number;
      M : Month_Number;
      D : Day_Number;
      N : Integer_64;
   begin
      Split_Date (Date, Time_Zone, Y, M, D, N);
      return Hour_Number (N / NS_Per_Hour);
   end Hour;

   function Minute (Date : Time;
                    Time_Zone : Time_Zones.Time_Offset := 0)
     return Minute_Number
   is
      Y : Year_Number;
      M : Month_Number;
      D : Day_Number;
      N : Integer_64;
   begin
      Split_Date (Date, Time_Zone, Y, M, D, N);
      return Minute_Number (N / NS_Per_Minute mod 60);
   end Minute;

   function Second (Date : Time) return Second_Number is
      Y : Year_Number;
      M : Month_Number;
      D : Day_Number;
      N : Integer_64;
   begin
      Split_Date (Date, 0, Y, M, D, N);
      return Second_Number (N / Nanoseconds_Per_Second mod 60);
   end Second;

   function Sub_Second (Date : Time) return Second_Duration is
      Y : Year_Number;
      M : Month_Number;
      D : Day_Number;
      N : Integer_64;
   begin
      Split_Date (Date, 0, Y, M, D, N);
      return To_Duration (N mod Nanoseconds_Per_Second);
   end Sub_Second;

   ----------------
   -- Seconds_Of --
   ----------------

   function Seconds_Of (Hour       : Hour_Number;
                        Minute     : Minute_Number;
                        Second     : Second_Number := 0;
                        Sub_Second : Second_Duration := 0.0)
     return Day_Duration is
   begin
      return To_Duration
               (Integer_64 (Hour) * NS_Per_Hour
                  + Integer_64 (Minute) * NS_Per_Minute
                  + Integer_64 (Second) * Nanoseconds_Per_Second
                  + To_Nanoseconds (Sub_Second));
   end Seconds_Of;

   -----------
   -- Split --
   -----------

   procedure Split_Day (N          : Integer_64;
                        Hour       : out Hour_Number;
                        Minute     : out Minute_Number;
                        Second     : out Second_Number;
                        Sub_Second : out Second_Duration) is
   begin
      Hour := Hour_Number (N / NS_Per_Hour);
      Minute := Minute_Number (N / NS_Per_Minute mod 60);
      Second := Second_Number (N / Nanoseconds_Per_Second mod 60);
      Sub_Second := To_Duration (N mod Nanoseconds_Per_Second);
   end Split_Day;

   procedure Split (Seconds    : in Day_Duration;
                    Hour       : out Hour_Number;
                    Minute     : out Minute_Number;
                    Second     : out Second_Number;
                    Sub_Second : out Second_Duration) is
   begin
      if Seconds = 86_400.0 then
         raise Time_Error;
      end if;
      Split_Day (To_Nanoseconds (Seconds), Hour, Minute, Second, Sub_Second);
   end Split;

   procedure Split (Date       : in Time;
                    Year       : out Year_Number;
                    Month      : out Month_Number;
                    Day        : out Day_Number;
                    Hour       : out Hour_Number;
                    Minute     : out Minute_Number;
                    Second     : out Second_Number;
                    Sub_Second : out Second_Duration;
                    Time_Zone  : in Time_Zones.Time_Offset := 0)
   is
      N : Integer_64;
   begin
      Split_Date (Date, Time_Zone, Year, Month, Day, N);
      Split_Day (N, Hour, Minute, Second, Sub_Second);
   end Split;

   procedure Split (Date       : in Time;
                    Year       : out Year_Number;
                    Month      : out Month_Number;
                    Day        : out Day_Number;
                    Hour       : out Hour_Number;
                    Minute     : out Minute_Number;
                    Second     : out Second_Number;
                    Sub_Second : out Second_Duration;
                    Leap_Second: out Boolean;
                    Time_Zone  : in Time_Zones.Time_Offset := 0) is
   begin
      Split (Date, Year, Month, Day, Hour, Minute, Second, Sub_Second,
             Time_Zone);
      Leap_Second := False;
   end Split;

   procedure Split (Date       : in Time;
                    Year       : out Year_Number;
                    Month      : out Month_Number;
                    Day        : out Day_Number;
                    Seconds    : out Day_Duration;
                    Leap_Second: out Boolean;
                    Time_Zone  : in Time_Zones.Time_Offset := 0)
   is
      N : Integer_64;
   begin
      Split_Date (Date, Time_Zone, Year, Month, Day, N);
      Seconds := To_Duration (N);
      Leap_Second := False;
   end Split;

   -------------
   -- Time_Of --
   -------------

   function Time_Of (Year       : Year_Number;
                     Month      : Month_Number;
                     Day        : Day_Number;
                     Hour       : Hour_Number;
                     Minute     : Minute_Number;
                     Second     : Second_Number;
                     Sub_Second : Second_Duration := 0.0;
                     Leap_Second: Boolean := False;
                     Time_Zone  : Time_Zones.Time_Offset := 0)
     return Time is
   begin
      if Leap_Second then
         raise Time_Error;
      end if;
      return Make_Time
               (Year, Month, Day,
                Integer_64 (Hour) * NS_Per_Hour
                  + Integer_64 (Minute) * NS_Per_Minute
                  + Integer_64 (Second) * Nanoseconds_Per_Second
                  + To_Nanoseconds (Sub_Second),
                Integer (Time_Zone));
   end Time_Of;

   function Time_Of (Year       : Year_Number;
                     Month      : Month_Number;
                     Day        : Day_Number;
                     Seconds    : Day_Duration := 0.0;
                     Leap_Second: Boolean := False;
                     Time_Zone  : Time_Zones.Time_Offset := 0)
     return Time is
   begin
      if Leap_Second then
         raise Time_Error;
      end if;
      return Make_Time (Year, Month, Day, To_Nanoseconds (Seconds),
                        Integer (Time_Zone));
   end Time_Of;

   -----------
   -- Image --
   -----------

   --  N's decimal digits: at least Width (with leading zeros)
   function Digits_Of (N : Integer_64; Width : Positive) return String is
      Result : String (1 .. 20) := (others => '0');
      First  : Natural := Result'Last + 1;
      V      : Integer_64 := N;
   begin
      loop
         First := First - 1;
         Result (First) := Character'Val (Character'Pos ('0') + V mod 10);
         V := V / 10;
         exit when V = 0;
      end loop;
      First := Integer'Min (First, Result'Last - Width + 1);
      return Result (First .. Result'Last);
   end Digits_Of;

   --  "HH:MM:SS" (or more digits of hours) for N nanoseconds, and ".hh"
   function Clock_Image (N : Integer_64; Fraction : Boolean) return String is
      Main : constant String :=
        Digits_Of (N / NS_Per_Hour, 2) & ':'
        & Digits_Of (N / NS_Per_Minute mod 60, 2) & ':'
        & Digits_Of (N / Nanoseconds_Per_Second mod 60, 2);
   begin
      if Fraction then
         return Main & '.'
           & Digits_Of (N mod Nanoseconds_Per_Second / 10_000_000, 2);
      end if;
      return Main;
   end Clock_Image;

   function Image (Date : Time;
                   Include_Time_Fraction : Boolean := False;
                   Time_Zone  : Time_Zones.Time_Offset := 0) return String
   is
      Y : Year_Number;
      M : Month_Number;
      D : Day_Number;
      N : Integer_64;
   begin
      Split_Date (Date, Time_Zone, Y, M, D, N);
      return Digits_Of (Integer_64 (Y), 4) & '-'
        & Digits_Of (Integer_64 (M), 2) & '-'
        & Digits_Of (Integer_64 (D), 2) & ' '
        & Clock_Image (N, Include_Time_Fraction);
   end Image;

   function Image (Elapsed_Time : Duration;
                   Include_Time_Fraction : Boolean := False) return String is
   begin
      if Elapsed_Time < 0.0 then
         return '-' & Image (-Elapsed_Time, Include_Time_Fraction);
      end if;
      return Clock_Image (To_Nanoseconds (Elapsed_Time),
                          Include_Time_Fraction);
   end Image;

   -----------
   -- Value --
   -----------

   --  The number of the decimal digits S (First .. Last): Constraint_Error if
   --  there are none or they are not all digits
   function Number (S : String; First, Last : Integer) return Integer_64 is
      N : Integer_64 := 0;
   begin
      if First > Last or else First < S'First or else Last > S'Last then
         raise Constraint_Error;
      end if;
      for C of S (First .. Last) loop
         if C not in '0' .. '9' then
            raise Constraint_Error;
         end if;
         N := N * 10 + Integer_64 (Character'Pos (C) - Character'Pos ('0'));
      end loop;
      return N;
   end Number;

   --  The nanoseconds of the fraction S (First .. S'Last) (a point and two
   --  digits, as Image gives it), or 0 if First > S'Last
   function Fraction (S : String; First : Integer) return Integer_64 is
   begin
      if First > S'Last then
         return 0;
      elsif S (First) /= '.' or else S'Last /= First + 2 then
         raise Constraint_Error;
      end if;
      return Number (S, First + 1, First + 2) * 10_000_000;
   end Fraction;

   function Value (Date : String;
                   Time_Zone  : Time_Zones.Time_Offset := 0) return Time
   is
      F : constant Integer := Date'First;
      Y, M, D, H, Mi, S, N : Integer_64;
   begin
      if Date'Length < 19
        or else Date (F + 4) /= '-' or else Date (F + 7) /= '-'
        or else Date (F + 10) /= ' '
        or else Date (F + 13) /= ':' or else Date (F + 16) /= ':'
      then
         raise Constraint_Error;
      end if;
      Y := Number (Date, F, F + 3);
      M := Number (Date, F + 5, F + 6);
      D := Number (Date, F + 8, F + 9);
      H := Number (Date, F + 11, F + 12);
      Mi := Number (Date, F + 14, F + 15);
      S := Number (Date, F + 17, F + 18);
      N := Fraction (Date, F + 19);
      if Y not in 1901 .. 2399 or else M not in 1 .. 12
        or else D not in 1 .. 31 or else H > 23 or else Mi > 59
        or else S > 59
      then
         raise Constraint_Error;
      end if;
      return Make_Time
               (Integer (Y), Integer (M), Integer (D),
                H * NS_Per_Hour + Mi * NS_Per_Minute
                  + S * Nanoseconds_Per_Second + N,
                Integer (Time_Zone));
   exception
      when Time_Error =>
         raise Constraint_Error;
   end Value;

   function Value (Elapsed_Time : String) return Duration is
      F     : Integer := Elapsed_Time'First;
      Colon : Integer;
      H, Mi, S, N : Integer_64;
      Negative : Boolean := False;
   begin
      if F <= Elapsed_Time'Last and then Elapsed_Time (F) = '-' then
         Negative := True;
         F := F + 1;
      end if;
      Colon := F;
      while Colon <= Elapsed_Time'Last and then Elapsed_Time (Colon) /= ':'
      loop
         Colon := Colon + 1;
      end loop;
      if Colon - F < 2 or else Colon + 5 > Elapsed_Time'Last
        or else Elapsed_Time (Colon + 3) /= ':'
      then
         raise Constraint_Error;
      end if;
      H := Number (Elapsed_Time, F, Colon - 1);
      Mi := Number (Elapsed_Time, Colon + 1, Colon + 2);
      S := Number (Elapsed_Time, Colon + 4, Colon + 5);
      N := Fraction (Elapsed_Time, Colon + 6);
      if Mi > 59 or else S > 59 then
         raise Constraint_Error;
      end if;
      N := H * NS_Per_Hour + Mi * NS_Per_Minute
             + S * Nanoseconds_Per_Second + N;
      return To_Duration (if Negative then -N else N);
   end Value;

end Ada.Calendar.Formatting;
