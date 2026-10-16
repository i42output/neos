# Python semantics test: run with CPython and with neos; the output must be identical

def twice(n: int) -> int:
    return helper(n) * 2  # helper is defined later

def helper(n: int) -> int:
    ...  # an ellipsis that is not the entire body is an empty statement
    return n + 1

def sign(x: int) -> int:
    if(x > 0):
        return(1)
    elif(x < 0):
        return -1
    return(0)

def floordiv(a: float, b: float) -> float:
    return a // b

def modulo(a: float, b: float) -> float:
    return a % b

# for loops: the range is evaluated once; the variable keeps its last value
for i in range(5):
    if i == 2:
        i = 100
    print(i)
print(i)
n = 3
for n in range(0, n): print(n)
print(n)
for k in range(10, 0, -3):
    pass
print(k)
for k in range(0, 0):
    print(k)
print(k)

# calls and temporaries in top level statements
x = twice(1) + twice(2) // 3
print(x)
print(sign(-5))
print(sign(0))

# integer and float floor division / modulo
print(7 // -2)
print(-7 % 3)
print(1.0 // 0.1)
print(1.0 % 0.1)
print(floordiv(1.0, 0.1))
print(modulo(1.0, 0.1))
print(modulo(6.0, -3.0))
print(floordiv(-7.5, 2.0))
print(modulo(-7.5, 2.0))
print(7 / 2)

# float formatting
print(1e16)
print(1.5e-5)
print(0.0001)
print(123456789.125)
print(-0.0)
print(1e22 / 3.0)
print(0.1 + 0.2)
print(0.0 * -1.0)
print(1e300 * 1e300)
print(5e-324)
print(4503599627370497.5)

# strings
empty = ""
other = ''
print(empty + other + "x")
print(len(empty))
print(len("héllo"))
print(int(" 42 ") + int("-1_000"))
print(float("  2.5 "))

q = 7
q //= 2
print(q)
print(not(q > 5))

# a name that shadows a builtin function is a variable
len = 3
print(len)
