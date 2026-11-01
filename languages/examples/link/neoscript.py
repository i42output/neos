# Linking example (see neoscript.sh): Python functions that neoscript (neoscript.neo) calls; Python calling neoscript's functions

def shout(s):
    return s.upper() + "!"

def square(x):
    return x * x

def report():
    print("neoscript.py: add(2, 3) =", add(2, 3), "hypot2(3, 4) =", hypot2(3, 4), "label:", label("python"))
    try:
        add("two", 3)
    except TypeError as e:
        print("neoscript.py: caught:", e)
