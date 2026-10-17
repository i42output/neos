class Shape:
    sides = 0
    def __init__(self, name='shape'):
        self.name = name
    def describe(self, prefix='A'):
        return f'{prefix} {self.name} with {self.sides} sides'
    def area(self):
        raise NotImplementedError
    def __lt__(self, other):
        return self.area() < other.area()
class Square(Shape):
    sides = 4
    def __init__(self, s):
        super().__init__('square')
        self.s = s
    def area(self):
        return self.s * self.s
class Tri(Shape):
    sides = 3
    def __init__(self, b, h):
        Shape.__init__(self, 'triangle')
        self.b = b
        self.h = h
    def area(self):
        return 0.5 * self.b * self.h
shapes = [Square(3), Tri(4, 5), Square(1)]
for s in sorted(shapes):
    print(s.describe(), s.area())
print(Square(2).describe('The'), Shape.sides, Square.sides, Square(2).sides)
try:
    Shape().area()
except NotImplementedError:
    print('abstract')
m = Square(5).area
print(m())
class Acc:
    def __init__(self):
        self.items = []
    def __call__(self, x):
        self.items.append(x)
        return len(self.items)
    def __contains__(self, x):
        return x in self.items
    def __bool__(self):
        return len(self.items) > 0
acc = Acc()
print(bool(acc), acc(5), acc(7), 5 in acc, 6 in acc, bool(acc))
def make():
    class Local:
        def hi(self):
            return 'hi'
    return Local()
print(make().hi())
class Node:
    def __init__(self, value, next=None):
        self.value = value
        self.next = next
def total(node):
    t = 0
    while node is not None:
        t += node.value
        node = node.next
    return t
print(total(Node(1, Node(2, Node(3)))))
print(hasattr(acc, 'items'), getattr(acc, 'missing', 'default'), hasattr(acc, 'nope'))
setattr(acc, 'extra', 9)
print(acc.extra, isinstance(acc, object), type(acc) is Acc, Acc.__name__)
class Temp:
    def __init__(self, c):
        self.c = c
    def __repr__(self):
        return f'Temp({self.c})'
    def __format__(self, spec):
        return format(self.c, spec) + 'C'
print(Temp(21.5))
print([Temp(1), Temp(2)])
class Num:
    def __init__(self, v):
        self.v = v
    def __radd__(self, other):
        return Num(other + self.v)
    def __mul__(self, k):
        return Num(self.v * k)
    def __neg__(self):
        return Num(-self.v)
    def __repr__(self):
        return 'Num(' + str(self.v) + ')'
print(1 + Num(2), Num(3) * 4, -Num(5), sum([Num(1), Num(2)], 0))
