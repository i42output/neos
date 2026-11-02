--  Exceptions: raising and handling, messages and occurrences, predefined checks, propagation through calls,
--  and finalization of controlled objects as a scope is left (normally, or by an exception)

with Ada.Text_IO; use Ada.Text_IO;
with Ada.Exceptions; use Ada.Exceptions;
with Ada.Finalization;

procedure Errors is

   Division_By_Zero : exception;

   function Divide (A, B : Float) return Float is
   begin
      if B = 0.0 then
         raise Division_By_Zero with "division of" & Float'Image (A) & " by zero";
      end if;
      return A / B;
   end Divide;

   package Resources is
      type Resource (Name : Character) is new Ada.Finalization.Limited_Controlled with null record;
      overriding procedure Finalize (R : in out Resource);
   end Resources;

   package body Resources is
      overriding procedure Finalize (R : in out Resource) is
      begin
         Put_Line ("closing " & R.Name);
      end Finalize;
   end Resources;
   use Resources;

   type Small is range 1 .. 10;
   N : Small := 10;
   Values : array (1 .. 3) of Integer := (others => 0);
   Index : Integer := 4;

begin
   Put_Line (Float'Image (Divide (10.0, 4.0)));
   begin
      Put_Line (Float'Image (Divide (1.0, 0.0)));
   exception
      when E : Division_By_Zero =>
         Put_Line (Exception_Name (E) & ": " & Exception_Message (E));
   end;

   --  (predefined checks)
   begin
      N := N + 1;
   exception
      when Constraint_Error =>
         Put_Line ("range check failed");
   end;
   begin
      Values (Index) := 1;
   exception
      when E : Constraint_Error =>
         Put_Line ("index check failed: " & Exception_Name (E));
   end;

   --  (a saved occurrence, reraised later)
   declare
      Saved : Exception_Occurrence;
   begin
      begin
         raise Program_Error with "saved";
      exception
         when E : others =>
            Save_Occurrence (Saved, E);
      end;
      Reraise_Occurrence (Saved);
   exception
      when E : Program_Error =>
         Put_Line ("reraised: " & Exception_Message (E));
   end;

   --  (controlled objects finalized as their scope is left, the last declared first)
   declare
      A : Resource ('a');
      B : Resource ('b');
   begin
      Put_Line ("using " & A.Name & " and " & B.Name);
   end;
   begin
      declare
         R : Resource ('r');
      begin
         raise Tasking_Error with "failed";
      end;
   exception
      when E : Tasking_Error =>
         Put_Line ("handled " & Exception_Message (E));
   end;
end Errors;
