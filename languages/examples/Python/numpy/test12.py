import numpy as np
print(np.argsort(np.array([1, 1, 1, 0, 1, 0, 2, 1])), np.argsort(np.array([1., 1, 1, 0, 1, 0, 2, 1])), np.argsort(np.array([3, 1, 2] * 20))[:20])
print(np.argsort(np.array([3, 1, 2] * 100, dtype=np.int32))[:30], np.argsort(np.array([3, 1, 2] * 100, dtype=np.float32))[-30:])
print(np.argsort(np.array([3, 1, 2] * 100, dtype=np.int8))[:30], np.argsort(np.array([3, 1, 2] * 100, dtype=np.int16))[:30], np.argsort(np.array([True, False] * 20)))
print(np.partition(np.array([3, 4, 2, 1, 9, 8, 7, 6, 5]), 3), np.argpartition(np.array([3, 4, 2, 1, 9, 8, 7, 6, 5]), 3))
x = np.arange(100) * 37 % 101
print(np.partition(x, 50), np.argpartition(x, 50), np.partition(x, [10, 50, 90]), np.argpartition(x, (5, 7)))
print(np.partition(x.astype(np.int8), 20), np.argpartition(x.astype(np.int16), 20), np.partition(x.astype(np.float16), 30))
print(np.partition(x.astype(complex), 30)[:40], np.argpartition(x.astype(np.uint8), 30)[:40])
print(np.sort(np.array([0.0, -0.0, 0.0, -0.0, 1.0, -0.0])), np.sort(np.array([np.nan, 1.0, -np.inf, 3.0, np.nan, 0.0])))
print(np.argsort(np.array([2, 1, 2, 1, 2]), kind='stable'), np.argsort(np.array([2, 1, 2, 1, 2] * 5), kind='heapsort'), np.sort(np.array([5, 3, 1]), kind='mergesort'))
a = np.array([[3, 2, 1], [6, 5, 4]])
a.partition(1)
print(a, np.argsort(a, axis=0), np.sort(a, axis=None))
y = np.arange(1000) * 7919 % 1009
print(np.argsort(y)[:20], np.argsort(y.astype(np.float64))[500:520], np.argsort(y % 10)[:40])
print(np.median(y), np.percentile(y, [10, 90]))
