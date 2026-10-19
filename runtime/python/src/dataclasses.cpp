/*
  dataclasses.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/dataclasses.py), Copyright (c) 2001-2024 Python Software
  Foundation; All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from Python to C++ for the neos Python runtime plugin and adapted to
  its object model (see runtime/python/NOTICE).
*/

// The module dataclasses (CPython 3.13's dataclasses.py: dataclass, field, Field, FrozenInstanceError, InitVar, KW_ONLY, MISSING, fields, asdict,
// astuple, make_dataclass, replace, is_dataclass); the methods that CPython generates as source and execs (__init__, __repr__, __eq__, the
// ordering methods, a frozen class's __setattr__ and __delattr__, and __hash__) are closures that do what that source does

#include <cctype>
#include <cstring>
#include <functional>
#include <map>
#include <optional>
#include <memory>
#include <set>
#include <string>
#include <vector>
#include "python.hpp"
#include "objects.hpp"
#include "values.hpp"

namespace neos::python_runtime::detail
{
    namespace
    {
        char const* const sModule = "dataclasses";

        // (the module's objects: made once (and never destroyed: they outlive the program's objects))
        struct module_state
        {
            std::map<std::string, py, std::less<>> classes;
            py missing;                 ///< (MISSING)
            py kwOnly;                  ///< (KW_ONLY)
            py hasDefaultFactory;       ///< (_HAS_DEFAULT_FACTORY)
            py emptyMetadata;           ///< (_EMPTY_METADATA)
            py fieldMarker;             ///< (_FIELD)
            py classVarMarker;          ///< (_FIELD_CLASSVAR)
            py initVarMarker;           ///< (_FIELD_INITVAR)
            py atomicTypes;             ///< (_ATOMIC_TYPES)
            py replace;                 ///< (_replace)
            py frozenInstanceError;
        };
        module_state& state()
        {
            static module_state* sState = new module_state{};
            return *sState;
        }

        py cls(char const* aName)
        {
            auto const c = state().classes.find(aName);
            if (c == state().classes.end())
                py_raise("NameError", "name '" + std::string{ aName } + "' is not defined");
            return c->second;
        }
        py none() { return py::none(); }
        py boolean(bool aValue) { return py::boolean(aValue); }

        bool is_str(py const& aValue) { return kind_of(aValue.get()) == NEOS_PY_STR; }

        // (getattr(aObject, aName, aDefault))
        py attr_or(py const& aObject, char const* aName, py const& aDefault)
        {
            try
            {
                return aObject.attr(aName);
            }
            catch (raised const&)
            {
                if (!caught({ "AttributeError" }))
                    throw;
                return aDefault;
            }
        }
        bool has_attr(py const& aObject, char const* aName)
        {
            try
            {
                aObject.attr(aName);
                return true;
            }
            catch (raised const&)
            {
                if (!caught({ "AttributeError" }))
                    throw;
                return false;
            }
        }
        // (aClass.__dict__)
        py class_dict(py const& aClass) { return aClass.attr("__dict__"); }
        bool in_class_dict(py const& aClass, char const* aName) { return class_dict(aClass).contains(py{ aName }); }

        using parameter = py_parameter;
        parameter positional(char const* aName, py aDefault = {}) { return py_positional(aName, std::move(aDefault)); }
        parameter keyword(char const* aName, py aDefault = {}) { return py_keyword(aName, std::move(aDefault)); }

        // (a closure as a function of the module: its qualified name is aQualname)
        py function(std::string const& aName, std::string const& aQualname, native_body aBody, char const* aDoc = "")
        {
            return make_native_function(aName, aQualname, aDoc, sModule, std::move(aBody));
        }

        // (a recursive call of a repr of the same object (in the same thread): "..." (reprlib.recursive_repr))
        template <typename F>
        py guarded_repr(py const& aSelf, F&& aRepr)
        {
            thread_local std::set<object*> tActive;
            if (tActive.contains(aSelf.get()))
                return py{ "..." };
            tActive.insert(aSelf.get());
            try
            {
                py result = aRepr();
                tActive.erase(aSelf.get());
                return result;
            }
            catch (...)
            {
                tActive.erase(aSelf.get());
                throw;
            }
        }

        // --- the module's classes

        // (a class: its methods are closures (or other values) in aNamespace)
        py make_dataclasses_class(char const* aName, std::vector<py> const& aBases, std::vector<std::pair<char const*, py>> const& aNamespace, char const* aDoc = nullptr)
        {
            py const ns = py_dict();
            ns.set_item(py{ "__module__" }, py{ sModule });
            ns.set_item(py{ "__qualname__" }, py{ aName });
            if (aDoc != nullptr)
                ns.set_item(py{ "__doc__" }, py{ aDoc });
            for (auto const& [name, value] : aNamespace)
                ns.set_item(py{ name }, value);
            py const result = builtin("type")(py{ aName }, py_tuple(aBases.empty() ? std::vector<py>{ builtin("object") } : aBases), ns);
            state().classes[aName] = result;
            return result;
        }

        // (a method of a class of the module)
        py method(char const* aClass, char const* aName, std::vector<parameter> aParameters, std::function<py(std::vector<py> const&)> aBody)
        {
            std::string const qualname = std::string{ aClass } + "." + aName;
            aParameters.insert(aParameters.begin(), positional("self"));
            return function(aName, qualname, [qualname, aParameters, aBody](std::vector<py> const& aArgs, object* aKwargs) -> py
            {
                return aBody(py_bind(qualname, aParameters, aArgs, aKwargs));
            });
        }

        py field_repr(py const& aSelf)
        {
            return guarded_repr(aSelf, [&]()
            {
                return py{ "Field(name=" + aSelf.attr("name").repr() + ",type=" + aSelf.attr("type").repr() + ",default=" + aSelf.attr("default").repr() +
                    ",default_factory=" + aSelf.attr("default_factory").repr() + ",init=" + aSelf.attr("init").repr() + ",repr=" + aSelf.attr("repr").repr() +
                    ",hash=" + aSelf.attr("hash").repr() + ",compare=" + aSelf.attr("compare").repr() + ",metadata=" + aSelf.attr("metadata").repr() +
                    ",kw_only=" + aSelf.attr("kw_only").repr() + ",_field_type=" + aSelf.attr("_field_type").str() + ")" };
            });
        }

