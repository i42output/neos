package Standard is

	type Boolean is (False, True);

	type Integer is range -(2 ** 31) .. +(2 ** 31 - 1);
	type Long_Integer is range -(2 ** 31) .. +(2 ** 31 - 1);
	type Long_Long_Integer is range -(2 ** 63) .. +(2 ** 63 - 1);

	subtype Natural  is Integer range 0 .. Integer'Last;
	subtype Positive is Integer range 1 .. Integer'Last;

	type Short_Float is digits 6 range -16#0.FFFF_FF#E+32 .. 16#0.FFFF_FF#E+32;
	type Float is digits 6 range -16#0.FFFF_FF#E+32 .. 16#0.FFFF_FF#E+32;
	type Long_Float is digits 15 range -16#0.FFFF_FFFF_FFFF_F8#E+256 .. 16#0.FFFF_FFFF_FFFF_F8#E+256;
	type Long_Long_FLoat is digits 15 range -16#0.FFFF_FFFF_FFFF_F8#E+256 .. 16#0.FFFF_FFFF_FFFF_F8#E+256;

	--  The declaration of type Character is based on the standard
	--  ISO 8859-1 character set.

	--  There are no character literals corresponding to the positions
	--  for control characters. They are indicated by lower case
	--  identifiers in the following list.

	--  Note: this type cannot be represented accurately in Ada

	--  type Character is

	--    (nul,  soh,  stx,  etx,     eot,  enq,  ack,  bel,
	--     bs,   ht,   lf,   vt,      ff,   cr,   so,   si,

	--     dle,  dc1,  dc2,  dc3,     dc4,  nak,  syn,  etb,
	--     can,  em,   sub,  esc,     fs,   gs,   rs,   us,

	--     ' ',  '!',  '"', '#',     '$',  '%',  '&',  ''',
	--     '(',  ')',  '*',  '+',     ',',  '-',  '.',  '/',

	--     '0',  '1',  '2',  '3',     '4',  '5',  '6',  '7',
	--     '8',  '9',  ':',  ';',     '<',  '=',  '>',  '?',

	--     '@',  'A',  'B',  'C',     'D',  'E',  'F',  'G',
	--     'H',  'I',  'J',  'K',     'L',  'M',  'N',  'O',

	--     'P',  'Q',  'R',  'S',     'T',  'U',  'V',  'W',
	--     'X',  'Y',  'Z',  '[',     '\',  ']',  '^',  '_',

	--     '`',  'a',  'b',  'c',     'd',  'e',  'f',  'g',
	--     'h',  'i',  'j',  'k',     'l',  'm',  'n',  'o',

	--     'p',  'q',  'r',  's',     't',  'u',  'v',  'w',
	--     'x',  'y',  'z',  '{',     '|',  '}',  '~',  del,

	--     reserved_128,     reserved_129,  bph,  nbh,
	--     reserved_132,     nel,     ssa,  esa,

	--     hts,  htj,  vts,  pld,     plu,  ri,   ss2,  ss3,

	--     dcs,  pu1,  pu2,  sts,     cch,  mw,   spa,  epa,

	--     sos, reserved_153, sci, csi,
	--     st,   osc,  pm,   apc,

	--   ... );

	--  The declaration of type Wide_Character is based on the standard
	--  ISO 10646 BMP character set.

	--  Note: this type cannot be represented accurately in Ada

	--  The first 256 positions have the same contents as type Character

	--  type Wide_Character is (nul, soh ... FFFE, FFFF);

	package ASCII is

		--  Control characters:

		NUL   : constant Character := Character'Val (16#00#);
		SOH   : constant Character := Character'Val (16#01#);
		STX   : constant Character := Character'Val (16#02#);
		ETX   : constant Character := Character'Val (16#03#);
		EOT   : constant Character := Character'Val (16#04#);
		ENQ   : constant Character := Character'Val (16#05#);
		ACK   : constant Character := Character'Val (16#06#);
		BEL   : constant Character := Character'Val (16#07#);
		BS    : constant Character := Character'Val (16#08#);
		HT    : constant Character := Character'Val (16#09#);
		LF    : constant Character := Character'Val (16#0A#);
		VT    : constant Character := Character'Val (16#0B#);
		FF    : constant Character := Character'Val (16#0C#);
		CR    : constant Character := Character'Val (16#0D#);
		SO    : constant Character := Character'Val (16#0E#);
		SI    : constant Character := Character'Val (16#0F#);
		DLE   : constant Character := Character'Val (16#10#);
		DC1   : constant Character := Character'Val (16#11#);
		DC2   : constant Character := Character'Val (16#12#);
		DC3   : constant Character := Character'Val (16#13#);
		DC4   : constant Character := Character'Val (16#14#);
		NAK   : constant Character := Character'Val (16#15#);
		SYN   : constant Character := Character'Val (16#16#);
		ETB   : constant Character := Character'Val (16#17#);
		CAN   : constant Character := Character'Val (16#18#);
		EM    : constant Character := Character'Val (16#19#);
		SUB   : constant Character := Character'Val (16#1A#);
		ESC   : constant Character := Character'Val (16#1B#);
		FS    : constant Character := Character'Val (16#1C#);
		GS    : constant Character := Character'Val (16#1D#);
		RS    : constant Character := Character'Val (16#1E#);
		US    : constant Character := Character'Val (16#1F#);
		DEL   : constant Character := Character'Val (16#7F#);

		-- Other characters:

		Exclam     : constant Character := '!';
		Quotation  : constant Character := '"'; --" Ignore this comment! It just fixes a highlighting bug
		Sharp      : constant Character := '#';
		Dollar     : constant Character := '$';
		Percent    : constant Character := '%';
		Ampersand  : constant Character := '&';
		Colon      : constant Character := ':';
		Semicolon  : constant Character := ';';
		Query      : constant Character := '?';
		At_Sign    : constant Character := '@';
		L_Bracket  : constant Character := '[';
		Back_Slash : constant Character := '\';
		R_Bracket  : constant Character := ']';
		Circumflex : constant Character := '^';
		Underline  : constant Character := '_';
		Grave      : constant Character := '`';
		L_Brace    : constant Character := '{';
		Bar        : constant Character := '|';
		R_Brace    : constant Character := '}';
		Tilde      : constant Character := '~';

		-- Lower case letters:

		LC_A : constant Character := 'a';
		LC_B : constant Character := 'b';
		LC_C : constant Character := 'c';
		LC_D : constant Character := 'd';
		LC_E : constant Character := 'e';
		LC_F : constant Character := 'f';
		LC_G : constant Character := 'g';
		LC_H : constant Character := 'h';
		LC_I : constant Character := 'i';
		LC_J : constant Character := 'j';
		LC_K : constant Character := 'k';
		LC_L : constant Character := 'l';
		LC_M : constant Character := 'm';
		LC_N : constant Character := 'n';
		LC_O : constant Character := 'o';
		LC_P : constant Character := 'p';
		LC_Q : constant Character := 'q';
		LC_R : constant Character := 'r';
		LC_S : constant Character := 's';
		LC_T : constant Character := 't';
		LC_U : constant Character := 'u';
		LC_V : constant Character := 'v';
		LC_W : constant Character := 'w';
		LC_X : constant Character := 'x';
		LC_Y : constant Character := 'y';
		LC_Z : constant Character := 'z';

	end ASCII;

	type String is array (Positive range <>) of Character;
	pragma Pack (String);

	type Wide_String is array (Positive range <>) of Wide_Character;
	pragma Pack (Wide_String);

	type Duration is delta 0.000000001 range -((2 ** 63 - 1) * 0.000000001) .. +((2 ** 63 - 1) * 0.000000001);
	for Duration'Small use 0.000000001;

	Constraint_Error : exception;
	Program_Error    : exception;
	Storage_Error    : exception;
	Tasking_Error    : exception;

end Standard;