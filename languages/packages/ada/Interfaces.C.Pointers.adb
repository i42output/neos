--  Interfaces.C.Pointers.adb
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

with Ada.Unchecked_Conversion;
with Interfaces.C.Strings;
with System.Storage_Elements;

package body Interfaces.C.Pointers is

   use System.Storage_Elements;

   function To_Address is new Ada.Unchecked_Conversion (Pointer, System.Address);
   function To_Pointer is new Ada.Unchecked_Conversion (System.Address, Pointer);

   --  (the distance between successive elements of an array, in storage elements)
   Stride : constant Storage_Offset :=
     Storage_Offset (Element_Array'Component_Size / System.Storage_Unit);

   function "+" (Left : in Pointer; Right : in ptrdiff_t) return Pointer is
   begin
      if Left = null then
         raise Pointer_Error;
      end if;
      return To_Pointer (To_Address (Left) + Storage_Offset (Right) * Stride);
   end "+";

   function "+" (Left : in ptrdiff_t; Right : in Pointer) return Pointer is
   begin
      return Right + Left;
   end "+";

   function "-" (Left : in Pointer; Right : in ptrdiff_t) return Pointer is
   begin
      if Left = null then
         raise Pointer_Error;
      end if;
      return To_Pointer (To_Address (Left) - Storage_Offset (Right) * Stride);
   end "-";

   function "-" (Left : in Pointer; Right : in Pointer) return ptrdiff_t is
   begin
      if Left = null or else Right = null then
         raise Pointer_Error;
      end if;
      return ptrdiff_t ((To_Address (Left) - To_Address (Right)) / Stride);
   end "-";

   procedure Increment (Ref : in out Pointer) is
   begin
      Ref := Ref + 1;
   end Increment;

   procedure Decrement (Ref : in out Pointer) is
   begin
      Ref := Ref - 1;
   end Decrement;

   function Virtual_Length (Ref        : in Pointer;
                            Terminator : in Element := Default_Terminator)
      return ptrdiff_t is
      Length : ptrdiff_t := 0;
      P : Pointer := Ref;
   begin
      if Ref = null then
         raise Interfaces.C.Strings.Dereference_Error;
      end if;
      while P.all /= Terminator loop
         Length := Length + 1;
         Increment (P);
      end loop;
      return Length;
   end Virtual_Length;

   function Value (Ref    : in Pointer;
                   Length : in ptrdiff_t)
      return Element_Array is
   begin
      if Ref = null then
         raise Interfaces.C.Strings.Dereference_Error;
      end if;
      declare
         Result : Element_Array
           (Index'First .. Index'Val (Index'Pos (Index'First) + Length - 1));
         P : Pointer := Ref;
      begin
         for I in Result'Range loop
            Result (I) := P.all;
            Increment (P);
         end loop;
         return Result;
      end;
   end Value;

   function Value (Ref        : in Pointer;
                   Terminator : in Element := Default_Terminator)
      return Element_Array is
   begin
      if Ref = null then
         raise Interfaces.C.Strings.Dereference_Error;
      end if;
      return Value (Ref, Virtual_Length (Ref, Terminator) + 1);
   end Value;

   procedure Copy_Terminated_Array
      (Source     : in Pointer;
       Target     : in Pointer;
       Limit      : in ptrdiff_t := ptrdiff_t'Last;
       Terminator : in Element :=  Default_Terminator) is
      S : Pointer := Source;
      T : Pointer := Target;
      Count : ptrdiff_t := 0;
   begin
      if Source = null or else Target = null then
         raise Interfaces.C.Strings.Dereference_Error;
      end if;
      while Count < Limit loop
         T.all := S.all;
         Count := Count + 1;
         exit when S.all = Terminator;
         Increment (S);
         Increment (T);
      end loop;
   end Copy_Terminated_Array;

   procedure Copy_Array (Source  : in Pointer;
                         Target  : in Pointer;
                         Length  : in ptrdiff_t) is
      S : Pointer := Source;
      T : Pointer := Target;
   begin
      if Source = null or else Target = null then
         raise Interfaces.C.Strings.Dereference_Error;
      end if;
      for Count in 1 .. Length loop
         T.all := S.all;
         if Count < Length then
            Increment (S);
            Increment (T);
         end if;
      end loop;
   end Copy_Array;

end Interfaces.C.Pointers;
