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

--  neos's dispatching does not depend on deadlines (its scheduler dispatches
--  by priority: see the Ada runtime's tasking): a task's deadlines are values
--  that this package keeps. The release time that Get_Last_Release_Time gives is the time at
--  which the task was last released by Delay_Until_And_Set_Deadline (other
--  releases are not recorded; Ada.Real_Time.Time_First if there has been
--  none).

with Ada.Task_Attributes;

package body Ada.Dispatching.EDF is

   package Deadlines is new Ada.Task_Attributes (Deadline, Default_Deadline);
   package Relative_Deadlines is
     new Ada.Task_Attributes (Relative_Deadline, Default_Relative_Deadline);
   package Release_Times is
     new Ada.Task_Attributes (Ada.Real_Time.Time, Ada.Real_Time.Time_First);

   procedure Set_Deadline
      (D : in Deadline;
       T : in Ada.Task_Identification.Task_Id :=
       Ada.Task_Identification.Current_Task) is
   begin
      Deadlines.Set_Value (D, T);
   end Set_Deadline;

   function Get_Deadline
      (T : Ada.Task_Identification.Task_Id :=
       Ada.Task_Identification.Current_Task) return Deadline is
   begin
      return Deadlines.Value (T);
   end Get_Deadline;

   procedure Set_Relative_Deadline
      (D : in Relative_Deadline;
       T : in Ada.Task_Identification.Task_Id :=
       Ada.Task_Identification.Current_Task) is
   begin
      Relative_Deadlines.Set_Value (D, T);
   end Set_Relative_Deadline;

   function Get_Relative_Deadline
      (T : Ada.Task_Identification.Task_Id :=
       Ada.Task_Identification.Current_Task)
       return Relative_Deadline is
   begin
      return Relative_Deadlines.Value (T);
   end Get_Relative_Deadline;

   procedure Delay_Until_And_Set_Deadline
      (Delay_Until_Time : in Ada.Real_Time.Time;
       Deadline_Offset : in Ada.Real_Time.Time_Span) is
      use type Ada.Real_Time.Time;
   begin
      delay until Delay_Until_Time;
      Set_Deadline (Delay_Until_Time + Deadline_Offset);
      Release_Times.Set_Value (Ada.Real_Time.Clock);
   end Delay_Until_And_Set_Deadline;

   function Get_Last_Release_Time
      (T : Ada.Task_Identification.Task_Id :=
       Ada.Task_Identification.Current_Task)
       return Ada.Real_Time.Time is
   begin
      return Release_Times.Value (T);
   end Get_Last_Release_Time;

end Ada.Dispatching.EDF;
