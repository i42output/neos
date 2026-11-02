--  Ada.Environment_Variables.adb
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

--  The environment variables are the host process's (see the native
--  primitives env_* in lib_os.cpp): changes made by other means (e.g. by the
--  host, or by other code of the program through the operating system) are
--  seen at once, and this package keeps no copy of them. A name that is
--  empty or contains '=' or NUL never exists; Set raises Constraint_Error for
--  such a name, or for a value that contains NUL. (On Windows, Set with an
--  empty value has the effect of Clear.)

with Interfaces;
with System;

package body Ada.Environment_Variables is

   use type Interfaces.Integer_64;

   subtype Integer_64 is Interfaces.Integer_64;

   function Env_Value (Name     : System.Address;
                       Length   : Integer_64;
                       Buffer   : System.Address;
                       Capacity : Integer_64) return Integer_64;
   pragma Import (Neos, Env_Value, "ada::env_value");

   function Env_Set (Name         : System.Address;
                     Length       : Integer_64;
                     Value        : System.Address;
                     Value_Length : Integer_64) return Integer_64;
   pragma Import (Neos, Env_Set, "ada::env_set");

   function Env_Clear (Name : System.Address; Length : Integer_64)
     return Integer_64;
   pragma Import (Neos, Env_Clear, "ada::env_clear");

   procedure Env_Clear_All;
   pragma Import (Neos, Env_Clear_All, "ada::env_clear_all");

   function Env_Count return Integer_64;
   pragma Import (Neos, Env_Count, "ada::env_count");

   function Env_Entry (Index    : Integer_64;
                       Buffer   : System.Address;
                       Capacity : Integer_64) return Integer_64;
   pragma Import (Neos, Env_Entry, "ada::env_entry");

   --  A string's address (null for an empty string)
   function Address_Of (S : String) return System.Address is
   begin
      if S'Length = 0 then
         return System.Null_Address;
      end if;
      return S'Address;
   end Address_Of;

   --  The value of a variable: Found is False if it does not exist
   procedure Get (Name : String; Found : out Boolean; Length : out Natural)
   is
      L : constant Integer_64 :=
        Env_Value (Address_Of (Name), Name'Length, System.Null_Address, 0);
   begin
      Found := L >= 0;
      Length := (if L >= 0 then Natural (L) else 0);
   end Get;

   function Get_Value (Name : String; Default : String) return String is
      Found  : Boolean;
      Length : Natural;
   begin
      loop
         Get (Name, Found, Length);
         if not Found then
            return Default;
         elsif Length = 0 then
            return "";
         end if;
         declare
            Result : String (1 .. Length);
            Got    : constant Integer_64 :=
              Env_Value (Address_Of (Name), Name'Length, Result'Address,
                         Integer_64 (Length));
         begin
            if Got = Integer_64 (Length) then
               return Result;
            elsif Got < 0 then
               return Default;
            end if;
            --  (it changed: again)
         end;
      end loop;
   end Get_Value;

   function Value (Name : in String) return String is
   begin
      if not Exists (Name) then
         raise Constraint_Error;
      end if;
      return Get_Value (Name, "");
   end Value;

   function Value (Name : in String; Default : in String) return String is
   begin
      return Get_Value (Name, Default);
   end Value;

   function Exists (Name : in String) return Boolean is
      Found  : Boolean;
      Length : Natural;
   begin
      Get (Name, Found, Length);
      return Found;
   end Exists;

   procedure Set (Name : in String; Value : in String) is
   begin
      if Env_Set (Address_Of (Name), Name'Length,
                  Address_Of (Value), Value'Length) /= 0
      then
         raise Constraint_Error;
      end if;
   end Set;

   procedure Clear (Name : in String) is
      Status : constant Integer_64 := Env_Clear (Address_Of (Name), Name'Length);
   begin
      if Status /= 0 then
         raise Constraint_Error;
      end if;
   end Clear;

   procedure Clear is
   begin
      Env_Clear_All;
   end Clear;

   --  Variable Index: "name=value" ("" if there is none)
   function Entry_Of (Index : Integer_64) return String is
      Length : Integer_64;
   begin
      loop
         Length := Env_Entry (Index, System.Null_Address, 0);
         if Length <= 0 then
            return "";
         end if;
         declare
            Result : String (1 .. Natural (Length));
            Got    : constant Integer_64 :=
              Env_Entry (Index, Result'Address, Length);
         begin
            if Got = Length then
               return Result;
            elsif Got < 0 then
               return "";
            end if;
         end;
      end loop;
   end Entry_Of;

   procedure Iterate
      (Process : not null access procedure (Name, Value : in String))
   is
      Count : constant Integer_64 := Env_Count;
   begin
      for Index in 0 .. Count - 1 loop
         declare
            Item : constant String := Entry_Of (Index);
         begin
            for I in Item'Range loop
               if Item (I) = '=' then
                  Process (Item (Item'First .. I - 1), Item (I + 1 .. Item'Last));
                  exit;
               end if;
            end loop;
         end;
      end loop;
   end Iterate;

end Ada.Environment_Variables;
