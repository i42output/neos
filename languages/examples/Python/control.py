# Python semantics with objects: run with CPython and with neos; the output must be identical
# functions, globals, control flow with objects
scale = 3
items = [5, 1, 4]

def scaled(v):
    return v * scale

def total(xs):
    t = 0
    for x in xs:
        t += x
    return t

def first_even(xs):
    for x in xs:
        if x % 2 == 0:
            return x
    return None

def describe(x):
    if x is None:
        return "none"
    elif isinstance(x, int):
        return "int " + str(x)
    return "other"

print(scaled(2), scaled(items))
print(total(items), total([1.5, 2.5]))
print(first_even(items), first_even([1, 3]))
print(describe(None), describe(4), describe("s"))

# break / continue
n = 0
while True:
    n += 1
    if n % 2 == 0:
        continue
    if n > 7:
        break
    print("odd", n)
for i in range(10):
    if i == 3:
        continue
    if i == 6:
        break
    print("i", i)
for w in ["a", "b", "c", "d"]:
    if w == "c":
        break
    print(w)
else_count = 0
for row in [[1, 2], [3, 4], [5, 6]]:
    for v in row:
        if v == 4:
            break
        else_count += v
print(else_count)

# swaps and unpacking
a, b = 1, 2
a, b = b, a
print(a, b)
x, y, z = [7, 8, 9]
print(x, y, z)
p, q = "hi"
print(p, q)
fa, fb = 0, 1
for k in range(10):
    fa, fb = fb, fa + fb
print(fa)

# augmented assignment on items and attributes
counts = {"a": 0}
counts["a"] += 5
counts["b"] = 1
print(counts)
lst = [1, 2, 3]
lst[1] *= 10
lst += [4]
print(lst)

# print with keywords
print(1, 2, 3, sep="-")
print("no newline", end="")
print(" then", "more", sep=", ")

# strings and slices
s = "Hello, World"
print(s.upper(), s.lower(), s[0:5], s[-5:], s[::2])
print("World" in s, "x" not in s)
print(len(s), s.find("o"), s.replace("l", "L"))
print(", ".join(["a", "b", "c"]))
print([1, 2, 3, 4, 5][1:4], (1, 2, 3)[::-1])

# mixed kinds
v = 1
v = "one"
print(v)
m = 2
m = m / 4
print(m)
print(abs(-3), abs(-2.5), round(2.675, 2), round(7.5), divmod(-7, 2))
print(max([3, 9, 2], key=None), min("bca"), sorted("bca"))
print(list(zip([1, 2], ["a", "b"])), list(enumerate("ab", 1)))
print(any([0, 0, 1]), all([1, 0]), sum([0.1] * 10))
print(int("42") + 1, float("2.5") * 2, str(3.0), repr("q"), bool([]), bool([0]))
print(type(1.5), type("s"), type(None), type([]) == list)
print(1 < 2 < 3, 3 < 2 < 4)
