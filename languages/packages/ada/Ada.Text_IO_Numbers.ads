--  Ada.Text_IO_Numbers.ads
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

--  The input and output of numeric and enumeration literals for Ada.Text_IO,
--  Ada.Wide_Text_IO and Ada.Wide_Wide_Text_IO (A.10.6 .. A.10.10): the
--  scanning of literals (from files and strings), their conversion to
--  values, and the images of values. Numeric values are carried in the
--  widest types: Interfaces.Integer_64 and Unsigned_64, Long_Float, and
--  for fixed point types the integer mantissa (the value divided by the
--  type's small).

with Interfaces;
with Ada.Text_IO_Engine;

private package Ada.Text_IO_Numbers is

   subtype Int is Interfaces.Integer_64;
   subtype Uns is Interfaces.Unsigned_64;

   type Literal_Kind is
     (Signed_Literal,    --  an optionally signed integer literal
      Unsigned_Literal,  --  an integer literal with an optional plus sign
      Real_Literal,      --  the real literals of A.10.9(14 .. 18)
      Enumeration_Literal);  --  an identifier or a character literal

   --  Scanning: the longest initial sequence of a literal of the kind.

   --  The literal read from File (A.10.6(5 .. 6)): Width zero: after the
   --  skipping of blanks and terminators (End_Error at the end of the file),
   --  the characters as long as they are an initial part of a literal;
   --  otherwise exactly Width characters (or those up to a line terminator).
   --  Data_Error if they are not a literal (but blanks, see Get_Literal
   --  below, those leading and those following it when Width is given).
   function Get_Literal
     (File  : Text_IO_Engine.File_Access;
      Kind  : Literal_Kind;
      Width : Natural := 0) return Wide_Wide_String;

   --  The literal at the start of From (after blanks; End_Error if there
   --  are only blanks): From (First .. Last); Data_Error if none.
   procedure Get_Literal
     (From  : Wide_Wide_String;
      Kind  : Literal_Kind;
      First : out Positive;
      Last  : out Natural);

   function To_Wide_Wide (Item : String) return Wide_Wide_String;
   function To_Wide_Wide (Item : Wide_String) return Wide_Wide_String;
   function To_String (Item : Wide_Wide_String) return String;
   --  (the characters that are not Latin-1 become NUL characters)

   --  Conversion of a literal to a value (Data_Error if it is malformed or
   --  its value is too large)

   procedure Integer_Value
     (Literal  : String;
      Value    : out Uns;
      Negative : out Boolean);

   function Real_Value (Literal : String) return Long_Float;

   --  The small of a fixed point type: Power_Of_Two: 2.0 ** Exponent;
   --  Decimal_Fraction: Factor * 10.0 ** (-Exponent); otherwise only
   --  approximately Approximation.
   type Small_Kind is (Power_Of_Two, Decimal_Fraction, Other);

   type Small_Description is record
      Kind          : Small_Kind := Other;
      Factor        : Int := 1;
      Exponent      : Integer := 0;
      Approximation : Long_Float := 1.0;
   end record;

   function Describe (Small : Long_Float) return Small_Description;
   function Decimal_Small (Scale : Integer) return Small_Description;
   --  (the small 10.0 ** (-Scale) of a decimal type)

   --  The mantissa of the value of a real literal (rounded to the nearest,
   --  halves away from zero; truncated towards zero if Truncate).
   function Fixed_Value
     (Literal  : String;
      Small    : Small_Description;
      Truncate : Boolean) return Int;

   --  Images

   function Integer_Image (Value : Int; Base : Positive) return String;
   function Unsigned_Image (Value : Uns; Base : Positive) return String;

   --  The image of a real value with the format Fore . Aft [E Exp]
   --  (A.10.9(22 .. 26)): Float_Image: Max_Digits is the number of
   --  significant digits beyond which the digits output are zeros;
   --  Fixed_Image: the value is Mantissa * Small.
   function Float_Image
     (Item       : Long_Float;
      Fore       : Natural;
      Aft        : Natural;
      Exp        : Natural;
      Max_Digits : Positive) return String;

   function Fixed_Image
     (Mantissa : Int;
      Small    : Small_Description;
      Fore     : Natural;
      Aft      : Natural;
      Exp      : Natural) return String;

   --  An enumeration literal's image (Latin-1) in lower case if Lower,
   --  followed by spaces up to Width.
   function Enumeration_Image
     (Image : String; Width : Natural; Lower : Boolean) return String;

   --  Output of an image of Length characters (A.10.6(7 .. 8)).
   procedure Put_Image
     (File   : Text_IO_Engine.File_Access;
      Image  : String;
      Length : Natural);

   --  Output to a string: Image right-justified in To (Layout_Error if it
   --  is too long); Left: left-justified (enumeration literals).
   procedure Put_Image
     (To    : out String;
      Image : String;
      Left  : Boolean := False);

end Ada.Text_IO_Numbers;
