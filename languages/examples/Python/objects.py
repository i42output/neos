# Python objects (lists, tuples, dicts, None, ...): run with CPython and with neos; the output must be identical
a = [1, 2, 3]
print(a)
a.append(4)
print(a, len(a))
b = (1, "x", 2.5)
print(b)
d = {"k": 1, "j": [1, 2]}
print(d)
print(d["j"])
a[0] = 10
print(a[0], a[-1], a[1:3])
x = None
print(x)
print(x is None)
s = "a,b,c".split(",")
print(s)
for item in a:
    print(item)
for k, v in d.items():
    print(k, v)
t = 0
for i in range(len(a)):
    t += a[i]
print(t)
print(sum(a), min(a), max(a))
print(sorted([3, 1, 2], reverse=True))
print(list(range(5)))
print(isinstance(a, list), type(a))
def f(n):
    return [n, n * 2]
print(f(3))
def g(lst):
    total = 0
    for v in lst:
        total = total + v
    return total
print(g([1, 2, 3]))
y = 1 if t > 5 else 2
print(y)
z = a or []
print(z)
e = []
print(e or "empty")
q, r = divmod(17, 5)
print(q, r)
