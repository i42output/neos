import itertools
from itertools import count, cycle, repeat, accumulate, batched, chain, compress, dropwhile, takewhile, filterfalse, groupby, islice, pairwise, starmap, tee, zip_longest, product, permutations, combinations, combinations_with_replacement
import operator

print(list(islice(count(), 5)), list(islice(count(10, 2.5), 3)), count(3), count(1.5, 0.5), next(count(-1, -1)))
print(list(islice(cycle('abc'), 7)), list(repeat('x', 3)), repeat(1, 2), repeat('z'), list(repeat(0, -1)))
print(list(accumulate([1, 2, 3, 4])), list(accumulate([1, 2, 3, 4], operator.mul)), list(accumulate([1, 2], initial=100)), list(accumulate([], initial=5)), list(accumulate([3, 1, 4], max)))
print(list(batched('abcdefg', 3)), list(batched(range(4), 2)))
try:
    list(batched('abc', 2, strict=True))
except ValueError as e:
    print('ValueError', e)
print(list(chain('ab', [1, 2], ())), list(chain.from_iterable(['xy', 'z'])), list(compress('ABCDEF', [1, 0, 1, 0, 1, 1])))
print(list(dropwhile(lambda x: x < 5, [1, 4, 6, 3, 8])), list(takewhile(lambda x: x < 5, [1, 4, 6, 3, 8])), list(filterfalse(lambda x: x % 2, range(10))), list(filterfalse(None, [0, 1, '', 'a'])))
print([(k, list(g)) for k, g in groupby('AAAABBBCCDAABBB')], [list(g) for k, g in groupby([1, 3, 2, 4, 6, 5], key=lambda x: x % 2)])
groups = groupby('aabbc')
k1, g1 = next(groups)
k2, g2 = next(groups)
print(k1, list(g1), k2, list(g2))
print(list(islice('ABCDEFG', 2)), list(islice('ABCDEFG', 2, 4)), list(islice('ABCDEFG', 2, None)), list(islice('ABCDEFG', 0, None, 2)), list(islice(range(10), 1, 8, 3)))
it = iter(range(10))
print(list(islice(it, 3)), next(it))
print(list(pairwise('ABCD')), list(pairwise([1])), list(starmap(pow, [(2, 5), (3, 2)])))
a, b = tee([1, 2, 3])
print(list(a), list(b), len(tee('x', 3)))
a, b = tee(iter(range(5)))
print(next(a), next(a), next(b), list(a), list(b))
print(list(zip_longest('ABCD', 'xy', fillvalue='-')), list(zip_longest([1, 2], [3])))
print(list(product('AB', 'xy')), list(product(range(2), repeat=3)), list(product()), len(list(product('ab', [], 'c'))))
print(list(permutations('ABC', 2)), list(permutations(range(3))), list(permutations([1, 2], 3)), list(permutations([], 0)))
print(list(combinations('ABCD', 2)), list(combinations(range(4), 3)), list(combinations('AB', 3)), list(combinations('AB', 0)))
print(list(combinations_with_replacement('ABC', 2)), list(combinations_with_replacement([], 1)))
for bad in [lambda: count('a'), lambda: islice('abc', -1), lambda: list(batched('a', 0)), lambda: combinations('ab', -1), lambda: product(repeat=-1)]:
    try:
        bad()
    except Exception as e:
        print(type(e).__name__, e)
print(type(count()).__name__, isinstance(chain(), chain), isinstance([], chain), sum(islice(count(1), 100)))
