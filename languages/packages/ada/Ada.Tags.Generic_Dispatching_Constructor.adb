--  Ada.Tags.Generic_Dispatching_Constructor.adb
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

function Ada.Tags.Generic_Dispatching_Constructor
   (The_Tag : Tag;
    Params  : not null access Parameters) return T'Class
is
   --  (the type identified by The_Tag: its Constructor (a tag-indeterminate
   --  call dispatching on Dispatching (an implementation-defined pragma)))
   Dispatching : aliased constant Tag := The_Tag;
   pragma Dispatching_Tag (Dispatching);
   function Level_Of (T : Tag) return Integer;
   pragma Import (Neos, Level_Of, "ada::tag_level");
   --  (a type declared within the instance: deeper than the instance)
   type Within is tagged null record;
begin
   --  (a concrete descendant of T whose master is a master of the instance
   --  (3.9(25.3/2)))
   if The_Tag = No_Tag or else Is_Abstract (The_Tag)
     or else not Is_Descendant_At_Same_Level (The_Tag, T'Tag)
     or else Level_Of (The_Tag) >= Level_Of (Within'Tag)
   then
      raise Tag_Error;
   end if;
   return Constructor (Params);
end Ada.Tags.Generic_Dispatching_Constructor;
