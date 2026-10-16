/*
  objects.cpp

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
  Python's core objects (None, bool, int, float, complex, str, list, tuple, dict, slice, range, iterators and builtin
  functions) and the object protocol (see neos/runtime/python/object.h): the operations that scripts perform on
  objects are dispatched to the objects' types, which may be those of other plugins (e.g. numpy).

  Limitation: an int is 64-bit (an operation whose result does not fit raises OverflowError).
*/

#include <algorithm>
#include <atomic>
#include <charconv>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <neos/runtime/python/object.h>
#include "python.hpp"

namespace neos::python_runtime
{
    namespace
    {
        using object = neos_py_object;

        // --- object storage

        struct header
        {
            neos_rt_object base;
            std::atomic<long> references{ 1 };
        };

        void retain_object(neos_rt_object* aObject)
        {
            reinterpret_cast<header*>(aObject)->references.fetch_add(1, std::memory_order_relaxed);
        }

        template <typename T>
        void release_object(neos_rt_object* aObject)
        {
            if (reinterpret_cast<header*>(aObject)->references.fetch_sub(1, std::memory_order_acq_rel) == 1)
                delete reinterpret_cast<T*>(aObject);
        }

        void retain_static(neos_rt_object*) {}
        void release_static(neos_rt_object*) {}

        void retain(object* aObject) { if (aObject != nullptr) aObject->type->retain(aObject); }
        void release(object* aObject) { if (aObject != nullptr) aObject->type->release(aObject); }

        // an owned reference
        class ref
        {
        public:
            ref() = default;
            explicit ref(object* aObject) : iObject{ aObject } {}
            ref(ref const& aOther) : iObject{ aOther.iObject } { retain(iObject); }
            ref(ref&& aOther) noexcept : iObject{ aOther.iObject } { aOther.iObject = nullptr; }
            ~ref() { release(iObject); }
            ref& operator=(ref aOther) { std::swap(iObject, aOther.iObject); return *this; }
            static ref borrowed(object* aObject) { retain(aObject); return ref{ aObject }; }
        public:
            object* get() const { return iObject; }
            object* release_ownership() { auto const result = iObject; iObject = nullptr; return result; }
            explicit operator bool() const { return iObject != nullptr; }
        private:
            object* iObject = nullptr;
        };

        neos_py_type const* python_type(object* aObject)
        {
            if (aObject == nullptr)
                return nullptr;
            auto const* type = reinterpret_cast<neos_py_type const*>(aObject->type);
            return type->magic == NEOS_PY_TYPE_MAGIC ? type : nullptr;
        }

        neos_py_context* context();

        // --- the core types

        neos_py_type make_type(char const* aQualifiedName, char const* aName, void (*aRetain)(neos_rt_object*), void (*aRelease)(neos_rt_object*))
        {
            neos_py_type type{};
            type.base = neos_rt_object_type{ aQualifiedName, aRetain, aRelease };
            type.magic = NEOS_PY_TYPE_MAGIC;
            type.name = aName;
            return type;
        }

        struct int_object { header h; long long value; };
        struct float_object { header h; double value; };
        struct complex_object { header h; double real; double imag; };
        struct str_object { header h; std::string value; };
        struct list_object { header h; std::vector<object*> items; ~list_object() { for (auto i : items) release(i); } };
        struct tuple_object { header h; std::vector<object*> items; ~tuple_object() { for (auto i : items) release(i); } };
        struct dict_object { header h; std::vector<std::pair<object*, object*>> items; ~dict_object() { for (auto& i : items) { release(i.first); release(i.second); } } };
        struct slice_object { header h; object* start; object* stop; object* step; ~slice_object() { release(start); release(stop); release(step); } };
        struct range_object { header h; long long start; long long stop; long long step; };
        struct iterator_object { header h; object* sequence; std::size_t index = 0u; ~iterator_object() { release(sequence); } };
        using native_function = neos_py_object* (*)(neos_py_context*, void*, neos_py_object*, neos_py_object*);
        struct function_object { header h; std::string name; native_function function; void* data; object* self; ~function_object() { release(self); } };
        struct singleton { neos_rt_object base; };

        neos_py_type sNoneType = make_type("python.NoneType", "NoneType", retain_static, release_static);
        neos_py_type sNotImplementedType = make_type("python.NotImplementedType", "NotImplementedType", retain_static, release_static);
        neos_py_type sEllipsisType = make_type("python.ellipsis", "ellipsis", retain_static, release_static);
        neos_py_type sBoolType = make_type("python.bool", "bool", retain_static, release_static);
        neos_py_type sIntType = make_type("python.int", "int", retain_object, release_object<int_object>);
        neos_py_type sFloatType = make_type("python.float", "float", retain_object, release_object<float_object>);
        neos_py_type sComplexType = make_type("python.complex", "complex", retain_object, release_object<complex_object>);
        neos_py_type sStrType = make_type("python.str", "str", retain_object, release_object<str_object>);
        neos_py_type sListType = make_type("python.list", "list", retain_object, release_object<list_object>);
        neos_py_type sTupleType = make_type("python.tuple", "tuple", retain_object, release_object<tuple_object>);
        neos_py_type sDictType = make_type("python.dict", "dict", retain_object, release_object<dict_object>);
        neos_py_type sSliceType = make_type("python.slice", "slice", retain_object, release_object<slice_object>);
        neos_py_type sRangeType = make_type("python.range", "range", retain_object, release_object<range_object>);
        neos_py_type sIteratorType = make_type("python.iterator", "iterator", retain_object, release_object<iterator_object>);
        neos_py_type sFunctionType = make_type("python.builtin_function_or_method", "builtin_function_or_method", retain_object, release_object<function_object>);

        singleton sNone{ &sNoneType.base };
        singleton sNotImplemented{ &sNotImplementedType.base };
        singleton sEllipsis{ &sEllipsisType.base };
        singleton sTrue{ &sBoolType.base };
        singleton sFalse{ &sBoolType.base };

        int kind_of(object* aObject)
        {
            if (aObject == nullptr)
                return NEOS_PY_OTHER;
            auto const type = aObject->type;
            if (type == &sNoneType.base) return NEOS_PY_NONE;
            if (type == &sNotImplementedType.base) return NEOS_PY_NOT_IMPLEMENTED;
            if (type == &sEllipsisType.base) return NEOS_PY_ELLIPSIS;
            if (type == &sBoolType.base) return NEOS_PY_BOOL;
            if (type == &sIntType.base) return NEOS_PY_INT;
            if (type == &sFloatType.base) return NEOS_PY_FLOAT;
            if (type == &sComplexType.base) return NEOS_PY_COMPLEX;
            if (type == &sStrType.base) return NEOS_PY_STR;
            if (type == &sListType.base) return NEOS_PY_LIST;
            if (type == &sTupleType.base) return NEOS_PY_TUPLE;
            if (type == &sDictType.base) return NEOS_PY_DICT;
            if (type == &sSliceType.base) return NEOS_PY_SLICE;
            if (type == &sRangeType.base) return NEOS_PY_RANGE;
            if (type == &sIteratorType.base) return NEOS_PY_ITERATOR;
            if (type == &sFunctionType.base) return NEOS_PY_FUNCTION;
            return NEOS_PY_OTHER;
        }

        template <typename T> T* as(object* aObject) { return reinterpret_cast<T*>(aObject); }

        object* none() { return &sNone.base; }
        object* not_implemented() { return &sNotImplemented.base; }
        object* make_bool(bool aValue) { return aValue ? &sTrue.base : &sFalse.base; }
        object* make_int(long long aValue) { return &(new int_object{ { { &sIntType.base } }, aValue })->h.base; }
        object* make_float(double aValue) { return &(new float_object{ { { &sFloatType.base } }, aValue })->h.base; }
        object* make_complex(double aReal, double aImag) { return &(new complex_object{ { { &sComplexType.base } }, aReal, aImag })->h.base; }
        object* make_str(std::string aValue) { return &(new str_object{ { { &sStrType.base } }, std::move(aValue) })->h.base; }
        object* make_list(std::vector<object*> aItems = {}) { return &(new list_object{ { { &sListType.base } }, std::move(aItems) })->h.base; }
        object* make_tuple(std::vector<object*> aItems = {}) { return &(new tuple_object{ { { &sTupleType.base } }, std::move(aItems) })->h.base; }
        object* make_dict() { return &(new dict_object{ { { &sDictType.base } }, {} })->h.base; }
        object* make_slice(object* aStart, object* aStop, object* aStep)
        {
            auto const value = [](object* aPart) { auto const result = aPart != nullptr ? aPart : none(); retain(result); return result; };
            return &(new slice_object{ { { &sSliceType.base } }, value(aStart), value(aStop), value(aStep) })->h.base;
        }
        object* make_range(long long aStart, long long aStop, long long aStep) { return &(new range_object{ { { &sRangeType.base } }, aStart, aStop, aStep })->h.base; }
        object* make_iterator(object* aSequence) { retain(aSequence); return &(new iterator_object{ { { &sIteratorType.base } }, aSequence })->h.base; }
        object* make_function(std::string aName, native_function aFunction, void* aData, object* aSelf = nullptr)
        {
            retain(aSelf);
            return &(new function_object{ { { &sFunctionType.base } }, std::move(aName), aFunction, aData, aSelf })->h.base;
        }

        // --- errors

        std::nullptr_t error(std::string const& aType, std::string const& aMessage)
        {
            python_runtime::raise(aType, aMessage);
            return nullptr;
        }

        char const* type_name(object* aObject)
        {
            if (aObject == nullptr)
                return "NULL";
            if (auto const type = python_type(aObject))
                return type->name;
            return aObject->type->name;
        }

        // --- conversions

        bool is_int_like(object* aObject)
        {
            auto const k = kind_of(aObject);
            return k == NEOS_PY_INT || k == NEOS_PY_BOOL;
        }

        long long int_value(object* aObject)
        {
            return kind_of(aObject) == NEOS_PY_BOOL ? (aObject == &sTrue.base ? 1 : 0) : as<int_object>(aObject)->value;
        }

        bool to_index(object* aObject, long long& aResult)
        {
            if (is_int_like(aObject))
            {
                aResult = int_value(aObject);
                return true;
            }
            if (auto const type = python_type(aObject); type != nullptr && type->index != nullptr)
                return type->index(context(), aObject, &aResult) == 0;
            error("TypeError", std::string{ "'" } + type_name(aObject) + "' object cannot be interpreted as an integer");
            return false;
        }

        bool to_double(object* aObject, double& aResult)
        {
            switch (kind_of(aObject))
            {
            case NEOS_PY_BOOL:
            case NEOS_PY_INT:
                aResult = static_cast<double>(int_value(aObject));
                return true;
            case NEOS_PY_FLOAT:
                aResult = as<float_object>(aObject)->value;
                return true;
            default:
                if (auto const type = python_type(aObject); type != nullptr && type->to_float != nullptr)
                    return type->to_float(context(), aObject, &aResult) == 0;
                error("TypeError", std::string{ "must be real number, not " } + type_name(aObject));
                return false;
            }
        }

        // --- repr

        std::string complex_part(double aValue)
        {
            // (as repr shows a float, without a trailing ".0")
            auto text = python_float(aValue);
            if (text.size() > 2u && text.ends_with(".0"))
                text.resize(text.size() - 2u);
            return text;
        }

        std::string str_repr(std::string_view aText)
        {
            bool const single = aText.find('\'') == std::string_view::npos || aText.find('"') != std::string_view::npos;
            char const quote = single ? '\'' : '"';
            std::string result(1u, quote);
            for (auto c : aText)
            {
                auto const b = static_cast<unsigned char>(c);
                if (c == quote || c == '\\')
                    (result += '\\') += c;
                else if (c == '\n')
                    result += "\\n";
                else if (c == '\r')
                    result += "\\r";
                else if (c == '\t')
                    result += "\\t";
                else if (b < 0x20u || b == 0x7Fu)
                {
                    char buffer[8];
                    std::snprintf(buffer, sizeof(buffer), "\\x%02x", b);
                    result += buffer;
                }
                else
                    result += c;
            }
            result += quote;
            return result;
        }

        bool repr_of(object* aObject, std::string& aResult);

        bool join_repr(std::vector<object*> const& aItems, std::string& aResult)
        {
            for (std::size_t i = 0u; i < aItems.size(); ++i)
            {
                if (i != 0u)
                    aResult += ", ";
                std::string item;
                if (!repr_of(aItems[i], item))
                    return false;
                aResult += item;
            }
            return true;
        }

        bool repr_of(object* aObject, std::string& aResult)
        {
            switch (kind_of(aObject))
            {
            case NEOS_PY_NONE: aResult = "None"; return true;
            case NEOS_PY_NOT_IMPLEMENTED: aResult = "NotImplemented"; return true;
            case NEOS_PY_ELLIPSIS: aResult = "Ellipsis"; return true;
            case NEOS_PY_BOOL: aResult = aObject == &sTrue.base ? "True" : "False"; return true;
            case NEOS_PY_INT: aResult = std::to_string(as<int_object>(aObject)->value); return true;
            case NEOS_PY_FLOAT: aResult = python_float(as<float_object>(aObject)->value); return true;
            case NEOS_PY_COMPLEX:
                {
                    auto const& c = *as<complex_object>(aObject);
                    if (c.real == 0.0 && !std::signbit(c.real))
                        aResult = complex_part(c.imag) + "j";
                    else
                        aResult = "(" + complex_part(c.real) + (c.imag < 0.0 || (c.imag == 0.0 && std::signbit(c.imag)) || std::isnan(c.imag) && std::signbit(c.imag) ? "" : "+") +
                            complex_part(c.imag) + "j)";
                }
                return true;
            case NEOS_PY_STR: aResult = str_repr(as<str_object>(aObject)->value); return true;
            case NEOS_PY_LIST:
                aResult = "[";
                if (!join_repr(as<list_object>(aObject)->items, aResult))
                    return false;
                aResult += "]";
                return true;
            case NEOS_PY_TUPLE:
                {
                    auto const& items = as<tuple_object>(aObject)->items;
                    aResult = "(";
                    if (!join_repr(items, aResult))
                        return false;
                    aResult += items.size() == 1u ? ",)" : ")";
                }
                return true;
            case NEOS_PY_DICT:
                aResult = "{";
                for (auto const& item : as<dict_object>(aObject)->items)
                {
                    if (aResult.size() > 1u)
                        aResult += ", ";
                    std::string key, value;
                    if (!repr_of(item.first, key) || !repr_of(item.second, value))
                        return false;
                    aResult += key + ": " + value;
                }
                aResult += "}";
                return true;
            case NEOS_PY_SLICE:
                {
                    auto const& s = *as<slice_object>(aObject);
                    std::string a, b, c;
                    if (!repr_of(s.start, a) || !repr_of(s.stop, b) || !repr_of(s.step, c))
                        return false;
                    aResult = "slice(" + a + ", " + b + ", " + c + ")";
                }
                return true;
            case NEOS_PY_RANGE:
                {
                    auto const& r = *as<range_object>(aObject);
                    aResult = "range(" + std::to_string(r.start) + ", " + std::to_string(r.stop) + (r.step != 1 ? ", " + std::to_string(r.step) : std::string{}) + ")";
                }
                return true;
            case NEOS_PY_ITERATOR:
                aResult = "<iterator object>";
                return true;
            case NEOS_PY_FUNCTION:
                aResult = "<built-in function " + as<function_object>(aObject)->name + ">";
                return true;
            default:
                {
                    auto const type = python_type(aObject);
                    if (type == nullptr || type->repr == nullptr)
                    {
                        aResult = std::string{ "<" } + type_name(aObject) + " object>";
                        return true;
                    }
                    ref const text{ type->repr(context(), aObject) };
                    if (!text)
                        return false;
                    if (kind_of(text.get()) != NEOS_PY_STR)
                    {
                        error("TypeError", std::string{ "__repr__ returned non-string (type " } + type_name(text.get()) + ")");
                        return false;
                    }
                    aResult = as<str_object>(text.get())->value;
                }
                return true;
            }
        }

        bool str_of(object* aObject, std::string& aResult)
        {
            if (kind_of(aObject) == NEOS_PY_STR)
            {
                aResult = as<str_object>(aObject)->value;
                return true;
            }
            if (auto const type = python_type(aObject); type != nullptr && type->str != nullptr && kind_of(aObject) == NEOS_PY_OTHER)
            {
                ref const text{ type->str(context(), aObject) };
                if (!text)
                    return false;
                if (kind_of(text.get()) != NEOS_PY_STR)
                {
                    error("TypeError", std::string{ "__str__ returned non-string (type " } + type_name(text.get()) + ")");
                    return false;
                }
                aResult = as<str_object>(text.get())->value;
                return true;
            }
            return repr_of(aObject, aResult);
        }

        // --- operations

        object* binary(int aOperation, object* aLhs, object* aRhs, bool aInplace);
        object* compare(int aOperation, object* aLhs, object* aRhs);
        int truth(object* aObject);

        char const* operator_symbol(int aOperation, bool aInplace = false)
        {
            static char const* const sSymbols[] = { "+", "-", "*", "/", "//", "%", "**", "@", "<<", ">>", "&", "|", "^" };
            static char const* const sInplaceSymbols[] = { "+=", "-=", "*=", "/=", "//=", "%=", "**=", "@=", "<<=", ">>=", "&=", "|=", "^=" };
            return aOperation >= 0 && aOperation <= NEOS_PY_XOR ? (aInplace ? sInplaceSymbols[aOperation] : sSymbols[aOperation]) : "?";
        }

        char const* comparison_symbol(int aOperation)
        {
            static char const* const sSymbols[] = { "<", "<=", "==", "!=", ">", ">=" };
            return aOperation >= 0 && aOperation <= NEOS_PY_GE ? sSymbols[aOperation] : "?";
        }

        // Python's floor division and modulo of floats (CPython's float_divmod)
        std::pair<double, double> float_divmod(double aVx, double aWx)
        {
            double mod = std::fmod(aVx, aWx);
            double div = (aVx - mod) / aWx;
            if (mod != 0.0)
            {
                if ((aWx < 0.0) != (mod < 0.0))
                {
                    mod += aWx;
                    div -= 1.0;
                }
            }
            else
                mod = std::copysign(0.0, aWx);
            double floordiv;
            if (div != 0.0)
            {
                floordiv = std::floor(div);
                if (div - floordiv > 0.5)
                    floordiv += 1.0;
            }
            else
                floordiv = std::copysign(0.0, aVx / aWx);
            return { floordiv, mod };
        }

