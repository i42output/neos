# Primes, the Collatz conjecture and Python's arithmetic

def is_prime(n: int) -> bool:
    if n < 2:
        return False
    d = 2
    while d * d <= n:
        if n % d == 0:
            return False
        d += 1
    return True

def collatz_steps(n: int) -> int:
    steps = 0
    while n != 1:
        if n % 2 == 0:
            n = n // 2
        else:
            n = 3 * n + 1
        steps += 1
    return steps

def describe(n: int) -> str:
    if n < 0:
        return "negative"
    elif n == 0:
        return "zero"
    elif n < 10:
        return "small"
    else:
        return "large"

def mean(total: float, count: int) -> float:
    return total / count

count = 0
for n in range(2, 50):
    if is_prime(n):
        count += 1
print("primes below 50: " + str(count))
print("collatz(27) takes " + str(collatz_steps(27)) + " steps")
print(describe(-3) + " " + describe(0) + " " + describe(7) + " " + describe(100))
print(mean(10.0, 4))

a = -7
print(a // 2)
print(a % 3)
print(a / 2)
print(-7 // 2)

flag = is_prime(97) and not is_prime(91)
print(flag)

total = 0
for k in range(10, 0, -2):
    total += k
print(total)
