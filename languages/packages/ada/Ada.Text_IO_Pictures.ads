--  Ada.Text_IO_Pictures.ads
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

--  The picture strings and edited output of Ada.Text_IO.Editing,
--  Ada.Wide_Text_IO.Editing and Ada.Wide_Wide_Text_IO.Editing (F.3).

private package Ada.Text_IO_Pictures is
   pragma Preelaborate;

   Max_Picture_Length : constant := 64;

   subtype Picture_String is String (1 .. Max_Picture_Length);

   --  The expansion of Pic (F.3.1(48 .. 51)) in upper case, padded with
   --  spaces, and its length; Valid: whether Pic is a well-formed picture
   --  string whose expansion is at most Max_Picture_Length long (and
   --  has no '*' if Blank_When_Zero).
   procedure Expand
     (Pic             : String;
      Blank_When_Zero : Boolean;
      Expanded        : out Picture_String;
      Length          : out Natural;
      Valid           : out Boolean);

   --  The length of the edited output for an expanded picture string.
   function Output_Length
     (Pic : String; Currency_Length : Natural) return Natural;

   --  The edited output (F.3.2) of a decimal value: its magnitude is
   --  Magnitude * 10 ** (-Scale) (Magnitude: decimal digits); Layout_Error
   --  if the rules identify a layout error.
   function Image
     (Pic             : String;
      Blank_When_Zero : Boolean;
      Negative        : Boolean;
      Magnitude       : String;
      Scale           : Integer;
      Currency        : Wide_Wide_String;
      Fill            : Wide_Wide_Character;
      Separator       : Wide_Wide_Character;
      Radix_Mark      : Wide_Wide_Character) return Wide_Wide_String;

end Ada.Text_IO_Pictures;
