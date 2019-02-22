--  Build with the following command:
-- 
--     gnatmake ada_numeric_literals.adb -cargs -gnata
-- 
--  Output should be:
-- 
--     Z =  0.00000E+00
-- 
--     Bin_136 =  136
--     Oct_136 =  136
--     Dec_136 =  136
--     Hex_136 =  136
-- 
--     Output_Devices (lights on ) =  136
--     Output_Devices (lights off) =  0
-- 
--     Zero        =  0.00000E+00
--     Zero_Approx =  1.00000E-29
--     Nil         =  0
-- 

with Ada.Text_IO;

procedure Ada_Numeric_Literals is
   Pi   : constant := 3.14159_26535_89793_23846_26433_83279_50288_41971_69399_37510;
   Pi2  : constant := 3.14159265358979323846264338327950288419716939937510;
   Z    : constant := Pi - Pi2;
   pragma Assert (Z = 0.0);

   Bin_136 : constant := 2#1000_1000#;
   Oct_136 : constant := 8#210#;
   Dec_136 : constant := 10#136#;
   Hex_136 : constant := 16#88#;
   pragma Assert (Bin_136 = 136);
   pragma Assert (Oct_136 = 136);
   pragma Assert (Dec_136 = 136);
   pragma Assert (Hex_136 = 136);

   Lights_On  : constant := 2#1000_1000#;
   Lights_Off : constant := 2#0111_0111#;

   type Byte is mod 256;
   Output_Devices : Byte := 0;
   --  for Output_Devices'Address use 16#DEAD_BEEF#;  --  Memory mapped Output

   Kilobyte  : constant := 2#1#e+10;
   Megabyte  : constant := 2#1#e+20;
   Gigabyte  : constant := 2#1#e+30;
   Terabyte  : constant := 2#1#e+40;
   Petabyte  : constant := 2#1#e+50;
   Exabyte   : constant := 2#1#e+60;
   Zettabyte : constant := 2#1#e+70;
   Yottabyte : constant := 2#1#e+80;

   One_Third : constant := 3#1.0#e-1;  --  same as 1.0/3.0
   Zero      : constant := 1.0 - 3.0 * One_Third;
   pragma Assert (Zero = 0.0);

   One_Third_Approx : constant := 0.33333333333333333333333333333;
   Zero_Approx      : constant := 1.0 - 3.0 * One_Third_Approx;

   Big_Sum : constant := 1         +
                         Kilobyte  +
                         Megabyte  +
                         Gigabyte  +
                         Terabyte  +
                         Petabyte  +
                         Exabyte   +
                         Zettabyte;

   Result : constant := (Yottabyte - 1) / (Kilobyte - 1);

   Nil : constant := Result - Big_Sum;
   pragma Assert (Nil = 0);

   use Ada.Text_IO;

begin
   Put_Line ("Z = " & Float'Image (Z));
   New_Line;
   Put_Line ("Bin_136 = " & Integer'Image (Bin_136));
   Put_Line ("Oct_136 = " & Integer'Image (Oct_136));
   Put_Line ("Dec_136 = " & Integer'Image (Dec_136));
   Put_Line ("Hex_136 = " & Integer'Image (Hex_136));
   New_Line;
   Output_Devices := Output_Devices  or   Lights_On;
   Put_Line ("Output_Devices (lights on ) = " & Byte'Image (Output_Devices));
   Output_Devices := Output_Devices  and  Lights_Off;
   Put_Line ("Output_Devices (lights off) = " & Byte'Image (Output_Devices));
   New_Line;
   Put_Line ("Zero        = " & Float'Image (Zero));
   Put_Line ("Zero_Approx = " & Float'Image (Zero_Approx));
   Put_Line ("Nil         = " & Integer'Image (Nil));
end Ada_Numeric_Literals; 