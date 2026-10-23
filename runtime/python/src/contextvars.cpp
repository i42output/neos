/*
  contextvars.cpp

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

  Portions of this file are derived from CPython 3.13 (Python/context.c, Python/clinic/context.c.h and
  Modules/_contextvarsmodule.c), Copyright (c) 2001-2024 Python Software Foundation; All Rights Reserved. They are used
  under the Python Software Foundation License Version 2 (see runtime/python/LICENSE.PSF). Changes: translated from C to
  C++ for the neos Python runtime plugin and adapted to its object model (see runtime/python/NOTICE).
*/

// The modules _contextvars and contextvars (CPython 3.13's Python/context.c): Context (a mapping of context variables to their
// values), ContextVar, Token and copy_context. Each thread has its current context (made when it is first needed: a new thread's is
// empty); Context.run enters a context (it is the thread's current context while the callable runs). A context's variables are an
// immutable map that contexts share (copy_context and Context.copy are O(1)); setting a variable makes a new map for the context.
// (CPython's map is a HAMT, which iterates in the order of its keys' hashes (addresses); this one iterates in the order the variables
// were set.)

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include "objects.hpp"

namespace neos::python_runtime
{
    namespace detail
    {
        namespace
        {
            // (a context's variables and their values (new references; immutable once shared))
            struct vars_map
            {
                std::vector<std::pair<object*, object*>> items;
                vars_map() = default;
                vars_map(vars_map const& aOther) : items{ aOther.items }
                {
                    for (auto const& [key, value] : items)
                    {
                        retain(key);
                        retain(value);
                    }
                }
                ~vars_map()
                {
                    for (auto const& [key, value] : items)
                    {
                        release(key);
                        release(value);
                    }
                }
                std::size_t find(object* aVar) const
                {
                    for (std::size_t i = 0u; i < items.size(); ++i)
                        if (items[i].first == aVar)
                            return i;
                    return items.size();
                }
            };
            using vars_ptr = std::shared_ptr<vars_map const>;

            vars_ptr empty_vars()
            {
                static vars_ptr const sEmpty = std::make_shared<vars_map const>();
                return sEmpty;
            }

            struct context_object
            {
                header h;
                vars_ptr vars = empty_vars();
                object* prev = nullptr;     ///< (entered: the thread's context before it)
                bool entered = false;
                ~context_object() { release(prev); }
            };

            struct contextvar_object
            {
                header h;
                object* name = nullptr;
                object* defaultValue = nullptr;     ///< (null: none)
                long long hash = 0;
                ~contextvar_object() { release(name); release(defaultValue); }
            };

            struct token_object
            {
                header h;
                object* context = nullptr;
                object* var = nullptr;
                object* oldValue = nullptr;         ///< (null: Token.MISSING)
                bool used = false;
                ~token_object() { release(context); release(var); release(oldValue); }
            };

            // (an iterator of a context's keys, items or values (CPython's hamt iterators: their types are 'keys', 'items' and 'values'))
            struct vars_iterator_object
            {
                header h;
                vars_ptr vars;
                std::size_t index = 0u;
                int what = 0;   ///< (0: keys, 1: items, 2: values)
            };

            neos_py_type make_context_type();
            neos_py_type make_contextvar_type();
            neos_py_type make_token_type();
            neos_py_type make_missing_type();
            neos_py_type make_vars_iterator_type(char const* aName);
            neos_py_type sContextType = make_context_type();
            neos_py_type sContextVarType = make_contextvar_type();
            neos_py_type sTokenType = make_token_type();
            neos_py_type sMissingType = make_missing_type();
            neos_py_type sVarsIteratorTypes[3] = { make_vars_iterator_type("keys"), make_vars_iterator_type("items"), make_vars_iterator_type("values") };

            singleton sMissing{ { &sMissingType.base } };   // (Token.MISSING)

            bool is_context(object* aObject) { return aObject != nullptr && aObject->type == &sContextType.base; }
            bool is_contextvar(object* aObject) { return aObject != nullptr && aObject->type == &sContextVarType.base; }
            bool is_token(object* aObject) { return aObject != nullptr && aObject->type == &sTokenType.base; }
            context_object& as_context(object* aObject) { return *reinterpret_cast<context_object*>(aObject); }
            contextvar_object& as_contextvar(object* aObject) { return *reinterpret_cast<contextvar_object*>(aObject); }
            token_object& as_token(object* aObject) { return *reinterpret_cast<token_object*>(aObject); }

