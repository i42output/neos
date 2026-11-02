--  Ada.Execution_Time.Timers.adb
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

package body Ada.Execution_Time.Timers is

   use type Ada.Task_Identification.Task_Id;

   procedure Check (TM : Timer) is
   begin
      if TM.T.all = Ada.Task_Identification.Null_Task_Id then
         raise Program_Error;
      end if;
      if Ada.Task_Identification.Is_Terminated (TM.T.all) then
         raise Tasking_Error;
      end if;
   end Check;

   procedure Set_Handler (TM      : in out Timer;
                          In_Time : in Time_Span;
                          Handler : in Timer_Handler) is
   begin
      Check (TM);
      if Handler /= null then
         raise Timer_Resource_Error;
      end if;
   end Set_Handler;

   procedure Set_Handler (TM      : in out Timer;
                          At_Time : in CPU_Time;
                          Handler : in Timer_Handler) is
   begin
      Check (TM);
      if Handler /= null then
         raise Timer_Resource_Error;
      end if;
   end Set_Handler;

   function Current_Handler (TM : Timer) return Timer_Handler is
   begin
      Check (TM);
      return null;
   end Current_Handler;

   procedure Cancel_Handler (TM        : in out Timer;
                             Cancelled :    out Boolean) is
   begin
      Check (TM);
      Cancelled := False;
   end Cancel_Handler;

   function Time_Remaining (TM : Timer) return Time_Span is
   begin
      Check (TM);
      return Time_Span_Zero;
   end Time_Remaining;

end Ada.Execution_Time.Timers;
