/*
  object.h

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
  The neos Python object protocol (C API)

  Python objects are runtime plugin objects (neos_rt_object, see neos/runtime/plugin.h) whose type is a
  neos_py_type. The Python runtime plugin (neos.runtime.python) implements Python's core objects (None, bool,
  int, float, str, list, tuple, dict, slice, ...) and the operations that scripts perform on objects; other
  runtime plugins (e.g. neos.runtime.numpy) implement objects of their own by filling in a neos_py_type.

  - Every function of a neos_py_type receives the context of the call (neos_py_context): the functions with which
    an implementation creates and inspects Python objects, raises exceptions and performs Python operations. A
    plugin that implements Python objects therefore needs no link to the Python runtime plugin.
  - Functions that return an object return a new reference; NULL means that an exception was raised (except where
    stated otherwise). Functions that return int return 0 (or a count) on success and -1 if an exception was raised.
  - Arguments are borrowed unless stated otherwise ("steals": the function takes over the caller's reference).
  - A NULL function in a neos_py_type means that the type does not support the operation (the caller raises
    TypeError, as Python does).
  - binop and compare are called on the left operand's type and then (if it returns NotImplemented) on the right
    operand's type, with the operands in their original order.
*/

#ifndef NEOS_RUNTIME_PYTHON_OBJECT_H
#define NEOS_RUNTIME_PYTHON_OBJECT_H

#include <stddef.h>
#include <neos/runtime/plugin.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NEOS_PY_PROTOCOL_VERSION 1
#define NEOS_PY_TYPE_MAGIC 0x5459504Eu /* (identifies a neos_py_type) */

typedef neos_rt_object neos_py_object;
typedef struct neos_py_context neos_py_context;

/* binary operators (inplace: the augmented assignment form) */
typedef enum neos_py_binary
{
    NEOS_PY_ADD,
    NEOS_PY_SUBTRACT,
    NEOS_PY_MULTIPLY,
    NEOS_PY_TRUE_DIVIDE,
    NEOS_PY_FLOOR_DIVIDE,
    NEOS_PY_REMAINDER,
    NEOS_PY_POWER,
    NEOS_PY_MATMUL,
    NEOS_PY_LSHIFT,
    NEOS_PY_RSHIFT,
    NEOS_PY_AND,
    NEOS_PY_OR,
    NEOS_PY_XOR
} neos_py_binary;

typedef enum neos_py_unary
{
    NEOS_PY_NEGATIVE,
    NEOS_PY_POSITIVE,
    NEOS_PY_INVERT,
    NEOS_PY_ABSOLUTE
} neos_py_unary;

typedef enum neos_py_comparison
{
    NEOS_PY_LT,
    NEOS_PY_LE,
    NEOS_PY_EQ,
    NEOS_PY_NE,
    NEOS_PY_GT,
    NEOS_PY_GE
} neos_py_comparison;

/* the Python runtime plugin's own types */
typedef enum neos_py_kind
{
    NEOS_PY_OTHER,          /* (a type of another plugin) */
    NEOS_PY_NONE,
    NEOS_PY_NOT_IMPLEMENTED,
    NEOS_PY_ELLIPSIS,
    NEOS_PY_BOOL,
    NEOS_PY_INT,
    NEOS_PY_FLOAT,
    NEOS_PY_STR,
    NEOS_PY_LIST,
    NEOS_PY_TUPLE,
    NEOS_PY_DICT,
    NEOS_PY_SLICE,
    NEOS_PY_RANGE,
    NEOS_PY_ITERATOR,
    NEOS_PY_FUNCTION,       /* (a builtin function or method) */
    NEOS_PY_COMPLEX
} neos_py_kind;

