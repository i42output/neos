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

	type Character is
     (nul,     soh,   stx,    etx,      eot,   enq,    ack,   bel,   --0 (16#00#) .. 7 (16#07#)
      bs,      ht,    lf,     vt,       ff,    cr,     so,    si,    --8 (16#08#) .. 15 (16#0F#)

      dle,     dc1,   dc2,    dc3,      dc4,   nak,    syn,   etb,   --16 (16#10#) .. 23 (16#17#)
      can,     em,    sub,    esc,      fs,    gs,     rs,    us,    --24 (16#18#) .. 31 (16#1F#)

      ' ',     '!',   '"',    '#',      '$',   '%',    '&',   ''',   --32 (16#20#) .. 39 (16#27#)
      '(',     ')',   '*',    '+',      ',',   '-',    '.',   '/',   --40 (16#28#) .. 47 (16#2F#)

      '0',     '1',   '2',    '3',      '4',   '5',    '6',   '7',   --48 (16#30#) .. 55 (16#37#)
      '8',     '9',   ':',    ';',      '<',   '=',    '>',   '?',   --56 (16#38#) .. 63 (16#3F#)

      '@',     'A',   'B',    'C',      'D',   'E',    'F',   'G',   --64 (16#40#) .. 71 (16#47#)
      'H',     'I',   'J',    'K',      'L',   'M',    'N',   'O',   --72 (16#48#) .. 79 (16#4F#)

      'P',     'Q',   'R',    'S',      'T',   'U',    'V',   'W',   --80 (16#50#) .. 87 (16#57#)
      'X',     'Y',   'Z',    '[',      '\',   ']',    '^',   '_',   --88 (16#58#) .. 95 (16#5F#)

      '`',     'a',   'b',    'c',      'd',   'e',    'f',   'g',   --96 (16#60#) .. 103 (16#67#)
      'h',     'i',   'j',    'k',      'l',   'm',    'n',   'o',   --104 (16#68#) .. 111 (16#6F#)

      'p',     'q',   'r',    's',      't',   'u',    'v',   'w',   --112 (16#70#) .. 119 (16#77#)
      'x',     'y',   'z',    '{',      '|',   '}',    '~',   del,   --120 (16#78#) .. 127 (16#7F#)

      reserved_128,   reserved_129,     bph,   nbh,                  --128 (16#80#) .. 131 (16#83#)
      reserved_132,   nel,    ssa,      esa,                         --132 (16#84#) .. 135 (16#87#)
      hts,     htj,   vts,    pld,      plu,   ri,     ss2,   ss3,   --136 (16#88#) .. 143 (16#8F#)

      dcs,     pu1,   pu2,    sts,      cch,   mw,     spa,   epa,   --144 (16#90#) .. 151 (16#97#)
      sos,     reserved_153,  sci,      csi,                         --152 (16#98#) .. 155 (16#9B#)
      st,      osc,   pm,     apc,                                   --156 (16#9C#) .. 159 (16#9F#)

      ' ',     '¡',   '¢',    '£',      '¤',   '¥',    '¦',   '§',   --160 (16#A0#) .. 167 (16#A7#)
      '¨',     '©',   'ª',    '«',      '¬',   '­',    '®',   '¯',   --168 (16#A8#) .. 175 (16#AF#)

      '°',     '±',   '²',    '³',      '´',   'µ',    '¶',   '·',   --176 (16#B0#) .. 183 (16#B7#)
      '¸',     '¹',   'º',    '»',      '¼',   '½',    '¾',   '¿',   --184 (16#B8#) .. 191 (16#BF#)

      'À',     'Á',   'Â',    'Ã',      'Ä',   'Å',    'Æ',   'Ç',   --192 (16#C0#) .. 199 (16#C7#)
      'È',     'É',   'Ê',    'Ë',      'Ì',   'Í',    'Î',   'Ï',   --200 (16#C8#) .. 207 (16#CF#)

      'Ð',     'Ñ',   'Ò',    'Ó',      'Ô',   'Õ',    'Ö',   '×',   --208 (16#D0#) .. 215 (16#D7#)
      'Ø',     'Ù',   'Ú',    'Û',      'Ü',   'Ý',    'Þ',   'ß',   --216 (16#D8#) .. 223 (16#DF#)

      'à',     'á',   'â',    'ã',      'ä',   'å',    'æ',   'ç',   --224 (16#E0#) .. 231 (16#E7#)
      'è',     'é',   'ê',    'ë',      'ì',   'í',    'î',   'ï',   --232 (16#E8#) .. 239 (16#EF#)

      'ð',     'ñ',   'ò',    'ó',      'ô',   'õ',    'ö',   '÷',   --240 (16#F0#) .. 247 (16#F7#)
      'ø',     'ù',   'ú',    'û',      'ü',   'ý',    'þ',   'ÿ');--248 (16#F8#) .. 255 (16#FF#)

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