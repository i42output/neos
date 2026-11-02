--  Interfaces.Fortran.adb
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

package body Interfaces.Fortran is

   function To_Fortran (Item : in Character) return Character_Set is
   begin
      return Character_Set'Val (Character'Pos (Item));
   end To_Fortran;

   function To_Ada (Item : in Character_Set) return Character is
   begin
      return Character'Val (Character_Set'Pos (Item));
   end To_Ada;

   function To_Fortran (Item : in String) return Fortran_Character is
      Result : Fortran_Character (1 .. Item'Length);
   begin
      for J in Result'Range loop
         Result (J) := To_Fortran (Item (Item'First + J - 1));
      end loop;
      return Result;
   end To_Fortran;

   function To_Ada (Item : in Fortran_Character) return String is
      Result : String (1 .. Item'Length);
   begin
      for J in Result'Range loop
         Result (J) := To_Ada (Item (Item'First + J - 1));
      end loop;
      return Result;
   end To_Ada;

   procedure To_Fortran (Item       : in String;
                         Target     : out Fortran_Character;
                         Last       : out Natural) is
   begin
      if Item'Length > Target'Length then
         raise Constraint_Error;
      end if;
      for J in 0 .. Item'Length - 1 loop
         Target (Target'First + J) := To_Fortran (Item (Item'First + J));
      end loop;
      if Item'Length = 0 then
         Last := 0;
      else
         Last := Target'First + Item'Length - 1;
      end if;
   end To_Fortran;

   procedure To_Ada (Item     : in Fortran_Character;
                     Target   : out String;
                     Last     : out Natural) is
   begin
      if Item'Length > Target'Length then
         raise Constraint_Error;
      end if;
      for J in 0 .. Item'Length - 1 loop
         Target (Target'First + J) := To_Ada (Item (Item'First + J));
      end loop;
      if Item'Length = 0 then
         Last := 0;
      else
         Last := Target'First + Item'Length - 1;
      end if;
   end To_Ada;

end Interfaces.Fortran;
