--  Ada.Strings.Wide_Wide_Fixed.adb
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

package body Ada.Strings.Wide_Wide_Fixed is

   use Ada.Strings.Wide_Wide_Maps;

   --  "Copy" procedure

   procedure Move (Source  : in  Wide_Wide_String;
                   Target  : out Wide_Wide_String;
                   Drop    : in  Truncation := Error;
                   Justify : in  Alignment  := Left;
                   Pad     : in  Wide_Wide_Character  := Wide_Wide_Space) is
      SL : constant Natural := Source'Length;
      TL : constant Natural := Target'Length;
   begin
      if SL = TL then
         Target := Source;
      elsif SL < TL then
         declare
            Front : Natural;  --  the number of Pad characters before Source
         begin
            case Justify is
               when Left   => Front := 0;
               when Right  => Front := TL - SL;
               when Center => Front := (TL - SL) / 2;
            end case;
            for I in Target'First .. Target'First + Front - 1 loop
               Target (I) := Pad;
            end loop;
            Target (Target'First + Front .. Target'First + Front + SL - 1) :=
              Source;
            for I in Target'First + Front + SL .. Target'Last loop
               Target (I) := Pad;
            end loop;
         end;
      else
         case Drop is
            when Left =>
               Target := Source (Source'Last - TL + 1 .. Source'Last);
            when Right =>
               Target := Source (Source'First .. Source'First + TL - 1);
            when Error =>
               case Justify is
                  when Left =>
                     for I in Source'First + TL .. Source'Last loop
                        if Source (I) /= Pad then
                           raise Length_Error;
                        end if;
                     end loop;
                     Target := Source (Source'First .. Source'First + TL - 1);
                  when Right =>
                     for I in Source'First .. Source'Last - TL loop
                        if Source (I) /= Pad then
                           raise Length_Error;
                        end if;
                     end loop;
                     Target := Source (Source'Last - TL + 1 .. Source'Last);
                  when Center =>
                     raise Length_Error;
               end case;
         end case;
      end if;
   end Move;

   --  Search subprograms

   function Matches (Source  : Wide_Wide_String;
                     At_Index : Integer;
                     Pattern : Wide_Wide_String;
                     Mapping : Wide_Wide_Maps.Wide_Wide_Character_Mapping) return Boolean is
      --  whether the slice of Source starting at At_Index matches Pattern
   begin
      for J in Pattern'Range loop
         if Value (Mapping, Source (At_Index + (J - Pattern'First)))
           /= Pattern (J)
         then
            return False;
         end if;
      end loop;
      return True;
   end Matches;

   function Matches (Source  : Wide_Wide_String;
                     At_Index : Integer;
                     Pattern : Wide_Wide_String;
                     Mapping : Wide_Wide_Maps.Wide_Wide_Character_Mapping_Function)
     return Boolean is
   begin
      for J in Pattern'Range loop
         if Mapping (Source (At_Index + (J - Pattern'First))) /= Pattern (J)
         then
            return False;
         end if;
      end loop;
      return True;
   end Matches;

   function Index (Source  : in Wide_Wide_String;
                   Pattern : in Wide_Wide_String;
                   From    : in Positive;
                   Going   : in Direction := Forward;
                   Mapping : in Wide_Wide_Maps.Wide_Wide_Character_Mapping := Wide_Wide_Maps.Identity)
      return Natural is
   begin
      if Source'Length = 0 then
         return 0;
      elsif From not in Source'Range then
         raise Index_Error;
      elsif Pattern'Length = 0 then
         raise Pattern_Error;
      end if;
      if Going = Forward then
         for I in From .. Source'Last - Pattern'Length + 1 loop
            if Matches (Source, I, Pattern, Mapping) then
               return I;
            end if;
         end loop;
      else
         for I in reverse Source'First .. From - Pattern'Length + 1 loop
            if Matches (Source, I, Pattern, Mapping) then
               return I;
            end if;
         end loop;
      end if;
      return 0;
   end Index;

   function Index (Source  : in Wide_Wide_String;
                   Pattern : in Wide_Wide_String;
                   From    : in Positive;
                   Going   : in Direction := Forward;
                   Mapping : in Wide_Wide_Maps.Wide_Wide_Character_Mapping_Function)
      return Natural is
   begin
      if Source'Length = 0 then
         return 0;
      elsif From not in Source'Range then
         raise Index_Error;
      elsif Pattern'Length = 0 then
         raise Pattern_Error;
      elsif Mapping = null then
         raise Constraint_Error;
      end if;
      if Going = Forward then
         for I in From .. Source'Last - Pattern'Length + 1 loop
            if Matches (Source, I, Pattern, Mapping) then
               return I;
            end if;
         end loop;
      else
         for I in reverse Source'First .. From - Pattern'Length + 1 loop
            if Matches (Source, I, Pattern, Mapping) then
               return I;
            end if;
         end loop;
      end if;
      return 0;
   end Index;

   function Index (Source   : in Wide_Wide_String;
                   Pattern  : in Wide_Wide_String;
                   Going    : in Direction := Forward;
                   Mapping  : in Wide_Wide_Maps.Wide_Wide_Character_Mapping := Wide_Wide_Maps.Identity)
      return Natural is
   begin
      if Source'Length = 0 then
         return 0;
      elsif Going = Forward then
         return Index (Source, Pattern, Source'First, Forward, Mapping);
      else
         return Index (Source, Pattern, Source'Last, Backward, Mapping);
      end if;
   end Index;

   function Index (Source   : in Wide_Wide_String;
                   Pattern  : in Wide_Wide_String;
                   Going    : in Direction := Forward;
                   Mapping  : in Wide_Wide_Maps.Wide_Wide_Character_Mapping_Function)
      return Natural is
   begin
      if Source'Length = 0 then
         return 0;
      elsif Going = Forward then
         return Index (Source, Pattern, Source'First, Forward, Mapping);
      else
         return Index (Source, Pattern, Source'Last, Backward, Mapping);
      end if;
   end Index;

   function Index (Source  : in Wide_Wide_String;
                   Set     : in Wide_Wide_Maps.Wide_Wide_Character_Set;
                   From    : in Positive;
                   Test    : in Membership := Inside;
                   Going   : in Direction := Forward)
      return Natural is
      Wanted : constant Boolean := Test = Inside;
   begin
      if Source'Length = 0 then
         return 0;
      elsif From not in Source'Range then
         raise Index_Error;
      end if;
      if Going = Forward then
         for I in From .. Source'Last loop
            if Is_In (Source (I), Set) = Wanted then
               return I;
            end if;
         end loop;
      else
         for I in reverse Source'First .. From loop
            if Is_In (Source (I), Set) = Wanted then
               return I;
            end if;
         end loop;
      end if;
      return 0;
   end Index;

   function Index (Source : in Wide_Wide_String;
                   Set    : in Wide_Wide_Maps.Wide_Wide_Character_Set;
                   Test   : in Membership := Inside;
                   Going  : in Direction  := Forward)
      return Natural is
   begin
      if Source'Length = 0 then
         return 0;
      elsif Going = Forward then
         return Index (Source, Set, Source'First, Test, Forward);
      else
         return Index (Source, Set, Source'Last, Test, Backward);
      end if;
   end Index;

   function Index_Non_Blank (Source : in Wide_Wide_String;
                             From   : in Positive;
                             Going  : in Direction := Forward)
      return Natural is
   begin
      return Index (Source, Wide_Wide_Maps.To_Set (Wide_Wide_Space), From, Outside, Going);
   end Index_Non_Blank;

   function Index_Non_Blank (Source : in Wide_Wide_String;
                             Going  : in Direction := Forward)
      return Natural is
   begin
      return Index (Source, Wide_Wide_Maps.To_Set (Wide_Wide_Space), Outside, Going);
   end Index_Non_Blank;

   function Count (Source   : in Wide_Wide_String;
                   Pattern  : in Wide_Wide_String;
                   Mapping  : in Wide_Wide_Maps.Wide_Wide_Character_Mapping := Wide_Wide_Maps.Identity)
      return Natural is
      N : Natural := 0;
      I : Integer := Source'First;
   begin
      if Pattern'Length = 0 then
         raise Pattern_Error;
      end if;
      while I <= Source'Last - Pattern'Length + 1 loop
         if Matches (Source, I, Pattern, Mapping) then
            N := N + 1;
            I := I + Pattern'Length;
         else
            I := I + 1;
         end if;
      end loop;
      return N;
   end Count;

   function Count (Source   : in Wide_Wide_String;
                   Pattern  : in Wide_Wide_String;
                   Mapping  : in Wide_Wide_Maps.Wide_Wide_Character_Mapping_Function)
      return Natural is
      N : Natural := 0;
      I : Integer := Source'First;
   begin
      if Pattern'Length = 0 then
         raise Pattern_Error;
      elsif Mapping = null then
         raise Constraint_Error;
      end if;
      while I <= Source'Last - Pattern'Length + 1 loop
         if Matches (Source, I, Pattern, Mapping) then
            N := N + 1;
            I := I + Pattern'Length;
         else
            I := I + 1;
         end if;
      end loop;
      return N;
   end Count;

   function Count (Source   : in Wide_Wide_String;
                   Set      : in Wide_Wide_Maps.Wide_Wide_Character_Set)
      return Natural is
      N : Natural := 0;
   begin
      for I in Source'Range loop
         if Is_In (Source (I), Set) then
            N := N + 1;
         end if;
      end loop;
      return N;
   end Count;

   procedure Find_Token (Source : in Wide_Wide_String;
                         Set    : in Wide_Wide_Maps.Wide_Wide_Character_Set;
                         From   : in Positive;
                         Test   : in Membership;
                         First  : out Positive;
                         Last   : out Natural) is
      Wanted : constant Boolean := Test = Inside;
   begin
      if Source'Length = 0 then
         First := From;
         Last := 0;
         return;
      elsif From not in Source'Range then
         raise Index_Error;
      end if;
      for I in From .. Source'Last loop
         if Is_In (Source (I), Set) = Wanted then
            First := I;
            Last := I;
            while Last < Source'Last
              and then Is_In (Source (Last + 1), Set) = Wanted
            loop
               Last := Last + 1;
            end loop;
            return;
         end if;
      end loop;
      First := From;
      Last := 0;
   end Find_Token;

   procedure Find_Token (Source : in Wide_Wide_String;
                         Set    : in Wide_Wide_Maps.Wide_Wide_Character_Set;
                         Test   : in Membership;
                         First  : out Positive;
                         Last   : out Natural) is
   begin
      Find_Token (Source, Set, Source'First, Test, First, Last);
   end Find_Token;

   --  String translation subprograms

   function Translate (Source  : in Wide_Wide_String;
                       Mapping : in Wide_Wide_Maps.Wide_Wide_Character_Mapping)
      return Wide_Wide_String is
      Result : Wide_Wide_String (1 .. Source'Length);
   begin
      for I in Source'Range loop
         Result (I - Source'First + 1) := Value (Mapping, Source (I));
      end loop;
      return Result;
   end Translate;

   procedure Translate (Source  : in out Wide_Wide_String;
                        Mapping : in Wide_Wide_Maps.Wide_Wide_Character_Mapping) is
   begin
      for I in Source'Range loop
         Source (I) := Value (Mapping, Source (I));
      end loop;
   end Translate;

   function Translate (Source  : in Wide_Wide_String;
                       Mapping : in Wide_Wide_Maps.Wide_Wide_Character_Mapping_Function)
      return Wide_Wide_String is
      Result : Wide_Wide_String (1 .. Source'Length);
   begin
      if Mapping = null then
         raise Constraint_Error;
      end if;
      for I in Source'Range loop
         Result (I - Source'First + 1) := Mapping (Source (I));
      end loop;
      return Result;
   end Translate;

   procedure Translate (Source  : in out Wide_Wide_String;
                        Mapping : in Wide_Wide_Maps.Wide_Wide_Character_Mapping_Function) is
   begin
      if Mapping = null then
         raise Constraint_Error;
      end if;
      for I in Source'Range loop
         Source (I) := Mapping (Source (I));
      end loop;
   end Translate;

   --  String transformation subprograms

   function Replace_Slice (Source   : in Wide_Wide_String;
                           Low      : in Positive;
                           High     : in Natural;
                           By       : in Wide_Wide_String)
      return Wide_Wide_String is
   begin
      if Low > Source'Last + 1 or else High < Source'First - 1 then
         raise Index_Error;
      end if;
      if High >= Low then
         declare
            Front_Length : constant Natural :=
              Natural'Max (0, Low - Source'First);
            Back_Length  : constant Natural :=
              Natural'Max (0, Source'Last - High);
            Result : Wide_Wide_String (1 .. Front_Length + By'Length + Back_Length);
         begin
            Result (1 .. Front_Length) :=
              Source (Source'First .. Source'First + Front_Length - 1);
            Result (Front_Length + 1 .. Front_Length + By'Length) := By;
            Result (Front_Length + By'Length + 1 .. Result'Last) :=
              Source (Source'Last - Back_Length + 1 .. Source'Last);
            return Result;
         end;
      else
         return Insert (Source, Before => Low, New_Item => By);
      end if;
   end Replace_Slice;

   procedure Replace_Slice (Source   : in out Wide_Wide_String;
                            Low      : in Positive;
                            High     : in Natural;
                            By       : in Wide_Wide_String;
                            Drop     : in Truncation := Error;
                            Justify  : in Alignment  := Left;
                            Pad      : in Wide_Wide_Character  := Wide_Wide_Space) is
   begin
      Move (Replace_Slice (Source, Low, High, By), Source, Drop, Justify, Pad);
   end Replace_Slice;

   function Insert (Source   : in Wide_Wide_String;
                    Before   : in Positive;
                    New_Item : in Wide_Wide_String)
      return Wide_Wide_String is
      Front_Length : constant Integer := Before - Source'First;
      Result : Wide_Wide_String (1 .. Source'Length + New_Item'Length);
   begin
      if Before < Source'First or else Before > Source'Last + 1 then
         raise Index_Error;
      end if;
      Result (1 .. Front_Length) := Source (Source'First .. Before - 1);
      Result (Front_Length + 1 .. Front_Length + New_Item'Length) := New_Item;
      Result (Front_Length + New_Item'Length + 1 .. Result'Last) :=
        Source (Before .. Source'Last);
      return Result;
   end Insert;

   procedure Insert (Source   : in out Wide_Wide_String;
                     Before   : in Positive;
                     New_Item : in Wide_Wide_String;
                     Drop     : in Truncation := Error) is
   begin
      Move (Insert (Source, Before, New_Item), Source, Drop);
   end Insert;

   function Overwrite (Source   : in Wide_Wide_String;
                       Position : in Positive;
                       New_Item : in Wide_Wide_String)
      return Wide_Wide_String is
   begin
      if Position < Source'First or else Position > Source'Last + 1 then
         raise Index_Error;
      end if;
      declare
         Front_Length : constant Natural := Position - Source'First;
         Result : Wide_Wide_String (1 .. Natural'Max (Source'Length,
                                            Front_Length + New_Item'Length));
      begin
         Result (1 .. Source'Length) := Source;
         Result (Front_Length + 1 .. Front_Length + New_Item'Length) :=
           New_Item;
         return Result;
      end;
   end Overwrite;

   procedure Overwrite (Source   : in out Wide_Wide_String;
                        Position : in Positive;
                        New_Item : in Wide_Wide_String;
                        Drop     : in Truncation := Right) is
   begin
      Move (Overwrite (Source, Position, New_Item), Source, Drop);
   end Overwrite;

   function Delete (Source  : in Wide_Wide_String;
                    From    : in Positive;
                    Through : in Natural)
      return Wide_Wide_String is
   begin
      if From <= Through then
         return Replace_Slice (Source, From, Through, "");
      else
         declare
            Result : constant Wide_Wide_String (1 .. Source'Length) := Source;
         begin
            return Result;
         end;
      end if;
   end Delete;

   procedure Delete (Source  : in out Wide_Wide_String;
                     From    : in Positive;
                     Through : in Natural;
                     Justify : in Alignment := Left;
                     Pad     : in Wide_Wide_Character := Wide_Wide_Space) is
   begin
      Move (Delete (Source, From, Through), Source,
            Justify => Justify, Pad => Pad);
   end Delete;

   --  String selector subprograms

   function Trim (Source : in Wide_Wide_String;
                  Side   : in Trim_End)
      return Wide_Wide_String is
      First : Integer := Source'First;
      Last  : Integer := Source'Last;
   begin
      if Side = Left or else Side = Both then
         while First <= Last and then Source (First) = Wide_Wide_Space loop
            First := First + 1;
         end loop;
      end if;
      if Side = Right or else Side = Both then
         while Last >= First and then Source (Last) = Wide_Wide_Space loop
            Last := Last - 1;
         end loop;
      end if;
      declare
         Result : constant Wide_Wide_String (1 .. Last - First + 1) :=
           Source (First .. Last);
      begin
         return Result;
      end;
   end Trim;

   procedure Trim (Source  : in out Wide_Wide_String;
                   Side    : in Trim_End;
                   Justify : in Alignment := Left;
                   Pad     : in Wide_Wide_Character := Wide_Wide_Space) is
   begin
      Move (Trim (Source, Side), Source, Justify => Justify, Pad => Pad);
   end Trim;

   function Trim (Source : in Wide_Wide_String;
                  Left   : in Wide_Wide_Maps.Wide_Wide_Character_Set;
                  Right  : in Wide_Wide_Maps.Wide_Wide_Character_Set)
      return Wide_Wide_String is
      First : Integer := Source'First;
      Last  : Integer := Source'Last;
   begin
      while First <= Last and then Is_In (Source (First), Left) loop
         First := First + 1;
      end loop;
      while Last >= First and then Is_In (Source (Last), Right) loop
         Last := Last - 1;
      end loop;
      declare
         Result : constant Wide_Wide_String (1 .. Last - First + 1) :=
           Source (First .. Last);
      begin
         return Result;
      end;
   end Trim;

   procedure Trim (Source  : in out Wide_Wide_String;
                   Left    : in Wide_Wide_Maps.Wide_Wide_Character_Set;
                   Right   : in Wide_Wide_Maps.Wide_Wide_Character_Set;
                   Justify : in Alignment := Strings.Left;
                   Pad     : in Wide_Wide_Character := Wide_Wide_Space) is
   begin
      Move (Trim (Source, Left, Right), Source,
            Justify => Justify, Pad => Pad);
   end Trim;

   function Head (Source : in Wide_Wide_String;
                  Count  : in Natural;
                  Pad    : in Wide_Wide_Character := Wide_Wide_Space)
      return Wide_Wide_String is
      Result : Wide_Wide_String (1 .. Count);
   begin
      if Count <= Source'Length then
         Result := Source (Source'First .. Source'First + Count - 1);
      else
         Result (1 .. Source'Length) := Source;
         for I in Source'Length + 1 .. Count loop
            Result (I) := Pad;
         end loop;
      end if;
      return Result;
   end Head;

   procedure Head (Source  : in out Wide_Wide_String;
                   Count   : in Natural;
                   Justify : in Alignment := Left;
                   Pad     : in Wide_Wide_Character := Wide_Wide_Space) is
   begin
      Move (Head (Source, Count, Pad), Source, Drop => Error,
            Justify => Justify, Pad => Pad);
   end Head;

   function Tail (Source : in Wide_Wide_String;
                  Count  : in Natural;
                  Pad    : in Wide_Wide_Character := Wide_Wide_Space)
      return Wide_Wide_String is
      Result : Wide_Wide_String (1 .. Count);
   begin
      if Count <= Source'Length then
         Result := Source (Source'Last - Count + 1 .. Source'Last);
      else
         for I in 1 .. Count - Source'Length loop
            Result (I) := Pad;
         end loop;
         Result (Count - Source'Length + 1 .. Count) := Source;
      end if;
      return Result;
   end Tail;

   procedure Tail (Source  : in out Wide_Wide_String;
                   Count   : in Natural;
                   Justify : in Alignment := Left;
                   Pad     : in Wide_Wide_Character := Wide_Wide_Space) is
   begin
      Move (Tail (Source, Count, Pad), Source, Drop => Error,
            Justify => Justify, Pad => Pad);
   end Tail;

   --  String constructor functions

   function "*" (Left  : in Natural;
                 Right : in Wide_Wide_Character) return Wide_Wide_String is
      Result : constant Wide_Wide_String (1 .. Left) := (others => Right);
   begin
      return Result;
   end "*";

   function "*" (Left  : in Natural;
                 Right : in Wide_Wide_String) return Wide_Wide_String is
      Result : Wide_Wide_String (1 .. Left * Right'Length);
      Next   : Positive := 1;
   begin
      for I in 1 .. Left loop
         Result (Next .. Next + Right'Length - 1) := Right;
         Next := Next + Right'Length;
      end loop;
      return Result;
   end "*";

end Ada.Strings.Wide_Wide_Fixed;
