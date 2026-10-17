import cmath
import math
vals = [0j, 1+0j, -1+0j, 1j, -1j, 0.5+0.5j, -2.5+3.25j, 3-4j, 1e300+1e300j, 1e-310+1e-310j, complex(math.inf, 1), complex(-math.inf, -2), complex(1, math.inf), complex(math.nan, 0), complex(0.0, -0.0), complex(-0.0, 0.0), 2, -3.5]
names = ['acos', 'acosh', 'asin', 'asinh', 'atan', 'atanh', 'cos', 'cosh', 'exp', 'log', 'log10', 'sin', 'sinh', 'sqrt', 'tan', 'tanh']
for n in names:
    f = getattr(cmath, n)
    out = []
    for v in vals:
        try:
            out.append(repr(f(v)))
        except (ValueError, OverflowError) as e:
            out.append(type(e).__name__ + ':' + str(e))
    print(n, ' '.join(out))
for v in vals:
    try:
        print(cmath.phase(v), cmath.polar(v), cmath.isfinite(v), cmath.isnan(v), cmath.isinf(v))
    except (ValueError, OverflowError) as e:
        print(type(e).__name__, e)
print(cmath.rect(2, 0.5), cmath.rect(1, 0), cmath.rect(-1, -0.0), cmath.rect(math.inf, 1.0))
try:
    cmath.rect(1, math.inf)
except ValueError as e:
    print(e)
print(cmath.log(8, 2), cmath.log(1j, 1+1j), cmath.log10(100))
try:
    cmath.log(2, 1)
except ValueError as e:
    print('log base 1', e)
print(cmath.isclose(1+1j, 1+1.0000000001j), cmath.isclose(1, 1.1, rel_tol=0.2), cmath.isclose(0, 1e-10, abs_tol=1e-9))
print(cmath.pi, cmath.e, cmath.tau, cmath.inf, cmath.infj, cmath.nan, cmath.nanj)
for bad in ['a', None]:
    try:
        cmath.sqrt(bad)
    except TypeError as e:
        print(e)
try:
    cmath.sqrt()
except TypeError as e:
    print(e)
try:
    cmath.rect(1)
except TypeError as e:
    print(e)
try:
    cmath.sqrt(10 ** 400)
except OverflowError as e:
    print(e)