typedef struct neos_py_type
{
    neos_rt_object_type base;   /* (name: the type's name, e.g. "numpy.ndarray") */
    unsigned magic;             /* NEOS_PY_TYPE_MAGIC */
    char const* name;           /* the name Python shows (e.g. "ndarray") */
    neos_py_object* (*repr)(neos_py_context* ctx, neos_py_object* self);                    /* -> str */
    neos_py_object* (*str)(neos_py_context* ctx, neos_py_object* self);                     /* -> str (NULL: repr) */
    neos_py_object* (*getattr)(neos_py_context* ctx, neos_py_object* self, char const* name);
    int (*setattr)(neos_py_context* ctx, neos_py_object* self, char const* name, neos_py_object* value);
    neos_py_object* (*call)(neos_py_context* ctx, neos_py_object* self, neos_py_object* args /* tuple */, neos_py_object* kwargs /* dict or NULL */);
    neos_py_object* (*getitem)(neos_py_context* ctx, neos_py_object* self, neos_py_object* key);
    int (*setitem)(neos_py_context* ctx, neos_py_object* self, neos_py_object* key, neos_py_object* value /* NULL: del */);
    neos_py_object* (*binary)(neos_py_context* ctx, int op, neos_py_object* lhs, neos_py_object* rhs, int inplace);
    neos_py_object* (*unary)(neos_py_context* ctx, int op, neos_py_object* self);
    neos_py_object* (*compare)(neos_py_context* ctx, int op, neos_py_object* lhs, neos_py_object* rhs);
    int (*truth)(neos_py_context* ctx, neos_py_object* self);                               /* 1, 0 or -1 */
    long long (*length)(neos_py_context* ctx, neos_py_object* self);                        /* -1: exception */
    neos_py_object* (*iter)(neos_py_context* ctx, neos_py_object* self);
    neos_py_object* (*next)(neos_py_context* ctx, neos_py_object* self);                    /* NULL without an exception: exhausted */
    int (*contains)(neos_py_context* ctx, neos_py_object* self, neos_py_object* item);      /* 1, 0 or -1 */
    int (*index)(neos_py_context* ctx, neos_py_object* self, long long* result);            /* __index__ */
    int (*to_float)(neos_py_context* ctx, neos_py_object* self, double* result);            /* __float__ */
    int (*to_int)(neos_py_context* ctx, neos_py_object* self, long long* result);           /* __int__ (truncating) */
    int (*hash)(neos_py_context* ctx, neos_py_object* self, long long* result);             /* __hash__ (NULL: the object's identity, 
                                                                                               or unhashable if compare is not NULL) */
} neos_py_type;

