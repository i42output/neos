package System is

	type Name is (SYSTEM_NAME_NEOS);

	System_Name : constant Name := SYSTEM_NAME_NEOS; 

	--  System-Dependent Named Numbers

	Min_Int               : constant := Long_Long_Integer'First; 
	Max_Int               : constant := Long_Long_Integer'Last; 

	Max_Binary_Modulus    : constant := 2 ** Long_Long_Integer'Size; 
	Max_Nonbinary_Modulus : constant := Integer'Last; 

	Max_Base_Digits       : constant := Long_Long_Float'Digits; 
	Max_Digits            : constant := Long_Long_Float'Digits; 

	Max_Mantissa          : constant := 63; 
	Fine_Delta            : constant := 2.0 ** (-Max_Mantissa); 

	Tick                  : constant := 1.0; 

	--  Storage-related Declarations

	type Address is private; 
	Null_Address : constant Address; 
	type ObjectSize is private;

	Storage_Unit : constant := 8; 
	Word_Size    : constant := 64; 
	Memory_Size  : constant := 2 ** 64; 

	--  Address comparison

	function "<"  (Left, Right : Address) return Boolean; 
	function "<=" (Left, Right : Address) return Boolean; 
	function ">"  (Left, Right : Address) return Boolean; 
	function ">=" (Left, Right : Address) return Boolean; 
	function "="  (Left, Right : Address) return Boolean; 

	pragma Import (Intrinsic, "<");
	pragma Import (Intrinsic, "<=");
	pragma Import (Intrinsic, ">");
	pragma Import (Intrinsic, ">=");
	pragma Import (Intrinsic, "=");

	--  Other System-Dependent Declarations

	type Bit_Order is (High_Order_First, Low_Order_First); 
	Default_Bit_Order : constant Bit_Order := Low_Order_First; 

	--  Priority-related Declarations (RM D.1)

	Max_Priority           : constant Positive := 30; 
	Max_Interrupt_Priority : constant Positive := 31; 

	subtype Any_Priority       is Integer      range  0 .. 31; 
	subtype Priority           is Any_Priority range  0 .. 30; 
	subtype Interrupt_Priority is Any_Priority range 31 .. 31; 

	Default_Priority : constant Priority := 15; 

private

	type Address is mod Memory_Size; 
	Null_Address : constant Address := 0; 
	type ObjectSize is mod Memory_Size; 

end System;