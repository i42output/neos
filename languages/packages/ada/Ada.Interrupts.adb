--  Ada.Interrupts.adb
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

--  A neos program receives no interrupts: every interrupt is reserved (no
--  handler can be attached to it), so each operation other than Is_Reserved
--  raises Program_Error (C.3.2(20)), and so does Reference (attaching task
--  entries to interrupts is not supported).

package body Ada.Interrupts is

   function Is_Reserved (Interrupt : Interrupt_Id)
      return Boolean is
   begin
      return True;
   end Is_Reserved;

   procedure Check (Interrupt : Interrupt_Id) is
   begin
      if Is_Reserved (Interrupt) then
         raise Program_Error;
      end if;
   end Check;

   function Is_Attached (Interrupt : Interrupt_Id)
      return Boolean is
   begin
      Check (Interrupt);
      return False;
   end Is_Attached;

   function Current_Handler (Interrupt : Interrupt_Id)
      return Parameterless_Handler is
   begin
      Check (Interrupt);
      return null;
   end Current_Handler;

   procedure Attach_Handler
      (New_Handler : in Parameterless_Handler;
       Interrupt   : in Interrupt_Id) is
   begin
      Check (Interrupt);
      if New_Handler /= null then
         raise Program_Error;
      end if;
   end Attach_Handler;

   procedure Exchange_Handler
      (Old_Handler : out Parameterless_Handler;
       New_Handler : in Parameterless_Handler;
       Interrupt   : in Interrupt_Id) is
   begin
      Check (Interrupt);
      Old_Handler := null;
      Attach_Handler (New_Handler, Interrupt);
   end Exchange_Handler;

   procedure Detach_Handler
      (Interrupt : in Interrupt_Id) is
   begin
      Check (Interrupt);
   end Detach_Handler;

   function Reference (Interrupt : Interrupt_Id)
      return System.Address is
   begin
      raise Program_Error;
      return Reference (Interrupt);
   end Reference;

   function Get_CPU (Interrupt : Interrupt_Id)
      return System.Multiprocessors.CPU_Range is
   begin
      Check (Interrupt);
      return 1;
   end Get_CPU;

end Ada.Interrupts;
