/*
  py.hpp

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

// Python operations in C++: the parts of NumPy that NumPy implements in Python are written with these
// (an operation that raises a Python exception throws py::raised; the module's functions return null for it)

#pragma once

#include <vector>
#include "numpy.hpp"

namespace neos::numpy::py
{
    struct raised {};

    class value;

    struct kw
    {
        char const* name;
        value const* v;
        kw(char const* aName, value const& aValue) : name{ aName }, v{ &aValue } {}
    };

    class value
    {
    public:
        value() = default;
        value(ref aRef);
        value(int aValue);
        value(long aValue);
        value(long long aValue);
        value(unsigned long aValue);
        value(unsigned long long aValue);
        value(double aValue);
        value(bool aValue);
        value(char const* aValue);
        value(std::string const& aValue);
        static value steal(object* aObject);  // (null: raised)
        static value borrow(object* aObject); // (null: empty)
    public:
        explicit operator bool() const = delete;
        bool empty() const { return !iRef; }
        bool is_none() const;
        object* get() const { return iRef.get(); }
        object* release() const { if (iRef) ctx->retain(iRef.get()); return iRef.get(); } // (a new reference)
        ndarray* array() const;               // (an ndarray or NumPy scalar; else TypeError)
    public:
        value attr(char const* aName) const;
        void set_attr(char const* aName, value const& aValue) const;
        value operator[](value const& aKey) const;
        void set(value const& aKey, value const& aValue) const;
        value call(std::vector<value> const& aArgs, std::vector<kw> const& aKwargs = {}) const;
        template <typename... Args>
        value operator()(Args const&... aArgs) const
        {
            std::vector<value> args;
            std::vector<kw> kwargs;
            (add_argument(args, kwargs, aArgs), ...);
            return call(args, kwargs);
        }
        template <typename... Args>
        value method(char const* aName, Args const&... aArgs) const
        {
            return attr(aName)(aArgs...);
        }
        bool truth() const;
        long long to_int() const;
        double to_double() const;
        std::string to_str() const;     // (a str)
        std::string str() const;        // str(x)
        std::string repr() const;       // repr(x)
        long long len() const;
        std::vector<value> items() const;   // (iterate)
        value shape() const { return attr("shape"); }
        long long ndim() const;
        long long size() const;
    private:
        static void add_argument(std::vector<value>& aArgs, std::vector<kw>&, value const& aValue) { aArgs.push_back(aValue); }
        static void add_argument(std::vector<value>&, std::vector<kw>& aKwargs, kw const& aValue) { aKwargs.push_back(aValue); }
        template <typename T>
        static void add_argument(std::vector<value>& aArgs, std::vector<kw>&, T const& aValue) { aArgs.push_back(value{ aValue }); }
    private:
        ref iRef;
    };

    value none();
    value np(char const* aName);    // numpy.<name>
    value tuple(std::vector<value> const& aItems);
    value list(std::vector<value> const& aItems);
    value dict(std::vector<std::pair<char const*, value>> const& aItems);
    value slice(value const& aStart, value const& aStop, value const& aStep = {});
    value all();                    // (slice(None))
    value ellipsis();
    value array_value(ndarray* aArray); // (steals)
    bool isinstance_array(value const& aValue);
    bool is_str(value const& aValue);
    bool is_int(value const& aValue);
    bool is_float(value const& aValue);
    bool is_tuple(value const& aValue);
    bool is_list(value const& aValue);
    bool is_sequence(value const& aValue);
    bool is_callable(value const& aValue);
    value dtype_of(value const& aValue);    // (aValue.dtype)
    [[noreturn]] void raise(char const* aType, std::string const& aMessage);
    void check();                   // (throws if an exception was raised)
    std::string caught();           // (the exception raised, "type: message", which is cleared)

    value operator+(value const& a, value const& b);
    value operator-(value const& a, value const& b);
    value operator*(value const& a, value const& b);
    value operator/(value const& a, value const& b);
    value operator%(value const& a, value const& b);
    value operator&(value const& a, value const& b);
    value operator|(value const& a, value const& b);
    value operator^(value const& a, value const& b);
    value operator<<(value const& a, value const& b);
    value operator>>(value const& a, value const& b);
    value floordiv(value const& a, value const& b);
    value pow(value const& a, value const& b);
    value matmul(value const& a, value const& b);
    value operator-(value const& a);
    value operator~(value const& a);
    value abs(value const& a);
    value operator==(value const& a, value const& b);
    value operator!=(value const& a, value const& b);
    value operator<(value const& a, value const& b);
    value operator<=(value const& a, value const& b);
    value operator>(value const& a, value const& b);
    value operator>=(value const& a, value const& b);
    bool is(value const& a, value const& b);
    inline bool truth(value const& a) { return a.truth(); }

    // a module function written with these: the arguments, the result, and exceptions
    object* guard(object* (*aFunction)(object*, object*), object* aArgs, object* aKwargs);
}
