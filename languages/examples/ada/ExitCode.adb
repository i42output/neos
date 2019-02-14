-- Example Ada program

with Process;
use Process;

package body Example is

	ExitCode : Integer;
	
	function Count(Step : Integer) return Integer is
	begin
		Integer x := 0;
		for i in 1 .. 42  loop
			x := x + Step;
		end loop;
		return x;
	end Count;

	procedure Test(Step : Integer; Result : out Integer) is
	begin
		Result = Count(C);
	end test;

begin
		Test(10, ExitCode);
		Process.SetExitCode(ExitCode);
end Example;