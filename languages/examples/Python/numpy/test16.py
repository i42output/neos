import numpy as np
# typed loops: contiguous, strided, scalars of other types, in place
a = np.arange(40, dtype=np.float64) * 0.75 - 9.0
b = np.arange(40, dtype=np.int32) % 7 - 3
print(a * 2, a - 3, 3 - a, a[:39:3] + a[1::3], np.sqrt(np.abs(a))[::5])
print(b * 3, b // 2, b % 3, -b, np.abs(b), b + np.int8(5), np.int16(7) - b)
c = a.copy()
c += 1.5
c[::2] *= -1
print(c)
d = np.arange(12, dtype=np.uint8)
d -= 3
print(d, d * d, d >> 1)
f = np.array([1.0, np.nan, -0.0, 0.0, 3.0, -np.inf, 2.0], dtype=np.float32)
g = np.array([0.0, 1.0, np.nan, 0.5, np.inf, 5.0, -2.0], dtype=np.float32)
print(np.maximum(f, g), np.minimum(f, g), np.fmax(f, g), np.fmin(f, g))
# reductions: in order, NaN and signed zeros
for v in [np.arange(100.0) % 17 - 8, np.array([-0.0] * 20 + [0.0] * 20), np.array([0.0] * 20 + [-0.0] * 20), np.where(np.arange(50) == 31, np.nan, np.arange(50.0)),
          np.full(30, np.nan), np.array([-np.inf] * 25), (np.arange(33.0) - 40) * -0.0]:
    print(np.max(v), np.min(v), np.fmax.reduce(v + 1.0), np.fmin.reduce(v + 1.0), np.signbit(np.max(v)), np.signbit(np.min(v)), np.prod(v[:5]), v.sum())
x = (np.arange(1000) * 7919) % 1009 - 500
print(x.sum(), x.max(), x.min(), x.prod(), np.bitwise_xor.reduce(x), x.astype(np.int8).sum(), x.astype(np.uint16).max(), x.reshape(10, 100).sum(axis=0)[:8], x.reshape(10, 100).max(axis=1))
print(np.add.reduce(x[::7]), np.maximum.reduce(x[::-3]), np.logical_or.reduce(x > 400), np.logical_and.reduce(x > -501))
# unique, sort lanes
print(np.unique(x % 37), np.unique(np.array([True, False, True])), np.unique(np.array([], dtype=np.int64)), np.unique(x.reshape(20, 50)[:, ::3] % 11))
y = x.reshape(25, 40)
print(np.sort(y, axis=0)[:2], np.sort(y, axis=1)[:2, :10], np.argsort(y, axis=0)[:2], np.argsort(y[:, ::2], axis=1)[:2, :10], np.sort(y.T)[:2, :8])
print(np.partition(x, 500)[500], np.argpartition(y, 10, axis=1)[:2, 10], np.sort(np.arange(10.0)[::-1]), np.argsort(np.array([3, 1, 2]))[::-1])
# matmul: types, strides, batches
m = (np.arange(30, dtype=np.float64).reshape(5, 6) % 7) / 7.0
n = (np.arange(42, dtype=np.float64).reshape(6, 7) % 5) - 2.0
print(m @ n, m[:, ::2] @ n[::2], m.T.T @ n, (m.astype(np.float32) @ n.astype(np.float32))[0], (m.astype(np.int64) @ n.astype(np.int64))[1])
p = np.arange(2 * 3 * 4, dtype=np.int32).reshape(2, 3, 4)
print(p @ p.transpose(0, 2, 1), np.ones((3, 0)) @ np.ones((0, 2)), (m.astype(np.complex128) @ (n * 1j))[0, :3])
print(np.arange(0.5, 7.0, 0.75), np.arange(3, 30, 4, dtype=np.int16), np.arange(5, dtype=np.float32), np.arange(0))
