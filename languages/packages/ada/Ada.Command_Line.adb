--  Ada.Command_Line.adb
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

--  The arguments are the ones that the host gives the program (see the
--  native primitives argument_count and argument in lib_os.cpp): each is one
--  of the strings that the host was given after the program's name (the
--  command name). Set_Exit_Status sets the status that the program returns
--  when its main subprogram completes normally (the runtime's set_status).

with Interfaces;
with System;

package body Ada.Command_Line is

   use type Interfaces.Integer_64;

   function Count_Of_Arguments return Interfaces.Integer_64;
   pragma Import (Neos, Count_Of_Arguments, "ada::argument_count");

   function Argument_Of (Number   : Interfaces.Integer_64;
                         Buffer   : System.Address;
                         Capacity : Interfaces.Integer_64)
     return Interfaces.Integer_64;
   pragma Import (Neos, Argument_Of, "ada::argument");

   procedure Set_Status (Status : Interfaces.Integer_64);
   pragma Import (Neos, Set_Status, "ada::set_status");

   --  Argument Number (0: the command name); Constraint_Error if there is
   --  no such argument
   function Get (Number : Natural) return String is
      Length : constant Interfaces.Integer_64 :=
        Argument_Of (Interfaces.Integer_64 (Number), System.Null_Address, 0);
   begin
      if Length < 0 then
         raise Constraint_Error;
      elsif Length = 0 then
         return "";
      end if;
      declare
         Result : String (1 .. Natural (Length));
         Got    : constant Interfaces.Integer_64 :=
           Argument_Of (Interfaces.Integer_64 (Number), Result'Address, Length);
      begin
         if Got /= Length then
            raise Constraint_Error;
         end if;
         return Result;
      end;
   end Get;

   function Argument_Count return Natural is
   begin
      return Natural (Count_Of_Arguments);
   end Argument_Count;

   function Argument (Number : in Positive) return String is
   begin
      if Number > Argument_Count then
         raise Constraint_Error;
      end if;
      return Get (Number);
   end Argument;

   function Command_Name return String is
   begin
      return Get (0);
   exception
      when Constraint_Error =>
         return "";
   end Command_Name;

   procedure Set_Exit_Status (Code : in Exit_Status) is
   begin
      Set_Status (Interfaces.Integer_64 (Code));
   end Set_Exit_Status;

end Ada.Command_Line;