            std::string repr_text_of(object* aObject)
            {
                std::string text;
                return repr_of(aObject, text) ? text : std::string{};
            }

            // (the thread's current context (a new reference held by the thread))
            thread_local object* tContext = nullptr;

            object* new_context(vars_ptr aVars)
            {
                auto const c = new context_object{ { { &sContextType.base } } };
                c->vars = std::move(aVars);
                return &c->h.base;
            }

            // (the thread's current context (borrowed): made (empty) if it has none)
            object* current_context()
            {
                if (tContext == nullptr)
                    tContext = new_context(empty_vars());
                return tContext;
            }

            bool context_enter(object* aContext)
            {
                auto& c = as_context(aContext);
                if (c.entered)
                    return (error("RuntimeError", "cannot enter context: " + repr_text_of(aContext) + " is already entered"), false);
                c.prev = tContext;  // (the thread's reference)
                c.entered = true;
                retain(aContext);
                tContext = aContext;
                return true;
            }

            bool context_exit(object* aContext)
            {
                auto& c = as_context(aContext);
                if (!c.entered)
                    return (error("RuntimeError", "cannot exit context: " + repr_text_of(aContext) + " has not been entered"), false);
                if (tContext != aContext)
                    return (error("RuntimeError", "cannot exit context: thread state references a different context object"), false);
                tContext = c.prev;
                c.prev = nullptr;
                c.entered = false;
                release(aContext);
                return true;
            }

            // (a variable's value in the current context: borrowed; null if it has none)
            object* var_value(object* aVar)
            {
                if (tContext == nullptr)
                    return nullptr;
                auto const& vars = *as_context(tContext).vars;
                auto const i = vars.find(aVar);
                return i < vars.items.size() ? vars.items[i].second : nullptr;
            }

            // (the current context's variables with aVar's value set (aValue null: removed (false: it had none)))
            bool var_assign(object* aVar, object* aValue)
            {
                auto& c = as_context(current_context());
                auto const i = c.vars->find(aVar);
                if (aValue == nullptr && i == c.vars->items.size())
                {
                    fn_raise(ref{ invoke(ref{ exception_class("LookupError") }.get(), { new_reference(aVar) }) }.get());
                    return false;
                }
                auto vars = std::make_shared<vars_map>(*c.vars);
                if (aValue == nullptr)
                {
                    release(vars->items[i].first);
                    release(vars->items[i].second);
                    vars->items.erase(vars->items.begin() + static_cast<std::ptrdiff_t>(i));
                }
                else if (i < vars->items.size())
                {
                    retain(aValue);
                    release(vars->items[i].second);
                    vars->items[i].second = aValue;
                }
                else
                {
                    retain(aVar);
                    retain(aValue);
                    vars->items.emplace_back(aVar, aValue);
                }
                c.vars = std::move(vars);
                return true;
            }

            object* make_token(object* aContext, object* aVar, object* aOldValue)
            {
                auto const t = new token_object{ { { &sTokenType.base } } };
                retain(aContext);
                retain(aVar);
                if (aOldValue != nullptr)
                    retain(aOldValue);
                t->context = aContext;
                t->var = aVar;
                t->oldValue = aOldValue;
                return &t->h.base;
            }

            object* lookup_error(object* aVar)
            {
                ref const e{ invoke(ref{ exception_class("LookupError") }.get(), { new_reference(aVar) }) };
                if (e)
                    fn_raise(e.get());
                return nullptr;
            }

            // --- methods (a table of each type's: a bound method's data is its entry)

            struct method_entry
            {
                char const* name;
                object* (*function)(object* aSelf, std::vector<object*> const& aArgs, object* aKwargs);
                char const* doc;
            };

