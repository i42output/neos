/*
  plugin.h

  Copyright (c) 2026 Leigh Johnston.  All Rights Reserved.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions are
  met:

     * Redistributions of source code must retain the above copyright
       notice, this list of conditions and the following disclaimer.

     * Redistributions in binary form must reproduce the above copyright
       notice, this list of conditions and the following disclaimer in the
       documentation and/or other materials provided with the distribution.

     * Neither the name of the copyright holder nor the names of its
       contributors may be used to endorse or promote products derived from
       this software without specific prior written permission.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
  IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
  THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
  PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
  CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
  EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
  PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
  PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
  LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
  NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

/*
  neos runtime plugins (C API)

  A runtime plugin is a shared library (DLL) of ordinary C functions that scripts call directly: the neos VM
  calls them through libffi, so a plugin needs no glue code per function, only a description of each
  function's C signature.

  - The plugin for the script namespace "x" is the shared library "neos.runtime.x" (".dll" on Windows, ".so"
    elsewhere); nested namespaces use dots ("a::b" -> "neos.runtime.a.b").
  - It is searched for in the directories listed in the NEOS_RUNTIME_PLUGIN_PATH environment variable (';'
    separated on Windows, ':' elsewhere), then in the directory of the executable and its "plugins" subdirectory.
  - It exports NEOS_RUNTIME_PLUGIN_ENTRY, which returns a description of the plugin: its namespace and its
    functions.
  - A script calls the function "f" of namespace "x" as an import named "x::f" (in neoscript: an "import fn"
    declared in "namespace x"); the import's types must match the description.
  - A function that can change the host environment (the file system, the process environment, other processes, 
    ...) is flagged NEOS_RT_RESTRICTED: a script running in restricted mode cannot call it.
  - A plugin whose functions can fail (with a runtime error that stops the script, e.g. a language runtime's
    RangeError) also exports NEOS_RUNTIME_PLUGIN_ERROR, which returns the error raised by the function the calling
    thread last called (clearing it) or NULL if it raised none. It is optional.
  - Such a plugin can also export NEOS_RUNTIME_PLUGIN_ERROR_PENDING, which returns the address of a long that is zero
    whenever NEOS_RUNTIME_PLUGIN_ERROR would return NULL on every thread (e.g. a count of the threads that have an error
    to report): native code calls NEOS_RUNTIME_PLUGIN_ERROR only when it is nonzero. It is optional.

  Types:
  - NEOS_RT_BOOL is C bool, NEOS_RT_CHAR is char, NEOS_RT_SIZE is size_t; the others are as named.
  - NEOS_RT_STRING is a NUL terminated "char const*": an argument is valid for the duration of the call; a
    result is copied by the VM (a null result is the empty string) so it may point to static or thread local storage.
  - NEOS_RT_OBJECT is a "neos_rt_object*", an object that a plugin implements (the VM only retains and releases it):
    an argument is borrowed for the duration of the call (a plugin that keeps it retains it); a result is a new
    reference that the VM takes over (a null result is no object). Scripts see objects as handles (i32; 0 is no
    object) that the VM validates, so they never see the objects' addresses; they retain and release handles with
    the VM's functions neos::object_retain and neos::object_release.
  - NEOS_RT_INTEGER is an integer of any size (int64_t): a small integer shifted left one bit (the low bit clear), or 
    an object (that the plugin implements, e.g. an arbitrary precision integer) as its "neos_rt_object*" with the low
    bit set (objects are at least two byte aligned). An object argument is borrowed and an object result is a new 
    reference, as NEOS_RT_OBJECT's. Scripts see an integer object as its handle shifted left one bit, with the low 
    bit set.
*/

#ifndef NEOS_RUNTIME_PLUGIN_H
#define NEOS_RUNTIME_PLUGIN_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NEOS_RUNTIME_PLUGIN_VERSION 14
#define NEOS_RUNTIME_PLUGIN_ENTRY_NAME "neos_runtime_plugin"

