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
* Debugger: breakpoints, stepping, variables, watches and catchpoints for every language, with gdb's commands (see [Debugger](#debugger)).
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

## Ada
Ada programs (`.adb`, `.ads`, `.ada`, `.a`, schema `languages/ada.neos`) are Ada 2022 (`NEOS_ADA_VERSION=2012` applies Ada 2012's rules instead). neos is given the main subprogram's file; the units that it names (its with clauses, its body stubs' subunits, and theirs) are found by their GNAT file names (`ada-text_io.ads`) or by their own (`Ada.Text_IO.ads`), in any case, in the main file's folder, the current folder, the folders listed in `ADA_INCLUDE_PATH` and `NEOS_ADA_PATH`, then the predefined library's folder (`languages/packages/ada`). A `gnat.adc` file in the main file's folder holds the partition's configuration pragmas, as GNAT's does. The predefined library is in Ada (the packages of Annexes A to G); the run-time system (exceptions, finalization, tasking, input-output) is native (the Ada runtime plugin, `runtime/ada`).

Tasks are OS threads that run in parallel (`System.Multiprocessors.Number_Of_CPUs` is the number of processors), and so are the logical threads of parallel constructs: parallel blocks, parallel `for` loops (one chunk per processor, unless a chunk specification says how many) and `'Parallel_Reduce`. A partition that asks for Annex D's scheduling runs its tasks as coroutines instead, one at a time on the program's thread (`Number_Of_CPUs` is then 1, and parallel constructs run sequentially): a task runs until it blocks or reaches a dispatching point, and Annex D's priorities, locking and queuing policies and task dispatching policies decide which task runs next. It asks for that with pragma `Task_Dispatching_Policy`, `Priority_Specific_Dispatching`, `Locking_Policy (Ceiling_Locking)` or `Generate_Deadlines`, the Ravenscar or Jorvik profile, a `Priority` or `Interrupt_Priority` pragma or aspect in its own units, or by depending on `Ada.Dispatching`, `Ada.Asynchronous_Task_Control`, `Ada.Dynamic_Priorities`, `Ada.Execution_Time`, `Ada.Real_Time.Timing_Events`, `Ada.Synchronous_Task_Control.EDF` or `System.Multiprocessors.Dispatching_Domains`; `NEOS_ADA_THREADS=0` selects it for any program. Annex E (distribution) has one partition (`System.RPC` works within it). A program can have C translation units as well (`neos main.adb,util.c`): its Ada units import the C units' functions and objects (convention C, Annex B) and export theirs to them, and the two languages share their memory (an address designates the same object in either). `System.Machine_Code` and `Ada.Directories.Information` are not provided.

Examples and benchmarks are in `languages/examples/Ada`.

## C
C programs (`.c`, schema `languages/c.neos`) are C23. The standard library's headers are in `languages/packages/c/include`; most of the library is native (the C runtime plugin, `runtime/c`), and the rest is C (`languages/packages/c/libc.c`, and the files in `languages/packages/c/lib`, each compiled with a program only if the program uses a function that it provides). A program of several translation units is one program, linked by C's rules (`neos main.c,list.c,util.c`). `#include <...>` also searches the folders listed in `NEOS_C_INCLUDE_PATH`, `C_INCLUDE_PATH` and `CPATH`.

Objects are in the C runtime's memory (pointers are 64-bit: LP64); `long double` is `double`; `_BitInt` is at most 64 bits; threads are truly parallel OS threads (`<threads.h>`, POSIX `<pthread.h>` and `<semaphore.h>`; `_Thread_local`/`thread_local`/`__thread` objects; `_Atomic` objects and `<stdatomic.h>`, `__atomic_*` and `__sync_*` operations are atomic, sequentially consistent), there are no complex types (`__STDC_NO_COMPLEX__`); signals are those that `raise` raises. Common GNU extensions are accepted: statement expressions, labels as values (`&&label`, `goto *p`), case ranges, `[a ... b]` designators, `typeof`/`__typeof__`, `__builtin_` functions, `#pragma pack`, and `__attribute__`, of which `aligned`, `packed`, `constructor`, `destructor`, `cleanup` and `noreturn` are implemented (as C23's `[[gnu::...]]`; others are ignored). Old-style (K&R) function definitions are accepted too.

Examples and benchmarks are in `languages/examples/C`.

## JavaScript
JavaScript programs (`.js`, `.mjs`, schema `languages/javascript.neos`) are ECMAScript 2025, with Temporal and explicit resource management (`using`, `await using`, `DisposableStack`) as well. A `.js` file is a script and a `.mjs` file a module; modules are imported by relative or absolute paths (`import { f } from "./lib.mjs"`, and `import()`), and JSON modules with `with { type: "json" }`. All of the language's built-in objects are native (the JavaScript runtime plugin, `runtime/javascript`, with its own regular expression engine and a non-moving mark-sweep garbage collector). Intl (ECMA-402) needs ICU: a build without it (`-DNEOS_ICU=OFF`, or ICU not found) has no Intl, and Temporal has only the ISO 8601 calendar.

The host's globals are `console` and `performance`: there are no Node.js or browser APIs (`require`, `process`, `setTimeout`, the DOM). `eval`, the `Function` constructors and `import()` compile their source when they run. ECMAScript's conformance tests (test262) need a build with `-DNEOS_TEST262_HOST=ON`, whose test host (`print` and `$262`) is defined when the `NEOS_TEST262` environment variable is set.

Examples and benchmarks are in `languages/examples/JavaScript`.

## Lua
Lua programs (`.lua`, schema `languages/lua.neos`) are Lua 5.4, with its standard library (the basic functions, `coroutine`, `debug`, `io`, `math`, `os`, `package`, `string`, `table` and `utf8`: the Lua runtime plugin, `runtime/lua`). `require` searches `package.path`, which is `LUA_PATH_5_4` or `LUA_PATH` (`;;` in it stands for the default, `./?.lua;./?/init.lua`); there are no C modules (`package.cpath` is empty).

The garbage collector is a non-moving mark-sweep collector that scans the native stacks conservatively, so a weak table's entry can outlive the last reference that the program knows of. The debug library has no hooks (`debug.sethook` does nothing), and `debug.getlocal` and `debug.getupvalue` return nothing (the neos debugger shows a Lua program's variables: see [Debugger](#debugger)).

Examples and benchmarks are in `languages/examples/Lua`.

## neoscript
neoscript (`.neo`, schema `languages/neoscript.neos`) is neoGFX's default scripting language: statically typed, with namespaces, procedures (`proc`) and functions (`fn`), structs, classes (whose methods are virtual unless final) and interfaces. Its packages are in `languages/packages/neoscript`: `neo.stream` (`neo::print`, `neo::input`: the neoscript runtime plugin, `runtime/neo`, through the host's standard streams), `neo.string`, and `c` (the C standard library: the C runtime plugin's functions, called through libffi; `using c;`).

Examples are in `languages/examples/neoscript`.

## Python
Python programs (`.py`, schema `languages/python.neos`) are Python 3.13; integers are of any size. The standard library is native (the Python runtime plugin, `runtime/python`): its modules are C++, many of them translated from CPython 3.13's own sources (see `runtime/python/NOTICE`), though not all of CPython's standard library is there yet. `languages/packages/python/builtins.py` (the prelude, compiled before every program) declares the built-in functions that the runtime provides, typed for the compiler. Other modules are imported from the program's folder and the folders listed in `PYTHONPATH`. `import numpy` loads the NumPy runtime plugin (`runtime/numpy`: NumPy 2.5.3's behaviour, with the bundled OpenBLAS for its linear algebra; see `runtime/numpy/NOTICE`).

Threads (`threading`) are OS threads that take turns under a global interpreter lock, as CPython's do. `eval`, `exec` and `compile` compile their source when they run.

Examples and benchmarks are in `languages/examples/Python` (NumPy's in `languages/examples/Python/numpy`).

## Console
`-c` (`--console`) loads the program into the neos console instead of running it; `neos` with no program starts the console with nothing loaded. In the console, `c` compiles the program, `r` runs it and `h` lists the other commands.

```
neos -c hello.py
```

## Debugger
`-g` (`--debug`) compiles the program to be debugged and runs it under the neos debugger, whose commands are gdb's (`(ndb)` is its prompt):

```
neos -g hello.py
neos debugger: "run" (or "start") runs the program; "help" lists the commands.
(ndb) break 4
Breakpoint 1 at hello.py:4
(ndb) run
Enter your name: Leigh

Breakpoint 1, #0  <module> () at hello.py:4
4	print(f"Hello {name}!")
(ndb) print name
$1 = 'Leigh'  (str)
(ndb) continue
Hello Leigh!
[Program exited with code 0]
(ndb) quit
```

It debugs programs of every language (Ada, C, JavaScript, Lua, neoscript and Python) and the variables of each are shown as the language shows them (records, arrays, tables, objects, ...):

* breakpoints at a line of a program's source (`break`, `tbreak`), with conditions (`break 12 if n > 100`) and ignore counts;
* catchpoints (`catch throw [<exception>]`): the program stops where an exception is thrown (Ada, JavaScript, Lua and Python), whether or not it is caught, with the frames of the code that threw it;
* stepping (`step`, `next`, `finish`, `continue`), and the stack (`backtrace`, `frame`, `up`, `down`);
* variables (`info locals`, `info args`, `info globals`), and expressions of them (`print`; `display` shows one at each stop): expressions are evaluated without running any of the program's code;
* threads: when the program stops, all of its threads stop (`info threads`, `thread`);
* an error that the program does not handle stops it before it ends the program (`set break-on-error off` to let it end the program); to stop where an exception is thrown, use a catchpoint;
* Ctrl+C pauses the running program, also while it waits for input.

`help` lists the commands and `help <command>` describes one. While it is debugged a program runs in the interpreter (not the JIT), so it runs more slowly. In the console, `d(ebug) [<path>]` debugs the program.

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
| `-g`, `--debug` | Debug the program (see [Debugger](#debugger)) |
| `--img[=<path>]` | Make an executable image of the program rather than run it |
| `-l`, `--load <program>` | Program to load (the option name can be omitted); a linked program: its translation units, separated by commas (see [Linking](#linking)) |
| `-t`, `--trace <0-5>` | Compiler trace |
| `--restricted` | Restricted mode: programs cannot call runtime plugin functions that can change the host environment |
| `--no-preparse` | No language's own pass over a source before it is parsed |
| `--trap-on-overflow` | Integer arithmetic whose result overflows its (fixed size) type traps (by default it wraps) |
| `--bigint-promotion <on\|off>` | Integers of any size (a result that overflows is promoted to an arbitrary precision integer), overriding the schema's `bigint.promotion` |
| `--parse-times` | Console: output the time of each parse (by default only the total) |
| `--cache` | Keep the results of compilations (a program's bytecode, a schema's parsed grammars) in the user's cache folder (`$XDG_CACHE_HOME/neos` or `~/.cache/neos`; on Windows `%LOCALAPPDATA%\neos\cache`) and reuse them while nothing they depend on has changed; deleting the folder is always safe |

## Architecture
A program goes from source to running code through these parts:

1. **The language schema** (`languages/<language>.neos`): the language's meta data (its file extensions, its prelude, its options), its stages (a tokenizer and a parser, or a parser alone, each a grammar in the EBNF-like notation) and the concept libraries that give its constructs their meaning. A grammar rule marked `$ <concept>` makes a node of that semantic concept; the other rules only shape the parse.
2. **The parser** (neolib's `parser.hpp`): a memoizing (packrat) parser with left recursion, driven by the schema's grammars; nodes without a concept are flattened as they are made, so the result is a tree of the program's semantic concepts (its AST, which is kept after compilation for syntax highlighting and debugging). A language's compiler can make its own pass over a source before it is parsed (e.g. the C preprocessor, Ada's case folding).
3. **The concept libraries** (`.ncl`, `concepts/src`): the core library (`core.ncl`: the language-neutral concepts of names, expressions, statements, functions and types), the universal mathematics library (`core.math.universal.ncl`) and one for each language (`ada.core.ncl`, `python.core.ncl`, ...). As the AST is walked the concepts fold: each combines with the ones that it takes (its operands, its children) and emits what it means to the compiler. Language-specific concepts are in their language's library, never the core's.
4. **The compiler** (`src/compiler.cpp`, `include/neos/language/i_code_generator.hpp`): compiles the core concepts to bytecode itself (its operand stack, functions, globals, the type inference of its passes, which are repeated until what they infer settles) and hands each language's own constructs, and the places where the language's semantics are its own, to that language's compiler plugin (`.ncp`, `compilers/<language>`, `include/neos/language/i_language_compiler.hpp`): e.g. the Ada compiler does all of Ada's static semantics, and the Python compiler its scopes and closures. Translation units of different languages are compiled one by one and linked (see [Linking](#linking)).
5. **The bytecode**: a WebAssembly module, with neos's extensions: external memories with 64-bit addresses (a language runtime's heap, and the memory that the units of a linked program share), atomics (WebAssembly's threads proposal), globals that each thread has its own copy of, line and position markers (for errors and the debugger), and custom sections that name the frames and describe the variables. The runtime plugins' functions are its imports.
6. **The VM** (`src/bytecode`): decodes the module and runs its functions: in an interpreter, or as native x86-64 code (on other processors the interpreter runs everything). The baseline JIT is a single-pass compiler that compiles each function when it is first called; functions and loops that run often are compiled again by the optimizing tier on a background thread (inlining, speculation with deoptimization, SSA optimizations, register allocation), and a loop that is still running in baseline code continues in the optimized code (on-stack replacement). A program can run on several threads (C threads, Ada tasks).
7. **The runtime plugins** (`neos.runtime.<name>`, `runtime/<language>`, `include/neos/runtime/plugin.h`): shared libraries of C functions that the program calls (through libffi, or directly from generated code), and the host interface (`neos_rt_host`) that they use: the program's globals, coroutines, threads, external and shared memories, the standard streams, frames, traps and linking. A language's runtime plugin is its object model, its garbage collector (if it has one) and its standard library; a language's library written in the language itself is in `languages/packages/<language>`.

Around them: the console (`console`) and the API (`include/neos/context.hpp`: a context for each session, whose standard streams can be redirected; neoGFX Design Studio runs neos in process this way); the cache of compilations (`src/cache.cpp`, see `--cache`); executable images (`src/image.cpp`, `stub`: the runtime stub with a program's bytecode appended); and the debugger (`src/debug.cpp`, `include/neos/runtime/debug.h`: a runtime plugin describes its language's values to it).

## Contributing
If you wish to contribute to the neos project then create an issue containing a prompt suitable for the project maintainer to review and for Claude Code to analyse. We are not accepting pull requests at this time.
