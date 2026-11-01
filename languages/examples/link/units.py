# Linking example (see units.sh): the first Python translation unit; units2.py, a second, uses its functions

import sys

def square(x):
    return x * x

count = 0

def bump():
    global count
    count += 1
    return count

print("units.py: square(7) =", square(7), "arguments:", sys.argv[1:])
