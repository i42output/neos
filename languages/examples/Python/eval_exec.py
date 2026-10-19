# eval(), exec() and compile(): run with CPython and with neos; the output must be identical

# --- eval: expressions
print(eval("1 + 2 * 3"))
print(eval("[n * n for n in range(5)]"))
print(eval("'-'.join(['a', 'b', 'c'])"))
print(eval("  (lambda x, y: x ** y)(2, 10)  "))
print(eval(repr({"k": [1, 2.5, None, True]})) == {"k": [1, 2.5, None, True]})

# eval reads the module's names
scale = 4
def triple(v):
    return v * 3
print(eval("triple(scale) + 1"))

# eval with explicit namespaces (globals, locals)
print(eval("a + b", {"a": 1, "b": 2}))
print(eval("a + b", {"a": 1}, {"b": 20}))
print(eval("len(xs)", {"xs": [1, 2, 3]}))
print(eval("abs(-7)", {"__builtins__": {"abs": abs}}))

# --- exec: statements
exec("print('hello from exec')")
exec("for i in range(3):\n    print('loop', i)")

# exec binds the module's names: functions see them
counter = 1
def read_counter():
    return counter * 10
exec("counter = 5")
print(counter, read_counter())
exec("counter += 1")
print(counter, read_counter())

# exec defines new functions and classes in the module
exec("""
def greet(name):
    return 'hi ' + name

class Box:
    def __init__(self, v):
        self.v = v
    def __repr__(self):
        return f'Box({self.v!r})'
""")
print(greet("neos"), Box(3), Box([1, 2]))

# exec with its own namespace dict
ns = {}
exec("x = 10\ny = x * 2\ndef f(n): return n + y", ns)
print(ns["x"], ns["y"], ns["f"](1), sorted(k for k in ns if k != "__builtins__"))

# exec with separate globals and locals
g = {"base": 100}
l = {}
exec("total = base + 1", g, l)
print(l, "total" in g)

# exec returns None; its result is in the namespace
print(exec("z = 1", {}))

# --- globals(): writes reach the module's variables
mode = "initial"
def get_mode():
    return mode
globals()["mode"] = "changed"
print(mode, get_mode())
globals().update(mode="updated")
print(mode, get_mode())

# --- compile(): code objects, run many times
code = compile("value * 2", "<expr>", "eval")
for value in (1, 2, 3):
    print(eval(code), end=" ")
print()
block = compile("result = [i for i in range(n)]", "<block>", "exec")
for n in (2, 4):
    exec(block)
    print(result)
print(type(code).__name__, code.co_filename)

# --- errors (a syntax error's location: its line, offset and text, as traceback shows it)
import traceback
for source in ("1 +", "def (:", "x = = 2", "if x == 1\n    pass", "total = (1 +\n    2))", "print 'hi'", "for i in range(3):\nprint(i)"):
    try:
        eval(source) if source == "1 +" else exec(source)
    except SyntaxError as e:
        print(f"{type(e).__name__}: {e.msg} (line {e.lineno}, offset {e.offset})")
        print("".join(traceback.format_exception_only(e)), end="")
try:
    eval("undefined_name + 1")
except NameError as e:
    print("NameError:", e)
try:
    exec("1 / 0")
except ZeroDivisionError as e:
    print("ZeroDivisionError:", e)
try:
    eval("abs(-1)", {"__builtins__": {}})
except NameError as e:
    print("NameError:", e)

# --- a small calculator: eval of user expressions in a restricted namespace
import math
allowed = {"__builtins__": {}, "sqrt": math.sqrt, "pi": math.pi, "max": max, "min": min}
for expression in ("sqrt(16) + 1", "round(pi, 3)", "max(3, 9, 2) - min(4, 1)"):
    try:
        print(expression, "=", eval(expression, allowed))
    except NameError as e:
        print(expression, "->", "NameError:", e)
