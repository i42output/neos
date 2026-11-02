--  Ada.Containers.Generic_Sort.adb
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

with Interfaces;

--  An introspective sort: quicksort with median-of-three pivots, switching
--  to heapsort when the recursion gets too deep and to insertion sort for
--  short ranges.  Elements are only ever moved by Swap.
procedure Ada.Containers.Generic_Sort
      (First, Last : Index_Type'Base)
is
   use type Interfaces.Integer_64;
   subtype Int is Interfaces.Integer_64;

   function Ix (P : Int) return Index_Type is (Index_Type'Val (P));

   function Less (I, J : Int) return Boolean is (Before (Ix (I), Ix (J)));

   procedure Exchange (I, J : Int) is
   begin
      if I /= J then
         Swap (Ix (I), Ix (J));
      end if;
   end Exchange;

   procedure Insertion_Sort (Lo, Hi : Int) is
      J : Int;
   begin
      for I in Lo + 1 .. Hi loop
         J := I;
         while J > Lo and then Less (J, J - 1) loop
            Exchange (J, J - 1);
            J := J - 1;
         end loop;
      end loop;
   end Insertion_Sort;

   procedure Heap_Sort (Lo, Hi : Int) is
      --  Heap positions 0 .. N - 1 map to Lo .. Hi.
      procedure Sift_Down (Root, N : Int) is
         R     : Int := Root;
         Child : Int;
      begin
         loop
            Child := 2 * R + 1;
            exit when Child >= N;
            if Child + 1 < N and then Less (Lo + Child, Lo + Child + 1) then
               Child := Child + 1;
            end if;
            exit when not Less (Lo + R, Lo + Child);
            Exchange (Lo + R, Lo + Child);
            R := Child;
         end loop;
      end Sift_Down;

      N : constant Int := Hi - Lo + 1;
   begin
      for Root in reverse 0 .. N / 2 - 1 loop
         Sift_Down (Root, N);
      end loop;
      for Size in reverse 1 .. N - 1 loop
         Exchange (Lo, Lo + Size);
         Sift_Down (0, Size);
      end loop;
   end Heap_Sort;

   procedure Quick_Sort (Low, High : Int; Depth_Limit : Natural) is
      Lo    : Int := Low;
      Hi    : Int := High;
      Depth : Natural := Depth_Limit;
      Mid, I, J : Int;
   begin
      while Hi - Lo > 12 loop
         if Depth = 0 then
            Heap_Sort (Lo, Hi);
            return;
         end if;
         Depth := Depth - 1;

         --  Order Lo, Mid and Hi, then use the median as the pivot, kept
         --  at Lo while partitioning.
         Mid := Lo + (Hi - Lo) / 2;
         if Less (Mid, Lo) then
            Exchange (Mid, Lo);
         end if;
         if Less (Hi, Mid) then
            Exchange (Hi, Mid);
            if Less (Mid, Lo) then
               Exchange (Mid, Lo);
            end if;
         end if;
         Exchange (Lo, Mid);

         I := Lo;
         J := Hi + 1;
         loop
            loop
               I := I + 1;
               exit when I = Hi or else not Less (I, Lo);
            end loop;
            loop
               J := J - 1;
               exit when not Less (Lo, J);
            end loop;
            exit when I >= J;
            Exchange (I, J);
         end loop;
         Exchange (Lo, J);

         --  Recurse on the shorter part, iterate on the longer one.
         if J - Lo < Hi - J then
            Quick_Sort (Lo, J - 1, Depth);
            Lo := J + 1;
         else
            Quick_Sort (J + 1, Hi, Depth);
            Hi := J - 1;
         end if;
      end loop;
      Insertion_Sort (Lo, Hi);
   end Quick_Sort;

   Lo : constant Int := Int (Index_Type'Base'Pos (First));
   Hi : constant Int := Int (Index_Type'Base'Pos (Last));
   N  : Int;
   Depth : Natural := 0;
begin
   if Hi <= Lo then
      return;
   end if;
   N := Hi - Lo + 1;
   while N > 1 loop
      Depth := Depth + 2;
      N := N / 2;
   end loop;
   Quick_Sort (Lo, Hi, Depth);
end Ada.Containers.Generic_Sort;
