/*
  values.cpp

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

  Portions of this file are derived from CPython 3.13 (Python/ceval.c), Copyright (c) 2001-2024 Python Software
  Foundation; All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from C to C++ for the neos Python runtime plugin and adapted to its
  object model (see runtime/python/NOTICE).
*/

// Functions whose bodies are C++ (values.hpp's make_native_function): e.g. the closures that the modules of the standard library that CPython 
// implements in Python make

#include "values.hpp"

namespace neos::python_runtime
{
    namespace detail
    {
        namespace
        {
            struct native_function_object
            {
                header h;
                std::string name;
                std::string qualname;
                std::string doc;
                std::string module;
                native_body body;
                object* dict = nullptr;     ///< (its other attributes (e.g. __wrapped__): a dict, made when one is set)
                std::shared_ptr<native_signature const> signature;     ///< (its parameters, if known)
                bound_body bound;           ///< (a function that binds its arguments itself (body is then empty))
                object* code = nullptr;     ///< (its __code__, once made)
                ~native_function_object() { release(dict); release(code); }
            };

            // --- the code objects of native functions (their __code__: their parameters (co_varnames, co_argcount, ..., co_flags), as a Python 
            // function's code's are; their source is not Python's: co_filename is "<native>")

            struct native_code_object
            {
                header h;
                std::string name;
                std::string qualname;
                std::shared_ptr<native_signature const> signature;
            };

            neos_py_type make_native_code_type()
            {
                auto type = make_type("code", "code", retain_object, release_object<native_code_object>);
                type.repr = [](neos_py_context*, object* aSelf) -> object*
                {
                    auto const& self = *reinterpret_cast<native_code_object*>(aSelf);
                    return make_str("<code object " + self.name + " at " + object_address(aSelf) + ", file \"<native>\", line 0>");
                };
                type.getattr = [](neos_py_context*, object* aSelf, char const* aName) -> object*
                {
                    auto const& self = *reinterpret_cast<native_code_object*>(aSelf);
                    auto const& sig = *self.signature;
                    std::string_view const name{ aName };
                    long long positional = 0;
                    long long positionalOnly = 0;
                    long long keywordOnly = 0;
                    for (auto const& p : sig.parameters)
                        (p.kind == py_parameter::KeywordOnly ? keywordOnly : positional) += 1, positionalOnly += p.kind == py_parameter::PositionalOnly ? 1 : 0;
                    if (name == "co_name")
                        return make_str(self.name);
                    if (name == "co_qualname")
                        return make_str(self.qualname);
                    if (name == "co_filename")
                        return make_str("<native>");
                    if (name == "co_firstlineno")
                        return make_int(0);
                    if (name == "co_argcount")
                        return make_int(positional);
                    if (name == "co_posonlyargcount")
                        return make_int(positionalOnly);
                    if (name == "co_kwonlyargcount")
                        return make_int(keywordOnly);
                    if (name == "co_flags")
                        return make_int(0x3 | (sig.varArgs ? 0x4 : 0) | (sig.varKeywords ? 0x8 : 0) | static_cast<long long>(sig.flags));
                    if (name == "co_varnames" || name == "co_nlocals")
                    {
                        std::vector<object*> names;
                        for (auto const& p : sig.parameters)
                            if (p.kind != py_parameter::KeywordOnly)
                                names.push_back(make_str(p.name));
                        for (auto const& p : sig.parameters)
                            if (p.kind == py_parameter::KeywordOnly)
                                names.push_back(make_str(p.name));
                        if (sig.varArgs)
                            names.push_back(make_str("args"));
                        if (sig.varKeywords)
                            names.push_back(make_str("kwargs"));
                        if (name == "co_nlocals")
                        {
                            auto const count = static_cast<long long>(names.size());
                            for (auto n : names)
                                release(n);
                            return make_int(count);
                        }
                        return make_tuple(std::move(names));
                    }
                    if (name == "co_freevars" || name == "co_cellvars")
                        return make_tuple();
                    return nullptr;
                };
                return type;
            }
            neos_py_type sNativeCodeType = make_native_code_type();

