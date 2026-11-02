--  Ada.Dispatching.Round_Robin.adb
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

--  neos does not support the Round_Robin_Within_Priorities policy: its tasks
--  run until they block or yield, at every priority (the predefined policy
--  that applies is FIFO_Within_Priorities' order without preemption).

package body Ada.Dispatching.Round_Robin is

   procedure Set_Quantum (Pri     : in System.Priority;
                          Quantum : in Ada.Real_Time.Time_Span) is
   begin
      raise Dispatching_Policy_Error;
   end Set_Quantum;

   procedure Set_Quantum (Low, High : in System.Priority;
                          Quantum   : in Ada.Real_Time.Time_Span) is
   begin
      raise Dispatching_Policy_Error;
   end Set_Quantum;

   function Actual_Quantum (Pri : System.Priority)
              return Ada.Real_Time.Time_Span is
   begin
      raise Dispatching_Policy_Error;
      return Default_Quantum;
   end Actual_Quantum;

   function Is_Round_Robin (Pri : System.Priority) return Boolean is
   begin
      return False;
   end Is_Round_Robin;

end Ada.Dispatching.Round_Robin;
