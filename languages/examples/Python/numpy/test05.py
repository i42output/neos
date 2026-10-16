import numpy as np
a = np.arange(24).reshape(2, 3, 4)
print(a[1, 2, 3], a[-1, -1], a[0, :, 1])
print(a[:, 1:, ::2])
print(a[..., 0])
print(a[:, np.newaxis, 0].shape)
print(a[1][2])
b = np.arange(10)
print(b[2:8:2], b[::-1], b[-3:], b[:0])
print(b[[1, 3, 5]], b[np.array([0, -1])])
print(b[b % 3 == 0])
c = np.arange(12).reshape(3, 4)
print(c[[0, 2]], c[[0, 1], [1, 2]], c[:, [0, 3]])
print(c[c > 5])
print(c[np.array([True, False, True])])
print(c[1:, [1, 2]])
b[3] = 100
print(b)
b[5:8] = -1
print(b)
b[b < 0] = 0
print(b)
c[0] = 9
print(c)
c[:, 1] = np.array([7, 8, 9])
print(c)
c[[0, 2], [0, 0]] = 55
print(c)
f = np.zeros(4)
f[1] = 2.7
f[2:] = 1
print(f)
f += 1
print(f)
f *= 2
print(f)
i = np.arange(5)
i += 2
print(i)
i[i > 4] *= 10
print(i)
v = np.arange(6)
w = v[1:4]
w[0] = 99
print(v)
x = v.reshape(2, 3)
x[1, 1] = -5
print(v)
print(len(c), c.shape[0])
for row in np.arange(6).reshape(3, 2):
    print(row)
for x in np.array([1.5, 2.5]):
    print(x)
print(3 in np.arange(5), 7 in np.arange(5))
print(np.arange(5)[np.int64(2)])
print(c.item(5), c.item(1, 2), np.float64(2.5).item())
print(c.tolist())
print(np.array([1.5, 2.0]).tolist())
print(float(np.array(2.5)), int(np.array(7)))
print(np.take(np.array([10, 20, 30]), [2, 0]))
c.fill(3)
print(c)
