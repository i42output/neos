# Linking example (see callbacks.sh): Python calling the JavaScript functions of callbacks.js with Python functions, and calling the
# JavaScript functions that they return

def square(n):
    return n * n

print("twice(square, 3) =", twice(square, 3))
print("twice(lambda s: s + '!', 'hey') =", twice(lambda s: s + "!", "hey"))

inc_then_square = compose(square, lambda n: n + 1)
print("compose(square, n + 1)(4) =", inc_then_square(4))

total = 0
def accumulate(i):
    global total
    total += i
each(10, accumulate)
print("sum of 1..10 =", total)
