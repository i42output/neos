# Fibonacci: recursion, iteration, for/range and string building

def fib(n: int) -> int:
    if n < 2:
        return n
    return fib(n - 1) + fib(n - 2)

def fib_iterative(n: int) -> int:
    a = 0
    b = 1
    for i in range(n):
        t = a + b
        a = b
        b = t
    return a

for i in range(10):
    print("fib(" + str(i) + ") = " + str(fib(i)))

print(fib_iterative(50))
