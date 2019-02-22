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