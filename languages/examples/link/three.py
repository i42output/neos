# Linking example (see three.sh): Python functions that JavaScript (three.js) calls; they call Lua's (three.lua)

def fib_table(n):
    return ", ".join(str(fib(i)) for i in range(n))

def banner(text):
    line = "-" * (len(text) + 4)
    return f"{line}\n| {text} |\n{line}"
