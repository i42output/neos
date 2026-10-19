import random

random.seed(42)
print(random.random(), random.random(), random.randint(1, 100), random.randrange(10), random.randrange(5, 50, 5), random.randrange(10, 0, -3))
print(random.choice('abcdef'), random.choice([1, 2, 3]), random.getrandbits(8), random.getrandbits(40), random.getrandbits(100), random.randbytes(5))
items = list(range(10))
random.shuffle(items)
print(items, random.sample(range(100), 5), random.sample('abcdefgh', 3), random.sample(range(1000), 30)[:5], random.sample(['a', 'b'], k=2, counts=[2, 3]))
print(random.choices('abc', k=5), random.choices([1, 2, 3], weights=[10, 1, 1], k=6), random.choices('xy', cum_weights=[1, 10], k=4))
print(random.uniform(1, 10), random.triangular(0, 10, 3), random.gauss(0, 1), random.gauss(5, 2), random.normalvariate(), random.lognormvariate(0, 1))
print(random.expovariate(2.0), random.vonmisesvariate(1.0, 4.0), random.gammavariate(2.0, 3.0), random.gammavariate(0.5, 1.0), random.betavariate(2, 5), random.paretovariate(3), random.weibullvariate(1, 2))
print(random.binomialvariate(10, 0.3), random.binomialvariate(100, 0.4), random.binomialvariate(50, 0.9), random.binomialvariate(1, 0.5))
for s in [0, 1, -5, 2**40, 2**100 + 7, 'hello', b'bytes', 3.75]:
    random.seed(s)
    print(repr(s), random.random(), random.randint(0, 10**6))
random.seed('hello', version=1)
print(random.random())
r = random.Random(123)
r2 = random.Random(123)
print(r.random() == r2.random(), r.randint(1, 6), isinstance(r, random.Random), r.choice(range(1000)))
state = r.getstate()
a = [r.random() for _ in range(3)]
r.setstate(state)
b = [r.random() for _ in range(3)]
print(a == b, len(state[1]), state[0])
random.seed(7)
print(random.randrange(2**70), random.randint(-2**65, 2**65), random.randrange(0, 10**30, 7))
for bad in [lambda: random.randrange(0), lambda: random.randint(5, 1), lambda: random.choice([]), lambda: random.sample([1, 2], 3), lambda: random.randrange(1.5),
            lambda: random.randrange(0, 10, 0), lambda: random.sample({1, 2}, 1), lambda: random.choices([1], [1], [1])]:
    try:
        bad()
    except (ValueError, IndexError, TypeError) as e:
        print(type(e).__name__, e)
