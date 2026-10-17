import numpy as np
def f(x):
    if x < 0:
        raise ValueError('negative: ' + str(x))
    return x * 2
try:
    print(f(2))
    print(f(-1))
    print('not reached')
except ValueError as e:
    print('caught', e, type(e).__name__, e.args)
try:
    np.arange(3) + np.arange(4)
except ValueError as e:
    print('numpy:', e)
try:
    [1, 2][5]
except (KeyError, IndexError) as e:
    print('tuple', type(e).__name__, e)
try:
    {}['k']
except LookupError as e:
    print('base', repr(e))
try:
    int('x')
except Exception:
    print('bare-ish')
try:
    x = 1
except ValueError:
    print('no')
else:
    print('else')
finally:
    print('finally')
def g():
    try:
        return 1
    finally:
        print('g finally')
print(g())
try:
    try:
        raise KeyError('inner')
    except KeyError:
        raise RuntimeError('outer')
except RuntimeError as e:
    print('re', e)
try:
    try:
        1 / 0
    except ZeroDivisionError:
        raise
except ZeroDivisionError as e:
    print('reraise', e)
for i in range(3):
    try:
        if i == 1:
            continue
        print('loop', i)
    finally:
        print('fin', i)
try:
    np.zeros(3).reshape(2, 2)
except ValueError as e:
    print(e)
try:
    assert 1 == 2, 'assertion text'
except AssertionError as e:
    print('assert', e)
with np.errstate(divide='ignore'):
    print(np.array([1.0]) / 0)
with np.printoptions(precision=2):
    print(np.array([1.23456]))
print(np.array([1.23456]))
