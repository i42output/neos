/*
  eval.cpp

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

// eval, the Function constructor (and its generator and async variants) and $262.evalScript: source compiled while the program
// runs (neos_rt_host::load), as the compiler's eval code (see the compiler's set_options)
//
// Names found at run time: code in (or within) a with statement, code that a sloppy direct eval can add var bindings to, and eval
// code itself refer to the bindings that may hold them by name (javascript::dyn_get etc.): the environments of the scopes that enclose
// such code have their bindings' names (env_names); a with statement's environment has its object; a var scope's environment has the
// var bindings that sloppy direct evals in it declared (its extension)

#include <map>
#include "js.hpp"
#include "api.hpp"

namespace neos::javascript_runtime
{
    namespace
    {
        // (an environment's bindings' names (slot order) and kinds: v var, l let (or class), c const, f function, p parameter, k catch
        // parameter, n a named function expression's own name, i internal (never found by name))
        class environment_names : public cell
        {
        public:
            environment_names() : cell{ cell_type::Internal } {}
        public:
            bool varScope = false;
            std::vector<std::pair<value, char>> entries;
            std::vector<std::pair<value, value>> extension;     ///< (a var scope: the var bindings that sloppy direct evals declared)
        };

        environment_names* names_of(environment& aEnvironment)
        {
            return aEnvironment.names != Undefined ? &cell_of<environment_names>(aEnvironment.names) : nullptr;
        }

        // (a binding found by name)
        struct binding_reference
        {
            enum kind_t { None, Slot, Extension, Object, GlobalLexical, Global } kind = None;
            environment* env = nullptr;
            std::size_t index = 0u;
            char bindingKind = 'v';
            value object = Undefined;
        };

        bool lexical_kind(char aKind)
        {
            return aKind == 'l' || aKind == 'c';
        }

        binding_reference find_binding(value aEnvironment, value aName)
        {
            binding_reference result;
            for (auto e = aEnvironment; e != Undefined && is_internal(e);)
            {
                auto& env = cell_of<environment>(e);
                if (env.withObject != Undefined)
                {
                    // (an object environment: its object's property, unless its @@unscopables exclude it)
                    auto const o = env.withObject;
                    if (has_property(o, aName))
                    {
                        auto const unscopables = get(o, the_realm().symUnscopables);
                        if (!is_object(unscopables) || !to_boolean(get(unscopables, aName)))
                        {
                            result.kind = binding_reference::Object;
                            result.object = o;
                            return result;
                        }
                    }
                }
                else if (auto* n = names_of(env))
                {
                    for (std::size_t i = 0u; i < n->entries.size(); ++i)
                        if (n->entries[i].first == aName && n->entries[i].second != 'i')
                        {
                            result.kind = binding_reference::Slot;
                            result.env = &env;
                            result.index = i;
                            result.bindingKind = n->entries[i].second;
                            return result;
                        }
                    for (std::size_t i = 0u; i < n->extension.size(); ++i)
                        if (n->extension[i].first == aName)
                        {
                            result.kind = binding_reference::Extension;
                            result.env = &env;
                            result.index = i;
                            return result;
                        }
                }
                e = env.parent;
            }
            auto& lexicals = global_lexicals();
            if (lexicals.contains(aName))
            {
                result.kind = binding_reference::GlobalLexical;
                return result;
            }
            if (has_property(the_realm().global, aName))
                result.kind = binding_reference::Global;
            return result;
        }

        std::string name_text(value aName)
        {
            return utf8(aName);
        }

        value& slot_of(binding_reference const& aReference)
        {
            if (aReference.kind == binding_reference::Slot)
                return aReference.env->slots[aReference.index];
            return names_of(*aReference.env)->extension[aReference.index].second;
        }

        // (an internal binding (e.g. a derived class constructor's %this) of the environments that aEnvironment encloses)
        value* internal_slot(value aEnvironment, value aName)
        {
            for (auto e = aEnvironment; e != Undefined && is_internal(e);)
            {
                auto& env = cell_of<environment>(e);
                if (env.withObject == Undefined)
                    if (auto* n = names_of(env))
                        for (std::size_t i = 0u; i < n->entries.size(); ++i)
                            if (n->entries[i].first == aName && n->entries[i].second == 'i')
                                return &env.slots[i];
                e = env.parent;
            }
            return nullptr;
        }

        value& derived_this_slot(value aEnvironment)
        {
            auto* slot = internal_slot(aEnvironment, intern(ascii("%this")));
            if (slot == nullptr)
                throw_syntax_error("'super' keyword unexpected here");
            return *slot;
        }

        // (the var scope of eval code that the environment aEnvironment encloses: its environment (null: the global environment); a lexical
        // binding of the same name between them is a SyntaxError)
        environment* var_environment(value aEnvironment, value aName)
        {
            for (auto e = aEnvironment; e != Undefined && is_internal(e);)
            {
                auto& env = cell_of<environment>(e);
                if (auto* n = names_of(env))
                {
                    for (auto const& entry : n->entries)
                        if (entry.first == aName && lexical_kind(entry.second))
                            throw_syntax_error("Identifier '" + name_text(aName) + "' has already been declared");
                    if (n->varScope)
                        return &env;
                }
                e = env.parent;
            }
            if (global_lexicals().contains(aName))
                throw_syntax_error("Identifier '" + name_text(aName) + "' has already been declared");
            return nullptr;
        }

        // --- the context of the eval code being run (the innermost)

        struct eval_context
        {
            value environment = Undefined;
            value thisValue = Undefined;
            value newTarget = Undefined;
            value home = Undefined;
            value result = Undefined;
            bool threw = false;
            bool completed = false;
        };

        thread_local std::vector<eval_context> tContexts;

        // (the programs compiled (the same source with the same options is compiled once))
        std::map<std::pair<std::string, std::string>, void*>& programs()
        {
            static std::map<std::pair<std::string, std::string>, void*> sPrograms;
            return sPrograms;
        }

        void* compile(std::string const& aSource, std::string const& aOptions)
        {
            auto& cache = programs();
            if (auto const existing = cache.find({ aOptions, aSource }); existing != cache.end())
                return existing->second;
            auto const* h = host();
            if (h == nullptr || h->load == nullptr)
                throw_error(the_realm().evalErrorPrototype, "code cannot be compiled while the program runs");
            char const* failure = nullptr;
            auto const program = h->load("javascript", aSource.c_str(), "<eval>", aOptions.c_str(), &failure);
            if (program == nullptr)
            {
                std::string message = failure != nullptr ? failure : "invalid code";
                // ("file(line,column): SyntaxError: message": the message)
                if (auto const at = message.find("SyntaxError: "); at != std::string::npos)
                    message = message.substr(at + 13u);
                while (!message.empty() && (message.back() == '\n' || message.back() == '\r'))
                    message.pop_back();
                throw_syntax_error(message);
            }
            cache.emplace(std::pair{ aOptions, aSource }, program);
            return program;
        }

        value run(void* aProgram, eval_context const& aContext)
        {
            tContexts.push_back(aContext);
            auto& extra = extra_arguments();
            auto const base = extra.size();
            neos_rt_value result{};
            auto const trap = host()->run(aProgram, NEOS_RT_VOID, &result);
            extra.resize(base);
            auto const context = tContexts.back();
            tContexts.pop_back();
            if (trap != nullptr && !context.completed)
            {
                std::string const message{ trap };
                if (message.find("stack") != std::string::npos)
                    throw_range_error("Maximum call stack size exceeded");
                throw_error(the_realm().errorPrototype, "internal error: " + message);
            }
            if (context.threw)
                throw js_exception{ context.result };
            return context.result;
        }

        std::string source_text(value aSource)
        {
            // (UTF-8 (lone surrogates as WTF-8))
            return wtf8(str(aSource));
        }

        value perform_eval(value aSource, std::string const& aOptions, eval_context const& aContext)
        {
            if (!is_string(aSource))
                return aSource;
            return run(compile(source_text(aSource), aOptions), aContext);
        }

        // (%eval%: indirect eval)
        value global_eval(value, arguments aArguments, value, function_object&)
        {
            eval_context context;
            context.thisValue = the_realm().global;
            return perform_eval(argument(aArguments, 0u), "eval-indirect", context);
        }
    }

    value evaluate_script(value aSource)
    {
        eval_context context;
        context.thisValue = the_realm().global;
        return run(compile(source_text(to_string(aSource)), "script-eval"), context);
    }

    value create_dynamic_function(arguments aArguments, value aNewTarget, int aKind)
    {
        // (CreateDynamicFunction: the source "<prefix> anonymous(<parameters>\n) {\n<body>\n}", parsed (as a whole, and checked to be that
        // function))
        auto const& r = the_realm();
        std::u16string parameters;
        for (std::size_t i = 0u; i + 1u < aArguments.size(); ++i)
        {
            if (i > 0u)
                parameters += u",";
            parameters += to_u16string(aArguments[i]);
        }
        std::u16string const body = aArguments.empty() ? std::u16string{} : to_u16string(aArguments.back());
        char const* const prefixes[] = { "function", "function*", "async function", "async function*" };
        std::string const prefix = std::string{ prefixes[aKind] } + " anonymous(";
        std::string const parametersText = wtf8(parameters);
        std::string const bodyText = wtf8(body);
        std::string const sourceText = prefix + parametersText + "\n) {\n" + bodyText + "\n}";
        std::string const source = "(" + sourceText + ")";
        std::size_t const parametersEnd = 1u + prefix.size() + parametersText.size() + 2u;
        std::size_t const bodyBegin = parametersEnd + 1u;
        eval_context context;
        context.thisValue = r.global;
        auto const f = run(compile(source, "function:" + std::to_string(parametersEnd) + ":" + std::to_string(bodyBegin)), context);
        // (its prototype: the new target's (or the realm's intrinsic))
        value realm::* const defaults[] = { &realm::functionPrototype, &realm::generatorFunctionPrototype, &realm::asyncFunctionPrototype,
            &realm::asyncGeneratorFunctionPrototype };
        if (aNewTarget != Undefined)
            obj(f).proto = get_prototype_from_constructor(aNewTarget, defaults[aKind]);
        return f;
    }

    void init_eval(realm& aRealm)
    {
        aRealm.eval = make_native("eval", 1u, global_eval);
        obj(aRealm.global).put(ascii("eval"), aRealm.eval);
    }

    namespace api
    {
        template <typename F>
        value guarded(F&& aFunction)
        {
            try
            {
                return aFunction();
            }
            catch (js_exception const& e)
            {
                pending_exception() = e.thrown;
                return Exception;
            }
        }

        value env_names(value aEnvironment, value aDescriptor)
        {
            // ("V" or "-" (a var scope or not), then ",<kind><name>" for each binding)
            auto& env = cell_of<environment>(aEnvironment);
            static std::unordered_map<value, value> sCache;     // (descriptors (interned literals) -> names)
            if (auto const existing = sCache.find(aDescriptor); existing != sCache.end())
            {
                // (the names are shared; the extension is the environment's own)
                auto const& shared = cell_of<environment_names>(existing->second);
                auto& n = the_heap().make<environment_names>();
                n.varScope = shared.varScope;
                n.entries = shared.entries;
                env.names = value_of(n);
                return Undefined;
            }
            auto const& text = str(aDescriptor);
            auto& n = the_heap().make<environment_names>();
            n.varScope = !text.empty() && text[0] == u'V';
            std::size_t i = 1u;
            while (i < text.size())
            {
                ++i;    // (',')
                auto const end = text.find(u',', i);
                auto const entry = text.substr(i, end == std::u16string::npos ? std::u16string::npos : end - i);
                if (!entry.empty())
                    n.entries.emplace_back(intern(std::u16string_view{ entry }.substr(1u)), static_cast<char>(entry[0]));
                i = end == std::u16string::npos ? text.size() : end;
            }
            auto& shared = the_heap().make<environment_names>();
            shared.varScope = n.varScope;
            shared.entries = n.entries;
            sCache.emplace(aDescriptor, value_of(shared));
            env.names = value_of(n);
            return Undefined;
        }

        value env_with(value aParent, value aObject)
        {
            auto& e = the_heap().make<environment>(aParent, 0u);
            e.withObject = aObject;
            return value_of(e);
        }

        value dyn_get(value aEnvironment, value aName, std::int32_t aTypeof)
        {
            return guarded([&]() -> value
            {
                auto const name = intern(aName);
                auto const b = find_binding(aEnvironment, name);
                switch (b.kind)
                {
                case binding_reference::Slot:
                case binding_reference::Extension:
                    {
                        auto const v = slot_of(b);
                        if (v == Empty)
                            throw_reference_error("Cannot access '" + name_text(name) + "' before initialization");
                        return v;
                    }
                case binding_reference::Object:
                    return get(b.object, name);
                case binding_reference::GlobalLexical:
                    {
                        auto const v = global_lexicals()[name].v;
                        if (v == Empty)
                            throw_reference_error("Cannot access '" + name_text(name) + "' before initialization");
                        return v;
                    }
                case binding_reference::Global:
                    return get(the_realm().global, name);
                default:
                    if (aTypeof != 0)
                        return Undefined;
                    throw_reference_error(name_text(name) + " is not defined");
                }
            });
        }

        value dyn_set(value aEnvironment, value aName, value aValue, std::int32_t aFlags)
        {
            // (aFlags: 1 strict, 2 initialization)
            return guarded([&]() -> value
            {
                bool const strict = (aFlags & 1) != 0;
                bool const initialize = (aFlags & 2) != 0;
                auto const name = intern(aName);
                auto const b = find_binding(aEnvironment, name);
                switch (b.kind)
                {
                case binding_reference::Slot:
                case binding_reference::Extension:
                    {
                        auto& slot = slot_of(b);
                        if (!initialize)
                        {
                            if (slot == Empty)
                                throw_reference_error("Cannot access '" + name_text(name) + "' before initialization");
                            if (b.bindingKind == 'c')
                                throw_type_error("Assignment to constant variable.");
                            if (b.bindingKind == 'n')
                            {
                                if (strict)
                                    throw_type_error("Assignment to constant variable.");
                                return Undefined;
                            }
                        }
                        slot = aValue;
                        return Undefined;
                    }
                case binding_reference::Object:
                    set(b.object, name, aValue, strict);
                    return Undefined;
                case binding_reference::GlobalLexical:
                    {
                        auto& g = global_lexicals()[name];
                        if (!initialize)
                        {
                            if (g.v == Empty)
                                throw_reference_error("Cannot access '" + name_text(name) + "' before initialization");
                            if (g.constant)
                                throw_type_error("Assignment to constant variable.");
                        }
                        g.v = aValue;
                        return Undefined;
                    }
                case binding_reference::Global:
                    set(the_realm().global, name, aValue, strict);
                    return Undefined;
                default:
                    if (strict)
                        throw_reference_error(name_text(name) + " is not defined");
                    set(the_realm().global, name, aValue, false);
                    return Undefined;
                }
            });
        }

        value dyn_delete(value aEnvironment, value aName)
        {
            return guarded([&]() -> value
            {
                auto const name = intern(aName);
                auto const b = find_binding(aEnvironment, name);
                switch (b.kind)
                {
                case binding_reference::Slot:
                case binding_reference::GlobalLexical:
                    return False;
                case binding_reference::Extension:
                    {
                        auto& extension = names_of(*b.env)->extension;
                        extension.erase(extension.begin() + static_cast<std::ptrdiff_t>(b.index));
                        return True;
                    }
                case binding_reference::Object:
                    return boolean(obj(b.object).delete_property(name));
                case binding_reference::Global:
                    return boolean(obj(the_realm().global).delete_property(name));
                default:
                    return True;
                }
            });
        }

        value dyn_this(value aEnvironment, value aName)
        {
            // (a call of a name: its this is the with statement's object that has it, else undefined (the reference's errors are the
            // callee's (dyn_get's)))
            try
            {
                auto const b = find_binding(aEnvironment, intern(aName));
                return b.kind == binding_reference::Object ? b.object : Undefined;
            }
            catch (js_exception const&)
            {
                return Undefined;
            }
        }

        value eval_declare(value aEnvironment, value aName, std::int32_t aFlags)
        {
            // (EvalDeclarationInstantiation of a sloppy direct eval's var or function (aFlags 1) declaration: checked (aFlags 2) or declared)
            return guarded([&]() -> value
            {
                auto const name = intern(aName);
                bool const function = (aFlags & 1) != 0;
                bool const check = (aFlags & 2) != 0;
                auto* varEnv = var_environment(aEnvironment, name);
                if (varEnv == nullptr)
                {
                    auto& g = obj(the_realm().global);
                    auto const existing = g.get_own_property(name);
                    if (check)
                    {
                        if (function && existing && !existing->configurable.value_or(false) &&
                            (existing->is_accessor() || !existing->writable.value_or(false) || !existing->enumerable.value_or(false)))
                            throw_type_error("Cannot redefine global function " + name_text(name));
                        if (!existing && !g.is_extensible())
                            throw_type_error("Cannot define global variable " + name_text(name));
                        return Undefined;
                    }
                    if (!existing && !function)
                        define_property_or_throw(the_realm().global, name, property_descriptor::data(Undefined, AttributesDefault));
                    return Undefined;
                }
                if (check && (aFlags & 4) != 0)
                {
                    // (a direct eval in parameters: a var named as a parameter would be hidden by it)
                    for (auto const& entry : names_of(*varEnv)->entries)
                        if (entry.first == name && entry.second == 'p')
                            throw_syntax_error("Identifier '" + name_text(name) + "' has already been declared");
                }
                if (check)
                    return Undefined;
                auto& n = *names_of(*varEnv);
                for (auto const& entry : n.entries)
                    if (entry.first == name)
                        return Undefined;
                for (auto const& entry : n.extension)
                    if (entry.first == name)
                        return Undefined;
                n.extension.emplace_back(name, Undefined);
                return Undefined;
            });
        }

        value eval_function(value aEnvironment, value aName, value aFunction)
        {
            return guarded([&]() -> value
            {
                auto const name = intern(aName);
                auto* varEnv = var_environment(aEnvironment, name);
                if (varEnv == nullptr)
                {
                    auto const existing = obj(the_realm().global).get_own_property(name);
                    if (!existing || existing->configurable.value_or(false))
                        define_property_or_throw(the_realm().global, name, property_descriptor::data(aFunction, AttributesDefault));
                    else
                        set(the_realm().global, name, aFunction, true);
                    return Undefined;
                }
                auto& n = *names_of(*varEnv);
                for (std::size_t i = 0u; i < n.entries.size(); ++i)
                    if (n.entries[i].first == name)
                    {
                        varEnv->slots[i] = aFunction;
                        return Undefined;
                    }
                for (auto& entry : n.extension)
                    if (entry.first == name)
                    {
                        entry.second = aFunction;
                        return Undefined;
                    }
                n.extension.emplace_back(name, aFunction);
                return Undefined;
            });
        }

        value eval_direct(value aCallee, value aSource, std::int32_t aCount, std::int32_t aFlags, value aEnvironment, value aThis, value aNewTarget,
            value aHome)
        {
            return guarded([&]() -> value
            {
                if (aCallee != the_realm().eval && (aFlags & 0x20) != 0)
                    return api::tail_call(aCallee, Undefined, std::min(aCount, 1), aSource, Undefined, Undefined, Undefined);
                aFlags &= ~0x20;
                if (aCallee != the_realm().eval)
                {
                    // (not %eval%: an ordinary call)
                    if (aCount == 0)
                        return call(aCallee, Undefined);
                    value const argument[] = { aSource };
                    return call(aCallee, Undefined, argument);
                }
                if (aCount == 0)
                    return Undefined;
                eval_context context;
                context.environment = aEnvironment;
                context.thisValue = aThis;
                context.newTarget = aNewTarget;
                context.home = aHome;
                return perform_eval(aSource, "eval:" + std::to_string(aFlags), context);
            });
        }

        value eval_this_derived(value aEnvironment)
        {
            // (this in eval code in a derived class's constructor: the constructor's (once super() has bound it))
            return guarded([&]() -> value
            {
                auto const v = derived_this_slot(aEnvironment);
                if (v == Empty)
                    throw_reference_error("Must call super constructor in derived class before accessing 'this' or returning from derived constructor");
                return v;
            });
        }

        value eval_bind_this(value aEnvironment, value aThis)
        {
            // (super() in eval code in a derived class's constructor: binds the constructor's this (once))
            return guarded([&]() -> value
            {
                auto& slot = derived_this_slot(aEnvironment);
                if (slot != Empty)
                    throw_reference_error("Super constructor may only be called once");
                slot = aThis;
                return Undefined;
            });
        }

        value eval_internal(value aEnvironment, value aName)
        {
            return guarded([&]() -> value
            {
                auto* slot = internal_slot(aEnvironment, intern(aName));
                if (slot == nullptr)
                    throw_syntax_error("'super' keyword unexpected here");
                return *slot;
            });
        }

        value eval_this()
        {
            return tContexts.empty() ? the_realm().global : tContexts.back().thisValue;
        }

        value eval_home()
        {
            return tContexts.empty() ? Undefined : tContexts.back().home;
        }

        value eval_new_target()
        {
            return tContexts.empty() ? Undefined : tContexts.back().newTarget;
        }

        value eval_environment()
        {
            return tContexts.empty() ? Undefined : tContexts.back().environment;
        }

        value eval_complete(value aResult)
        {
            if (tContexts.empty())
                return Undefined;
            auto& context = tContexts.back();
            context.completed = true;
            if (aResult == Exception)
            {
                context.threw = true;
                context.result = pending_exception();
                pending_exception() = Undefined;
            }
            else
                context.result = aResult;
            return Undefined;
        }
    }
}
