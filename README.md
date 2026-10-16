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
