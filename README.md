![](https://github.com/i42output/neos/blob/master/neos.png)

# Introduction
"neos" is a cross-platform language agnostic scripting engine with a custom bytecode JIT. 

neos is still a work in progress so don't expect a release anytime soon.

# Dependencies
* Boost
* libffi
* neolib
* neonumeric

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

The build's `bin` folder has everything neos loads at run time: the `neos` executable, the concept libraries (`core.ncl`, `core.math.universal.ncl`, and the languages' own: `python.core.ncl`, `javascript.core.ncl`) and the runtime plugins (`plugins/neos.runtime.*`: c, javascript, python and numpy). Run neos from the `languages` folder, e.g. `neos -s python.neos examples/Python/hello.py` then `c` (compile) and `r` (run).
