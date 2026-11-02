--  Ada.Directories.adb
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

--  File names are the host's (see the native primitives in lib_os.cpp): on
--  Windows '\' and '/' separate their parts and a full name starts with a
--  drive ("C:\") or a server and share ("\\server\share\"); elsewhere '/'
--  separates the parts and a full name starts with '/'. A name cannot be
--  empty or contain NUL (nor, on Windows, any of < > " | ? *). Simple_Name
--  and Containing_Directory ignore separators at the end of a name, and
--  Containing_Directory of a simple name is "." (the default directory).
--  Full_Name resolves "." and ".." parts (but not symbolic links).
--
--  The Form parameters are ignored. Copy_File replaces an existing target
--  file. Directory searches give every entry of the directory, "." and ".."
--  included; a pattern matches simple names as the shell does: '*' matches
--  any characters, '?' any one, and [...] any one in the set ([!...] any one
--  not in it; a-z a range); on Windows letters match either case. A pattern
--  that contains a separator, or a '[' with no ']', raises Name_Error. The
--  kind, size and modification time of an entry are those when it was
--  found; an entry that the host cannot classify (e.g. a symbolic link to
--  nothing) is a special file.

with Ada.Calendar.Formatting;
with Ada.Unchecked_Conversion;
with Ada.Unchecked_Deallocation;
with System;

package body Ada.Directories is

   use type Interfaces.Integer_64;

   subtype Integer_64 is Interfaces.Integer_64;

   function OS_Windows return Integer_64;
   pragma Import (Neos, OS_Windows, "ada::os_windows");

   function Native_Current_Directory (Buffer   : System.Address;
                                      Capacity : Integer_64)
     return Integer_64;
   pragma Import (Neos, Native_Current_Directory, "ada::current_directory");

   function Native_Set_Directory (Name : System.Address; Length : Integer_64)
     return Integer_64;
   pragma Import (Neos, Native_Set_Directory, "ada::set_directory");

   function Native_Create_Directory (Name   : System.Address;
                                     Length : Integer_64) return Integer_64;
   pragma Import (Neos, Native_Create_Directory, "ada::create_directory");

   function Native_Create_Path (Name : System.Address; Length : Integer_64)
     return Integer_64;
   pragma Import (Neos, Native_Create_Path, "ada::create_path");

   function Native_Delete_Directory (Name   : System.Address;
                                     Length : Integer_64) return Integer_64;
   pragma Import (Neos, Native_Delete_Directory, "ada::delete_directory");

   function Native_Delete_Tree (Name : System.Address; Length : Integer_64)
     return Integer_64;
   pragma Import (Neos, Native_Delete_Tree, "ada::delete_tree");

   function Native_Delete_File (Name : System.Address; Length : Integer_64)
     return Integer_64;
   pragma Import (Neos, Native_Delete_File, "ada::delete_file");

   function Native_Rename (Old_Name   : System.Address;
                           Old_Length : Integer_64;
                           New_Name   : System.Address;
                           New_Length : Integer_64) return Integer_64;
   pragma Import (Neos, Native_Rename, "ada::rename");

   function Native_Copy_File (Source        : System.Address;
                              Source_Length : Integer_64;
                              Target        : System.Address;
                              Target_Length : Integer_64) return Integer_64;
   pragma Import (Neos, Native_Copy_File, "ada::copy_file");

   function Native_Kind (Name : System.Address; Length : Integer_64)
     return Integer_64;
   pragma Import (Neos, Native_Kind, "ada::file_kind");

   function Native_Size (Name : System.Address; Length : Integer_64)
     return Integer_64;
   pragma Import (Neos, Native_Size, "ada::file_size");

   function Native_Time (Name : System.Address; Length : Integer_64)
     return Integer_64;
   pragma Import (Neos, Native_Time, "ada::file_time");

   function Native_Full_Name (Name     : System.Address;
                              Length   : Integer_64;
                              Buffer   : System.Address;
                              Capacity : Integer_64) return Integer_64;
   pragma Import (Neos, Native_Full_Name, "ada::full_name");

   function Native_Name_Case (Name : System.Address; Length : Integer_64)
     return Integer_64;
   pragma Import (Neos, Native_Name_Case, "ada::name_case");

   function Native_Open (Name : System.Address; Length : Integer_64)
     return Integer_64;
   pragma Import (Neos, Native_Open, "ada::dir_open");

   function Native_Next (Handle   : Integer_64;
                         Buffer   : System.Address;
                         Capacity : Integer_64) return Integer_64;
   pragma Import (Neos, Native_Next, "ada::dir_next");

   procedure Native_Close (Handle : Integer_64);
   pragma Import (Neos, Native_Close, "ada::dir_close");

   Windows : constant Boolean := OS_Windows /= 0;

   Separator : constant Character := (if Windows then '\' else '/');

   --  (the native primitives' status codes)
   Done          : constant := 0;
   No_Such_Name  : constant := 1;

   No_Such_File  : constant Integer_64 := Integer_64'First;
   Time_Unknown  : constant Integer_64 := Integer_64'First + 1;

   procedure Free is new Ada.Unchecked_Deallocation (String, String_Access);

   function To_Duration is new Ada.Unchecked_Conversion (Integer_64, Duration);

   --  A string's address (null for an empty string)
   function Address_Of (S : String) return System.Address is
   begin
      if S'Length = 0 then
         return System.Null_Address;
      end if;
      return S'Address;
   end Address_Of;

   function Length_Of (S : String) return Integer_64 is
   begin
      return Integer_64 (S'Length);
   end Length_Of;

   --  S with lower bound 1
   function Slide (S : String) return String is
      Result : constant String (1 .. S'Length) := S;
   begin
      return Result;
   end Slide;

   --  Raise the exception of a native operation's status
   procedure Check (Status : Integer_64) is
   begin
      if Status = Done then
         return;
      elsif Status = No_Such_Name then
         raise Name_Error;
      else
         raise Use_Error;
      end if;
   end Check;

   procedure Check_Name (Name : String) is
   begin
      if not Is_Valid_Name (Name) then
         raise Name_Error;
      end if;
   end Check_Name;

   ----------------------
   -- Name syntax --
   ----------------------

   function Is_Separator (C : Character) return Boolean is
   begin
      return C = '/' or else (Windows and then C = '\');
   end Is_Separator;

   function Root_Length (Name : String) return Natural is
      F : constant Integer := Name'First;
      I : Integer := F;
   begin
      if Windows then
         if Name'Length >= 2 and then Is_Separator (Name (F))
           and then Is_Separator (Name (F + 1))
         then
            --  \\server\share\
            I := F + 2;
            while I <= Name'Last and then not Is_Separator (Name (I)) loop
               I := I + 1;
            end loop;
            I := I + 1;
            while I <= Name'Last and then not Is_Separator (Name (I)) loop
               I := I + 1;
            end loop;
         elsif Name'Length >= 2 and then Name (F + 1) = ':'
           and then (Name (F) in 'A' .. 'Z' or else Name (F) in 'a' .. 'z')
         then
            I := F + 2;
         end if;
      end if;
      while I <= Name'Last and then Is_Separator (Name (I)) loop
         I := I + 1;
      end loop;
      return Integer'Min (I, Name'Last + 1) - F;
   end Root_Length;

   function Is_Valid_Name (Name : String) return Boolean is
   begin
      if Name'Length = 0 then
         return False;
      end if;
      for C of Name loop
         if C = Character'Val (0)
           or else (Windows and then (C = '<' or else C = '>' or else C = '"'
                                      or else C = '|' or else C = '?'
                                      or else C = '*'))
         then
            return False;
         end if;
      end loop;
      return True;
   end Is_Valid_Name;

   --  The last index of Name without its separators at the end (but not
   --  less than the end of its root)
   function Last_Of (Name : String) return Integer is
      Root : constant Integer := Name'First + Root_Length (Name) - 1;
      L    : Integer := Name'Last;
   begin
      while L > Root and then Is_Separator (Name (L)) loop
         L := L - 1;
      end loop;
      return L;
   end Last_Of;

   ------------------------------------------
   -- Directory and file operations --
   ------------------------------------------

   function Current_Directory return String is
      Length : Integer_64;
   begin
      loop
         Length := Native_Current_Directory (System.Null_Address, 0);
         if Length <= 0 then
            raise Use_Error;
         end if;
         declare
            Result : String (1 .. Natural (Length));
         begin
            if Native_Current_Directory (Result'Address, Length) = Length then
               return Result;
            end if;
         end;
      end loop;
   end Current_Directory;

   procedure Set_Directory (Directory : in String) is
   begin
      Check_Name (Directory);
      Check (Native_Set_Directory (Address_Of (Directory),
                                   Length_Of (Directory)));
   end Set_Directory;

   procedure Create_Directory (New_Directory : in String;
                               Form          : in String := "")
   is
   begin
      Check_Name (New_Directory);
      Check (Native_Create_Directory (Address_Of (New_Directory),
                                      Length_Of (New_Directory)));
   end Create_Directory;

   procedure Delete_Directory (Directory : in String) is
   begin
      Check_Name (Directory);
      Check (Native_Delete_Directory (Address_Of (Directory),
                                      Length_Of (Directory)));
   end Delete_Directory;

   procedure Create_Path (New_Directory : in String;
                          Form          : in String := "")
   is
   begin
      Check_Name (New_Directory);
      Check (Native_Create_Path (Address_Of (New_Directory),
                                 Length_Of (New_Directory)));
   end Create_Path;

   procedure Delete_Tree (Directory : in String) is
   begin
      Check_Name (Directory);
      Check (Native_Delete_Tree (Address_Of (Directory),
                                 Length_Of (Directory)));
   end Delete_Tree;

   procedure Delete_File (Name : in String) is
   begin
      Check_Name (Name);
      Check (Native_Delete_File (Address_Of (Name), Length_Of (Name)));
   end Delete_File;

   procedure Rename (Old_Name, New_Name : in String) is
   begin
      Check_Name (Old_Name);
      Check_Name (New_Name);
      Check (Native_Rename (Address_Of (Old_Name), Length_Of (Old_Name),
                            Address_Of (New_Name), Length_Of (New_Name)));
   end Rename;

   procedure Copy_File (Source_Name,
                        Target_Name : in String;
                        Form        : in String := "")
   is
   begin
      Check_Name (Source_Name);
      Check_Name (Target_Name);
      Check (Native_Copy_File
               (Address_Of (Source_Name), Length_Of (Source_Name),
                Address_Of (Target_Name), Length_Of (Target_Name)));
   end Copy_File;

   ----------------------------------------
   -- File and directory name operations --
   ----------------------------------------

   function Full_Name (Name : in String) return String is
      Length : Integer_64;
   begin
      Check_Name (Name);
      loop
         Length := Native_Full_Name (Address_Of (Name), Length_Of (Name),
                                     System.Null_Address, 0);
         if Length <= 0 then
            raise Name_Error;
         end if;
         declare
            Result : String (1 .. Natural (Length));
         begin
            if Native_Full_Name (Address_Of (Name), Length_Of (Name),
                                 Result'Address, Length) = Length
            then
               return Result;
            end if;
         end;
      end loop;
   end Full_Name;

   function Simple_Name (Name : in String) return String is
      Root  : Natural;
      Last  : Integer;
      First : Integer;
   begin
      Check_Name (Name);
      Root := Root_Length (Name);
      Last := Last_Of (Name);
      if Last < Name'First + Root then
         return Slide (Name (Name'First .. Name'First + Root - 1));
      end if;
      First := Last;
      while First > Name'First + Root and then not Is_Separator (Name (First - 1))
      loop
         First := First - 1;
      end loop;
      return Slide (Name (First .. Last));
   end Simple_Name;

   function Containing_Directory (Name : in String) return String is
      Root  : Natural;
      Last  : Integer;
      First : Integer;
   begin
      Check_Name (Name);
      Root := Root_Length (Name);
      Last := Last_Of (Name);
      if Last < Name'First + Root then
         raise Use_Error;  --  (a root)
      end if;
      First := Last;
      while First > Name'First + Root and then not Is_Separator (Name (First - 1))
      loop
         First := First - 1;
      end loop;
      if First = Name'First + Root then
         --  (a single part after the root)
         if Root > 0 then
            return Slide (Name (Name'First .. Name'First + Root - 1));
         elsif Name (First .. Last) = "." or else Name (First .. Last) = ".."
         then
            raise Use_Error;
         else
            return ".";
         end if;
      end if;
      Last := First - 1;
      while Last > Name'First + Root - 1 and then Is_Separator (Name (Last)) loop
         Last := Last - 1;
      end loop;
      if Last < Name'First + Root then
         return Slide (Name (Name'First .. Name'First + Root - 1));
      end if;
      return Slide (Name (Name'First .. Last));
   end Containing_Directory;

   --  Whether Name is a root (with any separators at its end)
   function Is_Root (Name : String) return Boolean is
      Root : constant Natural := Root_Length (Name);
   begin
      return Root > 0 and then Last_Of (Name) < Name'First + Root;
   end Is_Root;

   function Extension (Name : in String) return String is
      Simple : constant String := Simple_Name (Name);
   begin
      if Simple = "." or else Simple = ".." or else Is_Root (Name) then
         return "";
      end if;
      for I in reverse Simple'Range loop
         if Simple (I) = '.' then
            return Slide (Simple (I + 1 .. Simple'Last));
         end if;
      end loop;
      return "";
   end Extension;

   function Base_Name (Name : in String) return String is
      Simple : constant String := Simple_Name (Name);
   begin
      if Simple = "." or else Simple = ".." or else Is_Root (Name) then
         return Simple;
      end if;
      for I in reverse Simple'Range loop
         if Simple (I) = '.' then
            return Simple (Simple'First .. I - 1);
         end if;
      end loop;
      return Simple;
   end Base_Name;

   function Has_Separator (S : String) return Boolean is
   begin
      for C of S loop
         if Is_Separator (C) then
            return True;
         end if;
      end loop;
      return False;
   end Has_Separator;

   function Compose (Containing_Directory : in String := "";
                     Name                 : in String;
                     Extension            : in String := "") return String
   is
      Dir : String renames Containing_Directory;
   begin
      if (Dir'Length > 0 and then not Is_Valid_Name (Dir))
        or else not Is_Valid_Name (Name)
        or else (Extension'Length > 0
                 and then (not Is_Valid_Name (Extension)
                           or else Has_Separator (Extension)))
      then
         raise Name_Error;
      end if;
      if Root_Length (Name) = Name'Length then
         --  (a root)
         if Dir'Length > 0 or else Extension'Length > 0 then
            raise Name_Error;
         end if;
         return Slide (Name);
      elsif Has_Separator (Name) then
         raise Name_Error;
      end if;
      declare
         Joined : constant String :=
           (if Dir'Length = 0 then ""
            elsif Is_Separator (Dir (Dir'Last))
              or else Root_Length (Dir) = Dir'Length
            then Dir
            else Dir & Separator)
           & Name;
      begin
         if Extension'Length > 0 then
            return Slide (Joined & '.' & Extension);
         end if;
         return Slide (Joined);
      end;
   end Compose;

   function Name_Case_Equivalence (Name : in String) return Name_Case_Kind is
   begin
      if not Is_Valid_Name (Name) or else Root_Length (Name) = 0 then
         raise Name_Error;
      end if;
      return Name_Case_Kind'Val
               (Native_Name_Case (Address_Of (Name), Length_Of (Name)));
   end Name_Case_Equivalence;

   ------------------------------------
   -- File and directory queries --
   ------------------------------------

   function Kind_Of (Code : Integer_64) return File_Kind is
   begin
      case Code is
         when 0 => return Directory;
         when 1 => return Ordinary_File;
         when others => return Special_File;
      end case;
   end Kind_Of;

   function Exists (Name : in String) return Boolean is
   begin
      Check_Name (Name);
      return Native_Kind (Address_Of (Name), Length_Of (Name)) >= 0;
   end Exists;

   function Kind (Name : in String) return File_Kind is
      Code : Integer_64;
   begin
      Check_Name (Name);
      Code := Native_Kind (Address_Of (Name), Length_Of (Name));
      if Code < 0 then
         raise Name_Error;
      end if;
      return Kind_Of (Code);
   end Kind;

   function Size (Name : in String) return File_Size is
      Result : Integer_64;
   begin
      Check_Name (Name);
      Result := Native_Size (Address_Of (Name), Length_Of (Name));
      if Result < 0 then
         raise Name_Error;
      end if;
      return File_Size (Result);
   end Size;

   --  A native modification time as a Time
   function Time_Of (Nanoseconds : Integer_64) return Ada.Calendar.Time is
      use type Ada.Calendar.Time;
      Unix_Origin : constant Ada.Calendar.Time :=
        Ada.Calendar.Formatting.Time_Of (1970, 1, 1, 0.0, Time_Zone => 0);
   begin
      if Nanoseconds = No_Such_File then
         raise Name_Error;
      elsif Nanoseconds = Time_Unknown then
         raise Use_Error;
      end if;
      return Unix_Origin + To_Duration (Nanoseconds);
   exception
      when Ada.Calendar.Time_Error =>
         raise Use_Error;
   end Time_Of;

   function Modification_Time (Name : in String) return Ada.Calendar.Time is
   begin
      Check_Name (Name);
      return Time_Of (Native_Time (Address_Of (Name), Length_Of (Name)));
   end Modification_Time;

   --------------------------
   -- Directory searching --
   --------------------------

   function Fold (C : Character) return Character is
   begin
      if Windows and then C in 'A' .. 'Z' then
         return Character'Val (Character'Pos (C) + 32);
      end if;
      return C;
   end Fold;

   --  The index of the ']' that ends the set that starts at Pattern (I)
   --  (a '['), or 0 if there is none
   function Set_End (Pattern : String; I : Integer) return Natural is
      J : Integer := I + 1;
   begin
      if J <= Pattern'Last and then (Pattern (J) = '!' or else Pattern (J) = '^')
      then
         J := J + 1;
      end if;
      if J <= Pattern'Last and then Pattern (J) = ']' then
         J := J + 1;  --  (a ']' first is a member)
      end if;
      while J <= Pattern'Last loop
         if Pattern (J) = ']' then
            return J;
         end if;
         J := J + 1;
      end loop;
      return 0;
   end Set_End;

   function Valid_Pattern (Pattern : String) return Boolean is
      I : Integer := Pattern'First;
   begin
      for C of Pattern loop
         if Is_Separator (C) or else C = Character'Val (0) then
            return False;
         end if;
      end loop;
      while I <= Pattern'Last loop
         if Pattern (I) = '[' then
            I := Set_End (Pattern, I);
            if I = 0 then
               return False;
            end if;
         end if;
         I := I + 1;
      end loop;
      return True;
   end Valid_Pattern;

   --  Whether Name matches Pattern (a valid pattern)
   function Match (Name : String; Pattern : String) return Boolean is
      function In_Set (C : Character; First, Last : Integer) return Boolean is
         Negated : constant Boolean :=
           Pattern (First) = '!' or else Pattern (First) = '^';
         J       : Integer := (if Negated then First + 1 else First);
         Found   : Boolean := False;
      begin
         while J <= Last loop
            if J + 2 <= Last and then Pattern (J + 1) = '-' then
               if Fold (C) in Fold (Pattern (J)) .. Fold (Pattern (J + 2)) then
                  Found := True;
               end if;
               J := J + 3;
            else
               if Fold (C) = Fold (Pattern (J)) then
                  Found := True;
               end if;
               J := J + 1;
            end if;
         end loop;
         return Found /= Negated;
      end In_Set;

      function Match_At (N, P : Integer) return Boolean is
         I : Integer := N;
         J : Integer := P;
         E : Natural;
      begin
         while J <= Pattern'Last loop
            case Pattern (J) is
               when '*' =>
                  for K in I .. Name'Last + 1 loop
                     if Match_At (K, J + 1) then
                        return True;
                     end if;
                  end loop;
                  return False;
               when '?' =>
                  if I > Name'Last then
                     return False;
                  end if;
               when '[' =>
                  E := Set_End (Pattern, J);
                  if I > Name'Last or else not In_Set (Name (I), J + 1, E - 1)
                  then
                     return False;
                  end if;
                  J := E;
               when others =>
                  if I > Name'Last or else Fold (Name (I)) /= Fold (Pattern (J))
                  then
                     return False;
                  end if;
            end case;
            I := I + 1;
            J := J + 1;
         end loop;
         return I > Name'Last;
      end Match_At;
   begin
      return Pattern'Length = 0 or else Match_At (Name'First, Pattern'First);
   end Match;

   overriding procedure Finalize (Data : in out Entry_Data) is
   begin
      Free (Data.Simple);
      Free (Data.Full);
      Data.Valid := False;
   end Finalize;

   procedure Close (Search : in out Search_Data) is
   begin
      if Search.Handle /= 0 then
         Native_Close (Search.Handle);
         Search.Handle := 0;
      end if;
   end Close;

   overriding procedure Finalize (Data : in out Search_Data) is
   begin
      Close (Data);
      Free (Data.Directory);
      Free (Data.Pattern);
      Finalize (Data.Next);
   end Finalize;

   --  The next native entry's simple name ("" at the end)
   function Next_Name (Handle : Integer_64) return String is
      Buffer : String (1 .. 256);
      Length : constant Integer_64 :=
        Native_Next (Handle, Buffer'Address, Buffer'Length);
   begin
      if Length < 0 then
         return "";
      elsif Length <= Buffer'Length then
         return Buffer (1 .. Natural (Length));
      end if;
      declare
         Long : String (1 .. Natural (Length));
      begin
         if Native_Next (Handle, Long'Address, Length) /= Length then
            raise Use_Error;
         end if;
         return Long;
      end;
   end Next_Name;

   --  Search.Next := the next entry that matches (invalid if there is none)
   procedure Advance (Search : in out Search_Data) is
      E : Entry_Data renames Search.Next;
   begin
      Finalize (E);
      while Search.Handle /= 0 loop
         declare
            Simple : constant String := Next_Name (Search.Handle);
         begin
            if Simple'Length = 0 then
               Close (Search);
               exit;
            end if;
            if Match (Simple, Search.Pattern.all) then
               declare
                  Full : constant String :=
                    Compose (Search.Directory.all, Simple);
                  Code : constant Integer_64 :=
                    Native_Kind (Full'Address, Full'Length);
                  K    : constant File_Kind :=
                    (if Code < 0 then Special_File else Kind_Of (Code));
                  Size : constant Integer_64 :=
                    Native_Size (Full'Address, Full'Length);
               begin
                  if Search.Filter (K) then
                     E.Valid := True;
                     E.Simple := new String'(Simple);
                     E.Full := new String'(Full);
                     E.Kind := K;
                     E.Size := (if Size < 0 then 0 else File_Size (Size));
                     E.Time := Native_Time (Full'Address, Full'Length);
                     if E.Time = No_Such_File then
                        E.Time := Time_Unknown;
                     end if;
                     exit;
                  end if;
               end;
            end if;
         end;
      end loop;
   end Advance;

   procedure Start_Search (Search    : in out Search_Type;
                           Directory : in String;
                           Pattern   : in String;
                           Filter    : in Filter_Type := (others => True))
   is
      S      : Search_Data renames Search.Data;
      Handle : Integer_64;
   begin
      Finalize (S);
      Check_Name (Directory);
      if not Valid_Pattern (Pattern) then
         raise Name_Error;
      end if;
      declare
         Full : constant String := Full_Name (Directory);
      begin
         Handle := Native_Open (Full'Address, Full'Length);
         if Handle = -1 then
            raise Name_Error;
         elsif Handle <= 0 then
            raise Use_Error;
         end if;
         S.Handle := Handle;
         S.Directory := new String'(Full);
         S.Pattern := new String'(Pattern);
         S.Filter := Filter;
      end;
      Advance (S);
   end Start_Search;

   procedure End_Search (Search : in out Search_Type) is
   begin
      Finalize (Search.Data);
   end End_Search;

   function More_Entries (Search : in Search_Type) return Boolean is
   begin
      return Search.Data.Next.Valid;
   end More_Entries;

   procedure Get_Next_Entry (Search : in out Search_Type;
                             Directory_Entry : out Directory_Entry_Type)
   is
      S : Search_Data renames Search.Data;
      E : Entry_Data renames Directory_Entry.Data;
   begin
      if not S.Next.Valid then
         raise Status_Error;
      end if;
      Finalize (E);
      E.Valid := True;
      E.Simple := S.Next.Simple;
      E.Full := S.Next.Full;
      E.Kind := S.Next.Kind;
      E.Size := S.Next.Size;
      E.Time := S.Next.Time;
      S.Next.Simple := null;
      S.Next.Full := null;
      S.Next.Valid := False;
      Advance (S);
   end Get_Next_Entry;

   procedure Search (
      Directory : in String;
      Pattern   : in String;
      Filter    : in Filter_Type := (others => True);
      Process   : not null access procedure (
          Directory_Entry : in Directory_Entry_Type))
   is
      S : Search_Type;
      E : Directory_Entry_Type;
   begin
      Start_Search (S, Directory, Pattern, Filter);
      while More_Entries (S) loop
         Get_Next_Entry (S, E);
         Process (E);
      end loop;
   end Search;

   -------------------------------------
   -- Operations on directory entries --
   -------------------------------------

   procedure Check_Entry (Directory_Entry : Directory_Entry_Type) is
   begin
      if not Directory_Entry.Data.Valid then
         raise Status_Error;
      end if;
   end Check_Entry;

   function Simple_Name (Directory_Entry : in Directory_Entry_Type)
     return String is
   begin
      Check_Entry (Directory_Entry);
      return Directory_Entry.Data.Simple.all;
   end Simple_Name;

   function Full_Name (Directory_Entry : in Directory_Entry_Type)
     return String is
   begin
      Check_Entry (Directory_Entry);
      return Directory_Entry.Data.Full.all;
   end Full_Name;

   function Kind (Directory_Entry : in Directory_Entry_Type)
     return File_Kind is
   begin
      Check_Entry (Directory_Entry);
      return Directory_Entry.Data.Kind;
   end Kind;

   function Size (Directory_Entry : in Directory_Entry_Type)
     return File_Size is
   begin
      Check_Entry (Directory_Entry);
      return Directory_Entry.Data.Size;
   end Size;

   function Modification_Time (Directory_Entry : in Directory_Entry_Type)
     return Ada.Calendar.Time is
   begin
      Check_Entry (Directory_Entry);
      return Time_Of (Directory_Entry.Data.Time);
   end Modification_Time;

end Ada.Directories;