        object* int_overflow()
        {
            return error("OverflowError", "integer result too large (neos Python ints are 64-bit)");
        }

        // (portable overflow checking: true if the result overflows)
        bool add_overflow(long long a, long long b, long long* r)
        {
            if ((b > 0 && a > std::numeric_limits<long long>::max() - b) || (b < 0 && a < std::numeric_limits<long long>::min() - b))
                return true;
            *r = a + b;
            return false;
        }
        bool subtract_overflow(long long a, long long b, long long* r)
        {
            if ((b < 0 && a > std::numeric_limits<long long>::max() + b) || (b > 0 && a < std::numeric_limits<long long>::min() + b))
                return true;
            *r = a - b;
            return false;
        }
        bool multiply_overflow(long long a, long long b, long long* r)
        {
            if (a == 0 || b == 0)
            {
                *r = 0;
                return false;
            }
            auto const max = std::numeric_limits<long long>::max();
            auto const min = std::numeric_limits<long long>::min();
            if ((a == -1 && b == min) || (b == -1 && a == min))
                return true;
            if (a > 0 ? (b > 0 ? a > max / b : b < min / a) : (b > 0 ? a < min / b : a < max / b))
                return true;
            *r = a * b;
            return false;
        }

        object* int_binary(int aOperation, long long a, long long b)
        {
            long long r = 0;
            switch (aOperation)
            {
            case NEOS_PY_ADD:
                if (add_overflow(a, b, &r)) return int_overflow();
                return make_int(r);
            case NEOS_PY_SUBTRACT:
                if (subtract_overflow(a, b, &r)) return int_overflow();
                return make_int(r);
            case NEOS_PY_MULTIPLY:
                if (multiply_overflow(a, b, &r)) return int_overflow();
                return make_int(r);
            case NEOS_PY_TRUE_DIVIDE:
                if (b == 0) return error("ZeroDivisionError", "division by zero");
                return make_float(static_cast<double>(a) / static_cast<double>(b));
            case NEOS_PY_FLOOR_DIVIDE:
            case NEOS_PY_REMAINDER:
                {
                    if (b == 0)
                        return error("ZeroDivisionError", aOperation == NEOS_PY_FLOOR_DIVIDE ? "integer division or modulo by zero" : "integer modulo by zero");
                    if (a == std::numeric_limits<long long>::min() && b == -1)
                        return aOperation == NEOS_PY_FLOOR_DIVIDE ? int_overflow() : make_int(0);
                    long long q = a / b;
                    long long m = a % b;
                    if (m != 0 && ((m < 0) != (b < 0)))
                    {
                        m += b;
                        --q;
                    }
                    return make_int(aOperation == NEOS_PY_FLOOR_DIVIDE ? q : m);
                }
            case NEOS_PY_POWER:
                {
                    if (b < 0)
                    {
                        if (a == 0)
                            return error("ZeroDivisionError", "0.0 cannot be raised to a negative power");
                        return make_float(std::pow(static_cast<double>(a), static_cast<double>(b)));
                    }
                    long long result = 1;
                    long long base = a;
                    for (long long e = b; e > 0; e >>= 1)
                    {
                        if ((e & 1) != 0 && multiply_overflow(result, base, &result))
                            return int_overflow();
                        if (e > 1 && multiply_overflow(base, base, &base))
                            return int_overflow();
                    }
                    return make_int(result);
                }
            case NEOS_PY_LSHIFT:
                if (b < 0) return error("ValueError", "negative shift count");
                if (a == 0) return make_int(0);
                if (b >= 63 || (a > 0 ? a > (std::numeric_limits<long long>::max() >> b) : a < (std::numeric_limits<long long>::min() >> b)))
                    return int_overflow();
                return make_int(static_cast<long long>(static_cast<unsigned long long>(a) << b));
            case NEOS_PY_RSHIFT:
                if (b < 0) return error("ValueError", "negative shift count");
                return make_int(b >= 63 ? (a < 0 ? -1 : 0) : a >> b);
            case NEOS_PY_AND: return make_int(a & b);
            case NEOS_PY_OR: return make_int(a | b);
            case NEOS_PY_XOR: return make_int(a ^ b);
            default:
                return not_implemented();
            }
        }

        // complex arithmetic as CPython does it (Objects/complexobject.c)
        struct cpx { double real; double imag; };
        cpx c_prod(cpx a, cpx b) { return { a.real * b.real - a.imag * b.imag, a.real * b.imag + a.imag * b.real }; }
        bool c_quot(cpx a, cpx b, cpx& r)
        {
            double const abs_breal = b.real < 0 ? -b.real : b.real;
            double const abs_bimag = b.imag < 0 ? -b.imag : b.imag;
            if (abs_breal >= abs_bimag)
            {
                if (abs_breal == 0.0)
                    return false;
                double const ratio = b.imag / b.real;
                double const denom = b.real + b.imag * ratio;
                r = { (a.real + a.imag * ratio) / denom, (a.imag - a.real * ratio) / denom };
            }
            else if (abs_bimag >= abs_breal)
            {
                double const ratio = b.real / b.imag;
                double const denom = b.real * ratio + b.imag;
                r = { (a.real * ratio + a.imag) / denom, (a.imag * ratio - a.real) / denom };
            }
            else
                r = { std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::quiet_NaN() };
            return true;
        }
        cpx c_powu(cpx x, long n)
        {
            cpx r{ 1.0, 0.0 }, p = x;
            for (long mask = 1; mask > 0 && n >= mask; mask <<= 1)
            {
                if (n & mask)
                    r = c_prod(r, p);
                p = c_prod(p, p);
            }
            return r;
        }
        bool c_pow(cpx a, cpx b, cpx& r)
        {
            if (b.real == 0.0 && b.imag == 0.0)
                r = { 1.0, 0.0 };
            else if (a.real == 0.0 && a.imag == 0.0)
            {
                if (b.imag != 0.0 || b.real < 0.0)
                    return false;
                r = { 0.0, 0.0 };
            }
            else if (b.imag == 0.0 && b.real == std::floor(b.real) && std::fabs(b.real) <= 100.0)
            {
                // (c_powi)
                auto const n = static_cast<long>(b.real);
                if (n > 0)
                    r = c_powu(a, n);
                else
                    c_quot({ 1.0, 0.0 }, c_powu(a, -n), r);
            }
            else
            {
                double const vabs = std::hypot(a.real, a.imag);
                double len = std::pow(vabs, b.real);
                double const at = std::atan2(a.imag, a.real);
                double phase = at * b.real;
                if (b.imag != 0.0)
                {
                    len /= std::exp(at * b.imag);
                    phase += b.imag * std::log(vabs);
                }
                r = { len * std::cos(phase), len * std::sin(phase) };
            }
            return true;
        }

        object* complex_binary(int aOperation, std::complex<double> a, std::complex<double> b)
        {
            cpx const x{ a.real(), a.imag() }, y{ b.real(), b.imag() };
            switch (aOperation)
            {
            case NEOS_PY_ADD: return make_complex(x.real + y.real, x.imag + y.imag);
            case NEOS_PY_SUBTRACT: return make_complex(x.real - y.real, x.imag - y.imag);
            case NEOS_PY_MULTIPLY: { auto const r = c_prod(x, y); return make_complex(r.real, r.imag); }
            case NEOS_PY_TRUE_DIVIDE:
                {
                    cpx r{};
                    if (!c_quot(x, y, r))
                        return error("ZeroDivisionError", "division by zero");
                    return make_complex(r.real, r.imag);
                }
            case NEOS_PY_POWER:
                {
                    cpx r{};
                    if (!c_pow(x, y, r))
                        return error("ZeroDivisionError", "zero to a negative or complex power");
                    return make_complex(r.real, r.imag);
                }
            default:
                return not_implemented();
            }
        }

        object* float_binary(int aOperation, double a, double b)
        {
            switch (aOperation)
            {
            case NEOS_PY_ADD: return make_float(a + b);
            case NEOS_PY_SUBTRACT: return make_float(a - b);
            case NEOS_PY_MULTIPLY: return make_float(a * b);
            case NEOS_PY_TRUE_DIVIDE:
                if (b == 0.0) return error("ZeroDivisionError", "float division by zero");
                return make_float(a / b);
            case NEOS_PY_FLOOR_DIVIDE:
                if (b == 0.0) return error("ZeroDivisionError", "float floor division by zero");
                return make_float(float_divmod(a, b).first);
            case NEOS_PY_REMAINDER:
                if (b == 0.0) return error("ZeroDivisionError", "float modulo by zero");
                return make_float(float_divmod(a, b).second);
            case NEOS_PY_POWER:
                if (a == 0.0 && b < 0.0)
                    return error("ZeroDivisionError", "0.0 cannot be raised to a negative power");
                if (a < 0.0 && std::isfinite(b) && std::floor(b) != b)
                    return complex_binary(NEOS_PY_POWER, { a, 0.0 }, { b, 0.0 });
                return make_float(std::pow(a, b));
            default:
                return not_implemented();
            }
        }

        bool sequence_repeat(std::vector<object*> const& aItems, object* aCount, std::vector<object*>& aResult)
        {
            long long count = 0;
            if (!is_int_like(aCount))
                return false;
            count = std::max(0ll, int_value(aCount));
            for (long long i = 0; i < count; ++i)
                for (auto item : aItems)
                {
                    retain(item);
                    aResult.push_back(item);
                }
            return true;
        }

        // the core types' binary operators (NotImplemented if not supported)
        object* core_binary(int aOperation, object* aLhs, object* aRhs)
        {
            auto const lk = kind_of(aLhs);
            auto const rk = kind_of(aRhs);
            bool const lnum = (lk == NEOS_PY_BOOL || lk == NEOS_PY_INT || lk == NEOS_PY_FLOAT || lk == NEOS_PY_COMPLEX);
            bool const rnum = (rk == NEOS_PY_BOOL || rk == NEOS_PY_INT || rk == NEOS_PY_FLOAT || rk == NEOS_PY_COMPLEX);
            if (lnum && rnum)
            {
                if (lk == NEOS_PY_COMPLEX || rk == NEOS_PY_COMPLEX)
                {
                    auto const value = [](object* aObject, int aKind) -> std::complex<double>
                    {
                        if (aKind == NEOS_PY_COMPLEX)
                            return { as<complex_object>(aObject)->real, as<complex_object>(aObject)->imag };
                        if (aKind == NEOS_PY_FLOAT)
                            return { as<float_object>(aObject)->value, 0.0 };
                        return { static_cast<double>(int_value(aObject)), 0.0 };
                    };
                    if (aOperation == NEOS_PY_FLOOR_DIVIDE || aOperation == NEOS_PY_REMAINDER)
                        return error("TypeError", std::string{ "unsupported operand type(s) for " } + operator_symbol(aOperation) + ": '" + type_name(aLhs) + "' and '" + type_name(aRhs) + "'");
                    return complex_binary(aOperation, value(aLhs, lk), value(aRhs, rk));
                }
                if (lk == NEOS_PY_FLOAT || rk == NEOS_PY_FLOAT)
                {
                    if (aOperation >= NEOS_PY_MATMUL)
                        return not_implemented();
                    double a = 0.0, b = 0.0;
                    to_double(aLhs, a);
                    to_double(aRhs, b);
                    return float_binary(aOperation, a, b);
                }
                if (lk == NEOS_PY_BOOL && rk == NEOS_PY_BOOL && (aOperation == NEOS_PY_AND || aOperation == NEOS_PY_OR || aOperation == NEOS_PY_XOR))
                {
                    bool const a = aLhs == &sTrue.base, b = aRhs == &sTrue.base;
                    return make_bool(aOperation == NEOS_PY_AND ? a && b : aOperation == NEOS_PY_OR ? a || b : a != b);
                }
                return int_binary(aOperation, int_value(aLhs), int_value(aRhs));
            }
            if (aOperation == NEOS_PY_ADD)
            {
                if (lk == NEOS_PY_STR && rk == NEOS_PY_STR)
                    return make_str(as<str_object>(aLhs)->value + as<str_object>(aRhs)->value);
                if (lk == NEOS_PY_STR && rk != NEOS_PY_OTHER)
                    return error("TypeError", std::string{ "can only concatenate str (not \"" } + type_name(aRhs) + "\") to str");
                if ((lk == NEOS_PY_LIST && rk == NEOS_PY_LIST) || (lk == NEOS_PY_TUPLE && rk == NEOS_PY_TUPLE))
                {
                    auto const& a = lk == NEOS_PY_LIST ? as<list_object>(aLhs)->items : as<tuple_object>(aLhs)->items;
                    auto const& b = lk == NEOS_PY_LIST ? as<list_object>(aRhs)->items : as<tuple_object>(aRhs)->items;
                    std::vector<object*> items;
                    for (auto i : a) { retain(i); items.push_back(i); }
                    for (auto i : b) { retain(i); items.push_back(i); }
                    return lk == NEOS_PY_LIST ? make_list(std::move(items)) : make_tuple(std::move(items));
                }
                if (lk == NEOS_PY_LIST && rk != NEOS_PY_OTHER)
                    return error("TypeError", std::string{ "can only concatenate list (not \"" } + type_name(aRhs) + "\") to list");
                if (lk == NEOS_PY_TUPLE && rk != NEOS_PY_OTHER)
                    return error("TypeError", std::string{ "can only concatenate tuple (not \"" } + type_name(aRhs) + "\") to tuple");
            }
            if (aOperation == NEOS_PY_MULTIPLY)
            {
                auto const repeat = [&](object* aSequence, int aKind, object* aCount) -> object*
                {
                    if (aKind == NEOS_PY_STR && is_int_like(aCount))
                    {
                        std::string result;
                        for (long long i = 0; i < int_value(aCount); ++i)
                            result += as<str_object>(aSequence)->value;
                        return make_str(std::move(result));
                    }
                    std::vector<object*> items;
                    if (aKind == NEOS_PY_LIST && sequence_repeat(as<list_object>(aSequence)->items, aCount, items))
                        return make_list(std::move(items));
                    if (aKind == NEOS_PY_TUPLE && sequence_repeat(as<tuple_object>(aSequence)->items, aCount, items))
                        return make_tuple(std::move(items));
                    return nullptr;
                };
                if (auto const result = repeat(aLhs, lk, aRhs))
                    return result;
                if (auto const result = repeat(aRhs, rk, aLhs))
                    return result;
            }
            return not_implemented();
        }

        object* binary(int aOperation, object* aLhs, object* aRhs, bool aInplace)
        {
            auto const lt = python_type(aLhs);
            auto const rt = python_type(aRhs);
            if (aInplace && lt != nullptr && lt->binary != nullptr && kind_of(aLhs) == NEOS_PY_OTHER)
            {
                ref result{ lt->binary(context(), aOperation, aLhs, aRhs, 1) };
                if (!result || kind_of(result.get()) != NEOS_PY_NOT_IMPLEMENTED)
                    return result.release_ownership();
            }
            if (kind_of(aLhs) != NEOS_PY_OTHER)
            {
                ref result{ core_binary(aOperation, aLhs, aRhs) };
                if (!result || kind_of(result.get()) != NEOS_PY_NOT_IMPLEMENTED)
                    return result.release_ownership();
            }
            else if (lt != nullptr && lt->binary != nullptr)
            {
                ref result{ lt->binary(context(), aOperation, aLhs, aRhs, 0) };
                if (!result || kind_of(result.get()) != NEOS_PY_NOT_IMPLEMENTED)
                    return result.release_ownership();
            }
            if (kind_of(aRhs) == NEOS_PY_OTHER && rt != nullptr && rt->binary != nullptr && rt != lt)
            {
                ref result{ rt->binary(context(), aOperation, aLhs, aRhs, 0) };
                if (!result || kind_of(result.get()) != NEOS_PY_NOT_IMPLEMENTED)
                    return result.release_ownership();
            }
            return error("TypeError", std::string{ "unsupported operand type(s) for " } + operator_symbol(aOperation, aInplace) + ": '" + type_name(aLhs) + "' and '" + type_name(aRhs) + "'");
        }

        object* unary(int aOperation, object* aObject)
        {
            switch (kind_of(aObject))
            {
            case NEOS_PY_BOOL:
            case NEOS_PY_INT:
                {
                    auto const v = int_value(aObject);
                    switch (aOperation)
                    {
                    case NEOS_PY_NEGATIVE: if (v == std::numeric_limits<long long>::min()) return int_overflow(); return make_int(-v);
                    case NEOS_PY_POSITIVE: return make_int(v);
                    case NEOS_PY_INVERT: return make_int(~v);
                    case NEOS_PY_ABSOLUTE: if (v == std::numeric_limits<long long>::min()) return int_overflow(); return make_int(v < 0 ? -v : v);
                    }
                }
                break;
            case NEOS_PY_FLOAT:
                {
                    auto const v = as<float_object>(aObject)->value;
                    switch (aOperation)
                    {
                    case NEOS_PY_NEGATIVE: return make_float(-v);
                    case NEOS_PY_POSITIVE: return make_float(v);
                    case NEOS_PY_ABSOLUTE: return make_float(std::fabs(v));
                    }
                }
                break;
            case NEOS_PY_COMPLEX:
                {
                    auto const& c = *as<complex_object>(aObject);
                    switch (aOperation)
                    {
                    case NEOS_PY_NEGATIVE: return make_complex(-c.real, -c.imag);
                    case NEOS_PY_POSITIVE: return make_complex(c.real, c.imag);
                    case NEOS_PY_ABSOLUTE: return make_float(std::hypot(c.real, c.imag));
                    }
                }
                break;
            case NEOS_PY_OTHER:
                if (auto const type = python_type(aObject); type != nullptr && type->unary != nullptr)
                    return type->unary(context(), aOperation, aObject);
                break;
            default:
                break;
            }
            if (aOperation == NEOS_PY_ABSOLUTE)
                return error("TypeError", std::string{ "bad operand type for abs(): '" } + type_name(aObject) + "'");
            static char const* const sSymbols[] = { "-", "+", "~", "abs()" };
            return error("TypeError", std::string{ "bad operand type for unary " } + sSymbols[std::clamp(aOperation, 0, 3)] + ": '" + type_name(aObject) + "'");
        }

        // three way comparison of core objects: -1, 0, 1; 2 if unordered (NaN); 3 if not comparable
        int core_order(object* aLhs, object* aRhs, int& aError);