            native_function_object& as_native_function(object* aObject) { return *reinterpret_cast<native_function_object*>(aObject); }

            neos_py_type make_native_function_type()
            {
                auto type = make_type("function", "function", retain_object, release_object<native_function_object>);
                type.repr = [](neos_py_context*, object* aSelf) -> object*
                {
                    return make_str("<function " + as_native_function(aSelf).qualname + " at " + object_address(aSelf) + ">");
                };
                type.call = [](neos_py_context*, object* aSelf, object* aArgs, object* aKwargs) -> object*
                {
                    auto& self = as_native_function(aSelf);
                    if (self.bound)
                        return bound_function_call(aSelf, nullptr, as<tuple_object>(aArgs)->items, aKwargs);
                    std::vector<py> args;
                    for (auto a : as<tuple_object>(aArgs)->items)
                        args.push_back(py::borrowed(a));
                    ref const owner = ref::borrowed(aSelf);
                    return py_guarded([&]() { return self.body(args, aKwargs); });
                };
                type.getattr = [](neos_py_context*, object* aSelf, char const* aName) -> object*
                {
                    auto& self = as_native_function(aSelf);
                    std::string_view const name{ aName };
                    if (self.dict != nullptr)
                    {
                        ref const key{ make_str(aName) };
                        auto const at = dict_find(self.dict, key.get());
                        if (at >= 0)
                            return new_reference(as<dict_object>(self.dict)->items[static_cast<std::size_t>(at)].second);
                    }
                    if (name == "__dict__")
                    {
                        if (self.dict == nullptr)
                            self.dict = make_dict();
                        return new_reference(self.dict);
                    }
                    if (name == "__name__")
                        return make_str(self.name);
                    if (name == "__qualname__")
                        return make_str(self.qualname);
                    if (name == "__doc__")
                        return self.doc.empty() ? new_reference(none()) : make_str(self.doc);
                    if (name == "__module__")
                        return make_str(self.module);
                    if (name == "__defaults__" || name == "__kwdefaults__")
                    {
                        // (its parameters' defaults: the positional parameters' (a tuple), the keyword-only parameters' (a dict); None: none)
                        std::vector<object*> defaults;
                        ref kwdefaults;
                        if (self.signature)
                            for (auto const& p : self.signature->parameters)
                                if (!p.defaultValue.empty())
                                {
                                    if (p.kind != py_parameter::KeywordOnly)
                                        defaults.push_back(p.defaultValue.reference());
                                    else
                                    {
                                        if (!kwdefaults)
                                            kwdefaults = ref{ make_dict() };
                                        ref const key{ make_str(p.name) };
                                        dict_set(kwdefaults.get(), key.get(), p.defaultValue.get());
                                    }
                                }
                        if (name == "__kwdefaults__")
                            return kwdefaults ? kwdefaults.release_ownership() : new_reference(none());
                        return defaults.empty() ? new_reference(none()) : make_tuple(std::move(defaults));
                    }
                    if (name == "__code__" && self.signature)
                    {
                        if (self.code == nullptr)
                            self.code = &(new native_code_object{ { { &sNativeCodeType.base } }, self.name, self.qualname, self.signature })->h.base;
                        return new_reference(self.code);
                    }
                    if (name == "__annotations__" && self.signature)
                    {
                        if (self.dict == nullptr)
                            self.dict = make_dict();
                        ref annotations{ make_dict() };
                        ref const key{ make_str("__annotations__") };
                        dict_set(self.dict, key.get(), annotations.get());
                        return annotations.release_ownership();
                    }
                    return nullptr;
                };
                type.setattr = [](neos_py_context*, object* aSelf, char const* aName, object* aValue) -> int
                {
                    auto& self = as_native_function(aSelf);
                    std::string_view const name{ aName };
                    if (name == "__name__" || name == "__qualname__" || name == "__module__" || name == "__doc__")
                    {
                        if (aValue == nullptr)
                            return (error("TypeError", std::string{ aName } + " must be set to a string object"), -1);
                        std::string text;
                        if (kind_of(aValue) == NEOS_PY_STR)
                            text = as<str_object>(aValue)->value;
                        else if (name != "__doc__" || kind_of(aValue) != NEOS_PY_NONE)
                        {
                            if (name == "__module__" || name == "__doc__")
                            {
                                if (!str_of(aValue, text))
                                    return -1;
                            }
                            else
                                return (error("TypeError", std::string{ aName } + " must be set to a string object"), -1);
                        }
                        (name == "__name__" ? self.name : name == "__qualname__" ? self.qualname : name == "__module__" ? self.module : self.doc) = text;
                        return 0;
                    }
                    if (self.dict == nullptr)
                        self.dict = make_dict();
                    ref const key{ make_str(aName) };
                    if (aValue == nullptr)
                    {
                        if (dict_find(self.dict, key.get()) < 0)
                            return (error("AttributeError", std::string{ "'function' object has no attribute '" } + aName + "'"), -1);
                        ref const r{ call_method_named(self.dict, "__delitem__", { new_reference(key.get()) }) };
                        return r ? 0 : -1;
                    }
                    return dict_set(self.dict, key.get(), aValue);
                };
                return type;
            }

