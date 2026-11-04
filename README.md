![](https://github.com/i42output/neos/blob/master/neos.png)
![](https://github.com/i42output/neos/blob/master/underconstruction.png)

# Introduction
"neos" is a cross-platform language agnostic scripting engine with a WASM bytecode JIT. 

neos is still a work in progress so don't expect a release anytime soon.

<img width="1348" height="676" alt="image" src="https://github.com/user-attachments/assets/f403f2cf-665b-4d3b-842d-0251ac0e4fc5" />

# Dependencies
* Boost
* libffi
* neolib
* neonumeric
* ICU (optional: JavaScript's Intl and Temporal's non-ISO 8601 calendars; the system's ICU (ICU 67 or later), on Windows the ICU in Windows)

# Features
* Language agnostic: a schema combined with a semantic concept library describes syntax and semantics of the scripting language to use (theoretically allowing any language to be used).
* EBNF-like language schema file format.
* Invent your own scripting language to use with "neos" by writing a new language schema! 
* Runtime plugins: scripts call native C functions in plugin shared libraries through libffi (see `include/neos/runtime/plugin.h`); the C runtime plugin (`runtime/c`) makes the C standard library available to neoscript (`using c;`).

# Building
neos builds with CMake (3.20 or later) and MSVC, Clang or GCC:

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

neolib and neonumeric are found with `find_package` (installed with their own CMake builds), or are built as part of neos if their source folders are given: `-DNEOS_NEOLIB_SOURCE_DIR=<neolib> -DNEOS_NEONUMERIC_SOURCE_DIR=<neonumeric>`. An installed neolib must be built with position independent code (`-DCMAKE_POSITION_INDEPENDENT_CODE=ON`) on Linux: it is linked into the concept libraries. libffi is found with pkg-config or `LIBFFI_INCLUDE_DIR` and `LIBFFI_LIBRARY` (`-DNEOS_LIBFFI_STATIC=ON` for a static libffi); if it is not found (e.g. on Windows), or with `-DNEOS_LIBFFI_BUNDLED=ON`, the bundled libffi (`3rdparty/libffi-3.4.8`, which has a CMake build of its own) is built as a static library. With `-DNEOS_LIBFFI=OFF` neos runs programs that do not call runtime plugins.

# Running
Give neos a program and it compiles and runs it. The only output is the program's own; warnings and errors go to stderr, and an error makes neos exit with a failure status:

```
neos hello.py
```

The language schema is chosen by the program's file extension. Schemas are searched for in the `share/neos/languages` folder beside the folder `neos` is in (where `cmake --install build` puts the `languages` folder, e.g. `/usr/local/share/neos/languages` for `/usr/local/bin/neos`), then in the folder `neos` is in.

To use a particular schema, give `-s` a language name (searched for in the same folders) or the path of a schema file:

```
neos -s python hello.py
neos -s ~/code/neos/languages/python.neos hello.py
```

The build's `bin` folder has no schemas, so to run neos from there without installing it, give `-s` a path, e.g. from the `languages` folder: `neos -s python.neos examples/Python/hello.py`.

## Linking
A program can be made of more than one translation unit, of any languages: give neos the files that make it up, separated by commas (the arguments that follow are the program's, as a program's of one file are; its first file names it, e.g. Python's `sys.argv[0]`):

```
neos lib.py,main.js
neos fib.lua,format.py,main.js data.txt
neos lib.py,main.py --verbose
```

(In PowerShell, quote them: `neos "lib.py,main.js"`.)

Their top levels run in turn, in the order they are given (a library before the code that uses it). The code of each language uses the globals of the others (e.g. the functions that their top levels define) by their names: a name that a unit uses but does not bind (where it would otherwise be an error, e.g. JavaScript's `ReferenceError` or Python's `NameError`, or a nil global in Lua) is another unit's, if one has a global of that name. Each unit is a program of its own, whose globals are its own (e.g. a Python unit's module `__main__`), whatever the languages of the others (JavaScript's units share their global object, and Lua's their global table, as their scripts do). Functions can be called across translation units, and passed to each other's functions (callbacks):

| Value | Crosses as |
|---|---|
| `undefined`, `null`, `None`, `nil` | no value (`undefined`, `None`, `nil`) |
| booleans, integers, floats, strings | themselves (a JavaScript number that is an integer is an integer) |
| functions | a function of the other language that calls it |
| other values | an object of the other language that refers to it (given back, it is the value it was) |

An error that a call raises (an exception that the called code does not handle) is raised by the call in the calling language.

neoscript (a language with no runtime plugin) takes part through its types: the other units call the functions and procedures that a neoscript unit defines at its top level (not a namespace's or a class's) by their names (those whose parameters and results are booleans, integers, floats or strings); a neoscript unit calls the other units' functions (and reads their other values, as functions with no parameters) by importing them in the namespace `link`, with the types that its code gives them:

```
namespace link
{
    import fn shout(s : string) -> string;
}
```

Examples (and scripts that run them) are in `languages/examples/link`.

## C
C programs (`.c`, schema `languages/c.neos`) are C23. The standard library's headers are in `languages/packages/c/include`; most of the library is native (the C runtime plugin, `runtime/c`), and the rest is C (`languages/packages/c/libc.c`, and the files in `languages/packages/c/lib`, each compiled with a program only if the program uses a function that it provides). A program of several translation units is one program, linked by C's rules (`neos main.c,list.c,util.c`). `#include <...>` also searches the folders listed in `NEOS_C_INCLUDE_PATH`, `C_INCLUDE_PATH` and `CPATH`.

Objects are in the C runtime's memory (pointers are 64-bit: LP64); `long double` is `double`; `_BitInt` is at most 64 bits; threads are truly parallel OS threads (`<threads.h>`, POSIX `<pthread.h>` and `<semaphore.h>`; `_Thread_local`/`thread_local`/`__thread` objects; `_Atomic` objects and `<stdatomic.h>`, `__atomic_*` and `__sync_*` operations are atomic, sequentially consistent), there are no complex types (`__STDC_NO_COMPLEX__`); signals are those that `raise` raises. Common GNU extensions are accepted: statement expressions, labels as values (`&&label`, `goto *p`), case ranges, `[a ... b]` designators, `typeof`/`__typeof__`, `__builtin_` functions, `#pragma pack`, and `__attribute__`, of which `aligned`, `packed`, `constructor`, `destructor`, `cleanup` and `noreturn` are implemented (as C23's `[[gnu::...]]`; others are ignored). Old-style (K&R) function definitions are accepted too.

Examples and benchmarks are in `languages/examples/C`.

## Console
`-c` (`--console`) loads the program into the neos console instead of running it; `neos` with no program starts the console with nothing loaded. In the console, `c` compiles the program, `r` runs it and `h` lists the other commands.

```
neos -c hello.py
```

## Images
`--img` makes an executable image of the program instead of running it: a copy of the runtime stub (`neos.stub`, beside `neos`) with the program's bytecode appended. The image is written to `--img=<path>`, or by default to the current folder as `<program>.exe` on Windows and `a.out` elsewhere:

```
neos --img hello.py
neos --img=bin/hello hello.py
```

An image runs only on the platform that made it. A program that calls runtime plugins still loads them when its image runs (see [Runtime plugins](#runtime-plugins)). The console's `img [<path>]` command makes an image of the compiled program.

## Runtime plugins
A program's runtime plugins (`neos.runtime.<name>`, with `.dll` on Windows, `.dylib` on macOS and `.so` elsewhere) are loaded when it first calls one of their functions. They are searched for in the folders listed in the `NEOS_RUNTIME_PLUGIN_PATH` environment variable (separated by `;` on Windows and `:` elsewhere), then in the folder of the running executable and its `plugins` folder.

The build and `cmake --install build` put the runtime plugins in the `plugins` folder beside `neos` (e.g. `/usr/local/bin/plugins`), so `neos` itself needs no `NEOS_RUNTIME_PLUGIN_PATH`. It is for plugins kept somewhere else, and for images: an image is the running executable, so it searches its own folder, and an image that is not beside `neos` (and has no `plugins` folder of its own) finds the installed plugins only if `NEOS_RUNTIME_PLUGIN_PATH` names their folder:

```
export NEOS_RUNTIME_PLUGIN_PATH=/usr/local/bin/plugins
set NEOS_RUNTIME_PLUGIN_PATH=C:\usr\local\bin\plugins
```

(the first for a Unix shell, the second for the Windows command prompt).

## Options
| Option | |
|---|---|
| `-s`, `--schema <language or path>` | Language schema (by default the one for the program's file extension) |
| `-c`, `--console` | Start the console (with the program loaded) rather than run the program |
| `--compile` | Compile the program when the console starts |
| `--img[=<path>]` | Make an executable image of the program rather than run it |
| `-l`, `--load <program>` | Program to load (the option name can be omitted); a linked program: its translation units, separated by commas (see [Linking](#linking)) |
| `-t`, `--trace <0-5>` | Compiler trace |
| `--restricted` | Restricted mode: programs cannot call runtime plugin functions that can change the host environment |
| `--no-preparse` | No language's own pass over a source before it is parsed |
| `--trap-on-overflow` | Integer arithmetic whose result overflows its (fixed size) type traps (by default it wraps) |
| `--bigint-promotion <on\|off>` | Integers of any size (a result that overflows is promoted to an arbitrary precision integer), overriding the schema's `bigint.promotion` |
| `--parse-times` | Console: output the time of each parse (by default only the total) |
| `--cache` | Keep the results of compilations (a program's bytecode, a schema's parsed grammars) in the user's cache folder (`$XDG_CACHE_HOME/neos` or `~/.cache/neos`; on Windows `%LOCALAPPDATA%\neos\cache`) and reuse them while nothing they depend on has changed; deleting the folder is always safe |

## Contributing
If you wish to contribute to the neos project then create an issue containing a prompt suitable for the project maintainer to review and for Claude Code to analyse. We are not accepting pull requests at this time.
