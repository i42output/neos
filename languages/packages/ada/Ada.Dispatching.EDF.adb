--  Ada.Dispatching.EDF.adb
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

--  The deadlines are the runtime's: its scheduler orders the ready queues of
--  the priorities whose policy is EDF_Within_Priorities by the tasks' active
--  deadlines (D.2.6). A task is made ready when its scheduler finds it ready
--  (its release time: then, or the time its delay expired).

package body Ada.Dispatching.EDF is

   use Ada.Task_Identification;

   procedure Set (T : Task_Id; D : Deadline);
   pragma Import (Neos, Set, "ada::deadline_set");
   function Get (T : Task_Id) return Deadline;
   pragma Import (Neos, Get, "ada::deadline");
   procedure Set_Relative (T : Task_Id; D : Relative_Deadline);
   pragma Import (Neos, Set_Relative, "ada::relative_deadline_set");
   function Get_Relative (T : Task_Id) return Relative_Deadline;
   pragma Import (Neos, Get_Relative, "ada::relative_deadline");
   function Release_Time (T : Task_Id) return Ada.Real_Time.Time;
   pragma Import (Neos, Release_Time, "ada::release_time");
   procedure Release_Deadline (D : Deadline);
   pragma Import (Neos, Release_Deadline, "ada::release_deadline");
   procedure Release_Deadline_End;
   pragma Import (Neos, Release_Deadline_End, "ada::release_deadline_end");

   --  (Program_Error for Null_Task_Id, Tasking_Error for a terminated task
   --  (D.2.6(29/2)))
   procedure Check (T : Task_Id) is
   begin
      if T = Null_Task_Id then
         raise Program_Error;
      end if;
      if Is_Terminated (T) then
         raise Tasking_Error;
      end if;
   end Check;

   procedure Set_Deadline
      (D : in Deadline;
       T : in Ada.Task_Identification.Task_Id :=
       Ada.Task_Identification.Current_Task) is
   begin
      Check (T);
      Set (T, D);
   end Set_Deadline;

   function Get_Deadline
      (T : Ada.Task_Identification.Task_Id :=
       Ada.Task_Identification.Current_Task) return Deadline is
   begin
      Check (T);
      return Get (T);
   end Get_Deadline;

   procedure Set_Relative_Deadline
      (D : in Relative_Deadline;
       T : in Ada.Task_Identification.Task_Id :=
       Ada.Task_Identification.Current_Task) is
   begin
      Check (T);
      Set_Relative (T, D);
   end Set_Relative_Deadline;

   function Get_Relative_Deadline
      (T : Ada.Task_Identification.Task_Id :=
       Ada.Task_Identification.Current_Task)
       return Relative_Deadline is
   begin
      Check (T);
      return Get_Relative (T);
   end Get_Relative_Deadline;

   procedure Delay_Until_And_Set_Deadline
      (Delay_Until_Time : in Ada.Real_Time.Time;
       Deadline_Offset : in Ada.Real_Time.Time_Span) is
      use type Ada.Real_Time.Time;
   begin
      Release_Deadline (Delay_Until_Time + Deadline_Offset);
      delay until Delay_Until_Time;
      Release_Deadline_End;
   end Delay_Until_And_Set_Deadline;

   function Get_Last_Release_Time
      (T : Ada.Task_Identification.Task_Id :=
       Ada.Task_Identification.Current_Task)
       return Ada.Real_Time.Time is
   begin
      Check (T);
      return Release_Time (T);
   end Get_Last_Release_Time;

end Ada.Dispatching.EDF;
