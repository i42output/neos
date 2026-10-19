import functools
from functools import reduce, partial, lru_cache, cache, wraps, total_ordering, cmp_to_key, cached_property, singledispatch
import operator

print(reduce(operator.add, [1, 2, 3, 4]), reduce(lambda a, b: a * b, range(1, 6), 10), reduce(max, [3, 9, 2]))
try:
    reduce(operator.add, [])
except TypeError as e:
    print('TypeError', e)

def power(base, exponent, scale=1):
    return scale * base ** exponent
square = partial(power, exponent=2)
cube = partial(power, exponent=3, scale=10)
print(square(5), cube(2), square.func is power, square.args, square.keywords, partial(partial(power, 2), 3)(), partial(power, 2, 3).args)
print(repr(partial(max, 1, key=abs)).startswith('functools.partial(<built-in function max>, 1, key='), callable(square), isinstance(square, partial))

calls = []
@lru_cache(maxsize=2)
def slow(n):
    calls.append(n)
    return n * n
print(slow(2), slow(3), slow(2), slow(4), slow(3), calls, slow.cache_info())
slow.cache_clear()
print(slow.cache_info(), slow.__name__, slow.__wrapped__.__name__)

@lru_cache
def fib(n):
    return n if n < 2 else fib(n - 1) + fib(n - 2)
print(fib(60), fib.cache_info())

@cache
def factorial(n):
    return n * factorial(n - 1) if n else 1
print(factorial(20), factorial.cache_info().hits, factorial.cache_info().misses)

class Calc:
    def __init__(self, k):
        self.k = k
    @lru_cache(maxsize=None)
    def times(self, x):
        return self.k * x
c = Calc(3)
print(c.times(4), c.times(4), Calc.times.cache_info())

def logged(func):
    @wraps(func)
    def wrapper(*args, **kwargs):
        """wrapper doc"""
        return func(*args, **kwargs)
    return wrapper

@logged
def greet(name):
    """Greet someone."""
    return 'hi ' + name
print(greet('bob'), greet.__name__, greet.__doc__, greet.__wrapped__('x'))

@total_ordering
class Version:
    def __init__(self, major, minor):
        self.major, self.minor = major, minor
    def __eq__(self, other):
        return (self.major, self.minor) == (other.major, other.minor)
    def __lt__(self, other):
        return (self.major, self.minor) < (other.major, other.minor)
    def __repr__(self):
        return f'V{self.major}.{self.minor}'
v1, v2 = Version(1, 2), Version(1, 10)
print(v1 < v2, v1 > v2, v1 <= v2, v1 >= v2, v1 <= Version(1, 2), sorted([v2, v1, Version(0, 9)]), max(v1, v2))
try:
    @total_ordering
    class Nothing:
        pass
except ValueError as e:
    print('ValueError', e)

def compare_len(a, b):
    return len(a) - len(b)
print(sorted(['ccc', 'a', 'bb'], key=cmp_to_key(compare_len)), sorted([3, 1, 2], key=cmp_to_key(lambda a, b: b - a)))

class Circle:
    def __init__(self, r):
        self.r = r
        self.computed = 0
    @cached_property
    def area(self):
        self.computed += 1
        return 3 * self.r * self.r
circle = Circle(2)
print(circle.area, circle.area, circle.computed, 'area' in circle.__dict__)

@singledispatch
def describe(x):
    return 'object ' + repr(x)
@describe.register(int)
def _(x):
    return 'int ' + str(x)
@describe.register(list)
def _(x):
    return 'list of ' + str(len(x))
describe.register(str, lambda x: 'str ' + x)
class Base:
    pass
class Derived(Base):
    pass
describe.register(Base, lambda x: 'a Base')
print(describe(5), describe(True), describe([1, 2]), describe('s'), describe(2.5), describe(Derived()), describe.dispatch(bool) is describe.dispatch(int))
