--  System.Atomic_Operations.Modular_Arithmetic.adb
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

--  (neos switches a program's tasks only at task dispatching points, none of
--  which occurs within these bodies, so each operation is indivisible)

package body System.Atomic_Operations.Modular_Arithmetic is

   procedure Atomic_Add (Item  : aliased in out Atomic_Type;
                         Value : Atomic_Type) is
   begin
      Item := Item + Value;
   end Atomic_Add;

   procedure Atomic_Subtract (Item  : aliased in out Atomic_Type;
                              Value : Atomic_Type) is
   begin
      Item := Item - Value;
   end Atomic_Subtract;

   function Atomic_Fetch_And_Add
     (Item  : aliased in out Atomic_Type;
      Value : Atomic_Type) return Atomic_Type is
      Prior : constant Atomic_Type := Item;
   begin
      Item := Item + Value;
      return Prior;
   end Atomic_Fetch_And_Add;

   function Atomic_Fetch_And_Subtract
     (Item  : aliased in out Atomic_Type;
      Value : Atomic_Type) return Atomic_Type is
      Prior : constant Atomic_Type := Item;
   begin
      Item := Item - Value;
      return Prior;
   end Atomic_Fetch_And_Subtract;

   function Is_Lock_Free (Item : aliased Atomic_Type) return Boolean is
   begin
      return True;
   end Is_Lock_Free;

end System.Atomic_Operations.Modular_Arithmetic;