        int sequence_order(std::vector<object*> const& a, std::vector<object*> const& b, int& aError)
        {
            for (std::size_t i = 0u; i < std::min(a.size(), b.size()); ++i)
            {
                ref const equal{ compare(NEOS_PY_EQ, a[i], b[i]) };
                if (!equal) { aError = 1; return 0; }
                auto const t = truth(equal.get());
                if (t < 0) { aError = 1; return 0; }
                if (t == 0)
                {
                    ref const less{ compare(NEOS_PY_LT, a[i], b[i]) };
                    if (!less) { aError = 1; return 0; }
                    auto const l = truth(less.get());
                    if (l < 0) { aError = 1; return 0; }
                    return l != 0 ? -1 : 1;
                }
            }
            return a.size() < b.size() ? -1 : a.size() > b.size() ? 1 : 0;
        }

        int core_order(object* aLhs, object* aRhs, int& aError)
        {
            auto const lk = kind_of(aLhs);
            auto const rk = kind_of(aRhs);
            bool const lnum = (lk == NEOS_PY_BOOL || lk == NEOS_PY_INT || lk == NEOS_PY_FLOAT);
            bool const rnum = (rk == NEOS_PY_BOOL || rk == NEOS_PY_INT || rk == NEOS_PY_FLOAT);
            if (lnum && rnum)
            {
                if (lk != NEOS_PY_FLOAT && rk != NEOS_PY_FLOAT)
                {
                    auto const a = int_value(aLhs), b = int_value(aRhs);
                    return a < b ? -1 : a > b ? 1 : 0;
                }
                if (lk == NEOS_PY_FLOAT && rk == NEOS_PY_FLOAT)
                {
                    auto const a = as<float_object>(aLhs)->value, b = as<float_object>(aRhs)->value;
                    return std::isnan(a) || std::isnan(b) ? 2 : a < b ? -1 : a > b ? 1 : 0;
                }
                // (an int and a float: exactly)
                bool const floatLeft = (lk == NEOS_PY_FLOAT);
                double const f = floatLeft ? as<float_object>(aLhs)->value : as<float_object>(aRhs)->value;
                long long const i = floatLeft ? int_value(aRhs) : int_value(aLhs);
                int order;
                if (std::isnan(f))
                    return 2;
                if (f >= 9223372036854775808.0)
                    order = 1;
                else if (f < -9223372036854775808.0)
                    order = -1;
                else
                {
                    auto const t = std::trunc(f);
                    auto const ti = static_cast<long long>(t);
                    order = ti < i ? -1 : ti > i ? 1 : (f > t ? 1 : f < t ? -1 : 0);
                }
                return floatLeft ? order : -order;
            }
            if (lk == NEOS_PY_COMPLEX && (rnum || rk == NEOS_PY_COMPLEX) || rk == NEOS_PY_COMPLEX && lnum)
            {
                double ar = 0.0, ai = 0.0, br = 0.0, bi = 0.0;
                if (lk == NEOS_PY_COMPLEX) { ar = as<complex_object>(aLhs)->real; ai = as<complex_object>(aLhs)->imag; } else to_double(aLhs, ar);
                if (rk == NEOS_PY_COMPLEX) { br = as<complex_object>(aRhs)->real; bi = as<complex_object>(aRhs)->imag; } else to_double(aRhs, br);
                return ar == br && ai == bi ? 0 : 4; // (4: not equal, unorderable)
            }
            if (lk == NEOS_PY_STR && rk == NEOS_PY_STR)
            {
                auto const c = as<str_object>(aLhs)->value.compare(as<str_object>(aRhs)->value);
                return c < 0 ? -1 : c > 0 ? 1 : 0;
            }
            if (lk == NEOS_PY_LIST && rk == NEOS_PY_LIST)
                return sequence_order(as<list_object>(aLhs)->items, as<list_object>(aRhs)->items, aError);
            if (lk == NEOS_PY_TUPLE && rk == NEOS_PY_TUPLE)
                return sequence_order(as<tuple_object>(aLhs)->items, as<tuple_object>(aRhs)->items, aError);
            if (lk == NEOS_PY_NONE && rk == NEOS_PY_NONE)
                return 0;
            if (lk == NEOS_PY_DICT && rk == NEOS_PY_DICT)
            {
                auto const& a = as<dict_object>(aLhs)->items;
                auto const& b = as<dict_object>(aRhs)->items;
                if (a.size() != b.size())
                    return 4;
                for (auto const& item : a)
                {
                    bool found = false;
                    for (auto const& other : b)
                    {
                        if (core_order(item.first, other.first, aError) == 0)
                        {
                            ref const equal{ compare(NEOS_PY_EQ, item.second, other.second) };
                            if (!equal) { aError = 1; return 0; }
                            if (truth(equal.get()) != 1)
                                return 4;
                            found = true;
                            break;
                        }
                    }
                    if (!found)
                        return 4;
                }
                return 0;
            }
            if (lk == NEOS_PY_RANGE && rk == NEOS_PY_RANGE)
            {
                auto const& a = *as<range_object>(aLhs);
                auto const& b = *as<range_object>(aRhs);
                return a.start == b.start && a.stop == b.stop && a.step == b.step ? 0 : 4;
            }
            return aLhs == aRhs ? 0 : 3;
        }

        object* compare(int aOperation, object* aLhs, object* aRhs)
        {
            auto const lt = python_type(aLhs);
            auto const rt = python_type(aRhs);
            if (kind_of(aLhs) == NEOS_PY_OTHER && lt != nullptr && lt->compare != nullptr)
            {
                ref result{ lt->compare(context(), aOperation, aLhs, aRhs) };
                if (!result || kind_of(result.get()) != NEOS_PY_NOT_IMPLEMENTED)
                    return result.release_ownership();
            }
            if (kind_of(aRhs) == NEOS_PY_OTHER && rt != nullptr && rt->compare != nullptr && rt != lt)
            {
                ref result{ rt->compare(context(), aOperation, aLhs, aRhs) };
                if (!result || kind_of(result.get()) != NEOS_PY_NOT_IMPLEMENTED)
                    return result.release_ownership();
            }
            int failure = 0;
            auto const order = core_order(aLhs, aRhs, failure);
            if (failure != 0)
                return nullptr;
            switch (aOperation)
            {
            case NEOS_PY_EQ: return make_bool(order == 0);
            case NEOS_PY_NE: return make_bool(order != 0);
            default:
                if (order == 3 || order == 4)
                    return error("TypeError", std::string{ "'" } + comparison_symbol(aOperation) + "' not supported between instances of '" + type_name(aLhs) + "' and '" + type_name(aRhs) + "'");
                if (order == 2)
                    return make_bool(false);
                switch (aOperation)
                {
                case NEOS_PY_LT: return make_bool(order < 0);
                case NEOS_PY_LE: return make_bool(order <= 0);
                case NEOS_PY_GT: return make_bool(order > 0);
                default: return make_bool(order >= 0);
                }
            }
        }

        long long length(object* aObject)
        {
            switch (kind_of(aObject))
            {
            case NEOS_PY_STR:
                {
                    long long n = 0;
                    for (auto c : as<str_object>(aObject)->value)
                        if ((static_cast<unsigned char>(c) & 0xC0u) != 0x80u)
                            ++n;
                    return n;
                }
            case NEOS_PY_LIST: return static_cast<long long>(as<list_object>(aObject)->items.size());
            case NEOS_PY_TUPLE: return static_cast<long long>(as<tuple_object>(aObject)->items.size());
            case NEOS_PY_DICT: return static_cast<long long>(as<dict_object>(aObject)->items.size());
            case NEOS_PY_RANGE:
                {
                    auto const& r = *as<range_object>(aObject);
                    if (r.step > 0)
                        return r.stop > r.start ? (r.stop - r.start + r.step - 1) / r.step : 0;
                    return r.stop < r.start ? (r.start - r.stop - r.step - 1) / -r.step : 0;
                }
            case NEOS_PY_OTHER:
                if (auto const type = python_type(aObject); type != nullptr && type->length != nullptr)
                    return type->length(context(), aObject);
                break;
            default:
                break;
            }
            error("TypeError", std::string{ "object of type '" } + type_name(aObject) + "' has no len()");
            return -1;
        }

        int truth(object* aObject)
        {
            switch (kind_of(aObject))
            {
            case NEOS_PY_NONE: return 0;
            case NEOS_PY_BOOL: return aObject == &sTrue.base ? 1 : 0;
            case NEOS_PY_INT: return as<int_object>(aObject)->value != 0 ? 1 : 0;
            case NEOS_PY_FLOAT: return as<float_object>(aObject)->value != 0.0 ? 1 : 0;
            case NEOS_PY_COMPLEX: return as<complex_object>(aObject)->real != 0.0 || as<complex_object>(aObject)->imag != 0.0 ? 1 : 0;
            case NEOS_PY_STR: return as<str_object>(aObject)->value.empty() ? 0 : 1;
            case NEOS_PY_LIST: case NEOS_PY_TUPLE: case NEOS_PY_DICT: case NEOS_PY_RANGE:
                return length(aObject) != 0 ? 1 : 0;
            case NEOS_PY_OTHER:
                if (auto const type = python_type(aObject))
                {
                    if (type->truth != nullptr)
                        return type->truth(context(), aObject);
                    if (type->length != nullptr)
                    {
                        auto const n = type->length(context(), aObject);
                        return n < 0 ? -1 : n != 0 ? 1 : 0;
                    }
                }
                return 1;
            default:
                return 1;
            }
        }

        // a sequence index (negative: from the end) in range
        bool normalize_index(object* aKey, std::size_t aSize, std::size_t& aIndex, char const* aWhat)
        {
            long long i = 0;
            if (!to_index(aKey, i))
                return false;
            if (i < 0)
                i += static_cast<long long>(aSize);
            if (i < 0 || i >= static_cast<long long>(aSize))
            {
                error("IndexError", std::string{ aWhat } + " index out of range");
                return false;
            }
            aIndex = static_cast<std::size_t>(i);
            return true;
        }

        // Python's slice.indices()
        bool slice_indices(object* aSlice, long long aLength, long long& aStart, long long& aStop, long long& aStep)
        {
            auto const& s = *as<slice_object>(aSlice);
            aStep = 1;
            if (kind_of(s.step) != NEOS_PY_NONE && !to_index(s.step, aStep))
                return false;
            if (aStep == 0)
            {
                error("ValueError", "slice step cannot be zero");
                return false;
            }
            auto const bound = [&](object* aPart, long long aDefault, long long& aResult)
            {
                if (kind_of(aPart) == NEOS_PY_NONE)
                {
                    aResult = aDefault;
                    return true;
                }
                if (!to_index(aPart, aResult))
                    return false;
                if (aResult < 0)
                {
                    aResult += aLength;
                    if (aResult < 0)
                        aResult = aStep < 0 ? -1 : 0;
                }
                else if (aResult >= aLength)
                    aResult = aStep < 0 ? aLength - 1 : aLength;
                return true;
            };
            return bound(s.start, aStep < 0 ? aLength - 1 : 0, aStart) && bound(s.stop, aStep < 0 ? -1 : aLength, aStop);
        }

        std::vector<std::size_t> slice_positions(long long aStart, long long aStop, long long aStep)
        {
            std::vector<std::size_t> positions;
            if (aStep > 0)
                for (long long i = aStart; i < aStop; i += aStep)
                    positions.push_back(static_cast<std::size_t>(i));
            else
                for (long long i = aStart; i > aStop; i += aStep)
                    positions.push_back(static_cast<std::size_t>(i));
            return positions;
        }

        std::vector<std::string_view> code_points(std::string const& aText)
        {
            std::vector<std::string_view> result;
            for (std::size_t i = 0u; i < aText.size();)
            {
                auto const b = static_cast<unsigned char>(aText[i]);
                std::size_t const n = b < 0x80u ? 1u : b < 0xE0u ? 2u : b < 0xF0u ? 3u : 4u;
                result.emplace_back(aText.data() + i, std::min(n, aText.size() - i));
                i += n;
            }
            return result;
        }

        object* getitem(object* aObject, object* aKey)
        {
            auto const k = kind_of(aObject);
            if (k == NEOS_PY_LIST || k == NEOS_PY_TUPLE)
            {
                auto const& items = k == NEOS_PY_LIST ? as<list_object>(aObject)->items : as<tuple_object>(aObject)->items;
                if (kind_of(aKey) == NEOS_PY_SLICE)
                {
                    long long start = 0, stop = 0, step = 0;
                    if (!slice_indices(aKey, static_cast<long long>(items.size()), start, stop, step))
                        return nullptr;
                    std::vector<object*> result;
                    for (auto i : slice_positions(start, stop, step))
                    {
                        retain(items[i]);
                        result.push_back(items[i]);
                    }
                    return k == NEOS_PY_LIST ? make_list(std::move(result)) : make_tuple(std::move(result));
                }
                std::size_t i = 0u;
                if (!normalize_index(aKey, items.size(), i, k == NEOS_PY_LIST ? "list" : "tuple"))
                    return nullptr;
                retain(items[i]);
                return items[i];
            }
            if (k == NEOS_PY_STR)
            {
                auto const points = code_points(as<str_object>(aObject)->value);
                if (kind_of(aKey) == NEOS_PY_SLICE)
                {
                    long long start = 0, stop = 0, step = 0;
                    if (!slice_indices(aKey, static_cast<long long>(points.size()), start, stop, step))
                        return nullptr;
                    std::string result;
                    for (auto i : slice_positions(start, stop, step))
                        result += points[i];
                    return make_str(std::move(result));
                }
                std::size_t i = 0u;
                if (!normalize_index(aKey, points.size(), i, "string"))
                    return nullptr;
                return make_str(std::string{ points[i] });
            }
            if (k == NEOS_PY_RANGE)
            {
                auto const& r = *as<range_object>(aObject);
                std::size_t i = 0u;
                if (!normalize_index(aKey, static_cast<std::size_t>(length(aObject)), i, "range object"))
                    return nullptr;
                return make_int(r.start + static_cast<long long>(i) * r.step);
            }
            if (k == NEOS_PY_DICT)
            {
                for (auto const& item : as<dict_object>(aObject)->items)
                {
                    int failure = 0;
                    if (core_order(item.first, aKey, failure) == 0)
                    {
                        retain(item.second);
                        return item.second;
                    }
                }
                std::string key;
                repr_of(aKey, key);
                return error("KeyError", key);
            }
            if (k == NEOS_PY_OTHER)
                if (auto const type = python_type(aObject); type != nullptr && type->getitem != nullptr)
                    return type->getitem(context(), aObject, aKey);
            return error("TypeError", std::string{ "'" } + type_name(aObject) + "' object is not subscriptable");
        }

        int dict_set(object* aDict, object* aKey, object* aValue)
        {
            auto& items = as<dict_object>(aDict)->items;
            for (auto& item : items)
            {
                int failure = 0;
                if (core_order(item.first, aKey, failure) == 0)
                {
                    retain(aValue);
                    release(item.second);
                    item.second = aValue;
                    return 0;
                }
            }
            retain(aKey);
            retain(aValue);
            items.emplace_back(aKey, aValue);
            return 0;
        }

        object* iter(object* aObject);
        object* next(object* aIterator);

        bool collect(object* aIterable, std::vector<object*>& aItems)
        {
            ref const it{ iter(aIterable) };
            if (!it)
                return false;
            for (;;)
            {
                auto const item = next(it.get());
                if (item == nullptr)
                    return !failed();
                aItems.push_back(item);
            }
        }

        int setitem(object* aObject, object* aKey, object* aValue)
        {
            auto const k = kind_of(aObject);
            if (k == NEOS_PY_LIST)
            {
                auto& items = as<list_object>(aObject)->items;
                if (kind_of(aKey) == NEOS_PY_SLICE)
                {
                    long long start = 0, stop = 0, step = 0;
                    if (!slice_indices(aKey, static_cast<long long>(items.size()), start, stop, step))
                        return -1;
                    std::vector<object*> values;
                    if (aValue != nullptr && !collect(aValue, values))
                        return -1;
                    if (step == 1)
                    {
                        stop = std::max(stop, start);
                        for (auto i = start; i < stop; ++i)
                            release(items[static_cast<std::size_t>(i)]);
                        items.erase(items.begin() + start, items.begin() + stop);
                        items.insert(items.begin() + start, values.begin(), values.end());
                        return 0;
                    }
                    auto const positions = slice_positions(start, stop, step);
                    if (aValue == nullptr)
                    {
                        auto sorted = positions;
                        std::sort(sorted.rbegin(), sorted.rend());
                        for (auto i : sorted)
                        {
                            release(items[i]);
                            items.erase(items.begin() + static_cast<std::ptrdiff_t>(i));
                        }
                        return 0;
                    }
                    if (positions.size() != values.size())
                    {
                        for (auto v : values) release(v);
                        error("ValueError", "attempt to assign sequence of size " + std::to_string(values.size()) + " to extended slice of size " + std::to_string(positions.size()));
                        return -1;
                    }
                    for (std::size_t i = 0u; i < positions.size(); ++i)
                    {
                        release(items[positions[i]]);
                        items[positions[i]] = values[i];
                    }
                    return 0;
                }
                std::size_t i = 0u;
                if (!normalize_index(aKey, items.size(), i, "list assignment"))
                    return -1;
                if (aValue == nullptr)
                {
                    release(items[i]);
                    items.erase(items.begin() + static_cast<std::ptrdiff_t>(i));
                    return 0;
                }
                retain(aValue);
                release(items[i]);
                items[i] = aValue;
                return 0;
            }
            if (k == NEOS_PY_DICT)
            {
                if (aValue != nullptr)
                    return dict_set(aObject, aKey, aValue);
                auto& items = as<dict_object>(aObject)->items;
                for (auto i = items.begin(); i != items.end(); ++i)
                {
                    int failure = 0;
                    if (core_order(i->first, aKey, failure) == 0)
                    {
                        release(i->first);
                        release(i->second);
                        items.erase(i);
                        return 0;
                    }
                }
                std::string key;
                repr_of(aKey, key);
                error("KeyError", key);
                return -1;
            }
            if (k == NEOS_PY_OTHER)
                if (auto const type = python_type(aObject); type != nullptr && type->setitem != nullptr)
                    return type->setitem(context(), aObject, aKey, aValue);
            error("TypeError", std::string{ "'" } + type_name(aObject) + (aValue != nullptr ? "' object does not support item assignment" : "' object does not support item deletion"));
            return -1;
        }

