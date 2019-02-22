package Ada.Text_IO is

    type File_Type is limited private; 

    type Count is range 0 .. Natural'Last; 
    subtype Positive_Count is Count range 1 .. Count'Last; 

    Unbounded : constant Count := 0; 

    procedure Put(File : in File_Type; Item : in Character); 
    procedure Put(Item : in Character); 
    procedure Put(File : in File_Type; Item : in String); 
    procedure Put(Item : in String); 
    procedure New_Line(File : in File_Type; Spacing : in Positive_Count := 1); 
    procedure New_Line(Spacing : in Positive_Count := 1);     
    procedure Put_Line(File : in File_Type; Item : in String); 
    procedure Put_Line(Item : in String); 

private

    type File_Type is access all System.File.Handle; 

    procedure DoPut(File : in File_Type; Item : in Character); 

end Ada.Text_IO;