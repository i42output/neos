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