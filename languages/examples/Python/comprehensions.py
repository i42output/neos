# Comprehensions and implicit line joining: run with CPython and with neos; the output must be identical
squares = [x * x for x in range(6)]
print(squares)
evens = [x for x in squares if x % 2 == 0]
print(evens)
pairs = [(a, b) for a in range(3) for b in "xy" if a != 1]
print(pairs)
matrix = [[1, 2, 3],
          [4, 5, 6]]   # implicit line joining
print(matrix)
print([[row[i] for row in matrix] for i in range(3)])
print(sum(v * 2 for v in [1, 2, 3]), max(len(w) for w in ["a", "abc", "ab"]))
def f(items):
    return [str(i) + "!" for i in items]
print(f([1, 2]))
words = {"a": 1,
         "b": 2}
print(sorted([k for k, v in words.items() if v > 0]))
