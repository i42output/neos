--  System.RPC.adb
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

with Ada.Unchecked_Deallocation;

package body System.RPC is

   use type Ada.Streams.Stream_Element_Offset;

   procedure Free is new Ada.Unchecked_Deallocation
     (Ada.Streams.Stream_Element_Array, Elements_Access);

   Receivers : array (Partition_Id) of RPC_Receiver;

   overriding procedure Finalize (B : in out Buffer) is
   begin
      Free (B.Data);
   end Finalize;

   procedure Read(
      Stream : in out Params_Stream_Type;
      Item : out Ada.Streams.Stream_Element_Array;
      Last : out Ada.Streams.Stream_Element_Offset) is
      B     : Buffer renames Stream.Elements;
      Count : constant Ada.Streams.Stream_Element_Offset :=
        Ada.Streams.Stream_Element_Offset'Min (Item'Length, B.Last - B.First + 1);
   begin
      Item (Item'First .. Item'First + Count - 1) := B.Data (B.First .. B.First + Count - 1);
      B.First := B.First + Count;
      Last := Item'First + Count - 1;
   end Read;

   procedure Write(
      Stream : in out Params_Stream_Type;
      Item : in Ada.Streams.Stream_Element_Array) is
      B : Buffer renames Stream.Elements;
   begin
      --  (the elements not yet read moved to the front of the buffer, and
      --  the buffer grown, as needed)
      if B.Data = null then
         B.Data := new Ada.Streams.Stream_Element_Array
           (1 .. Ada.Streams.Stream_Element_Offset'Max (Stream.Initial_Size, Item'Length));
      elsif B.Last + Item'Length > B.Data'Last then
         declare
            Count  : constant Ada.Streams.Stream_Element_Offset := B.Last - B.First + 1;
            Size   : Ada.Streams.Stream_Element_Offset := Ada.Streams.Stream_Element_Offset'Max (B.Data'Length, 1);
            Data   : Elements_Access := B.Data;
         begin
            while Count + Item'Length > Size loop
               Size := Size * 2;
            end loop;
            if Size > B.Data'Length then
               Data := new Ada.Streams.Stream_Element_Array (1 .. Size);
            end if;
            Data (1 .. Count) := B.Data (B.First .. B.Last);
            if Data /= B.Data then
               Free (B.Data);
               B.Data := Data;
            end if;
            B.First := 1;
            B.Last := Count;
         end;
      end if;
      B.Data (B.Last + 1 .. B.Last + Item'Length) := Item;
      B.Last := B.Last + Item'Length;
   end Write;

   procedure Do_RPC(
      Partition  : in Partition_Id;
      Params     : access Params_Stream_Type;
      Result     : access Params_Stream_Type) is
   begin
      if Receivers (Partition) = null then
         raise Communication_Error;
      end if;
      Receivers (Partition) (Params, Result);
   end Do_RPC;

   procedure Do_APC(
      Partition  : in Partition_Id;
      Params     : access Params_Stream_Type) is
      Result : aliased Params_Stream_Type (0);
   begin
      Do_RPC (Partition, Params, Result'Access);
   end Do_APC;

   procedure Establish_RPC_Receiver(
      Partition : in Partition_Id;
      Receiver  : in RPC_Receiver) is
   begin
      Receivers (Partition) := Receiver;
   end Establish_RPC_Receiver;

end System.RPC;
