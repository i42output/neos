/*
  inspect.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/inspect.py), Copyright (c) 2001-2024 Python Software
  Foundation; All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from Python to C++ for the neos Python runtime plugin and adapted to
  its object model (see runtime/python/NOTICE).
*/

// The module inspect (CPython 3.13's inspect.py): the predicates (ismodule, isclass, isfunction, ...), getmembers, classify_class_attrs,
// getdoc and cleandoc, the source functions (getfile, getsourcefile, getmodule, findsource, getcomments, getblock, getsourcelines, getsource;
// getblock's tokens are tokenize's), getclasstree, getfullargspec, getcallargs, getclosurevars, the frame functions (getframeinfo, stack,
// trace, ...), getattr_static, the generator and coroutine states, and Signature, Parameter and BoundArguments (signature)

#include <cctype>
#include <cstring>
#include <functional>
#include <map>
#include <optional>
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
        char const* const sModule = "inspect";

        // (the module's objects: made once (and never destroyed: they outlive the program's objects))
        struct module_state
        {
            std::map<std::string, py, std::less<>> classes;
            py empty;                   ///< (_empty)
            py void_;                   ///< (_void)
            py sentinel;                ///< (_sentinel)
            py kinds[5];                ///< (_ParameterKind's members)
            py parameterKind;           ///< (_ParameterKind)
            py modulesByFile;           ///< (modulesbyfile)
            py filesByModname;          ///< (_filesbymodname)
            py coroutineMark;           ///< (_is_coroutine_mark)
            py positions;               ///< (dis.Positions)
        };
        module_state& state()
        {
            static module_state* sState = new module_state{};
            return *sState;
        }

        enum : int { PositionalOnly, PositionalOrKeyword, VarPositional, KeywordOnly, VarKeyword };

        long long const CO_VARARGS = 0x4;
        long long const CO_VARKEYWORDS = 0x8;
        long long const CO_GENERATOR = 0x20;
        long long const CO_COROUTINE = 0x80;
        long long const CO_ITERABLE_COROUTINE = 0x100;
        long long const CO_ASYNC_GENERATOR = 0x200;

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
        py types_attr(char const* aName) { return import_attribute("types", aName); }

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
        // (isinstance(aObject, types.aType), false if types has no such type)
        bool is_types_instance(py const& aObject, char const* aType)
        {
            py const t = attr_or(py{ fn_import("types") }, aType, py{});
            return !t.empty() && isinstance(aObject, t);
        }
        bool is_callable_object(py const& aObject) { return is_callable(aObject.get()); }
        py dict_of(py const& aObject) { return aObject.attr("__dict__"); }
        py sys_modules() { return import_attribute("sys", "modules"); }
        py os_path(char const* aName) { return import_attribute("os", "path").attr(aName); }
        py get_module(py const& aName) { return sys_modules().method("get", { aName }); }
        long long code_flags(py const& aCode) { return aCode.attr("co_flags").as_index(); }

        // (raise aException from aCause (None: the context is suppressed))
        [[noreturn]] void raise_from(py const& aException, py const& aCause)
        {
            aException.set_attr("__cause__", aCause);
            aException.set_attr("__suppress_context__", boolean(true));
            py_raise(aException);
        }

        // (a closure as a function of the module)
        py function(std::string const& aName, std::string const& aQualname, native_body aBody, char const* aDoc = "")
        {
            return make_native_function(aName, aQualname, aDoc, sModule, std::move(aBody));
        }

        // --- the predicates

        bool ismodule(py const& o) { return isinstance(o, types_attr("ModuleType")); }
        bool isclass(py const& o) { return isinstance(o, builtin("type")); }
        bool ismethod(py const& o) { return isinstance(o, types_attr("MethodType")); }
        bool isfunction(py const& o) { return isinstance(o, types_attr("FunctionType")); }
        bool is_partial(py const& o) { return isinstance(o, import_attribute("functools", "partial")); }
        bool ismethoddescriptor(py const& o)
        {
            if (isclass(o) || ismethod(o) || isfunction(o))
                return false;
            if (is_partial(o))
                return false;
            py const tp = type_of(o);
            return has_attr(tp, "__get__") && !has_attr(tp, "__set__") && !has_attr(tp, "__delete__");
        }
        bool isdatadescriptor(py const& o)
        {
            if (isclass(o) || ismethod(o) || isfunction(o))
                return false;
            py const tp = type_of(o);
            return has_attr(tp, "__set__") || has_attr(tp, "__delete__");
        }
        bool ismemberdescriptor(py const& o) { return is_types_instance(o, "MemberDescriptorType"); }
        bool isgetsetdescriptor(py const& o) { return is_types_instance(o, "GetSetDescriptorType"); }
        bool isbuiltin(py const& o) { return is_types_instance(o, "BuiltinFunctionType"); }
        bool ismethodwrapper(py const& o) { return is_types_instance(o, "MethodWrapperType"); }
        bool isroutine(py const& o) { return isbuiltin(o) || isfunction(o) || ismethod(o) || ismethoddescriptor(o) || ismethodwrapper(o); }
        bool iscode(py const& o) { return is_types_instance(o, "CodeType"); }
        bool isframe(py const& o) { return is_types_instance(o, "FrameType"); }
        bool istraceback(py const& o) { return is_types_instance(o, "TracebackType"); }
        bool isgenerator(py const& o) { return is_types_instance(o, "GeneratorType"); }
        bool iscoroutine(py const& o) { return is_types_instance(o, "CoroutineType"); }
        bool isasyncgen(py const& o) { return is_types_instance(o, "AsyncGeneratorType"); }

        bool signature_is_functionlike(py const& aObject);

        // (functools._unwrap_partial: the function of a chain of partials)
        py unwrap_partial(py aFunction)
        {
            while (is_partial(aFunction))
                aFunction = aFunction.attr("func");
            return aFunction;
        }
        // (functools._unwrap_partialmethod)
        py unwrap_partialmethod(py aFunction)
        {
            py const partialmethod = attr_or(py{ fn_import("functools") }, "partialmethod", py{});
            py const partialMethodAttribute = attr_or(py{ fn_import("functools") }, "_partialmethod_attribute_name", py{ "__partialmethod__" });
            py prev;
            while (prev.empty() || !aFunction.is(prev))
            {
                prev = aFunction;
                while (!partialmethod.empty() && isinstance(attr_or(aFunction, partialMethodAttribute.str().c_str(), none()), partialmethod))
                    aFunction = aFunction.attr(partialMethodAttribute.str().c_str());
                while (!partialmethod.empty() && isinstance(aFunction, partialmethod))
                    aFunction = attr_or(aFunction, "func", aFunction);
                aFunction = unwrap_partial(aFunction);
            }
            return aFunction;
        }

        bool has_code_flag(py f, long long aFlag)
        {
            f = unwrap_partialmethod(f);
            while (ismethod(f))
                f = f.attr("__func__");
            f = unwrap_partial(f);
            if (!(isfunction(f) || signature_is_functionlike(f)))
                return false;
            py const code = attr_or(f, "__code__", py{});
            if (code.empty())
                return false;
            return (code_flags(code) & aFlag) != 0;
        }
        bool has_coroutine_mark(py f)
        {
            while (ismethod(f))
                f = f.attr("__func__");
            f = unwrap_partial(f);
            return attr_or(f, "_is_coroutine_marker", none()).is(state().coroutineMark);
        }

        bool isabstract(py const& aObject)
        {
            if (!isclass(aObject))
                return false;
            // (Py_TPFLAGS_IS_ABSTRACT: a class's abstract methods (classes have no __flags__ here))
            py const flags = attr_or(aObject, "__flags__", none());
            if (flags.is_none())
            {
                if (attr_or(aObject, "__abstractmethods__", py_tuple()).truthy())
                    return true;
            }
            else if ((flags.as_index() & (1ll << 20)) != 0)
                return true;
            if (!issubclass(type_of(aObject), import_attribute("abc", "ABCMeta")))
                return false;
            if (has_attr(aObject, "__abstractmethods__"))
                return false;
            for (auto const& item : dict_of(aObject).method("items").items())
                if (attr_or(item[py{ 1 }], "__isabstractmethod__", boolean(false)).truthy())
                    return true;
            for (auto const& base : aObject.attr("__bases__").items())
                for (auto const& name : attr_or(base, "__abstractmethods__", py_tuple()).items())
                {
                    py const value = attr_or(aObject, name.str().c_str(), none());
                    if (attr_or(value, "__isabstractmethod__", boolean(false)).truthy())
                        return true;
                }
            return false;
        }

        // --- unwrap

        py unwrap(py const& aFunction, std::function<bool(py const&)> const& aStop = {})
        {
            py f = aFunction;
            std::set<object*> memo{ f.get() };
            auto const limit = recursion_limit();
            while (!isinstance(f, builtin("type")) && has_attr(f, "__wrapped__"))
            {
                if (aStop && aStop(f))
                    break;
                f = f.attr("__wrapped__");
                if (memo.contains(f.get()) || static_cast<long long>(memo.size()) >= limit)
                    py_raise("ValueError", "wrapper loop when unwrapping " + aFunction.repr());
                memo.insert(f.get());
            }
            return f;
        }

        // --- getattr_static

        py check_instance(py const& aObject, py const& aAttribute)
        {
            py instanceDict = py_dict();
            try
            {
                instanceDict = builtin("object").attr("__getattribute__")(aObject, py{ "__dict__" });
            }
            catch (raised const&)
            {
                if (!caught({ "AttributeError" }))
                    throw;
            }
            if (!isinstance(instanceDict, builtin("dict")))
                return state().sentinel;
            return builtin("dict").attr("get")(instanceDict, aAttribute, state().sentinel);
        }

        // (the class's MRO (type.__dict__['__mro__'].__get__))
        std::vector<py> static_mro(py const& aClass)
        {
            py const mro = attr_or(aClass, "__mro__", py{});
            if (mro.empty())
                return { aClass };
            return mro.items();
        }

        py shadowed_dict(py const& aClass)
        {
            for (auto const& entry : static_mro(aClass))
            {
                py const dunderDict = attr_or(entry, "__dict__", py{});
                if (dunderDict.empty())
                    continue;
                if (dunderDict.contains(py{ "__dict__" }))
                {
                    py const classDict = dunderDict[py{ "__dict__" }];
                    if (!(type_of(classDict).is(types_attr("GetSetDescriptorType")) && attr_or(classDict, "__name__", none()).repr() == "'__dict__'" &&
                        attr_or(classDict, "__objclass__", none()).is(entry)))
                        return classDict;
                }
            }
            return state().sentinel;
        }

        py check_class(py const& aClass, py const& aAttribute)
        {
            for (auto const& entry : static_mro(aClass))
                if (shadowed_dict(type_of(entry)).is(state().sentinel))
                {
                    py const d = attr_or(entry, "__dict__", py{});
                    if (!d.empty() && d.contains(aAttribute))
                        return d[aAttribute];
                }
            return state().sentinel;
        }

        py getattr_static(py const& aObject, py const& aAttribute, py const& aDefault)
        {
            auto const& sentinel = state().sentinel;
            py instanceResult = sentinel;
            py const objectType = type_of(aObject);
            py klass;
            bool isType = false;
            for (auto const& c : static_mro(objectType))
                if (c.is(builtin("type")))
                    isType = true;
            if (!isType)
            {
                klass = objectType;
                py const dictAttribute = shadowed_dict(klass);
                if (dictAttribute.is(sentinel) || type_of(dictAttribute).is(types_attr("MemberDescriptorType")))
                    instanceResult = check_instance(aObject, aAttribute);
            }
            else
                klass = aObject;
            py const klassResult = check_class(klass, aAttribute);
            if (!instanceResult.is(sentinel) && !klassResult.is(sentinel))
            {
                if (!check_class(type_of(klassResult), py{ "__get__" }).is(sentinel) && (!check_class(type_of(klassResult), py{ "__set__" }).is(sentinel) ||
                    !check_class(type_of(klassResult), py{ "__delete__" }).is(sentinel)))
                    return klassResult;
            }
            if (!instanceResult.is(sentinel))
                return instanceResult;
            if (!klassResult.is(sentinel))
                return klassResult;
            if (aObject.is(klass))
                for (auto const& entry : static_mro(type_of(klass)))
                    if (shadowed_dict(type_of(entry)).is(sentinel))
                    {
                        py const d = attr_or(entry, "__dict__", py{});
                        if (!d.empty() && d.contains(aAttribute))
                            return d[aAttribute];
                    }
            // (a native object (of a type of a module, e.g. io.StringIO's) whose type's __dict__ does not have its methods: its attribute 
            // (its lookup runs no code of the program's))
            if (!is_class(klass.get()) && !is_class(objectType.get()))
            {
                try
                {
                    return builtin("getattr")(aObject, aAttribute);
                }
                catch (raised const&)
                {
                    if (!caught({ "AttributeError" }))
                        throw;
                    py_take_exception();
                }
            }
            if (!aDefault.is(sentinel))
                return aDefault;
            py_raise(builtin("AttributeError")(aAttribute));
        }

        // --- members

        py getmembers_of(py const& aObject, py const& aPredicate, std::function<py(py const&, py const&)> const& aGetter)
        {
            py const results = py_list();
            py const processed = builtin("set")();
            py const names = builtin("dir")(aObject);
            std::vector<py> mro;
            if (isclass(aObject))
            {
                mro = aObject.attr("__mro__").items();
                try
                {
                    py const dynamic = attr_or(py{ fn_import("types") }, "DynamicClassAttribute", py{});
                    for (auto const& base : aObject.attr("__bases__").items())
                        for (auto const& item : dict_of(base).method("items").items())
                            if (!dynamic.empty() && isinstance(item[py{ 1 }], dynamic))
                                names.method("append", { item[py{ 0 }] });
                }
                catch (raised const&)
                {
                    if (!caught({ "AttributeError" }))
                        throw;
                }
            }
            for (auto const& key : names.items())
            {
                py value;
                try
                {
                    value = aGetter(aObject, key);
                    if (processed.contains(key))
                        py_raise("AttributeError", "");
                }
                catch (raised const&)
                {
                    if (!caught({ "AttributeError" }))
                        throw;
                    value = py{};
                    for (auto const& base : mro)
                        if (dict_of(base).contains(key))
                        {
                            value = dict_of(base)[key];
                            break;
                        }
                    if (value.empty())
                        continue;
                }
                if (aPredicate.is_none() || aPredicate(value).truthy())
                    results.method("append", { py_tuple({ key, value }) });
                processed.method("add", { key });
            }
            results.method("sort", {}, { { "key", function("<lambda>", "_getmembers.<locals>.<lambda>", [](std::vector<py> const& aArgs, object*) -> py
                { return aArgs[0][py{ 0 }]; }) } });
            return results;
        }

        py classify_class_attrs(py const& aClass)
        {
            auto const mro = aClass.attr("__mro__").items();
            std::vector<py> metamro;
            for (auto const& c : type_of(aClass).attr("__mro__").items())
                if (!c.is(builtin("type")) && !c.is(builtin("object")))
                    metamro.push_back(c);
            std::vector<py> classBases{ aClass };
            classBases.insert(classBases.end(), mro.begin(), mro.end());
            std::vector<py> allBases = classBases;
            allBases.insert(allBases.end(), metamro.begin(), metamro.end());
            py const names = builtin("dir")(aClass);
            py const dynamic = attr_or(py{ fn_import("types") }, "DynamicClassAttribute", py{});
            for (auto const& base : mro)
                for (auto const& item : dict_of(base).method("items").items())
                    if (!dynamic.empty() && isinstance(item[py{ 1 }], dynamic) && !item[py{ 1 }].attr("fget").is_none())
                        names.method("append", { item[py{ 0 }] });
            auto const contains = [](std::vector<py> const& aClasses, py const& aClass)
            {
                for (auto const& c : aClasses)
                    if (c == aClass)
                        return true;
                return false;
            };
            py const result = py_list();
            py const processed = builtin("set")();
            for (auto const& name : names.items())
            {
                py homeClass = none();
                py getObject;
                py dictObject;
                if (!processed.contains(name))
                {
                    try
                    {
                        if (name.str() == "__dict__")
                            py_raise("Exception", "__dict__ is special, don't want the proxy");
                        getObject = builtin("getattr")(aClass, name);
                    }
                    catch (raised const&)
                    {
                        if (!caught({ "Exception" }))
                            throw;
                        getObject = py{};
                    }
                    if (!getObject.empty())
                    {
                        homeClass = attr_or(getObject, "__objclass__", homeClass);
                        if (!contains(classBases, homeClass))
                        {
                            homeClass = none();
                            py lastClass;
                            for (auto const& searchClass : classBases)
                            {
                                py const searchObject = builtin("getattr")(searchClass, name, none());
                                if (searchObject.is(getObject))
                                    lastClass = searchClass;
                            }
                            for (auto const& searchClass : metamro)
                            {
                                py searchObject;
                                try
                                {
                                    searchObject = searchClass.attr("__getattr__")(aClass, name);
                                }
                                catch (raised const&)
                                {
                                    if (!caught({ "AttributeError" }))
                                        throw;
                                    continue;
                                }
                                if (searchObject.is(getObject))
                                    lastClass = searchClass;
                            }
                            if (!lastClass.empty())
                                homeClass = lastClass;
                        }
                    }
                }
                for (auto const& base : allBases)
                    if (dict_of(base).contains(name))
                    {
                        dictObject = dict_of(base)[name];
                        if (!contains(metamro, homeClass))
                            homeClass = base;
                        break;
                    }
                if (homeClass.is_none())
                    continue;
                py object = !getObject.empty() ? getObject : dictObject.empty() ? none() : dictObject;
                char const* kind = "data";
                if (!dictObject.empty() && (isinstance(dictObject, builtin("staticmethod")) || isinstance(dictObject, types_attr("BuiltinMethodType"))))
                {
                    kind = "static method";
                    object = dictObject;
                }
                else if (!dictObject.empty() && (isinstance(dictObject, builtin("classmethod")) || is_types_instance(dictObject, "ClassMethodDescriptorType")))
                {
                    kind = "class method";
                    object = dictObject;
                }
                else if (!dictObject.empty() && isinstance(dictObject, builtin("property")))
                {
                    kind = "property";
                    object = dictObject;
                }
                else if (isroutine(object))
                    kind = "method";
                result.method("append", { cls("Attribute")(name, py{ kind }, homeClass, object) });
                processed.method("add", { name });
            }
            return result;
        }

        // --- documentation

        long long indentsize(std::string const& aLine)
        {
            std::string const expanded = py{ aLine }.method("expandtabs").str();
            std::size_t i = 0u;
            while (i < expanded.size() && (expanded[i] == ' ' || expanded[i] == '\t' || expanded[i] == '\n' || expanded[i] == '\r' || expanded[i] == '\f' ||
                expanded[i] == '\v'))
                ++i;
            return static_cast<long long>(py{ expanded }.size() - py{ expanded.substr(i) }.size());
        }

        py cleandoc(py const& aDoc)
        {
            std::vector<py> lines = aDoc.method("expandtabs").method("split", { py{ "\n" } }).items();
            std::optional<long long> margin;
            for (std::size_t i = 1u; i < lines.size(); ++i)
            {
                auto const content = lines[i].method("lstrip", { py{ " " } }).size();
                if (content > 0)
                {
                    auto const indent = lines[i].size() - content;
                    margin = margin ? std::min(*margin, indent) : indent;
                }
            }
            if (!lines.empty())
                lines[0] = lines[0].method("lstrip", { py{ " " } });
            if (margin)
                for (std::size_t i = 1u; i < lines.size(); ++i)
                    lines[i] = lines[i][py{ make_slice(py{ *margin }.get(), nullptr, nullptr) }];
            while (!lines.empty() && lines.back().size() == 0)
                lines.pop_back();
            while (!lines.empty() && lines.front().size() == 0)
                lines.erase(lines.begin());
            return py{ "\n" }.method("join", { py_list(lines) });
        }

        py findclass(py const& aFunction)
        {
            py c = get_module(aFunction.attr("__module__"));
            if (c.is_none())
                return none();
            auto const parts = aFunction.attr("__qualname__").method("split", { py{ "." } }).items();
            for (std::size_t i = 0u; i + 1u < parts.size(); ++i)
                c = builtin("getattr")(c, parts[i]);
            if (!isclass(c))
                return none();
            return c;
        }

        py finddoc(py const& aObject)
        {
            if (isclass(aObject))
            {
                for (auto const& base : aObject.attr("__mro__").items())
                    if (!base.is(builtin("object")))
                    {
                        py doc;
                        try
                        {
                            doc = base.attr("__doc__");
                        }
                        catch (raised const&)
                        {
                            if (!caught({ "AttributeError" }))
                                throw;
                            continue;
                        }
                        if (!doc.is_none())
                            return doc;
                    }
                return none();
            }
            py name;
            py klass;
            if (ismethod(aObject))
            {
                name = aObject.attr("__func__").attr("__name__");
                py const self = aObject.attr("__self__");
                if (isclass(self) && attr_or(builtin("getattr")(self, name, none()), "__func__", none()).is(aObject.attr("__func__")))
                    klass = self;
                else
                    klass = self.attr("__class__");
            }
            else if (isfunction(aObject))
            {
                name = aObject.attr("__name__");
                klass = findclass(aObject);
                if (klass.is_none() || !builtin("getattr")(klass, name).is(aObject))
                    return none();
            }
            else if (isbuiltin(aObject))
            {
                name = aObject.attr("__name__");
                py const self = aObject.attr("__self__");
                if (isclass(self) && self.attr("__qualname__").str() + "." + name.str() == aObject.attr("__qualname__").str())
                    klass = self;
                else
                    klass = self.attr("__class__");
            }
            else if (isinstance(aObject, builtin("property")))
            {
                name = aObject.attr("__name__");
                klass = findclass(aObject.attr("fget"));
                if (klass.is_none() || !builtin("getattr")(klass, name).is(aObject))
                    return none();
            }
            else if (ismethoddescriptor(aObject) || isdatadescriptor(aObject))
            {
                name = aObject.attr("__name__");
                klass = aObject.attr("__objclass__");
                if (!builtin("getattr")(klass, name).is(aObject))
                    return none();
                if (ismemberdescriptor(aObject))
                {
                    py const slots = attr_or(klass, "__slots__", none());
                    if (isinstance(slots, builtin("dict")) && slots.contains(name))
                        return slots[name];
                }
            }
            else
                return none();
            for (auto const& base : klass.attr("__mro__").items())
            {
                py doc;
                try
                {
                    doc = builtin("getattr")(base, name).attr("__doc__");
                }
                catch (raised const&)
                {
                    if (!caught({ "AttributeError" }))
                        throw;
                    continue;
                }
                if (!doc.is_none())
                    return doc;
            }
            return none();
        }

        py getdoc(py const& aObject)
        {
            py doc;
            try
            {
                doc = aObject.attr("__doc__");
            }
            catch (raised const&)
            {
                if (!caught({ "AttributeError" }))
                    throw;
                return none();
            }
            if (doc.is_none())
            {
                try
                {
                    doc = finddoc(aObject);
                }
                catch (raised const&)
                {
                    if (!caught({ "AttributeError", "TypeError" }))
                        throw;
                    return none();
                }
            }
            if (!is_str(doc))
                return none();
            return cleandoc(doc);
        }

        // --- source

        bool ends_with(std::string const& aText, std::string const& aSuffix)
        {
            return aText.size() >= aSuffix.size() && aText.compare(aText.size() - aSuffix.size(), aSuffix.size(), aSuffix) == 0;
        }

        // (importlib.machinery's suffixes)
