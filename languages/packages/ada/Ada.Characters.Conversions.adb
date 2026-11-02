--  Ada.Characters.Conversions.adb
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

package body Ada.Characters.Conversions is

   function Is_Character (Item : in Wide_Character) return Boolean is
   begin
      return Wide_Character'Pos (Item) <= Character'Pos (Character'Last);
   end Is_Character;

   function Is_String (Item : in Wide_String) return Boolean is
   begin
      for I in Item'Range loop
         if not Is_Character (Item (I)) then
            return False;
         end if;
      end loop;
      return True;
   end Is_String;

   function Is_Character (Item : in Wide_Wide_Character) return Boolean is
   begin
      return Wide_Wide_Character'Pos (Item) <= Character'Pos (Character'Last);
   end Is_Character;

   function Is_String (Item : in Wide_Wide_String) return Boolean is
   begin
      for I in Item'Range loop
         if not Is_Character (Item (I)) then
            return False;
         end if;
      end loop;
      return True;
   end Is_String;

   function Is_Wide_Character (Item : in Wide_Wide_Character)
      return Boolean is
   begin
      return Wide_Wide_Character'Pos (Item)
        <= Wide_Character'Pos (Wide_Character'Last);
   end Is_Wide_Character;

   function Is_Wide_String (Item : in Wide_Wide_String) return Boolean is
   begin
      for I in Item'Range loop
         if not Is_Wide_Character (Item (I)) then
            return False;
         end if;
      end loop;
      return True;
   end Is_Wide_String;

   function To_Wide_Character (Item : in Character) return Wide_Character is
   begin
      return Wide_Character'Val (Character'Pos (Item));
   end To_Wide_Character;

   function To_Wide_String (Item : in String) return Wide_String is
      Result : Wide_String (1 .. Item'Length);
   begin
      for I in Item'Range loop
         Result (I - Item'First + 1) := To_Wide_Character (Item (I));
      end loop;
      return Result;
   end To_Wide_String;

   function To_Wide_Wide_Character (Item : in Character)
      return Wide_Wide_Character is
   begin
      return Wide_Wide_Character'Val (Character'Pos (Item));
   end To_Wide_Wide_Character;

   function To_Wide_Wide_String (Item : in String)
      return Wide_Wide_String is
      Result : Wide_Wide_String (1 .. Item'Length);
   begin
      for I in Item'Range loop
         Result (I - Item'First + 1) := To_Wide_Wide_Character (Item (I));
      end loop;
      return Result;
   end To_Wide_Wide_String;

   function To_Wide_Wide_Character (Item : in Wide_Character)
      return Wide_Wide_Character is
   begin
      return Wide_Wide_Character'Val (Wide_Character'Pos (Item));
   end To_Wide_Wide_Character;

   function To_Wide_Wide_String (Item : in Wide_String)
      return Wide_Wide_String is
      Result : Wide_Wide_String (1 .. Item'Length);
   begin
      for I in Item'Range loop
         Result (I - Item'First + 1) := To_Wide_Wide_Character (Item (I));
      end loop;
      return Result;
   end To_Wide_Wide_String;

   function To_Character (Item       : in Wide_Character;
                          Substitute : in Character := ' ')
      return Character is
   begin
      if Is_Character (Item) then
         return Character'Val (Wide_Character'Pos (Item));
      else
         return Substitute;
      end if;
   end To_Character;

   function To_String    (Item       : in Wide_String;
                          Substitute : in Character := ' ')
      return String is
      Result : String (1 .. Item'Length);
   begin
      for I in Item'Range loop
         Result (I - Item'First + 1) := To_Character (Item (I), Substitute);
      end loop;
      return Result;
   end To_String;

   function To_Character (Item :       in Wide_Wide_Character;
                          Substitute : in Character := ' ')
      return Character is
   begin
      if Is_Character (Item) then
         return Character'Val (Wide_Wide_Character'Pos (Item));
      else
         return Substitute;
      end if;
   end To_Character;

   function To_String    (Item :       in Wide_Wide_String;
                          Substitute : in Character := ' ')
      return String is
      Result : String (1 .. Item'Length);
   begin
      for I in Item'Range loop
         Result (I - Item'First + 1) := To_Character (Item (I), Substitute);
      end loop;
      return Result;
   end To_String;

   function To_Wide_Character (Item :       in Wide_Wide_Character;
                               Substitute : in Wide_Character := ' ')
      return Wide_Character is
   begin
      if Is_Wide_Character (Item) then
         return Wide_Character'Val (Wide_Wide_Character'Pos (Item));
      else
         return Substitute;
      end if;
   end To_Wide_Character;

   function To_Wide_String    (Item :       in Wide_Wide_String;
                               Substitute : in Wide_Character := ' ')
      return Wide_String is
      Result : Wide_String (1 .. Item'Length);
   begin
      for I in Item'Range loop
         Result (I - Item'First + 1) :=
           To_Wide_Character (Item (I), Substitute);
      end loop;
      return Result;
   end To_Wide_String;

end Ada.Characters.Conversions;
