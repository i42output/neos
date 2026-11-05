--  System.Multiprocessors.Dispatching_Domains.adb
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

package body System.Multiprocessors.Dispatching_Domains is

   use type Ada.Task_Identification.Task_Id;

   --  (the processor that a task is assigned to: the runtime's (its CPU
   --  aspect, D.16(14/5)))
   procedure Assign (T : Ada.Task_Identification.Task_Id; CPU : CPU_Range);
   pragma Import (Neos, Assign, "ada::task_cpu_set");
   function Assigned (T : Ada.Task_Identification.Task_Id) return CPU_Range;
   pragma Import (Neos, Assigned, "ada::task_cpu");

   Empty_Domain : constant Dispatching_Domain :=
     (First => CPU'First, Last => CPU_Range'First);

   procedure Check_Creator is
   begin
      if Ada.Task_Identification.Current_Task /=
         Ada.Task_Identification.Environment_Task
      then
         raise Dispatching_Domain_Error;
      end if;
   end Check_Creator;

   --  (CPU is Not_A_Specific_CPU or in Domain)
   function Is_Valid (Domain : Dispatching_Domain; CPU : CPU_Range) return Boolean is
   begin
      return CPU = Not_A_Specific_CPU or else CPU in Domain.First .. Domain.Last;
   end Is_Valid;

   function Create (First : CPU; Last : CPU_Range) return Dispatching_Domain is
   begin
      Check_Creator;
      if Last < First then
         return Empty_Domain;
      end if;
      --  (the processors First .. Last include one that does not exist, or the
      --  only one, which the system dispatching domain cannot give up)
      raise Dispatching_Domain_Error;
   end Create;

   function Get_First_CPU (Domain : Dispatching_Domain) return CPU is
   begin
      return Domain.First;
   end Get_First_CPU;

   function Get_Last_CPU (Domain : Dispatching_Domain) return CPU_Range is
   begin
      return Domain.Last;
   end Get_Last_CPU;

   function Create (Set : CPU_Set) return Dispatching_Domain is
   begin
      Check_Creator;
      for I in Set'Range loop
         if Set (I) then
            raise Dispatching_Domain_Error;
         end if;
      end loop;
      return Empty_Domain;
   end Create;

   function Get_CPU_Set (Domain : Dispatching_Domain) return CPU_Set is
      Result : constant CPU_Set (Domain.First .. Domain.Last) := (others => True);
   begin
      return Result;
   end Get_CPU_Set;

   function Get_Dispatching_Domain
      (T   : Ada.Task_Identification.Task_Id :=
                 Ada.Task_Identification.Current_Task)
           return Dispatching_Domain is
   begin
      if T = Ada.Task_Identification.Null_Task_Id then
         raise Program_Error;
      end if;
      return System_Dispatching_Domain;
   end Get_Dispatching_Domain;

   procedure Assign_Task
      (Domain : in out Dispatching_Domain;
       CPU    : in     CPU_Range := Not_A_Specific_CPU;
       T      : in     Ada.Task_Identification.Task_Id :=
                 Ada.Task_Identification.Current_Task) is
   begin
      if Domain.Last < Domain.First or else not Is_Valid (Domain, CPU) then
         raise Dispatching_Domain_Error;
      end if;
      Assign (T, CPU);
   end Assign_Task;

   procedure Set_CPU
      (CPU : in CPU_Range;
       T   : in Ada.Task_Identification.Task_Id :=
                 Ada.Task_Identification.Current_Task) is
   begin
      if not Is_Valid (System_Dispatching_Domain, CPU) then
         raise Dispatching_Domain_Error;
      end if;
      Assign (T, CPU);
   end Set_CPU;

   function Get_CPU
      (T   : Ada.Task_Identification.Task_Id :=
                 Ada.Task_Identification.Current_Task)
           return CPU_Range is
   begin
      if T = Ada.Task_Identification.Null_Task_Id then
         raise Program_Error;
      end if;
      return Assigned (T);
   end Get_CPU;

   procedure Delay_Until_And_Set_CPU
      (Delay_Until_Time : in Ada.Real_Time.Time; CPU : in CPU_Range) is
   begin
      if not Is_Valid (System_Dispatching_Domain, CPU) then
         raise Dispatching_Domain_Error;
      end if;
      delay until Delay_Until_Time;
      Set_CPU (CPU);
   end Delay_Until_And_Set_CPU;

end System.Multiprocessors.Dispatching_Domains;
