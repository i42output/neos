--  Neos.File.Stream.ads
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

with System;

package Neos.File.Stream is

   subtype Handle is System.Address; 
   subtype int is Integer;
   subtype size_t is System.ObjectSize;
   subtype cstring is System.Address;
   subtype buffer is System.Address;

   Error : constant Handle; 

   function stdin  return Handle; 
   function stdout return Handle; 
   function stderr return Handle; 

   function fputc(C : int; Stream : Handle) return int; 
   function fputs(S : cstring; Stream : Handle) return int; 
   function fwrite(Buffer: buffer; Size, Count : size_t; Stream : Handle) return size_t;
   function fread(Buffer: buffer; Size, Count : size_t; Stream : Handle) return size_t;

private

   pragma Import (neos, stderr, "process.stderr");
   pragma Import (neos, stdin,  "process.stdin");
   pragma Import (neos, stdout, "process.stdout");

   pragma Import (neos, fputc);
   pragma Import (neos, fputs);
   pragma Import (neos, fwrite);
   pragma Import (neos, fread);

   Error : constant Handle := System.Null_Address; 

end Neos.File_Stream;