        object* iter(object* aObject)
        {
            switch (kind_of(aObject))
            {
            case NEOS_PY_LIST: case NEOS_PY_TUPLE: case NEOS_PY_STR: case NEOS_PY_DICT: case NEOS_PY_RANGE:
                return make_iterator(aObject);
            case NEOS_PY_ITERATOR:
                retain(aObject);
                return aObject;
            case NEOS_PY_OTHER:
                if (auto const type = python_type(aObject))
                {
                    if (type->iter != nullptr)
                        return type->iter(context(), aObject);
                    if (type->getitem != nullptr && type->length != nullptr)
                        return make_iterator(aObject);
                    if (type->next != nullptr)
                    {
                        retain(aObject);
                        return aObject;
                    }
                }
                break;
            default:
                break;
            }
            return error("TypeError", std::string{ "'" } + type_name(aObject) + "' object is not iterable");
        }

        object* next(object* aIterator)
        {
            if (kind_of(aIterator) == NEOS_PY_ITERATOR)
            {
                auto& it = *as<iterator_object>(aIterator);
                auto const sequence = it.sequence;
                switch (kind_of(sequence))
                {
                case NEOS_PY_LIST:
                case NEOS_PY_TUPLE:
                    {
                        auto const& items = kind_of(sequence) == NEOS_PY_LIST ? as<list_object>(sequence)->items : as<tuple_object>(sequence)->items;
                        if (it.index >= items.size())
                            return nullptr;
                        retain(items[it.index]);
                        return items[it.index++];
                    }
                case NEOS_PY_STR:
                    {
                        auto const& text = as<str_object>(sequence)->value;
                        if (it.index >= text.size())
                            return nullptr;
                        auto const b = static_cast<unsigned char>(text[it.index]);
                        std::size_t const n = b < 0x80u ? 1u : b < 0xE0u ? 2u : b < 0xF0u ? 3u : 4u;
                        auto const result = make_str(text.substr(it.index, n));
                        it.index += n;
                        return result;
                    }
                case NEOS_PY_DICT:
                    {
                        auto const& items = as<dict_object>(sequence)->items;
                        if (it.index >= items.size())
                            return nullptr;
                        retain(items[it.index].first);
                        return items[it.index++].first;
                    }
                case NEOS_PY_RANGE:
                    {
                        if (static_cast<long long>(it.index) >= length(sequence))
                            return nullptr;
                        auto const& r = *as<range_object>(sequence);
                        return make_int(r.start + static_cast<long long>(it.index++) * r.step);
                    }
                default:
                    {
                        // (a sequence with __len__ and __getitem__)
                        auto const n = length(sequence);
                        if (n < 0)
                            return nullptr;
                        if (static_cast<long long>(it.index) >= n)
                            return nullptr;
                        ref const index{ make_int(static_cast<long long>(it.index++)) };
                        return getitem(sequence, index.get());
                    }
                }
            }
            if (auto const type = python_type(aIterator); type != nullptr && type->next != nullptr)
                return type->next(context(), aIterator);
            return error("TypeError", std::string{ "'" } + type_name(aIterator) + "' object is not an iterator");
        }

        int contains(object* aContainer, object* aItem)
        {
            switch (kind_of(aContainer))
            {
            case NEOS_PY_STR:
                if (kind_of(aItem) != NEOS_PY_STR)
                {
                    error("TypeError", std::string{ "'in <string>' requires string as left operand, not " } + type_name(aItem));
                    return -1;
                }
                return as<str_object>(aContainer)->value.find(as<str_object>(aItem)->value) != std::string::npos ? 1 : 0;
            case NEOS_PY_DICT:
                for (auto const& item : as<dict_object>(aContainer)->items)
                {
                    int failure = 0;
                    if (core_order(item.first, aItem, failure) == 0)
                        return 1;
                }
                return 0;
            case NEOS_PY_OTHER:
                if (auto const type = python_type(aContainer); type != nullptr && type->contains != nullptr)
                    return type->contains(context(), aContainer, aItem);
                [[fallthrough]];
            default:
                {
                    auto const k = kind_of(aContainer);
                    auto const type = python_type(aContainer);
                    bool const iterable = k == NEOS_PY_LIST || k == NEOS_PY_TUPLE || k == NEOS_PY_RANGE || k == NEOS_PY_ITERATOR ||
                        (k == NEOS_PY_OTHER && type != nullptr && (type->iter != nullptr || type->next != nullptr || (type->getitem != nullptr && type->length != nullptr)));
                    if (!iterable)
                    {
                        error("TypeError", std::string{ "argument of type '" } + type_name(aContainer) + "' is not iterable");
                        return -1;
                    }
                    ref const it{ iter(aContainer) };
                    if (!it)
                        return -1;
                    for (;;)
                    {
                        ref const item{ next(it.get()) };
                        if (!item)
                            return failed() ? -1 : 0;
                        if (item.get() == aItem)
                            return 1;
                        ref const equal{ compare(NEOS_PY_EQ, item.get(), aItem) };
                        if (!equal)
                            return -1;
                        auto const t = truth(equal.get());
                        if (t != 0)
                            return t;
                    }
                }
            }
        }

        // --- methods of the core types

        struct method_data
        {
            object* (*function)(object* aSelf, std::vector<object*> const& aArgs, object* aKwargs);
        };

        object* call_method(neos_py_context*, void* aData, neos_py_object* aArgs, neos_py_object* aKwargs);

        std::vector<object*> const& tuple_items(object* aTuple) { return as<tuple_object>(aTuple)->items; }

        bool arity(std::vector<object*> const& aArgs, std::size_t aMin, std::size_t aMax, char const* aName)
        {
            if (aArgs.size() >= aMin && aArgs.size() <= aMax)
                return true;
            if (aMin == aMax)
                error("TypeError", std::string{ aName } + "() takes exactly " + std::to_string(aMin) + " argument" + (aMin == 1u ? "" : "s") + " (" + std::to_string(aArgs.size()) + " given)");
            else
                error("TypeError", std::string{ aName } + "() expected at most " + std::to_string(aMax) + " argument" + (aMax == 1u ? "" : "s") + ", got " + std::to_string(aArgs.size()));
            return false;
        }

        object* list_append(object* aSelf, std::vector<object*> const& aArgs, object*)
        {
            if (!arity(aArgs, 1u, 1u, "list.append"))
                return nullptr;
            retain(aArgs[0]);
            as<list_object>(aSelf)->items.push_back(aArgs[0]);
            return none();
        }

        object* list_extend(object* aSelf, std::vector<object*> const& aArgs, object*)
        {
            if (!arity(aArgs, 1u, 1u, "list.extend"))
                return nullptr;
            std::vector<object*> items;
            if (!collect(aArgs[0], items))
                return nullptr;
            auto& list = as<list_object>(aSelf)->items;
            list.insert(list.end(), items.begin(), items.end());
            return none();
        }

        object* list_pop(object* aSelf, std::vector<object*> const& aArgs, object*)
        {
            if (!arity(aArgs, 0u, 1u, "pop"))
                return nullptr;
            auto& items = as<list_object>(aSelf)->items;
            if (items.empty())
                return error("IndexError", "pop from empty list");
            std::size_t i = items.size() - 1u;
            if (!aArgs.empty())
            {
                long long index = 0;
                if (!to_index(aArgs[0], index))
                    return nullptr;
                if (index < 0)
                    index += static_cast<long long>(items.size());
                if (index < 0 || index >= static_cast<long long>(items.size()))
                    return error("IndexError", "pop index out of range");
                i = static_cast<std::size_t>(index);
            }
            auto const result = items[i];
            items.erase(items.begin() + static_cast<std::ptrdiff_t>(i));
            return result;
        }

        object* list_insert(object* aSelf, std::vector<object*> const& aArgs, object*)
        {
            if (!arity(aArgs, 2u, 2u, "insert"))
                return nullptr;
            long long index = 0;
            if (!to_index(aArgs[0], index))
                return nullptr;
            auto& items = as<list_object>(aSelf)->items;
            auto const n = static_cast<long long>(items.size());
            if (index < 0)
                index = std::max(0ll, index + n);
            index = std::min(index, n);
            retain(aArgs[1]);
            items.insert(items.begin() + index, aArgs[1]);
            return none();
        }

        object* sequence_index(object* aSelf, std::vector<object*> const& aArgs, object*)
        {
            if (!arity(aArgs, 1u, 1u, "index"))
                return nullptr;
            auto const& items = kind_of(aSelf) == NEOS_PY_LIST ? as<list_object>(aSelf)->items : as<tuple_object>(aSelf)->items;
            for (std::size_t i = 0u; i < items.size(); ++i)
            {
                ref const equal{ compare(NEOS_PY_EQ, items[i], aArgs[0]) };
                if (!equal)
                    return nullptr;
                if (truth(equal.get()) == 1)
                    return make_int(static_cast<long long>(i));
            }
            std::string value;
            repr_of(aArgs[0], value);
            return error("ValueError", kind_of(aSelf) == NEOS_PY_LIST ? value + " is not in list" : "tuple.index(x): x not in tuple");
        }

        object* sequence_count(object* aSelf, std::vector<object*> const& aArgs, object*)
        {
            if (!arity(aArgs, 1u, 1u, "count"))
                return nullptr;
            auto const& items = kind_of(aSelf) == NEOS_PY_LIST ? as<list_object>(aSelf)->items : as<tuple_object>(aSelf)->items;
            long long n = 0;
            for (auto item : items)
            {
                ref const equal{ compare(NEOS_PY_EQ, item, aArgs[0]) };
                if (!equal)
                    return nullptr;
                n += truth(equal.get()) == 1 ? 1 : 0;
            }
            return make_int(n);
        }

        object* list_reverse(object* aSelf, std::vector<object*> const& aArgs, object*)
        {
            if (!arity(aArgs, 0u, 0u, "reverse"))
                return nullptr;
            auto& items = as<list_object>(aSelf)->items;
            std::reverse(items.begin(), items.end());
            return none();
        }

        object* list_copy(object* aSelf, std::vector<object*> const& aArgs, object*)
        {
            if (!arity(aArgs, 0u, 0u, "copy"))
                return nullptr;
            auto items = as<list_object>(aSelf)->items;
            for (auto i : items)
                retain(i);
            return make_list(std::move(items));
        }

        object* list_sort(object* aSelf, std::vector<object*> const& aArgs, object* aKwargs)
        {
            if (!arity(aArgs, 0u, 0u, "sort"))
                return nullptr;
            bool reverse = false;
            if (aKwargs != nullptr)
                for (auto const& item : as<dict_object>(aKwargs)->items)
                {
                    if (as<str_object>(item.first)->value == "reverse")
                        reverse = truth(item.second) == 1;
                    else
                        return error("TypeError", "'" + as<str_object>(item.first)->value + "' is an invalid keyword argument for sort()");
                }
            auto& items = as<list_object>(aSelf)->items;
            bool failure = false;
            std::stable_sort(items.begin(), items.end(), [&](object* a, object* b)
            {
                if (failure)
                    return false;
                ref const less{ compare(NEOS_PY_LT, reverse ? a : b, reverse ? b : a) };
                if (!less) { failure = true; return false; }
                auto const t = truth(less.get());
                if (t < 0) { failure = true; return false; }
                return t == 1;
            });
            if (reverse)
            {
                // (stable for equal elements: sort ascending by "b < a" then the order is as Python's)
            }
            return failure ? nullptr : none();
        }

        object* dict_get_method(object* aSelf, std::vector<object*> const& aArgs, object*)
        {
            if (!arity(aArgs, 1u, 2u, "get"))
                return nullptr;
            for (auto const& item : as<dict_object>(aSelf)->items)
            {
                int failure = 0;
                if (core_order(item.first, aArgs[0], failure) == 0)
                {
                    retain(item.second);
                    return item.second;
                }
            }
            auto const result = aArgs.size() > 1u ? aArgs[1] : none();
            retain(result);
            return result;
        }

        object* dict_view(object* aSelf, int aWhich)
        {
            std::vector<object*> items;
            for (auto const& item : as<dict_object>(aSelf)->items)
            {
                if (aWhich == 0) { retain(item.first); items.push_back(item.first); }
                else if (aWhich == 1) { retain(item.second); items.push_back(item.second); }
                else
                {
                    retain(item.first);
                    retain(item.second);
                    items.push_back(make_tuple({ item.first, item.second }));
                }
            }
            return make_list(std::move(items));
        }

        object* dict_keys(object* aSelf, std::vector<object*> const& aArgs, object*) { return arity(aArgs, 0u, 0u, "keys") ? dict_view(aSelf, 0) : nullptr; }
        object* dict_values(object* aSelf, std::vector<object*> const& aArgs, object*) { return arity(aArgs, 0u, 0u, "values") ? dict_view(aSelf, 1) : nullptr; }
        object* dict_items(object* aSelf, std::vector<object*> const& aArgs, object*) { return arity(aArgs, 0u, 0u, "items") ? dict_view(aSelf, 2) : nullptr; }

        object* str_case(object* aSelf, std::vector<object*> const& aArgs, bool aUpper)
        {
            if (!arity(aArgs, 0u, 0u, aUpper ? "upper" : "lower"))
                return nullptr;
            auto text = as<str_object>(aSelf)->value;
            for (auto& c : text)
                c = static_cast<char>(aUpper ? std::toupper(static_cast<unsigned char>(c)) : std::tolower(static_cast<unsigned char>(c)));
            return make_str(std::move(text));
        }
        object* str_upper(object* aSelf, std::vector<object*> const& aArgs, object*) { return str_case(aSelf, aArgs, true); }
        object* str_lower(object* aSelf, std::vector<object*> const& aArgs, object*) { return str_case(aSelf, aArgs, false); }

        object* str_strip(object* aSelf, std::vector<object*> const& aArgs, object*)
        {
            if (!arity(aArgs, 0u, 1u, "strip"))
                return nullptr;
            std::string const whitespace = aArgs.empty() || kind_of(aArgs[0]) == NEOS_PY_NONE ? std::string{ " \t\n\r\f\v" } : as<str_object>(aArgs[0])->value;
            auto const& text = as<str_object>(aSelf)->value;
            auto const begin = text.find_first_not_of(whitespace);
            if (begin == std::string::npos)
                return make_str({});
            return make_str(text.substr(begin, text.find_last_not_of(whitespace) - begin + 1u));
        }

        object* str_split(object* aSelf, std::vector<object*> const& aArgs, object*)
        {
            if (!arity(aArgs, 0u, 1u, "split"))
                return nullptr;
            auto const& text = as<str_object>(aSelf)->value;
            std::vector<object*> parts;
            if (aArgs.empty() || kind_of(aArgs[0]) == NEOS_PY_NONE)
            {
                std::size_t i = 0u;
                for (;;)
                {
                    i = text.find_first_not_of(" \t\n\r\f\v", i);
                    if (i == std::string::npos)
                        break;
                    auto const end = text.find_first_of(" \t\n\r\f\v", i);
                    parts.push_back(make_str(text.substr(i, end == std::string::npos ? std::string::npos : end - i)));
                    if (end == std::string::npos)
                        break;
                    i = end;
                }
            }
            else
            {
                if (kind_of(aArgs[0]) != NEOS_PY_STR)
                    return error("TypeError", std::string{ "must be str or None, not " } + type_name(aArgs[0]));
                auto const& separator = as<str_object>(aArgs[0])->value;
                if (separator.empty())
                    return error("ValueError", "empty separator");
                std::size_t i = 0u;
                for (;;)
                {
                    auto const end = text.find(separator, i);
                    parts.push_back(make_str(text.substr(i, end == std::string::npos ? std::string::npos : end - i)));
                    if (end == std::string::npos)
                        break;
                    i = end + separator.size();
                }
            }
            return make_list(std::move(parts));
        }

        object* str_join(object* aSelf, std::vector<object*> const& aArgs, object*)
        {
            if (!arity(aArgs, 1u, 1u, "join"))
                return nullptr;
            std::vector<object*> items;
            if (!collect(aArgs[0], items))
                return nullptr;
            std::string result;
            for (std::size_t i = 0u; i < items.size(); ++i)
            {
                if (kind_of(items[i]) != NEOS_PY_STR)
                {
                    auto const message = "sequence item " + std::to_string(i) + ": expected str instance, " + type_name(items[i]) + " found";
                    for (auto item : items) release(item);
                    return error("TypeError", message);
                }
                if (i != 0u)
                    result += as<str_object>(aSelf)->value;
                result += as<str_object>(items[i])->value;
            }
            for (auto item : items) release(item);
            return make_str(std::move(result));
        }

        object* str_replace(object* aSelf, std::vector<object*> const& aArgs, object*)
        {
            if (!arity(aArgs, 2u, 2u, "replace"))
                return nullptr;
            if (kind_of(aArgs[0]) != NEOS_PY_STR || kind_of(aArgs[1]) != NEOS_PY_STR)
                return error("TypeError", "replace() argument must be str");
            auto text = as<str_object>(aSelf)->value;
            auto const& from = as<str_object>(aArgs[0])->value;
            auto const& to = as<str_object>(aArgs[1])->value;
            if (from.empty())
                return make_str(std::move(text));
            std::string result;
            std::size_t i = 0u;
            for (auto j = text.find(from); j != std::string::npos; j = text.find(from, i))
            {
                result += text.substr(i, j - i) + to;
                i = j + from.size();
            }
            result += text.substr(i);
            return make_str(std::move(result));
        }

        object* str_affix(object* aSelf, std::vector<object*> const& aArgs, bool aStart)
        {
            if (!arity(aArgs, 1u, 1u, aStart ? "startswith" : "endswith"))
                return nullptr;
            if (kind_of(aArgs[0]) != NEOS_PY_STR)
                return error("TypeError", std::string{ aStart ? "startswith" : "endswith" } + " first arg must be str or a tuple of str, not " + type_name(aArgs[0]));
            auto const& text = as<str_object>(aSelf)->value;
            auto const& affix = as<str_object>(aArgs[0])->value;
            return make_bool(aStart ? text.starts_with(affix) : text.ends_with(affix));
        }
        object* str_startswith(object* aSelf, std::vector<object*> const& aArgs, object*) { return str_affix(aSelf, aArgs, true); }
        object* str_endswith(object* aSelf, std::vector<object*> const& aArgs, object*) { return str_affix(aSelf, aArgs, false); }

