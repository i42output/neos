-- Example Ada program: the program's exit status

with Ada.Command_Line;

procedure ExitCode is

	function Count (Step : Integer) return Integer is
		X : Integer := 0;
	begin
		for I in 1 .. 42 loop
			X := X + Step;
		end loop;
		return X;
	end Count;

	procedure Test (Step : Integer; Result : out Integer) is
	begin
		Result := Count (Step);
	end Test;

	Code : Integer;

begin
	Test (10, Code);
	Ada.Command_Line.Set_Exit_Status (Ada.Command_Line.Exit_Status (Code));
end ExitCode;
