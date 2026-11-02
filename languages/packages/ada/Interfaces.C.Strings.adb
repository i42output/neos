--  Interfaces.C.Strings.adb
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

with System.Address_To_Access_Conversions;
with System.Storage_Elements;

package body Interfaces.C.Strings is

   use type System.Storage_Elements.Storage_Offset;

   package Char_Pointers is new System.Address_To_Access_Conversions (char);

   --  (the Ada memory's allocator: a chars_ptr made by New_Char_Array is
   --  storage that it allocated, which Free gives back)
   function Allocate (Size : Interfaces.Integer_64) return System.Address;
   pragma Import (Neos, Allocate, "ada::allocate");
   procedure Deallocate (Storage : System.Address);
   pragma Import (Neos, Deallocate, "ada::deallocate");

   function Element (Item : chars_ptr; Offset : size_t) return char is
   begin
      return Char_Pointers.To_Pointer
        (System.Storage_Elements."+"
           (System.Address (Item),
            System.Storage_Elements.Storage_Offset (Offset))).all;
   end Element;

   procedure Set_Element (Item : chars_ptr; Offset : size_t; Value : char) is
   begin
      Char_Pointers.To_Pointer
        (System.Storage_Elements."+"
           (System.Address (Item),
            System.Storage_Elements.Storage_Offset (Offset))).all := Value;
   end Set_Element;

   procedure Check_Not_Null (Item : chars_ptr) is
   begin
      if Item = Null_Ptr then
         raise Dereference_Error;
      end if;
   end Check_Not_Null;

   function To_Chars_Ptr (Item      : in char_array_access;
                          Nul_Check : in Boolean := False)
      return chars_ptr is
   begin
      if Item = null then
         return Null_Ptr;
      end if;
      if Nul_Check and then not Is_Nul_Terminated (Item.all) then
         raise Terminator_Error;
      end if;
      return chars_ptr (Item.all'Address);
   end To_Chars_Ptr;

   function New_Char_Array (Chars : in char_array) return chars_ptr is
      Length : size_t := Chars'Length;
      Result : chars_ptr;
   begin
      for I in Chars'Range loop
         if Chars (I) = nul then
            Length := I - Chars'First;
            exit;
         end if;
      end loop;
      Result := chars_ptr (Allocate (Interfaces.Integer_64 (Length) + 1));
      for J in 1 .. Length loop
         Set_Element (Result, J - 1, Chars (Chars'First + J - 1));
      end loop;
      Set_Element (Result, Length, nul);
      return Result;
   end New_Char_Array;

   function New_String (Str : in String) return chars_ptr is
   begin
      return New_Char_Array (To_C (Str));
   end New_String;

   procedure Free (Item : in out chars_ptr) is
   begin
      if Item /= Null_Ptr then
         Deallocate (System.Address (Item));
         Item := Null_Ptr;
      end if;
   end Free;

   function Strlen (Item : in chars_ptr) return size_t is
      Length : size_t := 0;
   begin
      Check_Not_Null (Item);
      while Element (Item, Length) /= nul loop
         Length := Length + 1;
      end loop;
      return Length;
   end Strlen;

   function Value (Item : in chars_ptr) return char_array is
   begin
      Check_Not_Null (Item);
      declare
         Result : char_array (0 .. Strlen (Item));
      begin
         for I in Result'Range loop
            Result (I) := Element (Item, I);
         end loop;
         return Result;
      end;
   end Value;

   function Value (Item : in chars_ptr; Length : in size_t)
      return char_array is
      Last : size_t;
   begin
      Check_Not_Null (Item);
      if Length = 0 then
         raise Constraint_Error;
      end if;
      Last := Length - 1;
      for I in 0 .. Length - 1 loop
         if Element (Item, I) = nul then
            Last := I;
            exit;
         end if;
      end loop;
      declare
         Result : char_array (0 .. Last);
      begin
         for I in Result'Range loop
            Result (I) := Element (Item, I);
         end loop;
         return Result;
      end;
   end Value;

   function Value (Item : in chars_ptr) return String is
   begin
      return To_Ada (Value (Item), Trim_Nul => True);
   end Value;

   function Value (Item : in chars_ptr; Length : in size_t)
      return String is
   begin
      return To_Ada (Value (Item, Length) & nul, Trim_Nul => True);
   end Value;

   procedure Update (Item   : in chars_ptr;
                     Offset : in size_t;
                     Chars  : in char_array;
                     Check  : in Boolean := True) is
   begin
      Check_Not_Null (Item);
      if Check and then Offset + Chars'Length > Strlen (Item) then
         raise Update_Error;
      end if;
      for I in Chars'Range loop
         Set_Element (Item, Offset + (I - Chars'First), Chars (I));
      end loop;
   end Update;

   procedure Update (Item   : in chars_ptr;
                     Offset : in size_t;
                     Str    : in String;
                     Check  : in Boolean := True) is
   begin
      Update (Item, Offset, To_C (Str, Append_Nul => False), Check);
   end Update;

end Interfaces.C.Strings;