        object* str_find(object* aSelf, std::vector<object*> const& aArgs, object*)
        {
            if (!arity(aArgs, 1u, 1u, "find"))
                return nullptr;
            if (kind_of(aArgs[0]) != NEOS_PY_STR)
                return error("TypeError", std::string{ "must be str, not " } + type_name(aArgs[0]));
            auto const& text = as<str_object>(aSelf)->value;
            auto const i = text.find(as<str_object>(aArgs[0])->value);
            if (i == std::string::npos)
                return make_int(-1);
            long long n = 0;
            for (std::size_t j = 0u; j < i; ++j)
                if ((static_cast<unsigned char>(text[j]) & 0xC0u) != 0x80u)
                    ++n;
            return make_int(n);
        }

        struct method_entry
        {
            int kind;
            char const* name;
            method_data data;
        };

        method_entry const sMethods[] =
        {
            { NEOS_PY_LIST, "append", { list_append } },
            { NEOS_PY_LIST, "extend", { list_extend } },
            { NEOS_PY_LIST, "pop", { list_pop } },
            { NEOS_PY_LIST, "insert", { list_insert } },
            { NEOS_PY_LIST, "index", { sequence_index } },
            { NEOS_PY_LIST, "count", { sequence_count } },
            { NEOS_PY_LIST, "reverse", { list_reverse } },
            { NEOS_PY_LIST, "copy", { list_copy } },
            { NEOS_PY_LIST, "sort", { list_sort } },
            { NEOS_PY_TUPLE, "index", { sequence_index } },
            { NEOS_PY_TUPLE, "count", { sequence_count } },
            { NEOS_PY_DICT, "get", { dict_get_method } },
            { NEOS_PY_DICT, "keys", { dict_keys } },
            { NEOS_PY_DICT, "values", { dict_values } },
            { NEOS_PY_DICT, "items", { dict_items } },
            { NEOS_PY_STR, "upper", { str_upper } },
            { NEOS_PY_STR, "lower", { str_lower } },
            { NEOS_PY_STR, "strip", { str_strip } },
            { NEOS_PY_STR, "split", { str_split } },
            { NEOS_PY_STR, "join", { str_join } },
            { NEOS_PY_STR, "replace", { str_replace } },
            { NEOS_PY_STR, "startswith", { str_startswith } },
            { NEOS_PY_STR, "endswith", { str_endswith } },
            { NEOS_PY_STR, "find", { str_find } }
        };

        object* call_method(neos_py_context*, void* aData, neos_py_object* aArgs, neos_py_object* aKwargs)
        {
            // (a bound method: the function object's self is the first argument)
            auto const& args = tuple_items(aArgs);
            auto const* data = static_cast<method_entry const*>(aData);
            if (args.empty())
                return error("TypeError", "unbound method");
            std::vector<object*> rest{ args.begin() + 1, args.end() };
            if (aKwargs != nullptr && as<dict_object>(aKwargs)->items.size() != 0u && data->data.function != list_sort)
                return error("TypeError", std::string{ data->name } + "() takes no keyword arguments");
            return data->data.function(args[0], rest, aKwargs);
        }

        object* getattr(object* aObject, char const* aName)
        {
            auto const k = kind_of(aObject);
            if (k == NEOS_PY_OTHER)
            {
                if (auto const type = python_type(aObject); type != nullptr && type->getattr != nullptr)
                    return type->getattr(context(), aObject, aName);
            }
            else
            {
                for (auto const& method : sMethods)
                    if (method.kind == k && std::strcmp(method.name, aName) == 0)
                        return make_function(method.name, call_method, const_cast<method_entry*>(&method), aObject);
                if (k == NEOS_PY_COMPLEX)
                {
                    if (std::strcmp(aName, "real") == 0) return make_float(as<complex_object>(aObject)->real);
                    if (std::strcmp(aName, "imag") == 0) return make_float(as<complex_object>(aObject)->imag);
                }
                if (k == NEOS_PY_SLICE)
                {
                    auto const& s = *as<slice_object>(aObject);
                    object* part = std::strcmp(aName, "start") == 0 ? s.start : std::strcmp(aName, "stop") == 0 ? s.stop : std::strcmp(aName, "step") == 0 ? s.step : nullptr;
                    if (part != nullptr) { retain(part); return part; }
                }
                if ((k == NEOS_PY_INT || k == NEOS_PY_BOOL || k == NEOS_PY_FLOAT) && (std::strcmp(aName, "real") == 0 || std::strcmp(aName, "imag") == 0))
                {
                    if (std::strcmp(aName, "real") == 0) { retain(aObject); return aObject; }
                    return k == NEOS_PY_FLOAT ? make_float(0.0) : make_int(0);
                }
            }
            return error("AttributeError", std::string{ "'" } + type_name(aObject) + "' object has no attribute '" + aName + "'");
        }

        int setattr(object* aObject, char const* aName, object* aValue)
        {
            if (auto const type = python_type(aObject); type != nullptr && type->setattr != nullptr && kind_of(aObject) == NEOS_PY_OTHER)
                return type->setattr(context(), aObject, aName, aValue);
            error("AttributeError", std::string{ "'" } + type_name(aObject) + "' object has no attribute '" + aName + "'");
            return -1;
        }

        object* call(object* aCallable, object* aArgs, object* aKwargs)
        {
            ref args;
            if (aArgs == nullptr)
                args = ref{ make_tuple() };
            else
                args = ref::borrowed(aArgs);
            if (kind_of(aCallable) == NEOS_PY_FUNCTION)
            {
                auto const& f = *as<function_object>(aCallable);
                if (f.self != nullptr)
                {
                    // (a bound method)
                    std::vector<object*> items{ f.self };
                    retain(f.self);
                    for (auto item : tuple_items(args.get()))
                    {
                        retain(item);
                        items.push_back(item);
                    }
                    ref const bound{ make_tuple(std::move(items)) };
                    return f.function(context(), f.data, bound.get(), aKwargs);
                }
                return f.function(context(), f.data, args.get(), aKwargs);
            }
            if (auto const type = python_type(aCallable); type != nullptr && type->call != nullptr && kind_of(aCallable) == NEOS_PY_OTHER)
                return type->call(context(), aCallable, args.get(), aKwargs);
            return error("TypeError", std::string{ "'" } + type_name(aCallable) + "' object is not callable");
        }

        // --- the context

        void ctx_raise(neos_py_context*, char const* aType, char const* aMessage) { python_runtime::raise(aType, aMessage); }
        int ctx_failed(neos_py_context*) { return failed() ? 1 : 0; }
        object* ctx_none(neos_py_context*) { return none(); }
        object* ctx_not_implemented(neos_py_context*) { return not_implemented(); }
        object* ctx_ellipsis(neos_py_context*) { return &sEllipsis.base; }
        object* ctx_from_bool(neos_py_context*, int aValue) { return make_bool(aValue != 0); }
        object* ctx_from_int(neos_py_context*, long long aValue) { return make_int(aValue); }
        object* ctx_from_float(neos_py_context*, double aValue) { return make_float(aValue); }
        object* ctx_from_complex(neos_py_context*, double aReal, double aImag) { return make_complex(aReal, aImag); }
        object* ctx_from_str(neos_py_context*, char const* aText, std::size_t aLength) { return make_str(std::string{ aText, aLength }); }
        int ctx_kind(neos_py_context*, object* aObject) { return kind_of(aObject); }
        char const* ctx_type_name(neos_py_context*, object* aObject) { return type_name(aObject); }
        int ctx_as_int(neos_py_context*, object* aObject, long long* aResult) { return to_index(aObject, *aResult) ? 0 : -1; }
        int ctx_as_float(neos_py_context*, object* aObject, double* aResult) { return to_double(aObject, *aResult) ? 0 : -1; }
        int ctx_as_complex(neos_py_context*, object* aObject, double* aReal, double* aImag)
        {
            if (kind_of(aObject) == NEOS_PY_COMPLEX)
            {
                *aReal = as<complex_object>(aObject)->real;
                *aImag = as<complex_object>(aObject)->imag;
                return 0;
            }
            *aImag = 0.0;
            return to_double(aObject, *aReal) ? 0 : -1;
        }
        char const* ctx_as_str(neos_py_context*, object* aObject, std::size_t* aLength)
        {
            if (kind_of(aObject) != NEOS_PY_STR)
                return nullptr;
            if (aLength != nullptr)
                *aLength = as<str_object>(aObject)->value.size();
            return as<str_object>(aObject)->value.c_str();
        }
        object* ctx_tuple_new(neos_py_context*, std::size_t aSize)
        {
            std::vector<object*> items(aSize, none());
            return make_tuple(std::move(items));
        }
        void ctx_tuple_set(neos_py_context*, object* aTuple, std::size_t aIndex, object* aItem)
        {
            auto& items = as<tuple_object>(aTuple)->items;
            release(items[aIndex]);
            items[aIndex] = aItem;
        }
        object* ctx_list_new(neos_py_context*) { return make_list(); }
        int ctx_list_append(neos_py_context*, object* aList, object* aItem) { retain(aItem); as<list_object>(aList)->items.push_back(aItem); return 0; }
        std::size_t ctx_sequence_size(neos_py_context*, object* aObject)
        {
            auto const k = kind_of(aObject);
            return k == NEOS_PY_LIST ? as<list_object>(aObject)->items.size() : k == NEOS_PY_TUPLE ? as<tuple_object>(aObject)->items.size() : 0u;
        }
        object* ctx_sequence_item(neos_py_context*, object* aObject, std::size_t aIndex)
        {
            return kind_of(aObject) == NEOS_PY_LIST ? as<list_object>(aObject)->items[aIndex] : as<tuple_object>(aObject)->items[aIndex];
        }
        object* ctx_dict_new(neos_py_context*) { return make_dict(); }
        int ctx_dict_set(neos_py_context*, object* aDict, object* aKey, object* aValue) { return dict_set(aDict, aKey, aValue); }
        std::size_t ctx_dict_size(neos_py_context*, object* aDict) { return aDict == nullptr ? 0u : as<dict_object>(aDict)->items.size(); }
        void ctx_dict_item(neos_py_context*, object* aDict, std::size_t aIndex, object** aKey, object** aValue)
        {
            auto const& item = as<dict_object>(aDict)->items[aIndex];
            *aKey = item.first;
            *aValue = item.second;
        }
        object* ctx_dict_get(neos_py_context*, object* aDict, char const* aKey)
        {
            if (aDict == nullptr)
                return nullptr;
            for (auto const& item : as<dict_object>(aDict)->items)
                if (kind_of(item.first) == NEOS_PY_STR && as<str_object>(item.first)->value == aKey)
                    return item.second;
            return nullptr;
        }
        void ctx_slice_parts(neos_py_context*, object* aSlice, object** aStart, object** aStop, object** aStep)
        {
            auto const& s = *as<slice_object>(aSlice);
            *aStart = s.start;
            *aStop = s.stop;
            *aStep = s.step;
        }
        object* ctx_slice_new(neos_py_context*, object* aStart, object* aStop, object* aStep) { return make_slice(aStart, aStop, aStep); }
        object* ctx_function_new(neos_py_context*, char const* aName, native_function aFunction, void* aData) { return make_function(aName, aFunction, aData); }
        object* ctx_repr(neos_py_context*, object* aObject)
        {
            std::string text;
            return repr_of(aObject, text) ? make_str(std::move(text)) : nullptr;
        }
        object* ctx_str(neos_py_context*, object* aObject)
        {
            std::string text;
            return str_of(aObject, text) ? make_str(std::move(text)) : nullptr;
        }
        object* ctx_getattr(neos_py_context*, object* aObject, char const* aName) { return getattr(aObject, aName); }
        object* ctx_call(neos_py_context*, object* aCallable, object* aArgs, object* aKwargs) { return call(aCallable, aArgs, aKwargs); }
        object* ctx_getitem(neos_py_context*, object* aObject, object* aKey) { return getitem(aObject, aKey); }
        object* ctx_binary(neos_py_context*, int aOperation, object* aLhs, object* aRhs) { return binary(aOperation, aLhs, aRhs, false); }
        object* ctx_unary(neos_py_context*, int aOperation, object* aObject) { return unary(aOperation, aObject); }
        object* ctx_compare(neos_py_context*, int aOperation, object* aLhs, object* aRhs) { return compare(aOperation, aLhs, aRhs); }
        int ctx_truth(neos_py_context*, object* aObject) { return truth(aObject); }
        long long ctx_length(neos_py_context*, object* aObject) { return length(aObject); }
        object* ctx_iter(neos_py_context*, object* aObject) { return iter(aObject); }
        object* ctx_next(neos_py_context*, object* aIterator) { return next(aIterator); }

        object* ctx_method_new(neos_py_context*, char const* aName, native_function aFunction, void* aData, object* aSelf) { return make_function(aName, aFunction, aData, aSelf); }
        int ctx_setitem(neos_py_context*, object* aObject, object* aKey, object* aValue) { return setitem(aObject, aKey, aValue); }
        int ctx_setattr(neos_py_context*, object* aObject, char const* aName, object* aValue) { return setattr(aObject, aName, aValue); }
        object* ctx_type_new(neos_py_context*, char const* aName, char const* aQualifiedName, native_function aCall, void* aData, 
            int (*aInstancecheck)(neos_py_context*, void*, object*));

        char const* ctx_exception(neos_py_context*) { return failed() ? python_runtime::error_text() : nullptr; }
        void ctx_clear(neos_py_context*) { clear_error(); }

        neos_py_context sContext
        {
            NEOS_PY_PROTOCOL_VERSION,
            ctx_raise, ctx_failed,
            ctx_none, ctx_not_implemented, ctx_ellipsis, ctx_from_bool, ctx_from_int, ctx_from_float, ctx_from_complex, ctx_from_str,
            ctx_kind, ctx_type_name, ctx_as_int, ctx_as_float, ctx_as_complex, ctx_as_str,
            ctx_tuple_new, ctx_tuple_set, ctx_list_new, ctx_list_append, ctx_sequence_size, ctx_sequence_item,
            ctx_dict_new, ctx_dict_set, ctx_dict_size, ctx_dict_item, ctx_dict_get, ctx_slice_parts, ctx_slice_new, ctx_function_new,
            ctx_repr, ctx_str, ctx_getattr, ctx_call, ctx_getitem, ctx_binary, ctx_unary, ctx_compare, ctx_truth, ctx_length,
            ctx_iter, ctx_next, retain, release, ctx_method_new, ctx_setitem, ctx_setattr, ctx_type_new, ctx_exception, ctx_clear
        };

        neos_py_context* context() { return &sContext; }

        neos_rt_object_type const sContextType{ NEOS_PY_CONTEXT_TYPE_NAME, retain_static, release_static };
        neos_py_context_object sContextObject{ { &sContextType }, &sContext };

        // --- the functions scripts call (an exception stops the script: see python.cpp's plugin error)

        // (a result that is a new reference, or null if an exception was raised)
        object* checked(object* aResult)
        {
            if (aResult == nullptr && !failed())
                python_runtime::raise("SystemError", "error return without exception set");
            return aResult;
        }