#if defined(_WIN32)
#define NEOS_RUNTIME_PLUGIN_EXPORT __declspec(dllexport)
#else
#define NEOS_RUNTIME_PLUGIN_EXPORT __attribute__((visibility("default")))
#endif

typedef enum neos_rt_type
{
    NEOS_RT_VOID,
    NEOS_RT_BOOL,
    NEOS_RT_CHAR,
    NEOS_RT_I32,
    NEOS_RT_U32,
    NEOS_RT_I64,
    NEOS_RT_U64,
    NEOS_RT_SIZE,
    NEOS_RT_F32,
    NEOS_RT_F64,
    NEOS_RT_STRING,
    NEOS_RT_OBJECT,
    NEOS_RT_INTEGER
} neos_rt_type;

/* an object: its type (whose functions implement it) is its first member; each plugin defines its own types (which
   can extend neos_rt_object_type with functions of their own) */
typedef struct neos_rt_object neos_rt_object;
typedef struct neos_rt_object_type
{
    char const* name;                                   /* (for diagnostics) */
    void (*retain)(neos_rt_object* aObject);            /* add a reference */
    void (*release)(neos_rt_object* aObject);           /* remove a reference (the last destroys it) */
} neos_rt_object_type;
struct neos_rt_object
{
    neos_rt_object_type const* type;
};

#define NEOS_RT_MAX_PARAMETERS 16
#define NEOS_RT_EXTERNAL_MEMORIES 8                     /* external memories (memory indices 1 to 8): see neos_rt_host::external_memory_at */
#define NEOS_RT_EXTERNAL_MEMORY_FIXED 0x01u             /* external_memory_at: the region's address and size never change */

/* function flags */
#define NEOS_RT_RESTRICTED 0x1u /* can change the host environment: not callable by scripts running in restricted mode */

typedef void (*neos_rt_function_pointer)(void);

typedef struct neos_rt_function
{
    char const* name;                                   /* the name scripts use */
    neos_rt_function_pointer function;                  /* the C function (cast with NEOS_RT_FUNCTION) */
    neos_rt_type result;
    unsigned parameterCount;
    neos_rt_type parameters[NEOS_RT_MAX_PARAMETERS];
    unsigned flags;                                     /* NEOS_RT_RESTRICTED */
} neos_rt_function;

typedef struct neos_rt_plugin
{
    unsigned version;                                   /* NEOS_RUNTIME_PLUGIN_VERSION */
    char const* name;                                   /* the script namespace, e.g. "c" */
    char const* description;
    unsigned functionCount;
    neos_rt_function const* functions;
} neos_rt_plugin;

typedef neos_rt_plugin const* (*neos_rt_plugin_entry)(void);

#define NEOS_RUNTIME_PLUGIN_ERROR_NAME "neos_runtime_plugin_error"

typedef char const* (*neos_rt_plugin_error)(void);

/* (optional) whether a plugin's functions may have raised errors (see NEOS_RUNTIME_PLUGIN_ERROR_PENDING) */
#define NEOS_RUNTIME_PLUGIN_ERROR_PENDING_NAME "neos_runtime_plugin_error_pending"

typedef long const* (*neos_rt_plugin_error_pending)(void);

/* (optional) the host's functions: a plugin that calls back into the running program (e.g. a function of the program
   passed to it as a value) defines NEOS_RUNTIME_PLUGIN_SET_HOST { (keep aHost) }, which the host calls when it loads the
   plugin */
#define NEOS_RUNTIME_PLUGIN_SET_HOST_NAME "neos_runtime_plugin_set_host"

/* (optional) the end of a program: a plugin that has work to finish before the program's state is destroyed (e.g. 
   threads that run the program's functions) defines NEOS_RUNTIME_PLUGIN_END { ... }, which the host calls (on the 
   program's thread) when the program's entry point returns */
#define NEOS_RUNTIME_PLUGIN_END_NAME "neos_runtime_plugin_end"

