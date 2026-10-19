class A:
    count = 0
    def __init__(self, v):
        self.v = v
        A.count += 1
    def get(self):
        return self.v
    def __repr__(self):
        return 'A(' + repr(self.v) + ')'
a = A(3)
print(a.v, a.get(), a, A.count, type(a).__name__, isinstance(a, A))
class B(A):
    def __init__(self, v, w):
        super().__init__(v)
        self.w = w
    def get(self):
        return super().get() * 10 + self.w
    def __str__(self):
        return f'B<{self.v},{self.w}>'
b = B(1, 2)
print(b.get(), b, repr(b), isinstance(b, A), issubclass(B, A), A.count)
class V:
    def __init__(self, x, y):
        self.x = x
        self.y = y
    def __add__(self, o):
        return V(self.x + o.x, self.y + o.y)
    def __eq__(self, o):
        return self.x == o.x and self.y == o.y
    def __len__(self):
        return 2
    def __getitem__(self, i):
        return [self.x, self.y][i]
    def __repr__(self):
        return f'V({self.x}, {self.y})'
    @property
    def norm2(self):
        return self.x * self.x + self.y * self.y
    @staticmethod
    def zero():
        return V(0, 0)
    @classmethod
    def unit(cls):
        return cls(1, 1)
v = V(1, 2) + V(3, 4)
print(v, v == V(4, 6), v != V(4, 6), len(v), v[1], list(v), v.norm2, V.zero(), V.unit())
class MyError(ValueError):
    pass
class Detailed(Exception):
    def __init__(self, code):
        super().__init__('code ' + str(code))
        self.code = code
try:
    raise MyError('bad thing')
except ValueError as e:
    print('caught', type(e).__name__, e, repr(e), isinstance(e, MyError))
try:
    raise Detailed(42)
except Detailed as e:
    print(e.code, e, e.args)
class Counter:
    def __init__(self, n):
        self.n = n
        self.i = 0
    def __iter__(self):
        return self
    def __next__(self):
        if self.i >= self.n:
            raise StopIteration
        self.i += 1
        return self.i
print([x for x in Counter(3)])
class P:
    def __init__(self):
        self._v = 1
    @property
    def v(self):
        return self._v
    @v.setter
    def v(self, value):
        self._v = value * 2
p = P()
p.v = 5
print(p.v)
