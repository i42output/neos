--  Ada.Tags.adb
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

package body Ada.Tags is

   function Name_Of (T : Tag) return String;
   pragma Import (Neos, Name_Of, "ada::tag_name");
   function Abstract_Of (T : Tag) return Boolean;
   pragma Import (Neos, Abstract_Of, "ada::tag_abstract");
   function External_Of (T : Tag) return String;
   pragma Import (Neos, External_Of, "ada::tag_external");
   function Parent_Of (T : Tag) return Tag;
   pragma Import (Neos, Parent_Of, "ada::tag_parent");
   type Cell is mod 2 ** 64;
   function Table_Cell return Cell;
   pragma Import (Neos, Table_Cell, "neos::tag_table");
   function Internal_Of (Name : String; In_Cell : Cell) return Tag;
   pragma Import (Neos, Internal_Of, "ada::tag_internal");

   function Expanded_Name (T : Tag) return String is
   begin
      if T = No_Tag then
         raise Tag_Error;
      end if;
      return Name_Of (T);
   end Expanded_Name;

   function Wide_Expanded_Name (T : Tag) return Wide_String is
      Name   : constant String := Expanded_Name (T);
      Result : Wide_String (Name'Range);
   begin
      for I in Name'Range loop
         Result (I) := Wide_Character'Val (Character'Pos (Name (I)));
      end loop;
      return Result;
   end Wide_Expanded_Name;

   function Wide_Wide_Expanded_Name (T : Tag) return Wide_Wide_String is
      Name   : constant String := Expanded_Name (T);
      Result : Wide_Wide_String (Name'Range);
   begin
      for I in Name'Range loop
         Result (I) := Wide_Wide_Character'Val (Character'Pos (Name (I)));
      end loop;
      return Result;
   end Wide_Wide_Expanded_Name;

   --  (a type's external tag: its expanded name)
   function External_Tag (T : Tag) return String is
   begin
      if T = No_Tag then
         raise Tag_Error;
      end if;
      return External_Of (T);
   end External_Tag;

   function Internal_Tag (External : String) return Tag is
      Result : constant Tag := Internal_Of (External, Table_Cell);
   begin
      if Result = No_Tag then
         raise Tag_Error;
      end if;
      return Result;
   end Internal_Tag;

   function Is_Descendant (Descendant, Ancestor : Tag) return Boolean is
      T : Tag := Descendant;
   begin
      while T /= No_Tag loop
         if T = Ancestor then
            return True;
         end if;
         T := Parent_Of (T);
      end loop;
      return False;
   end Is_Descendant;

   function Descendant_Tag (External : String; Ancestor : Tag) return Tag is
      Result : constant Tag := Internal_Tag (External);
   begin
      if not Is_Descendant (Result, Ancestor) then
         raise Tag_Error;
      end if;
      return Result;
   end Descendant_Tag;

   function Is_Descendant_At_Same_Level (Descendant, Ancestor : Tag)
      return Boolean is
   begin
      return Is_Descendant (Descendant, Ancestor);
   end Is_Descendant_At_Same_Level;

   function Parent_Tag (T : Tag) return Tag is
   begin
      if T = No_Tag then
         raise Tag_Error;
      end if;
      return Parent_Of (T);
   end Parent_Tag;

   function Interface_Ancestor_Tags (T : Tag) return Tag_Array is
   begin
      if T = No_Tag then
         raise Tag_Error;
      end if;
      return (1 .. 0 => No_Tag);
   end Interface_Ancestor_Tags;

   function Is_Abstract (T : Tag) return Boolean is
   begin
      if T = No_Tag then
         raise Tag_Error;
      end if;
      return Abstract_Of (T);
   end Is_Abstract;

end Ada.Tags;
