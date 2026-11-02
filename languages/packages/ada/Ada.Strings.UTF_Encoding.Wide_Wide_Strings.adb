--  Ada.Strings.UTF_Encoding.Wide_Wide_Strings.adb
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

package body Ada.Strings.UTF_Encoding.Wide_Wide_Strings is

   --  The encoding and decoding of the code points of Wide_Wide_Characters;
   --  the other UTF_Encoding packages convert through these.

   Max_Code : constant := 16#10_FFFF#;

   function Code (C : Wide_Wide_Character) return Natural is
   begin
      return Wide_Wide_Character'Pos (C);
   end Code;

   function Byte (N : Natural) return Character is
   begin
      return Character'Val (N);
   end Byte;

   function Starts_With (Item : String; Prefix : String) return Boolean is
   begin
      return Item'Length >= Prefix'Length
        and then Item (Item'First .. Item'First + Prefix'Length - 1) = Prefix;
   end Starts_With;

   procedure Check_Encodable (C : Wide_Wide_Character) is
      --  (surrogates and code points beyond 16#10FFFF# have no encoding)
   begin
      if Code (C) in 16#D800# .. 16#DFFF# or else Code (C) > Max_Code then
         raise Encoding_Error;
      end if;
   end Check_Encodable;

   --  UTF-8

   function Encode (Item       : Wide_Wide_String;
                    Output_BOM : Boolean  := False) return UTF_8_String is
      Length : Natural := 0;
   begin
      for I in Item'Range loop
         Check_Encodable (Item (I));
         case Code (Item (I)) is
            when 0 .. 16#7F#         => Length := Length + 1;
            when 16#80# .. 16#7FF#   => Length := Length + 2;
            when 16#800# .. 16#FFFF# => Length := Length + 3;
            when others              => Length := Length + 4;
         end case;
      end loop;
      if Output_BOM then
         Length := Length + BOM_8'Length;
      end if;
      declare
         Result : UTF_8_String (1 .. Length);
         Last   : Natural := 0;
         C      : Natural;
      begin
         if Output_BOM then
            Result (1 .. BOM_8'Length) := BOM_8;
            Last := BOM_8'Length;
         end if;
         for I in Item'Range loop
            C := Code (Item (I));
            case C is
               when 0 .. 16#7F# =>
                  Result (Last + 1) := Byte (C);
                  Last := Last + 1;
               when 16#80# .. 16#7FF# =>
                  Result (Last + 1) := Byte (16#C0# + C / 2**6);
                  Result (Last + 2) := Byte (16#80# + C mod 2**6);
                  Last := Last + 2;
               when 16#800# .. 16#FFFF# =>
                  Result (Last + 1) := Byte (16#E0# + C / 2**12);
                  Result (Last + 2) := Byte (16#80# + C / 2**6 mod 2**6);
                  Result (Last + 3) := Byte (16#80# + C mod 2**6);
                  Last := Last + 3;
               when others =>
                  Result (Last + 1) := Byte (16#F0# + C / 2**18);
                  Result (Last + 2) := Byte (16#80# + C / 2**12 mod 2**6);
                  Result (Last + 3) := Byte (16#80# + C / 2**6 mod 2**6);
                  Result (Last + 4) := Byte (16#80# + C mod 2**6);
                  Last := Last + 4;
            end case;
         end loop;
         return Result;
      end;
   end Encode;

   function Decode (Item : UTF_8_String) return Wide_Wide_String is
      Result : Wide_Wide_String (1 .. Item'Length);
      Last   : Natural := 0;
      I      : Integer := Item'First;
      B      : Natural;
      C      : Natural;
      Count  : Natural;  --  the number of continuation bytes
   begin
      if Starts_With (Item, BOM_8) then
         I := I + BOM_8'Length;
      elsif Starts_With (Item, BOM_16BE) or else Starts_With (Item, BOM_16LE)
      then
         raise Encoding_Error;
      end if;
      while I <= Item'Last loop
         B := Character'Pos (Item (I));
         case B is
            when 0 .. 16#7F# =>
               C := B; Count := 0;
            when 16#C0# .. 16#DF# =>
               C := B mod 2**5; Count := 1;
            when 16#E0# .. 16#EF# =>
               C := B mod 2**4; Count := 2;
            when 16#F0# .. 16#F7# =>
               C := B mod 2**3; Count := 3;
            when others =>
               raise Encoding_Error;
         end case;
         if Count > Item'Last - I then
            raise Encoding_Error;
         end if;
         for K in 1 .. Count loop
            B := Character'Pos (Item (I + K));
            if B not in 16#80# .. 16#BF# then
               raise Encoding_Error;
            end if;
            C := C * 2**6 + B mod 2**6;
         end loop;
         --  (an overlong sequence is accepted: AI12-0088-1)
         if C > Max_Code or else C in 16#D800# .. 16#DFFF# then
            raise Encoding_Error;
         end if;
         Last := Last + 1;
         Result (Last) := Wide_Wide_Character'Val (C);
         I := I + 1 + Count;
      end loop;
      return Result (1 .. Last);
   end Decode;

   --  UTF-16

   function Encode (Item       : Wide_Wide_String;
                    Output_BOM : Boolean  := False)
      return UTF_16_Wide_String is
      Length : Natural := 0;
   begin
      for I in Item'Range loop
         Check_Encodable (Item (I));
         if Code (Item (I)) > 16#FFFF# then
            Length := Length + 2;
         else
            Length := Length + 1;
         end if;
      end loop;
      if Output_BOM then
         Length := Length + 1;
      end if;
      declare
         Result : UTF_16_Wide_String (1 .. Length);
         Last   : Natural := 0;
         C      : Natural;
      begin
         if Output_BOM then
            Result (1) := BOM_16 (BOM_16'First);
            Last := 1;
         end if;
         for I in Item'Range loop
            C := Code (Item (I));
            if C > 16#FFFF# then
               C := C - 16#1_0000#;
               Result (Last + 1) := Wide_Character'Val (16#D800# + C / 2**10);
               Result (Last + 2) := Wide_Character'Val (16#DC00# + C mod 2**10);
               Last := Last + 2;
            else
               Result (Last + 1) := Wide_Character'Val (C);
               Last := Last + 1;
            end if;
         end loop;
         return Result;
      end;
   end Encode;

   function Decode (Item : UTF_16_Wide_String) return Wide_Wide_String is
      Result : Wide_Wide_String (1 .. Item'Length);
      Last   : Natural := 0;
      I      : Integer := Item'First;
      C, D   : Natural;
   begin
      if Item'Length > 0 and then Item (Item'First) = BOM_16 (BOM_16'First) then
         I := I + 1;
      end if;
      while I <= Item'Last loop
         C := Wide_Character'Pos (Item (I));
         if C in 16#D800# .. 16#DBFF# then
            if I = Item'Last then
               raise Encoding_Error;
            end if;
            D := Wide_Character'Pos (Item (I + 1));
            if D not in 16#DC00# .. 16#DFFF# then
               raise Encoding_Error;
            end if;
            C := 16#1_0000# + (C - 16#D800#) * 2**10 + (D - 16#DC00#);
            I := I + 2;
         elsif C in 16#DC00# .. 16#DFFF# then
            raise Encoding_Error;
         else
            I := I + 1;
         end if;
         Last := Last + 1;
         Result (Last) := Wide_Wide_Character'Val (C);
      end loop;
      return Result (1 .. Last);
   end Decode;

   function To_Bytes (Item       : UTF_16_Wide_String;
                      Scheme     : Encoding_Scheme;
                      Output_BOM : Boolean) return UTF_String is
      --  Item in the byte order of Scheme (UTF_16BE or UTF_16LE)
      Result : UTF_String (1 .. 2 * Item'Length + 2);
      Last   : Natural := 0;
      C      : Natural;
   begin
      if Output_BOM then
         if Scheme = UTF_16BE then
            Result (1 .. 2) := BOM_16BE;
         else
            Result (1 .. 2) := BOM_16LE;
         end if;
         Last := 2;
      end if;
      for I in Item'Range loop
         C := Wide_Character'Pos (Item (I));
         if Scheme = UTF_16BE then
            Result (Last + 1) := Byte (C / 2**8);
            Result (Last + 2) := Byte (C mod 2**8);
         else
            Result (Last + 1) := Byte (C mod 2**8);
            Result (Last + 2) := Byte (C / 2**8);
         end if;
         Last := Last + 2;
      end loop;
      return Result (1 .. Last);
   end To_Bytes;

   function From_Bytes (Item   : UTF_String;
                        Scheme : Encoding_Scheme) return UTF_16_Wide_String is
      --  the 16 bit values of Item, in the byte order of Scheme (UTF_16BE or
      --  UTF_16LE), without its BOM
      First  : Integer := Item'First;
      Result : UTF_16_Wide_String (1 .. Item'Length / 2);
      Last   : Natural := 0;
      Hi, Lo : Natural;
   begin
      if Item'Length mod 2 /= 0 then
         raise Encoding_Error;
      end if;
      if (Scheme = UTF_16BE and then Starts_With (Item, BOM_16BE))
        or else (Scheme = UTF_16LE and then Starts_With (Item, BOM_16LE))
      then
         First := First + 2;
      elsif Starts_With (Item, BOM_8) or else Starts_With (Item, BOM_16BE)
        or else Starts_With (Item, BOM_16LE)
      then
         raise Encoding_Error;
      end if;
      while First < Item'Last loop
         if Scheme = UTF_16BE then
            Hi := Character'Pos (Item (First));
            Lo := Character'Pos (Item (First + 1));
         else
            Lo := Character'Pos (Item (First));
            Hi := Character'Pos (Item (First + 1));
         end if;
         Last := Last + 1;
         Result (Last) := Wide_Character'Val (Hi * 2**8 + Lo);
         First := First + 2;
      end loop;
      return Result (1 .. Last);
   end From_Bytes;

   --  Any scheme

   function Encode (Item          : Wide_Wide_String;
                    Output_Scheme : Encoding_Scheme;
                    Output_BOM    : Boolean  := False) return UTF_String is
   begin
      if Output_Scheme = UTF_8 then
         return UTF_8_String'(Encode (Item, Output_BOM));
      else
         return To_Bytes (UTF_16_Wide_String'(Encode (Item, False)),
                          Output_Scheme, Output_BOM);
      end if;
   end Encode;

   function Decode (Item         : UTF_String;
                    Input_Scheme : Encoding_Scheme) return Wide_Wide_String is
   begin
      if Input_Scheme = UTF_8 then
         return Decode (UTF_8_String'(Item));
      else
         return Decode (From_Bytes (Item, Input_Scheme));
      end if;
   end Decode;

end Ada.Strings.UTF_Encoding.Wide_Wide_Strings;
