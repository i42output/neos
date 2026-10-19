import numpy as np
a = np.arange(6).reshape(2, 3).T
print(a.flat[3], a.flat[1:4], a.flat[[0, 5]], len(a.flat), list(a.flat))
f = a.flat
print(next(f), next(f), f.index, f.coords)
b = np.zeros((2, 3)).T
b.flat[::2] = [1, 2]
print(b)
b.flat = 7
print(b, a.flat.base is a, a.flat.copy())
for v in np.arange(3).flat:
    print(v)
print([int(x) for x in np.nditer(a)])
it = np.nditer(a, flags=['multi_index'])
while not it.finished:
    print(it[0], it.multi_index)
    it.iternext()
for x, y in np.nditer([np.arange(3), np.arange(3).reshape(3, 1)]):
    print(x, y)
print([x for x in np.nditer(np.arange(6).reshape(2, 3), flags=['external_loop'])], [x for x in np.nditer(np.arange(6).reshape(2, 3), flags=['external_loop'], order='F')])
print([x.shape for x in np.nditer(np.arange(6).reshape(2, 3)[:, ::2], flags=['external_loop'])])
print([int(x) for x in np.nditer(np.arange(3)[::-1])])
it = np.nditer(np.arange(3)[::-1], flags=['multi_index'])
while not it.finished:
    print(it.multi_index, int(it[0]))
    it.iternext()
it = np.nditer(np.arange(6).reshape(2, 3), flags=['c_index', 'multi_index'], order='F')
while not it.finished:
    print(it.index, it.multi_index)
    it.iternext()
x = np.nditer(np.arange(4.0))
print(next(x), repr(next(x)), x.itersize, x.ndim, x.nop, x.shape, x.iterindex, x.finished)
print(np.nditer(np.arange(3)).operands, repr(np.nditer(np.arange(3)).value))
c = np.arange(4)
for v in np.nditer(c, op_flags=['readwrite']):
    v[...] = v * 2
print(c)
print([int(v) for v in np.nditer(np.arange(24).reshape(2, 3, 4).transpose(2, 0, 1))])
print([int(v) for v in np.nditer(np.arange(24).reshape(2, 3, 4).transpose(1, 2, 0), order='C')])
bb = np.broadcast(np.arange(3), np.arange(2).reshape(2, 1))
print(bb.shape, bb.nd, bb.ndim, bb.size, bb.numiter, list(bb))
print(np.ufunc, isinstance(np.add, np.ufunc), isinstance(np.sum, np.ufunc), np.astype(np.arange(3), np.float32), np.mask_indices(3, np.triu))
print(np.mask_indices(3, np.triu, 1))
print(np.geterrcall(), np.setbufsize(4096), np.getbufsize(), np.setbufsize(8192))
