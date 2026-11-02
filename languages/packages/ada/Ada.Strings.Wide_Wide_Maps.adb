--  Ada.Strings.Wide_Wide_Maps.adb
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

with Ada.Unchecked_Deallocation;

package body Ada.Strings.Wide_Wide_Maps is

   --  A set is represented by its ranges in increasing order, disjoint and
   --  not adjacent (null for the empty set); a mapping by the characters that
   --  do not map to themselves, in increasing order, and the characters they
   --  map to. Both own their arrays (Adjust copies them, Finalize frees them).

   procedure Free is new Ada.Unchecked_Deallocation
     (Wide_Wide_Character_Ranges, Wide_Wide_Character_Ranges_Access);
   procedure Free is new Ada.Unchecked_Deallocation
     (Wide_Wide_Character_Sequence, Wide_Wide_Character_Sequence_Access);

   function Pos (C : Wide_Wide_Character) return Natural is
   begin
      return Wide_Wide_Character'Pos (C);
   end Pos;

   No_Ranges : Wide_Wide_Character_Ranges (1 .. 0);

   function Ranges_Of (Set : Wide_Wide_Character_Set)
     return Wide_Wide_Character_Ranges is
   begin
      if Set.Ranges = null then
         return No_Ranges;
      else
         return Set.Ranges.all;
      end if;
   end Ranges_Of;

   function Make (Ranges : Wide_Wide_Character_Ranges)
     return Wide_Wide_Character_Set is
      --  the set of normalized Ranges
   begin
      if Ranges'Length = 0 then
         return Null_Set;
      end if;
      return (Ada.Finalization.Controlled with
              Ranges => new Wide_Wide_Character_Ranges'(Ranges));
   end Make;

   procedure Sort (Ranges : in out Wide_Wide_Character_Ranges) is
      --  heap sort by Low

      procedure Sift (Root : Natural; Count : Natural) is
         --  (the heap's elements are Ranges (First + 0 .. First + Count - 1))
         R     : Natural := Root;
         Child : Natural;
         Temp  : Wide_Wide_Character_Range;
         First : constant Integer := Ranges'First;
      begin
         loop
            Child := 2 * R + 1;
            exit when Child >= Count;
            if Child + 1 < Count
              and then Ranges (First + Child).Low < Ranges (First + Child + 1).Low
            then
               Child := Child + 1;
            end if;
            exit when not (Ranges (First + R).Low < Ranges (First + Child).Low);
            Temp := Ranges (First + R);
            Ranges (First + R) := Ranges (First + Child);
            Ranges (First + Child) := Temp;
            R := Child;
         end loop;
      end Sift;

      N    : constant Natural := Ranges'Length;
      Temp : Wide_Wide_Character_Range;
   begin
      for Root in reverse 0 .. N / 2 loop
         Sift (Root, N);
      end loop;
      for Last in reverse 1 .. N - 1 loop
         Temp := Ranges (Ranges'First);
         Ranges (Ranges'First) := Ranges (Ranges'First + Last);
         Ranges (Ranges'First + Last) := Temp;
         Sift (0, Last);
      end loop;
   end Sort;

   function Normalize (Ranges : Wide_Wide_Character_Ranges)
     return Wide_Wide_Character_Ranges is
      --  Ranges sorted, without the null ranges, and with the overlapping or
      --  adjacent ranges merged
      Work   : Wide_Wide_Character_Ranges (1 .. Ranges'Length);
      Count  : Natural := 0;
      Result : Natural := 0;
   begin
      for I in Ranges'Range loop
         if Ranges (I).Low <= Ranges (I).High then
            Count := Count + 1;
            Work (Count) := Ranges (I);
         end if;
      end loop;
      Sort (Work (1 .. Count));
      for I in 1 .. Count loop
         if Result > 0
           and then Pos (Work (I).Low) - 1 <= Pos (Work (Result).High)
         then
            if Work (I).High > Work (Result).High then
               Work (Result).High := Work (I).High;
            end if;
         else
            Result := Result + 1;
            Work (Result) := Work (I);
         end if;
      end loop;
      return Work (1 .. Result);
   end Normalize;

   overriding procedure Adjust (Object : in out Wide_Wide_Character_Set) is
   begin
      if Object.Ranges /= null then
         Object.Ranges := new Wide_Wide_Character_Ranges'(Object.Ranges.all);
      end if;
   end Adjust;

   overriding procedure Finalize (Object : in out Wide_Wide_Character_Set) is
   begin
      Free (Object.Ranges);
   end Finalize;

   overriding procedure Adjust (Object : in out Wide_Wide_Character_Mapping) is
   begin
      if Object.Domain /= null then
         Object.Domain :=
           new Wide_Wide_Character_Sequence'(Object.Domain.all);
         Object.Targets :=
           new Wide_Wide_Character_Sequence'(Object.Targets.all);
      end if;
   end Adjust;

   overriding procedure Finalize
     (Object : in out Wide_Wide_Character_Mapping) is
   begin
      Free (Object.Domain);
      Free (Object.Targets);
   end Finalize;

   procedure Read
     (Stream : not null access Ada.Streams.Root_Stream_Type'Class;
      Item   : out Wide_Wide_Character_Set) is
   begin
      Item := To_Set (Wide_Wide_Character_Ranges'Input (Stream));
   end Read;

   procedure Write
     (Stream : not null access Ada.Streams.Root_Stream_Type'Class;
      Item   : in Wide_Wide_Character_Set) is
   begin
      Wide_Wide_Character_Ranges'Output (Stream, Ranges_Of (Item));
   end Write;

   procedure Read
     (Stream : not null access Ada.Streams.Root_Stream_Type'Class;
      Item   : out Wide_Wide_Character_Mapping) is
      Domain : constant Wide_Wide_Character_Sequence :=
        Wide_Wide_Character_Sequence'Input (Stream);
   begin
      Item := To_Mapping (Domain, Wide_Wide_Character_Sequence'Input (Stream));
   end Read;

   procedure Write
     (Stream : not null access Ada.Streams.Root_Stream_Type'Class;
      Item   : in Wide_Wide_Character_Mapping) is
   begin
      Wide_Wide_Character_Sequence'Output (Stream, To_Domain (Item));
      Wide_Wide_Character_Sequence'Output (Stream, To_Range (Item));
   end Write;

   --  Sets

   function To_Set (Ranges : in Wide_Wide_Character_Ranges)
         return Wide_Wide_Character_Set is
   begin
      return Make (Normalize (Ranges));
   end To_Set;

   function To_Set (Span : in Wide_Wide_Character_Range)
         return Wide_Wide_Character_Set is
   begin
      return Make (Normalize ((1 => Span)));
   end To_Set;

   function To_Ranges (Set : in Wide_Wide_Character_Set)
         return Wide_Wide_Character_Ranges is
   begin
      return Ranges_Of (Set);
   end To_Ranges;

   function "=" (Left, Right : in Wide_Wide_Character_Set) return Boolean is
   begin
      return Ranges_Of (Left) = Ranges_Of (Right);
   end "=";

   function "not" (Right : in Wide_Wide_Character_Set)
         return Wide_Wide_Character_Set is
      R      : constant Wide_Wide_Character_Ranges := Ranges_Of (Right);
      Result : Wide_Wide_Character_Ranges (1 .. R'Length + 1);
      Count  : Natural := 0;
      Next   : Wide_Wide_Character := Wide_Wide_Character'First;
      Done   : Boolean := False;  --  (Next is past the last character)
   begin
      for I in R'Range loop
         if R (I).Low > Next then
            Count := Count + 1;
            Result (Count) :=
              (Next, Wide_Wide_Character'Pred (R (I).Low));
         end if;
         if R (I).High = Wide_Wide_Character'Last then
            Done := True;
         else
            Next := Wide_Wide_Character'Succ (R (I).High);
         end if;
      end loop;
      if not Done then
         Count := Count + 1;
         Result (Count) := (Next, Wide_Wide_Character'Last);
      end if;
      return Make (Result (1 .. Count));
   end "not";

   function "and" (Left, Right : in Wide_Wide_Character_Set)
         return Wide_Wide_Character_Set is
      L      : constant Wide_Wide_Character_Ranges := Ranges_Of (Left);
      R      : constant Wide_Wide_Character_Ranges := Ranges_Of (Right);
      Result : Wide_Wide_Character_Ranges (1 .. L'Length + R'Length);
      Count  : Natural := 0;
      I      : Integer := L'First;
      J      : Integer := R'First;
      Low, High : Wide_Wide_Character;
   begin
      while I <= L'Last and then J <= R'Last loop
         Low := Wide_Wide_Character'Max (L (I).Low, R (J).Low);
         High := Wide_Wide_Character'Min (L (I).High, R (J).High);
         if Low <= High then
            Count := Count + 1;
            Result (Count) := (Low, High);
         end if;
         if L (I).High < R (J).High then
            I := I + 1;
         else
            J := J + 1;
         end if;
      end loop;
      return Make (Result (1 .. Count));
   end "and";

   function "or" (Left, Right : in Wide_Wide_Character_Set)
         return Wide_Wide_Character_Set is
   begin
      return Make (Normalize (Ranges_Of (Left) & Ranges_Of (Right)));
   end "or";

   function "xor" (Left, Right : in Wide_Wide_Character_Set)
         return Wide_Wide_Character_Set is
   begin
      return (Left - Right) or (Right - Left);
   end "xor";

   function "-" (Left, Right : in Wide_Wide_Character_Set)
         return Wide_Wide_Character_Set is
   begin
      return Left and not Right;
   end "-";

   function Is_In (Element : in Wide_Wide_Character;
                   Set     : in Wide_Wide_Character_Set)
         return Boolean is
      Low, High, Mid : Integer;
   begin
      if Set.Ranges = null then
         return False;
      end if;
      Low := Set.Ranges'First;
      High := Set.Ranges'Last;
      while Low <= High loop
         Mid := Low + (High - Low) / 2;
         if Element < Set.Ranges (Mid).Low then
            High := Mid - 1;
         elsif Element > Set.Ranges (Mid).High then
            Low := Mid + 1;
         else
            return True;
         end if;
      end loop;
      return False;
   end Is_In;

   function Is_Subset (Elements : in Wide_Wide_Character_Set;
                       Set      : in Wide_Wide_Character_Set)
         return Boolean is
   begin
      return (Elements - Set) = Null_Set;
   end Is_Subset;

   function To_Set (Sequence : in Wide_Wide_Character_Sequence)
         return Wide_Wide_Character_Set is
      Ranges : Wide_Wide_Character_Ranges (1 .. Sequence'Length);
   begin
      for I in Sequence'Range loop
         Ranges (I - Sequence'First + 1) := (Sequence (I), Sequence (I));
      end loop;
      return Make (Normalize (Ranges));
   end To_Set;

   function To_Set (Singleton : in Wide_Wide_Character)
         return Wide_Wide_Character_Set is
   begin
      return Make ((1 => (Singleton, Singleton)));
   end To_Set;

   function To_Sequence (Set : in Wide_Wide_Character_Set)
         return Wide_Wide_Character_Sequence is
      R     : constant Wide_Wide_Character_Ranges := Ranges_Of (Set);
      Count : Natural := 0;
   begin
      for I in R'Range loop
         Count := Count + (Pos (R (I).High) - Pos (R (I).Low) + 1);
      end loop;
      declare
         Result : Wide_Wide_Character_Sequence (1 .. Count);
         Next   : Positive := 1;
      begin
         for I in R'Range loop
            for C in R (I).Low .. R (I).High loop
               Result (Next) := C;
               Next := Next + 1;
            end loop;
         end loop;
         return Result;
      end;
   end To_Sequence;

   --  Mappings

   function Value (Map     : in Wide_Wide_Character_Mapping;
                   Element : in Wide_Wide_Character)
         return Wide_Wide_Character is
      Low, High, Mid : Integer;
   begin
      if Map.Domain = null then
         return Element;
      end if;
      Low := Map.Domain'First;
      High := Map.Domain'Last;
      while Low <= High loop
         Mid := Low + (High - Low) / 2;
         if Element < Map.Domain (Mid) then
            High := Mid - 1;
         elsif Element > Map.Domain (Mid) then
            Low := Mid + 1;
         else
            return Map.Targets (Mid);
         end if;
      end loop;
      return Element;
   end Value;

   function To_Mapping (From, To : in Wide_Wide_Character_Sequence)
         return Wide_Wide_Character_Mapping is
      --  (the pairs are sorted as ranges: Low the character of From, High the
      --  character of To)
      Pairs : Wide_Wide_Character_Ranges (1 .. From'Length);
      Count : Natural := 0;
   begin
      if From'Length /= To'Length then
         raise Translation_Error;
      end if;
      for I in Pairs'Range loop
         Pairs (I) := (From (From'First + I - 1), To (To'First + I - 1));
      end loop;
      Sort (Pairs);
      for I in Pairs'First + 1 .. Pairs'Last loop
         if Pairs (I).Low = Pairs (I - 1).Low then
            raise Translation_Error;
         end if;
      end loop;
      for I in Pairs'Range loop
         if Pairs (I).Low /= Pairs (I).High then
            Count := Count + 1;
            Pairs (Count) := Pairs (I);
         end if;
      end loop;
      if Count = 0 then
         return Identity;
      end if;
      declare
         Domain  : Wide_Wide_Character_Sequence (1 .. Count);
         Targets : Wide_Wide_Character_Sequence (1 .. Count);
      begin
         for I in 1 .. Count loop
            Domain (I) := Pairs (I).Low;
            Targets (I) := Pairs (I).High;
         end loop;
         return (Ada.Finalization.Controlled with
                 Domain  => new Wide_Wide_Character_Sequence'(Domain),
                 Targets => new Wide_Wide_Character_Sequence'(Targets));
      end;
   end To_Mapping;

   function To_Domain (Map : in Wide_Wide_Character_Mapping)
         return Wide_Wide_Character_Sequence is
   begin
      if Map.Domain = null then
         return "";
      else
         return Map.Domain.all;
      end if;
   end To_Domain;

   function To_Range (Map : in Wide_Wide_Character_Mapping)
         return Wide_Wide_Character_Sequence is
   begin
      if Map.Targets = null then
         return "";
      else
         return Map.Targets.all;
      end if;
   end To_Range;

end Ada.Strings.Wide_Wide_Maps;
