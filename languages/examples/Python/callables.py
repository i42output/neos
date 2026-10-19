def g(a, b=2):
    return a + b
print(g(1), g(1, 5), g(b=10, a=1))
def h(*args):
    return len(args)
print(h(1, 2, 3), h())
def k(a, *rest, sep='-', **kw):
    print(a, rest, sep, kw)
k(1)
k(1, 2, 3, sep='+', x=4, y='z')
def m(x, items=[]):
    items.append(x)
    return items
print(m(1), m(2))
def n(a, b: int = 3, c: float = 1.5, d='s', e=None):
    print(a, b, c, d, e)
n(0)
n(0, 4, d='t')
def p(a, *, key):
    return a * key
print(p(3, key=4))
def k2(a, b, *rest, **kw):
    print(a, b, rest, kw)
args = [1, 2, 3]
k2(*args)
k2(0, *args, x=1)
opts = {'y': 2}
k2(1, 2, **opts)
print(max(*[3, 9, 2]), max(*args))
print(*[1, 2, 3])
d = {'sep': '-'}
print('a', 'b', **d)
def f(x):
    return x * 2
g = f
print(g(4), g(x=5), g.__name__)
def add(a, b=10):
    return a + b
h = add
print(h(1), h(1, 2), h(b=3, a=4))
def apply(func, value):
    return func(value)
print(apply(f, 21), apply(add, 1))
def typed(n: int, s: str = 'x') -> str:
    return s * n
t = typed
print(t(3), t(2, 'ab'))
def nothing():
    pass
fs = [f, add]
print([fn(1) for fn in fs])
print(list(map(f, [1, 2, 3])), sorted([3, 1, 2], key=f))
f = lambda x: x + 1
print(f(2), f.__name__)
print(list(map(lambda v: v * 3, [1, 2])), sorted(['bb', 'a', 'ccc'], key=lambda s: len(s)))
g = lambda a, b=2: a * b
print(g(3), g(3, b=5))
print((lambda: 'none')())
h = lambda x: (lambda y: y * 2)(x) + 1
print(h(5))
pairs = [(1, 'b'), (2, 'a')]
print(sorted(pairs, key=lambda p: p[1]))
