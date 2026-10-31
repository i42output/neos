--  Ada.Text_IO.adb
--
--  Copyright (c) 2025 Leigh Johnston.  All Rights Reserved.
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

with Neos.File.Stream;

package body Ada.Text_IO is

	procedure Put_C(Text : char_array);
	pragma Import (neos, Put_Line_C, "process.put");

    procedure Put(File : in File_Type; Item : in Character); 
    begin 
    	DoPut(File, Item);
    end Put;

    procedure Put(Item : in Character); 
    begin
    	Put(Neos.File.Stream.stdout(), Item);
    end Put;

    procedure Put(File : in File_Type; Item : in String)
    begin 
    	DoPut(File, Item);
    end Put;

    procedure Put(Item : in String)
    begin
    	Put(Neos.File.Stream.stdout(), Item);
    end Put;

    procedure New_Line(File : in File_Type; Spacing : in Positive_Count := 1)
	begin
		for K in 1 .. Spacing loop
	         DoPut(Character'Pos (ASCII.LF));
	    end loop;
	end New_Line;

    procedure New_Line(Spacing : in Positive_Count := 1)
    begin
    	New_Line(Neos.File.Stream.stdout(), Spacing)
    end New_Line;

    procedure Put_Line(File : in File_Type; Item : in String)
    begin
    	Put(File, Item);
    	New_Line(File);
    end Put_Line;

    procedure Put_Line(Item : in String)
    begin
    	Put_Line(Neos.File.Stream.stdout(), Item);
    end Put_Line;

private

    procedure DoPut(File : in File_Type; Item : in Character); 	
	begin
	end DoPut;

    procedure DoPut(File : in File_Type; Item : in String); 	
	begin
	end DoPut;

begin
end Ada.Text_IO;