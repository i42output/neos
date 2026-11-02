--  Ada.Storage_IO.adb
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

with Ada.Streams;
with System;

package body Ada.Storage_IO is

   use System.Storage_Elements;
   use type Ada.Streams.Stream_Element_Offset;

   --  (an item is stored as its stream representation (its components',
   --  not its implementation's (which may include indirections, e.g. a
   --  component whose size is dynamic), A.9(11)): a stream over the buffer)
   type Buffer_Stream is new Ada.Streams.Root_Stream_Type with record
      Base     : System.Address;
      Position : Storage_Count := 0;
   end record;

   overriding
   procedure Read (Stream : in out Buffer_Stream;
                   Item   : out Ada.Streams.Stream_Element_Array;
                   Last   : out Ada.Streams.Stream_Element_Offset);

   overriding
   procedure Write (Stream : in out Buffer_Stream;
                    Item   : in Ada.Streams.Stream_Element_Array);

   overriding
   procedure Read (Stream : in out Buffer_Stream;
                   Item   : out Ada.Streams.Stream_Element_Array;
                   Last   : out Ada.Streams.Stream_Element_Offset) is
      Data : Storage_Array (1 .. Buffer_Size)
         with Import, Address => Stream.Base;
   begin
      Last := Item'First - 1;
      for I in Item'Range loop
         exit when Stream.Position >= Buffer_Size;
         Stream.Position := Stream.Position + 1;
         Item (I) := Ada.Streams.Stream_Element (Data (Stream.Position));
         Last := I;
      end loop;
   end Read;

   overriding
   procedure Write (Stream : in out Buffer_Stream;
                    Item   : in Ada.Streams.Stream_Element_Array) is
      Data : Storage_Array (1 .. Buffer_Size)
         with Import, Address => Stream.Base;
   begin
      for I in Item'Range loop
         if Stream.Position >= Buffer_Size then
            raise Data_Error;
         end if;
         Stream.Position := Stream.Position + 1;
         Data (Stream.Position) := Storage_Element (Item (I));
      end loop;
   end Write;

   procedure Read (Buffer : in  Buffer_Type; Item : out Element_Type) is
      S : aliased Buffer_Stream;
   begin
      S.Base := Buffer'Address;
      Element_Type'Read (S'Access, Item);
   exception
      when Ada.IO_Exceptions.End_Error =>
         raise Data_Error;
   end Read;

   procedure Write(Buffer : out Buffer_Type; Item : in  Element_Type) is
      S : aliased Buffer_Stream;
   begin
      Buffer := (others => 0);
      S.Base := Buffer'Address;
      Element_Type'Write (S'Access, Item);
   end Write;

end Ada.Storage_IO;
