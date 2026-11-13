/*
  debug.h

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
  neos runtime plugins: the debugger (see neos/debug.hpp)
  A language runtime plugin describes the values of its language to the debugger: the values of a program's variables, as the 
  program holds them (64 bit words: a local's, a global's or a part's), with the types that its language's compiler gave them (see
  bytecode::VariablesSection: the plugin's own, e.g. "value" or "box"). It exports NEOS_RUNTIME_PLUGIN_DEBUG, which returns its 
  hooks. The debugger calls them (on the thread of the program's code that holds the value) while the program is stopped: the 
  values are valid (and do not change) until it resumes. It is optional: without it, its language's values are shown as numbers.
*/

#ifndef NEOS_RUNTIME_DEBUG_H
#define NEOS_RUNTIME_DEBUG_H

#include <stddef.h>
#include <neos/runtime/plugin.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NEOS_RUNTIME_DEBUG_VERSION 2

/* a part of a value (e.g. an element of an array, a field of a record, a property of an object): its name (e.g. "[2]", "x"), 
   its type (as the variables' types: the plugin's) and its value; the strings are valid until the plugin's hooks are next called 
   on the calling thread */
typedef struct neos_rt_debug_part
{
    char const* name;
    char const* type;
    unsigned long long value;
} neos_rt_debug_part;

typedef struct neos_rt_debug
{
    unsigned version;                                   /* NEOS_RUNTIME_DEBUG_VERSION */
    /* a value's text (as its language would show it, e.g. its repr: about aLimit bytes at most) and the name of its type (stored
       in *aTypeName, as its language names it, e.g. "list"); both valid until the hooks are next called on the calling thread */
    char const* (*describe)(char const* aType, unsigned long long aValue, size_t aLimit, char const** aTypeName);
    /* the number of a value's parts (0: none) */
    size_t (*part_count)(char const* aType, unsigned long long aValue);
    /* its parts from the part aStart: at most aCount, stored in aParts; returns the number stored */
    size_t (*parts)(char const* aType, unsigned long long aValue, size_t aStart, size_t aCount, neos_rt_debug_part* aParts);
} neos_rt_debug;

#define NEOS_RUNTIME_PLUGIN_DEBUG_NAME "neos_runtime_plugin_debug"
typedef neos_rt_debug const* (*neos_rt_plugin_debug)(void);

/* the debugger's functions that a language runtime plugin calls: a plugin that has errors that its language's code does not handle (e.g. 
   an uncaught exception) defines NEOS_RUNTIME_PLUGIN_SET_DEBUGGER { (keep aDebugger) }, which the host calls when it loads the plugin */
typedef struct neos_rt_debugger
{
    unsigned version;                                   /* NEOS_RUNTIME_DEBUG_VERSION */
    /* an error that the program does not handle, before the plugin reports it (on the thread whose code raised it, while the frames of 
       the code are as they were when it was raised, if they still are): if the program is being debugged (and its debugger stops on 
       errors) it stops, and this returns when it resumes; nonzero if the debugger is ending the program (e.g. it was ended where the 
       error was thrown: see thrown): the plugin need not report the error */
    int (*error)(char const* aMessage);
    /* (version 2) nonzero if an exception that the calling thread's code throws now could stop the program (it is being debugged and 
       its debugger has a catchpoint: see thrown); cheap: a plugin calls thrown only then (so it need not find an exception's type and
       message otherwise) */
    int (*catching)(void);
    /* (version 2) an exception that the program's code throws (raises), whether or not the program handles it, where it throws it (the
       frames of the code are as they are there), before it propagates; not one that propagates (or is thrown again as it is) from code
       that threw it before. aType: its type's name, as its language names it (e.g. "ValueError"); aMessage: its message ("" if none);
       aControl: nonzero if its language throws it for control flow (e.g. Python's StopIteration at the end of an iteration): only a 
       catchpoint that names its type stops for it. If a catchpoint (the debugger's "catch throw") matches, the program stops, and this
       returns when it resumes; nonzero if the debugger is ending the program */
    int (*thrown)(char const* aType, char const* aMessage, int aControl);
} neos_rt_debugger;

#define NEOS_RUNTIME_PLUGIN_SET_DEBUGGER_NAME "neos_runtime_plugin_set_debugger"
typedef void (*neos_rt_plugin_set_debugger)(neos_rt_debugger const* aDebugger);

#ifndef NEOS_RUNTIME_PLUGIN_HOST
NEOS_RUNTIME_PLUGIN_EXPORT neos_rt_debug const* neos_runtime_plugin_debug(void);
NEOS_RUNTIME_PLUGIN_EXPORT void neos_runtime_plugin_set_debugger(neos_rt_debugger const* aDebugger);
#endif

/* a language runtime plugin defines: NEOS_RUNTIME_PLUGIN_DEBUG { return &hooks; } */
#define NEOS_RUNTIME_PLUGIN_DEBUG NEOS_RUNTIME_PLUGIN_EXPORT neos_rt_debug const* neos_runtime_plugin_debug(void)
/* (optional) and: NEOS_RUNTIME_PLUGIN_SET_DEBUGGER { (keep aDebugger) } */
#define NEOS_RUNTIME_PLUGIN_SET_DEBUGGER NEOS_RUNTIME_PLUGIN_EXPORT void neos_runtime_plugin_set_debugger(neos_rt_debugger const* aDebugger)

#ifdef __cplusplus
}
#endif

#endif /* NEOS_RUNTIME_DEBUG_H */