#ifdef _WIN32
        std::vector<std::string> const sSourceSuffixes{ ".py", ".pyw" };
        std::vector<std::string> const sExtensionSuffixes{ ".cp313-win_amd64.pyd", ".pyd" };
#else
        std::vector<std::string> const sSourceSuffixes{ ".py" };
        std::vector<std::string> const sExtensionSuffixes{ ".cpython-313-x86_64-linux-gnu.so", ".abi3.so", ".so" };
#endif
        std::vector<std::string> const sBytecodeSuffixes{ ".pyc" };

        py getfile(py aObject)
        {
            if (ismodule(aObject))
            {
                py const file = attr_or(aObject, "__file__", none());
                if (file.truthy())
                    return aObject.attr("__file__");
                py_raise("TypeError", aObject.repr() + " is a built-in module");
            }
            if (isclass(aObject))
            {
                if (has_attr(aObject, "__module__"))
                {
                    py const module = get_module(aObject.attr("__module__"));
                    if (attr_or(module, "__file__", none()).truthy())
                        return module.attr("__file__");
                    if (aObject.attr("__module__") == py{ "__main__" })
                        py_raise("OSError", "source code not available");
                }
                py_raise("TypeError", aObject.repr() + " is a built-in class");
            }
            if (ismethod(aObject))
                aObject = aObject.attr("__func__");
            if (isfunction(aObject))
                aObject = aObject.attr("__code__");
            if (istraceback(aObject))
                aObject = aObject.attr("tb_frame");
            if (isframe(aObject))
                aObject = aObject.attr("f_code");
            if (iscode(aObject))
                return aObject.attr("co_filename");
            py_raise("TypeError", "module, class, method, function, traceback, frame, or code object was expected, got " + type_of(aObject).attr("__name__").str());
        }

        py getmodulename(py const& aPath)
        {
            std::string const name = os_path("basename")(aPath).str();
            std::vector<std::string> suffixes = sSourceSuffixes;
            suffixes.insert(suffixes.end(), sBytecodeSuffixes.begin(), sBytecodeSuffixes.end());
            suffixes.insert(suffixes.end(), sExtensionSuffixes.begin(), sExtensionSuffixes.end());
            std::stable_sort(suffixes.begin(), suffixes.end(), [](std::string const& a, std::string const& b) { return a.size() > b.size() || (a.size() == b.size() && a < b); });
            for (auto const& s : suffixes)
                if (ends_with(name, s))
                    return py{ py{ name }[py{ make_slice(nullptr, py{ -static_cast<long long>(py{ s }.size()) }.get(), nullptr) }] };
            return none();
        }

        py getmodule(py const& aObject, py const& aFilename);

        py getabsfile(py const& aObject, py aFilename)
        {
            extern py getsourcefile(py const& aObject);
            if (aFilename.is_none())
            {
                aFilename = getsourcefile(aObject);
                if (!aFilename.truthy())
                    aFilename = getfile(aObject);
            }
            return os_path("normcase")(os_path("abspath")(aFilename));
        }

        py getsourcefile(py const& aObject)
        {
            std::string filename = getfile(aObject).str();
            bool bytecode = false;
            for (auto const& s : sBytecodeSuffixes)
                if (ends_with(filename, s))
                    bytecode = true;
            if (bytecode)
                filename = os_path("splitext")(py{ filename })[py{ 0 }].str() + sSourceSuffixes[0];
            else
            {
                for (auto const& s : sExtensionSuffixes)
                    if (ends_with(filename, s))
                        return none();
                if (ends_with(filename, ".fwork"))
                    return none();
            }
            if (import_attribute("linecache", "cache").contains(py{ filename }))
                return py{ filename };
            if (os_path("exists")(py{ filename }).truthy())
                return py{ filename };
            py const module = getmodule(aObject, py{ filename });
            if (!attr_or(module, "__loader__", none()).is_none())
                return py{ filename };
            if (!attr_or(attr_or(module, "__spec__", none()), "loader", none()).is_none())
                return py{ filename };
            return none();
        }

        py getmodule(py const& aObject, py const& aFilename)
        {
            if (ismodule(aObject))
                return aObject;
            if (has_attr(aObject, "__module__"))
                return get_module(aObject.attr("__module__"));
            auto& s = state();
            if (!aFilename.is_none() && s.modulesByFile.contains(aFilename))
                return get_module(s.modulesByFile[aFilename]);
            py file;
            try
            {
                file = getabsfile(aObject, aFilename);
            }
            catch (raised const&)
            {
                if (!caught({ "TypeError", "FileNotFoundError" }))
                    throw;
                return none();
            }
            if (s.modulesByFile.contains(file))
                return get_module(s.modulesByFile[file]);
            for (auto const& item : sys_modules().method("copy").method("items").items())
            {
                py const modname = item[py{ 0 }];
                py const module = item[py{ 1 }];
                if (ismodule(module) && has_attr(module, "__file__"))
                {
                    py const f = module.attr("__file__");
                    if (f == s.filesByModname.method("get", { modname, none() }))
                        continue;
                    s.filesByModname.set_item(modname, f);
                    py const absolute = getabsfile(module, none());
                    s.modulesByFile.set_item(absolute, module.attr("__name__"));
                    s.modulesByFile.set_item(os_path("realpath")(absolute), module.attr("__name__"));
                }
            }
            if (s.modulesByFile.contains(file))
                return get_module(s.modulesByFile[file]);
            py const main = sys_modules()[py{ "__main__" }];
            if (!has_attr(aObject, "__name__"))
                return none();
            std::string const name = aObject.attr("__name__").str();
            if (has_attr(main, name.c_str()))
            {
                if (main.attr(name.c_str()).is(aObject))
                    return main;
            }
            py const builtins = sys_modules()[py{ "builtins" }];
            if (has_attr(builtins, name.c_str()))
            {
                if (builtins.attr(name.c_str()).is(aObject))
                    return builtins;
            }
            return none();
        }

        // (the first line of the decorators of the def or class at aLine (its first line here is its def's or class's line; CPython's is 
        // its first decorator's))
        py decorators_start(py const& aLines, py const& aLine)
        {
            auto line = aLine.as_index();
            if (line <= 0 || line >= aLines.size())
                return aLine;
            auto const text = [&](long long i) { return aLines[py{ i }].str(); };
            auto const indent = indentsize(text(line));
            auto start = line;
            for (auto k = line - 1; k >= 0; --k)
            {
                std::string const stripped = py{ text(k) }.method("lstrip").str();
                if (stripped.empty() || stripped.starts_with("#"))
                    break;
                auto const i = indentsize(text(k));
                if (i == indent && stripped.starts_with("@"))
                    start = k;
                else if (i <= indent)
                    break;
            }
            return py{ start };
        }

        py findsource(py aObject)
        {
            py file = getsourcefile(aObject);
            if (file.truthy())
                import_attribute("linecache", "checkcache")(file);
            else
            {
                file = getfile(aObject);
                std::string const f = file.str();
                if (!(!f.empty() && f.front() == '<' && f.back() == '>') || ends_with(f, ".fwork"))
                    py_raise("OSError", "source code not available");
            }
            py const module = getmodule(aObject, file);
            py lines;
            if (module.truthy())
            {
                lines = import_attribute("linecache", "getlines")(file, dict_of(module));
                if (!lines.truthy() && file.str().starts_with("<") && has_attr(aObject, "__code__"))
                {
                    py const fromCode = attr_or(py{ fn_import("linecache") }, "_getlines_from_code", py{});
                    if (!fromCode.empty())
                        lines = fromCode(aObject.attr("__code__"));
                }
            }
            else
                lines = import_attribute("linecache", "getlines")(file);
            if (!lines.truthy())
                py_raise("OSError", "could not get source code");
            if (ismodule(aObject))
                return py_tuple({ lines, py{ 0 } });
            if (isclass(aObject))
            {
                py lnum;
                try
                {
                    lnum = builtin("vars")(aObject)[py{ "__firstlineno__" }] - py{ 1 };
                }
                catch (raised const&)
                {
                    if (!caught({ "TypeError", "KeyError" }))
                        throw;
                    py_raise("OSError", "source code not available");
                }
                if (lnum.as_index() >= lines.size())
                    py_raise("OSError", "lineno is out of bounds");
                return py_tuple({ lines, decorators_start(lines, lnum) });
            }
            if (ismethod(aObject))
                aObject = aObject.attr("__func__");
            if (isfunction(aObject))
                aObject = aObject.attr("__code__");
            if (istraceback(aObject))
                aObject = aObject.attr("tb_frame");
            if (isframe(aObject))
                aObject = aObject.attr("f_code");
            if (iscode(aObject))
            {
                if (!has_attr(aObject, "co_firstlineno"))
                    py_raise("OSError", "could not find function definition");
                py const lnum = aObject.attr("co_firstlineno") - py{ 1 };
                if (lnum.as_index() >= lines.size())
                    py_raise("OSError", "lineno is out of bounds");
                return py_tuple({ lines, decorators_start(lines, lnum) });
            }
            py_raise("OSError", "could not find code object");
        }

        py getcomments(py const& aObject)
        {
            py found;
            try
            {
                found = findsource(aObject);
            }
            catch (raised const&)
            {
                if (!caught({ "OSError", "TypeError" }))
                    throw;
                return none();
            }
            auto const lines = found[py{ 0 }].items();
            auto const lnum = found[py{ 1 }].as_index();
            auto const text = [&](long long i) { return lines[static_cast<std::size_t>(i)].str(); };
            auto const lstripped = [](std::string const& s) { return py{ s }.method("lstrip").str(); };
            auto const stripped = [](std::string const& s) { return py{ s }.method("strip").str(); };
            auto const expanded = [](std::string const& s) { return py{ s }.method("expandtabs").str(); };
            long long const count = static_cast<long long>(lines.size());
            if (ismodule(aObject))
            {
                long long start = 0;
                if (count > 0 && text(0).starts_with("#!"))
                    start = 1;
                while (start < count && (stripped(text(start)).empty() || stripped(text(start)) == "#"))
                    ++start;
                if (start < count && text(start).starts_with("#"))
                {
                    std::string comments;
                    auto end = start;
                    while (end < count && text(end).starts_with("#"))
                    {
                        comments += expanded(text(end));
                        ++end;
                    }
                    return py{ comments };
                }
            }
            else if (lnum > 0)
            {
                auto const indent = indentsize(text(lnum));
                auto end = lnum - 1;
                if (end >= 0 && lstripped(text(end)).starts_with("#") && indentsize(text(end)) == indent)
                {
                    std::vector<std::string> comments{ lstripped(expanded(text(end))) };
                    if (end > 0)
                    {
                        --end;
                        auto comment = lstripped(expanded(text(end)));
                        while (comment.starts_with("#") && indentsize(text(end)) == indent)
                        {
                            comments.insert(comments.begin(), comment);
                            --end;
                            if (end < 0)
                                break;
                            comment = lstripped(expanded(text(end)));
                        }
                    }
                    while (!comments.empty() && stripped(comments.front()) == "#")
                        comments.erase(comments.begin());
                    while (!comments.empty() && stripped(comments.back()) == "#")
                        comments.pop_back();
                    std::string result;
                    for (auto const& c : comments)
                        result += c;
                    return py{ result };
                }
            }
            return none();
        }

        // --- getblock: the tokens of the lines (as tokenize.generate_tokens makes them) given to a BlockFinder

        enum token_type { ENDMARKER = 0, NAME = 1, NUMBER = 2, STRING = 3, NEWLINE = 4, INDENT = 5, DEDENT = 6, OP = 55, COMMENT = 65, NL = 66, ERRORTOKEN = 67 };

        struct block_finder
        {
            long long indent = 0;
            bool singleline = false;
            bool started = false;
            bool passline = false;
            bool indecorator = false;
            long long last = 1;
            std::optional<long long> bodyCol0;
            struct end_of_block {};
            void tokeneater(int aType, std::string const& aToken, std::pair<long long, long long> aStart, std::pair<long long, long long> aEnd)
            {
                if (!started && !indecorator)
                {
                    if (aType == INDENT || aType == COMMENT || aType == NL)
                        ;
                    else if (aToken == "async")
                        ;
                    else if (aToken == "@")
                        indecorator = true;
                    else
                    {
                        singleline = aToken != "def" && aToken != "class";
                        started = true;
                    }
                    passline = true;
                }
                else if (aType == NEWLINE)
                {
                    passline = false;
                    last = aStart.first;
                    if (singleline)
                        throw end_of_block{};
                    if (indecorator)
                        indecorator = false;
                }
                else if (passline)
                    ;
                else if (aType == INDENT)
                {
                    if (!bodyCol0 && started)
                        bodyCol0 = aEnd.second;
                    ++indent;
                    passline = true;
                }
                else if (aType == DEDENT)
                {
                    --indent;
                    if (indent <= 0)
                        throw end_of_block{};
                }
                else if (aType == COMMENT)
                {
                    if (bodyCol0 && aStart.second >= *bodyCol0)
                        last = aStart.first;
                }
                else if (indent == 0 && aType != COMMENT && aType != NL)
                    throw end_of_block{};
            }
        };

        // (Python's tokens of lines of source (enough of tokenize's for BlockFinder): aEat(type, string, start, end); false: an indentation error
        // (tokenize raises IndentationError))
        bool tokenize_lines(std::vector<std::string> const& aLines, std::function<void(int, std::string const&, std::pair<long long, long long>, std::pair<long long, long long>)> const& aEat)
        {
            std::vector<long long> indents{ 0 };
            long long parenlev = 0;
            bool continued = false;
            std::string stringQuote;        // (the quote of a string that continues on the next line)
            long long row = 0;
            auto const nameStart = [](unsigned char c) { return std::isalpha(c) || c == '_' || c >= 0x80u; };
            auto const nameChar = [](unsigned char c) { return std::isalnum(c) || c == '_' || c >= 0x80u; };
            bool lastNewline = true;
            for (auto const& line : aLines)
            {
                ++row;
                std::size_t pos = 0u;
                std::size_t const max = line.size();
                if (!stringQuote.empty())
                {
                    // (the rest of a triple-quoted (or continued) string)
                    auto const end = line.find(stringQuote);
                    if (end == std::string::npos)
                        continue;
                    pos = end + stringQuote.size();
                    stringQuote.clear();
                }
                else if (parenlev == 0 && !continued)
                {
                    long long column = 0;
                    while (pos < max)
                    {
                        if (line[pos] == ' ')
                            ++column;
                        else if (line[pos] == '\t')
                            column = (column / 8 + 1) * 8;
                        else if (line[pos] == '\f')
                            column = 0;
                        else
                            break;
                        ++pos;
                    }
                    if (pos == max)
                        break;
                    if (line[pos] == '#' || line[pos] == '\r' || line[pos] == '\n')
                    {
                        if (line[pos] == '#')
                        {
                            auto const comment = line.substr(pos, line.find_first_of("\r\n", pos) - pos);
                            aEat(COMMENT, comment, { row, static_cast<long long>(pos) }, { row, static_cast<long long>(pos + comment.size()) });
                            pos += comment.size();
                        }
                        aEat(NL, line.substr(pos), { row, static_cast<long long>(pos) }, { row, static_cast<long long>(max) });
                        continue;
                    }
                    if (column > indents.back())
                    {
                        indents.push_back(column);
                        aEat(INDENT, line.substr(0u, pos), { row, 0 }, { row, static_cast<long long>(pos) });
                    }
                    while (column < indents.back())
                    {
                        indents.pop_back();
                        if (column > indents.back())
                            return false;
                        aEat(DEDENT, "", { row, static_cast<long long>(pos) }, { row, static_cast<long long>(pos) });
                    }
                }
                else
                    continued = false;
                while (pos < max)
                {
                    auto const c = static_cast<unsigned char>(line[pos]);
                    if (c == ' ' || c == '\t' || c == '\f')
                    {
                        ++pos;
                        continue;
                    }
                    auto const start = pos;
                    if (c == '#')
                    {
                        auto const comment = line.substr(pos, line.find_first_of("\r\n", pos) - pos);
                        aEat(COMMENT, comment, { row, static_cast<long long>(pos) }, { row, static_cast<long long>(pos + comment.size()) });
                        pos += comment.size();
                        continue;
                    }
                    if (c == '\r' || c == '\n')
                    {
                        aEat(parenlev > 0 ? NL : NEWLINE, line.substr(pos), { row, static_cast<long long>(pos) }, { row, static_cast<long long>(max) });
                        lastNewline = true;
                        pos = max;
                        break;
                    }
                    if (c == '\\' && (pos + 1u == max || line[pos + 1u] == '\n' || line[pos + 1u] == '\r'))
                    {
                        continued = true;
                        pos = max;
                        break;
                    }
                    lastNewline = false;
                    // (a string (with a prefix))
                    std::size_t q = pos;
                    while (q < max && q - pos < 3u && std::strchr("rRbBuUfF", line[q]) != nullptr && line[q] != '\0')
                        ++q;
                    if (q < max && (line[q] == '\'' || line[q] == '"') && (q == pos || nameStart(c)))
                    {
                        char const quote = line[q];
                        bool const raw = line.substr(pos, q - pos).find_first_of("rR") != std::string::npos;
                        bool const triple = q + 2u < max && line[q + 1u] == quote && line[q + 2u] == quote;
                        std::string const delimiter = triple ? std::string(3u, quote) : std::string(1u, quote);
                        std::size_t i = q + delimiter.size();
                        bool closed = false;
                        while (i < max)
                        {
                            if (line[i] == '\\' && !raw)
                            {
                                i += 2u;
                                continue;
                            }
                            if (line.compare(i, delimiter.size(), delimiter) == 0)
                            {
                                i += delimiter.size();
                                closed = true;
                                break;
                            }
                            if (!triple && (line[i] == '\n' || line[i] == '\r'))
                                break;
                            ++i;
                        }
                        if (!closed)
                        {
                            if (triple || (i >= 1u && line[i - 1u] == '\\') || (i + 1u < max && line[i] == '\\'))
                            {
                                stringQuote = delimiter;
                                pos = max;
                                break;
                            }
                            aEat(ERRORTOKEN, line.substr(pos, 1u), { row, static_cast<long long>(pos) }, { row, static_cast<long long>(pos + 1u) });
                            pos = i;
                            continue;
                        }
                        aEat(STRING, line.substr(pos, i - pos), { row, static_cast<long long>(pos) }, { row, static_cast<long long>(i) });
                        pos = i;
                        continue;
                    }
                    if (nameStart(c))
                    {
                        while (pos < max && nameChar(static_cast<unsigned char>(line[pos])))
                            ++pos;
                        aEat(NAME, line.substr(start, pos - start), { row, static_cast<long long>(start) }, { row, static_cast<long long>(pos) });
                        continue;
                    }
                    if (std::isdigit(c) || (c == '.' && pos + 1u < max && std::isdigit(static_cast<unsigned char>(line[pos + 1u]))))
                    {
                        while (pos < max && (std::isalnum(static_cast<unsigned char>(line[pos])) || line[pos] == '.' || line[pos] == '_' ||
                            ((line[pos] == '+' || line[pos] == '-') && (line[pos - 1u] == 'e' || line[pos - 1u] == 'E'))))
                            ++pos;
                        aEat(NUMBER, line.substr(start, pos - start), { row, static_cast<long long>(start) }, { row, static_cast<long long>(pos) });
                        continue;
                    }
                    // (an operator)
                    static char const* const sOperators[] = { "**=", "//=", ">>=", "<<=", "...", "!=", "%=", "&=", "**", "*=", "+=", "-=", "->", "//", "/=", ":=", "<<",
                        "<=", "==", ">=", ">>", "@=", "^=", "|=" };
                    std::size_t length = 1u;
                    for (auto o : sOperators)
                        if (line.compare(pos, std::strlen(o), o) == 0)
                        {
                            length = std::strlen(o);
                            break;
                        }
                    if (c == '(' || c == '[' || c == '{')
                        ++parenlev;
                    else if (c == ')' || c == ']' || c == '}')
                        --parenlev;
                    aEat(OP, line.substr(pos, length), { row, static_cast<long long>(pos) }, { row, static_cast<long long>(pos + length) });
                    pos += length;
                }
            }
            if (!lastNewline && row > 0)
                aEat(NEWLINE, "", { row, static_cast<long long>(aLines.back().size()) }, { row, static_cast<long long>(aLines.back().size()) + 1 });
            for (std::size_t i = 1u; i < indents.size(); ++i)
                aEat(DEDENT, "", { row + 1, 0 }, { row + 1, 0 });
            aEat(ENDMARKER, "", { row + 1, 0 }, { row + 1, 0 });
            return true;
        }

        py getblock(py const& aLines)
        {
            auto const items = aLines.items();
            std::vector<std::string> lines;
            for (auto const& l : items)
                lines.push_back(l.str());
            block_finder finder;
            try
            {
                tokenize_lines(lines, [&](int aType, std::string const& aToken, std::pair<long long, long long> aStart, std::pair<long long, long long> aEnd)
                {
                    finder.tokeneater(aType, aToken, aStart, aEnd);
                });
            }
            catch (block_finder::end_of_block const&)
            {
            }
            return aLines[py{ make_slice(nullptr, py{ finder.last }.get(), nullptr) }];
        }

        py getsourcelines(py aObject)
        {
            aObject = unwrap(aObject);
            py const found = findsource(aObject);
            py const lines = found[py{ 0 }];
            py const lnum = found[py{ 1 }];
            if (istraceback(aObject))
                aObject = aObject.attr("tb_frame");
            if (ismodule(aObject) || (isframe(aObject) && aObject.attr("f_code").attr("co_name") == py{ "<module>" }))
                return py_tuple({ lines, py{ 0 } });
            return py_tuple({ getblock(lines[py{ make_slice(lnum.get(), nullptr, nullptr) }]), lnum + py{ 1 } });
        }

        py getsource(py const& aObject)
        {
            return py{ "" }.method("join", { getsourcelines(aObject)[py{ 0 }] });
        }

        // --- class trees

        py walktree(py const& aClasses, py const& aChildren, py const& aParent)
        {
            py const results = py_list();
            aClasses.method("sort", {}, { { "key", import_attribute("operator", "attrgetter")(py{ "__module__" }, py{ "__name__" }) } });
            for (auto const& c : aClasses.items())
            {
                results.method("append", { py_tuple({ c, c.attr("__bases__") }) });
                if (aChildren.contains(c))
                    results.method("append", { walktree(aChildren[c], aChildren, c) });
            }
            return results;
        }

        py getclasstree(py const& aClasses, bool aUnique)
        {
            py const children = py_dict();
            py const roots = py_list();
            for (auto const& c : aClasses.items())
            {
                if (c.attr("__bases__").truthy())
                {
                    for (auto const& parent : c.attr("__bases__").items())
                    {
                        if (!children.contains(parent))
                            children.set_item(parent, py_list());
                        if (!children[parent].contains(c))
                            children[parent].method("append", { c });
                        if (aUnique && aClasses.contains(parent))
                            break;
                    }
                }
                else if (!roots.contains(c))
                    roots.method("append", { c });
            }
            for (auto const& parent : children.items())
                if (!aClasses.contains(parent))
                    roots.method("append", { parent });
            return walktree(roots, children, none());
        }

        // --- annotations

        py get_annotations(py const& aObject, py aGlobals, py aLocals, bool aEvalStr)
        {
            py annotations;
            py objectGlobals = none();
            py objectLocals = none();
            py unwrapped = none();
            if (isclass(aObject))
            {
                py const objectDict = attr_or(aObject, "__dict__", none());
                if (objectDict.truthy() && has_attr(objectDict, "get"))
                {
                    annotations = objectDict.method("get", { py{ "__annotations__" }, none() });
                    if (is_types_instance(annotations, "GetSetDescriptorType"))
                        annotations = none();
                }
                else
                    annotations = none();
                py const moduleName = attr_or(aObject, "__module__", none());
                if (moduleName.truthy())
                {
                    py const module = sys_modules().method("get", { moduleName, none() });
                    if (module.truthy())
                        objectGlobals = attr_or(module, "__dict__", none());
                }
                objectLocals = builtin("dict")(builtin("vars")(aObject));
                unwrapped = aObject;
            }
            else if (ismodule(aObject))
            {
                annotations = attr_or(aObject, "__annotations__", none());
                objectGlobals = aObject.attr("__dict__");
            }
            else if (is_callable_object(aObject))
            {
                annotations = attr_or(aObject, "__annotations__", none());
                objectGlobals = attr_or(aObject, "__globals__", none());
                unwrapped = aObject;
            }
            else
                py_raise("TypeError", aObject.repr() + " is not a module, class, or callable.");
            if (annotations.is_none())
                return py_dict();
            if (!isinstance(annotations, builtin("dict")))
                py_raise("ValueError", aObject.repr() + ".__annotations__ is neither a dict nor None");
            if (!annotations.truthy())
                return py_dict();
            if (!aEvalStr)
                return builtin("dict")(annotations);
            if (!unwrapped.is_none())
            {
                for (;;)
                {
                    if (has_attr(unwrapped, "__wrapped__"))
                    {
                        unwrapped = unwrapped.attr("__wrapped__");
                        continue;
                    }
                    if (is_partial(unwrapped))
                    {
                        unwrapped = unwrapped.attr("func");
                        continue;
                    }
                    break;
                }
                if (has_attr(unwrapped, "__globals__"))
                    objectGlobals = unwrapped.attr("__globals__");
            }
            if (aGlobals.is_none())
                aGlobals = objectGlobals;
            if (aLocals.is_none())
                aLocals = objectLocals.truthy() ? objectLocals : py_dict();
            py const typeParams = attr_or(aObject, "__type_params__", py_tuple());
            if (typeParams.truthy())
            {
                py const merged = py_dict();
                for (auto const& p : typeParams.items())
                    merged.set_item(p.attr("__name__"), p);
                merged.method("update", { aLocals });
                aLocals = merged;
            }
            py const result = py_dict();
            for (auto const& item : annotations.method("items").items())
            {
                py value = item[py{ 1 }];
                if (is_str(value))
                {
                    std::string text = value.str();
                    // (typing._rewrite_star_unpack: "*Ts" is "Unpack[Ts]")
                    if (text.starts_with("*"))
                    {
                        py const rewrite = attr_or(get_module(py{ "typing" }), "_rewrite_star_unpack", py{});
                        if (!rewrite.empty() && !rewrite.is_none())
                            text = rewrite(py{ text }).str();
                    }
                    value = builtin("eval")(py{ text }, aGlobals, aLocals);
                }
                result.set_item(item[py{ 0 }], value);
            }
            return result;
        }

        // --- formatannotation

        py formatannotation(py const& aAnnotation, py const& aBaseModule)
        {
            py const module = attr_or(aAnnotation, "__module__", none());
            if (is_str(module) && module.str() == "typing")
            {
                std::string const text = aAnnotation.repr();
                std::string result;
                auto const word = [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '.' || static_cast<unsigned char>(c) >= 0x80u; };
                for (std::size_t i = 0u; i < text.size();)
                {
                    if (word(text[i]))
                    {
                        auto j = i;
                        while (j < text.size() && word(text[j]))
                            ++j;
                        std::string token = text.substr(i, j - i);
                        if (token.starts_with("typing."))
                            token.erase(0u, 7u);
                        result += token;
                        i = j;
                    }
                    else
                        result += text[i++];
                }
                return py{ result };
            }
            if (isinstance(aAnnotation, types_attr("GenericAlias")))
                return py{ aAnnotation.str() };
            if (isinstance(aAnnotation, builtin("type")))
            {
                py const m = aAnnotation.attr("__module__");
                if (m == py{ "builtins" } || (!aBaseModule.is_none() && m == aBaseModule))
                    return aAnnotation.attr("__qualname__");
                return py{ m.str() + "." + aAnnotation.attr("__qualname__").str() };
            }
            return py{ aAnnotation.repr() };
        }

        // --- Parameter, BoundArguments and Signature

        bool is_empty(py const& aValue) { return aValue.is(state().empty); }
        int kind_of_parameter(py const& aParameter) { return static_cast<int>(aParameter.attr("_kind").as_index()); }
        py kind(int aKind) { return state().kinds[aKind]; }

        py make_parameter(py const& aClass, py const& aName, py const& aKind, py const& aDefault, py const& aAnnotation)
        {
            return aClass.call({ aName, aKind }, { { "default", aDefault }, { "annotation", aAnnotation } });
        }
        py parameter(py const& aName, int aKind, py const& aDefault, py const& aAnnotation)
        {
            return make_parameter(cls("Parameter"), aName, kind(aKind), aDefault, aAnnotation);
        }

        py parameter_init(std::vector<py> const& a)
        {
            auto& s = state();
            py const& self = a[0];
            py kindValue;
            try
            {
                kindValue = s.parameterKind(a[2]);
            }
            catch (raised const&)
            {
                if (!caught({ "ValueError" }))
                    throw;
                py_raise("ValueError", "value " + a[2].repr() + " is not a valid Parameter.kind");
            }
            self.set_attr("_kind", kindValue);
            int k = static_cast<int>(kindValue.as_index());
            if (!is_empty(a[3]) && (k == VarPositional || k == VarKeyword))
                py_raise("ValueError", kindValue.attr("description").str() + " parameters cannot have default values");
            self.set_attr("_default", a[3]);
            self.set_attr("_annotation", a[4]);
            py name = a[1];
            if (is_empty(name))
                py_raise("ValueError", "name is a required attribute for Parameter");
            if (!is_str(name))
                py_raise("TypeError", "name must be a str, not a " + type_of(name).attr("__name__").str());
            std::string text = name.str();
            if (!text.empty() && text[0] == '.' && py{ text.substr(1u) }.method("isdigit").truthy())
            {
                if (k != PositionalOrKeyword)
                    py_raise("ValueError", "implicit arguments must be passed as positional or keyword arguments, not " + kindValue.attr("description").str());
                self.set_attr("_kind", kind(PositionalOnly));
                k = PositionalOnly;
                name = py{ "implicit" + text.substr(1u) };
            }
            bool const isKeyword = import_attribute("keyword", "iskeyword")(name).truthy() && k != PositionalOnly;
            if (isKeyword || !name.method("isidentifier").truthy())
                py_raise("ValueError", name.repr() + " is not a valid parameter name");
            self.set_attr("_name", name);
            return none();
        }

        std::string parameter_str(py const& aSelf)
        {
            int const k = kind_of_parameter(aSelf);
            std::string formatted = aSelf.attr("_name").str();
            py const annotation = aSelf.attr("_annotation");
            py const defaultValue = aSelf.attr("_default");
            if (!is_empty(annotation))
                formatted += ": " + formatannotation(annotation, none()).str();
            if (!is_empty(defaultValue))
                formatted += (is_empty(annotation) ? "=" : " = ") + defaultValue.repr();
            if (k == VarPositional)
                formatted = "*" + formatted;
            else if (k == VarKeyword)
                formatted = "**" + formatted;
            return formatted;
        }

        py signature_parameters(py const& aSignature) { return aSignature.attr("_parameters"); }
        std::vector<py> parameters_of(py const& aSignature) { return signature_parameters(aSignature).method("values").items(); }

        py signature_init(std::vector<py> const& a)
        {
            auto& s = state();
            py const& self = a[0];
            py const& parameters = a[1];
            py params = import_attribute("collections", "OrderedDict")();
            if (!parameters.is_none())
            {
                if (a[3].truthy())
                {
                    int topKind = PositionalOnly;
                    bool seenDefault = false;
                    for (auto const& p : parameters.items())
                    {
                        py const kindValue = p.attr("kind");
                        int const k = static_cast<int>(kindValue.as_index());
                        py const name = p.attr("name");
                        if (k < topKind)
                            py_raise("ValueError", "wrong parameter order: " + kind(topKind).attr("description").str() + " parameter before " +
                                kindValue.attr("description").str() + " parameter");
                        else if (k > topKind)
                            topKind = k;
                        if (k == PositionalOnly || k == PositionalOrKeyword)
                        {
                            if (is_empty(p.attr("default")))
                            {
                                if (seenDefault)
                                    py_raise("ValueError", "non-default argument follows default argument");
                            }
                            else
                                seenDefault = true;
                        }
                        if (params.contains(name))
                            py_raise("ValueError", "duplicate parameter name: " + name.repr());
                        params.set_item(name, p);
                    }
                }
                else
                    for (auto const& p : parameters.items())
                        params.set_item(p.attr("name"), p);
            }
            self.set_attr("_parameters", types_attr("MappingProxyType")(params));
            self.set_attr("_return_annotation", a[2]);
            (void)s;
            return none();
        }

        py make_signature(py const& aClass, std::vector<py> const& aParameters, py const& aReturnAnnotation, bool aValidate = true)
        {
            return aClass.call({ py_list(aParameters) }, { { "return_annotation", aReturnAnnotation }, { "__validate_parameters__", boolean(aValidate) } });
        }

        py signature_replace(py const& aSelf, py aParameters, py aReturnAnnotation)
        {
            auto& s = state();
            if (aParameters.is(s.void_))
                aParameters = signature_parameters(aSelf).method("values");
            if (aReturnAnnotation.is(s.void_))
                aReturnAnnotation = aSelf.attr("_return_annotation");
            return type_of(aSelf).call({ aParameters }, { { "return_annotation", aReturnAnnotation } });
        }

        std::string signature_format(py const& aSelf, py const& aMaxWidth)
        {
            std::vector<std::string> result;
            bool renderPosOnlySeparator = false;
            bool renderKwOnlySeparator = true;
            for (auto const& p : parameters_of(aSelf))
            {
                std::string const formatted = py{ builtin("str")(p) }.str();
                int const k = static_cast<int>(p.attr("kind").as_index());
                if (k == PositionalOnly)
                    renderPosOnlySeparator = true;
                else if (renderPosOnlySeparator)
                {
                    result.push_back("/");
                    renderPosOnlySeparator = false;
                }
                if (k == VarPositional)
                    renderKwOnlySeparator = false;
                else if (k == KeywordOnly && renderKwOnlySeparator)
                {
                    result.push_back("*");
                    renderKwOnlySeparator = false;
                }
                result.push_back(formatted);
            }
            if (renderPosOnlySeparator)
                result.push_back("/");
            std::string joined;
            for (std::size_t i = 0u; i < result.size(); ++i)
                joined += (i > 0u ? ", " : "") + result[i];
            std::string rendered = "(" + joined + ")";
            if (!aMaxWidth.is_none() && py{ rendered }.size() > aMaxWidth.as_index())
            {
                std::string lines;
                for (std::size_t i = 0u; i < result.size(); ++i)
                    lines += (i > 0u ? ",\n    " : "") + result[i];
                rendered = "(\n    " + lines + "\n)";
            }
            py const returnAnnotation = aSelf.attr("return_annotation");
            if (!is_empty(returnAnnotation))
                rendered += " -> " + formatannotation(returnAnnotation, none()).str();
            return rendered;
        }

        py hash_basis(py const& aSelf)
        {
            std::vector<py> params;
            py const kwoParams = py_dict();
            for (auto const& p : parameters_of(aSelf))
            {
                if (kind_of_parameter(p) != KeywordOnly)
                    params.push_back(p);
                else
                    kwoParams.set_item(p.attr("name"), p);
            }
            return py_tuple({ py_tuple(params), kwoParams, aSelf.attr("return_annotation") });
        }

        py signature_bind(py const& aSelf, std::vector<py> const& aArgs, py const& aKwargs, bool aPartial)
        {
            py const arguments = py_dict();
            auto const parameters = parameters_of(aSelf);
            std::size_t next = 0u;      // (the next parameter)
            std::vector<py> parametersEx;
            std::size_t argument = 0u;
            std::vector<py> positionalOnlyInKwargs;
            for (;;)
            {
                if (argument == aArgs.size())
                {
                    if (next == parameters.size())
                        break;
                    py const param = parameters[next++];
                    int const k = kind_of_parameter(param);
                    if (k == VarPositional)
                        break;
                    else if (aKwargs.contains(param.attr("name")))
                    {
                        if (k == PositionalOnly)
                        {
                            if (is_empty(param.attr("default")))
                                py_raise("TypeError", "missing a required positional-only argument: " + param.attr("name").repr());
                            positionalOnlyInKwargs.push_back(param);
                            continue;
                        }
                        parametersEx.push_back(param);
                        break;
                    }
                    else if (k == VarKeyword || !is_empty(param.attr("default")))
                    {
                        parametersEx.push_back(param);
                        break;
                    }
                    else if (aPartial)
                    {
                        parametersEx.push_back(param);
                        break;
                    }
                    else
                    {
                        std::string const argtype = k == KeywordOnly ? " keyword-only" : "";
                        raise_from(builtin("TypeError")(py{ "missing a required" + argtype + " argument: " + param.attr("name").repr() }), none());
                    }
                }
                else
                {
                    py const value = aArgs[argument++];
                    if (next == parameters.size())
                        raise_from(builtin("TypeError")(py{ "too many positional arguments" }), none());
                    py const param = parameters[next++];
                    int const k = kind_of_parameter(param);
                    if (k == VarKeyword || k == KeywordOnly)
                        raise_from(builtin("TypeError")(py{ "too many positional arguments" }), none());
                    if (k == VarPositional)
                    {
                        std::vector<py> values{ value };
                        values.insert(values.end(), aArgs.begin() + static_cast<std::ptrdiff_t>(argument), aArgs.end());
                        arguments.set_item(param.attr("name"), py_tuple(values));
                        break;
                    }
                    if (aKwargs.contains(param.attr("name")) && k != PositionalOnly)
                        raise_from(builtin("TypeError")(py{ "multiple values for argument " + param.attr("name").repr() }), none());
                    arguments.set_item(param.attr("name"), value);
                }
            }
            py kwargsParam = none();
            std::vector<py> rest = parametersEx;
            rest.insert(rest.end(), parameters.begin() + static_cast<std::ptrdiff_t>(next), parameters.end());
            for (auto const& param : rest)
            {
                int const k = kind_of_parameter(param);
                if (k == VarKeyword)
                {
                    kwargsParam = param;
                    continue;
                }
                if (k == VarPositional)
                    continue;
                py const name = param.attr("name");
                if (aKwargs.contains(name))
                    arguments.set_item(name, aKwargs.method("pop", { name }));
                else if (!aPartial && k != VarPositional && is_empty(param.attr("default")))
                    raise_from(builtin("TypeError")(py{ "missing a required argument: " + name.repr() }), none());
            }
            if (aKwargs.truthy())
            {
                if (!kwargsParam.is_none())
                    arguments.set_item(kwargsParam.attr("name"), aKwargs);
                else if (!positionalOnlyInKwargs.empty())
                {
                    std::string names;
                    for (auto const& p : positionalOnlyInKwargs)
                        names += (names.empty() ? "" : ", ") + p.attr("name").str();
                    py_raise("TypeError", "got some positional-only arguments passed as keyword arguments: " + py{ names }.repr());
                }
                else
                    py_raise("TypeError", "got an unexpected keyword argument " + builtin("next")(builtin("iter")(aKwargs)).repr());
            }
            return aSelf.attr("_bound_arguments_cls")(aSelf, arguments);
        }

        // --- signatures of callables

        struct signature_options
        {
            bool followWrapperChains = true;
            bool skipBoundArg = true;
            py globals;
            py locals;
            bool evalStr = false;
            py sigcls;
        };

        py signature_from_callable(py aObject, signature_options const& aOptions);

        py signature_bound_method(py const& aSignature)
        {
            auto params = parameters_of(aSignature);
            if (params.empty() || kind_of_parameter(params[0]) == VarKeyword || kind_of_parameter(params[0]) == KeywordOnly)
                py_raise("ValueError", "invalid method signature");
            int const k = kind_of_parameter(params[0]);
            if (k == PositionalOrKeyword || k == PositionalOnly)
                params.erase(params.begin());
            else if (k != VarPositional)
                py_raise("ValueError", "invalid argument type");
            return signature_replace(aSignature, py_tuple(params), state().void_);
        }

        bool is_non_user_defined_callable(py const& aObject)
        {
            for (auto t : { "WrapperDescriptorType", "MethodWrapperType", "ClassMethodDescriptorType", "BuiltinFunctionType" })
                if (is_types_instance(aObject, t))
                    return true;
            return false;
        }

        bool signature_is_builtin(py const& aObject)
        {
            return isbuiltin(aObject) || ismethoddescriptor(aObject) || is_non_user_defined_callable(aObject) || aObject.is(builtin("type")) ||
                aObject.is(builtin("object"));
        }

        bool signature_is_functionlike(py const& aObject)
        {
            if (!is_callable_object(aObject) || isclass(aObject))
                return false;
            py const name = attr_or(aObject, "__name__", none());
            py const code = attr_or(aObject, "__code__", none());
            py const defaults = attr_or(aObject, "__defaults__", state().void_);
            py const kwdefaults = attr_or(aObject, "__kwdefaults__", state().void_);
            py const annotations = attr_or(aObject, "__annotations__", none());
            return iscode(code) && is_str(name) && (defaults.is_none() || isinstance(defaults, builtin("tuple"))) &&
                (kwdefaults.is_none() || isinstance(kwdefaults, builtin("dict"))) && (isinstance(annotations, builtin("dict")) || annotations.is_none());
        }

        // (_signature_strip_non_python_syntax and _signature_fromstr: the Signature of a text signature (Argument Clinic's: e.g. "($self, /, x, y=None)"))
        py signature_fromstr(py const& aClass, py const& aObject, std::string const& aText, bool aSkipBoundArg = true)
        {
            auto const invalid = [&]() { py_raise("ValueError", aObject.repr() + " builtin has invalid signature"); };
            std::size_t open = aText.find('(');
            if (open == std::string::npos)
                invalid();
            // (the parameters' texts (at top level commas))
            std::vector<std::string> items;
            std::string current;
            int depth = 0;
            char quote = 0;
            std::size_t i = open + 1u;
            bool closed = false;
            for (; i < aText.size(); ++i)
            {
                char const c = aText[i];
                if (quote != 0)
                {
                    current += c;
                    if (c == '\\' && i + 1u < aText.size())
                        current += aText[++i];
                    else if (c == quote)
                        quote = 0;
                    continue;
                }
                if (c == '\'' || c == '"')
                    quote = c;
                else if (c == '(' || c == '[' || c == '{')
                    ++depth;
                else if (c == ')' || c == ']' || c == '}')
                {
                    if (depth == 0)
                    {
                        closed = true;
                        break;
                    }
                    --depth;
                }
                else if (c == ',' && depth == 0)
                {
                    items.push_back(current);
                    current.clear();
                    continue;
                }
                else if (c == '\n')
                    continue;
                current += c;
            }
            if (!closed)
                invalid();
            items.push_back(current);
            auto const trim = [](std::string s)
            {
                auto const b = s.find_first_not_of(" \t");
                if (b == std::string::npos)
                    return std::string{};
                return s.substr(b, s.find_last_not_of(" \t") - b + 1u);
            };
            if (items.size() == 1u && trim(items[0]).empty())
                items.clear();
            py moduleDict = py_dict();
            py moduleName = attr_or(aObject, "__module__", none());
            if (!moduleName.truthy())
                moduleName = attr_or(attr_or(aObject, "__objclass__", none()), "__module__", none());
            if (moduleName.truthy())
            {
                py const module = sys_modules().method("get", { moduleName, none() });
                if (module.truthy())
                    moduleDict = dict_of(module);
            }
            py const sysModuleDict = sys_modules().method("copy");
            auto const value_of = [&](std::string const& aDefault) -> py
            {
                py const text{ aDefault };
                try
                {
                    return import_attribute("ast", "literal_eval")(text);
                }
                catch (raised const&)
                {
                    if (!caught({ "ValueError", "SyntaxError", "ImportError", "ModuleNotFoundError", "TypeError" }))
                        throw;
                }
                try
                {
                    try
                    {
                        return builtin("eval")(text, moduleDict);
                    }
                    catch (raised const&)
                    {
                        if (!caught({ "NameError" }))
                            throw;
                        return builtin("eval")(text, sysModuleDict);
                    }
                }
                catch (raised const&)
                {
                    if (!caught({ "Exception" }))
                        throw;
                    raise_from(builtin("ValueError")(py{ aObject.repr() + " builtin has invalid signature" }), none());
                }
            };
            std::vector<py> parameters;
            std::optional<std::size_t> selfParameter;
            int k = PositionalOrKeyword;
            std::size_t positionalOnlyBefore = 0u;
            bool slash = false;
            for (std::size_t index = 0u; index < items.size(); ++index)
            {
                std::string item = trim(items[index]);
                if (item.starts_with("$"))
                {
                    selfParameter = index;
                    item = trim(item.substr(1u));
                }
                if (item == "/")
                {
                    slash = true;
                    positionalOnlyBefore = parameters.size();
                    continue;
                }
                if (item == "*")
                {
                    k = KeywordOnly;
                    continue;
                }
                int itemKind = k;
                if (item.starts_with("**"))
                {
                    itemKind = VarKeyword;
                    item = item.substr(2u);
                }
                else if (item.starts_with("*"))
                {
                    itemKind = VarPositional;
                    item = item.substr(1u);
                    k = KeywordOnly;
                }
                std::string name = item;
                py defaultValue = state().empty;
                auto const equals = item.find('=');
                if (equals != std::string::npos)
                {
                    name = trim(item.substr(0u, equals));
                    defaultValue = value_of(trim(item.substr(equals + 1u)));
                }
                if (name.find(':') != std::string::npos)
                    py_raise("ValueError", "Annotations are not currently supported");
                parameters.push_back(make_parameter(aClass.attr("_parameter_cls"), py{ name }, kind(itemKind), defaultValue, state().empty));
            }
            if (slash)
                for (std::size_t p = 0u; p < positionalOnlyBefore; ++p)
                    parameters[p] = parameters[p].method("replace", {}, { { "kind", kind(PositionalOnly) } });
            if (selfParameter)
            {
                if (parameters.empty())
                    invalid();
                py const self = attr_or(aObject, "__self__", none());
                bool const selfIsBound = !self.is_none();
                if (selfIsBound && (ismodule(self) || aSkipBoundArg))
                    parameters.erase(parameters.begin());
                else
                    parameters[0] = parameters[0].method("replace", {}, { { "kind", kind(PositionalOnly) } });
            }
            return aClass.call({ py_list(parameters) }, { { "return_annotation", state().empty } });
        }

        py signature_from_builtin(py const& aClass, py const& aFunction, bool aSkipBoundArg)
        {
            if (!signature_is_builtin(aFunction))
                py_raise("TypeError", aFunction.repr() + " is not a Python builtin function");
            py text = attr_or(aFunction, "__text_signature__", none());
            if (!text.truthy() && aFunction.is(builtin("object")))
                text = py{ "()" };
            if (!text.truthy())
                py_raise("ValueError", "no signature found for builtin " + aFunction.repr());
            return signature_fromstr(aClass, aFunction, text.str(), aSkipBoundArg);
        }

        py signature_from_function(py const& aClass, py const& aFunction, signature_options const& aOptions)
        {
            bool isDuckFunction = false;
            if (!isfunction(aFunction))
            {
                if (signature_is_functionlike(aFunction))
                    isDuckFunction = true;
                else
                    py_raise("TypeError", aFunction.repr() + " is not a Python function");
            }
            py const text = attr_or(aFunction, "__text_signature__", none());
            if (text.truthy())
                return signature_fromstr(aClass, aFunction, text.str(), aOptions.skipBoundArg);
            py const parameterClass = aClass.attr("_parameter_cls");
            py const code = aFunction.attr("__code__");
            auto const posCount = code.attr("co_argcount").as_index();
            auto const argNames = code.attr("co_varnames").items();
            auto const posonlyCount = code.attr("co_posonlyargcount").as_index();
            auto const keywordOnlyCount = code.attr("co_kwonlyargcount").as_index();
            py const annotations = get_annotations(aFunction, aOptions.globals.empty() ? none() : aOptions.globals, aOptions.locals.empty() ? none() : aOptions.locals,
                aOptions.evalStr);
            py const defaults = aFunction.attr("__defaults__");
            py const kwdefaults = aFunction.attr("__kwdefaults__");
            long long const posDefaultCount = defaults.truthy() ? defaults.size() : 0;
            auto const flags = code_flags(code);
            auto const name_at = [&](long long i) { return static_cast<std::size_t>(i) < argNames.size() ? argNames[static_cast<std::size_t>(i)] : py{ "" }; };
            auto const annotation = [&](py const& aName) { return annotations.method("get", { aName, state().empty }); };
            std::vector<py> parameters;
            auto const nonDefaultCount = posCount - posDefaultCount;
            auto posonlyLeft = posonlyCount;
            for (long long i = 0; i < posCount; ++i)
            {
                py const name = name_at(i);
                int const k = posonlyLeft > 0 ? PositionalOnly : PositionalOrKeyword;
                py const defaultValue = i < nonDefaultCount ? state().empty : defaults[py{ i - nonDefaultCount }];
                parameters.push_back(make_parameter(parameterClass, name, kind(k), defaultValue, annotation(name)));
                if (posonlyLeft > 0)
                    --posonlyLeft;
            }
            if ((flags & CO_VARARGS) != 0)
            {
                py const name = name_at(posCount + keywordOnlyCount);
                parameters.push_back(make_parameter(parameterClass, name, kind(VarPositional), state().empty, annotation(name)));
            }
            for (long long i = 0; i < keywordOnlyCount; ++i)
            {
                py const name = name_at(posCount + i);
                py defaultValue = state().empty;
                if (!kwdefaults.is_none())
                    defaultValue = kwdefaults.method("get", { name, state().empty });
                parameters.push_back(make_parameter(parameterClass, name, kind(KeywordOnly), defaultValue, annotation(name)));
            }
            if ((flags & CO_VARKEYWORDS) != 0)
            {
                auto index = posCount + keywordOnlyCount;
                if ((flags & CO_VARARGS) != 0)
                    ++index;
                py const name = name_at(index);
                parameters.push_back(make_parameter(parameterClass, name, kind(VarKeyword), state().empty, annotation(name)));
            }
            return make_signature(aClass, parameters, annotations.method("get", { py{ "return" }, state().empty }), isDuckFunction);
        }

        // (_descriptor_get: a descriptor's __get__(obj, type(obj)); a function's: it bound to obj (as function.__get__))
        py descriptor_get(py const& aDescriptor, py const& aObject)
        {
            if (isclass(aDescriptor))
                return aDescriptor;
            if (isfunction(aDescriptor))
                return types_attr("MethodType")(aDescriptor, aObject);
            py const get = attr_or(type_of(aDescriptor), "__get__", state().sentinel);
            if (get.is(state().sentinel))
                return aDescriptor;
            return get(aDescriptor, aObject, type_of(aObject));
        }

        py signature_get_user_defined_method(py const& aClass, char const* aName, bool aFollowWrapperChains)
        {
            py method = std::strcmp(aName, "__new__") == 0 ? builtin("getattr")(aClass, py{ aName }, none()) : getattr_static(aClass, py{ aName }, none());
            if (method.is_none())
                return none();
            py unwrapped = none();
            if (aFollowWrapperChains)
                unwrapped = unwrap(method, [](py const& m) { return has_attr(m, "__signature__") || signature_is_builtin(m); });
            if (is_non_user_defined_callable(method) || is_non_user_defined_callable(unwrapped))
                return none();
            if (std::strcmp(aName, "__new__") != 0)
                method = descriptor_get(method, aClass);
            return method;
        }

        py signature_get_partial(py const& aWrappedSignature, py const& aPartial, std::vector<py> const& aExtraArgs = {})
        {
            py const oldParams = signature_parameters(aWrappedSignature);
            py const newParams = import_attribute("collections", "OrderedDict")(oldParams.method("items"));
            py partialArgs = aPartial.attr("args");
            if (!partialArgs.truthy())
                partialArgs = py_tuple();
            py partialKeywords = aPartial.attr("keywords");
            if (!partialKeywords.truthy())
                partialKeywords = py_dict();
            std::vector<py> args = aExtraArgs;
            for (auto const& a : partialArgs.items())
                args.push_back(a);
            py bound;
            try
            {
                bound = signature_bind(aWrappedSignature, args, builtin("dict")(partialKeywords), true);
            }
            catch (raised const&)
            {
                if (!caught({ "TypeError" }))
                    throw;
                py const cause = py_take_exception();
                raise_from(builtin("ValueError")(py{ "partial object " + aPartial.repr() + " has incorrect arguments" }), cause);
            }
            bool transformToKwonly = false;
            for (auto const& item : oldParams.method("items").items())
            {
                py const paramName = item[py{ 0 }];
                py const param = item[py{ 1 }];
                int const k = kind_of_parameter(param);
                py const arguments = bound.attr("arguments");
                if (arguments.contains(paramName))
                {
                    py const argValue = arguments[paramName];
                    if (k == PositionalOnly)
                    {
                        newParams.method("pop", { paramName });
                        continue;
                    }
                    if (k == PositionalOrKeyword)
                    {
                        if (partialKeywords.contains(paramName))
                        {
                            transformToKwonly = true;
                            newParams.set_item(paramName, param.method("replace", {}, { { "default", argValue } }));
                        }
                        else
                        {
                            newParams.method("pop", { param.attr("name") });
                            continue;
                        }
                    }
                    if (k == KeywordOnly)
                        newParams.set_item(paramName, param.method("replace", {}, { { "default", argValue } }));
                }
                if (transformToKwonly)
                {
                    if (k == PositionalOrKeyword)
                    {
                        py const newParam = newParams[paramName].method("replace", {}, { { "kind", kind(KeywordOnly) } });
                        newParams.set_item(paramName, newParam);
                        newParams.method("move_to_end", { paramName });
                    }
                    else if (k == KeywordOnly || k == VarKeyword)
                        newParams.method("move_to_end", { paramName });
                    else if (k == VarPositional)
                        newParams.method("pop", { param.attr("name") });
                }
            }
            return signature_replace(aWrappedSignature, newParams.method("values"), state().void_);
        }

        py signature_from_callable(py aObject, signature_options const& aOptions)
        {
            auto& s = state();
            auto const of = [&](py const& o) { return signature_from_callable(o, aOptions); };
            if (!is_callable_object(aObject))
                py_raise("TypeError", aObject.repr() + " is not a callable object");
            if (ismethod(aObject))
            {
                py const sig = of(aObject.attr("__func__"));
                if (aOptions.skipBoundArg)
                    return signature_bound_method(sig);
                return sig;
            }
            if (aOptions.followWrapperChains)
            {
                aObject = unwrap(aObject, [](py const& f) { return has_attr(f, "__signature__") || ismethod(f); });
                if (ismethod(aObject))
                    return of(aObject);
            }
            py sig;
            bool hasSignature = true;
            try
            {
                sig = aObject.attr("__signature__");
            }
            catch (raised const&)
            {
                if (!caught({ "AttributeError" }))
                    throw;
                hasSignature = false;
            }
            if (hasSignature && !sig.is_none())
            {
                py const original = sig;
                if (!isinstance(sig, cls("Signature")) && !is_str(sig) && is_callable_object(sig))
                    sig = sig();
                if (is_str(sig))
                    sig = signature_fromstr(aOptions.sigcls, aObject, sig.str());
                if (!isinstance(sig, cls("Signature")))
                    py_raise("TypeError", "unexpected object " + original.repr() + " in __signature__ attribute");
                return sig;
            }
            py const partialmethodClass = attr_or(py{ fn_import("functools") }, "partialmethod", py{});
            py const partialmethod = attr_or(aObject, "__partialmethod__", py{});
            if (!partialmethod.empty() && !partialmethodClass.empty() && isinstance(partialmethod, partialmethodClass))
            {
                py const wrappedSig = of(partialmethod.attr("func"));
                py const partialSig = signature_get_partial(wrappedSig, partialmethod, { none() });
                py const firstWrappedParam = parameters_of(wrappedSig)[0];
                if (kind_of_parameter(firstWrappedParam) == VarPositional)
                    return partialSig;
                std::vector<py> newParams{ firstWrappedParam };
                for (auto const& p : parameters_of(partialSig))
                    newParams.push_back(p);
                return signature_replace(partialSig, py_tuple(newParams), s.void_);
            }
            if (is_partial(aObject))
                return signature_get_partial(of(aObject.attr("func")), aObject);
            if ((isfunction(aObject) && has_attr(aObject, "__code__")) || signature_is_functionlike(aObject))
                return signature_from_function(aOptions.sigcls, aObject, aOptions);
            if (signature_is_builtin(aObject))
                return signature_from_builtin(aOptions.sigcls, aObject, aOptions.skipBoundArg);
            if (isclass(aObject))
            {
                py const call = signature_get_user_defined_method(type_of(aObject), "__call__", aOptions.followWrapperChains);
                if (!call.is_none())
                    return of(call);
                py const newMethod = signature_get_user_defined_method(aObject, "__new__", aOptions.followWrapperChains);
                py const initMethod = signature_get_user_defined_method(aObject, "__init__", aOptions.followWrapperChains);
                auto const mro = aObject.attr("__mro__").items();
                for (auto const& base : mro)
                {
                    if (!newMethod.is_none() && dict_of(base).contains(py{ "__new__" }))
                    {
                        py result = of(newMethod);
                        if (aOptions.skipBoundArg)
                            result = signature_bound_method(result);
                        return result;
                    }
                    else if (!initMethod.is_none() && dict_of(base).contains(py{ "__init__" }))
                        return of(initMethod);
                }
                for (std::size_t i = 0u; i + 1u < mro.size(); ++i)
                {
                    py const textSig = attr_or(mro[i], "__text_signature__", none());
                    if (textSig.truthy())
                        return signature_fromstr(aOptions.sigcls, mro[i], textSig.str());
                }
                bool isType = false;
                for (auto const& base : mro)
                    if (base.is(builtin("type")))
                        isType = true;
                if (!isType)
                {
                    // (obj.__init__ is object.__init__ and obj.__new__ is object.__new__: none of its classes but object defines them (a builtin 
                    // type other than object (e.g. dict) has its own))
                    bool own = false;
                    for (auto const& base : mro)
                        if (!base.is(builtin("object")) && (is_type_object(base.get()) || dict_of(base).contains(py{ "__init__" }) || dict_of(base).contains(py{ "__new__" })))
                            own = true;
                    if (!own)
                        return aOptions.sigcls.attr("from_callable")(builtin("object"));
                    py_raise("ValueError", "no signature found for builtin type " + aObject.repr());
                }
            }
            else
            {
                py call = getattr_static(type_of(aObject), py{ "__call__" }, none());
                if (!call.is_none())
                {
                    py const textSig = attr_or(aObject, "__text_signature__", none());
                    if (textSig.truthy())
                        return signature_fromstr(aOptions.sigcls, aObject, textSig.str());
                    call = descriptor_get(call, aObject);
                    return of(call);
                }
            }
            py_raise("ValueError", "callable " + aObject.repr() + " is not supported by signature");
        }

        signature_options options_of(py const& aSigcls, bool aFollowWrapped, py const& aGlobals, py const& aLocals, bool aEvalStr)
        {
            signature_options o;
            o.followWrapperChains = aFollowWrapped;
            o.globals = aGlobals;
            o.locals = aLocals;
            o.evalStr = aEvalStr;
            o.sigcls = aSigcls;
            return o;
        }

        // --- argument specs

        py getargs(py const& aCode)
        {
            if (!iscode(aCode))
                py_raise("TypeError", aCode.repr() + " is not a code object");
            auto const names = aCode.attr("co_varnames").items();
            auto nargs = static_cast<std::size_t>(aCode.attr("co_argcount").as_index());
            auto const nkwargs = static_cast<std::size_t>(aCode.attr("co_kwonlyargcount").as_index());
            std::vector<py> args;
            for (std::size_t i = 0u; i < nargs + nkwargs && i < names.size(); ++i)
                args.push_back(names[i]);
            nargs += nkwargs;
            py varargs = none();
            auto const flags = code_flags(aCode);
            if ((flags & CO_VARARGS) != 0)
            {
                varargs = names.at(nargs);
                ++nargs;
            }
            py varkw = none();
            if ((flags & CO_VARKEYWORDS) != 0)
                varkw = names.at(nargs);
            return cls("Arguments")(py_list(args), varargs, varkw);
        }

        py getfullargspec(py const& aFunction)
        {
            py sig;
            try
            {
                signature_options o;
                o.followWrapperChains = false;
                o.skipBoundArg = false;
                o.sigcls = cls("Signature");
                sig = signature_from_callable(aFunction, o);
            }
            catch (raised const&)
            {
                if (!caught({ "Exception" }))
                    throw;
                py const cause = py_take_exception();
                raise_from(builtin("TypeError")(py{ "unsupported callable" }), cause);
            }
            std::vector<py> args;
            std::vector<py> posonlyargs;
            std::vector<py> kwonlyargs;
            std::vector<py> defaults;
            py varargs = none();
            py varkw = none();
            py const annotations = py_dict();
            py const kwdefaults = py_dict();
            if (!is_empty(sig.attr("return_annotation")))
                annotations.set_item(py{ "return" }, sig.attr("return_annotation"));
            for (auto const& param : parameters_of(sig))
            {
                int const k = kind_of_parameter(param);
                py const name = param.attr("name");
                if (k == PositionalOnly)
                {
                    posonlyargs.push_back(name);
                    if (!is_empty(param.attr("default")))
                        defaults.push_back(param.attr("default"));
                }
                else if (k == PositionalOrKeyword)
                {
                    args.push_back(name);
                    if (!is_empty(param.attr("default")))
                        defaults.push_back(param.attr("default"));
                }
                else if (k == VarPositional)
                    varargs = name;
                else if (k == KeywordOnly)
                {
                    kwonlyargs.push_back(name);
                    if (!is_empty(param.attr("default")))
                        kwdefaults.set_item(name, param.attr("default"));
                }
                else if (k == VarKeyword)
                    varkw = name;
                if (!is_empty(param.attr("annotation")))
                    annotations.set_item(name, param.attr("annotation"));
            }
            posonlyargs.insert(posonlyargs.end(), args.begin(), args.end());
            return cls("FullArgSpec")(py_list(posonlyargs), varargs, varkw, defaults.empty() ? none() : py_tuple(defaults), py_list(kwonlyargs),
                kwdefaults.truthy() ? kwdefaults : none(), annotations);
        }

        [[noreturn]] void missing_arguments(std::string const& aName, std::vector<py> const& aArgnames, bool aPositional, py const& aValues)
        {
            std::vector<std::string> names;
            for (auto const& n : aArgnames)
                if (!aValues.contains(n))
                    names.push_back(n.repr());
            std::string text;
            if (names.size() == 1u)
                text = names[0];
            else if (names.size() == 2u)
                text = names[0] + " and " + names[1];
            else
            {
                for (std::size_t i = 0u; i + 2u < names.size(); ++i)
                    text += (i > 0u ? ", " : "") + names[i];
                text += ", " + names[names.size() - 2u] + " and " + names.back();
            }
            py_raise("TypeError", aName + "() missing " + std::to_string(names.size()) + " required " + (aPositional ? "positional" : "keyword-only") +
                " argument" + (names.size() == 1u ? "" : "s") + ": " + text);
        }

        [[noreturn]] void too_many(std::string const& aName, std::vector<py> const& aArgs, std::vector<py> const& aKwonly, bool aVarargs, long long aDefcount,
            long long aGiven, py const& aValues)
        {
            long long const atleast = static_cast<long long>(aArgs.size()) - aDefcount;
            long long kwonlyGiven = 0;
            for (auto const& a : aKwonly)
                if (aValues.contains(a))
                    ++kwonlyGiven;
            bool plural;
            std::string sig;
            if (aVarargs)
            {
                plural = atleast != 1;
                sig = "at least " + std::to_string(atleast);
            }
            else if (aDefcount != 0)
            {
                plural = true;
                sig = "from " + std::to_string(atleast) + " to " + std::to_string(aArgs.size());
            }
            else
            {
                plural = aArgs.size() != 1u;
                sig = std::to_string(aArgs.size());
            }
            std::string kwonlySig;
            if (kwonlyGiven != 0)
                kwonlySig = std::string{ " positional argument" } + (aGiven != 1 ? "s" : "") + " (and " + std::to_string(kwonlyGiven) + " keyword-only argument" +
                    (kwonlyGiven != 1 ? "s" : "") + ")";
            py_raise("TypeError", aName + "() takes " + sig + " positional argument" + (plural ? "s" : "") + " but " + std::to_string(aGiven) + kwonlySig + " " +
                (aGiven == 1 && kwonlyGiven == 0 ? "was" : "were") + " given");
        }

        py getcallargs(py const& aFunction, std::vector<py> aPositional, py const& aNamed)
        {
            py const spec = getfullargspec(aFunction);
            auto const args = spec[py{ 0 }].items();
            py const varargs = spec[py{ 1 }];
            py const varkw = spec[py{ 2 }];
            py const defaults = spec[py{ 3 }];
            auto const kwonlyargs = spec[py{ 4 }].items();
            py const kwonlydefaults = spec[py{ 5 }];
            std::string const name = aFunction.attr("__name__").str();
            py const arg2value = py_dict();
            if (ismethod(aFunction) && !aFunction.attr("__self__").is_none())
                aPositional.insert(aPositional.begin(), aFunction.attr("__self__"));
            auto const numPos = aPositional.size();
            auto const numArgs = args.size();
            auto const numDefaults = defaults.truthy() ? static_cast<std::size_t>(defaults.size()) : 0u;
            auto const n = std::min(numPos, numArgs);
            for (std::size_t i = 0u; i < n; ++i)
                arg2value.set_item(args[i], aPositional[i]);
            if (varargs.truthy())
                arg2value.set_item(varargs, py_tuple(std::vector<py>{ aPositional.begin() + static_cast<std::ptrdiff_t>(n), aPositional.end() }));
            py const possibleKwargs = builtin("set")(py_list([&]() { auto v = args; v.insert(v.end(), kwonlyargs.begin(), kwonlyargs.end()); return v; }()));
            if (varkw.truthy())
                arg2value.set_item(varkw, py_dict());
            for (auto const& item : aNamed.method("items").items())
            {
                py const kw = item[py{ 0 }];
                py const value = item[py{ 1 }];
                if (!possibleKwargs.contains(kw))
                {
                    if (!varkw.truthy())
                        py_raise("TypeError", name + "() got an unexpected keyword argument " + kw.repr());
                    arg2value[varkw].set_item(kw, value);
                    continue;
                }
                if (arg2value.contains(kw))
                    py_raise("TypeError", name + "() got multiple values for argument " + kw.repr());
                arg2value.set_item(kw, value);
            }
            if (numPos > numArgs && !varargs.truthy())
                too_many(name, args, kwonlyargs, false, static_cast<long long>(numDefaults), static_cast<long long>(numPos), arg2value);
            if (numPos < numArgs)
            {
                std::vector<py> const required{ args.begin(), args.begin() + static_cast<std::ptrdiff_t>(numArgs - numDefaults) };
                for (auto const& a : required)
                    if (!arg2value.contains(a))
                        missing_arguments(name, required, true, arg2value);
                for (std::size_t i = numArgs - numDefaults; i < numArgs; ++i)
                    if (!arg2value.contains(args[i]))
                        arg2value.set_item(args[i], defaults[py{ i - (numArgs - numDefaults) }]);
            }
            std::size_t missing = 0u;
            for (auto const& kwarg : kwonlyargs)
                if (!arg2value.contains(kwarg))
                {
                    if (kwonlydefaults.truthy() && kwonlydefaults.contains(kwarg))
                        arg2value.set_item(kwarg, kwonlydefaults[kwarg]);
                    else
                        ++missing;
                }
            if (missing != 0u)
                missing_arguments(name, kwonlyargs, false, arg2value);
            return arg2value;
        }

        py getclosurevars(py aFunction)
        {
            if (ismethod(aFunction))
                aFunction = aFunction.attr("__func__");
            if (!isfunction(aFunction))
                py_raise("TypeError", aFunction.repr() + " is not a Python function");
            py const code = aFunction.attr("__code__");
            py const nonlocalVars = py_dict();
            py const closure = aFunction.attr("__closure__");
            if (!closure.is_none())
            {
                auto const freevars = code.attr("co_freevars").items();
                auto const cells = closure.items();
                for (std::size_t i = 0u; i < freevars.size() && i < cells.size(); ++i)
                    nonlocalVars.set_item(freevars[i], cells[i].attr("cell_contents"));
            }
            py const globalNs = aFunction.attr("__globals__");
            py builtinNs = globalNs.method("get", { py{ "__builtins__" }, import_attribute("builtins", "__dict__") });
            if (ismodule(builtinNs))
                builtinNs = dict_of(builtinNs);
            py const globalVars = py_dict();
            py const builtinVars = py_dict();
            py const unboundNames = builtin("set")();
            // (the names its code loads as globals (its code's names: there are no instructions to read))
            for (auto const& name : code.attr("co_names").items())
            {
                if (globalNs.contains(name))
                    globalVars.set_item(name, globalNs[name]);
                else if (builtinNs.contains(name))
                    builtinVars.set_item(name, builtinNs[name]);
                else
                    unboundNames.method("add", { name });
            }
            return cls("ClosureVars")(nonlocalVars, globalVars, builtinVars, unboundNames);
        }

        // --- frames

        py positions_of(std::vector<py> const& aValues)
        {
            return state().positions.call(aValues);
        }

        py getframeinfo(py aFrame, long long aContext)
        {
            py lineno;
            if (istraceback(aFrame))
            {
                lineno = aFrame.attr("tb_lineno");
                aFrame = aFrame.attr("tb_frame");
            }
            else
                lineno = aFrame.attr("f_lineno");
            // (the instruction's positions are not known: (lineno, None, None, None))
            std::vector<py> positions{ lineno, none(), none(), none() };
            if (!isframe(aFrame))
                py_raise("TypeError", aFrame.repr() + " is not a frame or traceback object");
            py filename = getsourcefile(aFrame);
            if (!filename.truthy())
                filename = getfile(aFrame);
            py lines = none();
            py index = none();
            if (aContext > 0)
            {
                long long start = lineno.as_index() - 1 - (aContext >= 0 ? aContext / 2 : -((-aContext + 1) / 2));
                try
                {
                    py const found = findsource(aFrame);
                    py const all = found[py{ 0 }];
                    start = std::max(0ll, std::min(start, static_cast<long long>(all.size()) - aContext));
                    lines = all[py{ make_slice(py{ start }.get(), py{ start + aContext }.get(), nullptr) }];
                    index = py{ lineno.as_index() - 1 - start };
                }
                catch (raised const&)
                {
                    if (!caught({ "OSError" }))
                        throw;
                    lines = none();
                    index = none();
                }
            }
            return cls("Traceback").call({ filename, lineno, aFrame.attr("f_code").attr("co_name"), lines, index }, { { "positions", positions_of(positions) } });
        }

        py getouterframes(py aFrame, long long aContext)
        {
            py const framelist = py_list();
            while (aFrame.truthy())
            {
                py const info = getframeinfo(aFrame, aContext);
                std::vector<py> values{ aFrame };
                for (auto const& v : info.items())
                    values.push_back(v);
                framelist.method("append", { cls("FrameInfo").call(values, { { "positions", info.attr("positions") } }) });
                aFrame = aFrame.attr("f_back");
            }
            return framelist;
        }

        py getinnerframes(py aTraceback, long long aContext)
        {
            py const framelist = py_list();
            while (aTraceback.truthy())
            {
                py const info = getframeinfo(aTraceback, aContext);
                std::vector<py> values{ aTraceback.attr("tb_frame") };
                for (auto const& v : info.items())
                    values.push_back(v);
                framelist.method("append", { cls("FrameInfo").call(values, { { "positions", info.attr("positions") } }) });
                aTraceback = aTraceback.attr("tb_next");
            }
            return framelist;
        }

        // (the frame of the caller of the module's function (sys._getframe(1) in CPython's: here the innermost Python frame))
        py caller_frame()
        {
            return import_attribute("sys", "_getframe")(py{ 0 });
        }

        // --- the module's classes

        py make_inspect_class(char const* aName, std::vector<py> const& aBases, std::vector<std::pair<char const*, py>> const& aNamespace, char const* aDoc = nullptr)
        {
            py const ns = py_dict();
            ns.set_item(py{ "__module__" }, py{ sModule });
            ns.set_item(py{ "__qualname__" }, py{ aName });
            if (aDoc != nullptr)
                ns.set_item(py{ "__doc__" }, py{ aDoc });
            for (auto const& [name, value] : aNamespace)
                ns.set_item(py{ name }, value);
            std::vector<py> const bases = aBases.empty() ? std::vector<py>{ builtin("object") } : aBases;
            py meta = builtin("type");
            for (auto const& b : bases)
            {
                py const t = type_of(b);
                if (!t.is(meta) && issubclass(t, meta))
                    meta = t;
            }
            py const result = meta(py{ aName }, py_tuple(bases), ns);
            state().classes[aName] = result;
            return result;
        }

        // (a method: its parameters after self)
        py method(char const* aClass, char const* aName, std::vector<py_parameter> aParameters, std::function<py(std::vector<py> const&)> aBody, bool aVarArgs = false,
            bool aVarKeywords = false)
        {
            std::string const qualname = std::string{ aClass } + "." + aName;
            aParameters.insert(aParameters.begin(), py_positional("self"));
            return function(aName, qualname, [qualname, aParameters, aBody, aVarArgs, aVarKeywords](std::vector<py> const& aArgs, object* aKwargs) -> py
            {
                py rest;
                py extra;
                auto a = py_bind(qualname, aParameters, aArgs, aKwargs, aVarArgs ? &rest : nullptr, aVarKeywords ? &extra : nullptr);
                if (aVarArgs)
                    a.push_back(rest);
                if (aVarKeywords)
                    a.push_back(extra);
                return aBody(a);
            });
        }
        py property_of(char const* aClass, char const* aName, std::function<py(py const&)> aGetter)
        {
            return builtin("property")(method(aClass, aName, {}, [aGetter](std::vector<py> const& a) { return aGetter(a[0]); }));
        }

        py namedtuple(char const* aName, py const& aFields, char const* aModule = sModule)
        {
            return import_attribute("collections", "namedtuple").call({ py{ aName }, aFields }, { { "module", py{ aModule } } });
        }

        // (a namedtuple's subclass whose instances also have positions (Traceback and FrameInfo))
        py positions_class(char const* aName, py const& aBase, std::vector<char const*> const& aFields)
        {
            std::vector<py_parameter> parameters{ py_positional("cls") };
            for (auto f : aFields)
                parameters.push_back(py_positional(f));
            parameters.push_back(py_keyword("positions", none()));
            std::string const name = aName;
            std::string const qualname = name + ".__new__";
            std::vector<std::string> fields{ aFields.begin(), aFields.end() };
            py const newFunction = function("__new__", qualname, [qualname, parameters, aBase](std::vector<py> const& aArgs, object* aKwargs) -> py
            {
                auto const a = py_bind(qualname, parameters, aArgs, aKwargs);
                std::vector<py> values{ a.begin(), a.end() - 1 };
                py const instance = aBase.attr("__new__").call(values);
                instance.set_attr("positions", a.back());
                return instance;
            });
            py const repr = function("__repr__", name + ".__repr__", [name, fields](std::vector<py> const& aArgs, object* aKwargs) -> py
            {
                auto const a = py_bind(name + ".__repr__", { py_positional("self") }, aArgs, aKwargs);
                std::string text = name + "(";
                for (auto const& f : fields)
                    text += f + "=" + a[0].attr(f.c_str()).repr() + ", ";
                return py{ text + "positions=" + a[0].attr("positions").repr() + ")" };
            });
            return make_inspect_class(aName, { aBase }, { { "__new__", builtin("staticmethod")(newFunction) }, { "__repr__", repr } });
        }

        void make_classes()
        {
            auto& s = state();
            make_inspect_class("_void", {}, {}, "A private marker - used in Parameter & Signature.");
            make_inspect_class("_empty", {}, {}, "Marker object for Signature.empty and Parameter.empty.");
            s.void_ = cls("_void");
            s.empty = cls("_empty");
            s.sentinel = builtin("object")();
            s.coroutineMark = builtin("object")();
            s.modulesByFile = py_dict();
            s.filesByModname = py_dict();
            s.classes["ClassFoundException"] = py{ module_exception("inspect.ClassFoundException", { "Exception" }) };
            s.classes["EndOfBlock"] = py{ module_exception("inspect.EndOfBlock", { "Exception" }) };
            try
            {
                s.positions = import_attribute("dis", "Positions");
            }
            catch (raised const&)
            {
                if (!caught({ "ImportError", "AttributeError" }))
                    throw;
                s.positions = import_attribute("collections", "namedtuple").call({ py{ "Positions" },
                    py_list({ py{ "lineno" }, py{ "end_lineno" }, py{ "col_offset" }, py{ "end_col_offset" } }) },
                    { { "defaults", py_list({ none(), none(), none(), none() }) }, { "module", py{ "dis" } } });
            }

            // (_ParameterKind)
            static char const* const sKindNames[] = { "POSITIONAL_ONLY", "POSITIONAL_OR_KEYWORD", "VAR_POSITIONAL", "KEYWORD_ONLY", "VAR_KEYWORD" };
            static char const* const sKindDescriptions[] = { "positional-only", "positional or keyword", "variadic positional", "keyword-only", "variadic keyword" };
            std::vector<py> members;
            for (int i = 0; i < 5; ++i)
                members.push_back(py_tuple({ py{ sKindNames[i] }, py{ i } }));
            s.parameterKind = import_attribute("enum", "IntEnum").call({ py{ "_ParameterKind" }, py_list(members) }, { { "module", py{ sModule } } });
            for (int i = 0; i < 5; ++i)
            {
                s.kinds[i] = s.parameterKind.attr(sKindNames[i]);
                s.kinds[i].set_attr("description", py{ sKindDescriptions[i] });
            }
            s.parameterKind.set_attr("__str__", function("__str__", "_ParameterKind.__str__", [](std::vector<py> const& aArgs, object* aKwargs) -> py
            {
                auto const a = py_bind("_ParameterKind.__str__", { py_positional("self") }, aArgs, aKwargs);
                return a[0].attr("name");
            }));
            s.classes["_ParameterKind"] = s.parameterKind;

            // (the namedtuples)
            s.classes["Attribute"] = namedtuple("Attribute", py{ "name kind defining_class object" });
            s.classes["Arguments"] = namedtuple("Arguments", py{ "args, varargs, varkw" });
            s.classes["FullArgSpec"] = namedtuple("FullArgSpec", py{ "args, varargs, varkw, defaults, kwonlyargs, kwonlydefaults, annotations" });
            s.classes["ArgInfo"] = namedtuple("ArgInfo", py{ "args varargs keywords locals" });
            s.classes["ClosureVars"] = namedtuple("ClosureVars", py{ "nonlocals globals builtins unbound" });
            s.classes["_Traceback"] = namedtuple("_Traceback", py{ "filename lineno function code_context index" });
            positions_class("Traceback", cls("_Traceback"), { "filename", "lineno", "function", "code_context", "index" });
            std::vector<py> frameFields{ py{ "frame" } };
            for (auto const& f : cls("Traceback").attr("_fields").items())
                frameFields.push_back(f);
            s.classes["_FrameInfo"] = namedtuple("_FrameInfo", py_tuple(frameFields));
            positions_class("FrameInfo", cls("_FrameInfo"), { "frame", "filename", "lineno", "function", "code_context", "index" });

            // (Parameter)
            std::vector<std::pair<char const*, py>> parameterNs{
                { "__slots__", py_tuple({ py{ "_name" }, py{ "_kind" }, py{ "_default" }, py{ "_annotation" } }) },
                { "POSITIONAL_ONLY", s.kinds[PositionalOnly] }, { "POSITIONAL_OR_KEYWORD", s.kinds[PositionalOrKeyword] }, { "VAR_POSITIONAL", s.kinds[VarPositional] },
                { "KEYWORD_ONLY", s.kinds[KeywordOnly] }, { "VAR_KEYWORD", s.kinds[VarKeyword] }, { "empty", s.empty },
                { "__init__", method("Parameter", "__init__", { py_positional("name"), py_positional("kind"), py_keyword("default", s.empty),
                    py_keyword("annotation", s.empty) }, parameter_init) },
                { "__reduce__", method("Parameter", "__reduce__", {}, [](std::vector<py> const& a)
                    {
                        py const state = py_dict();
                        state.set_item(py{ "_default" }, a[0].attr("_default"));
                        state.set_item(py{ "_annotation" }, a[0].attr("_annotation"));
                        return py_tuple({ type_of(a[0]), py_tuple({ a[0].attr("_name"), a[0].attr("_kind") }), state });
                    }) },
                { "__setstate__", method("Parameter", "__setstate__", { py_positional("state") }, [](std::vector<py> const& a)
                    {
                        a[0].set_attr("_default", a[1][py{ "_default" }]);
                        a[0].set_attr("_annotation", a[1][py{ "_annotation" }]);
                        return none();
                    }) },
                { "name", property_of("Parameter", "name", [](py const& self) { return self.attr("_name"); }) },
                { "default", property_of("Parameter", "default", [](py const& self) { return self.attr("_default"); }) },
                { "annotation", property_of("Parameter", "annotation", [](py const& self) { return self.attr("_annotation"); }) },
                { "kind", property_of("Parameter", "kind", [](py const& self) { return self.attr("_kind"); }) },
                { "__str__", method("Parameter", "__str__", {}, [](std::vector<py> const& a) { return py{ parameter_str(a[0]) }; }) },
                { "__repr__", method("Parameter", "__repr__", {}, [](std::vector<py> const& a)
                    {
                        return py{ "<" + a[0].attr("__class__").attr("__name__").str() + " \"" + parameter_str(a[0]) + "\">" };
                    }) },
                { "__hash__", method("Parameter", "__hash__", {}, [](std::vector<py> const& a)
                    {
                        return builtin("hash")(py_tuple({ a[0].attr("_name"), a[0].attr("_kind"), a[0].attr("_annotation"), a[0].attr("_default") }));
                    }) },
                { "__eq__", method("Parameter", "__eq__", { py_positional("other") }, [](std::vector<py> const& a) -> py
                    {
                        if (a[0].is(a[1]))
                            return boolean(true);
                        if (!isinstance(a[1], cls("Parameter")))
                            return py::borrowed(not_implemented());
                        return boolean(a[0].attr("_name") == a[1].attr("_name") && a[0].attr("_kind") == a[1].attr("_kind") &&
                            a[0].attr("_default") == a[1].attr("_default") && a[0].attr("_annotation") == a[1].attr("_annotation"));
                    }) } };
            py const parameterReplace = method("Parameter", "replace", { py_keyword("name", s.void_), py_keyword("kind", s.void_), py_keyword("annotation", s.void_),
                py_keyword("default", s.void_) }, [](std::vector<py> const& a)
                {
                    auto const& v = state().void_;
                    py const name = a[1].is(v) ? a[0].attr("_name") : a[1];
                    py const k = a[2].is(v) ? a[0].attr("_kind") : a[2];
                    py const annotation = a[3].is(v) ? a[0].attr("_annotation") : a[3];
                    py const defaultValue = a[4].is(v) ? a[0].attr("_default") : a[4];
                    return make_parameter(type_of(a[0]), name, k, defaultValue, annotation);
                });
            parameterReplace.set_attr("__doc__", py{ "Creates a customized copy of the Parameter." });
            parameterNs.emplace_back("replace", parameterReplace);
            parameterNs.emplace_back("__replace__", parameterReplace);
            make_inspect_class("Parameter", {}, parameterNs,
                "Represents a parameter in a function signature.\n\n    Has the following public attributes:\n\n    * name : str\n"
                "        The name of the parameter as a string.\n    * default : object\n        The default value for the parameter if specified.  If the\n"
                "        parameter has no default value, this attribute is set to\n        `Parameter.empty`.\n    * annotation\n"
                "        The annotation for the parameter if specified.  If the\n        parameter has no annotation, this attribute is set to\n"
                "        `Parameter.empty`.\n    * kind\n        Describes how argument values are bound to the parameter.\n"
                "        Possible values: `Parameter.POSITIONAL_ONLY`,\n        `Parameter.POSITIONAL_OR_KEYWORD`, `Parameter.VAR_POSITIONAL`,\n"
                "        `Parameter.KEYWORD_ONLY`, `Parameter.VAR_KEYWORD`.\n\n        Every value has a `description` attribute describing meaning.\n    ");

            // (BoundArguments)
            make_inspect_class("BoundArguments", {}, {
                { "__slots__", py_tuple({ py{ "arguments" }, py{ "_signature" }, py{ "__weakref__" } }) },
                { "__init__", method("BoundArguments", "__init__", { py_positional("signature"), py_positional("arguments") }, [](std::vector<py> const& a)
                    {
                        a[0].set_attr("arguments", a[2]);
                        a[0].set_attr("_signature", a[1]);
                        return none();
                    }) },
                { "signature", property_of("BoundArguments", "signature", [](py const& self) { return self.attr("_signature"); }) },
                { "args", property_of("BoundArguments", "args", [](py const& self)
                    {
                        std::vector<py> args;
                        py const arguments = self.attr("arguments");
                        for (auto const& item : signature_parameters(self.attr("_signature")).method("items").items())
                        {
                            py const param = item[py{ 1 }];
                            int const k = kind_of_parameter(param);
                            if (k == VarKeyword || k == KeywordOnly)
                                break;
                            if (!arguments.contains(item[py{ 0 }]))
                                break;
                            py const arg = arguments[item[py{ 0 }]];
                            if (k == VarPositional)
                            {
                                for (auto const& v : arg.items())
                                    args.push_back(v);
                            }
                            else
                                args.push_back(arg);
                        }
                        return py_tuple(args);
                    }) },
                { "kwargs", property_of("BoundArguments", "kwargs", [](py const& self)
                    {
                        py const kwargs = py_dict();
                        bool started = false;
                        py const arguments = self.attr("arguments");
                        for (auto const& item : signature_parameters(self.attr("_signature")).method("items").items())
                        {
                            py const name = item[py{ 0 }];
                            py const param = item[py{ 1 }];
                            int const k = kind_of_parameter(param);
                            if (!started)
                            {
                                if (k == VarKeyword || k == KeywordOnly)
                                    started = true;
                                else if (!arguments.contains(name))
                                {
                                    started = true;
                                    continue;
                                }
                            }
                            if (!started)
                                continue;
                            if (arguments.contains(name))
                            {
                                if (k == VarKeyword)
                                    kwargs.method("update", { arguments[name] });
                                else
                                    kwargs.set_item(name, arguments[name]);
                            }
                        }
                        return kwargs;
                    }) },
                { "apply_defaults", method("BoundArguments", "apply_defaults", {}, [](std::vector<py> const& a)
                    {
                        py const arguments = a[0].attr("arguments");
                        std::vector<py> newArguments;
                        for (auto const& item : signature_parameters(a[0].attr("_signature")).method("items").items())
                        {
                            py const name = item[py{ 0 }];
                            py const param = item[py{ 1 }];
                            if (arguments.contains(name))
                            {
                                newArguments.push_back(py_tuple({ name, arguments[name] }));
                                continue;
                            }
                            py value;
                            int const k = kind_of_parameter(param);
                            if (!is_empty(param.attr("default")))
                                value = param.attr("default");
                            else if (k == VarPositional)
                                value = py_tuple();
                            else if (k == VarKeyword)
                                value = py_dict();
                            else
                                continue;
                            newArguments.push_back(py_tuple({ name, value }));
                        }
                        a[0].set_attr("arguments", builtin("dict")(py_list(newArguments)));
                        return none();
                    }) },
                { "__eq__", method("BoundArguments", "__eq__", { py_positional("other") }, [](std::vector<py> const& a) -> py
                    {
                        if (a[0].is(a[1]))
                            return boolean(true);
                        if (!isinstance(a[1], cls("BoundArguments")))
                            return py::borrowed(not_implemented());
                        return boolean(a[0].attr("signature") == a[1].attr("signature") && a[0].attr("arguments") == a[1].attr("arguments"));
                    }) },
                { "__setstate__", method("BoundArguments", "__setstate__", { py_positional("state") }, [](std::vector<py> const& a)
                    {
                        a[0].set_attr("_signature", a[1][py{ "_signature" }]);
                        a[0].set_attr("arguments", a[1][py{ "arguments" }]);
                        return none();
                    }) },
                { "__getstate__", method("BoundArguments", "__getstate__", {}, [](std::vector<py> const& a)
                    {
                        py const result = py_dict();
                        result.set_item(py{ "_signature" }, a[0].attr("_signature"));
                        result.set_item(py{ "arguments" }, a[0].attr("arguments"));
                        return result;
                    }) },
                { "__repr__", method("BoundArguments", "__repr__", {}, [](std::vector<py> const& a)
                    {
                        std::string args;
                        for (auto const& item : a[0].attr("arguments").method("items").items())
                            args += (args.empty() ? "" : ", ") + item[py{ 0 }].str() + "=" + item[py{ 1 }].repr();
                        return py{ "<" + a[0].attr("__class__").attr("__name__").str() + " (" + args + ")>" };
                    }) } },
                "Result of `Signature.bind` call.  Holds the mapping of arguments\n    to the function's parameters.\n\n    Has the following public attributes:\n\n"
                "    * arguments : dict\n        An ordered mutable mapping of parameters' names to arguments' values.\n        Does not contain arguments' default values.\n"
                "    * signature : Signature\n        The Signature object that created this instance.\n    * args : tuple\n        Tuple of positional arguments values.\n"
                "    * kwargs : dict\n        Dict of keyword arguments values.\n    ");

            // (Signature)
            py const signatureReplace = method("Signature", "replace", { py_keyword("parameters", s.void_), py_keyword("return_annotation", s.void_) },
                [](std::vector<py> const& a) { return signature_replace(a[0], a[1], a[2]); });
            signatureReplace.set_attr("__doc__", py{ "Creates a customized copy of the Signature.\n        Pass 'parameters' and/or 'return_annotation' arguments\n"
                "        to override them in the new copy.\n        " });
            make_inspect_class("Signature", {}, {
                { "__slots__", py_tuple({ py{ "_return_annotation" }, py{ "_parameters" } }) },
                { "_parameter_cls", cls("Parameter") }, { "_bound_arguments_cls", cls("BoundArguments") }, { "empty", s.empty },
                { "__init__", method("Signature", "__init__", { py_positional("parameters", none()), py_keyword("return_annotation", s.empty),
                    py_keyword("__validate_parameters__", boolean(true)) }, signature_init) },
                { "from_callable", builtin("classmethod")(function("from_callable", "Signature.from_callable", [](std::vector<py> const& aArgs, object* aKwargs) -> py
                    {
                        auto const a = py_bind("Signature.from_callable", { py_positional("cls"), py_positional("obj"), py_keyword("follow_wrapped", boolean(true)),
                            py_keyword("globals", none()), py_keyword("locals", none()), py_keyword("eval_str", boolean(false)) }, aArgs, aKwargs);
                        return signature_from_callable(a[1], options_of(a[0], a[2].truthy(), a[3], a[4], a[5].truthy()));
                    }, "Constructs Signature for the given callable object.")) },
                { "parameters", property_of("Signature", "parameters", [](py const& self) { return self.attr("_parameters"); }) },
                { "return_annotation", property_of("Signature", "return_annotation", [](py const& self) { return self.attr("_return_annotation"); }) },
                { "replace", signatureReplace }, { "__replace__", signatureReplace },
                { "_hash_basis", method("Signature", "_hash_basis", {}, [](std::vector<py> const& a) { return hash_basis(a[0]); }) },
                { "__hash__", method("Signature", "__hash__", {}, [](std::vector<py> const& a)
                    {
                        py const basis = hash_basis(a[0]);
                        return builtin("hash")(py_tuple({ basis[py{ 0 }], builtin("frozenset")(basis[py{ 1 }].method("values")), basis[py{ 2 }] }));
                    }) },
                { "__eq__", method("Signature", "__eq__", { py_positional("other") }, [](std::vector<py> const& a) -> py
                    {
                        if (a[0].is(a[1]))
                            return boolean(true);
                        if (!isinstance(a[1], cls("Signature")))
                            return py::borrowed(not_implemented());
                        return boolean(hash_basis(a[0]) == hash_basis(a[1]));
                    }) },
                { "_bind", method("Signature", "_bind", { py_positional("args"), py_positional("kwargs"), py_keyword("partial", boolean(false)) },
                    [](std::vector<py> const& a) { return signature_bind(a[0], a[1].items(), a[2], a[3].truthy()); }) },
                { "bind", function("bind", "Signature.bind", [](std::vector<py> const& aArgs, object* aKwargs) -> py
                    {
                        py rest;
                        py extra;
                        auto const a = py_bind("Signature.bind", { py_positional_only("self") }, aArgs, aKwargs, &rest, &extra);
                        return signature_bind(a[0], rest.items(), extra, false);
                    }, "Get a BoundArguments object, that maps the passed `args`\n        and `kwargs` to the function's signature.  Raises `TypeError`\n"
                    "        if the passed arguments can not be bound.\n        ") },
                { "bind_partial", function("bind_partial", "Signature.bind_partial", [](std::vector<py> const& aArgs, object* aKwargs) -> py
                    {
                        py rest;
                        py extra;
                        auto const a = py_bind("Signature.bind_partial", { py_positional_only("self") }, aArgs, aKwargs, &rest, &extra);
                        return signature_bind(a[0], rest.items(), extra, true);
                    }, "Get a BoundArguments object, that partially maps the\n        passed `args` and `kwargs` to the function's signature.\n"
                    "        Raises `TypeError` if the passed arguments can not be bound.\n        ") },
                { "__reduce__", method("Signature", "__reduce__", {}, [](std::vector<py> const& a)
                    {
                        py const state = py_dict();
                        state.set_item(py{ "_return_annotation" }, a[0].attr("_return_annotation"));
                        return py_tuple({ type_of(a[0]), py_tuple({ py_tuple(parameters_of(a[0])) }), state });
                    }) },
                { "__setstate__", method("Signature", "__setstate__", { py_positional("state") }, [](std::vector<py> const& a)
                    {
                        a[0].set_attr("_return_annotation", a[1][py{ "_return_annotation" }]);
                        return none();
                    }) },
                { "__repr__", method("Signature", "__repr__", {}, [](std::vector<py> const& a)
                    {
                        return py{ "<" + a[0].attr("__class__").attr("__name__").str() + " " + builtin("str")(a[0]).str() + ">" };
                    }) },
                { "__str__", method("Signature", "__str__", {}, [](std::vector<py> const& a) { return a[0].method("format"); }) },
                { "format", method("Signature", "format", { py_keyword("max_width", none()) }, [](std::vector<py> const& a)
                    {
                        return py{ signature_format(a[0], a[1]) };
                    }) } },
                "A Signature object represents the overall signature of a function.\n    It stores a Parameter object for each parameter accepted by the\n"
                "    function, as well as information specific to the function itself.\n\n    A Signature object has the following public attributes and methods:\n\n"
                "    * parameters : OrderedDict\n        An ordered mapping of parameters' names to the corresponding\n"
                "        Parameter objects (keyword-only arguments are in the same order\n        as listed in `code.co_varnames`).\n"
                "    * return_annotation : object\n        The annotation for the return type of the function if specified.\n"
                "        If the function has no annotation for its return type, this\n        attribute is set to `Signature.empty`.\n"
                "    * bind(*args, **kwargs) -> BoundArguments\n        Creates a mapping from positional and keyword arguments to\n        parameters.\n"
                "    * bind_partial(*args, **kwargs) -> BoundArguments\n        Creates a partial mapping from positional and keyword arguments\n"
                "        to parameters (simulating 'functools.partial' behavior.)\n    ");

            // (BlockFinder: tokeneater as BlockFinder's)
            make_inspect_class("BlockFinder", {}, {
                { "__init__", method("BlockFinder", "__init__", {}, [](std::vector<py> const& a)
                    {
                        auto const& self = a[0];
                        self.set_attr("indent", py{ 0 });
                        self.set_attr("singleline", boolean(false));
                        self.set_attr("started", boolean(false));
                        self.set_attr("passline", boolean(false));
                        self.set_attr("indecorator", boolean(false));
                        self.set_attr("last", py{ 1 });
                        self.set_attr("body_col0", none());
                        return none();
                    }) },
                { "tokeneater", method("BlockFinder", "tokeneater", { py_positional("type"), py_positional("token"), py_positional("srowcol"), py_positional("erowcol"),
                    py_positional("line") }, [](std::vector<py> const& a)
                    {
                        auto const& self = a[0];
                        block_finder f;
                        f.indent = self.attr("indent").as_index();
                        f.singleline = self.attr("singleline").truthy();
                        f.started = self.attr("started").truthy();
                        f.passline = self.attr("passline").truthy();
                        f.indecorator = self.attr("indecorator").truthy();
                        f.last = self.attr("last").as_index();
                        if (!self.attr("body_col0").is_none())
                            f.bodyCol0 = self.attr("body_col0").as_index();
                        bool end = false;
                        try
                        {
                            f.tokeneater(static_cast<int>(a[1].as_index()), a[2].str(), { a[3][py{ 0 }].as_index(), a[3][py{ 1 }].as_index() },
                                { a[4][py{ 0 }].as_index(), a[4][py{ 1 }].as_index() });
                        }
                        catch (block_finder::end_of_block const&)
                        {
                            end = true;
                        }
                        self.set_attr("indent", py{ f.indent });
                        self.set_attr("singleline", boolean(f.singleline));
                        self.set_attr("started", boolean(f.started));
                        self.set_attr("passline", boolean(f.passline));
                        self.set_attr("indecorator", boolean(f.indecorator));
                        self.set_attr("last", py{ f.last });
                        self.set_attr("body_col0", f.bodyCol0 ? py{ *f.bodyCol0 } : none());
                        if (end)
                            py_raise(cls("EndOfBlock")());
                        return none();
                    }) } },
                "Provide a tokeneater() method to detect the end of a code block.");

            // (BufferFlags)
            py const flags = py_dict();
            for (auto const& [name, value] : std::initializer_list<std::pair<char const*, long long>>{ { "SIMPLE", 0x0 }, { "WRITABLE", 0x1 }, { "FORMAT", 0x4 },
                { "ND", 0x8 }, { "STRIDES", 0x18 }, { "C_CONTIGUOUS", 0x38 }, { "F_CONTIGUOUS", 0x58 }, { "ANY_CONTIGUOUS", 0x98 }, { "INDIRECT", 0x118 },
                { "CONTIG", 0x9 }, { "CONTIG_RO", 0x8 }, { "STRIDED", 0x19 }, { "STRIDED_RO", 0x18 }, { "RECORDS", 0x1D }, { "RECORDS_RO", 0x1C }, { "FULL", 0x11D },
                { "FULL_RO", 0x11C }, { "READ", 0x100 }, { "WRITE", 0x200 } })
                flags.set_item(py{ name }, py{ value });
            s.classes["BufferFlags"] = import_attribute("enum", "IntFlag").call({ py{ "BufferFlags" }, flags }, { { "module", py{ sModule } } });
        }

        // --- the module's functions

        using body_type = std::function<py(std::vector<py> const&)>;

        py module_function(char const* aName, std::vector<py_parameter> aParameters, body_type aBody, char const* aDoc = "", bool aVarArgs = false, bool aVarKeywords = false)
        {
            std::string const name = aName;
            return function(name, name, [name, aParameters, aBody, aVarArgs, aVarKeywords](std::vector<py> const& aArgs, object* aKwargs) -> py
            {
                py rest;
                py extra;
                auto a = py_bind(name, aParameters, aArgs, aKwargs, aVarArgs ? &rest : nullptr, aVarKeywords ? &extra : nullptr);
                if (aVarArgs)
                    a.push_back(rest);
                if (aVarKeywords)
                    a.push_back(extra);
                return aBody(a);
            }, aDoc);
        }

        py predicate(char const* aName, std::function<bool(py const&)> aTest, char const* aDoc)
        {
            return module_function(aName, { py_positional("object") }, [aTest](std::vector<py> const& a) { return boolean(aTest(a[0])); }, aDoc);
        }

        std::vector<std::pair<std::string, py>> make_functions()
        {
            auto& s = state();
            std::vector<std::pair<std::string, py>> r;
            r.emplace_back("get_annotations", module_function("get_annotations", { py_positional("obj"), py_keyword("globals", none()), py_keyword("locals", none()),
                py_keyword("eval_str", boolean(false)) }, [](std::vector<py> const& a) { return get_annotations(a[0], a[1], a[2], a[3].truthy()); },
                "Compute the annotations dict for an object.\n\n    obj may be a callable, class, or module.\n    Passing in an object of any other type raises TypeError.\n\n"
                "    Returns a dict.  get_annotations() returns a new dict every time\n    it's called; calling it twice on the same object will return two\n"
                "    different but equivalent dicts.\n"));
            r.emplace_back("ismodule", predicate("ismodule", ismodule, "Return true if the object is a module."));
            r.emplace_back("isclass", predicate("isclass", isclass, "Return true if the object is a class."));
            r.emplace_back("ismethod", predicate("ismethod", ismethod, "Return true if the object is an instance method."));
            r.emplace_back("ismethoddescriptor", predicate("ismethoddescriptor", ismethoddescriptor, "Return true if the object is a method descriptor.\n\n"
                "    But not if ismethod(), isclass() or isfunction() is true.\n"));
            r.emplace_back("isdatadescriptor", predicate("isdatadescriptor", isdatadescriptor, "Return true if the object is a data descriptor.\n\n"
                "    But not if ismethod(), isclass() or isfunction() is true.\n"));
            r.emplace_back("ismemberdescriptor", predicate("ismemberdescriptor", ismemberdescriptor, "Return true if the object is a member descriptor.\n\n"
                "        Member descriptors are specialized descriptors defined in extension\n        modules."));
            r.emplace_back("isgetsetdescriptor", predicate("isgetsetdescriptor", isgetsetdescriptor, "Return true if the object is a getset descriptor.\n\n"
                "        getset descriptors are specialized descriptors defined in extension\n        modules."));
            r.emplace_back("isfunction", predicate("isfunction", isfunction, "Return true if the object is a user-defined function."));
            r.emplace_back("isgeneratorfunction", module_function("isgeneratorfunction", { py_positional("obj") }, [](std::vector<py> const& a)
                { return boolean(has_code_flag(a[0], CO_GENERATOR)); }, "Return true if the object is a user-defined generator function."));
            r.emplace_back("markcoroutinefunction", module_function("markcoroutinefunction", { py_positional("func") }, [](std::vector<py> const& a)
                {
                    py func = a[0];
                    if (has_attr(func, "__func__"))
                        func = func.attr("__func__");
                    func.set_attr("_is_coroutine_marker", state().coroutineMark);
                    return func;
                }, "\n    Decorator to ensure callable is recognised as a coroutine function.\n    "));
            r.emplace_back("iscoroutinefunction", module_function("iscoroutinefunction", { py_positional("obj") }, [](std::vector<py> const& a)
                { return boolean(has_code_flag(a[0], CO_COROUTINE) || has_coroutine_mark(a[0])); }, "Return true if the object is a coroutine function."));
            r.emplace_back("isasyncgenfunction", module_function("isasyncgenfunction", { py_positional("obj") }, [](std::vector<py> const& a)
                { return boolean(has_code_flag(a[0], CO_ASYNC_GENERATOR)); }, "Return true if the object is an asynchronous generator function."));
            r.emplace_back("isasyncgen", predicate("isasyncgen", isasyncgen, "Return true if the object is an asynchronous generator."));
            r.emplace_back("isgenerator", predicate("isgenerator", isgenerator, "Return true if the object is a generator."));
            r.emplace_back("iscoroutine", predicate("iscoroutine", iscoroutine, "Return true if the object is a coroutine."));
            r.emplace_back("isawaitable", predicate("isawaitable", [](py const& o)
                {
                    return iscoroutine(o) || (isgenerator(o) && (code_flags(o.attr("gi_code")) & CO_ITERABLE_COROUTINE) != 0) ||
                        isinstance(o, import_attribute("collections.abc", "Awaitable"));
                }, "Return true if object can be passed to an ``await`` expression."));
            r.emplace_back("istraceback", predicate("istraceback", istraceback, "Return true if the object is a traceback."));
            r.emplace_back("isframe", predicate("isframe", isframe, "Return true if the object is a frame object."));
            r.emplace_back("iscode", predicate("iscode", iscode, "Return true if the object is a code object."));
            r.emplace_back("isbuiltin", predicate("isbuiltin", isbuiltin, "Return true if the object is a built-in function or method."));
            r.emplace_back("ismethodwrapper", predicate("ismethodwrapper", ismethodwrapper, "Return true if the object is a method wrapper."));
            r.emplace_back("isroutine", predicate("isroutine", isroutine, "Return true if the object is any kind of function or method."));
            r.emplace_back("isabstract", predicate("isabstract", isabstract, "Return true if the object is an abstract base class (ABC)."));
            r.emplace_back("getmembers", module_function("getmembers", { py_positional("object"), py_positional("predicate", none()) }, [](std::vector<py> const& a)
                {
                    return getmembers_of(a[0], a[1], [](py const& o, py const& k) { return builtin("getattr")(o, k); });
                }, "Return all members of an object as (name, value) pairs sorted by name.\n    Optionally, only return members that satisfy a given predicate."));
            r.emplace_back("getmembers_static", module_function("getmembers_static", { py_positional("object"), py_positional("predicate", none()) },
                [](std::vector<py> const& a)
                {
                    return getmembers_of(a[0], a[1], [](py const& o, py const& k) { return getattr_static(o, k, state().sentinel); });
                }, "Return all members of an object as (name, value) pairs sorted by name\n    without triggering dynamic lookup via the descriptor protocol,\n"
                "    __getattr__ or __getattribute__. Optionally, only return members that\n    satisfy a given predicate.\n"));
            r.emplace_back("classify_class_attrs", module_function("classify_class_attrs", { py_positional("cls") }, [](std::vector<py> const& a)
                { return classify_class_attrs(a[0]); }, "Return list of attribute-descriptor tuples."));
            r.emplace_back("getmro", module_function("getmro", { py_positional("cls") }, [](std::vector<py> const& a) { return a[0].attr("__mro__"); },
                "Return tuple of base classes (including cls) in method resolution order."));
            r.emplace_back("unwrap", module_function("unwrap", { py_positional("func"), py_keyword("stop", none()) }, [](std::vector<py> const& a)
                {
                    py const stop = a[1];
                    if (stop.is_none())
                        return unwrap(a[0]);
                    return unwrap(a[0], [stop](py const& f) { return stop(f).truthy(); });
                }, "Get the object wrapped by *func*.\n\n   Follows the chain of :attr:`__wrapped__` attributes returning the last\n   object in the chain.\n"));
            r.emplace_back("indentsize", module_function("indentsize", { py_positional("line") }, [](std::vector<py> const& a) { return py{ indentsize(a[0].str()) }; },
                "Return the indent size, in spaces, at the start of a line of text."));
            r.emplace_back("_findclass", module_function("_findclass", { py_positional("func") }, [](std::vector<py> const& a) { return findclass(a[0]); }));
            r.emplace_back("_finddoc", module_function("_finddoc", { py_positional("obj") }, [](std::vector<py> const& a) { return finddoc(a[0]); }));
            r.emplace_back("getdoc", module_function("getdoc", { py_positional("object") }, [](std::vector<py> const& a) { return getdoc(a[0]); },
                "Get the documentation string for an object.\n\n    All tabs are expanded to spaces.  To clean up docstrings that are\n"
                "    indented to line up with blocks of code, any whitespace than can be\n    uniformly removed from the second line onwards is removed."));
            r.emplace_back("cleandoc", module_function("cleandoc", { py_positional("doc") }, [](std::vector<py> const& a) { return cleandoc(a[0]); },
                "Clean up indentation from docstrings.\n\n    Any whitespace that can be uniformly removed from the second line\n    onwards is removed."));
            r.emplace_back("getfile", module_function("getfile", { py_positional("object") }, [](std::vector<py> const& a) { return getfile(a[0]); },
                "Work out which source or compiled file an object was defined in."));
            r.emplace_back("getmodulename", module_function("getmodulename", { py_positional("path") }, [](std::vector<py> const& a) { return getmodulename(a[0]); },
                "Return the module name for a given file, or None."));
            r.emplace_back("getsourcefile", module_function("getsourcefile", { py_positional("object") }, [](std::vector<py> const& a) { return getsourcefile(a[0]); },
                "Return the filename that can be used to locate an object's source.\n    Return None if no way can be identified to get the source.\n    "));
            r.emplace_back("getabsfile", module_function("getabsfile", { py_positional("object"), py_positional("_filename", none()) }, [](std::vector<py> const& a)
                { return getabsfile(a[0], a[1]); }, "Return an absolute path to the source or compiled file for an object.\n\n"
                "    The idea is for each object to have a unique origin, so this routine\n    normalizes the result as much as possible."));
            r.emplace_back("getmodule", module_function("getmodule", { py_positional("object"), py_positional("_filename", none()) }, [](std::vector<py> const& a)
                { return getmodule(a[0], a[1]); }, "Return the module an object was defined in, or None if not found."));
            r.emplace_back("findsource", module_function("findsource", { py_positional("object") }, [](std::vector<py> const& a) { return findsource(a[0]); },
                "Return the entire source file and starting line number for an object.\n\n    The argument may be a module, class, method, function, traceback, frame,\n"
                "    or code object.  The source code is returned as a list of all the lines\n    in the file and the line number indexes a line in that list.  An OSError\n"
                "    is raised if the source code cannot be retrieved."));
            r.emplace_back("getcomments", module_function("getcomments", { py_positional("object") }, [](std::vector<py> const& a) { return getcomments(a[0]); },
                "Get lines of comments immediately preceding an object's source code.\n\n    Returns None when source can't be found.\n    "));
            r.emplace_back("getblock", module_function("getblock", { py_positional("lines") }, [](std::vector<py> const& a) { return getblock(a[0]); },
                "Extract the block of code at the top of the given list of lines."));
            r.emplace_back("getsourcelines", module_function("getsourcelines", { py_positional("object") }, [](std::vector<py> const& a) { return getsourcelines(a[0]); },
                "Return a list of source lines and starting line number for an object.\n\n    The argument may be a module, class, method, function, traceback, frame,\n"
                "    or code object.  The source code is returned as a list of the lines\n    corresponding to the object and the line number indicates where in the\n"
                "    original source file the first line of code was found.  An OSError is\n    raised if the source code cannot be retrieved."));
            r.emplace_back("getsource", module_function("getsource", { py_positional("object") }, [](std::vector<py> const& a) { return getsource(a[0]); },
                "Return the text of the source code for an object.\n\n    The argument may be a module, class, method, function, traceback, frame,\n"
                "    or code object.  The source code is returned as a single string.  An\n    OSError is raised if the source code cannot be retrieved."));
            r.emplace_back("walktree", module_function("walktree", { py_positional("classes"), py_positional("children"), py_positional("parent") },
                [](std::vector<py> const& a) { return walktree(a[0], a[1], a[2]); }, "Recursive helper function for getclasstree()."));
            r.emplace_back("getclasstree", module_function("getclasstree", { py_positional("classes"), py_positional("unique", boolean(false)) },
                [](std::vector<py> const& a) { return getclasstree(a[0], a[1].truthy()); }, "Arrange the given list of classes into a hierarchy of nested lists."));
            r.emplace_back("getargs", module_function("getargs", { py_positional("co") }, [](std::vector<py> const& a) { return getargs(a[0]); },
                "Get information about the arguments accepted by a code object.\n\n    Three things are returned: (args, varargs, varkw), where\n"
                "    'args' is the list of argument names. Keyword-only arguments are\n    appended. 'varargs' and 'varkw' are the names of the * and **\n"
                "    arguments or None."));
            r.emplace_back("getfullargspec", module_function("getfullargspec", { py_positional("func") }, [](std::vector<py> const& a) { return getfullargspec(a[0]); },
                "Get the names and default values of a callable object's parameters."));
            r.emplace_back("getargvalues", module_function("getargvalues", { py_positional("frame") }, [](std::vector<py> const& a)
                {
                    py const args = getargs(a[0].attr("f_code"));
                    return cls("ArgInfo")(args[py{ 0 }], args[py{ 1 }], args[py{ 2 }], a[0].attr("f_locals"));
                }, "Get information about arguments passed into a particular frame."));
            r.emplace_back("formatannotation", module_function("formatannotation", { py_positional("annotation"), py_positional("base_module", none()) },
                [](std::vector<py> const& a) { return formatannotation(a[0], a[1]); }));
            r.emplace_back("formatannotationrelativeto", module_function("formatannotationrelativeto", { py_positional("object") }, [](std::vector<py> const& a)
                {
                    py const module = attr_or(a[0], "__module__", none());
                    return function("_formatannotation", "formatannotationrelativeto.<locals>._formatannotation", [module](std::vector<py> const& aArgs, object* aKwargs) -> py
                    {
                        auto const b = py_bind("formatannotationrelativeto.<locals>._formatannotation", { py_positional("annotation") }, aArgs, aKwargs);
                        return formatannotation(b[0], module);
                    });
                }));
            r.emplace_back("formatargvalues", module_function("formatargvalues", { py_positional("args"), py_positional("varargs"), py_positional("varkw"),
                py_positional("locals"), py_positional("formatarg", builtin("str")), py_positional("formatvarargs", none()), py_positional("formatvarkw", none()),
                py_positional("formatvalue", none()) }, [](std::vector<py> const& a)
                {
                    py const& locals = a[3];
                    auto const formatarg = [&](py const& n) { return a[4](n).str(); };
                    auto const formatvalue = [&](py const& v) { return a[7].is_none() ? "=" + v.repr() : a[7](v).str(); };
                    std::vector<std::string> specs;
                    for (auto const& name : a[0].items())
                        specs.push_back(formatarg(name) + formatvalue(locals[name]));
                    if (a[1].truthy())
                        specs.push_back((a[5].is_none() ? "*" + a[1].str() : a[5](a[1]).str()) + formatvalue(locals[a[1]]));
                    if (a[2].truthy())
                        specs.push_back((a[6].is_none() ? "**" + a[2].str() : a[6](a[2]).str()) + formatvalue(locals[a[2]]));
                    std::string text;
                    for (std::size_t i = 0u; i < specs.size(); ++i)
                        text += (i > 0u ? ", " : "") + specs[i];
                    return py{ "(" + text + ")" };
                }, "Format an argument spec from the 4 values returned by getargvalues."));
            r.emplace_back("getcallargs", module_function("getcallargs", { py_positional_only("func") }, [](std::vector<py> const& a)
                { return getcallargs(a[0], a[1].items(), a[2]); }, "Get the mapping of arguments to values.", true, true));
            r.emplace_back("getclosurevars", module_function("getclosurevars", { py_positional("func") }, [](std::vector<py> const& a) { return getclosurevars(a[0]); },
                "\n    Get the mapping of free variables to their current values.\n"));
            r.emplace_back("getframeinfo", module_function("getframeinfo", { py_positional("frame"), py_positional("context", py{ 1 }) }, [](std::vector<py> const& a)
                { return getframeinfo(a[0], a[1].as_index()); }, "Get information about a frame or traceback object."));
            r.emplace_back("getlineno", module_function("getlineno", { py_positional("frame") }, [](std::vector<py> const& a) { return a[0].attr("f_lineno"); },
                "Get the line number from a frame object, allowing for optimization."));
            r.emplace_back("getouterframes", module_function("getouterframes", { py_positional("frame"), py_positional("context", py{ 1 }) }, [](std::vector<py> const& a)
                { return getouterframes(a[0], a[1].as_index()); }, "Get a list of records for a frame and all higher (calling) frames."));
            r.emplace_back("getinnerframes", module_function("getinnerframes", { py_positional("tb"), py_positional("context", py{ 1 }) }, [](std::vector<py> const& a)
                { return getinnerframes(a[0], a[1].as_index()); }, "Get a list of records for a traceback's frame and all lower frames."));
            r.emplace_back("currentframe", module_function("currentframe", {}, [](std::vector<py> const&)
                {
                    if (!has_attr(py{ fn_import("sys") }, "_getframe"))
                        return none();
                    return caller_frame();
                }, "Return the frame of the caller or None if this is not possible."));
            r.emplace_back("stack", module_function("stack", { py_positional("context", py{ 1 }) }, [](std::vector<py> const& a)
                { return getouterframes(caller_frame(), a[0].as_index()); }, "Return a list of records for the stack above the caller's frame."));
            r.emplace_back("trace", module_function("trace", { py_positional("context", py{ 1 }) }, [](std::vector<py> const& a)
                {
                    py const exc = import_attribute("sys", "exception")();
                    py const tb = exc.is_none() ? none() : exc.attr("__traceback__");
                    return getinnerframes(tb, a[0].as_index());
                }, "Return a list of records for the stack below the current exception."));
            r.emplace_back("getattr_static", module_function("getattr_static", { py_positional("obj"), py_positional("attr"), py_positional("default", s.sentinel) },
                [](std::vector<py> const& a) { return getattr_static(a[0], a[1], a[2]); }, "Retrieve attributes without triggering dynamic lookup via the\n"
                "       descriptor protocol,  __getattr__ or __getattribute__.\n"));
            auto const state_function = [](char const* aName, char const* aPrefix, char const* aAttributePrefix, char const* aDoc)
            {
                std::string const prefix = aPrefix;
                std::string const attribute = aAttributePrefix;
                return module_function(aName, { py_positional(std::string{ aName } == "getgeneratorstate" ? "generator" : std::string{ aName } == "getcoroutinestate" ?
                    "coroutine" : "agen") }, [prefix, attribute](std::vector<py> const& a)
                    {
                        if (a[0].attr((attribute + "_running").c_str()).truthy())
                            return py{ prefix + "_RUNNING" };
                        if (a[0].attr((attribute + "_suspended").c_str()).truthy())
                            return py{ prefix + "_SUSPENDED" };
                        if (a[0].attr((attribute + "_frame").c_str()).is_none())
                            return py{ prefix + "_CLOSED" };
                        return py{ prefix + "_CREATED" };
                    }, aDoc);
            };
            r.emplace_back("getgeneratorstate", state_function("getgeneratorstate", "GEN", "gi", "Get current state of a generator-iterator."));
            r.emplace_back("getcoroutinestate", state_function("getcoroutinestate", "CORO", "cr", "Get current state of a coroutine object."));
            r.emplace_back("getasyncgenstate", state_function("getasyncgenstate", "AGEN", "ag", "Get current state of an asynchronous generator object."));
            r.emplace_back("getgeneratorlocals", module_function("getgeneratorlocals", { py_positional("generator") }, [](std::vector<py> const& a)
                {
                    if (!isgenerator(a[0]))
                        py_raise("TypeError", a[0].repr() + " is not a Python generator");
                    py const frame = attr_or(a[0], "gi_frame", none());
                    return frame.is_none() ? py_dict() : frame.attr("f_locals");
                }, "\n    Get the mapping of generator local variables to their current values.\n"));
            r.emplace_back("getcoroutinelocals", module_function("getcoroutinelocals", { py_positional("coroutine") }, [](std::vector<py> const& a)
                {
                    py const frame = attr_or(a[0], "cr_frame", none());
                    return frame.is_none() ? py_dict() : frame.attr("f_locals");
                }, "\n    Get the mapping of coroutine local variables to their current values.\n"));
            r.emplace_back("getasyncgenlocals", module_function("getasyncgenlocals", { py_positional("agen") }, [](std::vector<py> const& a)
                {
                    if (!isasyncgen(a[0]))
                        py_raise("TypeError", a[0].repr() + " is not a Python async generator");
                    py const frame = attr_or(a[0], "ag_frame", none());
                    return frame.is_none() ? py_dict() : frame.attr("f_locals");
                }, "\n    Get the mapping of asynchronous generator local variables to their current\n    values.\n"));
            r.emplace_back("signature", module_function("signature", { py_positional("obj"), py_keyword("follow_wrapped", boolean(true)), py_keyword("globals", none()),
                py_keyword("locals", none()), py_keyword("eval_str", boolean(false)) }, [](std::vector<py> const& a)
                {
                    return cls("Signature").attr("from_callable").call({ a[0] }, { { "follow_wrapped", a[1] }, { "globals", a[2] }, { "locals", a[3] }, { "eval_str", a[4] } });
                }, "Get a signature object for the passed callable."));
            r.emplace_back("_signature_from_callable", module_function("_signature_from_callable", { py_positional("obj"), py_keyword("follow_wrapper_chains", boolean(true)),
                py_keyword("skip_bound_arg", boolean(true)), py_keyword("globals", none()), py_keyword("locals", none()), py_keyword("eval_str", boolean(false)),
                py_keyword("sigcls") }, [](std::vector<py> const& a)
                {
                    auto o = options_of(a[6], a[1].truthy(), a[3], a[4], a[5].truthy());
                    o.skipBoundArg = a[2].truthy();
                    return signature_from_callable(a[0], o);
                }));
            r.emplace_back("_signature_fromstr", module_function("_signature_fromstr", { py_positional("cls"), py_positional("obj"), py_positional("s"),
                py_positional("skip_bound_arg", boolean(true)) }, [](std::vector<py> const& a) { return signature_fromstr(a[0], a[1], a[2].str(), a[3].truthy()); }));
            r.emplace_back("_signature_is_builtin", predicate("_signature_is_builtin", signature_is_builtin, ""));
            r.emplace_back("_signature_is_functionlike", predicate("_signature_is_functionlike", signature_is_functionlike, ""));
            r.emplace_back("_signature_bound_method", module_function("_signature_bound_method", { py_positional("sig") }, [](std::vector<py> const& a)
                { return signature_bound_method(a[0]); }));
            r.emplace_back("_descriptor_get", module_function("_descriptor_get", { py_positional("descriptor"), py_positional("obj") }, [](std::vector<py> const& a)
                { return descriptor_get(a[0], a[1]); }));
            r.emplace_back("_check_instance", module_function("_check_instance", { py_positional("obj"), py_positional("attr") }, [](std::vector<py> const& a)
                { return check_instance(a[0], a[1]); }));
            r.emplace_back("_check_class", module_function("_check_class", { py_positional("klass"), py_positional("attr") }, [](std::vector<py> const& a)
                { return check_class(a[0], a[1]); }));
            r.emplace_back("_shadowed_dict", module_function("_shadowed_dict", { py_positional("klass") }, [](std::vector<py> const& a) { return shadowed_dict(a[0]); }));
            return r;
        }

        bool inspect_module(std::vector<std::pair<std::string, object*>>& aAttributes)
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
                static char const* const sKindNames[] = { "_POSITIONAL_ONLY", "_POSITIONAL_OR_KEYWORD", "_VAR_POSITIONAL", "_KEYWORD_ONLY", "_VAR_KEYWORD" };
                for (int i = 0; i < 5; ++i)
                    aAttributes.emplace_back(sKindNames[i], s.kinds[i].reference());
                for (auto const& [name, value] : std::initializer_list<std::pair<char const*, long long>>{ { "CO_OPTIMIZED", 1 }, { "CO_NEWLOCALS", 2 },
                    { "CO_VARARGS", 4 }, { "CO_VARKEYWORDS", 8 }, { "CO_NESTED", 16 }, { "CO_GENERATOR", 32 }, { "CO_NOFREE", 64 }, { "CO_COROUTINE", 128 },
                    { "CO_ITERABLE_COROUTINE", 256 }, { "CO_ASYNC_GENERATOR", 512 }, { "TPFLAGS_IS_ABSTRACT", 1ll << 20 } })
                    aAttributes.emplace_back(name, make_int(value));
                for (auto prefix : { "GEN", "CORO", "AGEN" })
                    for (auto suffix : { "_CREATED", "_RUNNING", "_SUSPENDED", "_CLOSED" })
                        aAttributes.emplace_back(std::string{ prefix } + suffix, make_str(std::string{ prefix } + suffix));
                for (auto const& [name, value] : std::initializer_list<std::pair<char const*, py>>{ { "_empty", s.empty }, { "_void", s.void_ },
                    { "_sentinel", s.sentinel }, { "_is_coroutine_mark", s.coroutineMark }, { "modulesbyfile", s.modulesByFile },
                    { "_filesbymodname", s.filesByModname } })
                    aAttributes.emplace_back(name, value.reference());
                std::vector<py> nonUser;
                for (auto t : { "WrapperDescriptorType", "MethodWrapperType", "ClassMethodDescriptorType", "BuiltinFunctionType" })
                    nonUser.push_back(types_attr(t));
                aAttributes.emplace_back("_NonUserDefinedCallables", py_tuple(nonUser).release());
                static char const* const sAll[] = { "AGEN_CLOSED", "AGEN_CREATED", "AGEN_RUNNING", "AGEN_SUSPENDED", "ArgInfo", "Arguments", "Attribute", "BlockFinder",
                    "BoundArguments", "BufferFlags", "CORO_CLOSED", "CORO_CREATED", "CORO_RUNNING", "CORO_SUSPENDED", "CO_ASYNC_GENERATOR", "CO_COROUTINE", "CO_GENERATOR",
                    "CO_ITERABLE_COROUTINE", "CO_NESTED", "CO_NEWLOCALS", "CO_NOFREE", "CO_OPTIMIZED", "CO_VARARGS", "CO_VARKEYWORDS", "ClassFoundException", "ClosureVars",
                    "EndOfBlock", "FrameInfo", "FullArgSpec", "GEN_CLOSED", "GEN_CREATED", "GEN_RUNNING", "GEN_SUSPENDED", "Parameter", "Signature", "TPFLAGS_IS_ABSTRACT",
                    "Traceback", "classify_class_attrs", "cleandoc", "currentframe", "findsource", "formatannotation", "formatannotationrelativeto", "formatargvalues",
                    "get_annotations", "getabsfile", "getargs", "getargvalues", "getasyncgenlocals", "getasyncgenstate", "getattr_static", "getblock", "getcallargs",
                    "getclasstree", "getclosurevars", "getcomments", "getcoroutinelocals", "getcoroutinestate", "getdoc", "getfile", "getframeinfo", "getfullargspec",
                    "getgeneratorlocals", "getgeneratorstate", "getinnerframes", "getlineno", "getmembers", "getmembers_static", "getmodule", "getmodulename", "getmro",
                    "getouterframes", "getsource", "getsourcefile", "getsourcelines", "indentsize", "isabstract", "isasyncgen", "isasyncgenfunction", "isawaitable",
                    "isbuiltin", "isclass", "iscode", "iscoroutine", "iscoroutinefunction", "isdatadescriptor", "isframe", "isfunction", "isgenerator",
                    "isgeneratorfunction", "isgetsetdescriptor", "ismemberdescriptor", "ismethod", "ismethoddescriptor", "ismethodwrapper", "ismodule", "isroutine",
                    "istraceback", "markcoroutinefunction", "signature", "stack", "trace", "unwrap", "walktree" };
                std::vector<py> all;
                for (auto n : sAll)
                    all.emplace_back(n);
                aAttributes.emplace_back("__all__", py_list(all).release());
                aAttributes.emplace_back("__author__", py_tuple({ py{ "Ka-Ping Yee <ping@lfw.org>" }, py{ "Yury Selivanov <yselivanov@sprymix.com>" } }).release());
                for (auto module : { "abc", "collections", "enum", "itertools", "linecache", "os", "re", "sys", "types", "functools", "builtins" })
                    aAttributes.emplace_back(module, py{ fn_import(module) }.release());
                aAttributes.emplace_back("iskeyword", import_attribute("keyword", "iskeyword").release());
                aAttributes.emplace_back("attrgetter", import_attribute("operator", "attrgetter").release());
                aAttributes.emplace_back("namedtuple", import_attribute("collections", "namedtuple").release());
                aAttributes.emplace_back("OrderedDict", import_attribute("collections", "OrderedDict").release());
                aAttributes.emplace_back("make_weakref", import_attribute("weakref", "ref").release());
                return none();
            });
            release(result);
            return result != nullptr;
        }

        inline bool const sInspect = register_module("inspect", inspect_module);
    }
}
