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

--  The interrupts are the process's signals that can be handled (SIGINT and
--  SIGTERM, and on POSIX systems SIGHUP, SIGQUIT, SIGUSR1, SIGUSR2, SIGPIPE
--  and SIGALRM): the others are reserved. Their handlers are the runtime's:
--  an interrupt that occurs is handled (its handler, a protected procedure,
--  called) when the running task next polls, outside protected actions (see
--  the Ada runtime's tasking). A handler is an access-to-protected-procedure
--  value: the address of its descriptor (its procedure, static link and
--  object), which the runtime keeps. Reference raises Program_Error
--  (attaching task entries to interrupts is not supported, J.7.1).

with Ada.Unchecked_Conversion;

package body Ada.Interrupts is

   type Address_Value is range -2 ** 63 .. 2 ** 63 - 1;
   function To_Value is new Ada.Unchecked_Conversion (Parameterless_Handler, Address_Value);
   function To_Handler is new Ada.Unchecked_Conversion (Address_Value, Parameterless_Handler);

   function Reserved (Interrupt : Interrupt_Id) return Integer;
   pragma Import (Neos, Reserved, "ada::interrupt_reserved");
   --  (the handler replaced (Program_Error: reserved, attached statically, or
   --  not an interrupt handler (C.3.2(15-17/3))))
   function Attach (Interrupt : Interrupt_Id; Handler : Address_Value) return Address_Value;
   pragma Import (Neos, Attach, "ada::interrupt_attach");
   function Handler (Interrupt : Interrupt_Id) return Address_Value;
   pragma Import (Neos, Handler, "ada::interrupt_handler");

   function Is_Reserved (Interrupt : Interrupt_Id)
      return Boolean is
   begin
      return Reserved (Interrupt) /= 0;
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
      return Handler (Interrupt) /= 0;
   end Is_Attached;

   function Current_Handler (Interrupt : Interrupt_Id)
      return Parameterless_Handler is
   begin
      Check (Interrupt);
      return To_Handler (Handler (Interrupt));
   end Current_Handler;

   procedure Attach_Handler
      (New_Handler : in Parameterless_Handler;
       Interrupt   : in Interrupt_Id) is
      Old : Address_Value;
   begin
      Old := Attach (Interrupt, To_Value (New_Handler));
   end Attach_Handler;

   procedure Exchange_Handler
      (Old_Handler : out Parameterless_Handler;
       New_Handler : in Parameterless_Handler;
       Interrupt   : in Interrupt_Id) is
   begin
      Old_Handler := To_Handler (Attach (Interrupt, To_Value (New_Handler)));
   end Exchange_Handler;

   procedure Detach_Handler
      (Interrupt : in Interrupt_Id) is
      Old : Address_Value;
   begin
      Old := Attach (Interrupt, 0);
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
