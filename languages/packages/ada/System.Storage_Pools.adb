--  System.Storage_Pools.adb
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

package body System.Storage_Pools is

   function Heap_Allocate (Size : Storage_Elements.Storage_Count)
      return Address;
   pragma Import (Neos, Heap_Allocate, "ada::allocate");
   procedure Heap_Deallocate (Storage_Address : Address);
   pragma Import (Neos, Heap_Deallocate, "ada::deallocate");

   overriding
   procedure Allocate(
     Pool : in out Standard_Pool;
     Storage_Address : out Address;
     Size_In_Storage_Elements : in Storage_Elements.Storage_Count;
     Alignment : in Storage_Elements.Storage_Count) is
      pragma Unreferenced (Pool, Alignment);
   begin
      Storage_Address := Heap_Allocate (Size_In_Storage_Elements);
   end Allocate;

   overriding
   procedure Deallocate(
     Pool : in out Standard_Pool;
     Storage_Address : in Address;
     Size_In_Storage_Elements : in Storage_Elements.Storage_Count;
     Alignment : in Storage_Elements.Storage_Count) is
      pragma Unreferenced (Pool, Size_In_Storage_Elements, Alignment);
   begin
      Heap_Deallocate (Storage_Address);
   end Deallocate;

   overriding
   function Storage_Size(Pool : Standard_Pool)
       return Storage_Elements.Storage_Count is
      pragma Unreferenced (Pool);
   begin
      return Storage_Elements.Storage_Count'Last;
   end Storage_Size;

end System.Storage_Pools;
