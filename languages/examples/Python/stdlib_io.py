import io
import sys
import os
path = 'neos_io_test.txt'
with open(path, 'w') as f:
    print(f.write('line one\n'), f.write('line two\nline three'), f.mode, f.name, f.closed)
print(f.closed)
with open(path) as f:
    print(repr(f.read()))
with open(path) as f:
    print(f.readline(), end='')
    print(repr(f.readline()), f.readlines())
with open(path) as f:
    for line in f:
        print('>', line.rstrip())
with open(path, 'a') as f:
    f.writelines(['\nappended', ' more'])
print(open(path).read().splitlines())
with open(path, 'rb') as f:
    data = f.read()
    print(type(data).__name__, len(data), data[:8], f.tell())
    f.seek(5)
    print(f.read(3), f.tell())
with open('neos_bin.dat', 'wb') as f:
    f.write(bytes(range(10)))
    f.write(b'\r\n\x00')
print(open('neos_bin.dat', 'rb').read())
with open(path, 'w', encoding='latin-1') as f:
    f.write('caf\xe9')
print(open(path, 'rb').read(), open(path, encoding='latin-1').read())
with open(path, 'w') as f:
    f.write('héllo wörld €')
print(open(path, encoding='utf-8').read(), len(open(path, 'rb').read()))
with open(path, 'w', newline='') as f:
    f.write('a\r\nb\rc\n')
print(repr(open(path).read()), repr(open(path, newline='').read()))
s = io.StringIO()
s.write('hello ')
print('world', file=s)
print(repr(s.getvalue()), s.tell())
s.seek(0)
print(repr(s.read(5)), repr(s.readline()), repr(s.read()))
s2 = io.StringIO('a\nb\nc')
print(list(s2), s2.closed)
b = io.BytesIO(b'abc')
b.seek(0, 2)
b.write(b'def')
print(b.getvalue(), b.tell())
print('to stderr', file=sys.stderr)
sys.stdout.write('direct write\n')
print(sys.stdout.name, sys.stdin.mode)
try:
    open('/nonexistent/file.txt')
except FileNotFoundError as e:
    print(e, e.errno, e.strerror, e.filename)
try:
    open(path, 'z')
except ValueError as e:
    print(e)
f = open(path)
f.close()
try:
    f.read()
except ValueError as e:
    print(e)
try:
    with open(path) as f:
        f.write('x')
except io.UnsupportedOperation as e:
    print('UnsupportedOperation', e)
os.remove(path)
os.remove('neos_bin.dat')
print(os.path.exists(path))
