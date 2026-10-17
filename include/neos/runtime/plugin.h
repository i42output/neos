/*
  plugin.h

  Copyright (c) 2026 Leigh Johnston.  All Rights Reserved.

  This program is free software: you can redistribute it and / or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
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

#define NEOS_RUNTIME_PLUGIN_VERSION 2
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

/* (optional) the host's functions: a plugin that calls back into the running program (e.g. a function of the program
   passed to it as a value) defines NEOS_RUNTIME_PLUGIN_SET_HOST { (keep aHost) }, which the host calls when it loads the
   plugin */
#define NEOS_RUNTIME_PLUGIN_SET_HOST_NAME "neos_runtime_plugin_set_host"

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

typedef struct neos_rt_host
{
    unsigned version;                                   /* NEOS_RUNTIME_PLUGIN_VERSION */
    /* call function aFunction of the running program (its index, as the program gave it to the plugin) with aCount 
       arguments of the types aParameters (objects and strings are borrowed); its result (of type aResult) is stored in
       *aResultValue: an object is a new reference, a string is valid until the next call; returns NULL, or the error
       message if the program trapped (valid until the next call) */
    char const* (*call)(unsigned aFunction, unsigned aCount, neos_rt_type const* aParameters, neos_rt_value const* aArguments,
        neos_rt_type aResult, neos_rt_value* aResultValue);
} neos_rt_host;

typedef void (*neos_rt_plugin_set_host)(neos_rt_host const* aHost);

#ifndef NEOS_RUNTIME_PLUGIN_HOST
/* the entry points a plugin exports (declared here so that they have C linkage in a plugin written in C++) */
NEOS_RUNTIME_PLUGIN_EXPORT neos_rt_plugin const* neos_runtime_plugin(void);
NEOS_RUNTIME_PLUGIN_EXPORT char const* neos_runtime_plugin_error(void);
NEOS_RUNTIME_PLUGIN_EXPORT void neos_runtime_plugin_set_host(neos_rt_host const* aHost);
#endif

#define NEOS_RT_FUNCTION(f) ((neos_rt_function_pointer)(f))

/* a plugin defines: NEOS_RUNTIME_PLUGIN_ENTRY { return &description; } */
#define NEOS_RUNTIME_PLUGIN_ENTRY NEOS_RUNTIME_PLUGIN_EXPORT neos_rt_plugin const* neos_runtime_plugin(void)

/* (optional) a plugin whose functions can fail defines: NEOS_RUNTIME_PLUGIN_ERROR { return (the error raised, cleared) or NULL; } */
#define NEOS_RUNTIME_PLUGIN_ERROR NEOS_RUNTIME_PLUGIN_EXPORT char const* neos_runtime_plugin_error(void)

/* (optional) a plugin that calls back into the program defines: NEOS_RUNTIME_PLUGIN_SET_HOST { (keep aHost) } */
#define NEOS_RUNTIME_PLUGIN_SET_HOST NEOS_RUNTIME_PLUGIN_EXPORT void neos_runtime_plugin_set_host(neos_rt_host const* aHost)

#ifdef __cplusplus
}
#endif

#endif /* NEOS_RUNTIME_PLUGIN_H */
