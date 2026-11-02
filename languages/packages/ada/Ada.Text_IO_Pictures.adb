--  Ada.Text_IO_Pictures.adb
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

with Ada.IO_Exceptions;

package body Ada.Text_IO_Pictures is

   Layout_Error : exception renames IO_Exceptions.Layout_Error;

   Insertions : constant String := "_B0/";

   function Upper (C : Character) return Character is
     (if C in 'a' .. 'z' then Character'Val (Character'Pos (C) - 32) else C);

   function Is_In (C : Character; Class : String) return Boolean is
   begin
      for D of Class loop
         if C = D then
            return True;
         end if;
      end loop;
      return False;
   end Is_In;

   ---------------------------------------------------------------------------
   --  Recognition of the picture strings of F.3.1: each nonterminal maps the
   --  set of the positions where it can start to the set of the positions
   --  that follow its matches (the grammar has no recursion).

   type Position_Set is array (1 .. Max_Picture_Length + 1) of Boolean;

   Empty : constant Position_Set := (others => False);

   function Matches (P : String; Pic_Length : Natural) return Boolean is

      L : constant Natural := Pic_Length;

      --  The positions after a character of Class at a position of S.
      function Char (S : Position_Set; Class : String) return Position_Set
      is
         R : Position_Set := Empty;
      begin
         for I in 1 .. L loop
            if S (I) and then Is_In (P (I), Class) then
               R (I + 1) := True;
            end if;
         end loop;
         return R;
      end Char;

      function Pair (S : Position_Set; Text : String) return Position_Set is
         R : Position_Set := Empty;
      begin
         for I in 1 .. L - 1 loop
            if S (I) and then P (I .. I + 1) = Text then
               R (I + 2) := True;
            end if;
         end loop;
         return R;
      end Pair;

      --  { Class }
      function Rep (S : Position_Set; Class : String) return Position_Set is
         R : Position_Set := S;
      begin
         for I in 1 .. L loop
            if R (I) and then Is_In (P (I), Class) then
               R (I + 1) := True;
            end if;
         end loop;
         return R;
      end Rep;

      function "or" (A, B : Position_Set) return Position_Set is
         R : Position_Set;
      begin
         for I in R'Range loop
            R (I) := A (I) or else B (I);
         end loop;
         return R;
      end "or";

      --  The forms of the picture strings with LHS_Sign K (' ': none).
      function Picture (K : Character) return Position_Set is

         Start : Position_Set := Empty;
         Sign  : constant String := (if K = ' ' then "" else (1 => K));

         function Radix (S : Position_Set) return Position_Set is
           (Char (S, ".V"));

         function Number (S : Position_Set) return Position_Set is
            Fore  : constant Position_Set :=
               Rep (Char (S, "9"), "9" & Insertions);
            R1    : constant Position_Set := Radix (Fore);
            R2    : constant Position_Set := Radix (S);
         begin
            return Fore or Rep (R1, Insertions)
              or Rep (Char (Rep (R1, "9" & Insertions), "9"), Insertions)
              or Rep (Char (Rep (R2, "9" & Insertions), "9"), Insertions);
         end Number;

         function Zero_Suppression (S : Position_Set) return Position_Set is
           (Rep (Char (S, "Z"), "Z" & Insertions)
            or Rep (Char (S, "*"), "*" & Insertions));

         function Opt_Zero (S : Position_Set) return Position_Set is
           (S or Zero_Suppression (S));

         --  C {insertion} C {C | insertion}
         function Floating (S : Position_Set; C : String) return Position_Set
         is
           (Rep (Char (Rep (Char (S, C), Insertions), C), C & Insertions));

         function Floating_Sign (S : Position_Set) return Position_Set is
           (if K = ' ' then Empty else Floating (S, Sign));

         --  The all_X_number of the characters C
         function All_Number (S : Position_Set; C : String)
            return Position_Set
         is
            Fore : constant Position_Set := Floating (S, C);
            R    : constant Position_Set := Radix (Fore);
         begin
            return Fore or R or Char (Rep (R, C & Insertions), C);
         end All_Number;

         function All_Zero (S : Position_Set) return Position_Set is
            Z : constant Position_Set := Rep (Char (S, "Z"), "Z" & Insertions);
            A : constant Position_Set := Rep (Char (S, "*"), "*" & Insertions);
            RZ : constant Position_Set := Radix (Z);
            RA : constant Position_Set := Radix (A);
         begin
            return Z or RZ or Char (Rep (RZ, "Z" & Insertions), "Z")
              or A or RA or Char (Rep (RA, "*" & Insertions), "*");
         end All_Zero;

         --  (without the optional '>', which is the RHS sign below)
         function All_Sign (S : Position_Set) return Position_Set is
           (if K = ' ' then Empty else All_Number (S, Sign));

         function All_Currency (S : Position_Set) return Position_Set is
           (All_Number (S, "$") or All_Number (S, "#"));

         function Opt_RHS (S : Position_Set) return Position_Set is
         begin
            case K is
               when ' ' =>
                  --  ('>' only with '<', as GNAT requires)
                  return S or Char (S, "+-") or Pair (S, "CR")
                    or Pair (S, "DB");
               when '<' =>
                  return Char (S, ">");
               when others =>
                  return S;
            end case;
         end Opt_RHS;

         --  [fixed_LHS_sign], [fixed_LHS_sign {direct_insertion}]
         function Opt_LHS (S : Position_Set) return Position_Set is
           (if K = ' ' then S else S or Char (S, Sign));

         function Opt_LHS_Insertions (S : Position_Set) return Position_Set
         is
           (if K = ' ' then S else S or Rep (Char (S, Sign), Insertions));

         function Ins (S : Position_Set) return Position_Set is
           (Rep (S, Insertions));

         function Multiple_Hash (S : Position_Set) return Position_Set is
           (Rep (Char (Char (S, "#"), "#"), "#"));

         function Fixed_Hash (S : Position_Set) return Position_Set is
           (Char (S, "#") or Multiple_Hash (S));

         R : Position_Set := Empty;
      begin
         Start (1) := True;
         --  fixed_$_picture_string
         R := R
           or Opt_RHS (Number (Opt_Zero (Ins (Char (Opt_LHS (Start), "$")))))
           or Opt_RHS
                (Ins (Char (Number (Opt_Zero (Opt_LHS_Insertions (Start))),
                            "$")))
           or Opt_RHS (Ins (Char (Number (Floating_Sign (Start)), "$")))
           or Opt_RHS (Ins (All_Zero (Ins (Char (Opt_LHS (Start), "$")))))
           or Opt_RHS
                (Ins (Char (Ins (All_Zero (Opt_LHS_Insertions (Start))),
                            "$")))
           or Opt_RHS (Ins (Char (Ins (All_Sign (Start)), "$")))
           or (if K = '<' then Ins (Char (Ins (Char (All_Sign (Start), ">")),
                                          "$"))
               else Empty);
         --  fixed_#_picture_string
         R := R
           or Opt_RHS (Number (Opt_Zero (Ins (Char (Opt_LHS (Start), "#")))))
           or Opt_RHS
                (Number (Zero_Suppression
                           (Ins (Multiple_Hash (Opt_LHS (Start))))))
           or Opt_RHS
                (Ins (Fixed_Hash
                        (Number (Opt_Zero (Opt_LHS_Insertions (Start))))))
           or Opt_RHS (Ins (Fixed_Hash (Number (Floating_Sign (Start)))))
           or Opt_RHS (Ins (All_Zero (Ins (Char (Opt_LHS (Start), "#")))))
           or Opt_RHS (Ins (All_Zero (Ins (Multiple_Hash (Opt_LHS (Start))))))
           or Opt_RHS
                (Ins (Fixed_Hash
                        (Ins (All_Zero (Opt_LHS_Insertions (Start))))))
           or Opt_RHS (Ins (Fixed_Hash (Ins (All_Sign (Start)))))
           or (if K = '<' then Ins (Fixed_Hash (Ins (Char (All_Sign (Start),
                                                           ">"))))
               else Empty);
         --  floating_currency_picture_string
         R := R
           or Opt_RHS (Number (Floating (Ins (Opt_LHS (Start)), "$")))
           or Opt_RHS (Number (Floating (Ins (Opt_LHS (Start)), "#")))
           or Opt_RHS (Ins (All_Currency (Ins (Opt_LHS (Start)))));
         --  non_currency_picture_string
         R := R
           or Opt_RHS (Number (Zero_Suppression (Opt_LHS_Insertions (Start))))
           or Opt_RHS (Number (Start or Floating_Sign (Start)))
           or Opt_RHS (Ins (All_Zero (Opt_LHS_Insertions (Start))))
           or Ins (if K = '<' then Char (All_Sign (Start), ">")
                   else All_Sign (Start));
         if K /= ' ' then
            --  (the RM's "fixed_LHS_sign direct_insertion {direct_insertion}
            --  number [RHS_sign]", also without insertions, as intended)
            R := R or Opt_RHS (Number (Ins (Char (Start, Sign))));
         end if;
         return R;
      end Picture;
   begin
      if L = 0 then
         return False;
      end if;
      --  (an LHS sign is always the first character)
      if P (1) in '+' | '-' | '<' then
         return Picture (P (1)) (L + 1);
      end if;
      return Picture (' ') (L + 1);
   end Matches;

   procedure Expand
     (Pic             : String;
      Blank_When_Zero : Boolean;
      Expanded        : out Picture_String;
      Length          : out Natural;
      Valid           : out Boolean)
   is
      Last       : Natural := 0;
      I          : Positive := Pic'First;
      Replicated : Boolean := False;  --  (the last character)
      Count      : Natural;
   begin
      Expanded := (others => ' ');
      Length := 0;
      Valid := False;
      while I <= Pic'Last loop
         if Pic (I) = '(' then
            --  char ( spaces count_string )
            if Last = 0 or else Replicated
              or else not Is_In (Expanded (Last), "9Z*$#+-<" & Insertions)
            then
               return;
            end if;
            I := I + 1;
            while I <= Pic'Last and then Pic (I) = ' ' loop
               I := I + 1;
            end loop;
            Count := 0;
            if I > Pic'Last or else Pic (I) not in '0' .. '9' then
               return;
            end if;
            while I <= Pic'Last and then Pic (I) in '0' .. '9' loop
               if Count > Max_Picture_Length then
                  return;
               end if;
               Count := Count * 10
                        + (Character'Pos (Pic (I)) - Character'Pos ('0'));
               I := I + 1;
            end loop;
            if I > Pic'Last or else Pic (I) /= ')' or else Count = 0
              or else Last + Count - 1 > Max_Picture_Length
            then
               return;
            end if;
            Expanded (Last + 1 .. Last + Count - 1) :=
               (others => Expanded (Last));
            Last := Last + Count - 1;
            Replicated := True;
         else
            if Last = Max_Picture_Length then
               return;
            end if;
            Last := Last + 1;
            Expanded (Last) := Upper (Pic (I));
            Replicated := False;
         end if;
         I := I + 1;
      end loop;
      if Blank_When_Zero then
         for C of Expanded (1 .. Last) loop
            if C = '*' then
               return;
            end if;
         end loop;
      end if;
      Length := Last;
      Valid := Matches (Expanded, Last);
   end Expand;

   function Output_Length
     (Pic : String; Currency_Length : Natural) return Natural
   is
      Result : Natural := Pic'Length;
      Dollar : Boolean := False;
   begin
      for C of Pic loop
         if C = '$' then
            Dollar := True;
         elsif C = 'V' then
            Result := Result - 1;
         end if;
      end loop;
      if Dollar then
         Result := Result + Currency_Length - 1;
      end if;
      return Result;
   end Output_Length;

   ---------------------------------------------------------------------------
   --  Edited output

   type Role is
     (Insertion,     --  a direct insertion character
      Fore_Digit,    --  '9' before the radix
      Aft_Digit,     --  a digit position after the radix
      Radix,
      Fixed_Sign,    --  a fixed LHS sign
      Right_Sign,    --  an RHS sign (the first of "CR" and "DB")
      Right_Sign_2,  --  the second character of "CR" and "DB"
      Fixed_Dollar,
      Fixed_Hash,    --  a character of a fixed # currency
      Floating,      --  a character of a floating sign or currency or zero
                     --  suppression (or all_X_number) before the radix
      Floating_Insertion);  --  an insertion character among them

   type Float_Kind is (None, Sign, Dollar, Hash, Suppression);

   function Image
     (Pic             : String;
      Blank_When_Zero : Boolean;
      Negative        : Boolean;
      Magnitude       : String;
      Scale           : Integer;
      Currency        : Wide_Wide_String;
      Fill            : Wide_Wide_Character;
      Separator       : Wide_Wide_Character;
      Radix_Mark      : Wide_Wide_Character) return Wide_Wide_String
   is
      P     : constant String (1 .. Pic'Length) := Pic;
      L     : constant Natural := P'Length;
      Roles : array (1 .. L) of Role := (others => Insertion);
      Kind  : Float_Kind := None;
      Float_Char  : Character := ' ';   --  (the floating symbol)
      Float_First : Natural := 0;       --  (the floating characters)
      Float_Last  : Natural := 0;
      All_Number  : Boolean := False;   --  (the floating characters extend
                                        --  over the radix)
      All_Last       : Natural := 0;    --  (the end of the all_X_number)
      Radix_Position : Natural := 0;
      Has_Sign       : Boolean := False;
      I : Positive := 1;

      function Is_Insertion (C : Character) return Boolean is
        (Is_In (C, Insertions));

      --  The floating characters C from I (with insertions between).
      procedure Scan_Floating (C : Character) is
         J : Positive := I;
      begin
         Float_First := I;
         Float_Char := C;
         while J <= L and then (P (J) = C or else Is_Insertion (P (J))) loop
            if P (J) = C then
               Float_Last := J;
            end if;
            J := J + 1;
         end loop;
         --  an all_X_number: the characters extend over the radix
         if Float_Last + 1 <= L and then P (Float_Last + 1) in '.' | 'V'
         then
            J := Float_Last + 2;
            declare
               Last_C : Natural := 0;
            begin
               while J <= L and then (P (J) = C or else Is_Insertion (P (J)))
               loop
                  if P (J) = C then
                     Last_C := J;
                  end if;
                  J := J + 1;
               end loop;
               if Last_C /= 0 or else J > L or else P (J) not in '9' then
                  All_Number := True;
                  Radix_Position := Float_Last + 1;
                  Roles (Radix_Position) := Radix;
                  for K in Radix_Position + 1 .. Last_C loop
                     Roles (K) := (if P (K) = C then Aft_Digit else Insertion);
                  end loop;
                  for K in Float_First .. Float_Last loop
                     Roles (K) := (if P (K) = C then Floating
                                   else Floating_Insertion);
                  end loop;
                  All_Last := Natural'Max (Last_C, Radix_Position);
                  I := All_Last + 1;
                  return;
               end if;
            end;
         end if;
         for K in Float_First .. Float_Last loop
            Roles (K) := (if P (K) = C then Floating else Floating_Insertion);
         end loop;
         I := Float_Last + 1;
         --  an all_X_number without a radix: no number follows
         J := I;
         while J <= L and then Is_Insertion (P (J)) loop
            J := J + 1;
         end loop;
         if J > L or else P (J) not in '9' | '.' | 'V' then
            All_Number := True;
            All_Last := Float_Last;
         else
            --  (the insertion characters after the last floating character
            --  are part of the floating string (F.3.1): blank before the
            --  first significant digit)
            for K in I .. J - 1 loop
               Roles (K) := Floating_Insertion;
            end loop;
            I := J;
         end if;
      end Scan_Floating;

      function Count (C : Character; From : Positive) return Natural is
         N : Natural := 0;
         J : Positive := From;
      begin
         while J <= L and then (P (J) = C or else Is_Insertion (P (J))) loop
            if P (J) = C then
               N := N + 1;
            end if;
            J := J + 1;
         end loop;
         return N;
      end Count;

      procedure Skip_Insertions is
      begin
         while I <= L and then Is_Insertion (P (I)) loop
            I := I + 1;
         end loop;
      end Skip_Insertions;
   begin
      --  The roles of the picture's characters
      if P (1) in '+' | '-' | '<' then
         Has_Sign := True;
         if Count (P (1), 1) >= 2 then
            Kind := Sign;
            Scan_Floating (P (1));
         else
            Roles (1) := Fixed_Sign;
            I := 2;
         end if;
      end if;
      if Kind = None then
         Skip_Insertions;
         if I <= L and then P (I) = '$' then
            if Count ('$', I) >= 2 then
               Kind := Dollar;
               Scan_Floating ('$');
            else
               Roles (I) := Fixed_Dollar;
               I := I + 1;
            end if;
         elsif I <= L and then P (I) = '#' then
            declare
               J : Positive := I;
            begin
               while J <= L and then P (J) = '#' loop
                  J := J + 1;
               end loop;
               declare
                  K : Positive := J;
               begin
                  while K <= L and then Is_Insertion (P (K)) loop
                     K := K + 1;
                  end loop;
                  if J - I = 1 or else (K <= L and then P (K) in 'Z' | '*')
                  then
                     Roles (I .. J - 1) := (others => Fixed_Hash);
                     I := J;
                  else
                     Kind := Hash;
                     Scan_Floating ('#');
                  end if;
               end;
            end;
         end if;
         if Kind = None then
            Skip_Insertions;
            if I <= L and then P (I) in 'Z' | '*' then
               Kind := Suppression;
               Scan_Floating (P (I));
            end if;
         end if;
      end if;
      --  The number
      while I <= L and then (P (I) = '9' or else Is_Insertion (P (I))) loop
         if P (I) = '9' then
            Roles (I) := (if Radix_Position = 0 then Fore_Digit
                          else Aft_Digit);
         end if;
         I := I + 1;
      end loop;
      if I <= L and then P (I) in '.' | 'V' and then Radix_Position = 0 then
         Radix_Position := I;
         Roles (I) := Radix;
         I := I + 1;
         while I <= L and then (P (I) = '9' or else Is_Insertion (P (I)))
         loop
            if P (I) = '9' then
               Roles (I) := Aft_Digit;
            end if;
            I := I + 1;
         end loop;
      end if;
      --  The trailing currency and sign
      while I <= L loop
         case P (I) is
            when '$' =>
               Roles (I) := Fixed_Dollar;
            when '#' =>
               Roles (I) := Fixed_Hash;
            when '+' | '-' | '>' =>
               Roles (I) := Right_Sign;
               Has_Sign := True;
            when 'C' | 'D' =>
               Roles (I) := Right_Sign;
               Roles (I + 1) := Right_Sign_2;
               Has_Sign := True;
               I := I + 1;
            when others =>
               null;  --  (an insertion)
         end case;
         I := I + 1;
      end loop;

      declare
         N : Natural := 0;
      begin
         for R of Roles loop
            if R = Aft_Digit then
               N := N + 1;
            end if;
         end loop;
         declare
            Q : constant Natural := Natural'Min (Natural'Max (Scale, 0), N);

            --  Magnitude * 10 ** (Q - Scale) rounded (halves away from 0)
            function Rounded return String is
               M    : constant String := Magnitude;
               Drop : constant Integer := Scale - Q;
            begin
               if Drop <= 0 then
                  return M & (1 .. -Drop => '0');
               elsif Drop > M'Length then
                  return "";
               end if;
               declare
                  Keep : String := '0' & M (M'First .. M'Last - Drop);
               begin
                  if M (M'Last - Drop + 1) >= '5' then
                     for J in reverse Keep'Range loop
                        if Keep (J) = '9' then
                           Keep (J) := '0';
                        else
                           Keep (J) := Character'Succ (Keep (J));
                           exit;
                        end if;
                     end loop;
                  end if;
                  return Keep;
               end;
            end Rounded;

            function Trim (S : String) return String is
            begin
               for J in S'Range loop
                  if S (J) /= '0' then
                     return S (J .. S'Last);
                  end if;
               end loop;
               return "";
            end Trim;

            R_Digits : constant String := Trim (Rounded);
            Zero     : constant Boolean := R_Digits'Length = 0;
            Digits_All : constant String :=
               (1 .. Natural'Max (Q + 1 - R_Digits'Length, 0) => '0')
               & R_Digits;
            --  (at least Q + 1 digits)
            Int_Part  : constant String :=
               Trim (Digits_All (Digits_All'First .. Digits_All'Last - Q));
            Frac_Part : constant String :=
               Digits_All (Digits_All'Last - Q + 1 .. Digits_All'Last);
            Minus     : constant Boolean := Negative;

            Output : array (1 .. L) of Wide_Wide_Character :=
               (others => ' ');
            --  (the output of each position, except the currency strings)
            Is_Currency : array (1 .. L) of Boolean := (others => False);
            Is_Empty    : array (1 .. L) of Boolean := (others => False);
            Next_Digit  : Natural := Int_Part'Last;  --  (right to left)
            Next_Frac   : Positive := Frac_Part'First;

            function Digit (C : Character) return Wide_Wide_Character is
              (Wide_Wide_Character'Val (Character'Pos (C)));

            function Insertion_Of (C : Character) return Wide_Wide_Character
            is
            begin
               case C is
                  when '_' => return Separator;
                  when 'B' => return ' ';
                  when others => return Digit (C);
               end case;
            end Insertion_Of;

            function Sign_Of (C : Character) return Wide_Wide_Character is
            begin
               case C is
                  when '+' => return (if Minus then '-' else '+');
                  when '-' => return (if Minus then '-' else ' ');
                  when '<' => return (if Minus then '(' else ' ');
                  when '>' => return (if Minus then ')' else ' ');
                  when others => return ' ';
               end case;
            end Sign_Of;

            Fixed_Hash_Count : Natural := 0;
         begin
            --  (a currency string too long is a layout error even when the
            --  output would be blank)
            if Kind = Hash
              and then Float_Last - Float_First + 1 < Currency'Length
            then
               raise Layout_Error;
            end if;
            declare
               Hashes : Natural := 0;
            begin
               for R of Roles loop
                  if R = Fixed_Hash then
                     Hashes := Hashes + 1;
                  end if;
               end loop;
               if Hashes > 0 and then Currency'Length > Hashes then
                  raise Layout_Error;
               end if;
            end;
            if Blank_When_Zero and then Zero then
               return (1 .. Output_Length (P, Currency'Length) => ' ');
            end if;
            if Minus and then not Has_Sign then
               raise Layout_Error;
            end if;
            --  The number's digits
            for J in reverse 1 .. L loop
               if Roles (J) = Fore_Digit then
                  if Next_Digit >= Int_Part'First then
                     Output (J) := Digit (Int_Part (Next_Digit));
                     Next_Digit := Next_Digit - 1;
                  else
                     Output (J) := '0';
                  end if;
               end if;
            end loop;
            for J in 1 .. L loop
               case Roles (J) is
                  when Aft_Digit =>
                     if Next_Frac <= Frac_Part'Last then
                        Output (J) := Digit (Frac_Part (Next_Frac));
                        Next_Frac := Next_Frac + 1;
                     else
                        Output (J) := '0';
                     end if;
                  when Radix =>
                     if P (J) = 'V' then
                        Is_Empty (J) := True;
                     else
                        Output (J) := Radix_Mark;
                     end if;
                  when Insertion =>
                     Output (J) := Insertion_Of (P (J));
                  when Fixed_Sign | Right_Sign =>
                     if P (J) = 'C' then
                        Output (J) := (if Minus then 'C' else ' ');
                     elsif P (J) = 'D' then
                        Output (J) := (if Minus then 'D' else ' ');
                     else
                        Output (J) := Sign_Of (P (J));
                     end if;
                  when Right_Sign_2 =>
                     Output (J) :=
                        (if not Minus then ' '
                         else Wide_Wide_Character'Val (Character'Pos (P (J))));
                  when Fixed_Dollar =>
                     Is_Currency (J) := True;
                  when Fixed_Hash =>
                     Fixed_Hash_Count := Fixed_Hash_Count + 1;
                  when Fore_Digit | Floating | Floating_Insertion =>
                     null;
               end case;
            end loop;
            --  The fixed # currency: the currency string filled with spaces
            --  (on the left, or on the right after the radix).
            if Fixed_Hash_Count > 0 then
               if Currency'Length > Fixed_Hash_Count then
                  raise Layout_Error;
               end if;
               declare
                  First : Natural := 0;
                  After : Boolean;
                  Text  : Wide_Wide_String (1 .. Fixed_Hash_Count);
                  K     : Natural := 0;
               begin
                  for J in 1 .. L loop
                     if Roles (J) = Fixed_Hash and then First = 0 then
                        First := J;
                     end if;
                  end loop;
                  After := Radix_Position /= 0 and then First > Radix_Position;
                  if After then
                     Text := Currency
                             & (1 .. Fixed_Hash_Count - Currency'Length
                                => ' ');
                  else
                     Text := (1 .. Fixed_Hash_Count - Currency'Length => ' ')
                             & Currency;
                  end if;
                  for J in 1 .. L loop
                     if Roles (J) = Fixed_Hash then
                        K := K + 1;
                        Output (J) := Text (K);
                     end if;
                  end loop;
               end;
            end if;
            --  The floating characters
            if Kind /= None then
               declare
                  Excess   : constant Natural :=
                     (if Next_Digit >= Int_Part'First
                      then Next_Digit - Int_Part'First + 1 else 0);
                  Symbols  : Natural := 0;
                  Leftmost : Natural := 0;  --  (the leftmost replaced)
                  Fill_Char : constant Wide_Wide_Character :=
                     (if Kind = Suppression and then Float_Char = '*' then Fill
                      else ' ');
               begin
                  for J in Float_First .. Float_Last loop
                     if Roles (J) = Floating then
                        Symbols := Symbols + 1;
                     end if;
                  end loop;
                  if All_Number and then Zero and then Float_Char /= '*'
                    and then Radix_Position /= All_Last
                  then
                     --  (the edited output is all spaces)
                     return (1 .. Output_Length (P, Currency'Length) => ' ');
                  elsif All_Number and then Zero and then Float_Char = '*'
                  then
                     --  The whole all_X_number is spaces (or fill characters)
                     for J in Float_First .. All_Last loop
                        if Float_Char = '*' and then Roles (J) = Radix then
                           null;  --  (the radix mark)
                        else
                           Output (J) := (if Float_Char = '*' then Fill
                                          else ' ');
                           Is_Empty (J) := False;
                        end if;
                     end loop;
                  else
                     if (Kind = Suppression and then Excess > Symbols)
                       or else
                         (Kind /= Suppression and then Excess > Symbols - 1)
                     then
                        raise Layout_Error;
                     end if;
                     declare
                        Remaining : Natural := Excess;
                     begin
                        for J in reverse Float_First .. Float_Last loop
                           exit when Remaining = 0;
                           if Roles (J) = Floating then
                              Output (J) := Digit (Int_Part (Next_Digit));
                              Next_Digit := Next_Digit - 1;
                              Remaining := Remaining - 1;
                              Leftmost := J;
                           end if;
                        end loop;
                     end;
                     --  The other characters: insertions to the right of the
                     --  leftmost replaced, the sign or currency on its left,
                     --  spaces (or fill characters)
                     for J in Float_First .. Float_Last loop
                        if Roles (J) = Floating_Insertion then
                           Output (J) :=
                              (if Leftmost /= 0 and then J > Leftmost
                               then Insertion_Of (P (J)) else Fill_Char);
                        elsif Roles (J) = Floating
                          and then (Leftmost = 0 or else J < Leftmost)
                        then
                           Output (J) := Fill_Char;
                        end if;
                     end loop;
                     --  (the insertions after the last floating character:
                     --  replaced if a digit was placed among them, else
                     --  fill characters)
                     for J in Float_Last + 1 .. L loop
                        exit when Roles (J) /= Floating_Insertion;
                        Output (J) :=
                           (if Leftmost /= 0 then Insertion_Of (P (J))
                            else Fill_Char);
                     end loop;
                     if Kind /= Suppression then
                        declare
                           --  The position of the sign or currency: before
                           --  the leftmost replaced, else the last symbol.
                           At_Position : constant Positive :=
                              (if Leftmost /= 0 then Leftmost - 1
                               else Float_Last);
                        begin
                           case Kind is
                              when Sign =>
                                 Output (At_Position) := Sign_Of (Float_Char);
                              when Dollar =>
                                 Is_Currency (At_Position) := True;
                              when Hash =>
                                 if At_Position - Float_First + 1
                                      < Currency'Length
                                 then
                                    raise Layout_Error;
                                 end if;
                                 for K in 1 .. Currency'Length loop
                                    Output (At_Position - Currency'Length + K)
                                       := Currency (Currency'First + K - 1);
                                 end loop;
                              when None | Suppression =>
                                 null;
                           end case;
                        end;
                     end if;
                  end if;
               end;
            elsif Next_Digit >= Int_Part'First then
               raise Layout_Error;  --  (leading digits would be lost)
            end if;
            --  The result
            declare
               Result : Wide_Wide_String
                          (1 .. Output_Length (P, Currency'Length));
               Last   : Natural := 0;
            begin
               for J in 1 .. L loop
                  if Is_Currency (J) then
                     Result (Last + 1 .. Last + Currency'Length) := Currency;
                     Last := Last + Currency'Length;
                  elsif Is_Empty (J) then
                     null;
                  elsif P (J) = '$' then
                     --  (another character of a floating currency)
                     Last := Last + 1;
                     Result (Last) := Output (J);
                  else
                     Last := Last + 1;
                     Result (Last) := Output (J);
                  end if;
               end loop;
               if Last /= Result'Length then
                  --  (a floating $ currency: Currency_Length_Adjustment
                  --  counts it once)
                  return Result (1 .. Last);
               end if;
               return Result;
            end;
         end;
      end;
   end Image;

end Ada.Text_IO_Pictures;
