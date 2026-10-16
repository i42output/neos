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

  Types:
  - NEOS_RT_BOOL is C bool, NEOS_RT_CHAR is char, NEOS_RT_SIZE is size_t; the others are as named.
  - NEOS_RT_STRING is a NUL terminated "char const*": an argument is valid for the duration of the call; a
    result is copied by the VM (a null result is the empty string) so it may point to static or thread local storage.
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
    NEOS_RT_STRING
} neos_rt_type;

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

#ifndef NEOS_RUNTIME_PLUGIN_HOST
/* the entry point a plugin exports (declared here so that it has C linkage in a plugin written in C++) */
NEOS_RUNTIME_PLUGIN_EXPORT neos_rt_plugin const* neos_runtime_plugin(void);
#endif

#define NEOS_RT_FUNCTION(f) ((neos_rt_function_pointer)(f))

/* a plugin defines: NEOS_RUNTIME_PLUGIN_ENTRY { return &description; } */
#define NEOS_RUNTIME_PLUGIN_ENTRY NEOS_RUNTIME_PLUGIN_EXPORT neos_rt_plugin const* neos_runtime_plugin(void)

#ifdef __cplusplus
}
#endif

#endif /* NEOS_RUNTIME_PLUGIN_H */
