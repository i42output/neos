# generators: yield, send, throw, close, yield from, return values; lazy map, filter, zip and enumerate
def squares(n):
    for i in range(n):
        yield i * i
print(list(squares(5)), sum(squares(4)), max(squares(3)))
g = squares(3)
print(next(g), next(g), next(g), next(g, 'end'))
try:
    next(g)
except StopIteration:
    print('stopped')

def naturals():
    n = 0
    while True:
        yield n
        n += 1
it = naturals()
print([next(it) for _ in range(5)])

def lazy():
    print('start')
    yield 1
    print('middle')
    yield 2
    print('end')
for v in lazy():
    print('got', v)

def accumulator():
    total = 0
    while True:
        value = yield total
        if value is None:
            break
        total += value
acc = accumulator()
print(next(acc), acc.send(5), acc.send(10), acc.send(2))

def inner():
    yield 'a'
    received = yield 'b'
    return 'inner done ' + str(received)
def outer():
    result = yield from inner()
    yield result
    yield from [1, 2]
    yield from range(3, 5)
o = outer()
print(next(o), next(o), o.send('x'), list(o))

def with_return():
    yield 1
    return 42
w = with_return()
next(w)
try:
    next(w)
except StopIteration as e:
    print('returned', e.value)

def cleanup():
    try:
        yield 1
        yield 2
    finally:
        print('cleanup ran')
c = cleanup()
print(next(c))
c.close()
print('closed')

def catcher():
    while True:
        try:
            yield 'waiting'
        except ValueError as e:
            print('caught', e)
ct = catcher()
next(ct)
print(ct.throw(ValueError('boom')))

class Countdown:
    def __init__(self, start):
        self.start = start
    def __iter__(self):
        n = self.start
        while n > 0:
            yield n
            n -= 1
print(list(Countdown(4)), [x * 2 for x in Countdown(3)])

def fib():
    a, b = 0, 1
    while True:
        yield a
        a, b = b, a + b
def take(n, iterable):
    result = []
    for x in iterable:
        if len(result) >= n:
            break
        result.append(x)
    return result
print(take(10, fib()))

def make_gen(k):
    def gen():
        for i in range(k):
            yield i * k
    return gen
print(list(make_gen(3)()))

def pipeline(source):
    for x in source:
        if x % 2:
            yield x * 10
print(list(pipeline(squares(6))))
print(squares.__name__, type(squares(1)).__name__, repr(squares(1)).split(' at ')[0])

def gen_error():
    yield 1
    raise KeyError('bad')
ge = gen_error()
next(ge)
try:
    next(ge)
except KeyError as e:
    print('KeyError', e)
print(list(zip(squares(3), naturals())), dict(enumerate(squares(3))))
print(list(map(lambda a, b: a * b, naturals(), [3, 4, 5])), next(filter(lambda v: v > 10, naturals())))
print(list(zip([1, 2], "ab", strict=True)), list(enumerate("xy", start=5)))
try:
    list(zip([1, 2], [1], strict=True))
except ValueError as e:
    print(e)