        void make_classes()
        {
            auto& s = state();
            s.frozenInstanceError = py{ module_exception("dataclasses.FrozenInstanceError", { "AttributeError" }) };
            s.classes["FrozenInstanceError"] = s.frozenInstanceError;

            make_dataclasses_class("_HAS_DEFAULT_FACTORY_CLASS", {}, { { "__repr__", method("_HAS_DEFAULT_FACTORY_CLASS", "__repr__", {},
                [](std::vector<py> const&) { return py{ "<factory>" }; }) } });
            s.hasDefaultFactory = cls("_HAS_DEFAULT_FACTORY_CLASS")();
            make_dataclasses_class("_MISSING_TYPE", {}, {});
            s.missing = cls("_MISSING_TYPE")();
            make_dataclasses_class("_KW_ONLY_TYPE", {}, {});
            s.kwOnly = cls("_KW_ONLY_TYPE")();
            s.emptyMetadata = import_attribute("types", "MappingProxyType")(py_dict());

            make_dataclasses_class("_FIELD_BASE", {}, {
                { "__init__", method("_FIELD_BASE", "__init__", { positional("name") }, [](std::vector<py> const& a)
                    {
                        a[0].set_attr("name", a[1]);
                        return none();
                    }) },
                { "__repr__", method("_FIELD_BASE", "__repr__", {}, [](std::vector<py> const& a) { return a[0].attr("name"); }) } });
            s.fieldMarker = cls("_FIELD_BASE")(py{ "_FIELD" });
            s.classVarMarker = cls("_FIELD_BASE")(py{ "_FIELD_CLASSVAR" });
            s.initVarMarker = cls("_FIELD_BASE")(py{ "_FIELD_INITVAR" });

            make_dataclasses_class("InitVar", {}, {
                { "__slots__", py_tuple({ py{ "type" } }) },
                { "__init__", method("InitVar", "__init__", { positional("type") }, [](std::vector<py> const& a)
                    {
                        a[0].set_attr("type", a[1]);
                        return none();
                    }) },
                { "__repr__", method("InitVar", "__repr__", {}, [](std::vector<py> const& a)
                    {
                        py const type = a[0].attr("type");
                        std::string const name = isinstance(type, builtin("type")) ? type.attr("__name__").str() : type.repr();
                        return py{ "dataclasses.InitVar[" + name + "]" };
                    }) },
                { "__class_getitem__", builtin("classmethod")(function("__class_getitem__", "InitVar.__class_getitem__",
                    [](std::vector<py> const& aArgs, object* aKwargs) -> py
                    {
                        auto const a = py_bind("InitVar.__class_getitem__", { positional("cls"), positional("type") }, aArgs, aKwargs);
                        return cls("InitVar")(a[1]);
                    })) } });

            std::vector<py> fieldSlots;
            for (auto n : { "name", "type", "default", "default_factory", "repr", "hash", "init", "compare", "metadata", "kw_only", "_field_type" })
                fieldSlots.emplace_back(n);
            make_dataclasses_class("Field", {}, {
                { "__slots__", py_tuple(fieldSlots) },
                { "__init__", method("Field", "__init__", { positional("default"), positional("default_factory"), positional("init"), positional("repr"),
                    positional("hash"), positional("compare"), positional("metadata"), positional("kw_only") }, [](std::vector<py> const& a)
                    {
                        auto const& self = a[0];
                        self.set_attr("name", none());
                        self.set_attr("type", none());
                        self.set_attr("default", a[1]);
                        self.set_attr("default_factory", a[2]);
                        self.set_attr("init", a[3]);
                        self.set_attr("repr", a[4]);
                        self.set_attr("hash", a[5]);
                        self.set_attr("compare", a[6]);
                        self.set_attr("metadata", a[7].is_none() ? state().emptyMetadata : import_attribute("types", "MappingProxyType")(a[7]));
                        self.set_attr("kw_only", a[8]);
                        self.set_attr("_field_type", none());
                        return none();
                    }) },
                { "__repr__", method("Field", "__repr__", {}, [](std::vector<py> const& a) { return field_repr(a[0]); }) },
                { "__set_name__", method("Field", "__set_name__", { positional("owner"), positional("name") }, [](std::vector<py> const& a)
                    {
                        py const defaultValue = a[0].attr("default");
                        py const func = attr_or(type_of(defaultValue), "__set_name__", none());
                        if (func.truthy())
                            func(defaultValue, a[1], a[2]);
                        return none();
                    }) },
                { "__class_getitem__", builtin("classmethod")(import_attribute("types", "GenericAlias")) } });

            std::vector<py> paramSlots;
            std::vector<parameter> paramParameters;
            static char const* const sParams[] = { "init", "repr", "eq", "order", "unsafe_hash", "frozen", "match_args", "kw_only", "slots", "weakref_slot" };
            for (auto n : sParams)
            {
                paramSlots.emplace_back(n);
                paramParameters.push_back(positional(n));
            }
            make_dataclasses_class("_DataclassParams", {}, {
                { "__slots__", py_tuple(paramSlots) },
                { "__init__", method("_DataclassParams", "__init__", paramParameters, [](std::vector<py> const& a)
                    {
                        for (std::size_t i = 0u; i < std::size(sParams); ++i)
                            a[0].set_attr(sParams[i], a[i + 1u]);
                        return none();
                    }) },
                { "__repr__", method("_DataclassParams", "__repr__", {}, [](std::vector<py> const& a)
                    {
                        std::string text = "_DataclassParams(";
                        for (std::size_t i = 0u; i < std::size(sParams); ++i)
                            text += std::string{ i > 0u ? "," : "" } + sParams[i] + "=" + a[0].attr(sParams[i]).repr();
                        return py{ text + ")" };
                    }) } });

            std::vector<py> atomic;
            py const types = py{ fn_import("types") };
            for (auto n : { "NoneType", "EllipsisType", "NotImplementedType", "CodeType", "BuiltinFunctionType", "FunctionType" })
            {
                py const t = attr_or(types, n, py{});
                if (!t.empty())
                    atomic.push_back(t);
            }
            for (auto n : { "bool", "int", "float", "str", "complex", "bytes", "type", "range", "property" })
                atomic.push_back(builtin(n));
            s.atomicTypes = builtin("frozenset")(py_list(atomic));
        }

        bool is_field(py const& aValue) { return isinstance(aValue, cls("Field")); }
        bool is_missing(py const& aValue) { return aValue.is(state().missing); }

        // (field(*, default=MISSING, default_factory=MISSING, init=True, repr=True, hash=None, compare=True, metadata=None, kw_only=MISSING))
        py make_field(py const& aDefault, py const& aDefaultFactory, py const& aInit, py const& aRepr, py const& aHash, py const& aCompare, py const& aMetadata,
            py const& aKwOnly)
        {
            if (!is_missing(aDefault) && !is_missing(aDefaultFactory))
                py_raise("ValueError", "cannot specify both default and default_factory");
            return cls("Field")(aDefault, aDefaultFactory, aInit, aRepr, aHash, aCompare, aMetadata, aKwOnly);
        }

        // --- the processing of a class

