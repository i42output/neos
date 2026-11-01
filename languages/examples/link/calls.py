# Linking example (see calls.sh): Python functions that JavaScript (calls.js) calls

def add(a, b):
    return a + b

def describe(value):
    return f"{value!r} is a Python {type(value).__name__}"

def greet(name, greeting="Hello"):
    return f"{greeting}, {name}!"
