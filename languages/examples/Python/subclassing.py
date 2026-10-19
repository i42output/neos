class MyDict(dict):
    def __setitem__(self, key, value):
        print('set', key, value)
        super().__setitem__(key, value * 2)

d = MyDict(a=1)
d['b'] = 2
print(d, len(d), d['b'], 'a' in d, list(d), sorted(d.items()), isinstance(d, dict), isinstance(d, MyDict), type(d).__name__)
print(d.get('a'), d.keys(), issubclass(MyDict, dict), MyDict.__mro__)

class Missing(dict):
    def __missing__(self, key):
        return key.upper()
m = Missing(x=1)
print(m['x'], m['abc'], m.get('abc'), 'abc' in m)

class Stack(list):
    def push(self, x):
        self.append(x)
    def peek(self):
        return self[-1]
s = Stack([1, 2])
s.push(3)
print(s, s.peek(), len(s), s + [4], [0] + s, s * 2, s == [1, 2, 3], bool(Stack()), s[1:], sum(s), sorted(s, reverse=True))
s += [9]
print(type(s).__name__, s)
s.sort(reverse=True)
print(s, s.pop(), s)

class Point(tuple):
    def __new__(cls, x, y):
        return super().__new__(cls, (x, y))
    @property
    def x(self):
        return self[0]
    def __repr__(self):
        return f'Point({self[0]}, {self[1]})'
p = Point(3, 4)
x, y = p
print(p, p.x, x + y, len(p), p == (3, 4), hash(p) == hash((3, 4)), isinstance(p, tuple), p.count(3), {p: 1}[(3, 4)])

class Upper(str):
    def shout(self):
        return self.upper() + '!'
u = Upper('hi')
print(u, repr(u), u.shout(), u + ' there', len(u), u == 'hi', isinstance(u, str), u.startswith('h'))

class Meters(float):
    def __str__(self):
        return f'{float(self)} m'
mm = Meters(2.5)
print(mm, mm + 1, mm * 2, round(mm), int(mm), mm > 2)

class Counted(int):
    pass
c = Counted(5)
print(c + 1, c * c, -c, c == 5, [10, 20, 30, 40, 50, 60][c], hex(c), isinstance(c, int))

class Singleton:
    _instance = None
    def __new__(cls):
        if cls._instance is None:
            cls._instance = super().__new__(cls)
        return cls._instance
print(Singleton() is Singleton())

class WithInit(list):
    def __init__(self, items, label):
        super().__init__(items)
        self.label = label
w = WithInit((1, 2), 'L')
print(w, w.label, len(w))
print(dict.__getitem__({'a': 1}, 'a'), list.__len__([1, 2]), int.__add__(1, 2), (5).__add__(3), 'ab'.__len__(), dict(MyDict(z=3)))
def kw(**k):
    return k
print(kw(**MyDict(q=1)))
