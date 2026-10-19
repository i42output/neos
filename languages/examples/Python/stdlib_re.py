import re

m = re.match(r'(\w+) (\w+)', 'Isaac Newton, physicist')
print(m, m.group(0), m.group(1), m.group(2), m.group(1, 2), m.groups(), m.span(), m.start(2), m.end(2), m[1], m.lastindex, m.re.pattern, m.string)
print(re.search(r'\d+', 'abc 123 def 45'), re.search('x', 'abc'), re.match('b', 'abc'), re.fullmatch(r'\d+', '123'), re.fullmatch(r'\d+', '123a'))
print(re.findall(r'\d+', 'a1b22c333'), re.findall(r'(\w)=(\d)', 'a=1 b=2'), re.findall(r'(\w+)@', 'x@ y@'), re.findall('', 'ab'))
print([m.span() for m in re.finditer(r'\w+', 'hello big world')], re.sub(r'\s+', ' ', 'a   b \t c'), re.sub(r'(\w+) (\w+)', r'\2 \1', 'hello world'))
print(re.sub('x*', '-', 'abxd'), re.subn('a', 'b', 'aaa'), re.sub('a', 'b', 'aaa', count=2), re.sub(r'(?P<first>\w+)', r'<\g<first>>', 'hi there'), re.sub(r'\d', lambda m: str(int(m.group()) * 2), 'a1b2c3'))
print(re.split(r'\W+', 'Words, words, words.'), re.split(r'(\W+)', 'Words, words.'), re.split(r'\W+', 'Words, words, words.', maxsplit=1), re.split('x*', 'axbc'), re.split(r'\b', 'a b'))
m = re.match(r'(?P<first>\w+) (?P<last>\w+)', 'Jane Doe')
print(m.group('first'), m['last'], m.groupdict(), m.lastgroup, m.start('last'), m.re.groupindex, m.expand(r'\g<last>, \g<first>'))
print(re.match(r'(a)(b)?', 'a').groups(), re.match(r'(a)(b)?', 'a').groups('-'), re.match(r'(a)(b)?', 'a').group(2), re.match(r'(a)(b)?', 'a').span(2))
p = re.compile(r'[a-z]+', re.IGNORECASE)
print(p, p.pattern, p.flags, p.groups, p.match('HeLLo World'), p.findall('One two THREE'), p.search('123 abc', 2), p.match('xy', 1), p.fullmatch('abc', 0, 2))
print(re.compile('a', re.I | re.M), re.compile('x'), re.compile(r'\d') == re.compile(r'\d'), re.compile('a') is re.compile('a'))
print(re.findall(r'^\w+', 'one\ntwo\nthree', re.M), re.findall(r'\w+$', 'one\ntwo\n'), re.findall('a.c', 'abc a\nc', re.S), re.findall('a.c', 'abc a\nc'))
print(re.findall(r'''\d+  # digits
    \.?   # optional dot''', '1. 22 3.', re.X))
print(re.match(r'(?i)hello', 'HELLO'), re.match(r'(?i:A)b', 'ab'), re.match(r'(?i:A)b', 'aB'))
print(re.findall(r'\d+(?=px)', '10px 20em 30px'), re.findall(r'\d+(?!px|\d)', '10px 20em 30px'), re.findall(r'(?<=\$)\d+', '$10 and $20 vs 30'), re.findall(r'(?<!\$)\b\d+', '$10 and 30'))
print(re.match(r'(\w)\1', 'aa'), re.match(r'(\w)\1', 'ab'), re.findall(r'(\w)\1', 'aabbcd'), re.match(r'(?P<q>[\'"]).*?(?P=q)', '"quoted" text').group())
print(re.findall(r'a+?', 'aaa'), re.findall(r'a*?b', 'aaab'), re.match(r'a{2,3}', 'aaaa'), re.match(r'a{2,3}?', 'aaaa'), re.findall(r'x{2}', 'xxxxx'), re.match(r'a{,2}', 'aaa'))
print(re.match(r'(?:ab)+', 'ababx'), re.match(r'(ab)+', 'ababx').group(1), re.match(r'(a|ab)(c|bcd)(d*)', 'abcd').groups(), re.match(r'(?>a+)b', 'aaab'), re.match(r'a++b', 'aaab'), re.match(r'(?>a+)a', 'aaa'))
print(re.match(r'(a)?(?(1)b|c)', 'ab'), re.match(r'(a)?(?(1)b|c)', 'c'), re.match(r'(a)?(?(1)b|c)', 'ac'))
print(re.findall(r'[^aeiou\s]+', 'hello world'), re.findall(r'[\d.]+', 'v1.2.3 and 4'), re.findall(r'[a-c-]', 'a-d'), re.findall(r'[]a]', ']a'), re.findall(r'\bcat\b', 'cat concat cat.'), re.findall(r'\Bcat', 'cat concat'))
print(re.escape('a.b*c'), re.escape('100% (sure)'), re.findall(r'\s', 'a b\tc\nd'), re.findall(r'\S+', ' x yz '), re.findall(r'\W', 'a-b c'), re.findall(r'\D+', '12ab34'))
print(re.match(r'\Aabc\Z', 'abc'), re.search(r'c\Z', 'abc\n'), re.search(r'c$', 'abc\n'), re.findall(r'é+', 'cafééé'), re.findall(r'\w+', 'naïve café'), re.findall(r'(?a)\w+', 'naïve café'))
print(re.sub(r'(\d+)', r'[\1]', 'a12b3'), re.sub('a', r'\n', 'bab'), re.sub(r'(a)', r'\g<1>0', 'a'), re.sub('', '-', 'abc'), re.sub(r'\b', '|', 'hi you'))
date = re.compile(r'(\d{4})-(\d{2})-(\d{2})')
print(date.sub(r'\3/\2/\1', 'on 2024-01-15 and 2023-12-31'), [m.groups() for m in date.finditer('2024-01-15 2023-12-31')], date.split('x2024-01-15y'))
print(bool(re.match('a', 'a')), isinstance(p, re.Pattern), isinstance(m, re.Match), re.match('a', 'a').pos, re.compile('b').search('abc', 1).endpos)
for bad in ['(', ')', '[a', '*a', 'a**', r'\q', '(?P<1>a)', 'a{2,1}', '(?<=a+)b', r'(a)\2', '(?z)']:
    try:
        re.compile(bad)
    except re.error as e:
        print('error', e, '|', e.msg, e.pos)
try:
    re.match('a', 5)
except TypeError as e:
    print('TypeError', e)
try:
    re.sub('(a)', r'\2', 'a')
except re.error as e:
    print('error', e)
