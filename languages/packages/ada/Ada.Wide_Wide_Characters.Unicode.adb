--  Ada.Wide_Wide_Characters.Unicode.adb
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

package body Ada.Wide_Wide_Characters.Unicode is

   function Category_Of (C : Code_Point) return Category is
      Low  : Integer := Categories'First;
      High : Integer := Categories'Last;
      Mid  : Integer;
   begin
      while Low <= High loop
         Mid := (Low + High) / 2;
         if C < Categories (Mid).First then
            High := Mid - 1;
         elsif C > Categories (Mid).Last then
            Low := Mid + 1;
         else
            case Categories (Mid).Class is
               when Upper_Even =>
                  if C mod 2 = 0 then
                     return Upper;
                  else
                     return Lower;
                  end if;
               when Upper_Odd =>
                  if C mod 2 = 1 then
                     return Upper;
                  else
                     return Lower;
                  end if;
               when others =>
                  return Categories (Mid).Class;
            end case;
         end if;
      end loop;
      return Other_Graphic;
   end Category_Of;

   function Find (Runs : Mapping_Runs; C : Code_Point) return Natural is
      --  the index of the run that maps C, or 0
      Low  : Integer := Runs'First;
      High : Integer := Runs'Last;
      Mid  : Integer;
   begin
      while Low <= High loop
         Mid := (Low + High) / 2;
         if C < Runs (Mid).First then
            High := Mid - 1;
         elsif C > Runs (Mid).Last then
            Low := Mid + 1;
         elsif Runs (Mid).Stride <= 1
           or else (C - Runs (Mid).First) mod Code_Point (Runs (Mid).Stride) = 0
         then
            return Mid;
         else
            return 0;
         end if;
      end loop;
      return 0;
   end Find;

   function Map (Runs : Mapping_Runs; C : Code_Point) return Code_Point is
      I : constant Natural := Find (Runs, C);
   begin
      if I = 0 then
         return C;
      elsif Runs (I).Stride = 0 then
         return Code_Point (Runs (I).Value);
      else
         return Code_Point (Code_Point'Base (C) + Code_Point'Base (Runs (I).Value));
      end if;
   end Map;

   function In_Runs (Runs : Mapping_Runs; C : Code_Point) return Boolean is
   begin
      return Find (Runs, C) /= 0;
   end In_Runs;

   function Is_NFKC (C : Code_Point) return Boolean is
      Low  : Integer := Not_NFKC'First;
      High : Integer := Not_NFKC'Last;
      Mid  : Integer;
   begin
      while Low <= High loop
         Mid := (Low + High) / 2;
         if C < Not_NFKC (Mid).First then
            High := Mid - 1;
         elsif C > Not_NFKC (Mid).Last then
            Low := Mid + 1;
         else
            return False;
         end if;
      end loop;
      return True;
   end Is_NFKC;

end Ada.Wide_Wide_Characters.Unicode;
