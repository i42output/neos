--  Ada.Execution_Time.adb
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

with Interfaces;

package body Ada.Execution_Time is

   use type Ada.Task_Identification.Task_Id;
   use type Interfaces.Integer_64;

   subtype Integer_64 is Interfaces.Integer_64;

   function Thread_CPU_Time return Integer_64;
   pragma Import (Neos, Thread_CPU_Time, "ada::cpu_time");

   Billion : constant := 1_000_000_000;

   --  (a time span as a number of nanoseconds, and back)
   function Nanoseconds_Of (TS : Time_Span) return Integer_64 is
      Nanosecond : constant Duration := 1.0E-9;
   begin
      return Integer_64 (To_Duration (TS) / Nanosecond);
   end Nanoseconds_Of;

   function To_Time_Span (N : Integer_64) return Time_Span is
   begin
      return Seconds (Integer (N / Billion)) + Nanoseconds (Integer (N rem Billion));
   end To_Time_Span;

   function Clock
     (T : Ada.Task_Identification.Task_Id
          := Ada.Task_Identification.Current_Task)
     return CPU_Time is
   begin
      if T = Ada.Task_Identification.Null_Task_Id then
         raise Program_Error;
      end if;
      if Ada.Task_Identification.Is_Terminated (T) then
         raise Tasking_Error;
      end if;
      return CPU_Time (Thread_CPU_Time);
   end Clock;

   function "+" (Left : CPU_Time; Right : Time_Span) return CPU_Time is
   begin
      return CPU_Time (Integer_64 (Left) + Nanoseconds_Of (Right));
   end "+";

   function "+" (Left : Time_Span; Right : CPU_Time) return CPU_Time is
   begin
      return Right + Left;
   end "+";

   function "-" (Left : CPU_Time; Right : Time_Span) return CPU_Time is
   begin
      return CPU_Time (Integer_64 (Left) - Nanoseconds_Of (Right));
   end "-";

   function "-" (Left : CPU_Time; Right : CPU_Time) return Time_Span is
   begin
      return To_Time_Span (Integer_64 (Left) - Integer_64 (Right));
   end "-";

   function "<" (Left, Right : CPU_Time) return Boolean is
   begin
      return Integer_64 (Left) < Integer_64 (Right);
   end "<";

   function "<=" (Left, Right : CPU_Time) return Boolean is
   begin
      return Integer_64 (Left) <= Integer_64 (Right);
   end "<=";

   function ">" (Left, Right : CPU_Time) return Boolean is
   begin
      return Integer_64 (Left) > Integer_64 (Right);
   end ">";

   function ">=" (Left, Right : CPU_Time) return Boolean is
   begin
      return Integer_64 (Left) >= Integer_64 (Right);
   end ">=";

   procedure Split
     (T : in CPU_Time; SC : out Seconds_Count; TS : out Time_Span) is
   begin
      SC := Seconds_Count (Integer_64 (T) / Billion);
      TS := Nanoseconds (Integer (Integer_64 (T) rem Billion));
   end Split;

   function Time_Of (SC : Seconds_Count;
                     TS : Time_Span := Time_Span_Zero) return CPU_Time is
   begin
      return CPU_Time (Integer_64 (SC) * Billion + Nanoseconds_Of (TS));
   end Time_Of;

   function Clock_For_Interrupts return CPU_Time is
   begin
      raise Program_Error;   --  (Interrupt_Clocks_Supported is False)
      return Clock_For_Interrupts;
   end Clock_For_Interrupts;

end Ada.Execution_Time;
