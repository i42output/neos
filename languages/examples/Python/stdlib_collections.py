from collections import namedtuple, deque, defaultdict, OrderedDict, Counter, ChainMap
import collections

Point = namedtuple('Point', 'x y')
p = Point(1, y=2)
print(p, p.x, p.y, p[0], p[-1], len(p), tuple(p), p == (1, 2), p._fields, Point._fields, isinstance(p, tuple), type(p).__name__)
x, y = p
print(x, y, p._asdict(), p._replace(x=10), Point._make([5, 6]), hash(p) == hash((1, 2)), p + (3,), p * 2, sorted([Point(2, 1), Point(1, 5)]))
P3 = namedtuple('P3', ['a', 'b', 'c'], defaults=[0, 0])
print(P3(1), P3(1, 2, 3), P3._field_defaults, P3.__doc__)
R = namedtuple('R', 'a def a _x', rename=True)
print(R._fields)
class Vec(namedtuple('Vec', 'x y')):
    __slots__ = ()
    def __abs__(self):
        return (self.x ** 2 + self.y ** 2) ** 0.5
    def __add__(self, other):
        return Vec(self.x + other.x, self.y + other.y)
v = Vec(3, 4)
print(v, abs(v), v + Vec(1, 1), isinstance(v, Vec), Vec.__mro__[1].__name__)
for bad in [lambda: Point(1), lambda: Point(1, 2, 3), lambda: Point(1, 2, z=3), lambda: Point(1, x=2), lambda: namedtuple('A', 'x x'),
            lambda: namedtuple('A', 'x _y'), lambda: namedtuple('A', 'class'), lambda: namedtuple('1A', 'x'), lambda: p._replace(z=1),
            lambda: Point._make([1]), lambda: setattr(p, 'x', 5)]:
    try:
        bad()
    except Exception as e:
        print(type(e).__name__, e)

d = deque([1, 2, 3])
d.append(4)
d.appendleft(0)
print(d, len(d), d[0], d[-1], d.pop(), d.popleft(), d, 2 in d, list(reversed(d)))
d.extend([7, 8])
d.extendleft([-1, -2])
print(d)
d.rotate(2)
print(d)
d.rotate(-3)
print(d, d.count(7), d.index(7))
d.remove(7)
d.insert(1, 99)
d[0] = 'first'
del d[2]
print(d, d.maxlen, d == deque(['first', 99, 2, 3, 8, -2]), d + deque([1]), deque([1, 2]) * 2)
b = deque(range(10), maxlen=3)
print(b, b.maxlen)
b.append(10)
b.appendleft(-1)
print(b, list(b), bool(deque()), deque('abc'), b.copy())
d2 = deque([1, 2])
d2 += [3]
d2.reverse()
print(d2, deque([1, 2]) < deque([1, 3]))
for bad in [lambda: deque().pop(), lambda: deque([1])[5], lambda: deque([1]).remove(2), lambda: deque(maxlen=-1), lambda: deque([1], maxlen=1).insert(0, 2),
            lambda: hash(deque())]:
    try:
        bad()
    except Exception as e:
        print(type(e).__name__, e)
try:
    for item in d2:
        d2.append(item)
except RuntimeError as e:
    print('RuntimeError', e)

dd = defaultdict(list)
for word in ['apple', 'bob', 'avocado', 'cat', 'banana']:
    dd[word[0]].append(word)
print(dd, dd['a'], dd['z'], len(dd), dd.default_factory, isinstance(dd, dict), sorted(dd))
counts = defaultdict(int)
for ch in 'mississippi':
    counts[ch] += 1
print(counts, dict(counts), counts.get('q'), 'q' in counts)
nd = defaultdict()
try:
    nd['x']
except KeyError as e:
    print('KeyError', e)
print(nd, defaultdict(lambda: 'v', a=1)['b'], defaultdict(None, {'k': 1}), dd.copy() == dd)
try:
    defaultdict(5)
except TypeError as e:
    print('TypeError', e)

od = OrderedDict()
od['b'] = 1
od['a'] = 2
od['c'] = 3
print(od, list(od), od.popitem(), od.popitem(last=False), od)
od.update(x=1, y=2)
od.move_to_end('a')
od.move_to_end('y', last=False)
print(od, list(reversed(od)), OrderedDict.fromkeys('ab', 0), OrderedDict(), OrderedDict(a=1) == OrderedDict(a=1))
print(OrderedDict(a=1, b=2) == OrderedDict(b=2, a=1), OrderedDict(a=1, b=2) == {'b': 2, 'a': 1}, isinstance(od, dict))
try:
    od.move_to_end('zz')
except KeyError as e:
    print('KeyError', e)

c = Counter('abracadabra')
print(c, c['a'], c['z'], c.most_common(2), sorted(c.elements()), c.total(), len(c))
c.update('aaz')
c.subtract({'b': 5})
print(c, +c, -c)
c2 = Counter(a=3, b=1)
c3 = Counter(a=1, b=2, d=4)
print(c2 + c3, c2 - c3, c2 | c3, c2 & c3, Counter(a=1) == Counter(a=1, b=0), Counter(a=1) <= Counter(a=2), Counter(a=2) < Counter(a=2))
words = 'the quick the lazy the end quick'.split()
print(Counter(words).most_common(), Counter(), Counter({'x': 2}), Counter(cats=4, dogs=8))
c4 = Counter(a=2)
c4 += Counter(a=1, b=1)
del c4['zz']
print(c4, isinstance(c4, dict), dict(c4), sum(c4.values()), list(c4.items()))

base = {'color': 'red', 'user': 'guest'}
overrides = {'user': 'admin'}
cm = ChainMap(overrides, base)
print(cm['user'], cm['color'], len(cm), sorted(cm), 'color' in cm, cm.get('missing', 'none'), cm)
cm['new'] = 1
child = cm.new_child({'user': 'child'})
print(overrides, child['user'], child.parents['user'], list(child.maps[0]), dict(cm.items()) == {'color': 'red', 'user': 'admin', 'new': 1}, list(cm.keys()))
try:
    cm['nope']
except KeyError as e:
    print('KeyError', e)
print(collections.namedtuple is namedtuple, type(d).__name__, isinstance(d, deque), repr(deque))
