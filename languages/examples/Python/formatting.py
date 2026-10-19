x = 3
name = 'neos'
pi = 3.14159
print(f'{1 + 2} and {3.5:.2f}')
print(f"hello {name}!", f'{name!r}', f'{x=}', f'{pi:{10}.{3}f}|', f'{{literal}} {x * 2}')
print(f'{x:>5}|{x:<5}|{x:05d}|{name:^10}|{pi:e}|{pi:%}')
w = 8
print(f'{name:>{w}}', f'''triple {x}''', rf'raw\n{x}')
items = [1, 2, 3]
print(f'{items} {len(items)} {items[0]} {"q"}')
print(f'{x!s:>4}', f'{"a" + "b"}', f'{x if x > 2 else 0}', f'{x == 3}', f'{x != 3}')
print('%d items' % 3)
print('%5.2f|%-6s|%03d' % (pi, 'ab', 7))
print('{} {}'.format(1, 2), format(255, '#x'), format(1234567, ','))
print(0xFF, 0o17, 0b101, 1_000_000, 0x_ff)
z = 3j + 1
print(z, 2j, 1.5j * 2, abs(3 + 4j))
d = {i: i * i for i in range(4)}
print(d)
e = {k: v for k, v in [('a', 1), ('b', 2)] if v > 1}
print(e)
def setg(v):
    global gv
    gv = v
setg(5)
print(gv)
try:
    print(1 / 0)
except ZeroDivisionError as err:
    print('caught', err)
try:
    print(7 // 0)
except ZeroDivisionError as err:
    print('caught', err)
try:
    print(7.5 % 0.0)
except ZeroDivisionError as err:
    print('caught', err)
x = [3, 1, 2]
x.sort()
print(x)
x.sort(reverse=True)
print(x)
print('a\x41\u00e9\101\\d', r'\d\n', 'tab\there', "q\"q", u'unicode')
print(r'C:\path\to', R"raw\"quote", f'{"nested"}', rf'{x}\n')
