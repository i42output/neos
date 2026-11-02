--  Ada.Streams.Storage.Bounded.adb
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

package body Ada.Streams.Storage.Bounded is

   overriding
   procedure Read (
      Stream : in out Stream_Type;
      Item   : out Stream_Element_Array;
      Last   : out Stream_Element_Offset)
   is
      Count : constant Stream_Element_Count :=
         Stream_Element_Count'Min (Stream.Count, Item'Length);
      Next  : Stream_Element_Offset := Item'First;
      Part  : Stream_Element_Count;
      Rest  : Stream_Element_Count := Count;
   begin
      while Rest > 0 loop
         --  The elements up to the end of Data, then those from its start.
         Part := Stream_Element_Count'Min
                   (Rest, Stream.Max_Elements - Stream.First + 1);
         Item (Next .. Next + Part - 1) :=
            Stream.Data (Stream.First .. Stream.First + Part - 1);
         Next := Next + Part;
         Rest := Rest - Part;
         Stream.First := Stream.First + Part;
         if Stream.First > Stream.Max_Elements then
            Stream.First := 1;
         end if;
      end loop;
      Stream.Count := Stream.Count - Count;
      if Stream.Count = 0 then
         Stream.First := 1;
      end if;
      Last := Item'First + Count - 1;
   end Read;

   overriding
   procedure Write (
      Stream : in out Stream_Type;
      Item   : in Stream_Element_Array)
   is
      Next : Stream_Element_Offset := Item'First;
      Rest : Stream_Element_Count := Item'Length;
      Free : Stream_Element_Offset;
      Part : Stream_Element_Count;
   begin
      if Stream.Count + Item'Length > Stream.Max_Elements then
         raise Constraint_Error;
      end if;
      while Rest > 0 loop
         Free := Stream.First + Stream.Count;
         if Free > Stream.Max_Elements then
            Free := Free - Stream.Max_Elements;
         end if;
         Part := Stream_Element_Count'Min (Rest, Stream.Max_Elements - Free + 1);
         Stream.Data (Free .. Free + Part - 1) := Item (Next .. Next + Part - 1);
         Next := Next + Part;
         Rest := Rest - Part;
         Stream.Count := Stream.Count + Part;
      end loop;
   end Write;

   overriding
   function Element_Count (Stream : Stream_Type)
      return Stream_Element_Count is
   begin
      return Stream.Count;
   end Element_Count;

   overriding
   procedure Clear (Stream : in out Stream_Type) is
   begin
      Stream.First := 1;
      Stream.Count := 0;
   end Clear;

end Ada.Streams.Storage.Bounded;
