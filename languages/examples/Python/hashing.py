print(hash(1), hash(-1), hash(2**61), hash(2**61 - 1), hash(-2**63), hash(10**30), hash(-10**30))
print(hash(1.0), hash(0.5), hash(-2.5), hash(1e300), hash(float('inf')), hash(float('-inf')), hash(1e-300))
print(hash(True), hash(False), hash(None), hash(()), hash((1, 2)), hash((1, (2.5, None), "")), hash(1j), hash(2 - 3.5j))
print(hash(range(0)), hash(range(5)), hash(range(1, 2)), hash(range(1, 10, 3)))
print(hash("a") == hash("a"), id(None) == id(None), type(1) is int, type("") is str, type([]) is list, type(1.5) is not int)
class A: pass
class B:
    def __eq__(self, o): return True
class C:
    def __hash__(self): return 12345
a = A()
print(hash(a) == hash(a), hash(C()), isinstance(1, (int, float)))
for x in ([], {}, B()):
    try:
        hash(x)
    except TypeError as e:
        print(e)
