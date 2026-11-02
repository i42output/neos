--  Ada.Wide_Wide_Text_IO.Complex_IO.adb
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

with Ada.Text_IO_Engine;
with Ada.Text_IO_Numbers;

package body Ada.Wide_Wide_Text_IO.Complex_IO is

   package Engine renames Ada.Text_IO_Engine;
   package Numbers renames Ada.Text_IO_Numbers;

   use type Engine.Int;

   package Real_IO is new Float_IO (Real'Base);

   function Is_Blank (C : Wide_Wide_Character) return Boolean is
     (C = ' ' or else Wide_Wide_Character'Pos (C) = 9);

   --  The image of Item: (real,imaginary).
   function Image (Item : Complex; Fore, Aft, Exp : Natural) return String is
     ("(" & Numbers.Float_Image
              (Long_Float (Re (Item)), Fore, Aft, Exp, 2 * Real'Digits)
      & "," & Numbers.Float_Image
              (Long_Float (Im (Item)), Fore, Aft, Exp, 2 * Real'Digits)
      & ")");

   procedure Get (File  : in  File_Type;
                  Item  : out Complex;
                  Width : in  Field := 0)
   is
      F         : constant Engine.File_Access := File.File;
      Paren     : Boolean := False;
      Real_Part : Real'Base;
      Imaginary : Real'Base;
      Line      : Engine.Int;
      Col       : Engine.Int;
      Page      : Engine.Int;
   begin
      Engine.Check_Input (F);
      if Width /= 0 then
         declare
            Buffer : Wide_Wide_String (1 .. Width);
            Last   : Natural := 0;
            Stop   : Positive;
            C      : Engine.Int;
         begin
            if Engine.Peek (F) = Engine.End_Of_Input then
               raise End_Error;
            end if;
            while Last < Width loop
               C := Engine.Peek (F);
               exit when C = Engine.End_Of_Input or else C = Engine.Line_Feed;
               Engine.Skip (F);
               Last := Last + 1;
               Buffer (Last) := Wide_Wide_Character'Val (C);
            end loop;
            Get (Buffer (1 .. Last), Item, Stop);
            for I in Stop + 1 .. Last loop
               if not Is_Blank (Buffer (I)) then
                  raise Data_Error;
               end if;
            end loop;
            return;
         end;
      end if;
      Engine.Skip_Blanks (F);
      if Engine.Peek (F) = Character'Pos ('(') then
         Engine.Skip (F);
         Paren := True;
      end if;
      Real_IO.Get (File, Real_Part);
      Line := F.Line;
      Col := F.Col;
      Page := F.Page;
      Engine.Skip_Blanks (F);
      if Engine.Peek (F) = Character'Pos (',') then
         Engine.Skip (F);
      elsif Line = F.Line and then Col = F.Col and then Page = F.Page then
         raise Data_Error;  --  (neither a comma nor a blank)
      end if;
      Real_IO.Get (File, Imaginary);
      if Paren then
         Engine.Skip_Blanks (F);
         if Engine.Peek (F) = Engine.End_Of_Input then
            raise End_Error;
         elsif Engine.Peek (F) /= Character'Pos (')') then
            raise Data_Error;
         end if;
         Engine.Skip (F);
      end if;
      Item := Compose_From_Cartesian (Real_Part, Imaginary);
   end Get;

   procedure Get (Item  : out Complex;
                  Width : in  Field := 0) is
   begin
      Get (Current_Input, Item, Width);
   end Get;

   procedure Put (File : in File_Type;
                  Item : in Complex;
                  Fore : in Field := Default_Fore;
                  Aft  : in Field := Default_Aft;
                  Exp  : in Field := Default_Exp)
   is
      Text : constant String := Image (Item, Fore, Aft, Exp);
   begin
      Numbers.Put_Image (File.File, Text, Text'Length);
   end Put;

   procedure Put (Item : in Complex;
                  Fore : in Field := Default_Fore;
                  Aft  : in Field := Default_Aft;
                  Exp  : in Field := Default_Exp) is
   begin
      Put (Current_Output, Item, Fore, Aft, Exp);
   end Put;

   procedure Get (From : in  Wide_Wide_String;
                  Item : out Complex;
                  Last : out Positive)
   is
      I         : Natural := From'First;
      Paren     : Boolean := False;
      Real_Part : Real'Base;
      Imaginary : Real'Base;
      Stop      : Positive;

      procedure Skip_Blanks is
      begin
         while I <= From'Last and then Is_Blank (From (I)) loop
            I := I + 1;
         end loop;
      end Skip_Blanks;
   begin
      Skip_Blanks;
      if I > From'Last then
         raise End_Error;
      end if;
      if From (I) = '(' then
         Paren := True;
         I := I + 1;
      end if;
      Real_IO.Get (From (I .. From'Last), Real_Part, Stop);
      I := Stop + 1;
      Skip_Blanks;
      if I > From'Last then
         raise End_Error;
      elsif From (I) = ',' then
         I := I + 1;
      elsif I = Stop + 1 then
         raise Data_Error;  --  (neither a comma nor a blank)
      end if;
      Real_IO.Get (From (I .. From'Last), Imaginary, Stop);
      if Paren then
         I := Stop + 1;
         Skip_Blanks;
         if I > From'Last then
            raise End_Error;
         elsif From (I) /= ')' then
            raise Data_Error;
         end if;
         Stop := I;
      end if;
      Item := Compose_From_Cartesian (Real_Part, Imaginary);
      Last := Stop;
   end Get;

   procedure Put (To   : out Wide_Wide_String;
                  Item : in  Complex;
                  Aft  : in  Field := Default_Aft;
                  Exp  : in  Field := Default_Exp)
   is
      Left  : constant String :=
         "(" & Numbers.Float_Image
                 (Long_Float (Re (Item)), 0, Aft, Exp, 2 * Real'Digits) & ",";
      Right : constant String :=
         Numbers.Float_Image
           (Long_Float (Im (Item)), 0, Aft, Exp, 2 * Real'Digits) & ")";
      Text  : String (1 .. To'Length);
   begin
      if Left'Length + Right'Length > To'Length then
         raise Layout_Error;
      end if;
      Text := Left & (1 .. To'Length - Left'Length - Right'Length => ' ')
              & Right;
      for I in Text'Range loop
         To (To'First + I - 1) :=
            Wide_Wide_Character'Val (Character'Pos (Text (I)));
      end loop;
   end Put;

end Ada.Wide_Wide_Text_IO.Complex_IO;
