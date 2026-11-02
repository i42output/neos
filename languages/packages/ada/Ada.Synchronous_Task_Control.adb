--  Ada.Synchronous_Task_Control.adb
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

package body Ada.Synchronous_Task_Control is

   protected body Suspension_Object is

      procedure Set_True is
      begin
         State := True;
      end Set_True;

      procedure Set_False is
      begin
         State := False;
      end Set_False;

      function Current_State return Boolean is
      begin
         return State;
      end Current_State;

      function Is_Waited_On return Boolean is
      begin
         return Wait'Count > 0;
      end Is_Waited_On;

      entry Wait when State is
      begin
         State := False;
      end Wait;

   end Suspension_Object;

   procedure Set_True (S : in out Suspension_Object) is
   begin
      S.Set_True;
   end Set_True;

   procedure Set_False (S : in out Suspension_Object) is
   begin
      S.Set_False;
   end Set_False;

   function Current_State (S : Suspension_Object) return Boolean is
   begin
      return S.Current_State;
   end Current_State;

   procedure Suspend_Until_True (S : in out Suspension_Object) is
   begin
      if S.Is_Waited_On then
         raise Program_Error;
      end if;
      S.Wait;
   end Suspend_Until_True;

end Ada.Synchronous_Task_Control;