            object* call_method_entry(neos_py_context*, void* aData, object* aArgs, object* aKwargs)
            {
                auto const& entry = *static_cast<method_entry const*>(aData);
                auto const& items = as<tuple_object>(aArgs)->items;
                std::vector<object*> const args{ items.begin() + 1, items.end() };
                return entry.function(items[0], args, aKwargs != nullptr && !as<dict_object>(aKwargs)->items.empty() ? aKwargs : nullptr);
            }

            template <std::size_t N>
            object* find_method(method_entry const (&aMethods)[N], object* aSelf, char const* aName)
            {
                for (auto const& m : aMethods)
                    if (std::strcmp(m.name, aName) == 0)
                    {
                        auto const f = make_function(m.name, call_method_entry, const_cast<method_entry*>(&m), aSelf);
                        if (f != nullptr)
                            as<function_object>(f)->doc = m.doc;
                        return f;
                    }
                return nullptr;
            }

            // (an unbound method of a type (e.g. ContextVar.get): of its instance and arguments)
            template <std::size_t N>
            object* find_unbound(method_entry const (&aMethods)[N], char const* aName, bool (*aCheck)(object*), char const* aType)
            {
                for (auto const& m : aMethods)
                    if (std::strcmp(m.name, aName) == 0)
                    {
                        struct unbound { method_entry const* entry; bool (*check)(object*); char const* type; };
                        static std::vector<std::unique_ptr<unbound>> sUnbound;
                        sUnbound.push_back(std::make_unique<unbound>(unbound{ &m, aCheck, aType }));
                        auto const f = make_function(m.name, [](neos_py_context*, void* aData, object* aArgs, object* aKwargs) -> object*
                        {
                            auto const& u = *static_cast<unbound const*>(aData);
                            auto const& items = as<tuple_object>(aArgs)->items;
                            if (items.empty())
                                return error("TypeError", std::string{ "unbound method " } + u.type + "." + u.entry->name + "() needs an argument");
                            if (!u.check(items[0]))
                                return error("TypeError", std::string{ "descriptor '" } + u.entry->name + "' for '" + u.type + "' objects doesn't apply to a '" +
                                    type_name(items[0]) + "' object");
                            std::vector<object*> const args{ items.begin() + 1, items.end() };
                            return u.entry->function(items[0], args, aKwargs != nullptr && !as<dict_object>(aKwargs)->items.empty() ? aKwargs : nullptr);
                        }, sUnbound.back().get());
                        if (f != nullptr)
                            as<function_object>(f)->doc = m.doc;
                        return f;
                    }
                return nullptr;
            }

            // --- Context

            object* context_lookup_key(object* aKey)
            {
                if (!is_contextvar(aKey))
                    return error("TypeError", "a ContextVar key was expected, got " + repr_text_of(aKey));
                return aKey;
            }

            object* vars_iterator(vars_ptr aVars, int aWhat)
            {
                auto const it = new vars_iterator_object{ { { &sVarsIteratorTypes[aWhat].base } } };
                it->vars = std::move(aVars);
                it->what = aWhat;
                return &it->h.base;
            }

