def f(l=range(99_999+1), m=range(9_999+1)):
    cnt = 0
    for i in l:
        for j in m: cnt += 1
    return cnt

print(f())
