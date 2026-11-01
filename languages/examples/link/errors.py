# Linking example (see errors.sh): a Python function that raises an exception, and Python handling JavaScript's (errors.js)

def checked_sqrt(x):
    if x < 0:
        raise ValueError(f"math domain error: {x}")
    return x ** 0.5
