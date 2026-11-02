--  System.Storage_Pools.Subpools.adb
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

package body System.Storage_Pools.Subpools is

   function Pool_of_Subpool (Subpool : not null Subpool_Handle)
      return access Root_Storage_Pool_With_Subpools'Class is
   begin
      return Subpool.Pool;
   end Pool_of_Subpool;

   procedure Set_Pool_of_Subpool (
      Subpool : in not null Subpool_Handle;
      To : in out Root_Storage_Pool_With_Subpools'Class) is
   begin
      if Subpool.Pool /= null then
         raise Program_Error;
      end if;
      Subpool.Pool := To'Unchecked_Access;
   end Set_Pool_of_Subpool;

   function Default_Subpool_for_Pool (
      Pool : in out Root_Storage_Pool_With_Subpools)
         return not null Subpool_Handle is
   begin
      raise Program_Error;
      return Default_Subpool_for_Pool (Pool);
   end Default_Subpool_for_Pool;

   procedure Allocate (
      Pool : in out Root_Storage_Pool_With_Subpools;
      Storage_Address : out Address;
      Size_In_Storage_Elements : in Storage_Elements.Storage_Count;
      Alignment : in Storage_Elements.Storage_Count) is
   begin
      Allocate_From_Subpool
        (Root_Storage_Pool_With_Subpools'Class (Pool),
         Storage_Address, Size_In_Storage_Elements, Alignment,
         Subpool => Default_Subpool_for_Pool
                      (Root_Storage_Pool_With_Subpools'Class (Pool)));
   end Allocate;

end System.Storage_Pools.Subpools;