            method_entry const sContextMethods[] =
            {
                { "get", [](object* aSelf, std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("get", aArgs, aKwargs, { "key", "default" }, 1u, 2u, a) || !context_lookup_key(a[0]))
                            return nullptr;
                        auto const& vars = *as_context(aSelf).vars;
                        auto const i = vars.find(a[0]);
                        if (i < vars.items.size())
                            return new_reference(vars.items[i].second);
                        return new_reference(a[1] != nullptr ? a[1] : none());
                    }, "Return the value for `key` if `key` has the value in the context object.\n\nIf `key` does not exist, return `default`.  If `default` is not\n"
                       "given, return None." },
                { "items", [](object* aSelf, std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        return parse_arguments("items", aArgs, aKwargs, {}, 0u, 0u, a) ? vars_iterator(as_context(aSelf).vars, 1) : nullptr;
                    }, "Return all variables and their values in the context object.\n\nThe result is returned as a list of 2-tuples (variable, value)." },
                { "keys", [](object* aSelf, std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        return parse_arguments("keys", aArgs, aKwargs, {}, 0u, 0u, a) ? vars_iterator(as_context(aSelf).vars, 0) : nullptr;
                    }, "Return a list of all variables in the context object." },
                { "values", [](object* aSelf, std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        return parse_arguments("values", aArgs, aKwargs, {}, 0u, 0u, a) ? vars_iterator(as_context(aSelf).vars, 2) : nullptr;
                    }, "Return a list of all variables' values in the context object." },
                { "copy", [](object* aSelf, std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        return parse_arguments("copy", aArgs, aKwargs, {}, 0u, 0u, a) ? new_context(as_context(aSelf).vars) : nullptr;
                    }, "Return a shallow copy of the context object." },
                { "run", [](object* aSelf, std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        if (aArgs.empty())
                            return error("TypeError", "run() missing 1 required positional argument");
                        if (!context_enter(aSelf))
                            return nullptr;
                        std::vector<object*> rest;
                        for (std::size_t i = 1u; i < aArgs.size(); ++i)
                            rest.push_back(new_reference(aArgs[i]));
                        ref const args{ make_tuple(std::move(rest)) };
                        ref result{ call(aArgs[0], args.get(), aKwargs) };
                        // (exited whether or not the callable raised (an exception it raised remains raised))
                        ref pending;
                        if (!result)
                            pending = ref{ take_exception() };
                        bool const exited = context_exit(aSelf);
                        if (pending)
                        {
                            if (exited)
                                reraise_exception(pending.get());
                            return nullptr;
                        }
                        return exited ? result.release_ownership() : nullptr;
                    }, nullptr }
            };

            neos_py_type make_context_type()
            {
                auto type = make_type("_contextvars.Context", "Context", retain_object, release_object<context_object>);
                type.repr = [](neos_py_context*, object* aSelf) -> object* { return make_str("<_contextvars.Context object at " + object_address(aSelf) + ">"); };
                type.getattr = [](neos_py_context*, object* aSelf, char const* aName) -> object*
                {
                    if (auto const m = find_method(sContextMethods, aSelf, aName))
                        return m;
                    return error("AttributeError", std::string{ "'_contextvars.Context' object has no attribute '" } + aName + "'");
                };
                type.getitem = [](neos_py_context*, object* aSelf, object* aKey) -> object*
                {
                    if (!context_lookup_key(aKey))
                        return nullptr;
                    auto const& vars = *as_context(aSelf).vars;
                    auto const i = vars.find(aKey);
                    if (i == vars.items.size())
                        return (raise_key_error(aKey), nullptr);
                    return new_reference(vars.items[i].second);
                };
                type.contains = [](neos_py_context*, object* aSelf, object* aKey) -> int
                {
                    if (!context_lookup_key(aKey))
                        return -1;
                    auto const& vars = *as_context(aSelf).vars;
                    return vars.find(aKey) < vars.items.size() ? 1 : 0;
                };
                type.length = [](neos_py_context*, object* aSelf) -> long long { return static_cast<long long>(as_context(aSelf).vars->items.size()); };
                type.iter = [](neos_py_context*, object* aSelf) -> object* { return vars_iterator(as_context(aSelf).vars, 0); };
                type.compare = [](neos_py_context*, int aOperation, object* aLhs, object* aRhs) -> object*
                {
                    if (!is_context(aLhs) || !is_context(aRhs) || (aOperation != NEOS_PY_EQ && aOperation != NEOS_PY_NE))
                        return new_reference(not_implemented());
                    auto const& l = *as_context(aLhs).vars;
                    auto const& r = *as_context(aRhs).vars;
                    bool same = l.items.size() == r.items.size();
                    for (std::size_t i = 0u; same && i < l.items.size(); ++i)
                    {
                        auto const j = r.find(l.items[i].first);
                        if (j == r.items.size())
                            same = false;
                        else
                        {
                            auto const e = equal(l.items[i].second, r.items[j].second);
                            if (e < 0)
                                return nullptr;
                            same = e == 1;
                        }
                    }
                    return make_bool(aOperation == NEOS_PY_EQ ? same : !same);
                };
                type.hash = [](neos_py_context*, object*, long long*) -> int
                {
                    error("TypeError", "unhashable type: '_contextvars.Context'");
                    return -1;
                };
                return type;
            }

            object* context_new(neos_py_context*, void*, object* aArgs, object* aKwargs)
            {
                if (!as<tuple_object>(aArgs)->items.empty() || (aKwargs != nullptr && !as<dict_object>(aKwargs)->items.empty()))
                    return error("TypeError", "Context() does not accept any arguments");
                return new_context(empty_vars());
            }

