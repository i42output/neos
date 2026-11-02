--  Tasks: a generator (a task with entries), a producer and consumer through a protected buffer, and a
--  selective accept with a timeout

with Ada.Text_IO; use Ada.Text_IO;

procedure Tasks is

   task type Generator (Limit : Positive) is
      entry Next (Value : out Natural; Done : out Boolean);
   end Generator;

   task body Generator is
   begin
      for I in 1 .. Limit loop
         accept Next (Value : out Natural; Done : out Boolean) do
            Value := I;
            Done := False;
         end Next;
      end loop;
      accept Next (Value : out Natural; Done : out Boolean) do
         Value := 0;
         Done := True;
      end Next;
   end Generator;

   type String_Access is access constant String;
   type Item_Array is array (Positive range <>) of String_Access;

   protected type Buffer is
      entry Put (Item : String_Access);
      entry Get (Item : out String_Access);
   private
      Slot : String_Access;
      Full : Boolean := False;
   end Buffer;

   protected body Buffer is
      entry Put (Item : String_Access) when not Full is
      begin
         Slot := Item;
         Full := True;
      end Put;
      entry Get (Item : out String_Access) when Full is
      begin
         Item := Slot;
         Full := False;
      end Get;
   end Buffer;

   Items : constant Item_Array := (new String'("apple"), new String'("banana"), new String'("cherry"));
   Shared : Buffer;

   task Producer;
   task body Producer is
   begin
      for Item of Items loop
         Shared.Put (Item);
      end loop;
      Shared.Put (null);
   end Producer;

   task Server is
      entry Request (N : Integer; Result : out Integer);
   end Server;

   task body Server is
   begin
      loop
         select
            accept Request (N : Integer; Result : out Integer) do
               Result := N * N;
            end Request;
         or
            delay 0.1;
            Put_Line ("server: no more requests");
            exit;
         end select;
      end loop;
   end Server;

begin
   declare
      G : Generator (5);
      Value : Natural;
      Done : Boolean;
   begin
      loop
         G.Next (Value, Done);
         exit when Done;
         Put (Natural'Image (Value));
      end loop;
      New_Line;
   end;
   loop
      declare
         Item : String_Access;
      begin
         Shared.Get (Item);
         exit when Item = null;
         Put_Line ("consumed " & Item.all);
      end;
   end loop;
   declare
      Result : Integer;
   begin
      for N in 1 .. 3 loop
         Server.Request (N, Result);
         Put (Integer'Image (Result));
      end loop;
      New_Line;
   end;
end Tasks;
