--  Ada.Exceptions.adb
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

package body Ada.Exceptions is

   function Name_Of (Id : Exception_Id) return String;
   pragma Import (Neos, Name_Of, "ada::exception_name");
   procedure Raise_It (Id : Exception_Id; Message : String);
   pragma Import (Neos, Raise_It, "ada::raise_exception");
   function Message_Of (X : Exception_Occurrence) return String;
   pragma Import (Neos, Message_Of, "ada::occurrence_message");
   function Information_Of (X : Exception_Occurrence) return String;
   pragma Import (Neos, Information_Of, "ada::occurrence_information");
   function Identity_Of (X : Exception_Occurrence) return Exception_Id;
   pragma Import (Neos, Identity_Of, "ada::occurrence_id");
   procedure Reraise_It (X : Exception_Occurrence);
   pragma Import (Neos, Reraise_It, "ada::reraise_occurrence");
   function Copy_Of (X : Exception_Occurrence) return Exception_Occurrence;
   pragma Import (Neos, Copy_Of, "ada::copy_occurrence");

   function To_Wide (S : String) return Wide_String is
      Result : Wide_String (S'Range);
   begin
      for I in S'Range loop
         Result (I) := Wide_Character'Val (Character'Pos (S (I)));
      end loop;
      return Result;
   end To_Wide;

   function To_Wide_Wide (S : String) return Wide_Wide_String is
      Result : Wide_Wide_String (S'Range);
   begin
      for I in S'Range loop
         Result (I) := Wide_Wide_Character'Val (Character'Pos (S (I)));
      end loop;
      return Result;
   end To_Wide_Wide;

   function Exception_Name (Id : Exception_Id) return String is
   begin
      if Id = Null_Id then
         raise Constraint_Error;
      end if;
      return Name_Of (Id);
   end Exception_Name;

   function Wide_Exception_Name (Id : Exception_Id) return Wide_String is
   begin
      return To_Wide (Exception_Name (Id));
   end Wide_Exception_Name;

   function Wide_Wide_Exception_Name (Id : Exception_Id) return Wide_Wide_String is
   begin
      return To_Wide_Wide (Exception_Name (Id));
   end Wide_Wide_Exception_Name;

   procedure Raise_Exception (E : in Exception_Id; Message : in String := "") is
   begin
      Raise_It (E, Message);
   end Raise_Exception;

   function Exception_Message (X : Exception_Occurrence) return String is
   begin
      if X = Null_Occurrence then
         raise Constraint_Error;
      end if;
      return Message_Of (X);
   end Exception_Message;

   procedure Reraise_Occurrence (X : in Exception_Occurrence) is
   begin
      if X /= Null_Occurrence then
         Reraise_It (X);
      end if;
   end Reraise_Occurrence;

   function Exception_Identity (X : Exception_Occurrence) return Exception_Id is
   begin
      if X = Null_Occurrence then
         return Null_Id;
      end if;
      return Identity_Of (X);
   end Exception_Identity;

   function Exception_Name (X : Exception_Occurrence) return String is
   begin
      return Exception_Name (Exception_Identity (X));
   end Exception_Name;

   function Wide_Exception_Name (X : Exception_Occurrence) return Wide_String is
   begin
      return To_Wide (Exception_Name (X));
   end Wide_Exception_Name;

   function Wide_Wide_Exception_Name (X : Exception_Occurrence) return Wide_Wide_String is
   begin
      return To_Wide_Wide (Exception_Name (X));
   end Wide_Wide_Exception_Name;

   function Exception_Information (X : Exception_Occurrence) return String is
   begin
      if X = Null_Occurrence then
         raise Constraint_Error;
      end if;
      return Information_Of (X);
   end Exception_Information;

   procedure Save_Occurrence (Target : out Exception_Occurrence;
                              Source : in Exception_Occurrence) is
   begin
      if Source = Null_Occurrence then
         Target := Null_Occurrence;
      else
         Target := Copy_Of (Source);
      end if;
   end Save_Occurrence;

   function Save_Occurrence (Source : Exception_Occurrence)
                             return Exception_Occurrence_Access is
      Result : constant Exception_Occurrence_Access := new Exception_Occurrence;
   begin
      Save_Occurrence (Result.all, Source);
      return Result;
   end Save_Occurrence;

end Ada.Exceptions;
