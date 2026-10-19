# statement separators, starred assignment targets, assignment expressions
a = 1; b = 2; print(a, b);
if a: print('x'); print('y')
for i in range(2): print(i); print(-i)
def f(x): y = x * 2; return y
print(f(3)); import math; print(math.pi)
while a < 3: a += 1; print('a', a)
a, *b = [1, 2, 3]
*c, d = 'xyz'
e, *f, g = range(6)
h, *i = (1,)
print(a, b, c, d, e, f, g, h, i)
for x, *y in [(1, 2, 3), (4,)]:
    print(x, y)
print([(p, q) for p, *q in ['ab', 'cde']])
try:
    j, *k, l = [1]
except ValueError as ex:
    print(ex)
if (n := 5) > 3: print(n)
data = [1, 5, 2, 8]
print([y for x in data if (y := x * 2) > 4], y)
while (m := len(data)) > 2:
    data.pop()
print(m, data)
def fw(v):
    if (w := v + 1) > 2:
        return w
    return -w
print(fw(1), fw(5))
