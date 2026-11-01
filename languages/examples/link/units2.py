# Linking example (see units.sh): a second Python translation unit (a program of its own, with globals of its own): it uses units.py's 
# functions (bump changes units.py's count) and units.js's

def cube(x):
    return square(x) * x

print("units2.py: cube(4) =", cube(4), shout("from python"), "bump() =", bump(), bump())