        object* fn_context() { return &sContextObject.base; }
        object* fn_none() { return none(); }
        object* fn_ellipsis() { return &sEllipsis.base; }
        object* fn_from_bool(bool aValue) { return make_bool(aValue); }
        object* fn_from_int(std::int64_t aValue) { return make_int(aValue); }
        object* fn_from_float(double aValue) { return make_float(aValue); }
        object* fn_from_str(char const* aText) { return make_str(aText); }
        bool fn_truth(object* aObject) { return truth(aObject) == 1; }
        char const* fn_str(object* aObject)
        {
            std::string text;
            if (!str_of(aObject, text))
                return "";
            return result(std::move(text));
        }
        char const* fn_repr(object* aObject)
        {
            std::string text;
            if (!repr_of(aObject, text))
                return "";
            return result(std::move(text));
        }
        std::int64_t fn_to_int(object* aObject)
        {
            // int(x)
            switch (kind_of(aObject))
            {
            case NEOS_PY_FLOAT:
                {
                    auto const v = as<float_object>(aObject)->value;
                    if (std::isnan(v))
                        return (error("ValueError", "cannot convert float NaN to integer"), 0);
                    if (std::isinf(v))
                        return (error("OverflowError", "cannot convert float infinity to integer"), 0);
                    if (v >= 9223372036854775808.0 || v < -9223372036854775808.0)
                        return (int_overflow(), 0);
                    return static_cast<std::int64_t>(v);
                }
            case NEOS_PY_STR:
                {
                    auto text = as<str_object>(aObject)->value;
                    auto const begin = text.find_first_not_of(" \t\n\r\f\v");
                    auto const end = text.find_last_not_of(" \t\n\r\f\v");
                    std::string digits;
                    if (begin != std::string::npos)
                        for (auto i = begin; i <= end; ++i)
                            if (text[i] != '_')
                                digits += text[i];
                    long long value = 0;
                    auto const start = digits.data() + (!digits.empty() && digits[0] == '+' ? 1 : 0);
                    auto const converted = std::from_chars(start, digits.data() + digits.size(), value);
                    if (digits.empty() || converted.ec != std::errc{} || converted.ptr != digits.data() + digits.size())
                        return (error("ValueError", "invalid literal for int() with base 10: " + str_repr(text)), 0);
                    return value;
                }
            case NEOS_PY_BOOL:
            case NEOS_PY_INT:
                return int_value(aObject);
            case NEOS_PY_OTHER:
                if (auto const type = python_type(aObject); type != nullptr && (type->to_int != nullptr || type->index != nullptr))
                {
                    long long value = 0;
                    return (type->to_int != nullptr ? type->to_int(context(), aObject, &value) : type->index(context(), aObject, &value)) == 0 ? value : 0;
                }
                [[fallthrough]];
            default:
                error("TypeError", std::string{ "int() argument must be a string, a bytes-like object or a real number, not '" } + type_name(aObject) + "'");
                return 0;
            }
        }
        double fn_to_float(object* aObject)
        {
            if (kind_of(aObject) == NEOS_PY_STR)
            {
                auto const& text = as<str_object>(aObject)->value;
                char* end = nullptr;
                auto const value = std::strtod(text.c_str(), &end);
                std::string_view rest{ end };
                if (end == text.c_str() || rest.find_first_not_of(" \t\n\r\f\v") != std::string_view::npos)
                    return (error("ValueError", "could not convert string to float: " + str_repr(text)), 0.0);
                return value;
            }
            double value = 0.0;
            if (!to_double(aObject, value))
            {
                python_runtime::raise("TypeError", std::string{ "float() argument must be a string or a real number, not '" } + type_name(aObject) + "'");
                return 0.0;
            }
            return value;
        }
        object* fn_binary(std::int32_t aOperation, object* aLhs, object* aRhs) { return checked(binary(aOperation, aLhs, aRhs, false)); }
        object* fn_inplace(std::int32_t aOperation, object* aLhs, object* aRhs) { return checked(binary(aOperation, aLhs, aRhs, true)); }
        object* fn_unary(std::int32_t aOperation, object* aObject) { return checked(unary(aOperation, aObject)); }
        object* fn_compare(std::int32_t aOperation, object* aLhs, object* aRhs) { return checked(compare(aOperation, aLhs, aRhs)); }
        bool fn_is(object* aLhs, object* aRhs) { return aLhs == aRhs; }
        bool fn_contains(object* aContainer, object* aItem) { return contains(aContainer, aItem) == 1; }
        object* fn_getattr(object* aObject, char const* aName) { return checked(getattr(aObject, aName)); }
        void fn_setattr(object* aObject, char const* aName, object* aValue) { setattr(aObject, aName, aValue); }
        object* fn_call(object* aCallable, object* aArgs, object* aKwargs) { return checked(call(aCallable, aArgs, aKwargs)); }
        object* fn_getitem(object* aObject, object* aKey) { return checked(getitem(aObject, aKey)); }
        void fn_setitem(object* aObject, object* aKey, object* aValue) { setitem(aObject, aKey, aValue); }
        void fn_delitem(object* aObject, object* aKey) { setitem(aObject, aKey, nullptr); }
        std::int64_t fn_len(object* aObject) { return length(aObject); }
        object* fn_iter(object* aObject) { return checked(iter(aObject)); }
        object* fn_next(object* aIterator) { return next(aIterator); }
        object* fn_tuple(std::int64_t aSize) { return ctx_tuple_new(context(), static_cast<std::size_t>(aSize)); }
        void fn_tuple_set(object* aTuple, std::int64_t aIndex, object* aItem) { retain(aItem); ctx_tuple_set(context(), aTuple, static_cast<std::size_t>(aIndex), aItem); }
        object* fn_list() { return make_list(); }
        void fn_list_append(object* aList, object* aItem) { ctx_list_append(context(), aList, aItem); }
        object* fn_dict() { return make_dict(); }
        void fn_dict_set(object* aDict, object* aKey, object* aValue) { dict_set(aDict, aKey, aValue); }
        void fn_kwarg(object* aDict, char const* aName, object* aValue) { ref const key{ make_str(aName) }; dict_set(aDict, key.get(), aValue); }
        object* fn_slice(object* aStart, object* aStop, object* aStep) { return make_slice(aStart, aStop, aStep); }
        object* fn_range(std::int64_t aStart, std::int64_t aStop, std::int64_t aStep)
        {
            if (aStep == 0)
                return error("ValueError", "range() arg 3 must not be zero");
            return make_range(aStart, aStop, aStep);
        }
        object* fn_list_of(object* aIterable)
        {
            std::vector<object*> items;
            if (!collect(aIterable, items))
                return nullptr;
            return make_list(std::move(items));
        }
        object* fn_tuple_of(object* aIterable)
        {
            std::vector<object*> items;
            if (!collect(aIterable, items))
                return nullptr;
            return make_tuple(std::move(items));
        }

        // --- type objects (builtin classes: int, float, ...; the classes of other plugins' objects)

        struct type_object
        {
            header h;
            std::string name;
            std::string qualifiedName;
            native_function call = nullptr;     ///< (a class of another plugin: constructs its objects)
            void* data = nullptr;
            int (*instancecheck)(neos_py_context*, void*, object*) = nullptr;
        };
        object* call_builtin(neos_py_context*, void* aData, neos_py_object* aArgs, neos_py_object* aKwargs);
        neos_py_type make_type_type();
        neos_py_type sTypeType = make_type_type();

        object* make_type_object(std::string aName, std::string aQualifiedName)
        {
            return &(new type_object{ { { &sTypeType.base } }, std::move(aName), std::move(aQualifiedName) })->h.base;
        }

        bool is_type_object(object* aObject)
        {
            return aObject != nullptr && aObject->type == &sTypeType.base;
        }

        // the name of an object's class as Python shows it in "<class '...'>"
        std::string class_name(object* aObject)
        {
            if (is_type_object(aObject))
                return "type";
            if (kind_of(aObject) == NEOS_PY_OTHER)
                return aObject->type->name;
            return type_name(aObject);
        }

        struct builtin_entry
        {
            char const* name;
            object* (*function)(std::vector<object*> const& aArgs, object* aKwargs);
        };
        builtin_entry const* find_builtin(std::string_view aName);

        neos_py_type make_type_type()
        {
            auto type = make_type("python.type", "type", retain_object, release_object<type_object>);
            type.repr = [](neos_py_context*, neos_py_object* aSelf) -> neos_py_object*
            {
                return make_str("<class '" + as<type_object>(aSelf)->qualifiedName + "'>");
            };
            type.call = [](neos_py_context* aContext, neos_py_object* aSelf, neos_py_object* aArgs, neos_py_object* aKwargs) -> neos_py_object*
            {
                if (as<type_object>(aSelf)->call != nullptr)
                    return as<type_object>(aSelf)->call(aContext, as<type_object>(aSelf)->data, aArgs, aKwargs);
                auto const* builtin = find_builtin(as<type_object>(aSelf)->name);
                if (builtin == nullptr || as<type_object>(aSelf)->name != as<type_object>(aSelf)->qualifiedName)
                    return error("TypeError", "cannot create '" + as<type_object>(aSelf)->qualifiedName + "' instances");
                return call_builtin(aContext, const_cast<builtin_entry*>(builtin), aArgs, aKwargs);
            };
            type.compare = [](neos_py_context*, int aOperation, neos_py_object* aLhs, neos_py_object* aRhs) -> neos_py_object*
            {
                if ((aOperation != NEOS_PY_EQ && aOperation != NEOS_PY_NE) || !is_type_object(aLhs) || !is_type_object(aRhs))
                    return not_implemented();
                bool const equal = as<type_object>(aLhs)->qualifiedName == as<type_object>(aRhs)->qualifiedName;
                return make_bool(aOperation == NEOS_PY_EQ ? equal : !equal);
            };
            type.getattr = [](neos_py_context*, neos_py_object* aSelf, char const* aName) -> neos_py_object*
            {
                if (std::strcmp(aName, "__name__") == 0)
                    return make_str(as<type_object>(aSelf)->name);
                if (std::strcmp(aName, "__qualname__") == 0)
                    return make_str(as<type_object>(aSelf)->name);
                if (std::strcmp(aName, "__module__") == 0)
                {
                    auto const& q = as<type_object>(aSelf)->qualifiedName;
                    auto const dot = q.rfind('.');
                    return make_str(dot == std::string::npos ? std::string{ "builtins" } : q.substr(0u, dot));
                }
                return error("AttributeError", "type object '" + as<type_object>(aSelf)->name + "' has no attribute '" + aName + "'");
            };
            return type;
        }

        object* ctx_type_new(neos_py_context*, char const* aName, char const* aQualifiedName, native_function aCall, void* aData, 
            int (*aInstancecheck)(neos_py_context*, void*, object*))
        {
            auto const result = make_type_object(aName, aQualifiedName);
            as<type_object>(result)->call = aCall;
            as<type_object>(result)->data = aData;
            as<type_object>(result)->instancecheck = aInstancecheck;
            return result;
        }

        // type(x)
        object* class_of(object* aObject)
        {
            if (is_type_object(aObject))
                return make_type_object("type", "type");
            if (kind_of(aObject) == NEOS_PY_OTHER)
            {
                std::string qualified = aObject->type->name;
                auto const type = python_type(aObject);
                std::string const name = type != nullptr ? type->name : qualified;
                return make_type_object(name, qualified);
            }
            std::string const name = kind_of(aObject) == NEOS_PY_FUNCTION ? "builtin_function_or_method" : type_name(aObject);
            return make_type_object(name, name);
        }

        // --- builtins (the functions and classes of Python's builtins module)

        object* call_builtin(neos_py_context*, void* aData, neos_py_object* aArgs, neos_py_object* aKwargs)
        {
            auto const* entry = static_cast<builtin_entry const*>(aData);
            auto const& args = tuple_items(aArgs);
            return entry->function(args, aKwargs != nullptr && as<dict_object>(aKwargs)->items.empty() ? nullptr : aKwargs);
        }

        // keyword arguments (each must be one of aAllowed)
        bool keywords(object* aKwargs, std::initializer_list<std::pair<char const*, object**>> aAllowed, char const* aFunction)
        {
            if (aKwargs == nullptr)
                return true;
            for (auto const& item : as<dict_object>(aKwargs)->items)
            {
                auto const& key = as<str_object>(item.first)->value;
                bool found = false;
                for (auto const& allowed : aAllowed)
                    if (key == allowed.first)
                    {
                        *allowed.second = item.second;
                        found = true;
                    }
                if (!found)
                {
                    error("TypeError", "'" + key + "' is an invalid keyword argument for " + aFunction + "()");
                    return false;
                }
            }
            return true;
        }

        bool no_keywords(object* aKwargs, char const* aFunction)
        {
            if (aKwargs == nullptr || as<dict_object>(aKwargs)->items.empty())
                return true;
            error("TypeError", std::string{ aFunction } + "() takes no keyword arguments");
            return false;
        }

        void write_out(std::string const& aText)
        {
            std::fwrite(aText.data(), 1u, aText.size(), stdout);
            std::fflush(stdout);
        }

        object* b_print(std::vector<object*> const& aArgs, object* aKwargs)
        {
            object* sep = nullptr;
            object* end = nullptr;
            object* file = nullptr;
            object* flush = nullptr;
            if (!keywords(aKwargs, { { "sep", &sep }, { "end", &end }, { "file", &file }, { "flush", &flush } }, "print"))
                return nullptr;
            auto const text_of = [](object* aValue, char const* aDefault, char const* aWhat, std::string& aResult)
            {
                if (aValue == nullptr || kind_of(aValue) == NEOS_PY_NONE)
                    aResult = aDefault;
                else if (kind_of(aValue) == NEOS_PY_STR)
                    aResult = as<str_object>(aValue)->value;
                else
                {
                    error("TypeError", std::string{ aWhat } + " must be None or a string, not " + type_name(aValue));
                    return false;
                }
                return true;
            };
            std::string separator, terminator;
            if (!text_of(sep, " ", "sep", separator) || !text_of(end, "\n", "end", terminator))
                return nullptr;
            std::string line;
            for (std::size_t i = 0u; i < aArgs.size(); ++i)
            {
                std::string text;
                if (!str_of(aArgs[i], text))
                    return nullptr;
                if (i != 0u)
                    line += separator;
                line += text;
            }
            write_out(line + terminator);
            return none();
        }

        object* b_len(std::vector<object*> const& aArgs, object* aKwargs)
        {
            if (!no_keywords(aKwargs, "len") || !arity(aArgs, 1u, 1u, "len"))
                return nullptr;
            auto const n = length(aArgs[0]);
            return n < 0 ? nullptr : make_int(n);
        }

        object* b_repr(std::vector<object*> const& aArgs, object* aKwargs)
        {
            if (!no_keywords(aKwargs, "repr") || !arity(aArgs, 1u, 1u, "repr"))
                return nullptr;
            std::string text;
            return repr_of(aArgs[0], text) ? make_str(std::move(text)) : nullptr;
        }

        object* b_str(std::vector<object*> const& aArgs, object* aKwargs)
        {
            if (!no_keywords(aKwargs, "str") || !arity(aArgs, 0u, 1u, "str"))
                return nullptr;
            if (aArgs.empty())
                return make_str({});
            std::string text;
            return str_of(aArgs[0], text) ? make_str(std::move(text)) : nullptr;
        }

        std::int64_t fn_to_int(object* aObject);
        double fn_to_float(object* aObject);

        object* b_int(std::vector<object*> const& aArgs, object* aKwargs)
        {
            if (!no_keywords(aKwargs, "int") || !arity(aArgs, 0u, 1u, "int"))
                return nullptr;
            if (aArgs.empty())
                return make_int(0);
            auto const value = fn_to_int(aArgs[0]);
            return failed() ? nullptr : make_int(value);
        }

        object* b_float(std::vector<object*> const& aArgs, object* aKwargs)
        {
            if (!no_keywords(aKwargs, "float") || !arity(aArgs, 0u, 1u, "float"))
                return nullptr;
            if (aArgs.empty())
                return make_float(0.0);
            auto const value = fn_to_float(aArgs[0]);
            return failed() ? nullptr : make_float(value);
        }

        object* b_bool(std::vector<object*> const& aArgs, object* aKwargs)
        {
            if (!no_keywords(aKwargs, "bool") || !arity(aArgs, 0u, 1u, "bool"))
                return nullptr;
            if (aArgs.empty())
                return make_bool(false);
            auto const t = truth(aArgs[0]);
            return t < 0 ? nullptr : make_bool(t == 1);
        }

        object* b_complex(std::vector<object*> const& aArgs, object* aKwargs)
        {
            if (!no_keywords(aKwargs, "complex") || !arity(aArgs, 0u, 2u, "complex"))
                return nullptr;
            double re = 0.0, im = 0.0, re2 = 0.0, im2 = 0.0;
            auto const parts = [](object* aObject, double& aReal, double& aImag)
            {
                if (kind_of(aObject) == NEOS_PY_COMPLEX)
                {
                    aReal = as<complex_object>(aObject)->real;
                    aImag = as<complex_object>(aObject)->imag;
                    return true;
                }
                aImag = 0.0;
                return to_double(aObject, aReal);
            };
            if (!aArgs.empty() && !parts(aArgs[0], re, im))
                return nullptr;
            if (aArgs.size() > 1u && !parts(aArgs[1], re2, im2))
                return nullptr;
            return make_complex(re - im2, im + re2);
        }

        object* b_abs(std::vector<object*> const& aArgs, object* aKwargs)
        {
            if (!no_keywords(aKwargs, "abs") || !arity(aArgs, 1u, 1u, "abs"))
                return nullptr;
            return unary(NEOS_PY_ABSOLUTE, aArgs[0]);
        }

        object* b_list(std::vector<object*> const& aArgs, object* aKwargs)
        {
            if (!no_keywords(aKwargs, "list") || !arity(aArgs, 0u, 1u, "list"))
                return nullptr;
            std::vector<object*> items;
            if (!aArgs.empty() && !collect(aArgs[0], items))
            {
                for (auto i : items)
                    release(i);
                return nullptr;
            }
            return make_list(std::move(items));
        }

        object* b_tuple(std::vector<object*> const& aArgs, object* aKwargs)
        {
            if (!no_keywords(aKwargs, "tuple") || !arity(aArgs, 0u, 1u, "tuple"))
                return nullptr;
            std::vector<object*> items;
            if (!aArgs.empty() && !collect(aArgs[0], items))
            {
                for (auto i : items)
                    release(i);
                return nullptr;
            }
            return make_tuple(std::move(items));
        }

        object* b_dict(std::vector<object*> const& aArgs, object* aKwargs)
        {
            if (!arity(aArgs, 0u, 1u, "dict"))
                return nullptr;
            ref result{ make_dict() };
            if (!aArgs.empty())
            {
                if (kind_of(aArgs[0]) == NEOS_PY_DICT)
                {
                    for (auto const& item : as<dict_object>(aArgs[0])->items)
                        if (dict_set(result.get(), item.first, item.second) < 0)
                            return nullptr;
                }
                else
                {
                    std::vector<object*> items;
                    bool const ok = collect(aArgs[0], items);
                    std::vector<ref> owned;
                    for (auto i : items)
                        owned.emplace_back(i);
                    if (!ok)
                        return nullptr;
                    for (std::size_t i = 0u; i < items.size(); ++i)
                    {
                        std::vector<object*> pair;
                        bool const pairOk = collect(items[i], pair);
                        std::vector<ref> ownedPair;
                        for (auto p : pair)
                            ownedPair.emplace_back(p);
                        if (!pairOk)
                            return error("TypeError", "cannot convert dictionary update sequence element #" + std::to_string(i) + " to a sequence");
                        if (pair.size() != 2u)
                            return error("ValueError", "dictionary update sequence element #" + std::to_string(i) + " has length " + std::to_string(pair.size()) + "; 2 is required");
                        if (dict_set(result.get(), pair[0], pair[1]) < 0)
                            return nullptr;
                    }
                }
            }
            if (aKwargs != nullptr)
                for (auto const& item : as<dict_object>(aKwargs)->items)
                    if (dict_set(result.get(), item.first, item.second) < 0)
                        return nullptr;
            return result.release_ownership();
        }

        object* b_range(std::vector<object*> const& aArgs, object* aKwargs)
        {
            if (!no_keywords(aKwargs, "range"))
                return nullptr;
            if (aArgs.empty())
                return error("TypeError", "range expected at least 1 argument, got 0");
            if (aArgs.size() > 3u)
                return error("TypeError", "range expected at most 3 arguments, got " + std::to_string(aArgs.size()));
            long long values[3] = { 0, 0, 1 };
            for (std::size_t i = 0u; i < aArgs.size(); ++i)
                if (!to_index(aArgs[i], values[aArgs.size() == 1u ? 1u : i]))
                    return nullptr;
            if (values[2] == 0)
                return error("ValueError", "range() arg 3 must not be zero");
            return make_range(values[0], values[1], values[2]);
        }

        // (the items of an iterable)
        bool items_of(object* aIterable, std::vector<ref>& aItems)
        {
            std::vector<object*> items;
            bool const ok = collect(aIterable, items);
            for (auto i : items)
                aItems.emplace_back(i);
            return ok;
        }

        object* iterator_over(std::vector<object*> aItems)
        {
            ref const list{ make_list(std::move(aItems)) };
            return make_iterator(list.get());
        }

