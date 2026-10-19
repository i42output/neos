s = 'Hello, World'
print(s.upper(), s.lower(), s.swapcase(), s.casefold(), s.title(), 'hello world'.capitalize())
print('straße'.upper(), 'ΣΊΣΥΦΟΣ'.lower(), 'Straße'.casefold(), 'éCOLE'.capitalize())
print(repr(s.center(20)), repr(s.center(20, '*')), repr(s.ljust(15, '.')), repr(s.rjust(15)), repr('ab'.center(5)), repr('abc'.center(6)))
print('42'.zfill(5), '-42'.zfill(5), '+7'.zfill(4), 'abc'.zfill(2))
print(s.count('l'), s.count('l', 5), s.count(''), 'aaaa'.count('aa'), s.count('l', -3, -1))
print(s.find('o'), s.rfind('o'), s.find('o', 5), s.find('z'), s.find('', 20), s.find('', 12), s.rfind('l', 0, 5))
print(s.index('W'), s.rindex('l'))
try:
    s.index('xyz')
except ValueError as e:
    print('ValueError:', e)
print(s.startswith('Hell'), s.startswith(('x', 'He')), s.endswith('World'), s.startswith('World', 7), s.endswith('lo', 0, 5), 'abc'.startswith('', 3), 'abc'.startswith('', 4))
print(repr('  xx  '.strip()), repr('  xx  '.lstrip()), repr('  xx  '.rstrip()), 'xxhixx'.strip('x'), 'abcba'.strip('ab'), repr('\t\n a \x0b'.strip()))
print('a,b,,c'.split(','), 'a b  c '.split(), 'a b c'.split(None, 1), 'a,b,c'.split(',', 1), 'a,b,c'.rsplit(',', 1), '  a b  c  '.rsplit(None, 1), ''.split(), ''.split(','))
print('line1\nline2\r\nline3\rline4'.splitlines(), 'a\nb\n'.splitlines(True), ''.splitlines(), 'x\x0by\x1cz w'.splitlines())
print('a=b=c'.partition('='), 'a=b=c'.rpartition('='), 'abc'.partition('x'), 'abc'.rpartition('x'))
print('-'.join(['a', 'b', 'c']), ''.join(x for x in 'xyz'), ', '.join(map(str, range(4))))
print('aaa'.replace('a', 'b'), 'aaa'.replace('a', 'b', 2), 'abc'.replace('', '-'), 'abc'.replace('', '-', 2), 'hello'.replace('l', 'L', count=1))
print('TestCase'.removeprefix('Test'), 'TestCase'.removesuffix('Case'), 'abc'.removeprefix('x'))
print(repr('a\tb\tc'.expandtabs()), repr('a\tbc\td'.expandtabs(4)), repr('01\t012\t0123\t01234'.expandtabs(4)))
table = str.maketrans('abc', 'xyz', 'd')
print(sorted(table.items()), 'abcd'.translate(table), 'hello'.translate({ord('l'): None, ord('h'): 'J'}))
print(str.maketrans({'a': '1', 98: None}))
print('{name} is {age}'.format_map({'name': 'Ann', 'age': 30}))
print('abc'.isalpha(), 'abc1'.isalpha(), 'abc1'.isalnum(), '123'.isdigit(), '½'.isnumeric(), '½'.isdigit(), '²'.isdigit(), '²'.isdecimal(), '٣'.isdecimal())
print(''.isalpha(), ''.isascii(), 'é'.isascii(), '   '.isspace(), '\xa0'.isspace(), ''.isspace(), 'abc'.islower(), 'aBc'.islower(), 'ABC1'.isupper(), '1'.isupper())
print('Hello World'.istitle(), 'Hello world'.istitle(), 'HELLO'.istitle(), 'abc'.isidentifier(), '1abc'.isidentifier(), '_x1'.isidentifier(), 'é'.isidentifier())
print('abc'.isprintable(), 'a\nb'.isprintable(), ''.isprintable(), '\xa0'.isprintable(), 'é'.isalpha(), 'ß'.islower(), '日本'.isalpha(), '一二三'.isnumeric())
print(list(map(str.upper, ['a', 'b'])), sorted(['b', 'A', 'c'], key=str.lower), str.join('-', 'abc'))
print("they're bill's friends".title(), 'ǆ'.upper() if False else 'x', 'hello wORLD'.title())
for bad in [lambda: 'a'.center(), lambda: 'a'.center(5, 'ab'), lambda: 'a'.center(5, 1), lambda: 'a'.split(1), lambda: 'a'.strip(1),
            lambda: 'a'.startswith(1), lambda: 'a'.startswith(('a', 1)), lambda: 'a'.replace(1, 'b'), lambda: 'a'.partition(''),
            lambda: 'a'.find(1), lambda: 'a'.upper(1), lambda: 'a'.zfill('x'), lambda: '-'.join([1, 2]), lambda: 'a'.removeprefix(1),
            lambda: 'a'.count('a', 'x'), lambda: 'a'.split(','*0), lambda: str.upper(1)]:
    try:
        print(bad())
    except Exception as e:
        print(type(e).__name__, e)
