--  Ada.Streams.Storage.Unbounded.adb
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

package body Ada.Streams.Storage.Unbounded is

   procedure Free is
      new Ada.Unchecked_Deallocation (Stream_Element_Array, Element_Array_Access);

   overriding
   procedure Read (
      Stream : in out Stream_Type;
      Item   : out Stream_Element_Array;
      Last   : out Stream_Element_Offset)
   is
      B     : Storage renames Stream.Buffer;
      Count : constant Stream_Element_Count :=
         Stream_Element_Count'Min (Element_Count (Stream), Item'Length);
   begin
      if Count = 0 then
         Last := Item'First - 1;
         return;
      end if;
      Item (Item'First .. Item'First + Count - 1) :=
         B.Data (B.First .. B.First + Count - 1);
      Last := Item'First + Count - 1;
      B.First := B.First + Count;
      if B.First > B.Last then
         B.First := 1;
         B.Last := 0;
      end if;
   end Read;

   overriding
   procedure Write (
      Stream : in out Stream_Type;
      Item   : in Stream_Element_Array)
   is
      B         : Storage renames Stream.Buffer;
      Available : constant Stream_Element_Count := Element_Count (Stream);
      Needed    : constant Stream_Element_Count := Available + Item'Length;
   begin
      if Item'Length = 0 then
         return;
      end if;
      if B.Data = null or else B.Data'Last - B.Last < Item'Length then
         if B.Data /= null and then B.Data'Length >= Needed
           and then Available <= B.Data'Length / 2
         then
            --  Enough room once the available elements are moved down.
            B.Data (1 .. Available) := B.Data (B.First .. B.Last);
         else
            declare
               Size : Stream_Element_Count :=
                  (if B.Data = null then 256 else B.Data'Length);
               New_Data : Element_Array_Access;
            begin
               while Size < Needed loop
                  Size := Size * 2;
               end loop;
               New_Data := new Stream_Element_Array (1 .. Size);
               if Available > 0 then
                  New_Data (1 .. Available) := B.Data (B.First .. B.Last);
               end if;
               Free (B.Data);
               B.Data := New_Data;
            end;
         end if;
         B.First := 1;
         B.Last := Available;
      end if;
      B.Data (B.Last + 1 .. B.Last + Item'Length) := Item;
      B.Last := B.Last + Item'Length;
   end Write;

   overriding
   function Element_Count (Stream : Stream_Type)
      return Stream_Element_Count is
   begin
      return Stream.Buffer.Last - Stream.Buffer.First + 1;
   end Element_Count;

   overriding
   procedure Clear (Stream : in out Stream_Type) is
   begin
      Stream.Buffer.First := 1;
      Stream.Buffer.Last := 0;
   end Clear;

   overriding procedure Finalize (Object : in out Storage) is
   begin
      Free (Object.Data);
      Object.First := 1;
      Object.Last := 0;
   end Finalize;

end Ada.Streams.Storage.Unbounded;