            neos_py_type sNativeFunctionType = make_native_function_type();
            // (a Python function: a class's attribute that is one is bound to an instance as a method)
            bool const sNativeFunctionBinds = (method_types().insert(&sNativeFunctionType.base), true);
        }

        namespace
        {
            std::string quoted_names(std::vector<std::string> const& aNames)
            {
                // ('a'; 'a' and 'b'; 'a', 'b', and 'c')
                std::string result;
                for (std::size_t i = 0u; i < aNames.size(); ++i)
                {
                    if (i > 0u)
                        result += aNames.size() == 2u ? " and " : i + 1u == aNames.size() ? ", and " : ", ";
                    result += "'" + aNames[i] + "'";
                }
                return result;
            }
        }

        std::vector<py> py_bind(std::string const& aQualname, std::vector<py_parameter> const& aParameters, std::vector<py> const& aArgs, object* aKwargs,
            py* aRest, py* aExtra)
        {
            std::size_t positionalOnly = 0u;
            std::size_t argcount = 0u;
            for (auto const& p : aParameters)
            {
                if (p.kind == py_parameter::PositionalOnly)
                    ++positionalOnly;
                if (p.kind != py_parameter::KeywordOnly)
                    ++argcount;
            }
            std::vector<py> result(aParameters.size());
            for (std::size_t i = 0u; i < aArgs.size() && i < argcount; ++i)
                result[i] = aArgs[i];
            if (aRest != nullptr)
                *aRest = py_tuple(aArgs.size() > argcount ? std::vector<py>{ aArgs.begin() + static_cast<std::ptrdiff_t>(argcount), aArgs.end() } : std::vector<py>{});
            if (aExtra != nullptr)
                *aExtra = py_dict();
            if (aKwargs != nullptr)
                for (auto const& item : as<dict_object>(aKwargs)->items)
                {
                    auto const& name = as<str_object>(item.first)->value;
                    std::size_t i = positionalOnly;
                    while (i < aParameters.size() && aParameters[i].name != name)
                        ++i;
                    if (i == aParameters.size())
                    {
                        if (aExtra != nullptr)
                        {
                            aExtra->set_item(py::borrowed(item.first), py::borrowed(item.second));
                            continue;
                        }
                        std::vector<std::string> passed;
                        for (auto const& other : as<dict_object>(aKwargs)->items)
                            for (std::size_t j = 0u; j < positionalOnly; ++j)
                                if (aParameters[j].name == as<str_object>(other.first)->value)
                                    passed.push_back(aParameters[j].name);
                        if (!passed.empty())
                        {
                            std::string names;
                            for (auto const& n : passed)
                                names += (names.empty() ? "" : ", ") + n;
                            py_raise("TypeError", aQualname + "() got some positional-only arguments passed as keyword arguments: '" + names + "'");
                        }
                        std::string message = aQualname + "() got an unexpected keyword argument '" + name + "'";
                        if (aParameters.size() > positionalOnly)
                        {
                            std::vector<py> candidates;
                            for (std::size_t j = positionalOnly; j < aParameters.size(); ++j)
                                candidates.emplace_back(aParameters[j].name);
                            py const suggestion = import_attribute("_suggestions", "_generate_suggestions")(py_list(candidates), py{ name });
                            if (!suggestion.is_none())
                                message += ". Did you mean '" + suggestion.str() + "'?";
                        }
                        py_raise("TypeError", message);
                    }
                    if (!result[i].empty())
                        py_raise("TypeError", aQualname + "() got multiple values for argument '" + name + "'");
                    result[i] = py::borrowed(item.second);
                }
            if (aArgs.size() > argcount && aRest == nullptr)
            {
                std::size_t defaults = 0u;
                for (std::size_t i = 0u; i < argcount; ++i)
                    if (!aParameters[i].defaultValue.empty())
                        ++defaults;
                std::size_t keywordOnlyGiven = 0u;
                for (std::size_t i = argcount; i < aParameters.size(); ++i)
                    if (!result[i].empty())
                        ++keywordOnlyGiven;
                std::string const takes = defaults > 0u ? "from " + std::to_string(argcount - defaults) + " to " + std::to_string(argcount) : std::to_string(argcount);
                bool const plural = defaults > 0u || argcount != 1u;
                std::string given = std::to_string(aArgs.size());
                if (keywordOnlyGiven > 0u)
                    given += std::string{ " positional argument" } + (aArgs.size() == 1u ? "" : "s") + " (and " + std::to_string(keywordOnlyGiven) +
                        " keyword-only argument" + (keywordOnlyGiven == 1u ? "" : "s") + ")";
                py_raise("TypeError", aQualname + "() takes " + takes + " positional argument" + (plural ? "s" : "") + " but " + given +
                    (aArgs.size() == 1u && keywordOnlyGiven == 0u ? " was" : " were") + " given");
            }
            std::vector<std::string> missing;
            for (std::size_t i = 0u; i < argcount; ++i)
                if (result[i].empty())
                {
                    if (aParameters[i].defaultValue.empty())
                        missing.push_back(aParameters[i].name);
                    else
                        result[i] = aParameters[i].defaultValue;
                }
            if (!missing.empty())
                py_raise("TypeError", aQualname + "() missing " + std::to_string(missing.size()) + " required positional argument" + (missing.size() == 1u ? "" : "s") +
                    ": " + quoted_names(missing));
            for (std::size_t i = argcount; i < aParameters.size(); ++i)
                if (result[i].empty())
                {
                    if (aParameters[i].defaultValue.empty())
                        missing.push_back(aParameters[i].name);
                    else
                        result[i] = aParameters[i].defaultValue;
                }
            if (!missing.empty())
                py_raise("TypeError", aQualname + "() missing " + std::to_string(missing.size()) + " required keyword-only argument" + (missing.size() == 1u ? "" : "s") +
                    ": " + quoted_names(missing));
            return result;
        }

