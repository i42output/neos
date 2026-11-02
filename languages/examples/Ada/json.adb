--  A small JSON encoder and decoder: a variant record for values, containers of access values for arrays and
--  objects (keys in order), recursive descent parsing, and string escapes

with Ada.Text_IO; use Ada.Text_IO;
with Ada.Strings.Unbounded; use Ada.Strings.Unbounded;
with Ada.Strings.Fixed; use Ada.Strings.Fixed;
with Ada.Long_Float_Text_IO;
with Ada.Containers.Vectors;
with Ada.Containers.Indefinite_Ordered_Maps;

procedure Json is

   type Kind is (Null_Value, Boolean_Value, Number_Value, String_Value, Array_Value, Object_Value);
   type Value;
   type Value_Access is access Value;
   package Value_Vectors is new Ada.Containers.Vectors (Positive, Value_Access);
   package Value_Maps is new Ada.Containers.Indefinite_Ordered_Maps (String, Value_Access);

   type Value (K : Kind := Null_Value) is record
      case K is
         when Null_Value => null;
         when Boolean_Value => B : Boolean;
         when Number_Value => N : Long_Float;
         when String_Value => S : Unbounded_String;
         when Array_Value => Items : Value_Vectors.Vector;
         when Object_Value => Members : Value_Maps.Map;
      end case;
   end record;

   Syntax_Error : exception;

   function Number_Image (N : Long_Float) return String is
   begin
      if N = Long_Float'Floor (N) and then abs N < 1.0E15 then
         return Trim (Long_Long_Integer'Image (Long_Long_Integer (N)), Ada.Strings.Left);
      end if;
      declare
         Buffer : String (1 .. 40);
         Last : Natural;
      begin
         --  (fixed point notation, without the trailing zeros)
         Ada.Long_Float_Text_IO.Put (Buffer, N, Aft => 12, Exp => 0);
         Last := Buffer'Last;
         while Buffer (Last) = '0' loop
            Last := Last - 1;
         end loop;
         return Trim (Buffer (1 .. Last), Ada.Strings.Left);
      end;
   end Number_Image;

   function Quote (S : String) return String is
      Result : Unbounded_String := To_Unbounded_String ("""");
   begin
      for C of S loop
         case C is
            when '"' => Append (Result, "\""");
            when '\' => Append (Result, "\\");
            when ASCII.LF => Append (Result, "\n");
            when ASCII.HT => Append (Result, "\t");
            when Character'Val (0) .. Character'Val (8) | Character'Val (11) .. Character'Val (31) =>
               declare
                  Hex : constant String := "0123456789abcdef";
               begin
                  Append (Result, "\u00" & Hex (Character'Pos (C) / 16 + 1) & Hex (Character'Pos (C) mod 16 + 1));
               end;
            when others => Append (Result, C);
         end case;
      end loop;
      return To_String (Result) & """";
   end Quote;

   function Encode (V : Value) return String is
      Result : Unbounded_String;
   begin
      case V.K is
         when Null_Value => return "null";
         when Boolean_Value => return (if V.B then "true" else "false");
         when Number_Value => return Number_Image (V.N);
         when String_Value => return Quote (To_String (V.S));
         when Array_Value =>
            for Item of V.Items loop
               Append (Result, (if Length (Result) > 0 then "," else "") & Encode (Item.all));
            end loop;
            return "[" & To_String (Result) & "]";
         when Object_Value =>
            for Position in V.Members.Iterate loop
               Append (Result, (if Length (Result) > 0 then "," else "") & Quote (Value_Maps.Key (Position)) & ":" &
                               Encode (Value_Maps.Element (Position).all));
            end loop;
            return "{" & To_String (Result) & "}";
      end case;
   end Encode;

   function Decode (Text : String) return Value_Access is
      Pos : Positive := Text'First;

      procedure Skip is
      begin
         while Pos <= Text'Last and then Text (Pos) in ' ' | ASCII.HT | ASCII.LF | ASCII.CR loop
            Pos := Pos + 1;
         end loop;
      end Skip;

      procedure Expect (C : Character) is
      begin
         Skip;
         if Pos > Text'Last or else Text (Pos) /= C then
            raise Syntax_Error with "'" & C & "' expected at" & Positive'Image (Pos);
         end if;
         Pos := Pos + 1;
      end Expect;

      function Next_Is (C : Character) return Boolean is
      begin
         Skip;
         return Pos <= Text'Last and then Text (Pos) = C;
      end Next_Is;

      function Parse_String return String is
         Result : Unbounded_String;
      begin
         Expect ('"');
         while Text (Pos) /= '"' loop
            if Text (Pos) = '\' then
               Pos := Pos + 1;
               case Text (Pos) is
                  when 'n' => Append (Result, ASCII.LF);
                  when 't' => Append (Result, ASCII.HT);
                  when 'u' =>
                     Append (Result, Character'Val (Integer'Value ("16#" & Text (Pos + 1 .. Pos + 4) & "#")));
                     Pos := Pos + 4;
                  when others => Append (Result, Text (Pos));
               end case;
            else
               Append (Result, Text (Pos));
            end if;
            Pos := Pos + 1;
         end loop;
         Pos := Pos + 1;
         return To_String (Result);
      end Parse_String;

      function Parse return Value_Access is
         Start : Positive;
      begin
         Skip;
         case Text (Pos) is
            when '{' =>
               declare
                  Result : constant Value_Access := new Value (Object_Value);
               begin
                  Pos := Pos + 1;
                  if not Next_Is ('}') then
                     loop
                        declare
                           Key : constant String := Parse_String;
                        begin
                           Expect (':');
                           Result.Members.Include (Key, Parse);
                        end;
                        exit when not Next_Is (',');
                        Pos := Pos + 1;
                     end loop;
                  end if;
                  Expect ('}');
                  return Result;
               end;
            when '[' =>
               declare
                  Result : constant Value_Access := new Value (Array_Value);
               begin
                  Pos := Pos + 1;
                  if not Next_Is (']') then
                     loop
                        Result.Items.Append (Parse);
                        exit when not Next_Is (',');
                        Pos := Pos + 1;
                     end loop;
                  end if;
                  Expect (']');
                  return Result;
               end;
            when '"' =>
               return new Value'(String_Value, To_Unbounded_String (Parse_String));
            when 't' | 'f' | 'n' =>
               Start := Pos;
               while Pos <= Text'Last and then Text (Pos) in 'a' .. 'z' loop
                  Pos := Pos + 1;
               end loop;
               if Text (Start .. Pos - 1) = "true" then
                  return new Value'(Boolean_Value, True);
               elsif Text (Start .. Pos - 1) = "false" then
                  return new Value'(Boolean_Value, False);
               elsif Text (Start .. Pos - 1) = "null" then
                  return new Value;
               end if;
               raise Syntax_Error with "unknown word " & Text (Start .. Pos - 1);
            when others =>
               Start := Pos;
               while Pos <= Text'Last and then Text (Pos) in '0' .. '9' | '-' | '+' | '.' | 'e' | 'E' loop
                  Pos := Pos + 1;
               end loop;
               declare
                  Number : constant String := Text (Start .. Pos - 1);
               begin
                  --  (Ada's literals need a fraction before an exponent)
                  if Index (Number, ".") = 0 and then Index (Number, "e") + Index (Number, "E") > 0 then
                     return new Value'(Number_Value, Long_Float (Long_Long_Integer'Value (Number)));
                  end if;
                  return new Value'(Number_Value, Long_Float'Value (Number));
               end;
         end case;
      end Parse;

   begin
      return Parse;
   end Decode;

   Data : constant Value_Access := Decode
     ("{""name"": ""neos"", ""version"": 1.5, ""tags"": [""ada"", ""wasm"", ""jit""], " &
      """nested"": {""a"": 1, ""b"": [true, false]}, ""esc"": ""line\nbreak \""quoted\"" \u0001""}");

begin
   Put_Line (Encode (Data.all));
   Put_Line (To_String (Data.Members ("name").S) & " " & Number_Image (Data.Members ("version").N) & " " &
             To_String (Data.Members ("tags").Items (2).S) & " " & Boolean'Image (Data.Members ("nested").Members ("b").Items (1).B));
   Put_Line (Encode (Decode ("[1, 2.5, ""x"", {""k"": [null, true]}, -3e2]").all));
   begin
      Put_Line (Encode (Decode ("[1, 2").all));
   exception
      when Syntax_Error =>
         Put_Line ("syntax error");
   end;
end Json;