            // --- ContextVar

            method_entry const sContextVarMethods[] =
            {
                { "get", [](object* aSelf, std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("get", aArgs, aKwargs, { "default" }, 0u, 1u, a))
                            return nullptr;
                        if (auto const value = var_value(aSelf))
                            return new_reference(value);
                        if (a[0] != nullptr)
                            return new_reference(a[0]);
                        if (auto const d = as_contextvar(aSelf).defaultValue)
                            return new_reference(d);
                        return lookup_error(aSelf);
                    }, "Return a value for the context variable for the current context.\n\nIf there is no value for the variable in the current context, the\n"
                       "method will:\n * return the value of the default argument of the method, if\n   provided; or\n * return the default value for the context "
                       "variable, if it was\n   created with one; or\n * raise a LookupError." },
                { "set", [](object* aSelf, std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("set", aArgs, aKwargs, { "value" }, 1u, 1u, a))
                            return nullptr;
                        auto const context = current_context();
                        ref const token{ make_token(context, aSelf, var_value(aSelf)) };
                        if (!var_assign(aSelf, a[0]))
                            return nullptr;
                        return new_reference(token.get());
                    }, "Call to set a new value for the context variable in the current context.\n\nThe required value argument is the new value for the context\n"
                       "variable.\n\nReturns a Token object that can be used to restore the variable to\nits previous value via the `ContextVar.reset()` method." },
                { "reset", [](object* aSelf, std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("reset", aArgs, aKwargs, { "token" }, 1u, 1u, a))
                            return nullptr;
                        if (!is_token(a[0]))
                            return error("TypeError", "expected an instance of Token, got " + repr_text_of(a[0]));
                        auto& t = as_token(a[0]);
                        if (t.used)
                            return error("RuntimeError", repr_text_of(a[0]) + " has already been used once");
                        if (t.var != aSelf)
                            return error("ValueError", repr_text_of(a[0]) + " was created by a different ContextVar");
                        if (current_context() != t.context)
                            return error("ValueError", repr_text_of(a[0]) + " was created in a different Context");
                        t.used = true;
                        return var_assign(aSelf, t.oldValue) ? new_reference(none()) : nullptr;
                    }, "Reset the context variable.\n\nThe variable is reset to the value it had before the\n`ContextVar.set()` that created the token was used." }
            };

            neos_py_type make_contextvar_type()
            {
                auto type = make_type("_contextvars.ContextVar", "ContextVar", retain_object, release_object<contextvar_object>);
                type.repr = [](neos_py_context*, object* aSelf) -> object*
                {
                    auto const& v = as_contextvar(aSelf);
                    std::string name, defaultText;
                    if (!repr_of(v.name, name) || (v.defaultValue != nullptr && !repr_of(v.defaultValue, defaultText)))
                        return nullptr;
                    return make_str("<ContextVar name=" + name + (v.defaultValue != nullptr ? " default=" + defaultText : std::string{}) + " at " +
                        object_address(aSelf) + ">");
                };
                type.getattr = [](neos_py_context*, object* aSelf, char const* aName) -> object*
                {
                    if (std::strcmp(aName, "name") == 0)
                        return new_reference(as_contextvar(aSelf).name);
                    if (auto const m = find_method(sContextVarMethods, aSelf, aName))
                        return m;
                    return error("AttributeError", std::string{ "'_contextvars.ContextVar' object has no attribute '" } + aName + "'");
                };
                type.setattr = [](neos_py_context*, object*, char const* aName, object*) -> int
                {
                    if (std::strcmp(aName, "name") == 0)
                        error("AttributeError", "readonly attribute");
                    else
                        error("AttributeError", std::string{ "'_contextvars.ContextVar' object has no attribute '" } + aName + "' and no __dict__ for setting new attributes");
                    return -1;
                };
                type.hash = [](neos_py_context*, object* aSelf, long long* aResult) -> int
                {
                    *aResult = as_contextvar(aSelf).hash;
                    return 0;
                };
                return type;
            }

            object* contextvar_new(neos_py_context*, void*, object* aArgs, object* aKwargs)
            {
                auto const& args = as<tuple_object>(aArgs)->items;
                if (args.size() != 1u)
                    return error("TypeError", args.empty() ? "ContextVar() takes exactly 1 positional argument (0 given)" :
                        "ContextVar() takes at most 1 positional argument (" + std::to_string(args.size()) + " given)");
                object* defaultValue = nullptr;
                if (aKwargs != nullptr)
                    for (auto const& [key, value] : as<dict_object>(aKwargs)->items)
                    {
                        if (kind_of(key) == NEOS_PY_STR && as<str_object>(key)->value == "default")
                            defaultValue = value;
                        else
                            return error("TypeError", "ContextVar() got an unexpected keyword argument '" + (kind_of(key) == NEOS_PY_STR ? as<str_object>(key)->value :
                                std::string{}) + "'");
                    }
                if (kind_of(args[0]) != NEOS_PY_STR)
                    return error("TypeError", "context variable name must be a str");
                long long nameHash = 0;
                if (!hash_of(args[0], nameHash))
                    return nullptr;
                auto const v = new contextvar_object{ { { &sContextVarType.base } } };
                v->name = new_reference(args[0]);
                if (defaultValue != nullptr)
                    v->defaultValue = new_reference(defaultValue);
                // (as CPython's: the hash of its address (_Py_HashPointer: rotated by 4 bits) xor its name's)
                auto const address = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(v));
                auto const h = static_cast<long long>((address >> 4) | (address << 60)) ^ nameHash;
                v->hash = h == -1 ? -2 : h;
                return &v->h.base;
            }

            // --- Token

            neos_py_type make_token_type()
            {
                auto type = make_type("_contextvars.Token", "Token", retain_object, release_object<token_object>);
                type.repr = [](neos_py_context*, object* aSelf) -> object*
                {
                    auto const& t = as_token(aSelf);
                    std::string var;
                    if (!repr_of(t.var, var))
                        return nullptr;
                    return make_str(std::string{ "<Token" } + (t.used ? " used" : "") + " var=" + var + " at " + object_address(aSelf) + ">");
                };
                type.getattr = [](neos_py_context*, object* aSelf, char const* aName) -> object*
                {
                    auto const& t = as_token(aSelf);
                    if (std::strcmp(aName, "var") == 0)
                        return new_reference(t.var);
                    if (std::strcmp(aName, "old_value") == 0)
                        return new_reference(t.oldValue != nullptr ? t.oldValue : &sMissing.base);
                    if (std::strcmp(aName, "MISSING") == 0)
                        return new_reference(&sMissing.base);
                    return error("AttributeError", std::string{ "'_contextvars.Token' object has no attribute '" } + aName + "'");
                };
                type.hash = [](neos_py_context*, object*, long long*) -> int
                {
                    error("TypeError", "unhashable type: '_contextvars.Token'");
                    return -1;
                };
                return type;
            }

            object* token_new(neos_py_context*, void*, object*, object*)
            {
                return error("RuntimeError", "Tokens can only be created by ContextVars");
            }

            neos_py_type make_missing_type()
            {
                auto type = make_type("Token.MISSING", "Token.MISSING", retain_static, release_static);
                type.repr = [](neos_py_context*, object*) -> object* { return make_str("<Token.MISSING>"); };
                return type;
            }

            // --- the iterators of a context's keys, items and values

            neos_py_type make_vars_iterator_type(char const* aName)
            {
                auto type = make_type(aName, aName, retain_object, release_object<vars_iterator_object>);
                type.iter = [](neos_py_context*, object* aSelf) -> object* { return new_reference(aSelf); };
                type.next = [](neos_py_context*, object* aSelf) -> object*
                {
                    auto& it = *reinterpret_cast<vars_iterator_object*>(aSelf);
                    if (it.index >= it.vars->items.size())
                        return nullptr;
                    auto const& [key, value] = it.vars->items[it.index++];
                    if (it.what == 0)
                        return new_reference(key);
                    if (it.what == 2)
                        return new_reference(value);
                    return make_tuple({ new_reference(key), new_reference(value) });
                };
                type.length = [](neos_py_context*, object* aSelf) -> long long
                {
                    auto const& it = *reinterpret_cast<vars_iterator_object*>(aSelf);
                    return static_cast<long long>(it.vars->items.size() - std::min(it.index, it.vars->items.size()));
                };
                return type;
            }

            // --- the module

            int context_instancecheck(neos_py_context*, void*, object* aObject) { return is_context(aObject) ? 1 : 0; }
            int contextvar_instancecheck(neos_py_context*, void*, object* aObject) { return is_contextvar(aObject) ? 1 : 0; }
            int token_instancecheck(neos_py_context*, void*, object* aObject) { return is_token(aObject) ? 1 : 0; }

            builtin_entry const sContextvarsFunctions[] =
            {
                { "copy_context", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("copy_context", aArgs, aKwargs, {}, 0u, 0u, a))
                            return nullptr;
                        return new_context(as_context(current_context()).vars);
                    } }
            };

            bool contextvars_attributes(std::vector<std::pair<std::string, object*>>& aAttributes, char const* aModule)
            {
                static bool const sRegistered = []()
                {
                    for (auto const& [qualified, type] : { std::pair{ "_contextvars.Context", &sContextType }, std::pair{ "_contextvars.ContextVar", &sContextVarType },
                        std::pair{ "_contextvars.Token", &sTokenType } })
                    {
                        observed_types().try_emplace(qualified, type);
                        register_final_type(qualified);
                    }
                    register_type_attributes("_contextvars.Context", [](char const* aName) -> object*
                        { return find_unbound(sContextMethods, aName, is_context, "_contextvars.Context"); });
                    register_type_attributes("_contextvars.ContextVar", [](char const* aName) -> object*
                        { return find_unbound(sContextVarMethods, aName, is_contextvar, "_contextvars.ContextVar"); });
                    register_type_attributes("_contextvars.Token", [](char const* aName) -> object*
                        { return std::strcmp(aName, "MISSING") == 0 ? new_reference(&sMissing.base) : nullptr; });
                    return true;
                }();
                (void)sRegistered;
                aAttributes.emplace_back("Context", context()->type_new(context(), "Context", "_contextvars.Context", context_new, nullptr, context_instancecheck));
                aAttributes.emplace_back("ContextVar", context()->type_new(context(), "ContextVar", "_contextvars.ContextVar", contextvar_new, nullptr,
                    contextvar_instancecheck));
                aAttributes.emplace_back("Token", context()->type_new(context(), "Token", "_contextvars.Token", token_new, nullptr, token_instancecheck));
                for (auto const& f : sContextvarsFunctions)
                {
                    aAttributes.emplace_back(f.name, make_builtin(f));
                    as<function_object>(aAttributes.back().second)->module = "_contextvars";
                }
                if (std::strcmp(aModule, "contextvars") == 0)
                    aAttributes.emplace_back("__all__", make_tuple({ make_str("Context"), make_str("ContextVar"), make_str("Token"), make_str("copy_context") }));
                else
                    aAttributes.emplace_back("__doc__", make_str("Context Variables"));
                return !failed();
            }

            inline bool const sContextvarsC = register_module("_contextvars", [](std::vector<std::pair<std::string, object*>>& aAttributes)
                { return contextvars_attributes(aAttributes, "_contextvars"); });
            inline bool const sContextvars = register_module("contextvars", [](std::vector<std::pair<std::string, object*>>& aAttributes)
                { return contextvars_attributes(aAttributes, "contextvars"); });
        }

        // (the current context: for asyncio's tasks and loops (its callbacks run in their contexts), as CPython's PyContext_CopyCurrent,
        // PyContext_Enter and PyContext_Exit)
        object* context_copy_current()
        {
            return new_context(as_context(current_context()).vars);
        }
        bool context_is(object* aObject)
        {
            return is_context(aObject);
        }
        bool context_run_enter(object* aContext)
        {
            if (!is_context(aContext))
                return (error("TypeError", "an instance of Context was expected"), false);
            return context_enter(aContext);
        }
        bool context_run_exit(object* aContext)
        {
            if (!is_context(aContext))
                return (error("TypeError", "an instance of Context was expected"), false);
            return context_exit(aContext);
        }
    }
}
