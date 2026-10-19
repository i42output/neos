/*
  values.hpp

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

#pragma once

// Python values as C++ values (for the modules of the standard library that CPython implements in Python): py owns a reference; its operations
// are Python's (e.g. a + b is binary(NEOS_PY_ADD)); an operation that raises a Python exception throws raised (the exception remains raised), which
// guarded catches at the boundary of a builtin function

#include <functional>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>
#include "objects.hpp"

namespace neos::python_runtime
{
    namespace detail
    {
        struct raised {};   // (a Python exception was raised: it is pending)

        inline object* py_checked(object* aObject)
        {
            if (aObject == nullptr)
                throw raised{};
            return aObject;
        }
        inline void py_checked(bool aOk)
        {
            if (!aOk)
                throw raised{};
        }

        class py
        {
        public:
            py() = default;
            explicit py(object* aNew) : iRef{ py_checked(aNew) } {}       // (a new reference (null: throws raised))
            py(ref aRef) : iRef{ std::move(aRef) } { py_checked(iRef.get()); }
            py(int aValue) : iRef{ make_int(aValue) } {}
            py(long aValue) : iRef{ make_int(aValue) } {}
            py(long long aValue) : iRef{ make_int(aValue) } {}
            py(bool) = delete;  // (py::boolean)
            py(std::size_t aValue) : iRef{ make_int(static_cast<long long>(aValue)) } {}
            py(double aValue) : iRef{ make_float(aValue) } {}
            py(char const* aValue) : iRef{ make_str(aValue) } {}
            py(std::string aValue) : iRef{ make_str(std::move(aValue)) } {}
            static py borrowed(object* aObject) { return py{ ref::borrowed(py_checked(aObject)) }; }
            static py boolean(bool aValue) { return py{ make_bool(aValue) }; }
            static py none() { return borrowed(detail::none()); }
        public:
            object* get() const { return iRef.get(); }
            object* release() { return iRef.release_ownership(); }     // (a new reference)
            object* reference() const { retain(iRef.get()); return iRef.get(); }
            bool empty() const { return iRef.get() == nullptr; }
            bool is_none() const { return iRef.get() == detail::none(); }
            bool is(py const& aOther) const { return iRef.get() == aOther.iRef.get(); }
        public:
            py attr(char const* aName) const { return py{ getattr(get(), aName) }; }
            void set_attr(char const* aName, py const& aValue) const { py_checked(setattr(get(), aName, aValue.get()) == 0); }
            py operator[](py const& aKey) const { return py{ getitem(get(), aKey.get()) }; }
            void set_item(py const& aKey, py const& aValue) const { py_checked(setitem(get(), aKey.get(), aValue.get()) == 0); }
            py call(std::vector<py> const& aArgs, std::vector<std::pair<char const*, py>> const& aKeywords = {}) const
            {
                std::vector<object*> args;
                for (auto const& a : aArgs)
                    args.push_back(a.reference());
                ref kwargs;
                if (!aKeywords.empty())
                {
                    kwargs = ref{ make_dict() };
                    for (auto const& [name, value] : aKeywords)
                    {
                        ref const key{ make_str(name) };
                        dict_set(kwargs.get(), key.get(), value.get());
                    }
                }
                return py{ invoke(get(), std::move(args), kwargs.get()) };
            }
            template <typename... Args>
            py operator()(Args const&... aArgs) const { return call(std::vector<py>{ py{ aArgs }... }); }
            py method(char const* aName, std::vector<py> const& aArgs = {}, std::vector<std::pair<char const*, py>> const& aKeywords = {}) const
            {
                return attr(aName).call(aArgs, aKeywords);
            }
        public:
            bool truthy() const
            {
                auto const t = truth(get());
                py_checked(t >= 0);
                return t == 1;
            }
            double as_double() const
            {
                double d = 0.0;
                py_checked(to_double(get(), d));
                return d;
            }
            long long as_index() const
            {
                long long n = 0;
                py_checked(to_index(get(), n));
                return n;
            }
            std::string str() const
            {
                std::string text;
                py_checked(str_of(get(), text));
                return text;
            }
            std::string repr() const
            {
                std::string text;
                py_checked(repr_of(get(), text));
                return text;
            }
            long long size() const
            {
                auto const n = length(get());
                py_checked(n >= 0);
                return n;
            }
            std::vector<py> items() const      // (its items (it is iterated))
            {
                std::vector<object*> values;
                py_checked(collect(get(), values));
                std::vector<py> result;
                for (auto v : values)
                    result.emplace_back(ref{ v });
                return result;
            }
            bool contains(py const& aItem) const
            {
                auto const c = detail::contains(get(), aItem.get());
                py_checked(c >= 0);
                return c == 1;
            }
        private:
            ref iRef;
        };

        inline py py_binary(int aOperation, py const& a, py const& b) { return py{ binary(aOperation, a.get(), b.get(), false) }; }
        inline py operator+(py const& a, py const& b) { return py_binary(NEOS_PY_ADD, a, b); }
        inline py operator-(py const& a, py const& b) { return py_binary(NEOS_PY_SUBTRACT, a, b); }
        inline py operator*(py const& a, py const& b) { return py_binary(NEOS_PY_MULTIPLY, a, b); }
        inline py operator/(py const& a, py const& b) { return py_binary(NEOS_PY_TRUE_DIVIDE, a, b); }
        inline py operator%(py const& a, py const& b) { return py_binary(NEOS_PY_REMAINDER, a, b); }
        inline py operator<<(py const& a, py const& b) { return py_binary(NEOS_PY_LSHIFT, a, b); }
        inline py operator>>(py const& a, py const& b) { return py_binary(NEOS_PY_RSHIFT, a, b); }
        inline py operator|(py const& a, py const& b) { return py_binary(NEOS_PY_OR, a, b); }
        inline py operator&(py const& a, py const& b) { return py_binary(NEOS_PY_AND, a, b); }
        inline py operator^(py const& a, py const& b) { return py_binary(NEOS_PY_XOR, a, b); }
        inline py floordiv(py const& a, py const& b) { return py_binary(NEOS_PY_FLOOR_DIVIDE, a, b); }
        inline py power(py const& a, py const& b) { return py_binary(NEOS_PY_POWER, a, b); }
        inline py operator-(py const& a) { return py{ unary(NEOS_PY_NEGATIVE, a.get()) }; }
        inline py py_abs(py const& a) { return py{ unary(NEOS_PY_ABSOLUTE, a.get()) }; }
        inline py py_compare(int aOperation, py const& a, py const& b) { return py{ compare(aOperation, a.get(), b.get()) }; }
        inline bool operator<(py const& a, py const& b) { return py_compare(NEOS_PY_LT, a, b).truthy(); }
        inline bool operator<=(py const& a, py const& b) { return py_compare(NEOS_PY_LE, a, b).truthy(); }
        inline bool operator>(py const& a, py const& b) { return py_compare(NEOS_PY_GT, a, b).truthy(); }
        inline bool operator>=(py const& a, py const& b) { return py_compare(NEOS_PY_GE, a, b).truthy(); }
        inline bool operator==(py const& a, py const& b) { return py_compare(NEOS_PY_EQ, a, b).truthy(); }
        inline bool operator!=(py const& a, py const& b) { return py_compare(NEOS_PY_NE, a, b).truthy(); }

        inline py builtin(char const* aName) { return py{ fn_builtin(aName) }; }
        inline py import_attribute(char const* aModule, char const* aName) { return py{ module_attribute(aModule, aName) }; }
        inline py py_list(std::vector<py> const& aItems = {})
        {
            std::vector<object*> items;
            for (auto const& i : aItems)
                items.push_back(i.reference());
            return py{ make_list(std::move(items)) };
        }
        inline py py_tuple(std::vector<py> const& aItems = {})
        {
            std::vector<object*> items;
            for (auto const& i : aItems)
                items.push_back(i.reference());
            return py{ make_tuple(std::move(items)) };
        }
        inline py py_dict() { return py{ make_dict() }; }
        inline py type_of(py const& aObject) { return py{ class_of(aObject.get()) }; }
        inline bool isinstance(py const& aObject, py const& aClass) { return py{ b_isinstance({ aObject.get(), aClass.get() }, nullptr) }.truthy(); }
        inline bool issubclass(py const& aClass, py const& aBase) { return py{ b_issubclass({ aClass.get(), aBase.get() }, nullptr) }.truthy(); }

        // (raise an exception: aClass(aMessage))
        [[noreturn]] inline void py_raise(char const* aClass, std::string const& aMessage)
        {
            error(aClass, aMessage);
            throw raised{};
        }
        [[noreturn]] inline void py_raise(py const& aException)
        {
            fn_raise(aException.get());
            throw raised{};
        }
        // (the pending exception, if it is an instance of one of aClasses: caught (true); otherwise it remains raised (false))
        inline bool caught(std::initializer_list<char const*> aClasses)
        {
            ref e = take_exception();
            for (auto c : aClasses)
                if (exception_is(e.get(), c))
                    return true;
            reraise_exception(e.get());
            return false;
        }
        // (raise an exception that was taken (as a bare raise: its context is unchanged))
        [[noreturn]] inline void py_reraise(py const& aException)
        {
            reraise_exception(aException.get());
            throw raised{};
        }
        // (the pending exception: taken (it is handled))
        inline py py_take_exception() { return py{ take_exception() }; }

        // (a parameter of a function whose body is C++, as a Python function's parameters are (py_bind))
        struct py_parameter
        {
            enum kind_type { PositionalOnly, PositionalOrKeyword, KeywordOnly };
            std::string name;
            kind_type kind = PositionalOrKeyword;
            py defaultValue;            ///< (empty: required)
        };
        inline py_parameter py_positional(char const* aName, py aDefault = {}) { return py_parameter{ aName, py_parameter::PositionalOrKeyword, std::move(aDefault) }; }
        inline py_parameter py_positional_only(char const* aName, py aDefault = {}) { return py_parameter{ aName, py_parameter::PositionalOnly, std::move(aDefault) }; }
        inline py_parameter py_keyword(char const* aName, py aDefault = {}) { return py_parameter{ aName, py_parameter::KeywordOnly, std::move(aDefault) }; }
        // (arguments bound to parameters as a Python function's are (CPython's messages; aQualname: the function's qualified name): their values
        // in order of the parameters; aRest non-null: the function has *args (the other positional arguments: a tuple); aExtra non-null: it has
        // **kwargs (the other keyword arguments: a dict) (values.cpp))
        std::vector<py> py_bind(std::string const& aQualname, std::vector<py_parameter> const& aParameters, std::vector<py> const& aArgs, object* aKwargs,
            py* aRest = nullptr, py* aExtra = nullptr);

        // (a function whose body is C++ (e.g. a closure): of its arguments and keyword arguments (values.cpp))
        using native_body = std::function<py(std::vector<py> const&, object*)>;
        py make_native_function(std::string aName, std::string aQualname, std::string aDoc, std::string aModule, native_body aBody);

        // (a generator, coroutine or asynchronous generator whose body is C++: it runs on a coroutine of its own (as a Python generator's 
        // body does), so py_yield and py_await suspend it)
        inline py make_native_generator(std::string const& aName, std::string const& aQualname, std::string const& aModule, std::function<py()> aBody, 
            object* (*aKind)(std::vector<object*> const&, object*) = b_generator)
        {
            py const body = make_native_function(aName, aQualname, "", aModule, [aBody](std::vector<py> const&, object*) { return aBody(); });
            py const name{ aName };
            py const qualname{ aQualname };
            return py{ aKind({ body.get(), name.get(), qualname.get() }, nullptr) };
        }
        inline py make_native_coroutine(std::string const& aName, std::string const& aQualname, std::string const& aModule, std::function<py()> aBody)
        {
            return make_native_generator(aName, aQualname, aModule, std::move(aBody), b_coroutine);
        }
        // (yield value: the value sent)
        inline py py_yield(py const& aValue) { return py{ b_yield({ aValue.get() }, nullptr) }; }
        // (await awaitable: its result)
        inline py py_await(py const& aAwaitable) { return py{ b_await({ aAwaitable.get() }, nullptr) }; }

        // (a builtin function's body: a Python exception that it raised (raised) is returned as null)
        template <typename F>
        inline object* py_guarded(F&& aBody)
        {
            try
            {
                return aBody().release();
            }
            catch (raised const&)
            {
                return nullptr;
            }
        }
    }
}