typedef void (*neos_rt_plugin_end)(void);

/* a coroutine: code that runs on a stack of its own, which it can suspend (e.g. the body of a generator) */
typedef struct neos_rt_coroutine neos_rt_coroutine;

typedef union neos_rt_value
{
    int b;
    int i32;
    unsigned u32;
    long long i64;
    unsigned long long u64;
    float f32;
    double f64;
    char const* string;
    neos_rt_object* object;
} neos_rt_value;

/* a frame of the running program (see neos_rt_host::frames): a call of one of its functions that has not returned. Its 
   function's name and its source's name are as the compiler named them (valid while the program is loaded); its line is the 
   line of the source that it is running (the call it is making: 0 if unknown); its first line is its function's; its program 
   is the program it belongs to (as thread_context's); its id identifies the call while it runs (the calls running have 
   different ids); (version 11) its position is the span of the source of the expression that the call evaluates, if the compiler gave one 
   (endLine: its last line (0: none); column and endColumn: its first column and the column after its last, in bytes, plus one 
   (0: none)) */
typedef struct neos_rt_frame
{
    char const* function;
    char const* source;
    unsigned line;
    unsigned firstLine;
    void* program;
    unsigned long long id;
    unsigned endLine;
    unsigned column;
    unsigned endColumn;
} neos_rt_frame;

