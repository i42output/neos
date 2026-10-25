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
| `-l`, `--load <program>` | Program to load (the option name can be omitted) |
| `-t`, `--trace <0-5>` | Compiler trace |
| `--restricted` | Restricted mode: programs cannot call runtime plugin functions that can change the host environment |
| `--no-preparse` | No language's own pass over a source before it is parsed |
| `--trap-on-overflow` | Integer arithmetic whose result overflows its (fixed size) type traps (by default it wraps) |
| `--bigint-promotion <on\|off>` | Integers of any size (a result that overflows is promoted to an arbitrary precision integer), overriding the schema's `bigint.promotion` |
| `--parse-times` | Console: output the time of each parse (by default only the total) |

## Contributing
If you wish to contribute to the neos project then create an issue containing a prompt suitable for the project maintainer to review and for Claude Code to analyse. We are not accepting pull requests at this time.
