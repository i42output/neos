package body Process is

	function ExitCode return Integer;
	pragma Import (neos, ExitCode, "process.exit_code");
	
	procedure SetExitCode(Code : Integer);
	pragma Import (neos, SetExitCode, "process.set_exit_code");

begin
	SetExitCode(0);
end Process;