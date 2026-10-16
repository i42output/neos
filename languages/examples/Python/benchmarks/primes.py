def is_prime(n: int) -> bool:
    if n < 2:
        return False
    d = 2
    while d * d <= n:
        if n % d == 0:
            return False
        d += 1
    return True

count = 0
for n in range(200000):
    if is_prime(n):
        count += 1
print(count)
