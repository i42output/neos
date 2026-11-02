--  Ada.Containers.Indefinite_Holders.adb
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

with Ada.Finalization;
with Ada.Streams;
with Ada.Unchecked_Conversion;
with Ada.Unchecked_Deallocation;

package body Ada.Containers.Indefinite_Holders is

   --  Query_Element takes the holder as an in parameter but still has to
   --  record that tampering is prohibited: the variable view is obtained by
   --  converting the access-to-constant value designating it.
   type Constant_Holder_Access is access constant Holder;

   function To_Variable is new Ada.Unchecked_Conversion
     (Constant_Holder_Access, Holder_Access);

   function Var (Container : Holder) return Holder_Access is
     (To_Variable (Container'Unchecked_Access));

   procedure Free is new Ada.Unchecked_Deallocation
     (Element_Type, Element_Access);

   procedure Check_Tampering (Container : Holder) is
   begin
      if Container.Lock > 0 then
         raise Program_Error with "attempt to tamper with the element";
      end if;
   end Check_Tampering;

   procedure Check_Not_Empty (Container : Holder) is
   begin
      if Container.Element = null then
         raise Constraint_Error with "holder is empty";
      end if;
   end Check_Not_Empty;

   --  Prohibits tampering while it exists.
   type Lock_Guard (Container : Holder_Access) is
     new Ada.Finalization.Limited_Controlled with null record;
   overriding procedure Initialize (Guard : in out Lock_Guard);
   overriding procedure Finalize (Guard : in out Lock_Guard);

   overriding procedure Initialize (Guard : in out Lock_Guard) is
   begin
      Guard.Container.Lock := Guard.Container.Lock + 1;
   end Initialize;

   overriding procedure Finalize (Guard : in out Lock_Guard) is
   begin
      Guard.Container.Lock := Guard.Container.Lock - 1;
   end Finalize;

   overriding procedure Adjust (Control : in out Reference_Control) is
   begin
      if Control.Container /= null then
         Control.Container.Lock := Control.Container.Lock + 1;
      end if;
   end Adjust;

   overriding procedure Finalize (Control : in out Reference_Control) is
   begin
      if Control.Container /= null then
         Control.Container.Lock := Control.Container.Lock - 1;
         Control.Container := null;
      end if;
   end Finalize;

   --  Records that tampering with elements is prohibited (by a reference
   --  object about to be created) and returns the container's access.
   function Lock (Container : Holder) return Holder_Access is
      C : constant Holder_Access := Var (Container);
   begin
      C.Lock := C.Lock + 1;
      return C;
   end Lock;

   overriding procedure Adjust (Container : in out Holder) is
   begin
      Container.Lock := 0;
      if Container.Element /= null then
         Container.Element := new Element_Type'(Container.Element.all);
      end if;
   end Adjust;

   --  (finalized while tampering is prohibited (e.g. the target of an
   --  assignment): Program_Error, the holder left unchanged)
   overriding procedure Finalize (Container : in out Holder) is
   begin
      if Container.Lock > 0 then
         raise Program_Error with "holder finalized while tampering is prohibited";
      end if;
      Free (Container.Element);
   end Finalize;

   procedure Write
     (Stream    : not null access Ada.Streams.Root_Stream_Type'Class;
      Container : Holder) is
   begin
      Boolean'Write (Stream, Container.Element = null);
      if Container.Element /= null then
         Element_Type'Output (Stream, Container.Element.all);
      end if;
   end Write;

   procedure Read
     (Stream    : not null access Ada.Streams.Root_Stream_Type'Class;
      Container : out Holder)
   is
      Empty : Boolean;
   begin
      Boolean'Read (Stream, Empty);
      if Empty then
         Clear (Container);
      else
         Replace_Element (Container, Element_Type'Input (Stream));
      end if;
   end Read;

   procedure Write
     (Stream : not null access Ada.Streams.Root_Stream_Type'Class;
      Item   : Constant_Reference_Type) is
   begin
      raise Program_Error with "attempt to stream a reference";
   end Write;

   procedure Read
     (Stream : not null access Ada.Streams.Root_Stream_Type'Class;
      Item   : out Constant_Reference_Type) is
   begin
      raise Program_Error with "attempt to stream a reference";
   end Read;

   procedure Write
     (Stream : not null access Ada.Streams.Root_Stream_Type'Class;
      Item   : Reference_Type) is
   begin
      raise Program_Error with "attempt to stream a reference";
   end Write;

   procedure Read
     (Stream : not null access Ada.Streams.Root_Stream_Type'Class;
      Item   : out Reference_Type) is
   begin
      raise Program_Error with "attempt to stream a reference";
   end Read;

   function "=" (Left, Right : Holder) return Boolean is
   begin
      if Var (Left) = Var (Right) then
         return True;
      elsif Left.Element = null or else Right.Element = null then
         return Left.Element = null and then Right.Element = null;
      end if;
      return Left.Element.all = Right.Element.all;
   end "=";

   function Tampering_With_The_Element_Prohibited
     (Container : Holder) return Boolean is
     (Container.Lock > 0);

   function To_Holder (New_Item : Element_Type) return Holder is
   begin
      return (Ada.Finalization.Controlled with
              Element => new Element_Type'(New_Item), Lock => 0);
   end To_Holder;

   function Is_Empty (Container : Holder) return Boolean is
     (Container.Element = null);

   procedure Clear (Container : in out Holder) is
   begin
      Check_Tampering (Container);
      Free (Container.Element);
   end Clear;

   function Element (Container : Holder) return Element_Type is
   begin
      Check_Not_Empty (Container);
      return Container.Element.all;
   end Element;

   procedure Replace_Element (Container : in out Holder;
                              New_Item  : in     Element_Type)
   is
      X : Element_Access;
   begin
      Check_Tampering (Container);
      X := new Element_Type'(New_Item);
      Free (Container.Element);
      Container.Element := X;
   end Replace_Element;

   procedure Query_Element
     (Container : in Holder;
      Process   : not null access procedure (Element : in Element_Type)) is
   begin
      Check_Not_Empty (Container);
      declare
         Guard : Lock_Guard (Var (Container));
      begin
         Process (Container.Element.all);
      end;
   end Query_Element;

   procedure Update_Element
     (Container : in out Holder;
      Process   : not null access procedure (Element : in out Element_Type)) is
   begin
      Check_Not_Empty (Container);
      declare
         Guard : Lock_Guard (Var (Container));
      begin
         Process (Container.Element.all);
      end;
   end Update_Element;

   function Constant_Reference (Container : aliased in Holder)
      return Constant_Reference_Type is
   begin
      Check_Not_Empty (Container);
      return (Element => Container.Element, Control => (Ada.Finalization.Controlled with
                          Container => Lock (Container)));
   end Constant_Reference;

   function Reference (Container : aliased in out Holder)
      return Reference_Type is
   begin
      Check_Not_Empty (Container);
      return (Element => Container.Element, Control => (Ada.Finalization.Controlled with
                          Container => Lock (Container)));
   end Reference;

   procedure Assign (Target : in out Holder; Source : in Holder) is
   begin
      if Var (Target) = Var (Source) then
         return;
      elsif Source.Element = null then
         Clear (Target);
      else
         Replace_Element (Target, Source.Element.all);
      end if;
   end Assign;

   function Copy (Source : Holder) return Holder is
   begin
      if Source.Element = null then
         return Empty_Holder;
      end if;
      return To_Holder (Source.Element.all);
   end Copy;

   procedure Move (Target : in out Holder; Source : in out Holder) is
   begin
      Check_Tampering (Target);
      Check_Tampering (Source);
      if Var (Target) = Var (Source) then
         return;
      end if;
      Free (Target.Element);
      Target.Element := Source.Element;
      Source.Element := null;
   end Move;

   procedure Swap (Left, Right : in out Holder) is
      X : constant Element_Access := Left.Element;
   begin
      Check_Tampering (Left);
      Check_Tampering (Right);
      if Var (Left) = Var (Right) then
         return;
      end if;
      Left.Element := Right.Element;
      Right.Element := X;
   end Swap;

end Ada.Containers.Indefinite_Holders;
