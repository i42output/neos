-- Example Ada program

with Ada.Text_IO;
use Ada.Text_IO;

procedure Hello is
begin
	Put("Enter your name: ");
	declare
		Name : constant String := Get_Line;
	begin
		Put_Line("Hello " & Name & "!");
	end;
end Hello;
