import math
from math import sqrt, pi
import sys
print(math.sqrt(2), sqrt(16), pi, math.e, math.tau, math.inf, math.nan)
print(math.floor(2.7), math.ceil(2.1), math.trunc(-2.7), math.floor(-0.5), math.ceil(5))
print(math.factorial(10), math.gcd(12, 18), math.lcm(4, 6), math.isqrt(99), math.comb(10, 3), math.perm(5, 2), math.perm(4))
print(math.log(100, 10), math.log(math.e), math.log2(8), math.log10(1000), math.log1p(1e-10), math.exp(1), math.expm1(1e-10))
print(math.sin(1), math.cos(1), math.tan(1), math.asin(0.5), math.acos(0.5), math.atan(1), math.atan2(1, -1))
print(math.sinh(1), math.cosh(1), math.tanh(1), math.asinh(1), math.acosh(2), math.atanh(0.5))
print(math.degrees(math.pi), math.radians(180), math.hypot(3, 4), math.hypot(1, 2, 3), math.dist((0, 0), (3, 4)))
print(math.fsum([0.1] * 10), sum([0.1] * 10), math.prod([1, 2, 3, 4]), math.prod([1.5, 2]), math.sumprod([1, 2, 3], [4, 5, 6]), math.sumprod([0.1, 0.2], [0.3, 0.4]))
print(math.isclose(1.0, 1.0 + 1e-10), math.isclose(1.0, 1.1), math.isnan(math.nan), math.isinf(-math.inf), math.isfinite(1.0))
print(math.copysign(3, -0.0), math.fmod(7, 3), math.fmod(-7, 3), math.remainder(7, 3), math.modf(3.5), math.frexp(8.0), math.ldexp(0.5, 4))
print(math.gamma(5), math.gamma(0.5), math.lgamma(10), math.erf(0.5), math.erfc(0.5), math.cbrt(27), math.exp2(10), math.pow(2, 10))
print(math.nextafter(1.0, 2.0), math.ulp(1.0), math.gamma(-1.5), math.lgamma(-1.5), math.gamma(171.5), math.pow(-8, 1/3) if False else 0)
print(sys.maxsize, sys.byteorder, sys.platform)
for expr in ['math.sqrt(-1)', 'math.log(0)', 'math.exp(1000)', 'math.factorial(-1)', 'math.floor(math.inf)', 'math.gamma(0)', 'math.pow(0, -1)', 'math.isqrt(-1)']:
    try:
        eval(expr) if False else None
    except ValueError as e:
        print(e)
try:
    math.sqrt(-1)
except ValueError as e:
    print('sqrt', e)
try:
    math.exp(1000)
except OverflowError as e:
    print('exp', e)
try:
    math.floor(math.inf)
except OverflowError as e:
    print('floor', e)
try:
    math.factorial(-1)
except ValueError as e:
    print('fact', e)
