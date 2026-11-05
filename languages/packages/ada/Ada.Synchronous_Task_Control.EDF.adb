--  Ada.Synchronous_Task_Control.EDF.adb
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

package body Ada.Synchronous_Task_Control.EDF is

   --  (the deadline when the task is next made ready (released): the clock
   --  then plus an offset; then (it was not blocked) the clock now plus it)
   procedure Release_Offset (TS : Ada.Real_Time.Time_Span);
   pragma Import (Neos, Release_Offset, "ada::release_offset");
   procedure Release_Deadline_End;
   pragma Import (Neos, Release_Deadline_End, "ada::release_deadline_end");

   procedure Suspend_Until_True_And_Set_Deadline
      (S  : in out Suspension_Object;
       TS : in     Ada.Real_Time.Time_Span) is
   begin
      Release_Offset (TS);
      Suspend_Until_True (S);
      Release_Deadline_End;
   end Suspend_Until_True_And_Set_Deadline;

end Ada.Synchronous_Task_Control.EDF;
