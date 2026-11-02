--  Ada.Strings.Wide_Unbounded.adb
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
with Ada.Strings.Wide_Fixed;

package body Ada.Strings.Wide_Unbounded is

   --  An Unbounded_String's text is Reference (1 .. Last); the buffer
   --  (Reference) is null or has lower bound 1 and is owned by the object
   --  (Adjust copies it, Finalize frees it).

   procedure Deallocate is
     new Ada.Unchecked_Deallocation (Wide_String, Wide_String_Access);

   Empty : aliased Wide_String := "";

   function Buffer (Source : Unbounded_Wide_String) return Wide_String_Access is
   begin
      if Source.Reference = null then
         return Empty'Access;
      else
         return Source.Reference;
      end if;
   end Buffer;

   function Make (Length : Natural) return Unbounded_Wide_String is
      --  an Unbounded_String of length Length (its characters undefined)
   begin
      if Length = 0 then
         return Null_Unbounded_Wide_String;
      end if;
      return (Ada.Finalization.Controlled with
              Reference => new Wide_String (1 .. Length), Last => Length);
   end Make;

   procedure Append_Characters (Source   : in out Unbounded_Wide_String;
                                New_Item : in Wide_String) is
      --  (New_Item can be a slice of Source's buffer)
      New_Last : constant Natural := Source.Last + New_Item'Length;
   begin
      if New_Item'Length = 0 then
         return;
      end if;
      if Source.Reference = null or else New_Last > Source.Reference'Length
      then
         declare
            Old_Capacity : Natural := 0;
            Capacity     : Natural;
            Old          : Wide_String_Access := Source.Reference;
         begin
            if Old /= null then
               Old_Capacity := Old'Length;
            end if;
            if Old_Capacity >= Natural'Last / 2 then
               Capacity := Natural'Last;
            else
               Capacity := Natural'Max (2 * Old_Capacity, 16);
            end if;
            Capacity := Natural'Max (Capacity, New_Last);
            Source.Reference := new Wide_String (1 .. Capacity);
            if Old /= null then
               Source.Reference (1 .. Source.Last) := Old (1 .. Source.Last);
            end if;
            Source.Reference (Source.Last + 1 .. New_Last) := New_Item;
            Deallocate (Old);
         end;
      else
         Source.Reference (Source.Last + 1 .. New_Last) := New_Item;
      end if;
      Source.Last := New_Last;
   end Append_Characters;

   overriding procedure Adjust (Object : in out Unbounded_Wide_String) is
   begin
      if Object.Reference /= null then
         if Object.Last = 0 then
            Object.Reference := null;
         else
            Object.Reference :=
              new Wide_String'(Object.Reference (1 .. Object.Last));
         end if;
      end if;
   end Adjust;

   overriding procedure Finalize (Object : in out Unbounded_Wide_String) is
   begin
      Deallocate (Object.Reference);
      Object.Last := 0;
   end Finalize;

   procedure Read
     (Stream : not null access Ada.Streams.Root_Stream_Type'Class;
      Item   : out Unbounded_Wide_String) is
   begin
      Set_Unbounded_Wide_String (Item, Wide_String'Input (Stream));
   end Read;

   procedure Write
     (Stream : not null access Ada.Streams.Root_Stream_Type'Class;
      Item   : in Unbounded_Wide_String) is
   begin
      Wide_String'Output (Stream, Buffer (Item) (1 .. Item.Last));
   end Write;

   function Length (Source : in Unbounded_Wide_String) return Natural is
   begin
      return Source.Last;
   end Length;

   procedure Free (X : in out Wide_String_Access) is
   begin
      Deallocate (X);
   end Free;

   --  Conversion, Concatenation, and Selection functions

   function To_Unbounded_Wide_String (Source : in Wide_String)
      return Unbounded_Wide_String is
      Result : Unbounded_Wide_String := Make (Source'Length);
   begin
      if Source'Length /= 0 then
         Result.Reference.all := Source;
      end if;
      return Result;
   end To_Unbounded_Wide_String;

   function To_Unbounded_Wide_String (Length : in Natural)
      return Unbounded_Wide_String is
   begin
      return Make (Length);
   end To_Unbounded_Wide_String;

   function To_Wide_String (Source : in Unbounded_Wide_String) return Wide_String is
   begin
      return Buffer (Source) (1 .. Source.Last);
   end To_Wide_String;

   procedure Set_Unbounded_Wide_String
     (Target :    out Unbounded_Wide_String;
      Source : in     Wide_String) is
   begin
      if Target.Reference /= null
        and then Target.Reference'Length >= Source'Length
      then
         Target.Reference (1 .. Source'Length) := Source;
         Target.Last := Source'Length;
      else
         Target := To_Unbounded_Wide_String (Source);
      end if;
   end Set_Unbounded_Wide_String;

   procedure Append (Source   : in out Unbounded_Wide_String;
                     New_Item : in Unbounded_Wide_String) is
   begin
      Append_Characters (Source, Buffer (New_Item) (1 .. New_Item.Last));
   end Append;

   procedure Append (Source   : in out Unbounded_Wide_String;
                     New_Item : in Wide_String) is
   begin
      Append_Characters (Source, New_Item);
   end Append;

   procedure Append (Source   : in out Unbounded_Wide_String;
                     New_Item : in Wide_Character) is
   begin
      Append_Characters (Source, (1 => New_Item));
   end Append;

   function Concatenate (Left, Right : Wide_String) return Unbounded_Wide_String is
      Result : Unbounded_Wide_String := Make (Left'Length + Right'Length);
   begin
      if Result.Last /= 0 then
         Result.Reference (1 .. Left'Length) := Left;
         Result.Reference (Left'Length + 1 .. Result.Last) := Right;
      end if;
      return Result;
   end Concatenate;

   function "&" (Left, Right : in Unbounded_Wide_String)
      return Unbounded_Wide_String is
   begin
      return Concatenate (Buffer (Left) (1 .. Left.Last),
                          Buffer (Right) (1 .. Right.Last));
   end "&";

   function "&" (Left : in Unbounded_Wide_String; Right : in Wide_String)
      return Unbounded_Wide_String is
   begin
      return Concatenate (Buffer (Left) (1 .. Left.Last), Right);
   end "&";

   function "&" (Left : in Wide_String; Right : in Unbounded_Wide_String)
      return Unbounded_Wide_String is
   begin
      return Concatenate (Left, Buffer (Right) (1 .. Right.Last));
   end "&";

   function "&" (Left : in Unbounded_Wide_String; Right : in Wide_Character)
      return Unbounded_Wide_String is
   begin
      return Concatenate (Buffer (Left) (1 .. Left.Last), (1 => Right));
   end "&";

   function "&" (Left : in Wide_Character; Right : in Unbounded_Wide_String)
      return Unbounded_Wide_String is
   begin
      return Concatenate ((1 => Left), Buffer (Right) (1 .. Right.Last));
   end "&";

   function Element (Source : in Unbounded_Wide_String;
                     Index  : in Positive)
      return Wide_Character is
   begin
      if Index > Source.Last then
         raise Index_Error;
      end if;
      return Source.Reference (Index);
   end Element;

   procedure Replace_Element (Source : in out Unbounded_Wide_String;
                              Index  : in Positive;
                              By     : in Wide_Character) is
   begin
      if Index > Source.Last then
         raise Index_Error;
      end if;
      Source.Reference (Index) := By;
   end Replace_Element;

   function Slice (Source : in Unbounded_Wide_String;
                   Low    : in Positive;
                   High   : in Natural)
      return Wide_String is
   begin
      if Low > Source.Last + 1 or else High > Source.Last then
         raise Index_Error;
      end if;
      return Buffer (Source) (Low .. High);
   end Slice;

   function Unbounded_Slice
      (Source : in Unbounded_Wide_String;
       Low    : in Positive;
       High   : in Natural)
          return Unbounded_Wide_String is
   begin
      return To_Unbounded_Wide_String (Slice (Source, Low, High));
   end Unbounded_Slice;

   procedure Unbounded_Slice
      (Source : in     Unbounded_Wide_String;
       Target :    out Unbounded_Wide_String;
       Low    : in     Positive;
       High   : in     Natural) is
   begin
      Set_Unbounded_Wide_String (Target, Slice (Source, Low, High));
   end Unbounded_Slice;

   --  Comparisons

   function "=" (Left, Right : in Unbounded_Wide_String) return Boolean is
   begin
      return Left.Last = Right.Last
        and then Buffer (Left) (1 .. Left.Last)
                   = Buffer (Right) (1 .. Right.Last);
   end "=";

   function "=" (Left : in Unbounded_Wide_String; Right : in Wide_String)
     return Boolean is
   begin
      return Buffer (Left) (1 .. Left.Last) = Right;
   end "=";

   function "=" (Left : in Wide_String; Right : in Unbounded_Wide_String)
     return Boolean is
   begin
      return Left = Buffer (Right) (1 .. Right.Last);
   end "=";

   function "<" (Left, Right : in Unbounded_Wide_String) return Boolean is
   begin
      return Buffer (Left) (1 .. Left.Last) < Buffer (Right) (1 .. Right.Last);
   end "<";

   function "<" (Left : in Unbounded_Wide_String; Right : in Wide_String)
     return Boolean is
   begin
      return Buffer (Left) (1 .. Left.Last) < Right;
   end "<";

   function "<" (Left : in Wide_String; Right : in Unbounded_Wide_String)
     return Boolean is
   begin
      return Left < Buffer (Right) (1 .. Right.Last);
   end "<";

   function "<=" (Left, Right : in Unbounded_Wide_String) return Boolean is
   begin
      return Buffer (Left) (1 .. Left.Last)
               <= Buffer (Right) (1 .. Right.Last);
   end "<=";

   function "<=" (Left : in Unbounded_Wide_String; Right : in Wide_String)
     return Boolean is
   begin
      return Buffer (Left) (1 .. Left.Last) <= Right;
   end "<=";

   function "<=" (Left : in Wide_String; Right : in Unbounded_Wide_String)
     return Boolean is
   begin
      return Left <= Buffer (Right) (1 .. Right.Last);
   end "<=";

   function ">" (Left, Right : in Unbounded_Wide_String) return Boolean is
   begin
      return Buffer (Left) (1 .. Left.Last) > Buffer (Right) (1 .. Right.Last);
   end ">";

   function ">" (Left : in Unbounded_Wide_String; Right : in Wide_String)
     return Boolean is
   begin
      return Buffer (Left) (1 .. Left.Last) > Right;
   end ">";

   function ">" (Left : in Wide_String; Right : in Unbounded_Wide_String)
     return Boolean is
   begin
      return Left > Buffer (Right) (1 .. Right.Last);
   end ">";

   function ">=" (Left, Right : in Unbounded_Wide_String) return Boolean is
   begin
      return Buffer (Left) (1 .. Left.Last)
               >= Buffer (Right) (1 .. Right.Last);
   end ">=";

   function ">=" (Left : in Unbounded_Wide_String; Right : in Wide_String)
     return Boolean is
   begin
      return Buffer (Left) (1 .. Left.Last) >= Right;
   end ">=";

   function ">=" (Left : in Wide_String; Right : in Unbounded_Wide_String)
     return Boolean is
   begin
      return Left >= Buffer (Right) (1 .. Right.Last);
   end ">=";

   --  Search subprograms

   function Index (Source  : in Unbounded_Wide_String;
                   Pattern : in Wide_String;
                   From    : in Positive;
                   Going   : in Direction := Forward;
                   Mapping : in Wide_Maps.Wide_Character_Mapping := Wide_Maps.Identity)
      return Natural is
   begin
      return Wide_Fixed.Index (Buffer (Source) (1 .. Source.Last), Pattern, From,
                          Going, Mapping);
   end Index;

   function Index (Source  : in Unbounded_Wide_String;
                   Pattern : in Wide_String;
                   From    : in Positive;
                   Going   : in Direction := Forward;
                   Mapping : in Wide_Maps.Wide_Character_Mapping_Function)
      return Natural is
   begin
      return Wide_Fixed.Index (Buffer (Source) (1 .. Source.Last), Pattern, From,
                          Going, Mapping);
   end Index;

   function Index (Source   : in Unbounded_Wide_String;
                   Pattern  : in Wide_String;
                   Going    : in Direction := Forward;
                   Mapping  : in Wide_Maps.Wide_Character_Mapping := Wide_Maps.Identity)
      return Natural is
   begin
      return Wide_Fixed.Index (Buffer (Source) (1 .. Source.Last), Pattern, Going,
                          Mapping);
   end Index;

   function Index (Source   : in Unbounded_Wide_String;
                   Pattern  : in Wide_String;
                   Going    : in Direction := Forward;
                   Mapping  : in Wide_Maps.Wide_Character_Mapping_Function)
      return Natural is
   begin
      return Wide_Fixed.Index (Buffer (Source) (1 .. Source.Last), Pattern, Going,
                          Mapping);
   end Index;

   function Index (Source  : in Unbounded_Wide_String;
                   Set     : in Wide_Maps.Wide_Character_Set;
                   From    : in Positive;
                   Test    : in Membership := Inside;
                   Going    : in Direction := Forward)
      return Natural is
   begin
      return Wide_Fixed.Index (Buffer (Source) (1 .. Source.Last), Set, From, Test,
                          Going);
   end Index;

   function Index (Source : in Unbounded_Wide_String;
                   Set    : in Wide_Maps.Wide_Character_Set;
                   Test   : in Membership := Inside;
                   Going  : in Direction  := Forward) return Natural is
   begin
      return Wide_Fixed.Index (Buffer (Source) (1 .. Source.Last), Set, Test,
                          Going);
   end Index;

   function Index_Non_Blank (Source : in Unbounded_Wide_String;
                             From   : in Positive;
                             Going  : in Direction := Forward)
      return Natural is
   begin
      return Wide_Fixed.Index_Non_Blank (Buffer (Source) (1 .. Source.Last), From,
                                    Going);
   end Index_Non_Blank;

   function Index_Non_Blank (Source : in Unbounded_Wide_String;
                             Going  : in Direction := Forward)
      return Natural is
   begin
      return Wide_Fixed.Index_Non_Blank (Buffer (Source) (1 .. Source.Last),
                                    Going);
   end Index_Non_Blank;

   function Count (Source   : in Unbounded_Wide_String;
                   Pattern  : in Wide_String;
                   Mapping  : in Wide_Maps.Wide_Character_Mapping := Wide_Maps.Identity)
      return Natural is
   begin
      return Wide_Fixed.Count (Buffer (Source) (1 .. Source.Last), Pattern,
                          Mapping);
   end Count;

   function Count (Source   : in Unbounded_Wide_String;
                   Pattern  : in Wide_String;
                   Mapping  : in Wide_Maps.Wide_Character_Mapping_Function)
      return Natural is
   begin
      return Wide_Fixed.Count (Buffer (Source) (1 .. Source.Last), Pattern,
                          Mapping);
   end Count;

   function Count (Source   : in Unbounded_Wide_String;
                   Set      : in Wide_Maps.Wide_Character_Set)
      return Natural is
   begin
      return Wide_Fixed.Count (Buffer (Source) (1 .. Source.Last), Set);
   end Count;

   procedure Find_Token (Source : in Unbounded_Wide_String;
                         Set    : in Wide_Maps.Wide_Character_Set;
                         From   : in Positive;
                         Test   : in Membership;
                         First  : out Positive;
                         Last   : out Natural) is
   begin
      Wide_Fixed.Find_Token (Buffer (Source) (1 .. Source.Last), Set, From, Test,
                        First, Last);
   end Find_Token;

   procedure Find_Token (Source : in Unbounded_Wide_String;
                         Set    : in Wide_Maps.Wide_Character_Set;
                         Test   : in Membership;
                         First  : out Positive;
                         Last   : out Natural) is
   begin
      Wide_Fixed.Find_Token (Buffer (Source) (1 .. Source.Last), Set, Test,
                        First, Last);
   end Find_Token;

   --  String translation subprograms

   function Translate (Source  : in Unbounded_Wide_String;
                       Mapping : in Wide_Maps.Wide_Character_Mapping)
      return Unbounded_Wide_String is
   begin
      return To_Unbounded_Wide_String
        (Wide_Fixed.Translate (Buffer (Source) (1 .. Source.Last), Mapping));
   end Translate;

   procedure Translate (Source  : in out Unbounded_Wide_String;
                        Mapping : in Wide_Maps.Wide_Character_Mapping) is
   begin
      Wide_Fixed.Translate (Buffer (Source) (1 .. Source.Last), Mapping);
   end Translate;

   function Translate (Source  : in Unbounded_Wide_String;
                       Mapping : in Wide_Maps.Wide_Character_Mapping_Function)
      return Unbounded_Wide_String is
   begin
      return To_Unbounded_Wide_String
        (Wide_Fixed.Translate (Buffer (Source) (1 .. Source.Last), Mapping));
   end Translate;

   procedure Translate (Source  : in out Unbounded_Wide_String;
                        Mapping : in Wide_Maps.Wide_Character_Mapping_Function) is
   begin
      Wide_Fixed.Translate (Buffer (Source) (1 .. Source.Last), Mapping);
   end Translate;

   --  String transformation subprograms

   function Replace_Slice (Source   : in Unbounded_Wide_String;
                           Low      : in Positive;
                           High     : in Natural;
                           By       : in Wide_String)
      return Unbounded_Wide_String is
   begin
      return To_Unbounded_Wide_String
        (Wide_Fixed.Replace_Slice (Buffer (Source) (1 .. Source.Last), Low, High,
                              By));
   end Replace_Slice;

   procedure Replace_Slice (Source   : in out Unbounded_Wide_String;
                            Low      : in Positive;
                            High     : in Natural;
                            By       : in Wide_String) is
   begin
      Set_Unbounded_Wide_String
        (Source, Wide_Fixed.Replace_Slice (Buffer (Source) (1 .. Source.Last), Low,
                                      High, By));
   end Replace_Slice;

   function Insert (Source   : in Unbounded_Wide_String;
                    Before   : in Positive;
                    New_Item : in Wide_String)
      return Unbounded_Wide_String is
   begin
      return To_Unbounded_Wide_String
        (Wide_Fixed.Insert (Buffer (Source) (1 .. Source.Last), Before, New_Item));
   end Insert;

   procedure Insert (Source   : in out Unbounded_Wide_String;
                     Before   : in Positive;
                     New_Item : in Wide_String) is
   begin
      Set_Unbounded_Wide_String
        (Source, Wide_Fixed.Insert (Buffer (Source) (1 .. Source.Last), Before,
                               New_Item));
   end Insert;

   function Overwrite (Source    : in Unbounded_Wide_String;
                       Position  : in Positive;
                       New_Item  : in Wide_String)
      return Unbounded_Wide_String is
   begin
      return To_Unbounded_Wide_String
        (Wide_Fixed.Overwrite (Buffer (Source) (1 .. Source.Last), Position,
                          New_Item));
   end Overwrite;

   procedure Overwrite (Source    : in out Unbounded_Wide_String;
                        Position  : in Positive;
                        New_Item  : in Wide_String) is
   begin
      Set_Unbounded_Wide_String
        (Source, Wide_Fixed.Overwrite (Buffer (Source) (1 .. Source.Last),
                                  Position, New_Item));
   end Overwrite;

   function Delete (Source  : in Unbounded_Wide_String;
                    From    : in Positive;
                    Through : in Natural)
      return Unbounded_Wide_String is
   begin
      return To_Unbounded_Wide_String
        (Wide_Fixed.Delete (Buffer (Source) (1 .. Source.Last), From, Through));
   end Delete;

   procedure Delete (Source  : in out Unbounded_Wide_String;
                     From    : in Positive;
                     Through : in Natural) is
   begin
      Set_Unbounded_Wide_String
        (Source, Wide_Fixed.Delete (Buffer (Source) (1 .. Source.Last), From,
                               Through));
   end Delete;

   function Trim (Source : in Unbounded_Wide_String;
                  Side   : in Trim_End)
      return Unbounded_Wide_String is
   begin
      return To_Unbounded_Wide_String
        (Wide_Fixed.Trim (Buffer (Source) (1 .. Source.Last), Side));
   end Trim;

   procedure Trim (Source : in out Unbounded_Wide_String;
                   Side   : in Trim_End) is
   begin
      Set_Unbounded_Wide_String
        (Source, Wide_Fixed.Trim (Buffer (Source) (1 .. Source.Last), Side));
   end Trim;

   function Trim (Source : in Unbounded_Wide_String;
                  Left   : in Wide_Maps.Wide_Character_Set;
                  Right  : in Wide_Maps.Wide_Character_Set)
      return Unbounded_Wide_String is
   begin
      return To_Unbounded_Wide_String
        (Wide_Fixed.Trim (Buffer (Source) (1 .. Source.Last), Left, Right));
   end Trim;

   procedure Trim (Source : in out Unbounded_Wide_String;
                   Left   : in Wide_Maps.Wide_Character_Set;
                   Right  : in Wide_Maps.Wide_Character_Set) is
   begin
      Set_Unbounded_Wide_String
        (Source, Wide_Fixed.Trim (Buffer (Source) (1 .. Source.Last), Left,
                             Right));
   end Trim;

   function Head (Source : in Unbounded_Wide_String;
                  Count  : in Natural;
                  Pad    : in Wide_Character := Wide_Space)
      return Unbounded_Wide_String is
   begin
      return To_Unbounded_Wide_String
        (Wide_Fixed.Head (Buffer (Source) (1 .. Source.Last), Count, Pad));
   end Head;

   procedure Head (Source : in out Unbounded_Wide_String;
                   Count  : in Natural;
                   Pad    : in Wide_Character := Wide_Space) is
   begin
      Set_Unbounded_Wide_String
        (Source, Wide_Fixed.Head (Buffer (Source) (1 .. Source.Last), Count, Pad));
   end Head;

   function Tail (Source : in Unbounded_Wide_String;
                  Count  : in Natural;
                  Pad    : in Wide_Character := Wide_Space)
      return Unbounded_Wide_String is
   begin
      return To_Unbounded_Wide_String
        (Wide_Fixed.Tail (Buffer (Source) (1 .. Source.Last), Count, Pad));
   end Tail;

   procedure Tail (Source : in out Unbounded_Wide_String;
                   Count  : in Natural;
                   Pad    : in Wide_Character := Wide_Space) is
   begin
      Set_Unbounded_Wide_String
        (Source, Wide_Fixed.Tail (Buffer (Source) (1 .. Source.Last), Count, Pad));
   end Tail;

   function "*" (Left  : in Natural;
                 Right : in Wide_Character)
      return Unbounded_Wide_String is
      Result : Unbounded_Wide_String := Make (Left);
   begin
      for I in 1 .. Left loop
         Result.Reference (I) := Right;
      end loop;
      return Result;
   end "*";

   function "*" (Left  : in Natural;
                 Right : in Wide_String)
      return Unbounded_Wide_String is
      Result : Unbounded_Wide_String := Make (Left * Right'Length);
      Next   : Positive := 1;
   begin
      if Right'Length /= 0 then
         for I in 1 .. Left loop
            Result.Reference (Next .. Next + Right'Length - 1) := Right;
            Next := Next + Right'Length;
         end loop;
      end if;
      return Result;
   end "*";

   function "*" (Left  : in Natural;
                 Right : in Unbounded_Wide_String)
      return Unbounded_Wide_String is
   begin
      return Left * Buffer (Right) (1 .. Right.Last);
   end "*";

end Ada.Strings.Wide_Unbounded;