        // (the identifier at the start of a string annotation and the module identifier before it, as _MODULE_IDENTIFIER_RE matches them
        // (r'^(?:\s*(\w+)\s*\.)?\s*(\w+)'): false if there is none)
        bool module_identifier(std::string const& aText, std::string& aModule, std::string& aName)
        {
            auto const space = [](char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v'; };
            auto const word = [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || static_cast<unsigned char>(c) >= 0x80u; };
            std::size_t i = 0u;
            auto const skip = [&](std::size_t& p) { while (p < aText.size() && space(aText[p])) ++p; };
            auto const identifier = [&](std::size_t& p)
            {
                auto const start = p;
                while (p < aText.size() && word(aText[p]))
                    ++p;
                return aText.substr(start, p - start);
            };
            skip(i);
            std::string const first = identifier(i);
            if (first.empty())
                return false;
            auto j = i;
            skip(j);
            if (j < aText.size() && aText[j] == '.')
            {
                ++j;
                skip(j);
                std::string const second = identifier(j);
                if (!second.empty())
                {
                    aModule = first;
                    aName = second;
                    return true;
                }
            }
            aModule.clear();
            aName = first;
            return true;
        }

        py module_of(py const& aName) { return import_attribute("sys", "modules").method("get", { aName }); }

        // (_is_type: whether a string annotation refers to aType of aModule)
        bool is_type(py const& aAnnotation, py const& aClass, py const& aModule, py const& aType, std::function<bool(py const&, py const&)> const& aPredicate)
        {
            std::string moduleName;
            std::string name;
            if (!module_identifier(aAnnotation.str(), moduleName, name))
                return false;
            py ns;
            if (moduleName.empty())
                ns = module_of(aClass.attr("__module__")).attr("__dict__");
            else
            {
                py const module = module_of(aClass.attr("__module__"));
                if (module.truthy() && module.attr("__dict__").method("get", { py{ moduleName } }).is(aModule))
                    ns = module_of(aType.attr("__module__")).attr("__dict__");
            }
            return !ns.empty() && ns.truthy() && aPredicate(ns.method("get", { py{ name } }), aModule);
        }

        bool is_classvar(py const& aType, py const& aTyping)
        {
            return aType.is(aTyping.attr("ClassVar")) || (type_of(aType).is(attr_or(aTyping, "_GenericAlias", py{ none() })) &&
                aType.attr("__origin__").is(aTyping.attr("ClassVar")));
        }
        bool is_initvar(py const& aType, py const&) { return aType.is(cls("InitVar")) || type_of(aType).is(cls("InitVar")); }
        bool is_kw_only(py const& aType, py const&) { return aType.is(state().kwOnly); }

        // (_get_field: the Field of a name and type of a class (its default from the class))
        py get_field(py const& aClass, py const& aName, py const& aType, py const& aDefaultKwOnly)
        {
            auto& s = state();
            py defaultValue = attr_or(aClass, aName.str().c_str(), s.missing);
            py f;
            if (is_field(defaultValue))
                f = defaultValue;
            else
            {
                if (isinstance(defaultValue, import_attribute("types", "MemberDescriptorType")))
                    defaultValue = s.missing;
                f = make_field(defaultValue, s.missing, boolean(true), boolean(true), none(), boolean(true), none(), s.missing);
            }
            f.set_attr("name", aName);
            f.set_attr("type", aType);
            f.set_attr("_field_type", s.fieldMarker);
            py const typing = import_attribute("sys", "modules").method("get", { py{ "typing" } });
            if (typing.truthy())
            {
                if (is_classvar(aType, typing) || (is_str(f.attr("type")) && is_type(f.attr("type"), aClass, typing, typing.attr("ClassVar"), is_classvar)))
                    f.set_attr("_field_type", s.classVarMarker);
            }
            if (f.attr("_field_type").is(s.fieldMarker))
            {
                py const dataclasses = import_attribute("sys", "modules")[py{ sModule }];
                if (is_initvar(aType, dataclasses) || (is_str(f.attr("type")) && is_type(f.attr("type"), aClass, dataclasses, cls("InitVar"), is_initvar)))
                    f.set_attr("_field_type", s.initVarMarker);
            }
            py const fieldType = f.attr("_field_type");
            if (fieldType.is(s.classVarMarker) || fieldType.is(s.initVarMarker))
                if (!is_missing(f.attr("default_factory")))
                    py_raise("TypeError", "field " + f.attr("name").str() + " cannot have a default factory");
            if (fieldType.is(s.fieldMarker) || fieldType.is(s.initVarMarker))
            {
                if (is_missing(f.attr("kw_only")))
                    f.set_attr("kw_only", aDefaultKwOnly);
            }
            else if (!is_missing(f.attr("kw_only")))
                py_raise("TypeError", "field " + f.attr("name").str() + " is a ClassVar but specifies kw_only");
            if (fieldType.is(s.fieldMarker) && f.attr("default").attr("__class__").attr("__hash__").is_none())
                py_raise("ValueError", "mutable default " + type_of(f.attr("default")).repr() + " for field " + f.attr("name").str() +
                    " is not allowed: use default_factory");
            return f;
        }

        // (_set_new_attribute: true if the class already has the attribute (in its __dict__))
        bool set_new_attribute(py const& aClass, char const* aName, py const& aValue)
        {
            if (in_class_dict(aClass, aName))
                return true;
            aClass.set_attr(aName, aValue);
            return false;
        }

        // (the text of an annotation as inspect's signatures show it (inspect.formatannotation))
        std::string format_annotation(py const& aAnnotation)
        {
            py const module = attr_or(aAnnotation, "__module__", none());
            if (is_str(module) && module.str() == "typing")
            {
                std::string const text = aAnnotation.repr();
                std::string result;
                for (std::size_t i = 0u; i < text.size();)
                {
                    if (text.compare(i, 7u, "typing.") == 0 && (i == 0u || !(std::isalnum(static_cast<unsigned char>(text[i - 1u])) || text[i - 1u] == '_' || text[i - 1u] == '.')))
                        i += 7u;
                    else
                        result += text[i++];
                }
                return result;
            }
            if (isinstance(aAnnotation, import_attribute("types", "GenericAlias")))
                return aAnnotation.str();
            if (isinstance(aAnnotation, builtin("type")))
            {
                std::string const m = aAnnotation.attr("__module__").str();
                if (m == "builtins")
                    return aAnnotation.attr("__qualname__").str();
                return m + "." + aAnnotation.attr("__qualname__").str();
            }
            return aAnnotation.repr();
        }

        // (a field of __init__: how the generated __init__ initializes it (_field_init))
        struct init_field
        {
            std::string name;
            enum { Parameter, ParameterOrFactory, Factory, Value, Skip } kind = Parameter;
            py value;                   ///< (the factory, or the value (Value))
            bool initVar = false;
            std::size_t parameter = 0u; ///< (its parameter's index (Parameter, ParameterOrFactory))
        };

        // (__init__ (_init_fn): of aFields (the fields and InitVars), whose parameters are aStandard then (keyword-only) aKwOnly)
        py init_function(py const& aClass, std::vector<py> const& aFields, std::vector<py> const& aStandard, std::vector<py> const& aKwOnly, bool aFrozen,
            bool aHasPostInit, std::string const& aSelfName, bool aSlots, std::string& aSignature)
        {
            auto& s = state();
            py seenDefault;
            for (auto const& f : aStandard)
                if (f.attr("init").truthy())
                {
                    if (!(is_missing(f.attr("default")) && is_missing(f.attr("default_factory"))))
                        seenDefault = f;
                    else if (!seenDefault.empty())
                        py_raise("TypeError", "non-default argument " + f.attr("name").repr() + " follows default argument " + seenDefault.attr("name").repr());
                }
            std::vector<parameter> parameters{ positional(aSelfName.c_str()) };
            py const annotations = py_dict();
            py const kwdefaults = py_dict();
            std::vector<py> defaults;
            std::string signature;
            auto const add = [&](py const& f, bool aKeywordOnly)
            {
                parameter p{ f.attr("name").str(), aKeywordOnly ? parameter::KeywordOnly : parameter::PositionalOrKeyword, {} };
                std::string text = p.name + ": " + format_annotation(f.attr("type"));
                if (!is_missing(f.attr("default")))
                    p.defaultValue = f.attr("default");
                else if (!is_missing(f.attr("default_factory")))
                    p.defaultValue = s.hasDefaultFactory;
                if (!p.defaultValue.empty())
                {
                    text += " = " + p.defaultValue.repr();
                    if (aKeywordOnly)
                        kwdefaults.set_item(py{ p.name }, p.defaultValue);
                    else
                        defaults.push_back(p.defaultValue);
                }
                annotations.set_item(py{ p.name }, f.attr("type"));
                signature += (signature.empty() ? "" : ", ") + text;
                parameters.push_back(std::move(p));
            };
            for (auto const& f : aStandard)
                add(f, false);
            if (!aKwOnly.empty())
                signature += signature.empty() ? "*" : ", *";
            for (auto const& f : aKwOnly)
                add(f, true);
            annotations.set_item(py{ "return" }, none());
            aSignature = "(" + signature + ")";
            std::vector<init_field> initialization;
            std::vector<std::size_t> initVars;
            for (auto const& f : aFields)
            {
                init_field i{ f.attr("name").str() };
                i.initVar = f.attr("_field_type").is(s.initVarMarker);
                for (std::size_t p = 1u; p < parameters.size(); ++p)
                    if (parameters[p].name == i.name)
                        i.parameter = p;
                bool const init = f.attr("init").truthy();
                if (!is_missing(f.attr("default_factory")))
                {
                    i.kind = init ? init_field::ParameterOrFactory : init_field::Factory;
                    i.value = f.attr("default_factory");
                }
                else if (init)
                    i.kind = init_field::Parameter;
                else if (aSlots && !is_missing(f.attr("default")))
                {
                    i.kind = init_field::Value;
                    i.value = f.attr("default");
                }
                else
                    i.kind = init_field::Skip;
                if (i.initVar)
                    initVars.push_back(i.parameter);
                initialization.push_back(std::move(i));
            }
            std::string const qualname = aClass.attr("__qualname__").str() + ".__init__";
            py const result = function("__init__", qualname, [qualname, parameters, initialization, initVars, aFrozen, aHasPostInit](std::vector<py> const& aArgs, object* aKwargs) -> py
            {
                auto const a = py_bind(qualname, parameters, aArgs, aKwargs);
                auto const& self = a[0];
                for (auto const& i : initialization)
                {
                    py value;
                    switch (i.kind)
                    {
                    case init_field::Parameter:
                        value = a[i.parameter];
                        break;
                    case init_field::ParameterOrFactory:
                        value = a[i.parameter].is(state().hasDefaultFactory) ? i.value() : a[i.parameter];
                        break;
                    case init_field::Factory:
                        value = i.value();
                        break;
                    case init_field::Value:
                        value = i.value;
                        break;
                    case init_field::Skip:
                        continue;
                    }
                    if (i.initVar)
                        continue;
                    if (aFrozen)
                        builtin("object").attr("__setattr__")(self, py{ i.name }, value);
                    else
                        self.set_attr(i.name.c_str(), value);
                }
                if (aHasPostInit)
                {
                    std::vector<py> args;
                    for (auto p : initVars)
                        args.push_back(a[p]);
                    self.method("__post_init__", args);
                }
                return none();
            });
            result.set_attr("__annotations__", annotations);
            result.set_attr("__defaults__", defaults.empty() ? none() : py_tuple(defaults));
            result.set_attr("__kwdefaults__", kwdefaults.size() == 0 ? none() : kwdefaults);
            // (its signature, as inspect.signature makes a Python function's (the source that CPython execs has its parameters): made when asked for)
            result.set_attr("__signature__", function("__signature__", qualname + ".__signature__", [parameters, annotations](std::vector<py> const&, object*) -> py
            {
                py const parameterClass = import_attribute("inspect", "Parameter");
                std::vector<py> signatureParameters;
                for (auto const& p : parameters)
                {
                    std::vector<std::pair<char const*, py>> options;
                    if (!p.defaultValue.empty())
                        options.emplace_back("default", p.defaultValue);
                    if (annotations.contains(py{ p.name }))
                        options.emplace_back("annotation", annotations[py{ p.name }]);
                    signatureParameters.push_back(parameterClass.call({ py{ p.name }, parameterClass.attr(p.kind == parameter::KeywordOnly ? "KEYWORD_ONLY" :
                        "POSITIONAL_OR_KEYWORD") }, options));
                }
                return import_attribute("inspect", "Signature").call({ py_list(signatureParameters) }, { { "return_annotation", none() } });
            }));
            return result;
        }

        // (the functions that the processing of a class adds to it (_FuncBuilder): in order, with whether one cannot overwrite an attribute
        // of the class (and the message's addition) or always overwrites it)
        struct generated_function
        {
            std::string name;
            py function;
            bool overwriteError = false;
            std::string errorExtra;
            bool unconditional = false;
        };

        // (the class that the generated functions refer to as __class__ (a cell: _add_slots replaces it))
        using class_cell = std::shared_ptr<py>;

        void add_functions(py const& aClass, std::vector<generated_function> const& aFunctions)
        {
            for (auto const& g : aFunctions)
            {
                g.function.set_attr("__qualname__", py{ aClass.attr("__qualname__").str() + "." + g.function.attr("__name__").str() });
                g.function.set_attr("__module__", attr_or(aClass, "__module__", none()));
                if (g.unconditional)
                    aClass.set_attr(g.name.c_str(), g.function);
                else if (set_new_attribute(aClass, g.name.c_str(), g.function) && g.overwriteError)
                {
                    std::string message = "Cannot overwrite attribute " + g.function.attr("__name__").str() + " in class " + aClass.attr("__name__").str();
                    if (!g.errorExtra.empty())
                        message += " " + g.errorExtra;
                    py_raise("TypeError", message);
                }
            }
        }

        // (a tuple of the fields' values: (obj.x,obj.y))
        py fields_tuple(py const& aObject, std::vector<std::string> const& aNames)
        {
            std::vector<py> values;
            for (auto const& n : aNames)
                values.push_back(aObject.attr(n.c_str()));
            return py_tuple(values);
        }

        std::vector<std::string> names_of(std::vector<py> const& aFields)
        {
            std::vector<std::string> result;
            for (auto const& f : aFields)
                result.push_back(f.attr("name").str());
            return result;
        }

        py fields_of(py const& aClassOrInstance);

        // (_get_slots: the slots of a class (a generator in CPython))
        std::vector<py> get_slots(py const& aClass)
        {
            py const slots = class_dict(aClass).method("get", { py{ "__slots__" } });
            std::vector<py> result;
            if (slots.is_none())
            {
                if (py_compare(NEOS_PY_NE, attr_or(aClass, "__weakrefoffset__", py{ -1 }), py{ 0 }).truthy())
                    result.emplace_back("__weakref__");
                if (py_compare(NEOS_PY_NE, attr_or(aClass, "__dictoffset__", py{ -1 }), py{ 0 }).truthy())
                    result.emplace_back("__dict__");
            }
            else if (is_str(slots))
                result.push_back(slots);
            else if (!has_attr(slots, "__next__"))
                result = slots.items();
            else
                py_raise("TypeError", "Slots of '" + aClass.attr("__name__").str() + "' cannot be determined");
            return result;
        }

        // (_update_func_cell_for__class__)
        bool update_class_cell(py const& aFunction, py const& aOld, py const& aNew)
        {
            if (aFunction.is_none())
                return false;
            py freevars;
            try
            {
                freevars = aFunction.attr("__code__").attr("co_freevars");
            }
            catch (raised const&)
            {
                if (!caught({ "AttributeError" }))
                    throw;
                return false;
            }
            if (!freevars.contains(py{ "__class__" }))
                return false;
            py const closure = aFunction.attr("__closure__")[freevars.method("index", { py{ "__class__" } })];
            py contents;
            try
            {
                contents = closure.attr("cell_contents");
            }
            catch (raised const&)
            {
                if (!caught({ "ValueError" }))
                    throw;
                return false;
            }
            if (contents.is(aOld))
            {
                closure.set_attr("cell_contents", aNew);
                return true;
            }
            return false;
        }

        // (inspect.unwrap: the function that a chain of __wrapped__ attributes ends at)
        py unwrap(py const& aFunction)
        {
            py f = aFunction;
            std::set<object*> seen{ f.get() };
            while (has_attr(f, "__wrapped__"))
            {
                f = f.attr("__wrapped__");
                if (!seen.insert(f.get()).second)
                    py_raise("ValueError", "wrapper loop when unwrapping " + aFunction.repr());
            }
            return f;
        }

        // (_add_slots: a new class (with __slots__) replacing aClass)
        py add_slots(py const& aClass, bool aFrozen, bool aWeakrefSlot, class_cell const& aCell)
        {
            if (in_class_dict(aClass, "__slots__"))
                py_raise("TypeError", aClass.attr("__name__").str() + " already specifies __slots__");
            py const classDict = builtin("dict")(class_dict(aClass));
            std::vector<py> fieldNames;
            for (auto const& f : fields_of(aClass).items())
                fieldNames.push_back(f.attr("name"));
            py const inherited = builtin("set")();
            auto const mro = aClass.attr("__mro__").items();
            for (std::size_t i = 1u; i + 1u < mro.size(); ++i)
                for (auto const& slot : get_slots(mro[i]))
                    inherited.method("add", { slot });
            std::vector<py> slots;
            for (auto const& n : fieldNames)
                if (!inherited.contains(n))
                    slots.push_back(n);
            if (aWeakrefSlot && !inherited.contains(py{ "__weakref__" }))
                slots.emplace_back("__weakref__");
            classDict.set_item(py{ "__slots__" }, py_tuple(slots));
            for (auto const& n : fieldNames)
                classDict.method("pop", { n, none() });
            classDict.method("pop", { py{ "__dict__" }, none() });
            classDict.method("pop", { py{ "__weakref__" }, none() });
            py const qualname = attr_or(aClass, "__qualname__", none());
            py const newClass = type_of(aClass)(aClass.attr("__name__"), aClass.attr("__bases__"), classDict);
            if (!qualname.is_none())
                newClass.set_attr("__qualname__", qualname);
            if (aFrozen)
            {
                if (!classDict.contains(py{ "__getstate__" }))
                    newClass.set_attr("__getstate__", function("_dataclass_getstate", "_dataclass_getstate", [](std::vector<py> const& aArgs, object* aKwargs) -> py
                    {
                        auto const a = py_bind("_dataclass_getstate", { positional("self") }, aArgs, aKwargs);
                        std::vector<py> values;
                        for (auto const& f : fields_of(a[0]).items())
                            values.push_back(a[0].attr(f.attr("name").str().c_str()));
                        return py_list(values);
                    }));
                if (!classDict.contains(py{ "__setstate__" }))
                    newClass.set_attr("__setstate__", function("_dataclass_setstate", "_dataclass_setstate", [](std::vector<py> const& aArgs, object* aKwargs) -> py
                    {
                        auto const a = py_bind("_dataclass_setstate", { positional("self"), positional("state") }, aArgs, aKwargs);
                        py const zipped = builtin("zip")(fields_of(a[0]), a[1]);
                        for (auto const& pair : zipped.items())
                            builtin("object").attr("__setattr__")(a[0], pair[py{ 0 }].attr("name"), pair[py{ 1 }]);
                        return none();
                    }));
            }
            // (the generated functions' __class__)
            if (aCell->is(aClass))
                *aCell = newClass;
            for (auto const& member : class_dict(newClass).method("values").items())
            {
                py const m = unwrap(member);
                if (isinstance(m, import_attribute("types", "FunctionType")))
                {
                    if (update_class_cell(m, aClass, newClass))
                        break;
                }
                else if (isinstance(m, builtin("property")))
                {
                    if (update_class_cell(m.attr("fget"), aClass, newClass) || update_class_cell(m.attr("fset"), aClass, newClass) ||
                        update_class_cell(m.attr("fdel"), aClass, newClass))
                        break;
                }
            }
            return newClass;
        }

        // (_process_class)
        py process_class(py aClass, bool aInit, bool aRepr, bool aEq, bool aOrder, bool aUnsafeHash, bool aFrozen, bool aMatchArgs, bool aKwOnly, bool aSlots,
            bool aWeakrefSlot)
        {
            auto& s = state();
            py const fields = py_dict();
            aClass.set_attr("__dataclass_params__", cls("_DataclassParams")(boolean(aInit), boolean(aRepr), boolean(aEq), boolean(aOrder), boolean(aUnsafeHash),
                boolean(aFrozen), boolean(aMatchArgs), boolean(aKwOnly), boolean(aSlots), boolean(aWeakrefSlot)));
            bool anyFrozenBase = false;
            std::optional<bool> allFrozenBases;
            bool hasDataclassBases = false;
            auto const mro = aClass.attr("__mro__").items();
            for (std::size_t i = mro.size(); i-- > 1u;)
            {
                py const baseFields = attr_or(mro[i], "__dataclass_fields__", none());
                if (!baseFields.is_none())
                {
                    hasDataclassBases = true;
                    for (auto const& f : baseFields.method("values").items())
                        fields.set_item(f.attr("name"), f);
                    if (!allFrozenBases)
                        allFrozenBases = true;
                    bool const currentFrozen = mro[i].attr("__dataclass_params__").attr("frozen").truthy();
                    allFrozenBases = *allFrozenBases && currentFrozen;
                    anyFrozenBase = anyFrozenBase || currentFrozen;
                }
            }
            // (inspect.get_annotations(cls))
            py classAnnotations = class_dict(aClass).method("get", { py{ "__annotations__" }, none() });
            if (classAnnotations.is_none())
                classAnnotations = py_dict();
            else if (!isinstance(classAnnotations, builtin("dict")))
                py_raise("ValueError", classAnnotations.repr() + ".__annotations__ is neither a dict nor None");
            else
                classAnnotations = builtin("dict")(classAnnotations);
            std::vector<py> classFields;
            bool kwOnlySeen = false;
            py kwOnly = boolean(aKwOnly);
            py const dataclasses = import_attribute("sys", "modules")[py{ sModule }];
            for (auto const& item : classAnnotations.method("items").items())
            {
                py const name = item[py{ 0 }];
                py const type = item[py{ 1 }];
                if (is_kw_only(type, dataclasses) || (is_str(type) && is_type(type, aClass, dataclasses, s.kwOnly, is_kw_only)))
                {
                    if (kwOnlySeen)
                        py_raise("TypeError", name.repr() + " is KW_ONLY, but KW_ONLY has already been specified");
                    kwOnlySeen = true;
                    kwOnly = boolean(true);
                }
                else
                    classFields.push_back(get_field(aClass, name, type, kwOnly));
            }
            for (auto const& f : classFields)
            {
                fields.set_item(f.attr("name"), f);
                if (is_field(attr_or(aClass, f.attr("name").str().c_str(), none())))
                {
                    if (is_missing(f.attr("default")))
                        builtin("delattr")(aClass, f.attr("name"));
                    else
                        aClass.set_attr(f.attr("name").str().c_str(), f.attr("default"));
                }
            }
            for (auto const& item : class_dict(aClass).method("items").items())
            {
                py const name = item[py{ 0 }];
                if (is_field(item[py{ 1 }]) && !classAnnotations.contains(name))
                    py_raise("TypeError", name.repr() + " is a field but has no type annotation");
            }
            if (hasDataclassBases)
            {
                if (anyFrozenBase && !aFrozen)
                    py_raise("TypeError", "cannot inherit non-frozen dataclass from a frozen one");
                if (allFrozenBases && !*allFrozenBases && aFrozen)
                    py_raise("TypeError", "cannot inherit frozen dataclass from a non-frozen one");
            }
            aClass.set_attr("__dataclass_fields__", fields);
            py const classHash = class_dict(aClass).method("get", { py{ "__hash__" }, s.missing });
            bool const hasExplicitHash = !(is_missing(classHash) || (classHash.is_none() && in_class_dict(aClass, "__eq__")));
            if (aOrder && !aEq)
                py_raise("ValueError", "eq must be true if order is true");
            std::vector<py> allInitFields;
            std::vector<py> stdInitFields;
            std::vector<py> kwOnlyInitFields;
            for (auto const& f : fields.method("values").items())
            {
                py const fieldType = f.attr("_field_type");
                if (fieldType.is(s.fieldMarker) || fieldType.is(s.initVarMarker))
                {
                    allInitFields.push_back(f);
                    if (f.attr("init").truthy())
                        (f.attr("kw_only").truthy() ? kwOnlyInitFields : stdInitFields).push_back(f);
                }
            }
            class_cell const cell = std::make_shared<py>(aClass);
            std::string const prefix = aClass.attr("__qualname__").str() + ".";
            std::vector<generated_function> functions;
            std::string initSignature;
            if (aInit)
            {
                bool const hasPostInit = has_attr(aClass, "__post_init__");
                functions.push_back({ "__init__", init_function(aClass, allInitFields, stdInitFields, kwOnlyInitFields, aFrozen, hasPostInit,
                    fields.contains(py{ "self" }) ? "__dataclass_self__" : "self", aSlots, initSignature) });
            }
            set_new_attribute(aClass, "__replace__", s.replace);
            std::vector<py> fieldList;
            for (auto const& f : fields.method("values").items())
                if (f.attr("_field_type").is(s.fieldMarker))
                    fieldList.push_back(f);
            auto const compared = [&]()
            {
                std::vector<py> result;
                for (auto const& f : fieldList)
                    if (f.attr("compare").truthy())
                        result.push_back(f);
                return names_of(result);
            };
            if (aRepr)
            {
                std::vector<py> shown;
                for (auto const& f : fieldList)
                    if (f.attr("repr").truthy())
                        shown.push_back(f);
                auto const names = names_of(shown);
                functions.push_back({ "__repr__", function("__repr__", "__repr__", [names, prefix](std::vector<py> const& aArgs, object* aKwargs) -> py
                {
                    auto const a = py_bind(prefix + "__repr__", { positional("self") }, aArgs, aKwargs);
                    auto const& self = a[0];
                    return guarded_repr(self, [&]()
                    {
                        std::string text = self.attr("__class__").attr("__qualname__").str() + "(";
                        for (std::size_t i = 0u; i < names.size(); ++i)
                            text += (i > 0u ? ", " : "") + names[i] + "=" + self.attr(names[i].c_str()).repr();
                        return py{ text + ")" };
                    });
                }) });
            }
            if (aEq)
            {
                auto const names = compared();
                functions.push_back({ "__eq__", function("__eq__", "__eq__", [names, prefix](std::vector<py> const& aArgs, object* aKwargs) -> py
                {
                    auto const a = py_bind(prefix + "__eq__", { positional("self"), positional("other") }, aArgs, aKwargs);
                    auto const& self = a[0];
                    auto const& other = a[1];
                    if (self.is(other))
                        return boolean(true);
                    if (other.attr("__class__").is(self.attr("__class__")))
                    {
                        // (self.x==other.x and self.y==other.y)
                        py result = boolean(true);
                        for (auto const& n : names)
                        {
                            result = py_compare(NEOS_PY_EQ, self.attr(n.c_str()), other.attr(n.c_str()));
                            if (!result.truthy())
                                return result;
                        }
                        return result;
                    }
                    return py::borrowed(not_implemented());
                }) });
            }
            if (aOrder)
            {
                auto const names = compared();
                for (auto const& [name, op] : { std::pair{ "__lt__", NEOS_PY_LT }, std::pair{ "__le__", NEOS_PY_LE }, std::pair{ "__gt__", NEOS_PY_GT },
                    std::pair{ "__ge__", NEOS_PY_GE } })
                {
                    std::string const n = name;
                    int const operation = op;
                    functions.push_back({ name, function(name, name, [names, n, operation, prefix](std::vector<py> const& aArgs, object* aKwargs) -> py
                    {
                        auto const a = py_bind(prefix + n, { positional("self"), positional("other") }, aArgs, aKwargs);
                        if (a[1].attr("__class__").is(a[0].attr("__class__")))
                            return py_compare(operation, fields_tuple(a[0], names), fields_tuple(a[1], names));
                        return py::borrowed(not_implemented());
                    }), true, "Consider using functools.total_ordering" });
                }
            }
            if (aFrozen)
            {
                auto const names = names_of(fieldList);
                std::set<std::string> const frozenNames{ names.begin(), names.end() };
                functions.push_back({ "__setattr__", function("__setattr__", "__setattr__", [cell, frozenNames, prefix](std::vector<py> const& aArgs, object* aKwargs) -> py
                {
                    auto const a = py_bind(prefix + "__setattr__", { positional("self"), positional("name"), positional("value") }, aArgs, aKwargs);
                    if (type_of(a[0]).is(*cell) || (is_str(a[1]) && frozenNames.contains(a[1].str())))
                        py_raise(state().frozenInstanceError(py{ "cannot assign to field " + a[1].repr() }));
                    builtin("super")(*cell, a[0]).method("__setattr__", { a[1], a[2] });
                    return none();
                }), true });
                functions.push_back({ "__delattr__", function("__delattr__", "__delattr__", [cell, frozenNames, prefix](std::vector<py> const& aArgs, object* aKwargs) -> py
                {
                    auto const a = py_bind(prefix + "__delattr__", { positional("self"), positional("name") }, aArgs, aKwargs);
                    if (type_of(a[0]).is(*cell) || (is_str(a[1]) && frozenNames.contains(a[1].str())))
                        py_raise(state().frozenInstanceError(py{ "cannot delete field " + a[1].repr() }));
                    builtin("super")(*cell, a[0]).method("__delattr__", { a[1] });
                    return none();
                }), true });
            }
            // (_hash_action)
            enum { NoAction, SetNone, Add, Exception } hashAction = NoAction;
            if (aUnsafeHash)
                hashAction = hasExplicitHash ? Exception : Add;
            else if (aEq && !hasExplicitHash)
                hashAction = aFrozen ? Add : SetNone;
            if (hashAction == Exception)
                py_raise("TypeError", "Cannot overwrite attribute __hash__ in class " + aClass.attr("__name__").str());
            if (hashAction != NoAction)
                aClass.set_attr("__hash__", none());
            if (hashAction == Add)
            {
                std::vector<py> hashed;
                for (auto const& f : fieldList)
                    if (f.attr("hash").is_none() ? f.attr("compare").truthy() : f.attr("hash").truthy())
                        hashed.push_back(f);
                auto const names = names_of(hashed);
                functions.push_back({ "__hash__", function("__hash__", "__hash__", [names, prefix](std::vector<py> const& aArgs, object* aKwargs) -> py
                {
                    auto const a = py_bind(prefix + "__hash__", { positional("self") }, aArgs, aKwargs);
                    return builtin("hash")(fields_tuple(a[0], names));
                }), false, {}, true });
            }
            add_functions(aClass, functions);
            if (!attr_or(aClass, "__doc__", none()).truthy())
            {
                std::string textSignature;
                py inspect;
                try
                {
                    inspect = py{ fn_import("inspect") };
                }
                catch (raised const&)
                {
                    if (!caught({ "ImportError" }))
                        throw;
                }
                if (!inspect.empty())
                {
                    try
                    {
                        textSignature = inspect.attr("signature")(aClass).str();
                        for (std::string::size_type at; (at = textSignature.find(" -> None")) != std::string::npos;)
                            textSignature.erase(at, 8u);
                    }
                    catch (raised const&)
                    {
                        if (!caught({ "TypeError", "ValueError" }))
                            throw;
                        textSignature.clear();
                    }
                }
                else if (!functions.empty() && functions[0].name == "__init__" && class_dict(aClass).method("get", { py{ "__init__" } }).is(functions[0].function))
                    textSignature = initSignature;
                aClass.set_attr("__doc__", py{ aClass.attr("__name__").str() + textSignature });
            }
            if (aMatchArgs)
                set_new_attribute(aClass, "__match_args__", py_tuple([&]() { std::vector<py> n; for (auto const& f : stdInitFields) n.push_back(f.attr("name")); return n; }()));
            if (aWeakrefSlot && !aSlots)
                py_raise("TypeError", "weakref_slot is True but slots is False");
            if (aSlots)
                aClass = add_slots(aClass, aFrozen, aWeakrefSlot, cell);
            import_attribute("abc", "update_abstractmethods")(aClass);
            return aClass;
        }

        // (dataclass's keyword parameters)
        std::vector<parameter> dataclass_parameters()
        {
            return { keyword("init", boolean(true)), keyword("repr", boolean(true)), keyword("eq", boolean(true)), keyword("order", boolean(false)),
                keyword("unsafe_hash", boolean(false)), keyword("frozen", boolean(false)), keyword("match_args", boolean(true)), keyword("kw_only", boolean(false)),
                keyword("slots", boolean(false)), keyword("weakref_slot", boolean(false)) };
        }

        // (dataclass(cls, ...) of its keyword arguments (aOptions: in order of dataclass_parameters))
        py apply_dataclass(py const& aClass, std::vector<py> const& aOptions)
        {
            auto const t = [&](std::size_t i) { return aOptions[i].truthy(); };
            return process_class(aClass, t(0), t(1), t(2), t(3), t(4), t(5), t(6), t(7), t(8), t(9));
        }

        py fields_of(py const& aClassOrInstance)
        {
            py fields;
            try
            {
                fields = aClassOrInstance.attr("__dataclass_fields__");
            }
            catch (raised const&)
            {
                if (!caught({ "AttributeError" }))
                    throw;
                py const e = builtin("TypeError")(py{ "must be called with a dataclass type or instance" });
                e.set_attr("__suppress_context__", boolean(true));
                py_raise(e);
            }
            std::vector<py> result;
            for (auto const& f : fields.method("values").items())
                if (f.attr("_field_type").is(state().fieldMarker))
                    result.push_back(f);
            return py_tuple(result);
        }

        bool is_dataclass_instance(py const& aObject) { return has_attr(type_of(aObject), "__dataclass_fields__"); }

        py asdict_inner(py const& aObject, py const& aDictFactory)
        {
            py const objectType = type_of(aObject);
            if (state().atomicTypes.contains(objectType))
                return aObject;
            if (has_attr(objectType, "__dataclass_fields__"))
            {
                if (aDictFactory.is(builtin("dict")))
                {
                    py const result = py_dict();
                    for (auto const& f : fields_of(aObject).items())
                        result.set_item(f.attr("name"), asdict_inner(aObject.attr(f.attr("name").str().c_str()), aDictFactory));
                    return result;
                }
                std::vector<py> pairs;
                for (auto const& f : fields_of(aObject).items())
                    pairs.push_back(py_tuple({ f.attr("name"), asdict_inner(aObject.attr(f.attr("name").str().c_str()), aDictFactory) }));
                return aDictFactory(py_list(pairs));
            }
            if (objectType.is(builtin("list")))
            {
                std::vector<py> items;
                for (auto const& v : aObject.items())
                    items.push_back(asdict_inner(v, aDictFactory));
                return py_list(items);
            }
            if (objectType.is(builtin("dict")))
            {
                py const result = py_dict();
                for (auto const& item : aObject.method("items").items())
                    result.set_item(asdict_inner(item[py{ 0 }], aDictFactory), asdict_inner(item[py{ 1 }], aDictFactory));
                return result;
            }
            if (objectType.is(builtin("tuple")))
            {
                std::vector<py> items;
                for (auto const& v : aObject.items())
                    items.push_back(asdict_inner(v, aDictFactory));
                return py_tuple(items);
            }
            if (issubclass(objectType, builtin("tuple")))
            {
                std::vector<py> items;
                for (auto const& v : aObject.items())
                    items.push_back(asdict_inner(v, aDictFactory));
                if (has_attr(aObject, "_fields"))
                    return objectType.call(items);
                return objectType(py_list(items));
            }
            if (issubclass(objectType, builtin("dict")))
            {
                if (has_attr(objectType, "default_factory"))
                {
                    py const result = objectType(aObject.attr("default_factory"));
                    for (auto const& item : aObject.method("items").items())
                        result.set_item(asdict_inner(item[py{ 0 }], aDictFactory), asdict_inner(item[py{ 1 }], aDictFactory));
                    return result;
                }
                std::vector<py> pairs;
                for (auto const& item : aObject.method("items").items())
                    pairs.push_back(py_tuple({ asdict_inner(item[py{ 0 }], aDictFactory), asdict_inner(item[py{ 1 }], aDictFactory) }));
                return objectType(py_list(pairs));
            }
            if (issubclass(objectType, builtin("list")))
            {
                std::vector<py> items;
                for (auto const& v : aObject.items())
                    items.push_back(asdict_inner(v, aDictFactory));
                return objectType(py_list(items));
            }
            return import_attribute("copy", "deepcopy")(aObject);
        }

        py astuple_inner(py const& aObject, py const& aTupleFactory)
        {
            if (state().atomicTypes.contains(type_of(aObject)))
                return aObject;
            if (is_dataclass_instance(aObject))
            {
                std::vector<py> items;
                for (auto const& f : fields_of(aObject).items())
                    items.push_back(astuple_inner(aObject.attr(f.attr("name").str().c_str()), aTupleFactory));
                return aTupleFactory(py_list(items));
            }
            if (isinstance(aObject, builtin("tuple")) && has_attr(aObject, "_fields"))
            {
                std::vector<py> items;
                for (auto const& v : aObject.items())
                    items.push_back(astuple_inner(v, aTupleFactory));
                return type_of(aObject).call(items);
            }
            if (isinstance(aObject, builtin("list")) || isinstance(aObject, builtin("tuple")))
            {
                std::vector<py> items;
                for (auto const& v : aObject.items())
                    items.push_back(astuple_inner(v, aTupleFactory));
                return type_of(aObject)(py_list(items));
            }
            if (isinstance(aObject, builtin("dict")))
            {
                py const objectType = type_of(aObject);
                if (has_attr(objectType, "default_factory"))
                {
                    py const result = objectType(aObject.attr("default_factory"));
                    for (auto const& item : aObject.method("items").items())
                        result.set_item(astuple_inner(item[py{ 0 }], aTupleFactory), astuple_inner(item[py{ 1 }], aTupleFactory));
                    return result;
                }
                std::vector<py> pairs;
                for (auto const& item : aObject.method("items").items())
                    pairs.push_back(py_tuple({ astuple_inner(item[py{ 0 }], aTupleFactory), astuple_inner(item[py{ 1 }], aTupleFactory) }));
                return objectType(py_list(pairs));
            }
            return import_attribute("copy", "deepcopy")(aObject);
        }

        // (_replace(self, /, **changes))
        py replace_of(py const& aSelf, py const& aChanges)
        {
            auto& s = state();
            for (auto const& f : aSelf.attr("__dataclass_fields__").method("values").items())
            {
                py const fieldType = f.attr("_field_type");
                if (fieldType.is(s.classVarMarker))
                    continue;
                py const name = f.attr("name");
                if (!f.attr("init").truthy())
                {
                    if (aChanges.contains(name))
                        py_raise("TypeError", "field " + name.str() + " is declared with init=False, it cannot be specified with replace()");
                    continue;
                }
                if (!aChanges.contains(name))
                {
                    if (fieldType.is(s.initVarMarker) && is_missing(f.attr("default")))
                        py_raise("TypeError", "InitVar " + name.repr() + " must be specified with replace()");
                    aChanges.set_item(name, aSelf.attr(name.str().c_str()));
                }
            }
            return py{ invoke(aSelf.attr("__class__").get(), {}, aChanges.get()) };
        }

        // (the name of the module that called a function of this module (sys._getframe: the innermost Python frame))
        py caller_module()
        {
            try
            {
                py const frame = import_attribute("sys", "_getframe")(py{ 0 });
                return frame.attr("f_globals").method("get", { py{ "__name__" }, py{ "__main__" } });
            }
            catch (raised const&)
            {
                if (!caught({ "AttributeError", "ValueError" }))
                    throw;
                return {};
            }
        }

        py module_function(char const* aName, std::vector<parameter> aParameters, std::function<py(std::vector<py> const&, py const&)> aBody, char const* aDoc,
            bool aVarKeywords = false)
        {
            std::string const name = aName;
            return function(name, name, [name, aParameters, aBody, aVarKeywords](std::vector<py> const& aArgs, object* aKwargs) -> py
            {
                py extra;
                auto const a = py_bind(name, aParameters, aArgs, aKwargs, nullptr, aVarKeywords ? &extra : nullptr);
                return aBody(a, extra);
            }, aDoc);
        }

        std::vector<std::pair<std::string, py>> make_functions()
        {
            auto& s = state();
            std::vector<std::pair<std::string, py>> result;
            result.emplace_back("field", module_function("field", { keyword("default", s.missing), keyword("default_factory", s.missing), keyword("init", boolean(true)),
                keyword("repr", boolean(true)), keyword("hash", none()), keyword("compare", boolean(true)), keyword("metadata", none()), keyword("kw_only", s.missing) },
                [](std::vector<py> const& a, py const&) { return make_field(a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7]); },
                "Return an object to identify dataclass fields.\n\n    default is the default value of the field.  default_factory is a\n"
                "    0-argument function called to initialize a field's value.  If init\n    is true, the field will be a parameter to the class's __init__()\n"
                "    function.  If repr is true, the field will be included in the\n    object's repr().  If hash is true, the field will be included in the\n"
                "    object's hash().  If compare is true, the field will be used in\n    comparison functions.  metadata, if specified, must be a mapping\n"
                "    which is stored but not otherwise examined by dataclass.  If kw_only\n    is true, the field will become a keyword-only parameter to\n"
                "    __init__().\n\n    It is an error to specify both default and default_factory.\n    "));
            std::vector<parameter> dataclassParameters{ parameter{ "cls", parameter::PositionalOnly, none() } };
            for (auto& p : dataclass_parameters())
                dataclassParameters.push_back(std::move(p));
            result.emplace_back("dataclass", module_function("dataclass", dataclassParameters, [](std::vector<py> const& a, py const&) -> py
                {
                    std::vector<py> const options{ a.begin() + 1, a.end() };
                    if (a[0].is_none())
                        return function("wrap", "dataclass.<locals>.wrap", [options](std::vector<py> const& aArgs, object* aKwargs) -> py
                        {
                            auto const w = py_bind("dataclass.<locals>.wrap", { positional("cls") }, aArgs, aKwargs);
                            return apply_dataclass(w[0], options);
                        });
                    return apply_dataclass(a[0], options);
                },
                "Add dunder methods based on the fields defined in the class.\n\n    Examines PEP 526 __annotations__ to determine fields.\n\n"
                "    If init is true, an __init__() method is added to the class. If repr\n    is true, a __repr__() method is added. If order is true, rich\n"
                "    comparison dunder methods are added. If unsafe_hash is true, a\n    __hash__() method is added. If frozen is true, fields may not be\n"
                "    assigned to after instance creation. If match_args is true, the\n    __match_args__ tuple is added. If kw_only is true, then by default\n"
                "    all fields are keyword-only. If slots is true, a new class with a\n    __slots__ attribute is returned.\n    "));
            result.emplace_back("fields", module_function("fields", { positional("class_or_instance") }, [](std::vector<py> const& a, py const&) { return fields_of(a[0]); },
                "Return a tuple describing the fields of this dataclass.\n\n    Accepts a dataclass or an instance of one. Tuple elements are of\n    type Field.\n    "));
            result.emplace_back("_is_dataclass_instance", module_function("_is_dataclass_instance", { positional("obj") },
                [](std::vector<py> const& a, py const&) { return boolean(is_dataclass_instance(a[0])); }, "Returns True if obj is an instance of a dataclass."));
            result.emplace_back("is_dataclass", module_function("is_dataclass", { positional("obj") }, [](std::vector<py> const& a, py const&)
                {
                    py const c = isinstance(a[0], builtin("type")) ? a[0] : type_of(a[0]);
                    return boolean(has_attr(c, "__dataclass_fields__"));
                }, "Returns True if obj is a dataclass or an instance of a\n    dataclass."));
            result.emplace_back("asdict", module_function("asdict", { positional("obj"), keyword("dict_factory", builtin("dict")) }, [](std::vector<py> const& a, py const&)
                {
                    if (!is_dataclass_instance(a[0]))
                        py_raise("TypeError", "asdict() should be called on dataclass instances");
                    return asdict_inner(a[0], a[1]);
                },
                "Return the fields of a dataclass instance as a new dictionary mapping\n    field names to field values.\n\n    Example usage::\n\n"
                "      @dataclass\n      class C:\n          x: int\n          y: int\n\n      c = C(1, 2)\n      assert asdict(c) == {'x': 1, 'y': 2}\n\n"
                "    If given, 'dict_factory' will be used instead of built-in dict.\n    The function applies recursively to field values that are\n"
                "    dataclass instances. This will also look into built-in containers:\n    tuples, lists, and dicts. Other objects are copied with 'copy.deepcopy()'.\n    "));
            result.emplace_back("_asdict_inner", module_function("_asdict_inner", { positional("obj"), positional("dict_factory") },
                [](std::vector<py> const& a, py const&) { return asdict_inner(a[0], a[1]); }, ""));
            result.emplace_back("astuple", module_function("astuple", { positional("obj"), keyword("tuple_factory", builtin("tuple")) }, [](std::vector<py> const& a, py const&)
                {
                    if (!is_dataclass_instance(a[0]))
                        py_raise("TypeError", "astuple() should be called on dataclass instances");
                    return astuple_inner(a[0], a[1]);
                },
                "Return the fields of a dataclass instance as a new tuple of field values.\n\n    Example usage::\n\n      @dataclass\n      class C:\n"
                "          x: int\n          y: int\n\n      c = C(1, 2)\n      assert astuple(c) == (1, 2)\n\n"
                "    If given, 'tuple_factory' will be used instead of built-in tuple.\n    The function applies recursively to field values that are\n"
                "    dataclass instances. This will also look into built-in containers:\n    tuples, lists, and dicts. Other objects are copied with 'copy.deepcopy()'.\n    "));
            result.emplace_back("_astuple_inner", module_function("_astuple_inner", { positional("obj"), positional("tuple_factory") },
                [](std::vector<py> const& a, py const&) { return astuple_inner(a[0], a[1]); }, ""));
            std::vector<parameter> makeParameters{ positional("cls_name"), positional("fields"), keyword("bases", py_tuple()), keyword("namespace", none()) };
            for (auto& p : dataclass_parameters())
                makeParameters.push_back(std::move(p));
            makeParameters.push_back(keyword("module", none()));
            result.emplace_back("make_dataclass", module_function("make_dataclass", makeParameters, [](std::vector<py> const& a, py const&) -> py
                {
                    py const namespace_ = a[3].is_none() ? py_dict() : a[3];
                    py const seen = builtin("set")();
                    py const annotations = py_dict();
                    py const defaults = py_dict();
                    for (auto const& item : a[1].items())
                    {
                        py name;
                        py tp;
                        if (is_str(item))
                        {
                            name = item;
                            tp = py{ "typing.Any" };
                        }
                        else if (item.size() == 2)
                        {
                            auto const parts = item.items();
                            if (parts.size() != 2u)
                                py_raise("ValueError", "too many values to unpack (expected 2)");
                            name = parts[0];
                            tp = parts[1];
                        }
                        else if (item.size() == 3)
                        {
                            auto const parts = item.items();
                            if (parts.size() != 3u)
                                py_raise("ValueError", "too many values to unpack (expected 3)");
                            name = parts[0];
                            tp = parts[1];
                            defaults.set_item(name, parts[2]);
                        }
                        else
                            py_raise("TypeError", "Invalid field: " + item.repr());
                        if (!is_str(name) || !name.method("isidentifier").truthy())
                            py_raise("TypeError", "Field names must be valid identifiers: " + name.repr());
                        if (import_attribute("keyword", "iskeyword")(name).truthy())
                            py_raise("TypeError", "Field names must not be keywords: " + name.repr());
                        if (seen.contains(name))
                            py_raise("TypeError", "Field name duplicated: " + name.repr());
                        seen.method("add", { name });
                        annotations.set_item(name, tp);
                    }
                    py const execBody = function("exec_body_callback", "make_dataclass.<locals>.exec_body_callback",
                        [namespace_, defaults, annotations](std::vector<py> const& aArgs, object* aKwargs) -> py
                        {
                            auto const e = py_bind("make_dataclass.<locals>.exec_body_callback", { positional("ns") }, aArgs, aKwargs);
                            e[0].method("update", { namespace_ });
                            e[0].method("update", { defaults });
                            e[0].set_item(py{ "__annotations__" }, annotations);
                            return none();
                        });
                    py const result = import_attribute("types", "new_class")(a[0], a[2], py_dict(), execBody);
                    py module = a[14];
                    if (module.is_none())
                        module = caller_module();
                    if (!module.empty() && !module.is_none())
                        result.set_attr("__module__", module);
                    return apply_dataclass(result, std::vector<py>{ a.begin() + 4, a.begin() + 14 });
                },
                "Return a new dynamically created dataclass.\n\n    The dataclass name will be 'cls_name'.  'fields' is an iterable\n"
                "    of either (name), (name, type) or (name, type, Field) objects. If type is\n    omitted, use the string 'typing.Any'.  Field objects are created by\n"
                "    the equivalent of calling 'field(name, type [, Field-info])'.::\n\n"
                "      C = make_dataclass('C', ['x', ('y', int), ('z', int, field(init=False))], bases=(Base,))\n\n    is equivalent to::\n\n"
                "      @dataclass\n      class C(Base):\n          x: 'typing.Any'\n          y: int\n          z: int = field(init=False)\n\n"
                "    For the bases and namespace parameters, see the builtin type() function.\n\n"
                "    The parameters init, repr, eq, order, unsafe_hash, frozen, match_args, kw_only,\n    slots, and weakref_slot are passed to dataclass().\n\n"
                "    If module parameter is defined, the '__module__' attribute of the dataclass is\n    set to that value.\n    "));
            result.emplace_back("replace", module_function("replace", { parameter{ "obj", parameter::PositionalOnly, {} } }, [](std::vector<py> const& a, py const& aChanges)
                {
                    if (!is_dataclass_instance(a[0]))
                        py_raise("TypeError", "replace() should be called on dataclass instances");
                    return replace_of(a[0], aChanges);
                },
                "Return a new object replacing specified fields with new values.\n\n    This is especially useful for frozen classes.  Example usage::\n\n"
                "      @dataclass(frozen=True)\n      class C:\n          x: int\n          y: int\n\n      c = C(1, 2)\n      c1 = replace(c, x=3)\n"
                "      assert c1.x == 3 and c1.y == 2\n    ", true));
            s.replace = module_function("_replace", { parameter{ "self", parameter::PositionalOnly, {} } },
                [](std::vector<py> const& a, py const& aChanges) { return replace_of(a[0], aChanges); }, "", true);
            result.emplace_back("_replace", s.replace);
            return result;
        }

