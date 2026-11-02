--  Interfaces.C.adb
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

package body Interfaces.C is

   function To_C (Item : in Character) return char is
   begin
      return char'Val (Character'Pos (Item));
   end To_C;

   function To_Ada (Item : in char) return Character is
   begin
      return Character'Val (char'Pos (Item));
   end To_Ada;

   function To_C (Item : in Wide_Character) return wchar_t is
   begin
      return wchar_t'Val (Wide_Character'Pos (Item));
   end To_C;

   function To_Ada (Item : in wchar_t) return Wide_Character is
   begin
      return Wide_Character'Val (wchar_t'Pos (Item));
   end To_Ada;

   function To_C (Item : in Wide_Character) return char16_t is
   begin
      return char16_t'Val (Wide_Character'Pos (Item));
   end To_C;

   function To_Ada (Item : in char16_t) return Wide_Character is
   begin
      return Wide_Character'Val (char16_t'Pos (Item));
   end To_Ada;

   function To_C (Item : in Wide_Wide_Character) return char32_t is
   begin
      return char32_t'Val (Wide_Wide_Character'Pos (Item));
   end To_C;

   function To_Ada (Item : in char32_t) return Wide_Wide_Character is
   begin
      return Wide_Wide_Character'Val (char32_t'Pos (Item));
   end To_Ada;

   function Is_Nul_Terminated (Item : in char_array) return Boolean is
   begin
      for I in Item'Range loop
         if Item (I) = nul then
            return True;
         end if;
      end loop;
      return False;
   end Is_Nul_Terminated;

   --  (the number of elements of Item before the first nul (Terminator_Error
   --  if there is none), or all of them if not Trim_Nul)
   function Ada_Length (Item : in char_array; Trim_Nul : in Boolean) return Natural is
   begin
      if Trim_Nul then
         for I in Item'Range loop
            if Item (I) = nul then
               return Natural (I - Item'First);
            end if;
         end loop;
         raise Terminator_Error;
      end if;
      return Item'Length;
   end Ada_Length;

   function To_C (Item       : in String;
                  Append_Nul : in Boolean := True)
      return char_array is
   begin
      if not Append_Nul and then Item'Length = 0 then
         raise Constraint_Error;
      end if;
      declare
         Result : char_array (0 .. size_t (Item'Length) - Boolean'Pos (not Append_Nul));
      begin
         for I in Item'Range loop
            Result (size_t (I - Item'First)) := To_C (Item (I));
         end loop;
         if Append_Nul then
            Result (Result'Last) := nul;
         end if;
         return Result;
      end;
   end To_C;

   function To_Ada (Item     : in char_array;
                    Trim_Nul : in Boolean := True)
      return String is
      Result : String (1 .. Ada_Length (Item, Trim_Nul));
   begin
      for J in Result'Range loop
         Result (J) := To_Ada (Item (Item'First + size_t (J - 1)));
      end loop;
      return Result;
   end To_Ada;

   procedure To_C (Item       : in  String;
                   Target     : out char_array;
                   Count      : out size_t;
                   Append_Nul : in  Boolean := True) is
      Needed : constant Natural := Item'Length + Boolean'Pos (Append_Nul);
   begin
      if Needed > Target'Length then
         raise Constraint_Error;
      end if;
      for J in 0 .. Item'Length - 1 loop
         Target (Target'First + size_t (J)) := To_C (Item (Item'First + J));
      end loop;
      if Append_Nul then
         Target (Target'First + size_t (Item'Length)) := nul;
      end if;
      Count := size_t (Needed);
   end To_C;

   procedure To_Ada (Item     : in  char_array;
                     Target   : out String;
                     Count    : out Natural;
                     Trim_Nul : in  Boolean := True) is
      Length : constant Natural := Ada_Length (Item, Trim_Nul);
   begin
      if Length > Target'Length then
         raise Constraint_Error;
      end if;
      for J in 0 .. Length - 1 loop
         Target (Target'First + J) := To_Ada (Item (Item'First + size_t (J)));
      end loop;
      Count := Length;
   end To_Ada;

   function Is_Nul_Terminated (Item : in wchar_array) return Boolean is
   begin
      for I in Item'Range loop
         if Item (I) = wide_nul then
            return True;
         end if;
      end loop;
      return False;
   end Is_Nul_Terminated;

   --  (the number of elements of Item before the first nul (Terminator_Error
   --  if there is none), or all of them if not Trim_Nul)
   function Ada_Length (Item : in wchar_array; Trim_Nul : in Boolean) return Natural is
   begin
      if Trim_Nul then
         for I in Item'Range loop
            if Item (I) = wide_nul then
               return Natural (I - Item'First);
            end if;
         end loop;
         raise Terminator_Error;
      end if;
      return Item'Length;
   end Ada_Length;

   function To_C (Item       : in Wide_String;
                  Append_Nul : in Boolean := True)
      return wchar_array is
   begin
      if not Append_Nul and then Item'Length = 0 then
         raise Constraint_Error;
      end if;
      declare
         Result : wchar_array (0 .. size_t (Item'Length) - Boolean'Pos (not Append_Nul));
      begin
         for I in Item'Range loop
            Result (size_t (I - Item'First)) := To_C (Item (I));
         end loop;
         if Append_Nul then
            Result (Result'Last) := wide_nul;
         end if;
         return Result;
      end;
   end To_C;

   function To_Ada (Item     : in wchar_array;
                    Trim_Nul : in Boolean := True)
      return Wide_String is
      Result : Wide_String (1 .. Ada_Length (Item, Trim_Nul));
   begin
      for J in Result'Range loop
         Result (J) := To_Ada (Item (Item'First + size_t (J - 1)));
      end loop;
      return Result;
   end To_Ada;

   procedure To_C (Item       : in  Wide_String;
                   Target     : out wchar_array;
                   Count      : out size_t;
                   Append_Nul : in  Boolean := True) is
      Needed : constant Natural := Item'Length + Boolean'Pos (Append_Nul);
   begin
      if Needed > Target'Length then
         raise Constraint_Error;
      end if;
      for J in 0 .. Item'Length - 1 loop
         Target (Target'First + size_t (J)) := To_C (Item (Item'First + J));
      end loop;
      if Append_Nul then
         Target (Target'First + size_t (Item'Length)) := wide_nul;
      end if;
      Count := size_t (Needed);
   end To_C;

   procedure To_Ada (Item     : in  wchar_array;
                     Target   : out Wide_String;
                     Count    : out Natural;
                     Trim_Nul : in  Boolean := True) is
      Length : constant Natural := Ada_Length (Item, Trim_Nul);
   begin
      if Length > Target'Length then
         raise Constraint_Error;
      end if;
      for J in 0 .. Length - 1 loop
         Target (Target'First + J) := To_Ada (Item (Item'First + size_t (J)));
      end loop;
      Count := Length;
   end To_Ada;

   function Is_Nul_Terminated (Item : in char16_array) return Boolean is
   begin
      for I in Item'Range loop
         if Item (I) = char16_nul then
            return True;
         end if;
      end loop;
      return False;
   end Is_Nul_Terminated;

   --  (the number of elements of Item before the first nul (Terminator_Error
   --  if there is none), or all of them if not Trim_Nul)
   function Ada_Length (Item : in char16_array; Trim_Nul : in Boolean) return Natural is
   begin
      if Trim_Nul then
         for I in Item'Range loop
            if Item (I) = char16_nul then
               return Natural (I - Item'First);
            end if;
         end loop;
         raise Terminator_Error;
      end if;
      return Item'Length;
   end Ada_Length;

   function To_C (Item       : in Wide_String;
                  Append_Nul : in Boolean := True)
      return char16_array is
   begin
      if not Append_Nul and then Item'Length = 0 then
         raise Constraint_Error;
      end if;
      declare
         Result : char16_array (0 .. size_t (Item'Length) - Boolean'Pos (not Append_Nul));
      begin
         for I in Item'Range loop
            Result (size_t (I - Item'First)) := To_C (Item (I));
         end loop;
         if Append_Nul then
            Result (Result'Last) := char16_nul;
         end if;
         return Result;
      end;
   end To_C;

   function To_Ada (Item     : in char16_array;
                    Trim_Nul : in Boolean := True)
      return Wide_String is
      Result : Wide_String (1 .. Ada_Length (Item, Trim_Nul));
   begin
      for J in Result'Range loop
         Result (J) := To_Ada (Item (Item'First + size_t (J - 1)));
      end loop;
      return Result;
   end To_Ada;

   procedure To_C (Item       : in  Wide_String;
                   Target     : out char16_array;
                   Count      : out size_t;
                   Append_Nul : in  Boolean := True) is
      Needed : constant Natural := Item'Length + Boolean'Pos (Append_Nul);
   begin
      if Needed > Target'Length then
         raise Constraint_Error;
      end if;
      for J in 0 .. Item'Length - 1 loop
         Target (Target'First + size_t (J)) := To_C (Item (Item'First + J));
      end loop;
      if Append_Nul then
         Target (Target'First + size_t (Item'Length)) := char16_nul;
      end if;
      Count := size_t (Needed);
   end To_C;

   procedure To_Ada (Item     : in  char16_array;
                     Target   : out Wide_String;
                     Count    : out Natural;
                     Trim_Nul : in  Boolean := True) is
      Length : constant Natural := Ada_Length (Item, Trim_Nul);
   begin
      if Length > Target'Length then
         raise Constraint_Error;
      end if;
      for J in 0 .. Length - 1 loop
         Target (Target'First + J) := To_Ada (Item (Item'First + size_t (J)));
      end loop;
      Count := Length;
   end To_Ada;

   function Is_Nul_Terminated (Item : in char32_array) return Boolean is
   begin
      for I in Item'Range loop
         if Item (I) = char32_nul then
            return True;
         end if;
      end loop;
      return False;
   end Is_Nul_Terminated;

   --  (the number of elements of Item before the first nul (Terminator_Error
   --  if there is none), or all of them if not Trim_Nul)
   function Ada_Length (Item : in char32_array; Trim_Nul : in Boolean) return Natural is
   begin
      if Trim_Nul then
         for I in Item'Range loop
            if Item (I) = char32_nul then
               return Natural (I - Item'First);
            end if;
         end loop;
         raise Terminator_Error;
      end if;
      return Item'Length;
   end Ada_Length;

   function To_C (Item       : in Wide_Wide_String;
                  Append_Nul : in Boolean := True)
      return char32_array is
   begin
      if not Append_Nul and then Item'Length = 0 then
         raise Constraint_Error;
      end if;
      declare
         Result : char32_array (0 .. size_t (Item'Length) - Boolean'Pos (not Append_Nul));
      begin
         for I in Item'Range loop
            Result (size_t (I - Item'First)) := To_C (Item (I));
         end loop;
         if Append_Nul then
            Result (Result'Last) := char32_nul;
         end if;
         return Result;
      end;
   end To_C;

   function To_Ada (Item     : in char32_array;
                    Trim_Nul : in Boolean := True)
      return Wide_Wide_String is
      Result : Wide_Wide_String (1 .. Ada_Length (Item, Trim_Nul));
   begin
      for J in Result'Range loop
         Result (J) := To_Ada (Item (Item'First + size_t (J - 1)));
      end loop;
      return Result;
   end To_Ada;

   procedure To_C (Item       : in  Wide_Wide_String;
                   Target     : out char32_array;
                   Count      : out size_t;
                   Append_Nul : in  Boolean := True) is
      Needed : constant Natural := Item'Length + Boolean'Pos (Append_Nul);
   begin
      if Needed > Target'Length then
         raise Constraint_Error;
      end if;
      for J in 0 .. Item'Length - 1 loop
         Target (Target'First + size_t (J)) := To_C (Item (Item'First + J));
      end loop;
      if Append_Nul then
         Target (Target'First + size_t (Item'Length)) := char32_nul;
      end if;
      Count := size_t (Needed);
   end To_C;

   procedure To_Ada (Item     : in  char32_array;
                     Target   : out Wide_Wide_String;
                     Count    : out Natural;
                     Trim_Nul : in  Boolean := True) is
      Length : constant Natural := Ada_Length (Item, Trim_Nul);
   begin
      if Length > Target'Length then
         raise Constraint_Error;
      end if;
      for J in 0 .. Length - 1 loop
         Target (Target'First + J) := To_Ada (Item (Item'First + size_t (J)));
      end loop;
      Count := Length;
   end To_Ada;

end Interfaces.C;
