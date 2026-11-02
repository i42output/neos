--  Ada.Strings.Wide_Wide_Bounded.adb
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

with Ada.Strings.Wide_Wide_Fixed;

package body Ada.Strings.Wide_Wide_Bounded is

   package body Generic_Bounded_Length is

      --  A Bounded_String's text is Data (1 .. Length).

      function Length (Source : in Bounded_Wide_Wide_String) return Length_Range is
      begin
         return Source.Length;
      end Length;

      --  Conversion, Concatenation, and Selection functions

      function To_Bounded_Wide_Wide_String (Source : in Wide_Wide_String;
                                  Drop   : in Truncation := Error)
         return Bounded_Wide_Wide_String is
         Result : Bounded_Wide_Wide_String;
      begin
         if Source'Length <= Max_Length then
            Result.Length := Source'Length;
            Result.Data (1 .. Source'Length) := Source;
         else
            case Drop is
               when Left =>
                  Result.Data :=
                    Source (Source'Last - Max_Length + 1 .. Source'Last);
               when Right =>
                  Result.Data :=
                    Source (Source'First .. Source'First + Max_Length - 1);
               when Error =>
                  raise Length_Error;
            end case;
            Result.Length := Max_Length;
         end if;
         return Result;
      end To_Bounded_Wide_Wide_String;

      function To_Wide_Wide_String (Source : in Bounded_Wide_Wide_String) return Wide_Wide_String is
      begin
         return Source.Data (1 .. Source.Length);
      end To_Wide_Wide_String;

      procedure Set_Bounded_Wide_Wide_String
         (Target :    out Bounded_Wide_Wide_String;
          Source : in     Wide_Wide_String;
          Drop   : in     Truncation := Error) is
      begin
         Target := To_Bounded_Wide_Wide_String (Source, Drop);
      end Set_Bounded_Wide_Wide_String;

      function Append (Left, Right : in Bounded_Wide_Wide_String;
                       Drop        : in Truncation  := Error)
         return Bounded_Wide_Wide_String is
      begin
         return To_Bounded_Wide_Wide_String (Left.Data (1 .. Left.Length)
                                   & Right.Data (1 .. Right.Length), Drop);
      end Append;

      function Append (Left  : in Bounded_Wide_Wide_String;
                       Right : in Wide_Wide_String;
                       Drop  : in Truncation := Error)
         return Bounded_Wide_Wide_String is
      begin
         return To_Bounded_Wide_Wide_String (Left.Data (1 .. Left.Length) & Right, Drop);
      end Append;

      function Append (Left  : in Wide_Wide_String;
                       Right : in Bounded_Wide_Wide_String;
                       Drop  : in Truncation := Error)
         return Bounded_Wide_Wide_String is
      begin
         return To_Bounded_Wide_Wide_String (Left & Right.Data (1 .. Right.Length),
                                   Drop);
      end Append;

      function Append (Left  : in Bounded_Wide_Wide_String;
                       Right : in Wide_Wide_Character;
                       Drop  : in Truncation := Error)
         return Bounded_Wide_Wide_String is
      begin
         return To_Bounded_Wide_Wide_String (Left.Data (1 .. Left.Length) & Right, Drop);
      end Append;

      function Append (Left  : in Wide_Wide_Character;
                       Right : in Bounded_Wide_Wide_String;
                       Drop  : in Truncation := Error)
         return Bounded_Wide_Wide_String is
      begin
         return To_Bounded_Wide_Wide_String (Left & Right.Data (1 .. Right.Length),
                                   Drop);
      end Append;

      procedure Append (Source   : in out Bounded_Wide_Wide_String;
                        New_Item : in Bounded_Wide_Wide_String;
                        Drop     : in Truncation  := Error) is
      begin
         Source := Append (Source, New_Item, Drop);
      end Append;

      procedure Append (Source   : in out Bounded_Wide_Wide_String;
                        New_Item : in Wide_Wide_String;
                        Drop     : in Truncation  := Error) is
      begin
         Source := Append (Source, New_Item, Drop);
      end Append;

      procedure Append (Source   : in out Bounded_Wide_Wide_String;
                        New_Item : in Wide_Wide_Character;
                        Drop     : in Truncation  := Error) is
      begin
         Source := Append (Source, New_Item, Drop);
      end Append;

      function "&" (Left, Right : in Bounded_Wide_Wide_String)
         return Bounded_Wide_Wide_String is
      begin
         return Append (Left, Right, Error);
      end "&";

      function "&" (Left : in Bounded_Wide_Wide_String; Right : in Wide_Wide_String)
         return Bounded_Wide_Wide_String is
      begin
         return Append (Left, Right, Error);
      end "&";

      function "&" (Left : in Wide_Wide_String; Right : in Bounded_Wide_Wide_String)
         return Bounded_Wide_Wide_String is
      begin
         return Append (Left, Right, Error);
      end "&";

      function "&" (Left : in Bounded_Wide_Wide_String; Right : in Wide_Wide_Character)
         return Bounded_Wide_Wide_String is
      begin
         return Append (Left, Right, Error);
      end "&";

      function "&" (Left : in Wide_Wide_Character; Right : in Bounded_Wide_Wide_String)
         return Bounded_Wide_Wide_String is
      begin
         return Append (Left, Right, Error);
      end "&";

      function Element (Source : in Bounded_Wide_Wide_String;
                        Index  : in Positive)
         return Wide_Wide_Character is
      begin
         if Index > Source.Length then
            raise Index_Error;
         end if;
         return Source.Data (Index);
      end Element;

      procedure Replace_Element (Source : in out Bounded_Wide_Wide_String;
                                 Index  : in Positive;
                                 By     : in Wide_Wide_Character) is
      begin
         if Index > Source.Length then
            raise Index_Error;
         end if;
         Source.Data (Index) := By;
      end Replace_Element;

      function Slice (Source : in Bounded_Wide_Wide_String;
                      Low    : in Positive;
                      High   : in Natural)
         return Wide_Wide_String is
      begin
         if Low > Source.Length + 1 or else High > Source.Length then
            raise Index_Error;
         end if;
         return Source.Data (Low .. High);
      end Slice;

      function Bounded_Slice
         (Source : in Bounded_Wide_Wide_String;
          Low    : in Positive;
          High   : in Natural)
             return Bounded_Wide_Wide_String is
      begin
         return To_Bounded_Wide_Wide_String (Slice (Source, Low, High));
      end Bounded_Slice;

      procedure Bounded_Slice
         (Source : in     Bounded_Wide_Wide_String;
          Target :    out Bounded_Wide_Wide_String;
          Low    : in     Positive;
          High   : in     Natural) is
      begin
         Target := Bounded_Slice (Source, Low, High);
      end Bounded_Slice;

      --  Comparisons

      function "=" (Left, Right : in Bounded_Wide_Wide_String) return Boolean is
      begin
         return Left.Data (1 .. Left.Length) = Right.Data (1 .. Right.Length);
      end "=";

      function "=" (Left : in Bounded_Wide_Wide_String; Right : in Wide_Wide_String)
        return Boolean is
      begin
         return Left.Data (1 .. Left.Length) = Right;
      end "=";

      function "=" (Left : in Wide_Wide_String; Right : in Bounded_Wide_Wide_String)
        return Boolean is
      begin
         return Left = Right.Data (1 .. Right.Length);
      end "=";

      function "<" (Left, Right : in Bounded_Wide_Wide_String) return Boolean is
      begin
         return Left.Data (1 .. Left.Length) < Right.Data (1 .. Right.Length);
      end "<";

      function "<" (Left : in Bounded_Wide_Wide_String; Right : in Wide_Wide_String)
        return Boolean is
      begin
         return Left.Data (1 .. Left.Length) < Right;
      end "<";

      function "<" (Left : in Wide_Wide_String; Right : in Bounded_Wide_Wide_String)
        return Boolean is
      begin
         return Left < Right.Data (1 .. Right.Length);
      end "<";

      function "<=" (Left, Right : in Bounded_Wide_Wide_String) return Boolean is
      begin
         return Left.Data (1 .. Left.Length)
                  <= Right.Data (1 .. Right.Length);
      end "<=";

      function "<=" (Left : in Bounded_Wide_Wide_String; Right : in Wide_Wide_String)
        return Boolean is
      begin
         return Left.Data (1 .. Left.Length) <= Right;
      end "<=";

      function "<=" (Left : in Wide_Wide_String; Right : in Bounded_Wide_Wide_String)
        return Boolean is
      begin
         return Left <= Right.Data (1 .. Right.Length);
      end "<=";

      function ">" (Left, Right : in Bounded_Wide_Wide_String) return Boolean is
      begin
         return Left.Data (1 .. Left.Length) > Right.Data (1 .. Right.Length);
      end ">";

      function ">" (Left : in Bounded_Wide_Wide_String; Right : in Wide_Wide_String)
        return Boolean is
      begin
         return Left.Data (1 .. Left.Length) > Right;
      end ">";

      function ">" (Left : in Wide_Wide_String; Right : in Bounded_Wide_Wide_String)
        return Boolean is
      begin
         return Left > Right.Data (1 .. Right.Length);
      end ">";

      function ">=" (Left, Right : in Bounded_Wide_Wide_String) return Boolean is
      begin
         return Left.Data (1 .. Left.Length)
                  >= Right.Data (1 .. Right.Length);
      end ">=";

      function ">=" (Left : in Bounded_Wide_Wide_String; Right : in Wide_Wide_String)
        return Boolean is
      begin
         return Left.Data (1 .. Left.Length) >= Right;
      end ">=";

      function ">=" (Left : in Wide_Wide_String; Right : in Bounded_Wide_Wide_String)
        return Boolean is
      begin
         return Left >= Right.Data (1 .. Right.Length);
      end ">=";

      --  Search subprograms

      function Index (Source  : in Bounded_Wide_Wide_String;
                      Pattern : in Wide_Wide_String;
                      From    : in Positive;
                      Going   : in Direction := Forward;
                      Mapping : in Wide_Wide_Maps.Wide_Wide_Character_Mapping := Wide_Wide_Maps.Identity)
         return Natural is
      begin
         return Wide_Wide_Fixed.Index (Source.Data (1 .. Source.Length), Pattern, From,
                             Going, Mapping);
      end Index;

      function Index (Source  : in Bounded_Wide_Wide_String;
                      Pattern : in Wide_Wide_String;
                      From    : in Positive;
                      Going   : in Direction := Forward;
                      Mapping : in Wide_Wide_Maps.Wide_Wide_Character_Mapping_Function)
         return Natural is
      begin
         return Wide_Wide_Fixed.Index (Source.Data (1 .. Source.Length), Pattern, From,
                             Going, Mapping);
      end Index;

      function Index (Source   : in Bounded_Wide_Wide_String;
                      Pattern  : in Wide_Wide_String;
                      Going    : in Direction := Forward;
                      Mapping  : in Wide_Wide_Maps.Wide_Wide_Character_Mapping := Wide_Wide_Maps.Identity)
         return Natural is
      begin
         return Wide_Wide_Fixed.Index (Source.Data (1 .. Source.Length), Pattern, Going,
                             Mapping);
      end Index;

      function Index (Source   : in Bounded_Wide_Wide_String;
                      Pattern  : in Wide_Wide_String;
                      Going    : in Direction := Forward;
                      Mapping  : in Wide_Wide_Maps.Wide_Wide_Character_Mapping_Function)
         return Natural is
      begin
         return Wide_Wide_Fixed.Index (Source.Data (1 .. Source.Length), Pattern, Going,
                             Mapping);
      end Index;

      function Index (Source  : in Bounded_Wide_Wide_String;
                      Set     : in Wide_Wide_Maps.Wide_Wide_Character_Set;
                      From    : in Positive;
                      Test    : in Membership := Inside;
                      Going   : in Direction := Forward)
         return Natural is
      begin
         return Wide_Wide_Fixed.Index (Source.Data (1 .. Source.Length), Set, From, Test,
                             Going);
      end Index;

      function Index (Source : in Bounded_Wide_Wide_String;
                      Set    : in Wide_Wide_Maps.Wide_Wide_Character_Set;
                      Test   : in Membership := Inside;
                      Going  : in Direction  := Forward)
         return Natural is
      begin
         return Wide_Wide_Fixed.Index (Source.Data (1 .. Source.Length), Set, Test,
                             Going);
      end Index;

      function Index_Non_Blank (Source : in Bounded_Wide_Wide_String;
                                From   : in Positive;
                                Going  : in Direction := Forward)
         return Natural is
      begin
         return Wide_Wide_Fixed.Index_Non_Blank (Source.Data (1 .. Source.Length), From,
                                       Going);
      end Index_Non_Blank;

      function Index_Non_Blank (Source : in Bounded_Wide_Wide_String;
                                Going  : in Direction := Forward)
         return Natural is
      begin
         return Wide_Wide_Fixed.Index_Non_Blank (Source.Data (1 .. Source.Length),
                                       Going);
      end Index_Non_Blank;

      function Count (Source   : in Bounded_Wide_Wide_String;
                      Pattern  : in Wide_Wide_String;
                      Mapping  : in Wide_Wide_Maps.Wide_Wide_Character_Mapping := Wide_Wide_Maps.Identity)
         return Natural is
      begin
         return Wide_Wide_Fixed.Count (Source.Data (1 .. Source.Length), Pattern,
                             Mapping);
      end Count;

      function Count (Source   : in Bounded_Wide_Wide_String;
                      Pattern  : in Wide_Wide_String;
                      Mapping  : in Wide_Wide_Maps.Wide_Wide_Character_Mapping_Function)
         return Natural is
      begin
         return Wide_Wide_Fixed.Count (Source.Data (1 .. Source.Length), Pattern,
                             Mapping);
      end Count;

      function Count (Source   : in Bounded_Wide_Wide_String;
                      Set      : in Wide_Wide_Maps.Wide_Wide_Character_Set)
         return Natural is
      begin
         return Wide_Wide_Fixed.Count (Source.Data (1 .. Source.Length), Set);
      end Count;

      procedure Find_Token (Source : in Bounded_Wide_Wide_String;
                            Set    : in Wide_Wide_Maps.Wide_Wide_Character_Set;
                            From   : in Positive;
                            Test   : in Membership;
                            First  : out Positive;
                            Last   : out Natural) is
      begin
         Wide_Wide_Fixed.Find_Token (Source.Data (1 .. Source.Length), Set, From, Test,
                           First, Last);
      end Find_Token;

      procedure Find_Token (Source : in Bounded_Wide_Wide_String;
                            Set    : in Wide_Wide_Maps.Wide_Wide_Character_Set;
                            Test   : in Membership;
                            First  : out Positive;
                            Last   : out Natural) is
      begin
         Wide_Wide_Fixed.Find_Token (Source.Data (1 .. Source.Length), Set, Test,
                           First, Last);
      end Find_Token;

      --  String translation subprograms

      function Translate (Source  : in Bounded_Wide_Wide_String;
                          Mapping : in Wide_Wide_Maps.Wide_Wide_Character_Mapping)
         return Bounded_Wide_Wide_String is
         Result : Bounded_Wide_Wide_String := Source;
      begin
         Wide_Wide_Fixed.Translate (Result.Data (1 .. Result.Length), Mapping);
         return Result;
      end Translate;

      procedure Translate (Source  : in out Bounded_Wide_Wide_String;
                           Mapping : in Wide_Wide_Maps.Wide_Wide_Character_Mapping) is
      begin
         Wide_Wide_Fixed.Translate (Source.Data (1 .. Source.Length), Mapping);
      end Translate;

      function Translate (Source  : in Bounded_Wide_Wide_String;
                          Mapping : in Wide_Wide_Maps.Wide_Wide_Character_Mapping_Function)
         return Bounded_Wide_Wide_String is
         Result : Bounded_Wide_Wide_String := Source;
      begin
         Wide_Wide_Fixed.Translate (Result.Data (1 .. Result.Length), Mapping);
         return Result;
      end Translate;

      procedure Translate (Source  : in out Bounded_Wide_Wide_String;
                           Mapping : in Wide_Wide_Maps.Wide_Wide_Character_Mapping_Function) is
      begin
         Wide_Wide_Fixed.Translate (Source.Data (1 .. Source.Length), Mapping);
      end Translate;

      --  String transformation subprograms

      function Replace_Slice (Source   : in Bounded_Wide_Wide_String;
                              Low      : in Positive;
                              High     : in Natural;
                              By       : in Wide_Wide_String;
                              Drop     : in Truncation := Error)
         return Bounded_Wide_Wide_String is
      begin
         return To_Bounded_Wide_Wide_String
           (Wide_Wide_Fixed.Replace_Slice (Source.Data (1 .. Source.Length), Low, High,
                                 By), Drop);
      end Replace_Slice;

      procedure Replace_Slice (Source   : in out Bounded_Wide_Wide_String;
                               Low      : in Positive;
                               High     : in Natural;
                               By       : in Wide_Wide_String;
                               Drop     : in Truncation := Error) is
      begin
         Source := Replace_Slice (Source, Low, High, By, Drop);
      end Replace_Slice;

      function Insert (Source   : in Bounded_Wide_Wide_String;
                       Before   : in Positive;
                       New_Item : in Wide_Wide_String;
                       Drop     : in Truncation := Error)
         return Bounded_Wide_Wide_String is
      begin
         return To_Bounded_Wide_Wide_String
           (Wide_Wide_Fixed.Insert (Source.Data (1 .. Source.Length), Before, New_Item),
            Drop);
      end Insert;

      procedure Insert (Source   : in out Bounded_Wide_Wide_String;
                        Before   : in Positive;
                        New_Item : in Wide_Wide_String;
                        Drop     : in Truncation := Error) is
      begin
         Source := Insert (Source, Before, New_Item, Drop);
      end Insert;

      function Overwrite (Source    : in Bounded_Wide_Wide_String;
                          Position  : in Positive;
                          New_Item  : in Wide_Wide_String;
                          Drop      : in Truncation := Error)
         return Bounded_Wide_Wide_String is
      begin
         return To_Bounded_Wide_Wide_String
           (Wide_Wide_Fixed.Overwrite (Source.Data (1 .. Source.Length), Position,
                             New_Item), Drop);
      end Overwrite;

      procedure Overwrite (Source    : in out Bounded_Wide_Wide_String;
                           Position  : in Positive;
                           New_Item  : in Wide_Wide_String;
                           Drop      : in Truncation := Error) is
      begin
         Source := Overwrite (Source, Position, New_Item, Drop);
      end Overwrite;

      function Delete (Source  : in Bounded_Wide_Wide_String;
                       From    : in Positive;
                       Through : in Natural)
         return Bounded_Wide_Wide_String is
      begin
         return To_Bounded_Wide_Wide_String
           (Wide_Wide_Fixed.Delete (Source.Data (1 .. Source.Length), From, Through));
      end Delete;

      procedure Delete (Source  : in out Bounded_Wide_Wide_String;
                        From    : in Positive;
                        Through : in Natural) is
      begin
         Source := Delete (Source, From, Through);
      end Delete;

      --  String selector subprograms

      function Trim (Source : in Bounded_Wide_Wide_String;
                     Side   : in Trim_End)
         return Bounded_Wide_Wide_String is
      begin
         return To_Bounded_Wide_Wide_String
           (Wide_Wide_Fixed.Trim (Source.Data (1 .. Source.Length), Side));
      end Trim;

      procedure Trim (Source : in out Bounded_Wide_Wide_String;
                      Side   : in Trim_End) is
      begin
         Source := Trim (Source, Side);
      end Trim;

      function Trim (Source : in Bounded_Wide_Wide_String;
                     Left   : in Wide_Wide_Maps.Wide_Wide_Character_Set;
                     Right  : in Wide_Wide_Maps.Wide_Wide_Character_Set)
         return Bounded_Wide_Wide_String is
      begin
         return To_Bounded_Wide_Wide_String
           (Wide_Wide_Fixed.Trim (Source.Data (1 .. Source.Length), Left, Right));
      end Trim;

      procedure Trim (Source : in out Bounded_Wide_Wide_String;
                      Left   : in Wide_Wide_Maps.Wide_Wide_Character_Set;
                      Right  : in Wide_Wide_Maps.Wide_Wide_Character_Set) is
      begin
         Source := Trim (Source, Left, Right);
      end Trim;

      function Head (Source : in Bounded_Wide_Wide_String;
                     Count  : in Natural;
                     Pad    : in Wide_Wide_Character  := Wide_Wide_Space;
                     Drop   : in Truncation := Error)
         return Bounded_Wide_Wide_String is
         --  To_Bounded_String (Fixed.Head (To_String (Source), Count, Pad),
         --  Drop), without making the fixed-length string
         Result : Bounded_Wide_Wide_String;
         Length : constant Natural := Source.Length;
      begin
         if Count <= Max_Length then
            Result.Length := Count;
            if Count <= Length then
               Result.Data (1 .. Count) := Source.Data (1 .. Count);
            else
               Result.Data (1 .. Length) := Source.Data (1 .. Length);
               Result.Data (Length + 1 .. Count) := (others => Pad);
            end if;
         else
            Result.Length := Max_Length;
            case Drop is
               when Right =>
                  Result.Data (1 .. Length) := Source.Data (1 .. Length);
                  Result.Data (Length + 1 .. Max_Length) := (others => Pad);
               when Left =>
                  --  the rightmost Max_Length characters of Source followed
                  --  by Count - Length Pad characters
                  declare
                     Pads : constant Natural := Count - Length;
                  begin
                     if Pads >= Max_Length then
                        Result.Data := (others => Pad);
                     else
                        Result.Data (1 .. Max_Length - Pads) :=
                          Source.Data (Length - (Max_Length - Pads) + 1
                                       .. Length);
                        Result.Data (Max_Length - Pads + 1 .. Max_Length) :=
                          (others => Pad);
                     end if;
                  end;
               when Error =>
                  raise Length_Error;
            end case;
         end if;
         return Result;
      end Head;

      procedure Head (Source : in out Bounded_Wide_Wide_String;
                      Count  : in Natural;
                      Pad    : in Wide_Wide_Character  := Wide_Wide_Space;
                      Drop   : in Truncation := Error) is
      begin
         Source := Head (Source, Count, Pad, Drop);
      end Head;

      function Tail (Source : in Bounded_Wide_Wide_String;
                     Count  : in Natural;
                     Pad    : in Wide_Wide_Character  := Wide_Wide_Space;
                     Drop   : in Truncation := Error)
         return Bounded_Wide_Wide_String is
         --  To_Bounded_String (Fixed.Tail (To_String (Source), Count, Pad),
         --  Drop), without making the fixed-length string
         Result : Bounded_Wide_Wide_String;
         Length : constant Natural := Source.Length;
      begin
         if Count <= Max_Length then
            Result.Length := Count;
            if Count <= Length then
               Result.Data (1 .. Count) :=
                 Source.Data (Length - Count + 1 .. Length);
            else
               Result.Data (1 .. Count - Length) := (others => Pad);
               Result.Data (Count - Length + 1 .. Count) :=
                 Source.Data (1 .. Length);
            end if;
         else
            Result.Length := Max_Length;
            case Drop is
               when Left =>
                  Result.Data (1 .. Max_Length - Length) := (others => Pad);
                  Result.Data (Max_Length - Length + 1 .. Max_Length) :=
                    Source.Data (1 .. Length);
               when Right =>
                  --  Count - Length Pad characters followed by Source, the
                  --  leftmost Max_Length characters of which are kept
                  declare
                     Pads : constant Natural := Count - Length;
                  begin
                     if Pads >= Max_Length then
                        Result.Data := (others => Pad);
                     else
                        Result.Data (1 .. Pads) := (others => Pad);
                        Result.Data (Pads + 1 .. Max_Length) :=
                          Source.Data (1 .. Max_Length - Pads);
                     end if;
                  end;
               when Error =>
                  raise Length_Error;
            end case;
         end if;
         return Result;
      end Tail;

      procedure Tail (Source : in out Bounded_Wide_Wide_String;
                      Count  : in Natural;
                      Pad    : in Wide_Wide_Character  := Wide_Wide_Space;
                      Drop   : in Truncation := Error) is
      begin
         Source := Tail (Source, Count, Pad, Drop);
      end Tail;

      --  String constructor subprograms

      function "*" (Left  : in Natural;
                    Right : in Wide_Wide_Character)
         return Bounded_Wide_Wide_String is
      begin
         return Replicate (Left, Right, Error);
      end "*";

      function "*" (Left  : in Natural;
                    Right : in Wide_Wide_String)
         return Bounded_Wide_Wide_String is
      begin
         return Replicate (Left, Right, Error);
      end "*";

      function "*" (Left  : in Natural;
                    Right : in Bounded_Wide_Wide_String)
         return Bounded_Wide_Wide_String is
      begin
         return Replicate (Left, Right.Data (1 .. Right.Length), Error);
      end "*";

      function Replicate (Count : in Natural;
                          Item  : in Wide_Wide_Character;
                          Drop  : in Truncation := Error)
         return Bounded_Wide_Wide_String is
         Result : Bounded_Wide_Wide_String;
      begin
         if Count > Max_Length then
            if Drop = Error then
               raise Length_Error;
            end if;
            Result.Length := Max_Length;
         else
            Result.Length := Count;
         end if;
         Result.Data (1 .. Result.Length) := (others => Item);
         return Result;
      end Replicate;

      function Replicate (Count : in Natural;
                          Item  : in Wide_Wide_String;
                          Drop  : in Truncation := Error)
         return Bounded_Wide_Wide_String is
         --  To_Bounded_String (Fixed."*" (Count, Item), Drop), without
         --  making the fixed-length string
         Result : Bounded_Wide_Wide_String;
         Length : constant Natural := Item'Length;
         Offset : Natural := 0;  --  the position in Item of Result.Data (1)
      begin
         if Length = 0 then
            return Null_Bounded_Wide_Wide_String;
         end if;
         if Count <= Max_Length / Length then
            Result.Length := Count * Length;
         else
            case Drop is
               when Error =>
                  raise Length_Error;
               when Right =>
                  null;
               when Left =>
                  --  the result ends at the end of a copy of Item
                  Offset := (Length - Max_Length mod Length) mod Length;
            end case;
            Result.Length := Max_Length;
         end if;
         for I in 1 .. Result.Length loop
            Result.Data (I) := Item (Item'First + (Offset + I - 1) mod Length);
         end loop;
         return Result;
      end Replicate;

      function Replicate (Count : in Natural;
                          Item  : in Bounded_Wide_Wide_String;
                          Drop  : in Truncation := Error)
         return Bounded_Wide_Wide_String is
      begin
         return Replicate (Count, Item.Data (1 .. Item.Length), Drop);
      end Replicate;

   end Generic_Bounded_Length;

end Ada.Strings.Wide_Wide_Bounded;
