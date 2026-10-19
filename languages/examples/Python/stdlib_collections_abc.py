from collections.abc import Iterable, Iterator, Sized, Container, Hashable, Callable, Sequence, MutableSequence, Mapping, MutableMapping, Set, Collection, Reversible, Generator, KeysView
import collections.abc
from collections import deque, OrderedDict

def gen():
    yield 1
values = [[1], (1,), 'ab', {'a': 1}, {1}, range(3), iter([1]), 5, 2.5, None, len, deque([1]), OrderedDict(), gen(), {}.keys()]
for cls in [Iterable, Iterator, Sized, Container, Hashable, Callable, Sequence, MutableSequence, Mapping, MutableMapping, Set, Collection, Reversible, Generator, KeysView]:
    print(cls.__name__, [isinstance(v, cls) for v in values])

class Registry(Mapping):
    def __init__(self, **items):
        self._items = dict(items)
    def __getitem__(self, key):
        return self._items[key]
    def __iter__(self):
        return iter(self._items)
    def __len__(self):
        return len(self._items)
r = Registry(a=1, b=2)
print(len(r), r['a'], 'b' in r, 'z' in r, r.get('z', 0), sorted(r.keys()), sorted(r.items()), sorted(r.values()), r == {'a': 1, 'b': 2}, isinstance(r, Mapping), isinstance(r, Sized))

class Store(MutableMapping):
    def __init__(self):
        self._d = {}
    def __getitem__(self, k):
        return self._d[k]
    def __setitem__(self, k, v):
        self._d[k] = v
    def __delitem__(self, k):
        del self._d[k]
    def __iter__(self):
        return iter(self._d)
    def __len__(self):
        return len(self._d)
s = Store()
s['x'] = 1
s.update({'y': 2}, z=3)
print(s.setdefault('w', 0), s.pop('x'), s.pop('nope', 'default'), sorted(s.items()), len(s))
s.clear()
print(len(s), isinstance(s, MutableMapping), isinstance(s, dict))

class Seq(Sequence):
    def __init__(self, n):
        self.n = n
    def __getitem__(self, i):
        if i >= self.n:
            raise IndexError(i)
        return i * i
    def __len__(self):
        return self.n
q = Seq(5)
print(list(q), 9 in q, 7 in q, q.index(9), q.count(4), list(reversed(q)), isinstance(q, Sequence), isinstance(q, Iterable))
print(issubclass(Seq, Sequence), issubclass(Registry, Mapping), collections.abc.Sized is Sized)
