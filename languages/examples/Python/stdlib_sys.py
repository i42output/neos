import sys
print(sys.version_info[:2] >= (3, 13), sys.version_info.major, type(sys.version_info).__name__, sys.hexversion >= 0x030D0000)
print(sys.maxsize, sys.byteorder, sys.platform in ('linux', 'win32', 'darwin'), isinstance(sys.argv, list), isinstance(sys.path, list))
print(sys.getrecursionlimit(), sys.getswitchinterval(), sys.float_info.max, sys.float_info.epsilon, sys.float_info.dig, sys.int_info.bits_per_digit)
print(sys.hash_info.width, sys.hash_info.modulus, sys.hash_info.inf, sys.maxunicode, sys.getdefaultencoding(), sys.getfilesystemencoding())
sys.setrecursionlimit(2000)
print(sys.getrecursionlimit())
print(sys.getsizeof(0), sys.getsizeof(1), sys.getsizeof(2**64), sys.getsizeof(1.5), sys.getsizeof(''), sys.getsizeof('abc'), sys.getsizeof([]), sys.getsizeof(()), sys.getsizeof((1, 2)))
print('sys' in sys.modules, sys.modules['sys'] is sys, 'math' in sys.builtin_module_names or True, 'os' in sys.stdlib_module_names)
print(sys.intern('abc') == 'abc', sys.flags.optimize, sys.thread_info.name in ('pthread', 'nt'))
try:
    raise ValueError('boom')
except ValueError:
    t, v, tb = sys.exc_info()
    print(t.__name__, v, sys.exception() is v)
print(sys.exc_info(), sys.exception())
sys.stdout.write('written\n')
print('err', file=sys.stderr)
print(sys.stdout is sys.__stdout__, sys.stdout.name if hasattr(sys.stdout, 'name') else '', sys.stdin.readable() if hasattr(sys.stdin, 'readable') else True)
sys.stdout.flush()
def f():
    try:
        sys.exit(3)
    except SystemExit as e:
        print('SystemExit', e.code)
f()
try:
    sys.exit('message')
except SystemExit as e:
    print('SystemExit', e.code, e.args)
print('done')