        bool dataclasses_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            auto const result = py_guarded([&]()
            {
                auto& s = state();
                if (s.classes.empty())
                    make_classes();
                static std::vector<std::pair<std::string, py>> const sFunctions = make_functions();
                for (auto const& [name, f] : sFunctions)
                    aAttributes.emplace_back(name, f.reference());
                for (auto const& [name, c] : s.classes)
                    aAttributes.emplace_back(name, c.reference());
                for (auto const& [name, value] : std::initializer_list<std::pair<char const*, py>>{ { "MISSING", s.missing }, { "KW_ONLY", s.kwOnly },
                    { "_HAS_DEFAULT_FACTORY", s.hasDefaultFactory }, { "_EMPTY_METADATA", s.emptyMetadata }, { "_FIELD", s.fieldMarker },
                    { "_FIELD_CLASSVAR", s.classVarMarker }, { "_FIELD_INITVAR", s.initVarMarker }, { "_ATOMIC_TYPES", s.atomicTypes },
                    { "_FIELDS", py{ "__dataclass_fields__" } }, { "_PARAMS", py{ "__dataclass_params__" } }, { "_POST_INIT_NAME", py{ "__post_init__" } } })
                    aAttributes.emplace_back(name, value.reference());
                aAttributes.emplace_back("_MODULE_IDENTIFIER_RE", import_attribute("re", "compile")(py{ "^(?:\\s*(\\w+)\\s*\\.)?\\s*(\\w+)" }).release());
                std::vector<py> all;
                for (auto name : { "dataclass", "field", "Field", "FrozenInstanceError", "InitVar", "KW_ONLY", "MISSING", "fields", "asdict", "astuple",
                    "make_dataclass", "replace", "is_dataclass" })
                    all.emplace_back(name);
                aAttributes.emplace_back("__all__", py_list(all).release());
                for (auto module : { "re", "sys", "copy", "types", "keyword", "itertools", "abc" })
                    aAttributes.emplace_back(module, py{ fn_import(module) }.release());
                aAttributes.emplace_back("recursive_repr", import_attribute("reprlib", "recursive_repr").release());
                return none();
            });
            release(result);
            return result != nullptr;
        }

        inline bool const sDataclasses = register_module("dataclasses", dataclasses_module);
    }
}
