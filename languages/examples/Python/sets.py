# sets and frozensets (CPython's hash table: their order is CPython's (with PYTHONHASHSEED=0 for str))
a = {1, 2}
b = {1, 2, 3}
c = {3, 1, 2, 3, 'x', -1, 2 ** 10, (1, 'a'), 1.5}
x = 5
d = {x, 1, 2}
e = {1, 2, x, 3}
print(a, b, c, d, e, type(a))
print({i * i for i in range(-5, 6)}, {c for c in 'abracadabra' if c != 'a'})
print({(i, j) for i in range(3) for j in range(i)})
print({1, 9, 17, 25, 33, 41, 49, 57}, {8, 16, 24, 32, 40, 48, 56, 64, 72})
print({'apple', 'banana', 'cherry', 'date', 'elderberry', 'fig'}, {None, True, 0.0, 'x'})
print({1,
       2,   # two
       3,}, {-0.0, 0, False})
print(len({1, 1, 1}), 2 in {1, 2, 3}, 'z' not in {'a', 'b'}, {1, 2} | {3}, {1, 2, 3} - {2}, {1, 2} & {2, 3}, {1, 2} ^ {2, 3})
words = 'the quick brown fox jumps over the lazy dog the end'.split()
print(sorted({w for w in words}), len(set(words)), {len(w) for w in words})
s = set([3, 1, 2, 3, 'a', (1, 2), None, 2.5])
print(s, len(s), 3 in s, 4 in s, type(s), type(s) is set, isinstance(s, set))
f = frozenset(range(20))
print(f, hash(frozenset()), hash(frozenset([1, 2, 3])), hash(f))
print(set(), frozenset(), set('hello'), sorted(set('mississippi')))
a = set(range(0, 30, 2))
b = set(range(0, 30, 3))
print(a | b, a & b, a - b, a ^ b)
print(a.union(b, [100, 101]), a.intersection(b, range(10)), a.difference(b, [4]), a.symmetric_difference([1, 2, 3, 4]))
print(a <= b, a < a | b, a | b >= b, a == set(a), a != b, frozenset(a) == a, a.issubset(range(40)), a.issuperset([2, 4]), a.isdisjoint([1, 3]))
s = set()
for i in range(100):
    s.add(i * 7919 % 1000)
print(s)
for i in range(0, 100, 3):
    s.discard(i * 7919 % 1000)
print(s, len(s))
s.remove(7919 % 1000)
print(s.pop(), s.pop(), len(s))
try:
    s.remove(-1)
except KeyError as e:
    print('KeyError', e)
try:
    set().pop()
except KeyError as e:
    print('KeyError', e)
try:
    hash(set())
except TypeError as e:
    print(e)
try:
    set([[1]])
except TypeError as e:
    print(e)
t = set([1, 2, 3])
t |= set([4])
t &= set([1, 2, 4])
t -= set([1])
t ^= set([7, 2])
print(t, frozenset([1, 2]) in set([frozenset([1, 2])]), set([1, 2]) in set([frozenset([1, 2])]))
u = set(['x', 'yy', 'zzz', 'a', 'b', 'c', 'd', 'e', 'f', 'g'])
print(len(u), sorted(u))
d = {1: 'a', 2: 'b', 'k': 3}
print(set(d), set(d.keys()), set([1.0, 1, True]), set([0, False, 0.0]))
f2 = frozenset([1, 2])
print(f2 | set([3]), set([3]) | f2, f2.copy() is f2, frozenset(f2) is f2, set([5, 6]).copy())
print(list(set([10, 20, 30])), tuple(frozenset('ab')), sum(set([1, 2, 3])), max(set([4, 9, 2])))
v = set([1, 2, 3])
v.update([4, 5], (6,))
v.intersection_update(range(2, 10))
v.difference_update([3])
v.symmetric_difference_update([2, 100])
print(v, bool(set()), bool(v))
v.clear()
print(v)
s = {1, 2, 3}
for e in [lambda: s < 1, lambda: s | [1], lambda: {[1, 2]}, lambda: s.add(), lambda: frozenset([1]).add(2), lambda: set(1), lambda: set([1], [2]), lambda: s.pop(1), lambda: {{1}}]:
    try:
        e()
    except (TypeError, AttributeError) as ex:
        print(type(ex).__name__, ex)
it = iter(s)
next(it)
s.add(4)
try:
    next(it)
except RuntimeError as ex:
    print('RuntimeError', ex)
print(repr(frozenset({1, 2})), str({1}), {frozenset({1, 2}): 'a'}.get(frozenset([2, 1])))
print(hash(frozenset({1, 2, 3})) == hash(frozenset([3, 2, 1])), {1, 2} == frozenset([1, 2]), {1} != {1}, {1, 2} > {1}, {1, 2} >= {1, 2}, {1} < {1})
print(set(range(5)) == [0, 1, 2, 3, 4], [1] in [{1}], {(1, 2): 3} == {(1, 2): 3})
