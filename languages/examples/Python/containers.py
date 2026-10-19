d = {'a': 1, 'b': 2, 'c': 3}
print(d.keys(), d.values(), d.items(), len(d.keys()))
k = d.keys()
d['d'] = 4
print(k, 'd' in k, ('a', 1) in d.items(), ('a', 2) in d.items(), 4 in d.values())
print(d.keys() == {'a', 'b', 'c', 'd'}, d.keys() & {'a', 'z'}, sorted(d.keys() | {'z'}), sorted(d.keys() - {'a'}), d.items() == {('a', 1), ('b', 2), ('c', 3), ('d', 4)})
print(d.pop('a'), d.pop('zz', None), d, d.popitem(), d)
try:
    d.pop('zz')
except KeyError as e:
    print('KeyError', e)
print(d.setdefault('b', 9), d.setdefault('x', 10), d.setdefault('y'), d)
d.update({'b': 20}, z=26)
d.update([('p', 1), ('q', 2)])
print(d)
d2 = d.copy()
d.clear()
print(d, d2, dict.fromkeys('abc'), dict.fromkeys([1, 2], 0))
try:
    {}.popitem()
except KeyError as e:
    print('KeyError', e)
try:
    d.update([(1, 2, 3)])
except ValueError as e:
    print('ValueError', e)
try:
    d.update([1])
except TypeError as e:
    print('TypeError', e)
try:
    for key in d2:
        d2['new'] = 1
except RuntimeError as e:
    print('RuntimeError', e)
for key, value in {'x': 1, 'y': 2}.items():
    print(key, value, end='; ')
print()
print(list({'x': 1}.items()), list(reversed([1, 2, 3])), sorted({'b': 1, 'a': 2}.items(), key=lambda kv: kv[1]))
l = [3, 1, 2, 1]
l.remove(1)
print(l, l.index(2), [1, 2, 3, 2].index(2, 2), (1, 2, 3).index(3))
l.sort(key=lambda x: -x)
print(l)
l.sort(reverse=True)
print(l)
words = ['banana', 'Apple', 'cherry']
words.sort(key=str.lower)
print(words)
l.clear()
print(l)
for bad in [lambda: [1].remove(5), lambda: [1].index(5), lambda: (1,).index(5), lambda: [1].sort(1), lambda: list.append(1, 2)]:
    try:
        bad()
    except Exception as e:
        print(type(e).__name__, e)
x = []
list.append(x, 5)
print(x, dict.get({'a': 1}, 'a'), list(map({'a': 1, 'b': 2}.__getitem__, 'ab')))
