def steps(n: int) -> int:
    s = 0
    while n != 1:
        if n % 2 == 0:
            n = n // 2
        else:
            n = 3 * n + 1
        s += 1
    return s

best = 0
arg = 0
for i in range(1, 300000):
    k = steps(i)
    if k > best:
        best = k
        arg = i
print(arg)
print(best)
