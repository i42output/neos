--  Interfaces.adb
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

package body Interfaces is

   function Shift_Left (Value : Unsigned_8; Amount : Natural) return Unsigned_8 is
   begin
      if Amount >= 8 then
         return 0;
      end if;
      return Value * 2 ** Amount;
   end Shift_Left;

   function Shift_Right (Value : Unsigned_8; Amount : Natural) return Unsigned_8 is
   begin
      if Amount >= 8 then
         return 0;
      end if;
      return Value / 2 ** Amount;
   end Shift_Right;

   function Shift_Right_Arithmetic (Value : Unsigned_8; Amount : Natural) return Unsigned_8 is
   begin
      if Value < 2 ** 7 then
         return Shift_Right (Value, Amount);
      elsif Amount >= 8 then
         return Unsigned_8'Last;
      end if;
      return Shift_Right (Value, Amount) or not Shift_Right (Unsigned_8'Last, Amount);
   end Shift_Right_Arithmetic;

   function Rotate_Left (Value : Unsigned_8; Amount : Natural) return Unsigned_8 is
      A : constant Natural := Amount mod 8;
   begin
      if A = 0 then
         return Value;
      end if;
      return Shift_Left (Value, A) or Shift_Right (Value, 8 - A);
   end Rotate_Left;

   function Rotate_Right (Value : Unsigned_8; Amount : Natural) return Unsigned_8 is
      A : constant Natural := Amount mod 8;
   begin
      if A = 0 then
         return Value;
      end if;
      return Shift_Right (Value, A) or Shift_Left (Value, 8 - A);
   end Rotate_Right;

   function Shift_Left (Value : Unsigned_16; Amount : Natural) return Unsigned_16 is
   begin
      if Amount >= 16 then
         return 0;
      end if;
      return Value * 2 ** Amount;
   end Shift_Left;

   function Shift_Right (Value : Unsigned_16; Amount : Natural) return Unsigned_16 is
   begin
      if Amount >= 16 then
         return 0;
      end if;
      return Value / 2 ** Amount;
   end Shift_Right;

   function Shift_Right_Arithmetic (Value : Unsigned_16; Amount : Natural) return Unsigned_16 is
   begin
      if Value < 2 ** 15 then
         return Shift_Right (Value, Amount);
      elsif Amount >= 16 then
         return Unsigned_16'Last;
      end if;
      return Shift_Right (Value, Amount) or not Shift_Right (Unsigned_16'Last, Amount);
   end Shift_Right_Arithmetic;

   function Rotate_Left (Value : Unsigned_16; Amount : Natural) return Unsigned_16 is
      A : constant Natural := Amount mod 16;
   begin
      if A = 0 then
         return Value;
      end if;
      return Shift_Left (Value, A) or Shift_Right (Value, 16 - A);
   end Rotate_Left;

   function Rotate_Right (Value : Unsigned_16; Amount : Natural) return Unsigned_16 is
      A : constant Natural := Amount mod 16;
   begin
      if A = 0 then
         return Value;
      end if;
      return Shift_Right (Value, A) or Shift_Left (Value, 16 - A);
   end Rotate_Right;

   function Shift_Left (Value : Unsigned_32; Amount : Natural) return Unsigned_32 is
   begin
      if Amount >= 32 then
         return 0;
      end if;
      return Value * 2 ** Amount;
   end Shift_Left;

   function Shift_Right (Value : Unsigned_32; Amount : Natural) return Unsigned_32 is
   begin
      if Amount >= 32 then
         return 0;
      end if;
      return Value / 2 ** Amount;
   end Shift_Right;

   function Shift_Right_Arithmetic (Value : Unsigned_32; Amount : Natural) return Unsigned_32 is
   begin
      if Value < 2 ** 31 then
         return Shift_Right (Value, Amount);
      elsif Amount >= 32 then
         return Unsigned_32'Last;
      end if;
      return Shift_Right (Value, Amount) or not Shift_Right (Unsigned_32'Last, Amount);
   end Shift_Right_Arithmetic;

   function Rotate_Left (Value : Unsigned_32; Amount : Natural) return Unsigned_32 is
      A : constant Natural := Amount mod 32;
   begin
      if A = 0 then
         return Value;
      end if;
      return Shift_Left (Value, A) or Shift_Right (Value, 32 - A);
   end Rotate_Left;

   function Rotate_Right (Value : Unsigned_32; Amount : Natural) return Unsigned_32 is
      A : constant Natural := Amount mod 32;
   begin
      if A = 0 then
         return Value;
      end if;
      return Shift_Right (Value, A) or Shift_Left (Value, 32 - A);
   end Rotate_Right;

   function Shift_Left (Value : Unsigned_64; Amount : Natural) return Unsigned_64 is
   begin
      if Amount >= 64 then
         return 0;
      end if;
      return Value * 2 ** Amount;
   end Shift_Left;

   function Shift_Right (Value : Unsigned_64; Amount : Natural) return Unsigned_64 is
   begin
      if Amount >= 64 then
         return 0;
      end if;
      return Value / 2 ** Amount;
   end Shift_Right;

   function Shift_Right_Arithmetic (Value : Unsigned_64; Amount : Natural) return Unsigned_64 is
   begin
      if Value < 2 ** 63 then
         return Shift_Right (Value, Amount);
      elsif Amount >= 64 then
         return Unsigned_64'Last;
      end if;
      return Shift_Right (Value, Amount) or not Shift_Right (Unsigned_64'Last, Amount);
   end Shift_Right_Arithmetic;

   function Rotate_Left (Value : Unsigned_64; Amount : Natural) return Unsigned_64 is
      A : constant Natural := Amount mod 64;
   begin
      if A = 0 then
         return Value;
      end if;
      return Shift_Left (Value, A) or Shift_Right (Value, 64 - A);
   end Rotate_Left;

   function Rotate_Right (Value : Unsigned_64; Amount : Natural) return Unsigned_64 is
      A : constant Natural := Amount mod 64;
   begin
      if A = 0 then
         return Value;
      end if;
      return Shift_Right (Value, A) or Shift_Left (Value, 64 - A);
   end Rotate_Right;

end Interfaces;
