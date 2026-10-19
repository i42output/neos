# nested functions, closures (nonlocal, lambdas, default values), decorators
def make_adder(n):
    def add(x):
        return x + n
    return add
add3 = make_adder(3)
print(add3(4), make_adder(10)(5), add3.__name__, add3.__qualname__)

def counter():
    count = 0
    def increment():
        nonlocal count
        count += 1
        return count
    return increment
c = counter()
print(c(), c(), c())

def outer():
    x = 1
    def middle():
        def inner():
            return x * 100
        return inner()
    x = 2
    return middle()
print(outer())

def fib_memo():
    memo = {}
    def fib(n):
        if n < 2:
            return n
        if n not in memo:
            memo[n] = fib(n - 1) + fib(n - 2)
        return memo[n]
    return fib
print(fib_memo()(40))

def decorator(fn):
    def wrapper(*args, **kwargs):
        print('calling', fn.__name__, args, kwargs)
        return fn(*args, **kwargs)
    return wrapper

@decorator
def multiply(a, b=2):
    return a * b
print(multiply(3), multiply(3, b=5))

def repeat(times):
    def deco(fn):
        def wrapper(*args):
            return [fn(*args) for _ in range(times)]
        return wrapper
    return deco

@repeat(3)
def greet(name):
    return 'hi ' + name
print(greet('bob'))

make_mul = lambda n: lambda x: x * n
print(make_mul(4)(5))
fs = [lambda i=i: i * 10 for i in range(3)]
print([f() for f in fs])
gs = [lambda: i for i in range(3)]
print([g() for g in gs])

def scale(values, factor):
    return list(map(lambda v: v * factor, values))
print(scale([1, 2, 3], 7))

def sort_by(items, index):
    return sorted(items, key=lambda t: t[index])
print(sort_by([(1, 'b'), (2, 'a'), (0, 'c')], 1))

def accumulate():
    total = []
    def add(v):
        total.append(v)
        return sum(total)
    def reset():
        nonlocal total
        total = []
    return add, reset
add, reset = accumulate()
print(add(1), add(2), add(3))
reset()
print(add(10))

def defaults(a, b=[1, 2]):
    def show(c=a * 2, *rest, d=b, **kw):
        return a, c, rest, d, kw
    return show
print(defaults(5)(), defaults(5)(1, 2, 3, d=0, e=9))

def cells_in_loop():
    result = []
    for k in range(3):
        def get():
            return k
        result.append(get)
    return [r() for r in result]
print(cells_in_loop())

class Greeter:
    def __init__(self, greeting):
        self.greeting = greeting
    def make(self, suffix):
        def say(name):
            return self.greeting + ', ' + name + suffix
        return say
print(Greeter('hello').make('!')('ann'))

def recursive_outer(n):
    def fact(k):
        return 1 if k <= 1 else k * fact(k - 1)
    return fact(n)
print(recursive_outer(10))

def apply_twice(f):
    return lambda x: f(f(x))
print(apply_twice(lambda x: x + 3)(10))
print(repr(make_adder(1)).split(' at ')[0], make_adder.__qualname__)
def wraps(original):
    def apply(wrapper):
        wrapper.__name__ = original.__name__
        wrapper.__qualname__ = original.__qualname__
        wrapper.__wrapped__ = original
        return wrapper
    return apply

def logged(fn):
    @wraps(fn)
    def inner(*args):
        result = fn(*args)
        print(fn.__name__, args, '->', result)
        return result
    return inner

@logged
def square(x):
    return x * x
square(4)
print(square.__name__, square.__qualname__, square.__wrapped__.__name__)

class Shape:
    def __init__(self, sides):
        self.sides = sides
    def describe(self):
        def detail(prefix):
            return prefix + str(self.sides)
        return detail
    @staticmethod
    def make(n):
        return Shape(n)
print(Shape(3).describe()('sides='), Shape(3).describe().__qualname__, Shape.make(5).sides)

def a():
    x = 'a'
    def b():
        def c():
            nonlocal x
            x = x + 'c'
            return x
        return c()
    r = b()
    return r, x
print(a())

total = 0
def outer_global():
    def bump(n):
        global total
        total += n
    for i in range(4):
        bump(i)
    return total
print(outer_global(), total)

def make_counters(n):
    return [lambda k=k: k * k for k in range(n)], [(lambda: k) for k in range(n)]
sq, last = make_counters(4)
print([f() for f in sq], [f() for f in last])

def compose(*fns):
    def composed(x):
        for f in reversed(fns):
            x = f(x)
        return x
    return composed
inc = lambda v: v + 1
dbl = lambda v: v * 2
print(compose(inc, dbl)(5), compose(dbl, inc)(5))

def memoize(fn):
    cache = {}
    def lookup(n):
        if n in cache:
            return cache[n]
        value = fn(n)
        cache[n] = value
        return value
    lookup.cache = cache
    return lookup

@memoize
def slow(n):
    return n * 3
print(slow(2), slow(2), sorted(slow.cache.items()))

def counter_class():
    count = [0]
    class Counter:
        def hit(self):
            return 1
    def make():
        count[0] += 1
        return Counter()
    return make, count
mk, cnt = counter_class()
mk().hit(); mk()
print(cnt)

def deep(n):
    if n == 0:
        return lambda: 'bottom'
    inner = deep(n - 1)
    return lambda: str(n) + inner()
print(deep(3)())
