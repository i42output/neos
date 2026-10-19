import operator
from operator import itemgetter, attrgetter, methodcaller
import heapq
import bisect
import copy

print(operator.add(2, 3), operator.sub(5, 7), operator.mul('ab', 3), operator.truediv(7, 2), operator.floordiv(7, 2), operator.mod(7, 3), operator.pow(2, 10))
print(operator.neg(5), operator.pos(-5), operator.abs(-3), operator.invert(5), operator.not_(0), operator.truth([1]), operator.and_(6, 3), operator.or_(6, 3), operator.xor(6, 3))
print(operator.lt(1, 2), operator.le(2, 2), operator.eq(1, 1.0), operator.ne(1, 2), operator.gt(1, 2), operator.ge(3, 2), operator.is_(None, None), operator.is_not(1, None))
print(operator.contains([1, 2], 2), operator.countOf([1, 2, 1], 1), operator.indexOf('abc', 'c'), operator.getitem({'a': 1}, 'a'), operator.concat([1], [2]), operator.index(7))
lst = [1, 2, 3]
operator.setitem(lst, 0, 'x')
operator.delitem(lst, 1)
print(lst, operator.iadd([1], [2]), operator.lshift(1, 4), operator.length_hint([1, 2, 3]), operator.__add__(1, 2), operator.call(max, 1, 5))
data = [('apple', 3, 0.5), ('banana', 1, 0.25), ('cherry', 2, 1.0)]
print(sorted(data, key=itemgetter(1)), list(map(itemgetter(0, 2), data)), itemgetter(1)('abc'), itemgetter(slice(1, 3))('abcd'), itemgetter('k')({'k': 9}))
class Person:
    def __init__(self, name, age):
        self.name = name
        self.age = age
        self.info = self
    def greet(self, greeting, punct='!'):
        return f'{greeting}, {self.name}{punct}'
people = [Person('Ann', 30), Person('Bob', 25)]
print([p.name for p in sorted(people, key=attrgetter('age'))], attrgetter('name', 'age')(people[0]), attrgetter('info.info.name')(people[1]))
print(methodcaller('greet', 'Hello')(people[0]), methodcaller('greet', 'Hi', punct='?')(people[1]), methodcaller('upper')('abc'))
print(itemgetter(1), attrgetter('a', 'b'), methodcaller('m', 1, k=2))
for bad in [lambda: operator.concat(1, 2), lambda: itemgetter(5)([1]), lambda: attrgetter(1), lambda: operator.indexOf([1], 5)]:
    try:
        bad()
    except Exception as e:
        print(type(e).__name__, e)

h = []
for x in [5, 1, 8, 3, 9, 2, 7]:
    heapq.heappush(h, x)
print(h, heapq.heappop(h), heapq.heappop(h), h)
data = [9, 4, 7, 1, 8, 2]
heapq.heapify(data)
print(data, heapq.heapreplace(data, 5), data, heapq.heappushpop(data, 0), heapq.heappushpop(data, 6), data)
print(heapq.nlargest(3, [5, 1, 8, 3, 9]), heapq.nsmallest(2, [5, 1, 8, 3, 9]), heapq.nlargest(2, ['bb', 'a', 'ccc'], key=len), list(heapq.merge([1, 4, 7], [2, 5], [3])))
tasks = []
heapq.heappush(tasks, (2, 'write'))
heapq.heappush(tasks, (1, 'plan'))
heapq.heappush(tasks, (3, 'test'))
print([heapq.heappop(tasks)[1] for _ in range(3)], list(heapq.merge('ace', 'bd', key=str.upper)))
try:
    heapq.heappop([])
except IndexError as e:
    print('IndexError', e)

a = [1, 2, 4, 4, 8]
print(bisect.bisect_left(a, 4), bisect.bisect_right(a, 4), bisect.bisect(a, 5), bisect.bisect_left(a, 0), bisect.bisect_right(a, 9), bisect.bisect_left(a, 4, 3), bisect.bisect_left(a, 4, 0, 2))
bisect.insort(a, 3)
bisect.insort_left(a, 4)
print(a)
records = [('a', 1), ('b', 3), ('c', 5)]
print(bisect.bisect_left(records, 3, key=itemgetter(1)))
bisect.insort(records, ('x', 4), key=itemgetter(1))
print(records)
def grade(score, breakpoints=[60, 70, 80, 90], grades='FDCBA'):
    return grades[bisect.bisect(breakpoints, score)]
print([grade(s) for s in [33, 99, 77, 70, 89, 90, 100]])
try:
    bisect.bisect([], 1, -1)
except ValueError as e:
    print('ValueError', e)

orig = {'list': [1, [2, 3]], 'tuple': (1, [2]), 'n': 5}
shallow = copy.copy(orig)
deep = copy.deepcopy(orig)
orig['list'][1].append(99)
print(shallow['list'], deep['list'], shallow['list'] is orig['list'], deep['tuple'] is orig['tuple'], deep['tuple'] == orig['tuple'])
cyclic = [1]
cyclic.append(cyclic)
dc = copy.deepcopy(cyclic)
print(dc[1] is dc, dc[0], copy.copy((1, 2)) is not None, copy.deepcopy((1, 'a')) == (1, 'a'))
class Point:
    def __init__(self, x, y):
        self.x = x
        self.y = y
        self.tags = ['t']
p = Point(1, 2)
p2 = copy.copy(p)
p3 = copy.deepcopy(p)
p.tags.append('u')
print(p2.x, p2.tags, p3.tags, p2 is not p, type(p3).__name__)
class Custom:
    def __init__(self):
        self.n = 1
    def __copy__(self):
        c = Custom()
        c.n = 'copied'
        return c
    def __deepcopy__(self, memo):
        c = Custom()
        c.n = 'deep'
        return c
print(copy.copy(Custom()).n, copy.deepcopy(Custom()).n, copy.deepcopy([Custom()])[0].n)
from collections import deque, OrderedDict
dq = deque([[1], [2]], maxlen=5)
dq2 = copy.deepcopy(dq)
dq[0].append(9)
print(dq2, copy.copy(dq), copy.deepcopy({1, 2}), copy.copy(OrderedDict(a=[1])))
class MyList(list):
    pass
ml = MyList([[1], 2])
ml.extra = 'e'
mc = copy.deepcopy(ml)
ml[0].append(5)
print(mc, type(mc).__name__, mc.extra, copy.copy(ml))
