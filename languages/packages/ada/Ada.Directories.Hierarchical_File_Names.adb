--  Ada.Directories.Hierarchical_File_Names.adb
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

--  The names are those of Ada.Directories (see its body). "." and ".." are
--  simple names (as well as current and parent directory names); a root is
--  "/" (on Windows, also a drive, "C:\" or "C:", or "\\server\share\"),
--  with any separators that follow it.

package body Ada.Directories.Hierarchical_File_Names is

   function Has_Separator (S : String) return Boolean is
   begin
      for C of S loop
         if Is_Separator (C) then
            return True;
         end if;
      end loop;
      return False;
   end Has_Separator;

   --  S with lower bound 1
   function Slide (S : String) return String is
      Result : constant String (1 .. S'Length) := S;
   begin
      return Result;
   end Slide;

   function Is_Simple_Name (Name : in String) return Boolean is
   begin
      return Is_Valid_Name (Name) and then not Has_Separator (Name)
        and then Root_Length (Name) = 0;
   end Is_Simple_Name;

   function Is_Root_Directory_Name (Name : in String) return Boolean is
   begin
      return Is_Valid_Name (Name) and then Root_Length (Name) = Name'Length;
   end Is_Root_Directory_Name;

   function Is_Parent_Directory_Name (Name : in String) return Boolean is
   begin
      return Name = "..";
   end Is_Parent_Directory_Name;

   function Is_Current_Directory_Name (Name : in String) return Boolean is
   begin
      return Name = ".";
   end Is_Current_Directory_Name;

   function Is_Full_Name (Name : in String) return Boolean is
   begin
      return Is_Valid_Name (Name) and then Root_Length (Name) > 0;
   end Is_Full_Name;

   function Is_Relative_Name (Name : in String) return Boolean is
   begin
      return Is_Valid_Name (Name) and then Root_Length (Name) = 0;
   end Is_Relative_Name;

   --  The last index of Name's initial directory part
   function Initial_Last (Name : String) return Integer is
      Root : constant Natural := Root_Length (Name);
      I    : Integer := Name'First;
   begin
      if Root > 0 then
         return Name'First + Root - 1;
      end if;
      while I <= Name'Last and then not Is_Separator (Name (I)) loop
         I := I + 1;
      end loop;
      return I - 1;
   end Initial_Last;

   function Initial_Directory (Name : in String) return String is
   begin
      if not Is_Valid_Name (Name) then
         raise Name_Error;
      end if;
      return Slide (Name (Name'First .. Initial_Last (Name)));
   end Initial_Directory;

   function Relative_Name (Name : in String) return String is
      I : Integer;
   begin
      if not Is_Valid_Name (Name) then
         raise Name_Error;
      end if;
      I := Initial_Last (Name) + 1;
      while I <= Name'Last and then Is_Separator (Name (I)) loop
         I := I + 1;
      end loop;
      if I > Name'Last then
         raise Name_Error;  --  (a single part)
      end if;
      return Slide (Name (I .. Name'Last));
   end Relative_Name;

   function Compose (Directory      : in String := "";
                     Relative_Name  : in String;
                     Extension      : in String := "") return String
   is
   begin
      if (Directory'Length > 0 and then not Is_Valid_Name (Directory))
        or else not Is_Relative_Name (Relative_Name)
        or else (Extension'Length > 0
                 and then (not Is_Valid_Name (Extension)
                           or else Has_Separator (Extension)))
      then
         raise Name_Error;
      end if;
      if Extension'Length > 0 then
         declare
            Simple : constant String := Simple_Name (Relative_Name);
         begin
            if Simple = "." or else Simple = ".." then
               raise Name_Error;
            end if;
         end;
      end if;
      declare
         Joined : constant String :=
           (if Directory'Length = 0 then ""
            elsif Is_Separator (Directory (Directory'Last))
              or else Root_Length (Directory) = Directory'Length
            then Directory
            else Directory & (if Is_Separator ('\') then '\' else '/'))
           & Relative_Name;
      begin
         if Extension'Length > 0 then
            return Slide (Joined & '.' & Extension);
         end if;
         return Slide (Joined);
      end;
   end Compose;

end Ada.Directories.Hierarchical_File_Names;
