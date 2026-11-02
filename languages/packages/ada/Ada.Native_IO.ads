--  Ada.Native_IO.ads
--
--  Copyright (c) 2026 Leigh Johnston.  All Rights Reserved.
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

--  The native primitives of the input-output packages (see lib_io.cpp in
--  the Ada runtime): external files are identified by integer handles; 0,
--  1 and 2 are the standard input, output and error.

with Interfaces;
with System;

private package Ada.Native_IO is
   pragma Preelaborate;

   use type Interfaces.Integer_64;

   subtype Int is Interfaces.Integer_64;

   Standard_Input_Handle  : constant Int := 0;
   Standard_Output_Handle : constant Int := 1;
   Standard_Error_Handle  : constant Int := 2;
   No_Handle              : constant Int := -1;

   type Open_Mode is
     (Read,               --  an existing file, read only
      Write,              --  created or truncated, write only
      Append,             --  created if need be, written at its end
      Read_Write,         --  an existing file, read and write
      Read_Write_Create,  --  created or truncated, read and write
      Read_Append);       --  created if need be, read, written at its end

   --  Each raises the Reference Manual's exception for an error (Name_Error,
   --  Use_Error or Device_Error). The files opened by Open and Open_Temporary
   --  that are still open when the program ends are closed then.

   function Open
     (Name : String; Mode : Open_Mode; Must_Exist : Boolean := False)
      return Int;
   function Open_Temporary return Int;  --  (read/write; deleted when closed)
   procedure Reopen (Handle : Int; Mode : Open_Mode);
   procedure Close (Handle : Int);
   procedure Delete (Name : String);
   function Name (Handle : Int) return String;  --  (the full name)

   function Read
     (Handle : Int; Buffer : System.Address; Length : Int) return Int;
   --  The number of bytes read (0: end of file); the standard input is read
   --  a line at a time.
   procedure Write
     (Handle     : Int;
      Data       : System.Address;
      Length     : Int;
      Line_Feeds : Int := 0);
   procedure Write (Handle : Int; Data : String; Line_Feeds : Int := 0);
   procedure Flush (Handle : Int);
   function At_End (Handle : Int) return Boolean;
   procedure Seek (Handle : Int; Position : Int);  --  (bytes from the start)
   function Size (Handle : Int) return Int;

   --  The exact decimal value of X: a sign ('+' or '-'), its significant
   --  digits (none for a zero), 'e' and the decimal exponent E, so that X is
   --  sign 0.digits * 10 ** E ("+15e1" for 1.5, "-e0" for -0.0); "NaN",
   --  "+Inf" or "-Inf" if X is not finite.
   function Float_Digits (X : Long_Float) return String;

   --  The value of a decimal literal "ddd.dddE+xx" (digits, an optional
   --  point and digits, an optional exponent) or of a hexadecimal one
   --  "0xhhh.hhhp+xx", correctly rounded (infinite if too large).
   function Float_Value (Text : String) return Long_Float;

   --  UTF-8 (file names that are wide strings): the characters of a
   --  Wide_String are code points (no surrogate pairs); an invalid sequence
   --  is decoded as the code points of its bytes; a code point beyond
   --  Wide_Character is decoded as U+FFFD in a Wide_String.
   function Encode (Item : Wide_Wide_String) return String;
   function Encode (Item : Wide_String) return String;
   function Decode (Item : String) return Wide_Wide_String;
   function Decode (Item : String) return Wide_String;

end Ada.Native_IO;
