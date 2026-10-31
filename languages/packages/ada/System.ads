--  System.ads
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
--
--  This package specification is an altered copy of the specification of
--  package System in the Ada Reference Manual (ISO/IEC 8652:1995, 13.7), which
--  carries the following notice:
--
--    Copyright © 1992, 1993, 1994, 1995, Intermetrics, Inc.
--
--    This copyright is assigned to the U.S. Government. All rights reserved.
--
--    This document may be copied, in whole or in part, in any form or by any
--    means, as is or with alterations, provided that (1) alterations are clearly
--    marked as alterations and (2) this copyright notice is included unmodified
--    in any copy. Compiled copies of standard library units and examples need
--    not contain this copyright notice so long as the notice is included in all
--    copies of source code and documentation.
--
--  Alterations: the implementation-defined values are neos's, and System_Name
--  (SYSTEM_NAME_NEOS), the type ObjectSize and the pragmas Import are neos's
--  (see languages/packages/Ada/NOTICE).

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