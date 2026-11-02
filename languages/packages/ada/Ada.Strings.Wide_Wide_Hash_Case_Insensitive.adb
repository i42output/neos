--  Ada.Strings.Wide_Wide_Hash_Case_Insensitive.adb
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

with Ada.Wide_Wide_Characters.Handling;

function Ada.Strings.Wide_Wide_Hash_Case_Insensitive (Key : Wide_Wide_String)
   return Containers.Hash_Type is
   --  FNV-1a over the positions of the characters' simple case foldings
   use type Containers.Hash_Type;
   function Fold (C : Wide_Wide_Character) return Wide_Wide_Character is
      --  The simple case folding of C: its lower case form after its upper
      --  case form, except for the few characters where that differs (the
      --  folding of these is their own, or a character equivalent to them)
   begin
      case Wide_Wide_Character'Pos (C) is
         when 16#0130# | 16#0131# =>
            return C;
         when 16#1FD3# =>
            return Wide_Wide_Character'Val (16#0390#);
         when 16#1FE3# =>
            return Wide_Wide_Character'Val (16#03B0#);
         when 16#FB05# =>
            return Wide_Wide_Character'Val (16#FB06#);
         when others =>
            return Ada.Wide_Wide_Characters.Handling.To_Lower (Ada.Wide_Wide_Characters.Handling.To_Upper (C));
      end case;
   end Fold;

   H : Containers.Hash_Type := 2166136261;
begin
   for I in Key'Range loop
      H := (H xor Wide_Wide_Character'Pos (Fold (Key (I)))) * 16777619;
   end loop;
   return H;
end Ada.Strings.Wide_Wide_Hash_Case_Insensitive;
