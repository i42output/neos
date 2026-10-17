import numpy as np

# vectorised arithmetic and reductions on large arrays
n = 1000000
a = np.arange(n, dtype=np.float64)
b = np.sqrt(a) * 0.5 + 1.0
total = 0.0
for k in range(20):
    c = a * b - k
    total += float(c.sum())
print(total)

# integer arrays: modulo, sorting, unique values
x = (np.arange(n) * 7919) % 1000003
s = np.sort(x)
print(int(s[0]), int(s[n // 2]), int(s[n - 1]), int(np.argsort(x)[0]))
print(len(np.unique(x % 1000)))

# matrix product
m = (np.arange(40000, dtype=np.float64).reshape(200, 200) % 17) / 17.0
p = m @ m
print(round(float(p.trace()), 6))

# element-wise access from Python (interpreter / NumPy boundary)
v = np.arange(100000)
acc = 0
for i in range(100000):
    acc += int(v[i]) % 3
print(acc)
