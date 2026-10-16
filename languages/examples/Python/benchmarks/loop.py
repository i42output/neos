total = 0
for i in range(3000000):
    total += (i * i) % 7
print(total)