        object* b_enumerate(std::vector<object*> const& aArgs, object* aKwargs)
        {
            object* start = nullptr;
            if (!keywords(aKwargs, { { "start", &start } }, "enumerate") || !arity(aArgs, 1u, 2u, "enumerate"))
                return nullptr;
            if (aArgs.size() > 1u)
                start = aArgs[1];
            long long first = 0;
            if (start != nullptr && !to_index(start, first))
                return nullptr;
            std::vector<ref> items;
            if (!items_of(aArgs[0], items))
                return nullptr;
            std::vector<object*> result;
            for (std::size_t i = 0u; i < items.size(); ++i)
            {
                auto const item = items[i].get();
                retain(item);
                result.push_back(make_tuple({ make_int(first + static_cast<long long>(i)), item }));
            }
            return iterator_over(std::move(result));
        }

        object* b_zip(std::vector<object*> const& aArgs, object* aKwargs)
        {
            object* strict = nullptr;
            if (!keywords(aKwargs, { { "strict", &strict } }, "zip"))
                return nullptr;
            std::vector<std::vector<ref>> sequences(aArgs.size());
            std::size_t n = std::numeric_limits<std::size_t>::max();
            for (std::size_t i = 0u; i < aArgs.size(); ++i)
            {
                if (!items_of(aArgs[i], sequences[i]))
                    return nullptr;
                n = std::min(n, sequences[i].size());
            }
            if (aArgs.empty())
                n = 0u;
            std::vector<object*> result;
            for (std::size_t j = 0u; j < n; ++j)
            {
                std::vector<object*> tuple;
                for (auto const& sequence : sequences)
                {
                    retain(sequence[j].get());
                    tuple.push_back(sequence[j].get());
                }
                result.push_back(make_tuple(std::move(tuple)));
            }
            return iterator_over(std::move(result));
        }

        object* b_reversed(std::vector<object*> const& aArgs, object* aKwargs)
        {
            if (!no_keywords(aKwargs, "reversed") || !arity(aArgs, 1u, 1u, "reversed"))
                return nullptr;
            auto const k = kind_of(aArgs[0]);
            if (k != NEOS_PY_LIST && k != NEOS_PY_TUPLE && k != NEOS_PY_STR && k != NEOS_PY_RANGE && !(k == NEOS_PY_OTHER && python_type(aArgs[0]) != nullptr && python_type(aArgs[0])->getitem != nullptr))
                return error("TypeError", std::string{ "'" } + type_name(aArgs[0]) + "' object is not reversible");
            std::vector<ref> items;
            if (!items_of(aArgs[0], items))
                return nullptr;
            std::vector<object*> result;
            for (auto i = items.rbegin(); i != items.rend(); ++i)
            {
                retain(i->get());
                result.push_back(i->get());
            }
            return iterator_over(std::move(result));
        }

        object* b_iter(std::vector<object*> const& aArgs, object* aKwargs)
        {
            if (!no_keywords(aKwargs, "iter") || !arity(aArgs, 1u, 1u, "iter"))
                return nullptr;
            return iter(aArgs[0]);
        }

        object* b_next(std::vector<object*> const& aArgs, object* aKwargs)
        {
            if (!no_keywords(aKwargs, "next") || !arity(aArgs, 1u, 2u, "next"))
                return nullptr;
            auto const result = next(aArgs[0]);
            if (result != nullptr || failed())
                return result;
            if (aArgs.size() > 1u)
            {
                retain(aArgs[1]);
                return aArgs[1];
            }
            return error("StopIteration", "");
        }

        // a key function's value (aKey: null for the item itself)
        object* key_of(object* aKey, object* aItem)
        {
            if (aKey == nullptr || kind_of(aKey) == NEOS_PY_NONE)
            {
                retain(aItem);
                return aItem;
            }
            ref const args{ make_tuple({ (retain(aItem), aItem) }) };
            return call(aKey, args.get(), nullptr);
        }

        int less_than(object* a, object* b)
        {
            ref const less{ compare(NEOS_PY_LT, a, b) };
            if (!less)
                return -1;
            return truth(less.get());
        }

        object* minmax(std::vector<object*> const& aArgs, object* aKwargs, bool aMax)
        {
            char const* const name = aMax ? "max" : "min";
            object* key = nullptr;
            object* fallback = nullptr;
            if (!keywords(aKwargs, { { "key", &key }, { "default", &fallback } }, name))
                return nullptr;
            if (aArgs.empty())
                return error("TypeError", std::string{ name } + " expected at least 1 argument, got 0");
            std::vector<ref> items;
            if (aArgs.size() == 1u)
            {
                if (!items_of(aArgs[0], items))
                    return nullptr;
            }
            else
                for (auto a : aArgs)
                    items.push_back(ref::borrowed(a));
            if (items.empty())
            {
                if (fallback != nullptr)
                {
                    retain(fallback);
                    return fallback;
                }
                return error("ValueError", std::string{ name } + "() iterable argument is empty");
            }
            ref best = items[0];
            ref bestKey{ key_of(key, best.get()) };
            if (!bestKey)
                return nullptr;
            for (std::size_t i = 1u; i < items.size(); ++i)
            {
                ref const k{ key_of(key, items[i].get()) };
                if (!k)
                    return nullptr;
                auto const t = aMax ? less_than(bestKey.get(), k.get()) : less_than(k.get(), bestKey.get());
                if (t < 0)
                    return nullptr;
                if (t == 1)
                {
                    best = items[i];
                    bestKey = k;
                }
            }
            return best.release_ownership();
        }

        object* b_min(std::vector<object*> const& aArgs, object* aKwargs) { return minmax(aArgs, aKwargs, false); }
        object* b_max(std::vector<object*> const& aArgs, object* aKwargs) { return minmax(aArgs, aKwargs, true); }

        object* b_sum(std::vector<object*> const& aArgs, object* aKwargs)
        {
            object* start = nullptr;
            if (!keywords(aKwargs, { { "start", &start } }, "sum") || !arity(aArgs, 1u, 2u, "sum"))
                return nullptr;
            if (aArgs.size() > 1u)
                start = aArgs[1];
            if (start != nullptr && kind_of(start) == NEOS_PY_STR)
                return error("TypeError", "sum() can't sum strings [use ''.join(seq) instead]");
            std::vector<ref> items;
            if (!items_of(aArgs[0], items))
                return nullptr;
            ref total = start != nullptr ? ref::borrowed(start) : ref{ make_int(0) };
            // (CPython sums floats with compensated summation)
            bool allFloat = true;
            for (auto const& item : items)
                if (kind_of(item.get()) != NEOS_PY_FLOAT && kind_of(item.get()) != NEOS_PY_INT && kind_of(item.get()) != NEOS_PY_BOOL)
                    allFloat = false;
            if (allFloat && (kind_of(total.get()) == NEOS_PY_INT || kind_of(total.get()) == NEOS_PY_FLOAT || kind_of(total.get()) == NEOS_PY_BOOL))
            {
                std::size_t i = 0u;
                // integers first (exactly), then floats (Neumaier)
                long long integer = 0;
                bool isFloat = kind_of(total.get()) == NEOS_PY_FLOAT;
                if (!isFloat)
                {
                    integer = int_value(total.get());
                    for (; i < items.size() && kind_of(items[i].get()) != NEOS_PY_FLOAT; ++i)
                        if (add_overflow(integer, int_value(items[i].get()), &integer))
                            return int_overflow();
                    if (i == items.size())
                        return make_int(integer);
                }
                double sum = isFloat ? as<float_object>(total.get())->value : static_cast<double>(integer);
                double c = 0.0;
                for (; i < items.size(); ++i)
                {
                    double x = 0.0;
                    to_double(items[i].get(), x);
                    double const t = sum + x;
                    if (std::fabs(sum) >= std::fabs(x))
                        c += (sum - t) + x;
                    else
                        c += (x - t) + sum;
                    sum = t;
                }
                // (as CPython: the compensation is not added if it is zero, or the sum is not finite)
                if (c != 0.0 && std::isfinite(c))
                    sum += c;
                return make_float(sum);
            }
            for (auto const& item : items)
            {
                ref next{ binary(NEOS_PY_ADD, total.get(), item.get(), false) };
                if (!next)
                    return nullptr;
                total = next;
            }
            return total.release_ownership();
        }

        object* any_all(std::vector<object*> const& aArgs, object* aKwargs, bool aAll)
        {
            if (!no_keywords(aKwargs, aAll ? "all" : "any") || !arity(aArgs, 1u, 1u, aAll ? "all" : "any"))
                return nullptr;
            std::vector<ref> items;
            if (!items_of(aArgs[0], items))
                return nullptr;
            for (auto const& item : items)
            {
                auto const t = truth(item.get());
                if (t < 0)
                    return nullptr;
                if ((t == 1) != aAll)
                    return make_bool(!aAll);
            }
            return make_bool(aAll);
        }

        object* b_any(std::vector<object*> const& aArgs, object* aKwargs) { return any_all(aArgs, aKwargs, false); }
        object* b_all(std::vector<object*> const& aArgs, object* aKwargs) { return any_all(aArgs, aKwargs, true); }

        object* b_sorted(std::vector<object*> const& aArgs, object* aKwargs)
        {
            object* key = nullptr;
            object* reverse = nullptr;
            if (!keywords(aKwargs, { { "key", &key }, { "reverse", &reverse } }, "sort") || !arity(aArgs, 1u, 1u, "sorted"))
                return nullptr;
            std::vector<ref> items;
            if (!items_of(aArgs[0], items))
                return nullptr;
            std::vector<std::pair<ref, ref>> keyed;
            for (auto const& item : items)
            {
                ref k{ key_of(key, item.get()) };
                if (!k)
                    return nullptr;
                keyed.emplace_back(k, item);
            }
            bool const descending = reverse != nullptr && truth(reverse) == 1;
            bool failure = false;
            // (stable: equal items keep their order, also when descending)
            std::stable_sort(keyed.begin(), keyed.end(), [&](auto const& a, auto const& b)
            {
                if (failure)
                    return false;
                auto const t = descending ? less_than(b.first.get(), a.first.get()) : less_than(a.first.get(), b.first.get());
                if (t < 0)
                    failure = true;
                return t == 1;
            });
            if (failure)
                return nullptr;
            std::vector<object*> result;
            for (auto& k : keyed)
                result.push_back(k.second.release_ownership());
            return make_list(std::move(result));
        }

        object* b_isinstance(std::vector<object*> const& aArgs, object* aKwargs)
        {
            if (!no_keywords(aKwargs, "isinstance") || !arity(aArgs, 2u, 2u, "isinstance"))
                return nullptr;
            std::vector<object*> classes;
            if (kind_of(aArgs[1]) == NEOS_PY_TUPLE)
                classes = as<tuple_object>(aArgs[1])->items;
            else
                classes.push_back(aArgs[1]);
            ref const actual{ class_of(aArgs[0]) };
            auto const& name = as<type_object>(actual.get())->qualifiedName;
            for (auto c : classes)
            {
                if (!is_type_object(c))
                    return error("TypeError", "isinstance() arg 2 must be a type, a tuple of types, or a union");
                if (as<type_object>(c)->instancecheck != nullptr)
                {
                    if (as<type_object>(c)->instancecheck(context(), as<type_object>(c)->data, aArgs[0]) == 1)
                        return make_bool(true);
                    continue;
                }
                auto const& expected = as<type_object>(c)->qualifiedName;
                if (expected == name || expected == "object" || (expected == "int" && name == "bool"))
                    return make_bool(true);
            }
            return make_bool(false);
        }

        object* b_type(std::vector<object*> const& aArgs, object* aKwargs)
        {
            if (!no_keywords(aKwargs, "type") || !arity(aArgs, 1u, 1u, "type"))
                return nullptr;
            return class_of(aArgs[0]);
        }

        object* b_round(std::vector<object*> const& aArgs, object* aKwargs)
        {
            object* digits = nullptr;
            if (!keywords(aKwargs, { { "ndigits", &digits } }, "round") || !arity(aArgs, 1u, 2u, "round"))
                return nullptr;
            if (aArgs.size() > 1u)
                digits = aArgs[1];
            bool const none = digits == nullptr || kind_of(digits) == NEOS_PY_NONE;
            long long n = 0;
            if (!none && !to_index(digits, n))
                return nullptr;
            auto const k = kind_of(aArgs[0]);
            if (k == NEOS_PY_INT || k == NEOS_PY_BOOL)
            {
                auto const value = int_value(aArgs[0]);
                if (none || n >= 0)
                    return make_int(value);
                if (n < -18)
                    return make_int(0);
                long long p = 1;
                for (long long i = 0; i < -n; ++i)
                    p *= 10;
                // (round half to even)
                auto q = value / p;
                auto r = value % p;
                if (r < 0) { r += p; --q; }
                if (2 * r > p || (2 * r == p && (q % 2) != 0))
                    ++q;
                return make_int(q * p);
            }
            if (k == NEOS_PY_FLOAT)
            {
                auto const x = as<float_object>(aArgs[0])->value;
                if (none)
                {
                    if (!std::isfinite(x))
                        return error(std::isnan(x) ? "ValueError" : "OverflowError", std::isnan(x) ? "cannot convert float NaN to integer" : "cannot convert float infinity to integer");
                    auto const r = std::nearbyint(x); // (round half to even)
                    if (r >= 9223372036854775808.0 || r < -9223372036854775808.0)
                        return int_overflow();
                    return make_int(static_cast<long long>(r));
                }
                if (!std::isfinite(x) || x == 0.0)
                    return make_float(x);
                if (n > 323)
                    return make_float(x);
                if (n < -308)
                    return make_float(std::copysign(0.0, x));
                // (as CPython: the correctly rounded decimal string, then back to a double)
                char buffer[512];
                std::to_chars_result converted;
                if (n >= 0)
                    converted = std::to_chars(buffer, buffer + sizeof(buffer), x, std::chars_format::fixed, static_cast<int>(n));
                else
                {
                    double const scale = std::pow(10.0, static_cast<double>(-n));
                    auto const y = x / scale;
                    converted = std::to_chars(buffer, buffer + sizeof(buffer), std::nearbyint(y) * scale, std::chars_format::fixed, 0);
                }
                double result = 0.0;
                std::from_chars(buffer, converted.ptr, result);
                if (result == 0.0)
                    result = std::copysign(0.0, x);
                return make_float(result);
            }
            if (k == NEOS_PY_OTHER)
            {
                // (an object of another plugin: its __round__ is its unary operator "round")
                if (auto const type = python_type(aArgs[0]); type != nullptr && type->getattr != nullptr)
                {
                    ref const method{ type->getattr(context(), aArgs[0], "__round__") };
                    if (method)
                    {
                        ref const args{ none ? make_tuple() : make_tuple({ make_int(n) }) };
                        return call(method.get(), args.get(), nullptr);
                    }
                    return nullptr;
                }
            }
            return error("TypeError", std::string{ "type " } + type_name(aArgs[0]) + " doesn't define __round__ method");
        }

        object* b_divmod(std::vector<object*> const& aArgs, object* aKwargs)
        {
            if (!no_keywords(aKwargs, "divmod") || !arity(aArgs, 2u, 2u, "divmod"))
                return nullptr;
            ref q{ binary(NEOS_PY_FLOOR_DIVIDE, aArgs[0], aArgs[1], false) };
            if (!q)
                return nullptr;
            ref r{ binary(NEOS_PY_REMAINDER, aArgs[0], aArgs[1], false) };
            if (!r)
                return nullptr;
            return make_tuple({ q.release_ownership(), r.release_ownership() });
        }

        object* b_pow(std::vector<object*> const& aArgs, object* aKwargs)
        {
            if (!no_keywords(aKwargs, "pow") || !arity(aArgs, 2u, 3u, "pow"))
                return nullptr;
            if (aArgs.size() == 2u || kind_of(aArgs[2]) == NEOS_PY_NONE)
                return binary(NEOS_PY_POWER, aArgs[0], aArgs[1], false);
            long long b = 0, e = 0, m = 0;
            if (!is_int_like(aArgs[0]) || !is_int_like(aArgs[1]) || !is_int_like(aArgs[2]))
                return error("TypeError", "pow() 3rd argument not allowed unless all arguments are integers");
            b = int_value(aArgs[0]); e = int_value(aArgs[1]); m = int_value(aArgs[2]);
            if (m == 0)
                return error("ValueError", "pow() 3rd argument cannot be 0");
            if (e < 0)
                return error("ValueError", "base is not invertible for the given modulus");
            auto const modulus = static_cast<unsigned long long>(m < 0 ? -m : m);
            // (a * b % modulus without overflow)
            auto const multiply_mod = [modulus](unsigned long long a, unsigned long long b)
            {
                unsigned long long result = 0u;
                a %= modulus;
                for (; b != 0u; b >>= 1)
                {
                    if ((b & 1u) != 0u)
                        result = (result >= modulus - a) ? result - (modulus - a) : result + a;
                    a = (a >= modulus - a) ? a - (modulus - a) : a + a;
                }
                return result;
            };
            long long bb = b % static_cast<long long>(modulus);
            if (bb < 0)
                bb += static_cast<long long>(modulus);
            unsigned long long result = 1u % modulus;
            unsigned long long base = static_cast<unsigned long long>(bb);
            for (; e > 0; e >>= 1)
            {
                if ((e & 1) != 0)
                    result = multiply_mod(result, base);
                base = multiply_mod(base, base);
            }
            auto r = static_cast<long long>(result);
            if (m < 0 && r != 0)
                r += m;
            return make_int(r);
        }

        object* b_chr(std::vector<object*> const& aArgs, object* aKwargs)
        {
            if (!no_keywords(aKwargs, "chr") || !arity(aArgs, 1u, 1u, "chr"))
                return nullptr;
            long long c = 0;
            if (!to_index(aArgs[0], c))
                return nullptr;
            if (c < 0 || c > 0x10FFFF)
                return error("ValueError", "chr() arg not in range(0x110000)");
            std::string text;
            auto const u = static_cast<unsigned long>(c);
            if (u < 0x80u) text += static_cast<char>(u);
            else if (u < 0x800u) { text += static_cast<char>(0xC0u | (u >> 6)); text += static_cast<char>(0x80u | (u & 0x3Fu)); }
            else if (u < 0x10000u) { text += static_cast<char>(0xE0u | (u >> 12)); text += static_cast<char>(0x80u | ((u >> 6) & 0x3Fu)); text += static_cast<char>(0x80u | (u & 0x3Fu)); }
            else { text += static_cast<char>(0xF0u | (u >> 18)); text += static_cast<char>(0x80u | ((u >> 12) & 0x3Fu)); text += static_cast<char>(0x80u | ((u >> 6) & 0x3Fu)); text += static_cast<char>(0x80u | (u & 0x3Fu)); }
            return make_str(std::move(text));
        }

