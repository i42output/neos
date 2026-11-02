--  Ada.Wide_Text_IO.Editing.adb
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

with Interfaces;

package body Ada.Wide_Text_IO.Editing is

   package Pictures renames Ada.Text_IO_Pictures;

   use type Interfaces.Integer_64;
   use type Interfaces.Unsigned_64;

   function Valid (Pic_String      : in String;
                   Blank_When_Zero : in Boolean := False) return Boolean
   is
      Expanded : Pictures.Picture_String;
      Length   : Natural;
      Result   : Boolean;
   begin
      Pictures.Expand (Pic_String, Blank_When_Zero, Expanded, Length, Result);
      return Result;
   end Valid;

   function To_Picture (Pic_String      : in String;
                        Blank_When_Zero : in Boolean := False)
      return Picture
   is
      Result : Picture;
      Ok     : Boolean;
   begin
      Pictures.Expand
        (Pic_String, Blank_When_Zero, Result.Expanded, Result.Length, Ok);
      if not Ok then
         raise Picture_Error;
      end if;
      Result.Blank_When_Zero := Blank_When_Zero;
      return Result;
   end To_Picture;

   function Pic_String      (Pic : in Picture) return String is
   begin
      return Pic.Expanded (1 .. Pic.Length);
   end Pic_String;

   function Blank_When_Zero (Pic : in Picture) return Boolean is
   begin
      return Pic.Blank_When_Zero;
   end Blank_When_Zero;

   function To_WW (Item : Wide_String) return Wide_Wide_String is
      Result : Wide_Wide_String (1 .. Item'Length);
   begin
      for I in Item'Range loop
         Result (I - Item'First + 1) :=
            Wide_Wide_Character'Val (Wide_Character'Pos (Item (I)));
      end loop;
      return Result;
   end To_WW;

   function From_WW (Item : Wide_Wide_String) return Wide_String is
      Result : Wide_String (1 .. Item'Length);
   begin
      for I in Item'Range loop
         Result (I - Item'First + 1) :=
            Wide_Character'Val (Wide_Wide_Character'Pos (Item (I)));
      end loop;
      return Result;
   end From_WW;

   package body Decimal_Output is

      function Length (Pic      : in Picture;
                       Currency : in Wide_String := Default_Currency)
         return Natural is
      begin
         return Pictures.Output_Length
                  (Pic.Expanded (1 .. Pic.Length), Currency'Length);
      end Length;

      function Valid (Item     : in Num;
                      Pic      : in Picture;
                      Currency : in Wide_String := Default_Currency)
         return Boolean is
      begin
         declare
            Ignored : constant Wide_String := Image (Item, Pic, Currency);
         begin
            return True;
         end;
      exception
         when Layout_Error =>
            return False;
      end Valid;

      function Image (Item       : in Num;
                      Pic        : in Picture;
                      Currency   : in Wide_String    := Default_Currency;
                      Fill       : in Wide_Character := Default_Fill;
                      Separator  : in Wide_Character := Default_Separator;
                      Radix_Mark : in Wide_Character := Default_Radix_Mark)
         return Wide_String
      is
         Mantissa  : constant Interfaces.Integer_64 :=
            Interfaces.Integer_64 (Item / Num'Base'(Num'Small));
         Magnitude : constant Interfaces.Unsigned_64 :=
            (if Mantissa = Interfaces.Integer_64'First then 2 ** 63
             else Interfaces.Unsigned_64 (abs Mantissa));
         Image_Text : constant String :=
            Interfaces.Unsigned_64'Image (Magnitude);
      begin
         if Currency'Length = 0 then
            raise Constraint_Error;
         end if;
         return From_WW
                  (Pictures.Image
                     (Pic             => Pic.Expanded (1 .. Pic.Length),
                      Blank_When_Zero => Pic.Blank_When_Zero,
                      Negative        => Mantissa < 0,
                      Magnitude       =>
                         Image_Text (Image_Text'First + 1 .. Image_Text'Last),
                      Scale           => Num'Scale,
                      Currency        => To_WW (Currency),
                      Fill            => To_WW ((1 => Fill)) (1),
                      Separator       => To_WW ((1 => Separator)) (1),
                      Radix_Mark      => To_WW ((1 => Radix_Mark)) (1)));
      end Image;

      procedure Put (File       : in File_Type;
                     Item       : in Num;
                     Pic        : in Picture;
                     Currency   : in Wide_String    := Default_Currency;
                     Fill       : in Wide_Character := Default_Fill;
                     Separator  : in Wide_Character := Default_Separator;
                     Radix_Mark : in Wide_Character := Default_Radix_Mark)
      is
         Text : constant Wide_String :=
            Image (Item, Pic, Currency, Fill, Separator, Radix_Mark);
      begin
         Text_IO_Engine.Put_Item
           (File.File, Text_IO_Engine.Encode (To_WW (Text)), Text'Length);
      end Put;

      procedure Put (Item       : in Num;
                     Pic        : in Picture;
                     Currency   : in Wide_String    := Default_Currency;
                     Fill       : in Wide_Character := Default_Fill;
                     Separator  : in Wide_Character := Default_Separator;
                     Radix_Mark : in Wide_Character := Default_Radix_Mark) is
      begin
         Put (Current_Output, Item, Pic, Currency, Fill, Separator,
              Radix_Mark);
      end Put;

      procedure Put (To         : out Wide_String;
                     Item       : in Num;
                     Pic        : in Picture;
                     Currency   : in Wide_String    := Default_Currency;
                     Fill       : in Wide_Character := Default_Fill;
                     Separator  : in Wide_Character := Default_Separator;
                     Radix_Mark : in Wide_Character := Default_Radix_Mark)
      is
         Text : constant Wide_String :=
            Image (Item, Pic, Currency, Fill, Separator, Radix_Mark);
      begin
         if Text'Length > To'Length then
            raise Layout_Error;
         end if;
         To := (1 .. To'Length - Text'Length => ' ') & Text;
      end Put;

   end Decimal_Output;

end Ada.Wide_Text_IO.Editing;
