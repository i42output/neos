# bytes and bytearray, str.encode and bytes.decode; del statements
b = b"abc"
print(b, len(b), b[0], b[-1], b[1:], b[::-1], bytes([65, 66]), "x".encode(), type(b), isinstance(b, bytes))
print(b'\x00\x01\xff\n\t\'"', b"it's", b'say "hi"', b'both \' and "', rb'\x41\n', B'A' b'B' Rb'\C', b'''tri
ple''')
print(bytes(3), bytes(range(5)), bytearray(b'xyz'), bytearray(4), bytes(bytearray([1, 2])), bytes('é', 'utf-8'), bytearray('abc', 'ascii'))
print('héllo €'.encode(), 'héllo'.encode('latin-1'), 'abc'.encode('ascii'), b'h\xc3\xa9llo'.decode(), b'h\xe9'.decode('latin-1'))
print(b'\xff\xfeabc'.decode('utf-8', 'replace'), b'\xffabc'.decode('utf-8', errors='ignore'), b'\xffab'.decode('ascii', 'backslashreplace'), 'é€'.encode('ascii', 'xmlcharrefreplace'), 'é€'.encode('ascii', 'replace'))
print(b'hello world'.split(), b'a,b,,c'.split(b','), b'a b c'.split(maxsplit=1), b'a b c'.rsplit(None, 1), b'  x  '.strip(), b'xxhixx'.strip(b'x'), b'abc'.upper(), b'ABC'.lower())
print(b'hello'.find(b'l'), b'hello'.rfind(b'l'), b'hello'.find(108), b'hello'.count(b'l'), b'hello'.index(b'e'), b'hello'.startswith(b'he'), b'hello'.endswith((b'x', b'lo')))
print(b'-'.join([b'a', b'b', bytearray(b'c')]), b'abc'.replace(b'b', b'XY'), b'abc'.hex(), b'abcdef'.hex(':', 2), bytes.fromhex('de ad be ef'), bytearray.fromhex('00ff'))
print(b'a' + b'b', b'ab' * 3, 2 * b'x', b'b' in b'abc', 98 in b'abc', b'abc' == b'abc', b'abc' < b'abd', b'a' == 'a', hash(b'abc') == hash(b'abc'), hash(b''), hash(b'abc'))
print(list(b'hi'), [x for x in bytearray(b'AB')], sorted(b'cab'), max(b'xyz'), b'hello world'.title(), b'hello'.capitalize(), b'Hi'.swapcase())
print(b'abc'.isalpha(), b'123'.isdigit(), b'a1'.isalnum(), b' '.isspace(), b'Ab'.istitle(), b'ab'.islower(), b'AB'.isupper(), b'\x80'.isascii())
print(b'a=b'.partition(b'='), b'a=b=c'.rpartition(b'='), b'prefix-x'.removeprefix(b'prefix-'), b'x.py'.removesuffix(b'.py'), b'ab'.center(6, b'*'), b'ab'.ljust(4), b'ab'.rjust(4, b'0'), b'-42'.zfill(6))
print(b'a\nb\r\nc'.splitlines(), b'a\nb\r\n'.splitlines(True), b'a\tb'.expandtabs(4), b'abc'.translate(None, b'b'))
ba = bytearray(b'hello')
ba[0] = 72
ba.append(33)
ba.extend(b'??')
ba.insert(0, 62)
print(ba, ba.pop(), ba.pop(0), len(ba))
ba.remove(33)
ba.reverse()
print(ba, ba.copy(), bytes(ba))
ba[1:3] = b'XYZ'
del ba[0]
ba += b'++'
ba *= 2
print(ba)
del ba[::2]
print(ba)
ba.clear()
print(ba, repr(ba), str(b'x'), str(b'abc', 'ascii'), str(bytearray(b'z'), encoding='utf-8'))
tests = [
    ('b"a"+"b"', lambda: b"a"+"b"),
    ('b"a"*1.5', lambda: b"a"*1.5),
    ('bytes(-1)', lambda: bytes(-1)),
    ('bytes([256])', lambda: bytes([256])),
    ('bytes("abc")', lambda: bytes("abc")),
    ('bytes(1.5)', lambda: bytes(1.5)),
    ('b"\\xff".decode()', lambda: b"\xff".decode()),
    ('"\\xe9".encode("ascii")', lambda: "\xe9".encode("ascii")),
    ('"ab\\u20ac\\u20acc".encode("latin-1")', lambda: "ab\u20ac\u20acc".encode("latin-1")),
    ('b"\\xe2\\x82".decode()', lambda: b"\xe2\x82".decode()),
    ('b"a".decode("foo")', lambda: b"a".decode("foo")),
    ('bytes.fromhex("zz")', lambda: bytes.fromhex("zz")),
    ('b"a".startswith("a")', lambda: b"a".startswith("a")),
    ('b" ".join(["a"])', lambda: b" ".join(["a"])),
    ('bytearray(b"x").remove(5)', lambda: bytearray(b"x").remove(5)),
    ('b"a" < "a"', lambda: b"a" < "a"),
    ('hash(bytearray())', lambda: hash(bytearray())),
    ('b"\\xc3\\x28".decode()', lambda: b"\xc3\x28".decode()),
    ('b"\\xed\\xa0\\x80".decode()', lambda: b"\xed\xa0\x80".decode()),
    ('b"a"["x"]', lambda: b"a"[str("x")]),
    ('b"a"[5]', lambda: b"a"[5]),
    ('bytearray(b"a")[5]', lambda: bytearray(b"a")[5]),
    ('b"a".find()', lambda: b"a".find()),
    ('b"".decode(1)', lambda: b"".decode(1)),
    ('b"".decode(foo=1)', lambda: b"".decode(foo=1)),
    ('b"".strip(x=1)', lambda: b"".strip(x=1)),
    ('b"".zfill()', lambda: b"".zfill()),
    ('b"".upper(1)', lambda: b"".upper(1)),
    ('str("a", "utf-8")', lambda: str("a", "utf-8")),
    ('str(5, "utf-8")', lambda: str(5, "utf-8")),
    ('b"a".count("a")', lambda: b"a".count("a")),
]
for t, f in tests:
    try:
        print(t, repr(f()))
    except Exception as e:
        print(t, type(e).__name__, e)
d = {'a': 1, 'b': 2, 'c': 3}
del d['b']
l = [0, 1, 2, 3, 4, 5]
del l[0], l[-1]
del l[1:3]
print(d, l)
class C:
    pass
c = C()
c.x = 1
c.y = 2
del c.x
print(hasattr(c, 'x'), c.y)
try:
    del c.z
except AttributeError as e:
    print(e)
try:
    del d['zz']
except KeyError as e:
    print('KeyError', e)
x = [1]
del x
delattr(c, 'y')
print(hasattr(c, 'y'))
