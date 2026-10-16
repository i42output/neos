import numpy as np
for n in [5, 17, 64, 100, 257, 1000, 5000]:
    x = (np.arange(n) * 7919) % 13
    print(n, np.argsort(x)[:30], np.argsort(x)[-10:])
    for t in [np.int8, np.uint8, np.int16, np.uint16, np.int32, np.uint32, np.int64, np.uint64, np.float16, np.float32, np.float64, np.complex64]:
        y = x.astype(t)
        print(t.__name__, np.argsort(y)[:20], np.argsort(y, kind='stable')[:10], np.partition(y, n // 2)[:12], np.argpartition(y, n // 3)[:12], np.sort(y)[-5:])
f = (np.arange(300) * 0.37) % 1.3
f[::17] = np.nan
print(np.argsort(f)[:40], np.sort(f)[-20:], np.argpartition(f, 100)[:30])
print(np.argsort(np.arange(24).reshape(4, 6) % 5, axis=0), np.argsort(np.arange(24).reshape(4, 6) % 5, axis=1))
print(np.argsort(np.array([True, False] * 40))[:40])