/* the functions available to an implementation (all take the context of the call) */
struct neos_py_context
{
    unsigned version;           /* NEOS_PY_PROTOCOL_VERSION */
    /* exceptions */
    void (*raise)(neos_py_context* ctx, char const* type /* e.g. "ValueError" */, char const* message);
    int (*failed)(neos_py_context* ctx);
    /* singletons and scalars */
    neos_py_object* (*none)(neos_py_context* ctx);
    neos_py_object* (*not_implemented)(neos_py_context* ctx);
    neos_py_object* (*ellipsis)(neos_py_context* ctx);
    neos_py_object* (*from_bool)(neos_py_context* ctx, int value);
    neos_py_object* (*from_int)(neos_py_context* ctx, long long value);
    neos_py_object* (*from_float)(neos_py_context* ctx, double value);
    neos_py_object* (*from_complex)(neos_py_context* ctx, double real, double imag);
    neos_py_object* (*from_str)(neos_py_context* ctx, char const* utf8, size_t length);
    int (*kind)(neos_py_context* ctx, neos_py_object* object);                              /* neos_py_kind */
    char const* (*type_name)(neos_py_context* ctx, neos_py_object* object);                 /* (as Python shows it) */
    int (*as_int)(neos_py_context* ctx, neos_py_object* object, long long* result);         /* bool, int or __index__ */
    int (*as_float)(neos_py_context* ctx, neos_py_object* object, double* result);          /* bool, int, float or __float__ */
    int (*as_complex)(neos_py_context* ctx, neos_py_object* object, double* real, double* imag);
    char const* (*as_str)(neos_py_context* ctx, neos_py_object* object, size_t* length);    /* (a str; borrowed; NULL if not) */
    /* containers */
    neos_py_object* (*tuple_new)(neos_py_context* ctx, size_t size);                        /* (items are None) */
    void (*tuple_set)(neos_py_context* ctx, neos_py_object* tuple, size_t index, neos_py_object* item /* steals */);
    neos_py_object* (*list_new)(neos_py_context* ctx);
    int (*list_append)(neos_py_context* ctx, neos_py_object* list, neos_py_object* item);
    size_t (*sequence_size)(neos_py_context* ctx, neos_py_object* object);                  /* (a list or tuple) */
    neos_py_object* (*sequence_item)(neos_py_context* ctx, neos_py_object* object, size_t index); /* (a list or tuple; borrowed) */
    neos_py_object* (*dict_new)(neos_py_context* ctx);
    int (*dict_set)(neos_py_context* ctx, neos_py_object* dict, neos_py_object* key, neos_py_object* value);
    size_t (*dict_size)(neos_py_context* ctx, neos_py_object* dict);
    void (*dict_item)(neos_py_context* ctx, neos_py_object* dict, size_t index, neos_py_object** key, neos_py_object** value); /* (borrowed) */
    neos_py_object* (*dict_get)(neos_py_context* ctx, neos_py_object* dict, char const* key);   /* (borrowed; NULL if absent, no exception) */
    void (*slice_parts)(neos_py_context* ctx, neos_py_object* slice, neos_py_object** start, neos_py_object** stop, neos_py_object** step); /* (borrowed) */
    neos_py_object* (*slice_new)(neos_py_context* ctx, neos_py_object* start, neos_py_object* stop, neos_py_object* step);
    neos_py_object* (*function_new)(neos_py_context* ctx, char const* name,
        neos_py_object* (*function)(neos_py_context* ctx, void* data, neos_py_object* args, neos_py_object* kwargs), void* data);
    /* operations (dispatched to the objects' types) */
    neos_py_object* (*repr)(neos_py_context* ctx, neos_py_object* object);
    neos_py_object* (*str)(neos_py_context* ctx, neos_py_object* object);
    neos_py_object* (*getattr)(neos_py_context* ctx, neos_py_object* object, char const* name);
    neos_py_object* (*call)(neos_py_context* ctx, neos_py_object* callable, neos_py_object* args, neos_py_object* kwargs);
    neos_py_object* (*getitem)(neos_py_context* ctx, neos_py_object* object, neos_py_object* key);
    neos_py_object* (*binary)(neos_py_context* ctx, int op, neos_py_object* lhs, neos_py_object* rhs);
    neos_py_object* (*unary)(neos_py_context* ctx, int op, neos_py_object* object);
    neos_py_object* (*compare)(neos_py_context* ctx, int op, neos_py_object* lhs, neos_py_object* rhs);
    int (*truth)(neos_py_context* ctx, neos_py_object* object);
    long long (*length)(neos_py_context* ctx, neos_py_object* object);
    neos_py_object* (*iter)(neos_py_context* ctx, neos_py_object* object);
    neos_py_object* (*next)(neos_py_context* ctx, neos_py_object* iterator);
    void (*retain)(neos_py_object* object);
    void (*release)(neos_py_object* object);
    /* a bound method: a function whose first argument is self (retained by the method) */
    neos_py_object* (*method_new)(neos_py_context* ctx, char const* name,
        neos_py_object* (*function)(neos_py_context* ctx, void* data, neos_py_object* args, neos_py_object* kwargs), void* data, neos_py_object* self);
    /* setitem and setattr (dispatched to the objects' types; -1: an exception was raised) */
    int (*setitem)(neos_py_context* ctx, neos_py_object* object, neos_py_object* key, neos_py_object* value);
    int (*setattr)(neos_py_context* ctx, neos_py_object* object, char const* name, neos_py_object* value);
    /* a type object (a class) of another plugin's type: callable (aCall) and an isinstance target */
    neos_py_object* (*type_new)(neos_py_context* ctx, char const* name, char const* qualifiedName,
        neos_py_object* (*call)(neos_py_context* ctx, void* data, neos_py_object* args, neos_py_object* kwargs), void* data,
        int (*instancecheck)(neos_py_context* ctx, void* data, neos_py_object* object));
    /* the exception raised ("type: message"; NULL if none) and clearing it (catching it) */
    char const* (*exception)(neos_py_context* ctx);
    void (*clear)(neos_py_context* ctx);
};

/* a plugin that provides a Python module ("import m") exports the function "module" (scripts call "m::module"),
   which takes the Python runtime plugin's context object (from python::context: a neos_py_context_object) and
   returns the module object */
#define NEOS_PY_CONTEXT_TYPE_NAME "neos.python.context"
typedef struct neos_py_context_object
{
    neos_rt_object base;        /* (base.type->name is NEOS_PY_CONTEXT_TYPE_NAME) */
    neos_py_context* context;
} neos_py_context_object;

#ifdef __cplusplus
}
#endif

#endif /* NEOS_RUNTIME_PYTHON_OBJECT_H */
