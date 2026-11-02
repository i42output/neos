--  Ada.Locales.adb
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

--  The active locale is the host's: on Windows, the user's default locale;
--  elsewhere, the locale named by the environment variable LC_ALL, else
--  LC_MESSAGES, else LANG (e.g. "en_GB.UTF-8"). Its two letter language code
--  (ISO 639-1) is given as the corresponding ISO 639-3 code (see the native
--  primitive locale in lib_os.cpp).

with Interfaces;
with System;

package body Ada.Locales is

   use type Interfaces.Integer_64;

   function Locale (Buffer   : System.Address;
                    Capacity : Interfaces.Integer_64)
     return Interfaces.Integer_64;
   pragma Import (Neos, Locale, "ada::locale");

   --  The language (1 .. 3) and the country (4 .. 5)
   function Get return String is
      Result : String (1 .. 5) := "undZZ";
   begin
      if Locale (Result'Address, Result'Length) /= Result'Length then
         return "undZZ";
      end if;
      return Result;
   end Get;

   function Language return Language_Code is
      Code : constant String := Get;
   begin
      for C of Code (1 .. 3) loop
         if C not in 'a' .. 'z' then
            return Language_Unknown;
         end if;
      end loop;
      return Language_Code (Code (1 .. 3));
   end Language;

   function Country return Country_Code is
      Code : constant String := Get;
   begin
      for C of Code (4 .. 5) loop
         if C not in 'A' .. 'Z' then
            return Country_Unknown;
         end if;
      end loop;
      return Country_Code (Code (4 .. 5));
   end Country;

end Ada.Locales;
