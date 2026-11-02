--  Ada.Strings.Maps.adb
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

package body Ada.Strings.Maps is

   --  Sets

   function To_Set (Ranges : in Character_Ranges) return Character_Set is
      Result : Character_Set := Null_Set;
   begin
      for I in Ranges'Range loop
         for C in Ranges (I).Low .. Ranges (I).High loop
            Result.Bits (C) := True;
         end loop;
      end loop;
      return Result;
   end To_Set;

   function To_Set (Span : in Character_Range) return Character_Set is
      Result : Character_Set := Null_Set;
   begin
      for C in Span.Low .. Span.High loop
         Result.Bits (C) := True;
      end loop;
      return Result;
   end To_Set;

   function To_Ranges (Set : in Character_Set) return Character_Ranges is
      Result : Character_Ranges (1 .. 128);  --  (at most 128 ranges)
      Count  : Natural := 0;
      C      : Character := Character'First;
   begin
      loop
         if Set.Bits (C) then
            Count := Count + 1;
            Result (Count).Low := C;
            while C < Character'Last and then Set.Bits (Character'Succ (C))
            loop
               C := Character'Succ (C);
            end loop;
            Result (Count).High := C;
         end if;
         exit when C = Character'Last;
         C := Character'Succ (C);
      end loop;
      return Result (1 .. Count);
   end To_Ranges;

   function "=" (Left, Right : in Character_Set) return Boolean is
   begin
      return Left.Bits = Right.Bits;
   end "=";

   function "not" (Right : in Character_Set) return Character_Set is
   begin
      return (Bits => not Right.Bits);
   end "not";

   function "and" (Left, Right : in Character_Set) return Character_Set is
   begin
      return (Bits => Left.Bits and Right.Bits);
   end "and";

   function "or" (Left, Right : in Character_Set) return Character_Set is
   begin
      return (Bits => Left.Bits or Right.Bits);
   end "or";

   function "xor" (Left, Right : in Character_Set) return Character_Set is
   begin
      return (Bits => Left.Bits xor Right.Bits);
   end "xor";

   function "-" (Left, Right : in Character_Set) return Character_Set is
   begin
      return (Bits => Left.Bits and not Right.Bits);
   end "-";

   function Is_In (Element : in Character;
                   Set     : in Character_Set)
      return Boolean is
   begin
      return Set.Bits (Element);
   end Is_In;

   function Is_Subset (Elements : in Character_Set;
                       Set      : in Character_Set)
      return Boolean is
   begin
      return (Elements.Bits and not Set.Bits) = Null_Set.Bits;
   end Is_Subset;

   function To_Set (Sequence : in Character_Sequence) return Character_Set is
      Result : Character_Set := Null_Set;
   begin
      for I in Sequence'Range loop
         Result.Bits (Sequence (I)) := True;
      end loop;
      return Result;
   end To_Set;

   function To_Set (Singleton : in Character) return Character_Set is
      Result : Character_Set := Null_Set;
   begin
      Result.Bits (Singleton) := True;
      return Result;
   end To_Set;

   function To_Sequence (Set : in Character_Set) return Character_Sequence is
      Result : String (1 .. 256);
      Count  : Natural := 0;
   begin
      for C in Character loop
         if Set.Bits (C) then
            Count := Count + 1;
            Result (Count) := C;
         end if;
      end loop;
      return Result (1 .. Count);
   end To_Sequence;

   --  Mappings

   function Value (Map     : in Character_Mapping;
                   Element : in Character)
      return Character is
   begin
      return Map.Map (Element);
   end Value;

   function To_Mapping (From, To : in Character_Sequence)
      return Character_Mapping is
      Result : Character_Mapping := Identity;
      Seen   : Character_Set_Bits := (others => False);
   begin
      if From'Length /= To'Length then
         raise Translation_Error;
      end if;
      for I in From'Range loop
         if Seen (From (I)) then
            raise Translation_Error;
         end if;
         Seen (From (I)) := True;
         Result.Map (From (I)) := To (I - From'First + To'First);
      end loop;
      return Result;
   end To_Mapping;

   function To_Domain (Map : in Character_Mapping)
      return Character_Sequence is
      Result : String (1 .. 256);
      Count  : Natural := 0;
   begin
      for C in Character loop
         if Map.Map (C) /= C then
            Count := Count + 1;
            Result (Count) := C;
         end if;
      end loop;
      return Result (1 .. Count);
   end To_Domain;

   function To_Range (Map : in Character_Mapping)
      return Character_Sequence is
      Result : String (1 .. 256);
      Count  : Natural := 0;
   begin
      for C in Character loop
         if Map.Map (C) /= C then
            Count := Count + 1;
            Result (Count) := Map.Map (C);
         end if;
      end loop;
      return Result (1 .. Count);
   end To_Range;

end Ada.Strings.Maps;