        object* b_ord(std::vector<object*> const& aArgs, object* aKwargs)
        {
            if (!no_keywords(aKwargs, "ord") || !arity(aArgs, 1u, 1u, "ord"))
                return nullptr;
            if (kind_of(aArgs[0]) != NEOS_PY_STR)
                return error("TypeError", std::string{ "ord() expected string of length 1, but " } + type_name(aArgs[0]) + " found");
            auto const& text = as<str_object>(aArgs[0])->value;
            auto const n = length(aArgs[0]);
            if (n != 1)
                return error("TypeError", "ord() expected a character, but string of length " + std::to_string(n) + " found");
            auto const b = static_cast<unsigned char>(text[0]);
            unsigned long c = b;
            if (b >= 0xF0u) c = ((b & 0x07u) << 18) | ((text[1] & 0x3Fu) << 12) | ((text[2] & 0x3Fu) << 6) | (text[3] & 0x3Fu);
            else if (b >= 0xE0u) c = ((b & 0x0Fu) << 12) | ((text[1] & 0x3Fu) << 6) | (text[2] & 0x3Fu);
            else if (b >= 0xC0u) c = ((b & 0x1Fu) << 6) | (text[1] & 0x3Fu);
            return make_int(static_cast<long long>(c));
        }

        object* radix(std::vector<object*> const& aArgs, object* aKwargs, char const* aName, int aBase, char const* aPrefix)
        {
            if (!no_keywords(aKwargs, aName) || !arity(aArgs, 1u, 1u, aName))
                return nullptr;
            long long v = 0;
            if (!to_index(aArgs[0], v))
                return nullptr;
            unsigned long long u = v < 0 ? 0ull - static_cast<unsigned long long>(v) : static_cast<unsigned long long>(v);
            char buffer[80];
            auto const converted = std::to_chars(buffer, buffer + sizeof(buffer), u, aBase);
            return make_str(std::string{ v < 0 ? "-" : "" } + aPrefix + std::string{ buffer, converted.ptr });
        }

        object* b_hex(std::vector<object*> const& aArgs, object* aKwargs) { return radix(aArgs, aKwargs, "hex", 16, "0x"); }
        object* b_oct(std::vector<object*> const& aArgs, object* aKwargs) { return radix(aArgs, aKwargs, "oct", 8, "0o"); }
        object* b_bin(std::vector<object*> const& aArgs, object* aKwargs) { return radix(aArgs, aKwargs, "bin", 2, "0b"); }

        object* b_getattr(std::vector<object*> const& aArgs, object* aKwargs)
        {
            if (!no_keywords(aKwargs, "getattr") || !arity(aArgs, 2u, 3u, "getattr"))
                return nullptr;
            if (kind_of(aArgs[1]) != NEOS_PY_STR)
                return error("TypeError", "attribute name must be string, not '" + std::string{ type_name(aArgs[1]) } + "'");
            auto const result = getattr(aArgs[0], as<str_object>(aArgs[1])->value.c_str());
            if (result == nullptr && aArgs.size() == 3u)
            {
                clear_error();
                retain(aArgs[2]);
                return aArgs[2];
            }
            return result;
        }

        object* b_hasattr(std::vector<object*> const& aArgs, object* aKwargs)
        {
            if (!no_keywords(aKwargs, "hasattr") || !arity(aArgs, 2u, 2u, "hasattr"))
                return nullptr;
            if (kind_of(aArgs[1]) != NEOS_PY_STR)
                return error("TypeError", "attribute name must be string, not '" + std::string{ type_name(aArgs[1]) } + "'");
            ref const result{ getattr(aArgs[0], as<str_object>(aArgs[1])->value.c_str()) };
            if (!result)
                clear_error();
            return make_bool(static_cast<bool>(result));
        }

        object* b_setattr(std::vector<object*> const& aArgs, object* aKwargs)
        {
            if (!no_keywords(aKwargs, "setattr") || !arity(aArgs, 3u, 3u, "setattr"))
                return nullptr;
            if (kind_of(aArgs[1]) != NEOS_PY_STR)
                return error("TypeError", "attribute name must be string, not '" + std::string{ type_name(aArgs[1]) } + "'");
            return setattr(aArgs[0], as<str_object>(aArgs[1])->value.c_str(), aArgs[2]) < 0 ? nullptr : none();
        }

        object* b_callable(std::vector<object*> const& aArgs, object* aKwargs)
        {
            if (!no_keywords(aKwargs, "callable") || !arity(aArgs, 1u, 1u, "callable"))
                return nullptr;
            auto const k = kind_of(aArgs[0]);
            auto const type = python_type(aArgs[0]);
            return make_bool(k == NEOS_PY_FUNCTION || (k == NEOS_PY_OTHER && type != nullptr && type->call != nullptr));
        }

        object* b_map(std::vector<object*> const& aArgs, object* aKwargs)
        {
            if (!no_keywords(aKwargs, "map"))
                return nullptr;
            if (aArgs.size() < 2u)
                return error("TypeError", "map() must have at least two arguments.");
            std::vector<object*> rest{ aArgs.begin() + 1, aArgs.end() };
            ref const zipped{ b_zip(rest, nullptr) };
            if (!zipped)
                return nullptr;
            std::vector<ref> tuples;
            if (!items_of(zipped.get(), tuples))
                return nullptr;
            std::vector<object*> result;
            for (auto const& t : tuples)
            {
                auto const value = call(aArgs[0], t.get(), nullptr);
                if (value == nullptr)
                {
                    for (auto r : result)
                        release(r);
                    return nullptr;
                }
                result.push_back(value);
            }
            return iterator_over(std::move(result));
        }

        object* b_filter(std::vector<object*> const& aArgs, object* aKwargs)
        {
            if (!no_keywords(aKwargs, "filter") || !arity(aArgs, 2u, 2u, "filter"))
                return nullptr;
            std::vector<ref> items;
            if (!items_of(aArgs[1], items))
                return nullptr;
            std::vector<object*> result;
            for (auto const& item : items)
            {
                ref const value{ key_of(aArgs[0], item.get()) };
                if (!value)
                    return nullptr;
                auto const t = truth(value.get());
                if (t < 0)
                    return nullptr;
                if (t == 1)
                {
                    retain(item.get());
                    result.push_back(item.get());
                }
            }
            return iterator_over(std::move(result));
        }

        object* b_input(std::vector<object*> const& aArgs, object* aKwargs)
        {
            if (!no_keywords(aKwargs, "input") || !arity(aArgs, 0u, 1u, "input"))
                return nullptr;
            if (!aArgs.empty())
            {
                std::string prompt;
                if (!str_of(aArgs[0], prompt))
                    return nullptr;
                write_out(prompt);
            }
            std::string line;
            char buffer[256];
            while (std::fgets(buffer, sizeof(buffer), stdin) != nullptr)
            {
                line += buffer;
                if (!line.empty() && line.back() == '\n')
                    break;
            }
            while (!line.empty() && (line.back() == '\n' || line.back() == '\r'))
                line.pop_back();
            return make_str(std::move(line));
        }

        object* b_slice(std::vector<object*> const& aArgs, object* aKwargs)
        {
            if (!no_keywords(aKwargs, "slice") || !arity(aArgs, 1u, 3u, "slice"))
                return nullptr;
            if (aArgs.size() == 1u)
                return make_slice(nullptr, aArgs[0], nullptr);
            return make_slice(aArgs[0], aArgs[1], aArgs.size() > 2u ? aArgs[2] : nullptr);
        }

        object* b_object(std::vector<object*> const&, object*)
        {
            return error("TypeError", "object() instances are not supported");
        }

        builtin_entry const sBuiltinFunctions[] =
        {
            { "abs", b_abs }, { "all", b_all }, { "any", b_any }, { "bin", b_bin }, { "callable", b_callable }, { "chr", b_chr },
            { "divmod", b_divmod }, { "enumerate", b_enumerate }, { "filter", b_filter }, { "getattr", b_getattr }, { "hasattr", b_hasattr }, 
            { "hex", b_hex }, { "input", b_input }, { "isinstance", b_isinstance }, { "iter", b_iter }, { "len", b_len }, { "map", b_map },
            { "max", b_max }, { "min", b_min }, { "next", b_next }, { "oct", b_oct }, { "ord", b_ord }, { "pow", b_pow }, { "print", b_print },
            { "repr", b_repr }, { "reversed", b_reversed }, { "round", b_round }, { "setattr", b_setattr }, { "sorted", b_sorted }, { "sum", b_sum },
            { "zip", b_zip }
        };

        // (the builtin classes)
        builtin_entry const sBuiltinClasses[] =
        {
            { "int", b_int }, { "float", b_float }, { "str", b_str }, { "bool", b_bool }, { "complex", b_complex }, { "list", b_list },
            { "tuple", b_tuple }, { "dict", b_dict }, { "range", b_range }, { "type", b_type }, { "slice", b_slice }, { "object", b_object }
        };

        builtin_entry const* find_builtin(std::string_view aName)
        {
            for (auto const& entry : sBuiltinFunctions)
                if (aName == entry.name)
                    return &entry;
            for (auto const& entry : sBuiltinClasses)
                if (aName == entry.name)
                    return &entry;
            return nullptr;
        }

        object* fn_builtin(char const* aName)
        {
            std::string_view const name{ aName };
            for (auto const& entry : sBuiltinClasses)
                if (name == entry.name)
                    return make_type_object(entry.name, entry.name);
            for (auto const& entry : sBuiltinFunctions)
                if (name == entry.name)
                    return make_function(entry.name, call_builtin, const_cast<builtin_entry*>(&entry));
            if (name == "NotImplemented")
                return not_implemented();
            if (name == "Ellipsis")
                return &sEllipsis.base;
            return error("NameError", "name '" + std::string{ name } + "' is not defined");
        }

        // a, b = value
        object* fn_unpack(object* aValue, std::int64_t aCount)
        {
            auto const k = kind_of(aValue);
            if (k == NEOS_PY_OTHER ? python_type(aValue) == nullptr || (python_type(aValue)->iter == nullptr && python_type(aValue)->getitem == nullptr) :
                (k != NEOS_PY_LIST && k != NEOS_PY_TUPLE && k != NEOS_PY_STR && k != NEOS_PY_DICT && k != NEOS_PY_RANGE && k != NEOS_PY_ITERATOR))
                return error("TypeError", std::string{ "cannot unpack non-iterable " } + type_name(aValue) + " object");
            std::vector<object*> items;
            if (!collect(aValue, items))
            {
                for (auto i : items)
                    release(i);
                return nullptr;
            }
            if (static_cast<std::int64_t>(items.size()) != aCount)
            {
                auto const n = items.size();
                for (auto i : items)
                    release(i);
                if (static_cast<std::int64_t>(n) > aCount)
                    return error("ValueError", "too many values to unpack (expected " + std::to_string(aCount) + ")");
                return error("ValueError", "not enough values to unpack (expected " + std::to_string(aCount) + ", got " + std::to_string(n) + ")");
            }
            return make_tuple(std::move(items));
        }

        object* fn_tuple_get(object* aTuple, std::int64_t aIndex)
        {
            auto const item = as<tuple_object>(aTuple)->items[static_cast<std::size_t>(aIndex)];
            retain(item);
            return item;
        }

        // (builtins.py's stubs whose argument is an object)
        void fn_print_object(object* aObject)
        {
            std::string text;
            if (str_of(aObject, text))
                write_out(text + "\n");
        }
        object* fn_abs_object(object* aObject) { return checked(unary(NEOS_PY_ABSOLUTE, aObject)); }
        bool fn_bool_object(object* aObject) { return truth(aObject) == 1; }

        constexpr auto OBJ = NEOS_RT_OBJECT;
        constexpr auto I64 = NEOS_RT_I64;
        constexpr auto I32 = NEOS_RT_I32;
        constexpr auto F64 = NEOS_RT_F64;
        constexpr auto STR = NEOS_RT_STRING;
        constexpr auto BOOL = NEOS_RT_BOOL;
        constexpr auto VOID = NEOS_RT_VOID;
    }

    std::vector<neos_rt_function> const& object_functions()
    {
        static std::vector<neos_rt_function> const sFunctions =
        {
            { "context", NEOS_RT_FUNCTION(fn_context), OBJ, 0u, {}, 0u },
            { "none", NEOS_RT_FUNCTION(fn_none), OBJ, 0u, {}, 0u },
            { "ellipsis", NEOS_RT_FUNCTION(fn_ellipsis), OBJ, 0u, {}, 0u },
            { "from_bool", NEOS_RT_FUNCTION(fn_from_bool), OBJ, 1u, { BOOL }, 0u },
            { "from_int", NEOS_RT_FUNCTION(fn_from_int), OBJ, 1u, { I64 }, 0u },
            { "from_float", NEOS_RT_FUNCTION(fn_from_float), OBJ, 1u, { F64 }, 0u },
            { "from_str", NEOS_RT_FUNCTION(fn_from_str), OBJ, 1u, { STR }, 0u },
            { "truth", NEOS_RT_FUNCTION(fn_truth), BOOL, 1u, { OBJ }, 0u },
            { "str", NEOS_RT_FUNCTION(fn_str), STR, 1u, { OBJ }, 0u },
            { "repr", NEOS_RT_FUNCTION(fn_repr), STR, 1u, { OBJ }, 0u },
            { "to_int", NEOS_RT_FUNCTION(fn_to_int), I64, 1u, { OBJ }, 0u },
            { "to_float", NEOS_RT_FUNCTION(fn_to_float), F64, 1u, { OBJ }, 0u },
            { "binary", NEOS_RT_FUNCTION(fn_binary), OBJ, 3u, { I32, OBJ, OBJ }, 0u },
            { "inplace", NEOS_RT_FUNCTION(fn_inplace), OBJ, 3u, { I32, OBJ, OBJ }, 0u },
            { "unary", NEOS_RT_FUNCTION(fn_unary), OBJ, 2u, { I32, OBJ }, 0u },
            { "compare", NEOS_RT_FUNCTION(fn_compare), OBJ, 3u, { I32, OBJ, OBJ }, 0u },
            { "is", NEOS_RT_FUNCTION(fn_is), BOOL, 2u, { OBJ, OBJ }, 0u },
            { "contains", NEOS_RT_FUNCTION(fn_contains), BOOL, 2u, { OBJ, OBJ }, 0u },
            { "getattr", NEOS_RT_FUNCTION(fn_getattr), OBJ, 2u, { OBJ, STR }, 0u },
            { "setattr", NEOS_RT_FUNCTION(fn_setattr), VOID, 3u, { OBJ, STR, OBJ }, 0u },
            { "call", NEOS_RT_FUNCTION(fn_call), OBJ, 3u, { OBJ, OBJ, OBJ }, 0u },
            { "getitem", NEOS_RT_FUNCTION(fn_getitem), OBJ, 2u, { OBJ, OBJ }, 0u },
            { "setitem", NEOS_RT_FUNCTION(fn_setitem), VOID, 3u, { OBJ, OBJ, OBJ }, 0u },
            { "delitem", NEOS_RT_FUNCTION(fn_delitem), VOID, 2u, { OBJ, OBJ }, 0u },
            { "len", NEOS_RT_FUNCTION(fn_len), I64, 1u, { OBJ }, 0u },
            { "iter", NEOS_RT_FUNCTION(fn_iter), OBJ, 1u, { OBJ }, 0u },
            { "next", NEOS_RT_FUNCTION(fn_next), OBJ, 1u, { OBJ }, 0u },
            { "tuple", NEOS_RT_FUNCTION(fn_tuple), OBJ, 1u, { I64 }, 0u },
            { "tuple_set", NEOS_RT_FUNCTION(fn_tuple_set), VOID, 3u, { OBJ, I64, OBJ }, 0u },
            { "list", NEOS_RT_FUNCTION(fn_list), OBJ, 0u, {}, 0u },
            { "list_append", NEOS_RT_FUNCTION(fn_list_append), VOID, 2u, { OBJ, OBJ }, 0u },
            { "dict", NEOS_RT_FUNCTION(fn_dict), OBJ, 0u, {}, 0u },
            { "dict_set", NEOS_RT_FUNCTION(fn_dict_set), VOID, 3u, { OBJ, OBJ, OBJ }, 0u },
            { "kwarg", NEOS_RT_FUNCTION(fn_kwarg), VOID, 3u, { OBJ, STR, OBJ }, 0u },
            { "slice", NEOS_RT_FUNCTION(fn_slice), OBJ, 3u, { OBJ, OBJ, OBJ }, 0u },
            { "range", NEOS_RT_FUNCTION(fn_range), OBJ, 3u, { I64, I64, I64 }, 0u },
            { "list_of", NEOS_RT_FUNCTION(fn_list_of), OBJ, 1u, { OBJ }, 0u },
            { "tuple_of", NEOS_RT_FUNCTION(fn_tuple_of), OBJ, 1u, { OBJ }, 0u },
            { "builtin", NEOS_RT_FUNCTION(fn_builtin), OBJ, 1u, { STR }, 0u },
            { "unpack", NEOS_RT_FUNCTION(fn_unpack), OBJ, 2u, { OBJ, I64 }, 0u },
            { "tuple_get", NEOS_RT_FUNCTION(fn_tuple_get), OBJ, 2u, { OBJ, I64 }, 0u },
            { "print(object)", NEOS_RT_FUNCTION(fn_print_object), VOID, 1u, { OBJ }, 0u },
            { "str(object)", NEOS_RT_FUNCTION(fn_str), STR, 1u, { OBJ }, 0u },
            { "repr(object)", NEOS_RT_FUNCTION(fn_repr), STR, 1u, { OBJ }, 0u },
            { "len(object)", NEOS_RT_FUNCTION(fn_len), I64, 1u, { OBJ }, 0u },
            { "int(object)", NEOS_RT_FUNCTION(fn_to_int), I64, 1u, { OBJ }, 0u },
            { "float(object)", NEOS_RT_FUNCTION(fn_to_float), F64, 1u, { OBJ }, 0u },
            { "abs(object)", NEOS_RT_FUNCTION(fn_abs_object), OBJ, 1u, { OBJ }, 0u },
            { "bool(object)", NEOS_RT_FUNCTION(fn_bool_object), BOOL, 1u, { OBJ }, 0u }
        };
        return sFunctions;
    }
}