typedef struct neos_rt_host
{
    unsigned version;                                   /* NEOS_RUNTIME_PLUGIN_VERSION */
    /* call function aFunction of the running program (its index, as the program gave it to the plugin) with aCount 
       arguments of the types aParameters (objects and strings are borrowed); its result (of type aResult) is stored in
       *aResultValue: an object is a new reference, a string is valid until the next call; returns NULL, or the error
       message if the program trapped (valid until the next call) */
    char const* (*call)(unsigned aFunction, unsigned aCount, neos_rt_type const* aParameters, neos_rt_value const* aArguments,
        neos_rt_type aResult, neos_rt_value* aResultValue);
    /* coroutines: a new one (aEntry(aData) runs when it is first resumed); resume one (on the calling thread: returns when 
       it suspends itself (0) or when its entry returns (1)); suspend the running one (from its own code: the code that 
       resumed it continues); delete one (that has finished, or that is suspended: its stack is discarded) */
    neos_rt_coroutine* (*coroutine_new)(void (*aEntry)(void* aData), void* aData);
    int (*coroutine_resume)(neos_rt_coroutine* aCoroutine);
    void (*coroutine_suspend)(void);
    void (*coroutine_delete)(neos_rt_coroutine* aCoroutine);
    /* threads: the running program (as the calling thread runs it), and attaching another thread to it (so that it can 
       call the program's functions; NULL: detach) */
    void* (*thread_context)(void);
    void (*thread_attach)(void* aContext);
    /* programs compiled while a program runs (e.g. Python's exec): the program of the source aSource in the language 
       aLanguage (aOptions: the language's options, e.g. a mode) as a context (as thread_context's: it stays until the 
       process ends, or until it is unloaded (see unload); NULL: the error message is stored in *aError (valid until the next 
       call)); run its entry point on the calling thread (its result: as call's; returns NULL, or the error message if it trapped) */
    void* (*load)(char const* aLanguage, char const* aSource, char const* aSourceName, char const* aOptions, char const** aError);
    char const* (*run)(void* aContext, neos_rt_type aResult, neos_rt_value* aResultValue);
    /* restricted mode: nonzero if the running program (as the calling thread runs it) is running in restricted mode (a plugin's 
       functions that are not flagged NEOS_RT_RESTRICTED but can change the host environment (e.g. a language's own library's, that 
       its runtime dispatches) refuse to run) */
    int (*restricted)(void);
    /* a trap: the running program (as the calling thread runs it) stops with the runtime error aMessage when the plugin function 
       that the calling thread is in returns (the plugin reports an error then: its NEOS_RUNTIME_PLUGIN_ERROR's), even if the program 
       handles plugins' errors (e.g. a language's own library refusing to run in restricted mode) */
    void (*trap)(char const* aMessage);
    /* the calling thread's frames, innermost first (the innermost is the call of the plugin's function), across the programs 
       it is running and the coroutines that resumed the one it is running: the innermost aCount of them (or all, if there are 
       fewer) are stored in aFrames; returns how many it stored (functions that the compiler did not name (e.g. a language's 
       helpers) have none) */
    unsigned (*frames)(neos_rt_frame* aFrames, unsigned aCount);
    /* a function's frame (as frames' are, but of the function rather than of a call of it): the function aFunction (its index, 
       as the program gave it to the plugin) of the program aContext (as thread_context's; NULL: the running program, as the 
       calling thread runs it); its name, source and first line are stored in *aFrame (its line and id are 0); returns 0 if 
       the function has none (e.g. a language's helpers) */
    unsigned (*function_frame)(void* aContext, unsigned aFunction, neos_rt_frame* aFrame);
    /* the program's command line arguments (its own: the program (its path, as it was given) first, then the arguments that 
       follow it): the first aCount of them (or all, if there are fewer) are stored in aArguments (valid until the process ends); 
       returns how many there are */
    unsigned (*arguments)(char const** aArguments, unsigned aCount);
    /* (version 8) the values that the interpreter holds for the program's frames (their locals and operand stacks), e.g. for a
       language runtime's garbage collector: aVisit(aData, values, count) is called for each run of them (64 bit words: a value of
       a narrower type is in the low bits of its word); aCoroutine NULL: the frames of the calling thread's running code (the
       programs it is running, across the coroutines that resumed the one it is running), else those of a suspended coroutine.
       Native code's (JIT) frames are on the native stack of their thread (or coroutine): none of their values are visited */
    void (*values)(neos_rt_coroutine* aCoroutine, void (*aVisit)(void* aData, unsigned long long const* aValues, size_t aCount), void* aData);
    /* (version 8) load, with the source's length (aSourceLength bytes: the source may contain NUL characters) */
    void* (*load_n)(char const* aLanguage, char const* aSource, size_t aSourceLength, char const* aSourceName, char const* aOptions,
        char const** aError);
    /* (version 9) the standard streams of the running program (as the calling thread runs it; aStream: 0 (input), 1 (output) or 
       2 (error)): its host's (e.g. a window that a session of an IDE redirects them to), which a plugin uses rather than the 
       process's (stdin, stdout and stderr) so that programs that run at the same time each have their own. With no program 
       running, the process's. As the C library's: std_write writes aCount bytes of aData (returns the number written: fewer if 
       the stream failed); std_read reads at most aCount bytes into aBuffer, as fread does (returns the number read: fewer at 
       the end of input); std_getc reads a character, as fgetc does (EOF at the end of input); std_ungetc puts one back, as 
       ungetc does; std_flush flushes, as fflush does (0, else EOF) */
    size_t (*std_write)(int aStream, char const* aData, size_t aCount);
    size_t (*std_read)(int aStream, char* aBuffer, size_t aCount);
    int (*std_getc)(int aStream);
    int (*std_ungetc)(int aCharacter, int aStream);
    int (*std_flush)(int aStream);
    /* (version 10) a global of the running program (as the calling thread runs it): the address of its 64 bit word (the program's
       code reads it there (interpreted or compiled), so a plugin can keep a value there that the code reads without calling it; valid
       while the program is loaded); NULL if there is no such global */
    unsigned long long* (*global)(unsigned aGlobal);
    /* (version 12) unload a program that load compiled (aContext: as load returned), when nothing refers to its code any longer (e.g.
       eval code that made no functions): its memory and native code are released, and aContext is no longer valid. A program that
       is running (on the calling thread) is not unloaded */
    void (*unload)(void* aContext);
    /* (version 13) the external memory: a region of a plugin's (e.g. a language runtime's heap) that programs' code reads and writes
       as memory 1 (a memory access whose memarg has a memory index (flag 0x40) of 1, with a 64 bit address (an offset in the region)),
       so that the code a language's compiler generates can use the runtime's objects without calling it. aBase: the region's address;
       aSize: its size (an access beyond it traps). Process-wide (one memory 1, the last set); set again when the region grows */
    void (*external_memory)(void* aBase, unsigned long long aSize);
    /* (version 14) an external memory by its index (1 to NEOS_RT_EXTERNAL_MEMORIES; external_memory sets memory 1): as external_memory,
       for code whose memargs have that memory index, so that more than one plugin (e.g. the runtimes of different languages) can each
       have a region that code uses at once (a language's compiler and runtime agree the index that its code uses). aFlags: 
       NEOS_RT_EXTERNAL_MEMORY_FIXED: the region's address and size never change (code compiled after it is set may use them as 
       constants; set it before the programs that use it are loaded, e.g. when the plugin's host is set): an access within the size
       is not checked further (an access of an address in it that the plugin has not made accessible (e.g. a reserved page that is not
       committed) faults) */
    void (*external_memory_at)(unsigned aIndex, void* aBase, unsigned long long aSize, unsigned aFlags);
} neos_rt_host;

