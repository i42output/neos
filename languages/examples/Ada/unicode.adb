--  Unicode: Wide_Wide_String (code points), UTF-8 encoding and decoding, lengths in code points and in UTF-8 and
--  UTF-16 code units, and case mapping of non-ASCII characters

with Ada.Text_IO; use Ada.Text_IO;
with Ada.Strings.UTF_Encoding; use Ada.Strings.UTF_Encoding;
with Ada.Strings.UTF_Encoding.Wide_Wide_Strings; use Ada.Strings.UTF_Encoding.Wide_Wide_Strings;
with Ada.Strings.UTF_Encoding.Conversions;
with Ada.Wide_Wide_Characters.Handling; use Ada.Wide_Wide_Characters.Handling;

procedure Unicode is

   A : constant Wide_Wide_String := "h" & Wide_Wide_Character'Val (16#E9#) & "llo";
   B : constant Wide_Wide_String := Wide_Wide_Character'Val (16#65E5#) & Wide_Wide_Character'Val (16#672C#) &
                                    Wide_Wide_Character'Val (16#8A9E#);
   C : constant Wide_Wide_String := Wide_Wide_Character'Val (16#1F600#) & "x";

   procedure Show (S : Wide_Wide_String) is
      Encoded : constant UTF_8_String := Encode (S);
      UTF_16 : constant UTF_16_Wide_String := Ada.Strings.UTF_Encoding.Conversions.Convert (Encoded);
   begin
      Put_Line (Encoded & ":" & Natural'Image (S'Length) & " code points," & Natural'Image (Encoded'Length) &
                " UTF-8 bytes," & Natural'Image (UTF_16'Length) & " UTF-16 units");
   end Show;

   Built : Wide_Wide_String (1 .. 10);
   Total : Natural := 0;

begin
   Show (A);
   Show (B);
   Show (C);
   Show (A & B & C);
   for I in 0 .. 4 loop
      Built (2 * I + 1) := Wide_Wide_Character'Val (16#E9#);
      Built (2 * I + 2) := Wide_Wide_Character'Val (Character'Pos ('0') + I);
   end loop;
   Show (Built);
   for Ch of Built loop
      Total := Total + Wide_Wide_Character'Pos (Ch);
   end loop;
   Put_Line (Natural'Image (Total));
   Put_Line (Encode (To_Upper (A & " " & Wide_Wide_Character'Val (16#3B1#) & Wide_Wide_Character'Val (16#3B2#))));
   Put_Line (Natural'Image (Decode (Encode (B))'Length) & " " & Boolean'Image (Decode (Encode (C)) = C) & " " &
             Boolean'Image (Is_Letter (B (1))) & " " & Boolean'Image (Is_Digit (Built (2))));
end Unicode;