        py make_native_function(std::string aName, std::string aQualname, std::string aDoc, std::string aModule, native_body aBody)
        {
            return py{ &(new native_function_object{ { { &sNativeFunctionType.base } }, std::move(aName), std::move(aQualname), std::move(aDoc), std::move(aModule),
                std::move(aBody) })->h.base };
        }
        py make_bound_function(std::string aName, std::string aQualname, std::string aDoc, std::string aModule, std::shared_ptr<native_signature const> aSignature,
            bound_body aBody)
        {
            auto const f = new native_function_object{ { { &sNativeFunctionType.base } }, std::move(aName), std::move(aQualname), std::move(aDoc), std::move(aModule),
                native_body{} };
            f->signature = std::move(aSignature);
            f->bound = std::move(aBody);
            return py{ &f->h.base };
        }

        bool is_bound_function(object* aFunction)
        {
            return aFunction != nullptr && aFunction->type == &sNativeFunctionType.base && static_cast<bool>(as_native_function(aFunction).bound);
        }

        namespace
        {
            // (the arguments (aSelf, if not null, then aArgs, and aKwargs) bound to a signature's parameters (aOut: one for each, then *args 
            // and **kwargs, if it has them): the positional arguments (and their defaults) without a vector made for them; py_bind's otherwise 
            // (keyword arguments, or an error's message))
            void bind_arguments(std::string const& aQualname, native_signature const& aSignature, object* aSelf, std::span<object* const> aArgs, object* aKwargs,
                py* aOut)
            {
                auto const& parameters = aSignature.parameters;
                std::size_t const given = (aSelf != nullptr ? 1u : 0u) + aArgs.size();
                auto const argument = [&](std::size_t i) { return aSelf != nullptr ? (i == 0u ? aSelf : aArgs[i - 1u]) : aArgs[i]; };
                if (aKwargs == nullptr || as<dict_object>(aKwargs)->items.empty())
                {
                    std::size_t argcount = 0u;
                    while (argcount < parameters.size() && parameters[argcount].kind != py_parameter::KeywordOnly)
                        ++argcount;
                    if (given <= argcount || aSignature.varArgs)
                    {
                        bool complete = true;
                        for (std::size_t i = 0u; i < parameters.size() && complete; ++i)
                        {
                            if (i < argcount && i < given)
                                aOut[i] = py::borrowed(argument(i));
                            else if (!parameters[i].defaultValue.empty())
                                aOut[i] = parameters[i].defaultValue;
                            else
                                complete = false;
                        }
                        if (complete)
                        {
                            auto next = parameters.size();
                            if (aSignature.varArgs)
                            {
                                std::vector<object*> rest;
                                for (std::size_t i = argcount; i < given; ++i)
                                    rest.push_back((retain(argument(i)), argument(i)));
                                aOut[next++] = py{ make_tuple(std::move(rest)) };
                            }
                            if (aSignature.varKeywords)
                                aOut[next++] = py_dict();
                            return;
                        }
                    }
                }
                std::vector<py> args;
                args.reserve(given);
                for (std::size_t i = 0u; i < given; ++i)
                    args.push_back(py::borrowed(argument(i)));
                py rest;
                py extra;
                auto bound = py_bind(aQualname, parameters, args, aKwargs, aSignature.varArgs ? &rest : nullptr, aSignature.varKeywords ? &extra : nullptr);
                auto next = parameters.size();
                for (std::size_t i = 0u; i < next; ++i)
                    aOut[i] = std::move(bound[i]);
                if (aSignature.varArgs)
                    aOut[next++] = rest;
                if (aSignature.varKeywords)
                    aOut[next++] = extra;
            }
        }

        object* bound_function_call(object* aFunction, object* aSelf, std::span<object* const> aArgs, object* aKwargs)
        {
            auto& f = as_native_function(aFunction);
            ref const owner = ref::borrowed(aFunction);
            auto const& signature = *f.signature;
            return py_guarded([&]()
            {
                std::size_t const count = signature.parameters.size() + (signature.varArgs ? 1u : 0u) + (signature.varKeywords ? 1u : 0u);
                constexpr std::size_t kSmall = 12u;
                py small[kSmall];
                std::vector<py> large;
                py* out = small;
                if (count > kSmall)
                {
                    large.resize(count);
                    out = large.data();
                }
                bind_arguments(f.qualname, signature, aSelf, aArgs, aKwargs, out);
                return f.bound(arguments{ out, count });
            });
        }

        py make_native_function(std::string aName, std::string aQualname, std::string aDoc, std::string aModule, native_body aBody,
            std::shared_ptr<native_signature const> aSignature)
        {
            auto const f = new native_function_object{ { { &sNativeFunctionType.base } }, std::move(aName), std::move(aQualname), std::move(aDoc), std::move(aModule),
                std::move(aBody) };
            f->signature = std::move(aSignature);
            return py{ &f->h.base };
        }
    }
}