typedef void (*neos_rt_plugin_set_host)(neos_rt_host const* aHost);

#ifndef NEOS_RUNTIME_PLUGIN_HOST
/* the entry points a plugin exports (declared here so that they have C linkage in a plugin written in C++) */
NEOS_RUNTIME_PLUGIN_EXPORT neos_rt_plugin const* neos_runtime_plugin(void);
NEOS_RUNTIME_PLUGIN_EXPORT char const* neos_runtime_plugin_error(void);
NEOS_RUNTIME_PLUGIN_EXPORT long const* neos_runtime_plugin_error_pending(void);
NEOS_RUNTIME_PLUGIN_EXPORT void neos_runtime_plugin_set_host(neos_rt_host const* aHost);
NEOS_RUNTIME_PLUGIN_EXPORT void neos_runtime_plugin_end(void);
#endif

#define NEOS_RT_FUNCTION(f) ((neos_rt_function_pointer)(f))

/* a plugin defines: NEOS_RUNTIME_PLUGIN_ENTRY { return &description; } */
#define NEOS_RUNTIME_PLUGIN_ENTRY NEOS_RUNTIME_PLUGIN_EXPORT neos_rt_plugin const* neos_runtime_plugin(void)

/* (optional) a plugin whose functions can fail defines: NEOS_RUNTIME_PLUGIN_ERROR { return (the error raised, cleared) or NULL; } */
#define NEOS_RUNTIME_PLUGIN_ERROR NEOS_RUNTIME_PLUGIN_EXPORT char const* neos_runtime_plugin_error(void)

/* (optional) and defines: NEOS_RUNTIME_PLUGIN_ERROR_PENDING { return (the address of a long that is zero while it raises none); } */
#define NEOS_RUNTIME_PLUGIN_ERROR_PENDING NEOS_RUNTIME_PLUGIN_EXPORT long const* neos_runtime_plugin_error_pending(void)

/* (optional) a plugin that calls back into the program defines: NEOS_RUNTIME_PLUGIN_SET_HOST { (keep aHost) } */
#define NEOS_RUNTIME_PLUGIN_SET_HOST NEOS_RUNTIME_PLUGIN_EXPORT void neos_runtime_plugin_set_host(neos_rt_host const* aHost)

/* (optional) a plugin with work to finish when a program ends defines: NEOS_RUNTIME_PLUGIN_END { ... } */
#define NEOS_RUNTIME_PLUGIN_END NEOS_RUNTIME_PLUGIN_EXPORT void neos_runtime_plugin_end(void)

#ifdef __cplusplus
}
#endif

#endif /* NEOS_RUNTIME_PLUGIN_H */
