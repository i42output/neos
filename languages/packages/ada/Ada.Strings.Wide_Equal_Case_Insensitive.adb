--  Ada.Strings.Wide_Equal_Case_Insensitive.adb
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

with Ada.Wide_Characters.Handling;

function Ada.Strings.Wide_Equal_Case_Insensitive (Left, Right : Wide_String)
   return Boolean is
   function Fold (C : Wide_Character) return Wide_Character is
      --  The simple case folding of C: its lower case form after its upper
      --  case form, except for the few characters where that differs (the
      --  folding of these is their own, or a character equivalent to them)
   begin
      case Wide_Character'Pos (C) is
         when 16#0130# | 16#0131# =>
            return C;
         when 16#1FD3# =>
            return Wide_Character'Val (16#0390#);
         when 16#1FE3# =>
            return Wide_Character'Val (16#03B0#);
         when 16#FB05# =>
            return Wide_Character'Val (16#FB06#);
         when others =>
            return Ada.Wide_Characters.Handling.To_Lower (Ada.Wide_Characters.Handling.To_Upper (C));
      end case;
   end Fold;

begin
   if Left'Length /= Right'Length then
      return False;
   end if;
   for I in 0 .. Left'Length - 1 loop
      if Fold (Left (Left'First + I)) /= Fold (Right (Right'First + I)) then
         return False;
      end if;
   end loop;
   return True;
end Ada.Strings.Wide_Equal_Case_Insensitive;
