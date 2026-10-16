total = 0.0
sign = 1.0
for k in range(3000000):
    total += sign / (2.0 * k + 1.0)
    sign = -sign
print(4.0 * total)
