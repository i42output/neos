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
#include <map>
#include <optional>
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

        char const* exception_type_name(object* aObject); // (an exception's class: else null)
        char const* instance_class_name(object* aObject); // (an instance's (user) class: else null)

        char const* type_name(object* aObject)
        {
            if (aObject == nullptr)
                return "NULL";
            if (auto const name = exception_type_name(aObject))
                return name;
            if (auto const name = instance_class_name(aObject))
                return name;
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
        // (user classes: see below)
        std::map<std::string, std::vector<std::string>, std::less<>>& user_exception_bases();
        object* user_getattr(object* aSelf, std::vector<std::pair<std::string, object*>> const& aAttributes, char const* aName);
        int user_setattr(object* aSelf, std::vector<std::pair<std::string, object*>>& aAttributes, char const* aName, object* aValue);
        object* special_method(object* aObject, char const* aName);
        object* user_class_of(object* aObject);
        bool is_class(object* aObject);
        bool is_subclass(object* aClass, object* aBase);
        std::string const& as_class_name(object* aClass);
        object* percent_format(std::string const& aFormat, object* aValues);
        void raise_key_error(object* aKey);

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
            if (aOperation == NEOS_PY_REMAINDER && lk == NEOS_PY_STR)
                return percent_format(as<str_object>(aLhs)->value, aRhs);
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
                raise_key_error(aKey);
                return nullptr;
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
                raise_key_error(aKey);
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
                // (reverse: descending, equal elements keeping their order (as Python))
                ref const less{ compare(NEOS_PY_LT, reverse ? b : a, reverse ? a : b) };
                if (!less) { failure = true; return false; }
                auto const t = truth(less.get());
                if (t < 0) { failure = true; return false; }
                return t == 1;
            });
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

        bool format_string(std::string const& aFormat, std::vector<object*> const& aArgs, object* aKwargs, std::string& aResult, int aDepth);

        object* str_format(object* aSelf, std::vector<object*> const& aArgs, object* aKwargs)
        {
            std::string result;
            if (!format_string(as<str_object>(aSelf)->value, aArgs, aKwargs, result, 0))
                return nullptr;
            return make_str(std::move(result));
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
            { NEOS_PY_STR, "find", { str_find } },
            { NEOS_PY_STR, "format", { str_format } }
        };

        object* call_method(neos_py_context*, void* aData, neos_py_object* aArgs, neos_py_object* aKwargs)
        {
            // (a bound method: the function object's self is the first argument)
            auto const& args = tuple_items(aArgs);
            auto const* data = static_cast<method_entry const*>(aData);
            if (args.empty())
                return error("TypeError", "unbound method");
            std::vector<object*> rest{ args.begin() + 1, args.end() };
            if (aKwargs != nullptr && as<dict_object>(aKwargs)->items.size() != 0u && data->data.function != list_sort && data->data.function != str_format)
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

        // --- formatting: format(value, spec) (the format specification mini-language), str.format and str % values

        std::size_t code_point_count(std::string const& aText)
        {
            std::size_t n = 0u;
            for (auto c : aText)
                if ((static_cast<unsigned char>(c) & 0xC0u) != 0x80u)
                    ++n;
            return n;
        }

        struct format_spec
        {
            std::string fill;
            char align = 0;
            char sign = 0;
            bool z = false;
            bool alternate = false;
            bool zero = false;
            long long width = -1;
            char grouping = 0;
            long long precision = -1;
            char type = 0;
        };

        bool parse_format_spec(std::string const& aSpec, format_spec& aResult, char const* aTypeName)
        {
            auto const is_align = [](char c) { return c == '<' || c == '>' || c == '=' || c == '^'; };
            std::size_t i = 0u;
            // ([fill]align: the fill is one code point)
            std::size_t fillLength = 0u;
            if (!aSpec.empty())
            {
                auto const lead = static_cast<unsigned char>(aSpec[0]);
                fillLength = lead < 0x80u ? 1u : lead >= 0xF0u ? 4u : lead >= 0xE0u ? 3u : 2u;
            }
            if (fillLength != 0u && aSpec.size() > fillLength && is_align(aSpec[fillLength]))
            {
                aResult.fill = aSpec.substr(0u, fillLength);
                aResult.align = aSpec[fillLength];
                i = fillLength + 1u;
            }
            else if (!aSpec.empty() && is_align(aSpec[0]))
            {
                aResult.align = aSpec[0];
                i = 1u;
            }
            if (i < aSpec.size() && (aSpec[i] == '+' || aSpec[i] == '-' || aSpec[i] == ' '))
                aResult.sign = aSpec[i++];
            if (i < aSpec.size() && aSpec[i] == 'z')
            {
                aResult.z = true;
                ++i;
            }
            if (i < aSpec.size() && aSpec[i] == '#')
            {
                aResult.alternate = true;
                ++i;
            }
            if (i < aSpec.size() && aSpec[i] == '0')
            {
                aResult.zero = true;
                ++i;
            }
            auto const digits = [&](long long& aValue)
            {
                bool any = false;
                aValue = 0;
                while (i < aSpec.size() && aSpec[i] >= '0' && aSpec[i] <= '9')
                {
                    aValue = aValue * 10 + (aSpec[i++] - '0');
                    any = true;
                }
                return any;
            };
            long long width = 0;
            if (digits(width))
                aResult.width = width;
            if (i < aSpec.size() && (aSpec[i] == ',' || aSpec[i] == '_'))
                aResult.grouping = aSpec[i++];
            if (i < aSpec.size() && (aSpec[i] == ',' || aSpec[i] == '_'))
            {
                error("ValueError", "Cannot specify both ',' and '_'.");
                return false;
            }
            if (i < aSpec.size() && aSpec[i] == '.')
            {
                ++i;
                long long precision = 0;
                if (!digits(precision))
                {
                    error("ValueError", "Format specifier missing precision");
                    return false;
                }
                aResult.precision = precision;
            }
            if (i < aSpec.size())
                aResult.type = aSpec[i++];
            if (i < aSpec.size())
            {
                error("ValueError", "Invalid format specifier '" + aSpec + "' for object of type '" + aTypeName + "'");
                return false;
            }
            return true;
        }

        // (digits grouped with aSeparator every aEvery digits)
        std::string group_digits(std::string const& aDigits, char aSeparator, std::size_t aEvery)
        {
            if (aSeparator == 0)
                return aDigits;
            std::string result;
            auto const n = aDigits.size();
            for (std::size_t i = 0u; i < n; ++i)
            {
                if (i != 0u && (n - i) % aEvery == 0u)
                    result += aSeparator;
                result += aDigits[i];
            }
            return result;
        }

        // (the sign, then the body, aligned and padded)
        std::string pad_formatted(std::string const& aSign, std::string const& aBody, format_spec const& aSpec, char aDefaultAlign)
        {
            std::string fill = aSpec.fill.empty() ? " " : aSpec.fill;
            char align = aSpec.align;
            if (aSpec.zero)
            {
                if (aSpec.fill.empty())
                    fill = "0";
                if (align == 0)
                    align = '=';
            }
            if (align == 0)
                align = aDefaultAlign;
            auto const length = static_cast<long long>(code_point_count(aSign) + code_point_count(aBody));
            if (aSpec.width <= length)
                return aSign + aBody;
            auto const n = static_cast<std::size_t>(aSpec.width - length);
            auto const repeat = [&](std::size_t aCount) { std::string s; for (std::size_t k = 0u; k < aCount; ++k) s += fill; return s; };
            switch (align)
            {
            case '<': return aSign + aBody + repeat(n);
            case '^': return repeat(n / 2u) + aSign + aBody + repeat(n - n / 2u);
            case '=': return aSign + repeat(n) + aBody;
            default: return repeat(n) + aSign + aBody;
            }
        }

        std::string sign_of(bool aNegative, char aSign)
        {
            return aNegative ? "-" : aSign == '+' ? "+" : aSign == ' ' ? " " : "";
        }

        // (the digits of a double: to_chars in a format with a precision)
        std::string chars_of(double aValue, std::chars_format aFormat, int aPrecision)
        {
            char buffer[512];
            auto const converted = std::to_chars(buffer, buffer + sizeof(buffer), aValue, aFormat, aPrecision);
            if (converted.ec != std::errc{})
            {
                // (very large fixed: printf)
                std::vector<char> big(1024u + static_cast<std::size_t>(aPrecision));
                std::snprintf(big.data(), big.size(), aFormat == std::chars_format::fixed ? "%.*f" : "%.*e", aPrecision, aValue);
                return big.data();
            }
            return std::string{ buffer, static_cast<std::size_t>(converted.ptr - buffer) };
        }

        // (C's %g: aValue >= 0)
        std::string general_format(double aValue, long long aPrecision, bool aAlternate, bool aUpper)
        {
            auto const p = static_cast<int>(aPrecision == 0 ? 1 : aPrecision);
            auto const scientific = chars_of(aValue, std::chars_format::scientific, p - 1);
            auto const exponent = std::stoi(scientific.substr(scientific.find('e') + 1u));
            std::string result;
            if (exponent >= -4 && exponent < p)
                result = chars_of(aValue, std::chars_format::fixed, p - 1 - exponent);
            else
                result = scientific;
            if (!aAlternate)
            {
                auto const e = result.find('e');
                std::string mantissa = result.substr(0u, e);
                std::string const rest = e == std::string::npos ? std::string{} : result.substr(e);
                if (mantissa.find('.') != std::string::npos)
                {
                    while (!mantissa.empty() && mantissa.back() == '0')
                        mantissa.pop_back();
                    if (!mantissa.empty() && mantissa.back() == '.')
                        mantissa.pop_back();
                }
                result = mantissa + rest;
            }
            else if (result.find('.') == std::string::npos)
            {
                auto const e = result.find('e');
                result.insert(e == std::string::npos ? result.size() : e, ".");
            }
            if (aUpper)
                for (auto& c : result)
                    c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            return result;
        }

        bool format_float(double aValue, format_spec const& aSpec, std::string& aResult)
        {
            auto type = aSpec.type;
            if (type != 0 && std::string_view{ "eEfFgGn%" }.find(type) == std::string_view::npos)
            {
                error("ValueError", std::string{ "Unknown format code '" } + type + "' for object of type 'float'");
                return false;
            }
            if (aSpec.alternate && false)
                return false;
            bool const upper = type == 'E' || type == 'F' || type == 'G';
            bool negative = std::signbit(aValue);
            double x = std::fabs(aValue);
            std::string body;
            if (std::isnan(aValue) || std::isinf(aValue))
            {
                body = std::isnan(aValue) ? (upper ? "NAN" : "nan") : (upper ? "INF" : "inf");
                if (type == '%')
                    body += "%";
            }
            else
            {
                auto const precision = aSpec.precision < 0 ? 6 : aSpec.precision;
                switch (type)
                {
                case 'e':
                case 'E':
                    body = chars_of(x, std::chars_format::scientific, static_cast<int>(precision));
                    if (aSpec.alternate && precision == 0)
                        body.insert(body.find('e'), ".");
                    if (upper)
                        body[body.find('e')] = 'E';
                    break;
                case 'f':
                case 'F':
                case '%':
                    body = chars_of(type == '%' ? x * 100.0 : x, std::chars_format::fixed, static_cast<int>(precision));
                    if (aSpec.alternate && precision == 0)
                        body += ".";
                    if (type == '%')
                        body += "%";
                    break;
                case 'g':
                case 'G':
                case 'n':
                    body = general_format(x, precision, aSpec.alternate, upper);
                    break;
                default:
                    if (aSpec.precision < 0)
                        body = python_float(x);
                    else
                    {
                        body = general_format(x, aSpec.precision, aSpec.alternate, false);
                        if (body.find_first_of(".e") == std::string::npos)
                            body += ".0";
                    }
                    break;
                }
                if (aSpec.z && negative && body.find_first_not_of("0.%") == std::string::npos)
                    negative = false;
                if (aSpec.grouping != 0)
                {
                    auto const end = body.find_first_not_of("0123456789");
                    body = group_digits(body.substr(0u, end), aSpec.grouping, 3u) + (end == std::string::npos ? std::string{} : body.substr(end));
                }
            }
            aResult = pad_formatted(sign_of(negative, aSpec.sign), body, aSpec, '>');
            return true;
        }

        bool format_int(long long aValue, format_spec const& aSpec, std::string& aResult)
        {
            auto const type = aSpec.type;
            if (type == 'e' || type == 'E' || type == 'f' || type == 'F' || type == 'g' || type == 'G' || type == '%')
                return format_float(static_cast<double>(aValue), aSpec, aResult);
            if (type != 0 && std::string_view{ "bcdoxXn" }.find(type) == std::string_view::npos)
            {
                error("ValueError", std::string{ "Unknown format code '" } + type + "' for object of type 'int'");
                return false;
            }
            if (aSpec.precision >= 0)
            {
                error("ValueError", "Precision not allowed in integer format specifier");
                return false;
            }
            if (aSpec.grouping == ',' && type != 0 && type != 'd')
            {
                error("ValueError", std::string{ "Cannot specify ',' with '" } + type + "'.");
                return false;
            }
            if (type == 'c')
            {
                if (aSpec.sign != 0)
                {
                    error("ValueError", "Sign not allowed with integer format specifier 'c'");
                    return false;
                }
                if (aValue < 0 || aValue > 0x10FFFF)
                {
                    error("OverflowError", "%c arg not in range(0x110000)");
                    return false;
                }
                std::string body;
                auto const c = static_cast<std::uint32_t>(aValue);
                if (c < 0x80u) body += static_cast<char>(c);
                else if (c < 0x800u) { body += static_cast<char>(0xC0u | (c >> 6)); body += static_cast<char>(0x80u | (c & 0x3Fu)); }
                else if (c < 0x10000u) { body += static_cast<char>(0xE0u | (c >> 12)); body += static_cast<char>(0x80u | ((c >> 6) & 0x3Fu)); body += static_cast<char>(0x80u | (c & 0x3Fu)); }
                else { body += static_cast<char>(0xF0u | (c >> 18)); body += static_cast<char>(0x80u | ((c >> 12) & 0x3Fu)); body += static_cast<char>(0x80u | ((c >> 6) & 0x3Fu)); body += static_cast<char>(0x80u | (c & 0x3Fu)); }
                aResult = pad_formatted("", body, aSpec, '<');
                return true;
            }
            bool const negative = aValue < 0;
            auto const magnitude = negative ? 0ull - static_cast<unsigned long long>(aValue) : static_cast<unsigned long long>(aValue);
            int base = 10;
            std::string prefix;
            switch (type)
            {
            case 'b': base = 2; prefix = "0b"; break;
            case 'o': base = 8; prefix = "0o"; break;
            case 'x': base = 16; prefix = "0x"; break;
            case 'X': base = 16; prefix = "0X"; break;
            default: break;
            }
            char buffer[80];
            auto const converted = std::to_chars(buffer, buffer + sizeof(buffer), magnitude, base);
            std::string digits{ buffer, static_cast<std::size_t>(converted.ptr - buffer) };
            if (type == 'X')
                for (auto& c : digits)
                    c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            digits = group_digits(digits, aSpec.grouping, base == 10 ? 3u : 4u);
            auto const sign = sign_of(negative, aSpec.sign) + (aSpec.alternate ? prefix : std::string{});
            aResult = pad_formatted(sign, digits, aSpec, '>');
            return true;
        }

        bool format_str(std::string const& aValue, format_spec const& aSpec, std::string& aResult)
        {
            if (aSpec.type != 0 && aSpec.type != 's')
            {
                error("ValueError", std::string{ "Unknown format code '" } + aSpec.type + "' for object of type 'str'");
                return false;
            }
            if (aSpec.sign != 0)
            {
                error("ValueError", "Sign not allowed in string format specifier");
                return false;
            }
            if (aSpec.alternate)
            {
                error("ValueError", "Alternate form (#) not allowed in string format specifier");
                return false;
            }
            if (aSpec.align == '=')
            {
                error("ValueError", "'=' alignment not allowed in string format specifier");
                return false;
            }
            std::string body = aValue;
            if (aSpec.precision >= 0)
            {
                auto const points = code_points(aValue);
                body.clear();
                for (std::size_t i = 0u; i < points.size() && static_cast<long long>(i) < aSpec.precision; ++i)
                    body += points[i];
            }
            auto spec = aSpec;
            if (spec.zero && spec.align == 0)
                spec.align = '<';
            aResult = pad_formatted("", body, spec, '<');
            return true;
        }

        // format(value, spec)
        bool format_value(object* aValue, std::string const& aSpec, std::string& aResult)
        {
            auto const k = kind_of(aValue);
            if (aSpec.empty())
                return str_of(aValue, aResult);
            format_spec spec;
            switch (k)
            {
            case NEOS_PY_BOOL:
            case NEOS_PY_INT:
                return parse_format_spec(aSpec, spec, "int") && format_int(int_value(aValue), spec, aResult);
            case NEOS_PY_FLOAT:
                return parse_format_spec(aSpec, spec, "float") && format_float(as<float_object>(aValue)->value, spec, aResult);
            case NEOS_PY_STR:
                return parse_format_spec(aSpec, spec, "str") && format_str(as<str_object>(aValue)->value, spec, aResult);
            case NEOS_PY_OTHER:
                {
                    // (another plugin's object: an index is an int, else a float (as NumPy's scalars))
                    auto const type = python_type(aValue);
                    if (!parse_format_spec(aSpec, spec, type_name(aValue)))
                        return false;
                    bool const integral = spec.type != 0 && std::string_view{ "bcdoxX" }.find(spec.type) != std::string_view::npos;
                    if (type != nullptr && type->index != nullptr && (integral || spec.type == 0))
                    {
                        long long value = 0;
                        if (type->index(context(), aValue, &value) == 0)
                            return format_int(value, spec, aResult);
                        clear_error();
                    }
                    if (type != nullptr && type->to_float != nullptr && !integral)
                    {
                        double value = 0.0;
                        if (type->to_float(context(), aValue, &value) == 0)
                            return format_float(value, spec, aResult);
                        clear_error();
                    }
                    std::string text;
                    if (!str_of(aValue, text))
                        return false;
                    if (spec.type == 0 || spec.type == 's')
                        return format_str(text, spec, aResult);
                    error("TypeError", std::string{ "unsupported format string passed to " } + type_name(aValue) + ".__format__");
                    return false;
                }
            default:
                error("TypeError", std::string{ "unsupported format string passed to " } + type_name(aValue) + ".__format__");
                return false;
            }
        }

        object* getitem(object* aObject, object* aKey);
        object* getattr(object* aObject, char const* aName);

        // (a value converted by !r, !s or !a)
        bool convert_field(object* aValue, char aConversion, std::string const& aSpec, std::string& aResult)
        {
            if (aConversion == 0)
                return format_value(aValue, aSpec, aResult);
            std::string text;
            if (aConversion == 'r' || aConversion == 'a')
            {
                if (!repr_of(aValue, text))
                    return false;
                if (aConversion == 'a')
                {
                    std::string ascii;
                    for (auto const& point : code_points(text))
                    {
                        if (point.size() == 1u)
                        {
                            ascii += point;
                            continue;
                        }
                        std::uint32_t c = static_cast<unsigned char>(point[0]) & (point.size() == 2u ? 0x1Fu : point.size() == 3u ? 0x0Fu : 0x07u);
                        for (std::size_t i = 1u; i < point.size(); ++i)
                            c = (c << 6) | (static_cast<unsigned char>(point[i]) & 0x3Fu);
                        char buffer[16];
                        std::snprintf(buffer, sizeof(buffer), c <= 0xFFu ? "\\x%02x" : c <= 0xFFFFu ? "\\u%04x" : "\\U%08x", c);
                        ascii += buffer;
                    }
                    text = ascii;
                }
            }
            else if (aConversion == 's')
            {
                if (!str_of(aValue, text))
                    return false;
            }
            else
            {
                error("ValueError", std::string{ "Unknown conversion specifier " } + aConversion);
                return false;
            }
            ref const converted{ make_str(text) };
            return format_value(converted.get(), aSpec, aResult);
        }

        // str.format
        bool format_string(std::string const& aFormat, std::vector<object*> const& aArgs, object* aKwargs, std::string& aResult, int aDepth = 0)
        {
            if (aDepth > 2)
            {
                error("ValueError", "Max string recursion exceeded");
                return false;
            }
            std::size_t automatic = 0u;
            bool usedAutomatic = false, usedManual = false;
            std::size_t i = 0u;
            auto const n = aFormat.size();
            while (i < n)
            {
                auto const c = aFormat[i];
                if (c == '}')
                {
                    if (i + 1u < n && aFormat[i + 1u] == '}')
                    {
                        aResult += '}';
                        i += 2u;
                        continue;
                    }
                    error("ValueError", "Single '}' encountered in format string");
                    return false;
                }
                if (c != '{')
                {
                    aResult += c;
                    ++i;
                    continue;
                }
                if (i + 1u < n && aFormat[i + 1u] == '{')
                {
                    aResult += '{';
                    i += 2u;
                    continue;
                }
                // (a replacement field: to its matching '}')
                std::size_t depth = 1u, j = i + 1u;
                for (; j < n && depth != 0u; ++j)
                {
                    if (aFormat[j] == '{') ++depth;
                    else if (aFormat[j] == '}') --depth;
                }
                if (depth != 0u)
                {
                    error("ValueError", "expected '}' before end of string");
                    return false;
                }
                auto const field = aFormat.substr(i + 1u, j - i - 2u);
                i = j;
                // field_name [!conversion] [:spec]
                std::size_t nameEnd = 0u;
                for (std::size_t bracket = 0u; nameEnd < field.size(); ++nameEnd)
                {
                    if (field[nameEnd] == '[') ++bracket;
                    else if (field[nameEnd] == ']') --bracket;
                    else if (bracket == 0u && (field[nameEnd] == '!' || field[nameEnd] == ':'))
                        break;
                }
                auto const name = field.substr(0u, nameEnd);
                char conversion = 0;
                std::string spec;
                std::size_t k = nameEnd;
                if (k < field.size() && field[k] == '!')
                {
                    if (k + 1u >= field.size())
                    {
                        error("ValueError", "end of string while looking for conversion specifier");
                        return false;
                    }
                    conversion = field[k + 1u];
                    k += 2u;
                    if (k < field.size() && field[k] != ':')
                    {
                        error("ValueError", "expected ':' after conversion specifier");
                        return false;
                    }
                }
                if (k < field.size() && field[k] == ':')
                {
                    std::string nested;
                    if (!format_string(field.substr(k + 1u), aArgs, aKwargs, nested, aDepth + 1))
                        return false;
                    spec = nested;
                }
                // (the first part of the name: an index, a keyword or automatic numbering)
                auto const first = name.find_first_of(".[");
                auto const head = name.substr(0u, first);
                ref value;
                if (head.empty() || std::all_of(head.begin(), head.end(), [](char d) { return d >= '0' && d <= '9'; }))
                {
                    std::size_t index;
                    if (head.empty())
                    {
                        if (usedManual)
                        {
                            error("ValueError", "cannot switch from manual field specification to automatic field numbering");
                            return false;
                        }
                        usedAutomatic = true;
                        index = automatic++;
                    }
                    else
                    {
                        if (usedAutomatic)
                        {
                            error("ValueError", "cannot switch from automatic field numbering to manual field specification");
                            return false;
                        }
                        usedManual = true;
                        index = static_cast<std::size_t>(std::stoull(head));
                    }
                    if (index >= aArgs.size())
                    {
                        error("IndexError", "Replacement index " + std::to_string(index) + " out of range for positional args tuple");
                        return false;
                    }
                    value = ref::borrowed(aArgs[index]);
                }
                else
                {
                    object* found = nullptr;
                    if (aKwargs != nullptr)
                        for (auto const& item : as<dict_object>(aKwargs)->items)
                            if (kind_of(item.first) == NEOS_PY_STR && as<str_object>(item.first)->value == head)
                                found = item.second;
                    if (found == nullptr)
                    {
                        error("KeyError", "'" + head + "'");
                        return false;
                    }
                    value = ref::borrowed(found);
                }
                // (.attribute and [key])
                for (std::size_t p = first; p != std::string::npos && p < name.size();)
                {
                    if (name[p] == '.')
                    {
                        auto const end = name.find_first_of(".[", p + 1u);
                        auto const attribute = name.substr(p + 1u, end == std::string::npos ? std::string::npos : end - p - 1u);
                        value = ref{ getattr(value.get(), attribute.c_str()) };
                        p = end;
                    }
                    else
                    {
                        auto const end = name.find(']', p);
                        auto const keyText = name.substr(p + 1u, end - p - 1u);
                        ref const key{ !keyText.empty() && std::all_of(keyText.begin(), keyText.end(), [](char d) { return d >= '0' && d <= '9'; }) ?
                            make_int(std::stoll(keyText)) : make_str(keyText) };
                        value = ref{ getitem(value.get(), key.get()) };
                        p = end == std::string::npos ? end : end + 1u;
                    }
                    if (!value)
                        return false;
                }
                std::string text;
                if (!convert_field(value.get(), conversion, spec, text))
                    return false;
                aResult += text;
            }
            return true;
        }

        // str % values
        object* percent_format(std::string const& aFormat, object* aValues)
        {
            std::vector<object*> values;
            bool const mapping = kind_of(aValues) == NEOS_PY_DICT;
            if (kind_of(aValues) == NEOS_PY_TUPLE)
                values = as<tuple_object>(aValues)->items;
            else
                values.push_back(aValues);
            std::size_t next = 0u;
            bool usedMapping = false;
            std::string result;
            auto const n = aFormat.size();
            auto const next_value = [&]() -> object*
            {
                if (next >= values.size())
                {
                    error("TypeError", "not enough arguments for format string");
                    return nullptr;
                }
                return values[next++];
            };
            for (std::size_t i = 0u; i < n;)
            {
                if (aFormat[i] != '%')
                {
                    result += aFormat[i++];
                    continue;
                }
                auto const start = i++;
                if (i >= n)
                    return error("ValueError", "incomplete format");
                ref keyed;
                if (aFormat[i] == '(')
                {
                    if (!mapping)
                        return error("TypeError", "format requires a mapping");
                    std::size_t depth = 1u, j = i + 1u;
                    for (; j < n && depth != 0u; ++j)
                    {
                        if (aFormat[j] == '(') ++depth;
                        else if (aFormat[j] == ')') --depth;
                    }
                    if (depth != 0u)
                        return error("ValueError", "incomplete format key");
                    ref const key{ make_str(aFormat.substr(i + 1u, j - i - 2u)) };
                    keyed = ref{ getitem(aValues, key.get()) };
                    if (!keyed)
                        return nullptr;
                    usedMapping = true;
                    i = j;
                }
                bool left = false, plus = false, space = false, alternate = false, zero = false;
                for (; i < n; ++i)
                {
                    auto const f = aFormat[i];
                    if (f == '-') left = true;
                    else if (f == '+') plus = true;
                    else if (f == ' ') space = true;
                    else if (f == '#') alternate = true;
                    else if (f == '0') zero = true;
                    else break;
                }
                long long width = -1, precision = -1;
                auto const number = [&](long long& aResult) -> bool
                {
                    if (i < n && aFormat[i] == '*')
                    {
                        ++i;
                        auto const v = next_value();
                        if (v == nullptr)
                            return false;
                        if (!is_int_like(v))
                        {
                            error("TypeError", "* wants int");
                            return false;
                        }
                        aResult = int_value(v);
                        return true;
                    }
                    if (i < n && aFormat[i] >= '0' && aFormat[i] <= '9')
                    {
                        aResult = 0;
                        while (i < n && aFormat[i] >= '0' && aFormat[i] <= '9')
                            aResult = aResult * 10 + (aFormat[i++] - '0');
                    }
                    return true;
                };
                if (!number(width))
                    return nullptr;
                if (width < -1)
                {
                    left = true;
                    width = -width;
                }
                if (i < n && aFormat[i] == '.')
                {
                    ++i;
                    precision = 0;
                    if (!number(precision))
                        return nullptr;
                }
                while (i < n && (aFormat[i] == 'h' || aFormat[i] == 'l' || aFormat[i] == 'L'))
                    ++i;
                if (i >= n)
                    return error("ValueError", "incomplete format");
                auto const type = aFormat[i++];
                if (type == '%')
                {
                    result += '%';
                    continue;
                }
                object* value = keyed ? keyed.get() : next_value();
                if (value == nullptr)
                    return nullptr;
                std::string sign, body;
                bool numeric = true;
                switch (type)
                {
                case 's':
                case 'r':
                case 'a':
                    {
                        numeric = false;
                        if (!convert_field(value, type == 's' ? 's' : type, "", body))
                            return nullptr;
                        if (precision >= 0)
                        {
                            auto const points = code_points(body);
                            std::string truncated;
                            for (std::size_t p = 0u; p < points.size() && static_cast<long long>(p) < precision; ++p)
                                truncated += points[p];
                            body = truncated;
                        }
                    }
                    break;
                case 'c':
                    {
                        numeric = false;
                        if (kind_of(value) == NEOS_PY_STR)
                        {
                            if (code_point_count(as<str_object>(value)->value) != 1u)
                                return error("TypeError", "%c requires an int or a unicode character, not a string of length " +
                                    std::to_string(code_point_count(as<str_object>(value)->value)));
                            body = as<str_object>(value)->value;
                        }
                        else
                        {
                            long long c = 0;
                            if (!to_index(value, c))
                            {
                                clear_error();
                                return error("TypeError", std::string{ "%c requires an int or a unicode character, not " } + type_name(value));
                            }
                            format_spec spec;
                            spec.type = 'c';
                            if (!format_int(c, spec, body))
                                return nullptr;
                        }
                    }
                    break;
                case 'd':
                case 'i':
                case 'u':
                case 'o':
                case 'x':
                case 'X':
                    {
                        long long v = 0;
                        if (is_int_like(value))
                            v = int_value(value);
                        else if ((type == 'd' || type == 'i' || type == 'u') && (kind_of(value) == NEOS_PY_FLOAT || kind_of(value) == NEOS_PY_OTHER))
                        {
                            double d = 0.0;
                            if (kind_of(value) == NEOS_PY_OTHER && to_index(value, v))
                                ;
                            else
                            {
                                clear_error();
                                if (!to_double(value, d))
                                {
                                    clear_error();
                                    return error("TypeError", std::string{ "%" } + type + " format: a real number is required, not " + type_name(value));
                                }
                                v = static_cast<long long>(std::trunc(d));
                            }
                        }
                        else if (!to_index(value, v))
                        {
                            clear_error();
                            return error("TypeError", std::string{ "%" } + type + " format: " + (type == 'd' || type == 'i' || type == 'u' ? "a real number" : "an integer") +
                                " is required, not " + type_name(value));
                        }
                        bool const negative = v < 0;
                        auto const magnitude = negative ? 0ull - static_cast<unsigned long long>(v) : static_cast<unsigned long long>(v);
                        char buffer[80];
                        auto const base = type == 'o' ? 8 : (type == 'x' || type == 'X') ? 16 : 10;
                        auto const converted = std::to_chars(buffer, buffer + sizeof(buffer), magnitude, base);
                        body.assign(buffer, converted.ptr);
                        if (type == 'X')
                            for (auto& c : body)
                                c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
                        if (precision >= 0 && static_cast<long long>(body.size()) < precision)
                            body.insert(0u, static_cast<std::size_t>(precision) - body.size(), '0');
                        sign = negative ? "-" : plus ? "+" : space ? " " : "";
                        if (alternate && base != 10)
                            sign += type == 'o' ? "0o" : type == 'x' ? "0x" : "0X";
                    }
                    break;
                case 'e':
                case 'E':
                case 'f':
                case 'F':
                case 'g':
                case 'G':
                    {
                        double d = 0.0;
                        if (!to_double(value, d))
                        {
                            clear_error();
                            return error("TypeError", std::string{ "must be real number, not " } + type_name(value));
                        }
                        format_spec spec;
                        spec.type = type;
                        spec.precision = precision < 0 ? 6 : precision;
                        spec.alternate = alternate;
                        std::string text;
                        if (!format_float(std::fabs(d), spec, text))
                            return nullptr;
                        body = text;
                        sign = std::signbit(d) && !std::isnan(d) ? "-" : plus ? "+" : space ? " " : "";
                    }
                    break;
                default:
                    {
                        char buffer[96];
                        std::snprintf(buffer, sizeof(buffer), "unsupported format character '%c' (0x%x) at index %zu", type, static_cast<unsigned>(static_cast<unsigned char>(type)), i - 1u);
                        (void)start;
                        return error("ValueError", buffer);
                    }
                }
                auto const length = static_cast<long long>(code_point_count(sign) + code_point_count(body));
                if (width > length)
                {
                    auto const padding = static_cast<std::size_t>(width - length);
                    if (left)
                        body = sign + body + std::string(padding, ' ');
                    else if (zero && numeric)
                        body = sign + std::string(padding, '0') + body;
                    else
                        body = std::string(padding, ' ') + sign + body;
                    sign.clear();
                }
                result += sign + body;
            }
            if (!mapping && next < values.size())
                return error("TypeError", "not all arguments converted during string formatting");
            (void)usedMapping;
            return make_str(std::move(result));
        }

        // --- functions of the program (a function used as a value): calls are made through the host

        struct script_parameter
        {
            enum kind_t { Positional, KeywordOnly, VarArgs, VarKeywords } kind = Positional;
            std::string name;
            char type = 'O';        // O(bject), I(nt), F(loat), B(ool), S(tr)
            object* defaultValue = nullptr;
        };
        struct script_function_object
        {
            header h;
            std::string name;
            unsigned index;
            std::vector<script_parameter> parameters;
            char result = 'O';      // (V: none)
            ~script_function_object() { for (auto& p : parameters) release(p.defaultValue); }
        };

        object* script_function_repr(neos_py_context*, object* aSelf)
        {
            char address[32];
            std::snprintf(address, sizeof(address), "%p", static_cast<void*>(aSelf));
            return make_str("<function " + as<script_function_object>(aSelf)->name + " at " + address + ">");
        }

        object* script_function_getattr(neos_py_context*, object* aSelf, char const* aName)
        {
            auto const& f = *as<script_function_object>(aSelf);
            if (std::strcmp(aName, "__name__") == 0 || std::strcmp(aName, "__qualname__") == 0)
                return make_str(f.name);
            if (std::strcmp(aName, "__module__") == 0)
                return make_str("__main__");
            return error("AttributeError", "'function' object has no attribute '" + std::string{ aName } + "'");
        }

        object* script_function_call(neos_py_context*, object* aSelf, object* aArgs, object* aKwargs)
        {
            auto const& f = *as<script_function_object>(aSelf);
            auto const& args = tuple_items(aArgs);
            auto const& parameters = f.parameters;
            std::vector<object*> bound(parameters.size(), nullptr);
            std::vector<ref> owned;
            std::vector<object*> extra;
            std::vector<std::pair<object*, object*>> extraKeywords;
            std::size_t positional = 0u, required = 0u;
            std::optional<std::size_t> varArgs, varKeywords;
            for (std::size_t p = 0u; p < parameters.size(); ++p)
            {
                if (parameters[p].kind == script_parameter::Positional)
                {
                    ++positional;
                    if (parameters[p].defaultValue == nullptr)
                        ++required;
                }
                else if (parameters[p].kind == script_parameter::VarArgs)
                    varArgs = p;
                else if (parameters[p].kind == script_parameter::VarKeywords)
                    varKeywords = p;
            }
            std::size_t next = 0u;
            for (auto a : args)
            {
                if (next < parameters.size() && parameters[next].kind == script_parameter::Positional)
                    bound[next++] = a;
                else if (varArgs)
                    extra.push_back(a);
                else
                {
                    std::string const takes = required == positional ? std::to_string(positional) : "from " + std::to_string(required) + " to " + std::to_string(positional);
                    return error("TypeError", f.name + "() takes " + takes + " positional argument" + (positional == 1u && required == positional ? "" : "s") + " but " +
                        std::to_string(args.size()) + (args.size() == 1u ? " was" : " were") + " given");
                }
            }
            if (aKwargs != nullptr)
                for (auto const& item : as<dict_object>(aKwargs)->items)
                {
                    auto const& key = as<str_object>(item.first)->value;
                    std::optional<std::size_t> slot;
                    for (std::size_t p = 0u; p < parameters.size(); ++p)
                        if ((parameters[p].kind == script_parameter::Positional || parameters[p].kind == script_parameter::KeywordOnly) && parameters[p].name == key)
                            slot = p;
                    if (slot)
                    {
                        if (bound[*slot] != nullptr)
                            return error("TypeError", f.name + "() got multiple values for argument '" + key + "'");
                        bound[*slot] = item.second;
                    }
                    else if (varKeywords)
                        extraKeywords.push_back(item);
                    else
                        return error("TypeError", f.name + "() got an unexpected keyword argument '" + key + "'");
                }
            std::vector<std::string> missing;
            bool missingKeywordOnly = false;
            for (std::size_t p = 0u; p < parameters.size(); ++p)
            {
                auto const& parameter = parameters[p];
                if (parameter.kind == script_parameter::VarArgs)
                {
                    for (auto x : extra)
                        retain(x);
                    owned.emplace_back(make_tuple(extra));
                    bound[p] = owned.back().get();
                }
                else if (parameter.kind == script_parameter::VarKeywords)
                {
                    owned.emplace_back(make_dict());
                    for (auto const& item : extraKeywords)
                        dict_set(owned.back().get(), item.first, item.second);
                    bound[p] = owned.back().get();
                }
                else if (bound[p] == nullptr)
                {
                    if (parameter.defaultValue != nullptr)
                        bound[p] = parameter.defaultValue;
                    else
                    {
                        missing.push_back(parameter.name);
                        missingKeywordOnly = parameter.kind == script_parameter::KeywordOnly;
                    }
                }
            }
            if (!missing.empty())
            {
                std::string names;
                for (std::size_t i = 0u; i < missing.size(); ++i)
                    names += (i == 0u ? "'" : i + 1u == missing.size() ? (missing.size() > 2u ? "', and '" : "' and '") : "', '") + missing[i];
                return error("TypeError", f.name + "() missing " + std::to_string(missing.size()) + " required " + (missingKeywordOnly ? "keyword-only" : "positional") +
                    " argument" + (missing.size() == 1u ? "" : "s") + ": " + names + "'");
            }
            // (the arguments as the function's parameter types)
            std::vector<neos_rt_type> types;
            std::vector<neos_rt_value> values;
            std::vector<std::string> strings;
            strings.reserve(parameters.size());
            for (std::size_t p = 0u; p < parameters.size(); ++p)
            {
                neos_rt_value v{};
                switch (parameters[p].type)
                {
                case 'I':
                    {
                        long long i = 0;
                        if (!to_index(bound[p], i))
                            return nullptr;
                        v.i64 = i;
                        types.push_back(NEOS_RT_I64);
                    }
                    break;
                case 'F':
                    if (!to_double(bound[p], v.f64))
                        return nullptr;
                    types.push_back(NEOS_RT_F64);
                    break;
                case 'B':
                    {
                        auto const t = truth(bound[p]);
                        if (t < 0)
                            return nullptr;
                        v.b = t;
                        types.push_back(NEOS_RT_BOOL);
                    }
                    break;
                case 'S':
                    strings.emplace_back();
                    if (!str_of(bound[p], strings.back()))
                        return nullptr;
                    v.string = strings.back().c_str();
                    types.push_back(NEOS_RT_STRING);
                    break;
                default:
                    v.object = bound[p];
                    types.push_back(NEOS_RT_OBJECT);
                    break;
                }
                values.push_back(v);
            }
            auto const* host = python_runtime::host();
            if (host == nullptr)
                return error("SystemError", "functions of the program cannot be called");
            neos_rt_type const resultType = f.result == 'I' ? NEOS_RT_I64 : f.result == 'F' ? NEOS_RT_F64 : f.result == 'B' ? NEOS_RT_BOOL :
                f.result == 'S' ? NEOS_RT_STRING : f.result == 'V' ? NEOS_RT_VOID : NEOS_RT_OBJECT;
            neos_rt_value r{};
            if (auto const message = host->call(f.index, static_cast<unsigned>(values.size()), types.data(), values.data(), resultType, &r))
            {
                std::string const text = message;
                auto const colon = text.find(": ");
                if (colon == std::string::npos)
                    python_runtime::raise(text, "");
                else
                    python_runtime::raise(text.substr(0u, colon), text.substr(colon + 2u));
                return nullptr;
            }
            switch (f.result)
            {
            case 'I': return make_int(r.i64);
            case 'F': return make_float(r.f64);
            case 'B': return make_bool(r.b != 0);
            case 'S': return make_str(r.string != nullptr ? r.string : "");
            case 'V': return none();
            default: return r.object != nullptr ? r.object : none();
            }
        }

        neos_py_type make_script_function_type()
        {
            auto type = make_type("python.function", "function", retain_object, release_object<script_function_object>);
            type.repr = script_function_repr;
            type.getattr = script_function_getattr;
            type.call = script_function_call;
            return type;
        }

        neos_py_type sScriptFunctionType = make_script_function_type();

        // python::function(name, index, signature, defaults): signature: "[*|**]name:T[=],...;R" (T: O, I, F, B or S; R also V)
        object* fn_function(char const* aName, std::int32_t aIndex, char const* aSignature, object* aDefaults)
        {
            auto* const f = new script_function_object{ { { &sScriptFunctionType.base } }, aName, static_cast<unsigned>(aIndex), {}, 'O' };
            std::string_view const signature{ aSignature };
            auto const semicolon = signature.rfind(';');
            auto const list = signature.substr(0u, semicolon);
            if (semicolon != std::string_view::npos && semicolon + 1u < signature.size())
                f->result = signature[semicolon + 1u];
            std::size_t nextDefault = 0u;
            auto const& defaults = aDefaults != nullptr && kind_of(aDefaults) == NEOS_PY_TUPLE ? tuple_items(aDefaults) : std::vector<object*>{};
            bool keywordOnly = false;
            std::size_t start = 0u;
            while (start < list.size())
            {
                auto end = list.find(',', start);
                if (end == std::string_view::npos)
                    end = list.size();
                auto item = list.substr(start, end - start);
                start = end + 1u;
                if (item == "*")
                {
                    keywordOnly = true;
                    continue;
                }
                script_parameter p;
                if (item.starts_with("**"))
                {
                    p.kind = script_parameter::VarKeywords;
                    item.remove_prefix(2u);
                }
                else if (item.starts_with("*"))
                {
                    p.kind = script_parameter::VarArgs;
                    keywordOnly = true;
                    item.remove_prefix(1u);
                }
                else if (keywordOnly)
                    p.kind = script_parameter::KeywordOnly;
                bool const hasDefault = item.ends_with("=");
                if (hasDefault)
                    item.remove_suffix(1u);
                auto const colon = item.find(':');
                p.name = std::string{ item.substr(0u, colon) };
                if (colon != std::string_view::npos && colon + 1u < item.size())
                    p.type = item[colon + 1u];
                if (hasDefault && nextDefault < defaults.size())
                {
                    p.defaultValue = defaults[nextDefault++];
                    retain(p.defaultValue);
                }
                f->parameters.push_back(p);
            }
            return &f->h.base;
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
        object* fn_from_complex(double aReal, double aImag) { return make_complex(aReal, aImag); }
        bool fn_truth(object* aObject) { return truth(aObject) == 1; }
        char const* fn_str(object* aObject)
        {
            std::string text;
            if (!str_of(aObject, text))
                return "";
            return result(std::move(text));
        }
        // (an f-string's replacement field: the value converted (aConversion: 0, 'r', 's' or 'a') and formatted)
        char const* fn_format_field(object* aObject, std::int32_t aConversion, char const* aSpec)
        {
            std::string text;
            if (!convert_field(aObject, static_cast<char>(aConversion), aSpec, text))
                return "";
            return result(std::move(text));
        }

        char const* fn_fstring(object* aObject, std::int64_t aConversion, char const* aSpec)
        {
            return fn_format_field(aObject, static_cast<std::int32_t>(aConversion), aSpec);
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
        object* exception_class(std::string const& aName);

        object* class_of(object* aObject)
        {
            if (auto const cls = user_class_of(aObject))
            {
                retain(cls);
                return cls;
            }
            if (is_type_object(aObject) || is_class(aObject))
                return make_type_object("type", "type");
            if (auto const name = exception_type_name(aObject))
                return exception_class(name);
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

        // --- exceptions (instances of BaseException's classes)

        struct exception_object
        {
            header h;
            std::string type;
            std::vector<object*> args;
            bool fromText = false;  ///< (raised by a plugin: its message is its str())
            object* cls = nullptr;  ///< (a user exception class)
            std::vector<std::pair<std::string, object*>> attributes;
            ~exception_object() { for (auto a : args) release(a); release(cls); for (auto& a : attributes) release(a.second); }
        };


        // (the classes' bases)
        std::map<std::string, std::vector<std::string>, std::less<>> const& exception_bases()
        {
            static std::map<std::string, std::vector<std::string>, std::less<>> const sBases =
            {
                { "BaseException", {} }, { "Exception", { "BaseException" } }, { "SystemExit", { "BaseException" } },
                { "KeyboardInterrupt", { "BaseException" } }, { "GeneratorExit", { "BaseException" } },
                { "ArithmeticError", { "Exception" } }, { "ZeroDivisionError", { "ArithmeticError" } }, { "OverflowError", { "ArithmeticError" } },
                { "FloatingPointError", { "ArithmeticError" } }, { "LookupError", { "Exception" } }, { "IndexError", { "LookupError" } },
                { "KeyError", { "LookupError" } }, { "ValueError", { "Exception" } }, { "UnicodeError", { "ValueError" } },
                { "TypeError", { "Exception" } }, { "AttributeError", { "Exception" } }, { "NameError", { "Exception" } },
                { "UnboundLocalError", { "NameError" } }, { "AssertionError", { "Exception" } }, { "StopIteration", { "Exception" } },
                { "ImportError", { "Exception" } }, { "ModuleNotFoundError", { "ImportError" } }, { "RuntimeError", { "Exception" } },
                { "NotImplementedError", { "RuntimeError" } }, { "RecursionError", { "RuntimeError" } }, { "OSError", { "Exception" } },
                { "FileNotFoundError", { "OSError" } }, { "EOFError", { "Exception" } }, { "MemoryError", { "Exception" } },
                { "SystemError", { "Exception" } }, { "BufferError", { "Exception" } }, { "Warning", { "Exception" } },
                { "UserWarning", { "Warning" } }, { "DeprecationWarning", { "Warning" } }, { "RuntimeWarning", { "Warning" } },
                // (NumPy's)
                { "AxisError", { "ValueError", "IndexError" } }, { "UFuncTypeError", { "TypeError" } }, { "LinAlgError", { "ValueError" } },
                { "ComplexWarning", { "RuntimeWarning" } }, { "DTypePromotionError", { "TypeError" } }
            };
            return sBases;
        }

        bool is_exception_class(std::string_view aName)
        {
            return exception_bases().find(aName) != exception_bases().end() || user_exception_bases().find(aName) != user_exception_bases().end();
        }

        bool is_exception_subclass(std::string_view aClass, std::string_view aBase)
        {
            if (aClass == aBase)
                return true;
            auto bases = exception_bases().find(aClass);
            if (bases == exception_bases().end())
                bases = user_exception_bases().find(aClass);
            if (bases == exception_bases().end() || bases == user_exception_bases().end())
            {
                if (auto const user = user_exception_bases().find(aClass); user != user_exception_bases().end())
                {
                    for (auto const& b : user->second)
                        if (is_exception_subclass(b, aBase))
                            return true;
                    return false;
                }
                return aBase == "Exception" || aBase == "BaseException";    // (an exception of another kind)
            }
            for (auto const& b : bases->second)
                if (is_exception_subclass(b, aBase))
                    return true;
            return false;
        }

        object* exception_repr(neos_py_context*, object* aSelf);
        object* exception_str(neos_py_context*, object* aSelf);
        object* exception_getattr(neos_py_context*, object* aSelf, char const* aName);
        int exception_setattr(neos_py_context*, object* aSelf, char const* aName, object* aValue);

        neos_py_type make_exception_type()
        {
            auto type = make_type("python.exception", "exception", retain_object, release_object<exception_object>);
            type.repr = exception_repr;
            type.str = exception_str;
            type.getattr = exception_getattr;
            type.setattr = exception_setattr;
            return type;
        }

        neos_py_type sExceptionType = make_exception_type();

        char const* exception_type_name(object* aObject)
        {
            if (aObject == nullptr || aObject->type != &sExceptionType.base)
                return nullptr;
            return as<exception_object>(aObject)->type.c_str();
        }

        object* make_exception(std::string aType, std::vector<object*> aArgs) // (steals the arguments)
        {
            return &(new exception_object{ { { &sExceptionType.base } }, std::move(aType), std::move(aArgs) })->h.base;
        }

        object* exception_repr(neos_py_context*, object* aSelf)
        {
            auto const& e = *as<exception_object>(aSelf);
            if (e.cls != nullptr)
                if (ref const method{ special_method(aSelf, "__repr__") })
                {
                    ref const args{ make_tuple() };
                    return call(method.get(), args.get(), nullptr);
                }
            std::string text = e.type + "(";
            for (std::size_t i = 0u; i < e.args.size(); ++i)
            {
                std::string r;
                if (!repr_of(e.args[i], r))
                    return nullptr;
                text += (i != 0u ? ", " : "") + r;
            }
            return make_str(text + ")");
        }

        bool exception_text(object* aException, std::string& aResult)
        {
            auto const& e = *as<exception_object>(aException);
            if (e.cls != nullptr)
                if (ref const method{ special_method(aException, "__str__") })
                {
                    ref const args{ make_tuple() };
                    ref const text{ call(method.get(), args.get(), nullptr) };
                    if (!text)
                        return false;
                    return str_of(text.get(), aResult);
                }
            if (e.args.empty())
            {
                aResult.clear();
                return true;
            }
            if (e.args.size() == 1u)
            {
                // (KeyError: the repr of its key)
                if (!e.fromText && is_exception_subclass(e.type, "KeyError"))
                    return repr_of(e.args[0], aResult);
                return str_of(e.args[0], aResult);
            }
            for (auto a : e.args)
                retain(a);
            ref const t{ make_tuple(e.args) };
            return str_of(t.get(), aResult);
        }

        object* exception_str(neos_py_context*, object* aSelf)
        {
            std::string text;
            if (!exception_text(aSelf, text))
                return nullptr;
            return make_str(text);
        }

        object* exception_getattr(neos_py_context*, object* aSelf, char const* aName)
        {
            auto const& e = *as<exception_object>(aSelf);
            if (std::strcmp(aName, "args") == 0)
            {
                for (auto a : e.args)
                    retain(a);
                return make_tuple(e.args);
            }
            if (e.cls != nullptr)
            {
                if (auto const r = user_getattr(aSelf, e.attributes, aName))
                    return r;
                if (failed())
                    return nullptr;
            }
            else
                for (auto const& a : e.attributes)
                    if (a.first == aName)
                    {
                        retain(a.second);
                        return a.second;
                    }
            return error("AttributeError", "'" + e.type + "' object has no attribute '" + aName + "'");
        }

        int exception_setattr(neos_py_context*, object* aSelf, char const* aName, object* aValue)
        {
            auto& e = *as<exception_object>(aSelf);
            if (std::strcmp(aName, "args") == 0)
            {
                for (auto a : e.args)
                    release(a);
                e.args = tuple_items(aValue);
                for (auto a : e.args)
                    retain(a);
                return 0;
            }
            return user_setattr(aSelf, e.attributes, aName, aValue);
        }

        object* construct_exception(neos_py_context*, void* aData, object* aArgs, object* aKwargs)
        {
            auto const& name = *static_cast<std::string const*>(aData);
            if (aKwargs != nullptr && !as<dict_object>(aKwargs)->items.empty())
                return error("TypeError", name + "() takes no keyword arguments");
            auto args = tuple_items(aArgs);
            for (auto a : args)
                retain(a);
            return make_exception(name, std::move(args));
        }

        int exception_instancecheck(neos_py_context*, void* aData, object* aObject)
        {
            auto const name = exception_type_name(aObject);
            return name != nullptr && is_exception_subclass(name, *static_cast<std::string const*>(aData)) ? 1 : 0;
        }

        object* exception_class(std::string const& aName)
        {
            static std::map<std::string, std::string, std::less<>> sNames;
            auto& name = sNames[aName];
            name = aName;
            auto const result = make_type_object(aName, aName);
            as<type_object>(result)->call = construct_exception;
            as<type_object>(result)->data = &name;
            as<type_object>(result)->instancecheck = exception_instancecheck;
            return result;
        }

        // (the exception last raised by a raise statement, and the one last caught)
        thread_local object* tRaised = nullptr;
        thread_local std::string tRaisedText;
        thread_local object* tCaught = nullptr;
        thread_local std::vector<object*> tHandling;   // (the exceptions being handled: except clauses can nest)

        void raise_exception(object* aException)
        {
            std::string message;
            if (!exception_text(aException, message))
                return;
            auto const& type = as<exception_object>(aException)->type;
            retain(aException);
            release(tRaised);
            tRaised = aException;
            tRaisedText = message.empty() ? type : type + ": " + message;
            python_runtime::raise(type, message);
        }

        // (KeyError(key))
        void raise_key_error(object* aKey)
        {
            retain(aKey);
            ref const e{ make_exception("KeyError", { aKey }) };
            raise_exception(e.get());
        }

        // (native division by zero)
        void fn_zero_division(std::int32_t aKind)
        {
            static char const* const sMessages[] = { "division by zero", "float division by zero", "float floor division by zero", "float modulo by zero",
                "integer division or modulo by zero", "integer modulo by zero" };
            error("ZeroDivisionError", sMessages[aKind >= 0 && aKind < 6 ? aKind : 0]);
        }

        // raise exception
        object* call(object* aCallable, object* aArgs, object* aKwargs);

        void fn_raise(object* aException)
        {
            if (exception_type_name(aException) != nullptr)
                raise_exception(aException);
            else if (is_class(aException) && is_exception_class(as_class_name(aException)))
            {
                ref const args{ make_tuple() };
                ref const e{ call(aException, args.get(), nullptr) };
                if (e)
                    fn_raise(e.get());
            }
            else if (is_type_object(aException) && is_exception_class(as<type_object>(aException)->name))
            {
                ref const e{ make_exception(as<type_object>(aException)->name, {}) };
                raise_exception(e.get());
            }
            else
                error("TypeError", "exceptions must derive from BaseException");
        }

        // raise (the exception being handled)
        void fn_reraise()
        {
            if (tCaught == nullptr)
                error("RuntimeError", "No active exception to reraise");
            else
                raise_exception(tCaught);
        }

        // the exception being handled (from its "type: message")
        object* fn_caught(char const* aText)
        {
            std::string const text = aText != nullptr ? aText : "";
            object* result = nullptr;
            if (tRaised != nullptr && text == tRaisedText)
            {
                retain(tRaised);
                result = tRaised;
            }
            else
            {
                auto const colon = text.find(": ");
                auto const type = text.substr(0u, colon);
                std::vector<object*> args;
                if (colon != std::string::npos)
                    args.push_back(make_str(text.substr(colon + 2u)));
                result = make_exception(type, std::move(args));
                as<exception_object>(result)->fromText = true;
            }
            retain(result);
            tHandling.push_back(tCaught);
            tCaught = result;
            return result;
        }

        // (the handler of the exception being handled has finished)
        void fn_handled()
        {
            release(tCaught);
            tCaught = nullptr;
            if (!tHandling.empty())
            {
                tCaught = tHandling.back();
                tHandling.pop_back();
            }
        }

        // except class (or tuple of classes)
        bool fn_exception_matches(object* aException, object* aClass)
        {
            if (kind_of(aClass) == NEOS_PY_TUPLE)
            {
                for (auto c : as<tuple_object>(aClass)->items)
                    if (fn_exception_matches(aException, c))
                        return true;
                return false;
            }
            if (is_class(aClass))
            {
                if (!is_exception_class(as_class_name(aClass)))
                {
                    error("TypeError", "catching classes that do not inherit from BaseException is not allowed");
                    return false;
                }
                auto const cls = user_class_of(aException);
                return cls != nullptr && is_subclass(cls, aClass);
            }
            if (!is_type_object(aClass) || !is_exception_class(as<type_object>(aClass)->name))
            {
                if (is_type_object(aClass) && as<type_object>(aClass)->instancecheck != nullptr)
                    return as<type_object>(aClass)->instancecheck(context(), as<type_object>(aClass)->data, aException) == 1;
                error("TypeError", "catching classes that do not inherit from BaseException is not allowed");
                return false;
            }
            auto const name = exception_type_name(aException);
            return name != nullptr && is_exception_subclass(name, as<type_object>(aClass)->name);
        }

        // with manager: manager.__enter__() and manager.__exit__(...)
        object* call_method_named(object* aObject, char const* aName, std::vector<object*> aArgs)
        {
            ref const method{ getattr(aObject, aName) };
            if (!method)
                return nullptr;
            ref const args{ make_tuple(std::move(aArgs)) };
            return call(method.get(), args.get(), nullptr);
        }
        object* fn_with_enter(object* aManager)
        {
            return call_method_named(aManager, "__enter__", {});
        }
        void fn_with_exit(object* aManager)
        {
            ref const r{ call_method_named(aManager, "__exit__", { none(), none(), none() }) };
        }
        bool fn_with_exit_exception(object* aManager, char const* aText)
        {
            ref const exception{ fn_caught(aText) };
            ref const type{ class_of(exception.get()) };
            retain(type.get());
            retain(exception.get());
            ref const r{ call_method_named(aManager, "__exit__", { type.get(), exception.get(), none() }) };
            fn_handled();
            if (!r)
                return false;
            return truth(r.get()) == 1;
        }

        // assert condition, message
        void fn_assertion(object* aMessage)
        {
            std::vector<object*> args;
            if (aMessage != nullptr && kind_of(aMessage) != NEOS_PY_NONE)
            {
                retain(aMessage);
                args.push_back(aMessage);
            }
            ref const e{ make_exception("AssertionError", std::move(args)) };
            raise_exception(e.get());
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

        // --- classes (class statements: __class(name, bases, namespace)) and their instances

        struct class_object
        {
            header h;
            std::string name;
            std::vector<object*> bases;     ///< (classes; builtin bases (object, exception classes) are not kept)
            std::vector<object*> mro;       ///< (without the class itself)
            std::vector<std::pair<std::string, object*>> attributes;
            std::string exceptionBase;      ///< (the builtin exception class it derives from, if any)
            ~class_object()
            {
                for (auto b : bases) release(b);
                for (auto m : mro) release(m);
                for (auto& a : attributes) release(a.second);
            }
        };

        struct instance_object
        {
            header h;
            object* cls;
            std::vector<std::pair<std::string, object*>> attributes;
            ~instance_object() { release(cls); for (auto& a : attributes) release(a.second); }
        };

        struct bound_method_object { header h; object* function; object* self; ~bound_method_object() { release(function); release(self); } };
        struct wrapper_object { header h; int kind; object* function; object* setter; ~wrapper_object() { release(function); release(setter); } };   // (staticmethod, classmethod or property)
        struct super_object { header h; object* cls; object* self; ~super_object() { release(cls); release(self); } };

        enum : int { StaticMethod, ClassMethod, Property };

        neos_py_type make_class_type();
        neos_py_type make_instance_type();
        neos_py_type make_bound_method_type();
        neos_py_type make_wrapper_type();
        neos_py_type make_super_type();
        neos_py_type sClassType = make_class_type();
        neos_py_type sInstanceType = make_instance_type();
        neos_py_type sBoundMethodType = make_bound_method_type();
        neos_py_type sWrapperType = make_wrapper_type();
        neos_py_type sSuperType = make_super_type();

        bool is_class(object* aObject) { return aObject != nullptr && aObject->type == &sClassType.base; }
        bool is_instance(object* aObject) { return aObject != nullptr && aObject->type == &sInstanceType.base; }

        std::string object_address(void const* aObject)
        {
            char buffer[32];
            std::snprintf(buffer, sizeof(buffer), "%p", aObject);
            return std::string{ "0x" } + (std::string_view{ buffer }.starts_with("0x") ? buffer + 2 : buffer);
        }

        // (a user class's class (instances and exceptions of user exception classes): else null)
        object* user_class_of(object* aObject)
        {
            if (is_instance(aObject))
                return as<instance_object>(aObject)->cls;
            if (aObject != nullptr && aObject->type == &sExceptionType.base)
                return as<exception_object>(aObject)->cls;
            return nullptr;
        }

        // (an attribute of a class: in its MRO; borrowed; null if none)
        object* class_lookup(object* aClass, std::string_view aName, object** aOwner = nullptr)
        {
            auto const find = [&](object* c) -> object*
            {
                for (auto& a : as<class_object>(c)->attributes)
                    if (a.first == aName)
                    {
                        if (aOwner != nullptr)
                            *aOwner = c;
                        return a.second;
                    }
                return nullptr;
            };
            if (auto const r = find(aClass))
                return r;
            for (auto c : as<class_object>(aClass)->mro)
                if (auto const r = find(c))
                    return r;
            return nullptr;
        }

        bool is_subclass(object* aClass, object* aBase)
        {
            if (aClass == aBase)
                return true;
            for (auto c : as<class_object>(aClass)->mro)
                if (c == aBase)
                    return true;
            return false;
        }

        object* make_bound_method(object* aFunction, object* aSelf)
        {
            retain(aFunction);
            retain(aSelf);
            return &(new bound_method_object{ { { &sBoundMethodType.base } }, aFunction, aSelf })->h.base;
        }

        // (an attribute found in a class, as accessed through an instance (or the class itself: aSelf null))
        object* bind_attribute(object* aAttribute, object* aSelf, object* aClass)
        {
            if (aAttribute->type == &sWrapperType.base)
            {
                auto const& w = *as<wrapper_object>(aAttribute);
                switch (w.kind)
                {
                case StaticMethod:
                    retain(w.function);
                    return w.function;
                case ClassMethod:
                    return make_bound_method(w.function, aClass);
                default:
                    if (aSelf == nullptr)
                    {
                        retain(aAttribute);
                        return aAttribute;
                    }
                    {
                        retain(aSelf);
                        ref const args{ make_tuple({ aSelf }) };
                        return call(w.function, args.get(), nullptr);
                    }
                }
            }
            if (aSelf != nullptr && (aAttribute->type == &sScriptFunctionType.base || (kind_of(aAttribute) == NEOS_PY_FUNCTION && as<function_object>(aAttribute)->self == nullptr)))
                return make_bound_method(aAttribute, aSelf);
            retain(aAttribute);
            return aAttribute;
        }

        // (a special method of an object's user class, bound to it: null (no exception) if it has none)
        object* special_method(object* aObject, char const* aName)
        {
            auto const cls = user_class_of(aObject);
            if (cls == nullptr)
                return nullptr;
            auto const attribute = class_lookup(cls, aName);
            if (attribute == nullptr)
                return nullptr;
            return bind_attribute(attribute, aObject, cls);
        }

        // (a call of a special method: aFound false if there is none)
        object* call_special(object* aObject, char const* aName, std::vector<object*> aArgs, bool& aFound) // (steals the arguments)
        {
            ref const method{ special_method(aObject, aName) };
            aFound = static_cast<bool>(method);
            if (!method)
            {
                for (auto a : aArgs)
                    release(a);
                return nullptr;
            }
            ref const args{ make_tuple(std::move(aArgs)) };
            return call(method.get(), args.get(), nullptr);
        }

        object* instance_getattr(neos_py_context*, object* aSelf, char const* aName);

        object* attributes_dict(std::vector<std::pair<std::string, object*>> const& aAttributes)
        {
            auto const d = make_dict();
            for (auto const& a : aAttributes)
            {
                ref const key{ make_str(a.first) };
                dict_set(d, key.get(), a.second);
            }
            return d;
        }

        // (getattr of an instance, or of an exception of a user exception class)
        object* user_getattr(object* aSelf, std::vector<std::pair<std::string, object*>> const& aAttributes, char const* aName)
        {
            std::string_view const name{ aName };
            auto const cls = user_class_of(aSelf);
            if (name == "__class__")
            {
                retain(cls);
                return cls;
            }
            if (name == "__dict__")
                return attributes_dict(aAttributes);
            // (a data descriptor (property) of the class first, then the instance's attributes, then the class's)
            auto const attribute = class_lookup(cls, name);
            if (attribute != nullptr && attribute->type == &sWrapperType.base && as<wrapper_object>(attribute)->kind == Property)
                return bind_attribute(attribute, aSelf, cls);
            for (auto const& a : aAttributes)
                if (a.first == name)
                {
                    retain(a.second);
                    return a.second;
                }
            if (attribute != nullptr)
                return bind_attribute(attribute, aSelf, cls);
            if (auto const getattr = class_lookup(cls, "__getattr__"))
            {
                ref const method{ bind_attribute(getattr, aSelf, cls) };
                ref const args{ make_tuple({ make_str(std::string{ name }) }) };
                return call(method.get(), args.get(), nullptr);
            }
            return nullptr;
        }

        int user_setattr(object* aSelf, std::vector<std::pair<std::string, object*>>& aAttributes, char const* aName, object* aValue)
        {
            auto const cls = user_class_of(aSelf);
            if (auto const attribute = class_lookup(cls, aName); attribute != nullptr && attribute->type == &sWrapperType.base &&
                as<wrapper_object>(attribute)->kind == Property)
            {
                auto const& p = *as<wrapper_object>(attribute);
                if (p.setter == nullptr)
                {
                    error("AttributeError", "property '" + std::string{ aName } + "' of '" + as<class_object>(cls)->name + "' object has no setter");
                    return -1;
                }
                retain(aSelf);
                retain(aValue);
                ref const args{ make_tuple({ aSelf, aValue }) };
                ref const r{ call(p.setter, args.get(), nullptr) };
                return r ? 0 : -1;
            }
            for (auto& a : aAttributes)
                if (a.first == aName)
                {
                    retain(aValue);
                    release(a.second);
                    a.second = aValue;
                    return 0;
                }
            retain(aValue);
            aAttributes.emplace_back(aName, aValue);
            return 0;
        }

        // --- instances

        object* instance_repr(neos_py_context*, object* aSelf)
        {
            bool found = false;
            if (auto const r = call_special(aSelf, "__repr__", {}, found); found)
                return r;
            auto const cls = as<instance_object>(aSelf)->cls;
            return make_str("<__main__." + as<class_object>(cls)->name + " object at " + object_address(aSelf) + ">");
        }

        object* instance_str(neos_py_context* aContext, object* aSelf)
        {
            bool found = false;
            if (auto const r = call_special(aSelf, "__str__", {}, found); found)
                return r;
            return instance_repr(aContext, aSelf);
        }

        object* instance_getattr(neos_py_context*, object* aSelf, char const* aName)
        {
            auto& instance = *as<instance_object>(aSelf);
            if (auto const r = user_getattr(aSelf, instance.attributes, aName))
                return r;
            if (failed())
                return nullptr;
            return error("AttributeError", "'" + as<class_object>(instance.cls)->name + "' object has no attribute '" + aName + "'");
        }

        int instance_setattr(neos_py_context*, object* aSelf, char const* aName, object* aValue)
        {
            return user_setattr(aSelf, as<instance_object>(aSelf)->attributes, aName, aValue);
        }

        char const* binary_method(int aOperation, bool aInplace, bool aReflected)
        {
            static char const* const sNames[][3] =
            {
                { "__add__", "__iadd__", "__radd__" }, { "__sub__", "__isub__", "__rsub__" }, { "__mul__", "__imul__", "__rmul__" },
                { "__truediv__", "__itruediv__", "__rtruediv__" }, { "__floordiv__", "__ifloordiv__", "__rfloordiv__" },
                { "__mod__", "__imod__", "__rmod__" }, { "__pow__", "__ipow__", "__rpow__" }, { "__matmul__", "__imatmul__", "__rmatmul__" },
                { "__lshift__", "__ilshift__", "__rlshift__" }, { "__rshift__", "__irshift__", "__rrshift__" }, { "__and__", "__iand__", "__rand__" },
                { "__or__", "__ior__", "__ror__" }, { "__xor__", "__ixor__", "__rxor__" }
            };
            return sNames[aOperation][aReflected ? 2 : aInplace ? 1 : 0];
        }

        object* user_binary(neos_py_context*, int aOperation, object* aLhs, object* aRhs, int aInplace)
        {
            bool found = false;
            if (user_class_of(aLhs) != nullptr)
            {
                if (aInplace)
                {
                    retain(aRhs);
                    ref r{ call_special(aLhs, binary_method(aOperation, true, false), { aRhs }, found) };
                    if (found && (!r || kind_of(r.get()) != NEOS_PY_NOT_IMPLEMENTED))
                        return r.release_ownership();
                    if (found)
                        return not_implemented();
                }
                retain(aRhs);
                ref r{ call_special(aLhs, binary_method(aOperation, false, false), { aRhs }, found) };
                if (found)
                    return r.release_ownership();
            }
            if (user_class_of(aRhs) != nullptr && user_class_of(aRhs) != user_class_of(aLhs))
            {
                retain(aLhs);
                ref r{ call_special(aRhs, binary_method(aOperation, false, true), { aLhs }, found) };
                if (found)
                    return r.release_ownership();
            }
            return not_implemented();
        }

        object* user_unary(neos_py_context*, int aOperation, object* aSelf)
        {
            static char const* const sNames[] = { "__neg__", "__pos__", "__invert__", "__abs__" };
            bool found = false;
            auto const r = call_special(aSelf, sNames[aOperation], {}, found);
            if (!found)
                return error("TypeError", std::string{ "bad operand type for unary " } + (aOperation == NEOS_PY_NEGATIVE ? "-" : aOperation == NEOS_PY_POSITIVE ? "+" :
                    aOperation == NEOS_PY_INVERT ? "~" : "abs()") + ": '" + type_name(aSelf) + "'");
            return r;
        }

        object* user_compare(neos_py_context*, int aOperation, object* aLhs, object* aRhs)
        {
            static char const* const sNames[] = { "__lt__", "__le__", "__eq__", "__ne__", "__gt__", "__ge__" };
            static int const sReflected[] = { NEOS_PY_GT, NEOS_PY_GE, NEOS_PY_EQ, NEOS_PY_NE, NEOS_PY_LT, NEOS_PY_LE };
            bool found = false;
            if (user_class_of(aLhs) != nullptr)
            {
                retain(aRhs);
                ref r{ call_special(aLhs, sNames[aOperation], { aRhs }, found) };
                if (found && (!r || kind_of(r.get()) != NEOS_PY_NOT_IMPLEMENTED))
                    return r.release_ownership();
            }
            if (user_class_of(aRhs) != nullptr)
            {
                retain(aLhs);
                ref r{ call_special(aRhs, sNames[sReflected[aOperation]], { aLhs }, found) };
                if (found && (!r || kind_of(r.get()) != NEOS_PY_NOT_IMPLEMENTED))
                    return r.release_ownership();
            }
            // (the default: identity; != is the inverse of ==)
            if (aOperation == NEOS_PY_EQ || aOperation == NEOS_PY_NE)
            {
                if (aOperation == NEOS_PY_NE && user_class_of(aLhs) != nullptr && special_method(aLhs, "__eq__") != nullptr)
                {
                    ref const eq{ user_compare(nullptr, NEOS_PY_EQ, aLhs, aRhs) };
                    if (!eq)
                        return nullptr;
                    if (kind_of(eq.get()) == NEOS_PY_NOT_IMPLEMENTED)
                        return make_bool(true);
                    auto const t = truth(eq.get());
                    return t < 0 ? nullptr : make_bool(t == 0);
                }
                return make_bool((aLhs == aRhs) == (aOperation == NEOS_PY_EQ));
            }
            return not_implemented();
        }

        int user_truth(neos_py_context*, object* aSelf)
        {
            bool found = false;
            ref r{ call_special(aSelf, "__bool__", {}, found) };
            if (!found)
            {
                r = ref{ call_special(aSelf, "__len__", {}, found) };
                if (!found)
                    return 1;
            }
            if (!r)
                return -1;
            return truth(r.get());
        }

        long long user_length(neos_py_context*, object* aSelf)
        {
            bool found = false;
            ref const r{ call_special(aSelf, "__len__", {}, found) };
            if (!found)
            {
                error("TypeError", "object of type '" + std::string{ type_name(aSelf) } + "' has no len()");
                return -1;
            }
            if (!r)
                return -1;
            long long n = 0;
            if (!to_index(r.get(), n))
                return -1;
            return n;
        }

        object* user_call(neos_py_context*, object* aSelf, object* aArgs, object* aKwargs)
        {
            ref const method{ special_method(aSelf, "__call__") };
            if (!method)
                return error("TypeError", "'" + std::string{ type_name(aSelf) } + "' object is not callable");
            return call(method.get(), aArgs, aKwargs);
        }

        object* user_getitem(neos_py_context*, object* aSelf, object* aKey)
        {
            bool found = false;
            retain(aKey);
            auto const r = call_special(aSelf, "__getitem__", { aKey }, found);
            if (!found)
                return error("TypeError", "'" + std::string{ type_name(aSelf) } + "' object is not subscriptable");
            return r;
        }

        int user_setitem(neos_py_context*, object* aSelf, object* aKey, object* aValue)
        {
            bool found = false;
            retain(aKey);
            ref r;
            if (aValue == nullptr)
                r = ref{ call_special(aSelf, "__delitem__", { aKey }, found) };
            else
            {
                retain(aValue);
                r = ref{ call_special(aSelf, "__setitem__", { aKey, aValue }, found) };
            }
            if (!found)
            {
                error("TypeError", "'" + std::string{ type_name(aSelf) } + "' object does not support item " + (aValue == nullptr ? "deletion" : "assignment"));
                return -1;
            }
            return r ? 0 : -1;
        }

        object* user_iter(neos_py_context*, object* aSelf)
        {
            bool found = false;
            auto const r = call_special(aSelf, "__iter__", {}, found);
            if (!found)
            {
                // (a sequence: __getitem__ with 0, 1, ...)
                if (special_method(aSelf, "__getitem__") == nullptr)
                    return error("TypeError", "'" + std::string{ type_name(aSelf) } + "' object is not iterable");
                std::vector<object*> items;
                for (long long i = 0;; ++i)
                {
                    bool ignored = false;
                    auto const item = call_special(aSelf, "__getitem__", { make_int(i) }, ignored);
                    if (item == nullptr)
                    {
                        if (failed() && error_text() != nullptr && std::string_view{ error_text() }.starts_with("IndexError"))
                        {
                            clear_error();
                            break;
                        }
                        for (auto x : items)
                            release(x);
                        return nullptr;
                    }
                    items.push_back(item);
                }
                ref const l{ make_list(std::move(items)) };
                return iter(l.get());
            }
            return r;
        }

        object* user_next(neos_py_context*, object* aSelf)
        {
            bool found = false;
            auto const r = call_special(aSelf, "__next__", {}, found);
            if (!found)
                return error("TypeError", "'" + std::string{ type_name(aSelf) } + "' object is not an iterator");
            if (r == nullptr && failed() && std::string_view{ error_text() }.starts_with("StopIteration"))
                clear_error();
            return r;
        }

        int user_contains(neos_py_context*, object* aSelf, object* aItem)
        {
            bool found = false;
            retain(aItem);
            ref const r{ call_special(aSelf, "__contains__", { aItem }, found) };
            if (!found)
            {
                ref const iterator{ user_iter(nullptr, aSelf) };
                if (!iterator)
                    return -1;
                for (;;)
                {
                    ref const item{ next(iterator.get()) };
                    if (!item)
                        return failed() ? -1 : 0;
                    ref const equal{ compare(NEOS_PY_EQ, item.get(), aItem) };
                    if (!equal)
                        return -1;
                    if (truth(equal.get()) == 1)
                        return 1;
                }
            }
            return r ? truth(r.get()) : -1;
        }

        int user_index(neos_py_context*, object* aSelf, long long* aResult)
        {
            bool found = false;
            ref const r{ call_special(aSelf, "__index__", {}, found) };
            if (!found)
            {
                error("TypeError", "'" + std::string{ type_name(aSelf) } + "' object cannot be interpreted as an integer");
                return -1;
            }
            return r && to_index(r.get(), *aResult) ? 0 : -1;
        }

        int user_to_float(neos_py_context*, object* aSelf, double* aResult)
        {
            bool found = false;
            ref const r{ call_special(aSelf, "__float__", {}, found) };
            if (!found)
            {
                error("TypeError", "float() argument must be a string or a real number, not '" + std::string{ type_name(aSelf) } + "'");
                return -1;
            }
            return r && to_double(r.get(), *aResult) ? 0 : -1;
        }

        int user_to_int(neos_py_context*, object* aSelf, long long* aResult)
        {
            bool found = false;
            ref r{ call_special(aSelf, "__int__", {}, found) };
            if (!found)
                r = ref{ call_special(aSelf, "__index__", {}, found) };
            if (!found)
            {
                error("TypeError", "int() argument must be a string, a bytes-like object or a real number, not '" + std::string{ type_name(aSelf) } + "'");
                return -1;
            }
            return r && to_index(r.get(), *aResult) ? 0 : -1;
        }

        neos_py_type make_instance_type()
        {
            auto type = make_type("python.instance", "object", retain_object, release_object<instance_object>);
            type.repr = instance_repr;
            type.str = instance_str;
            type.getattr = instance_getattr;
            type.setattr = instance_setattr;
            type.call = user_call;
            type.getitem = user_getitem;
            type.setitem = user_setitem;
            type.binary = user_binary;
            type.unary = user_unary;
            type.compare = user_compare;
            type.truth = user_truth;
            type.length = user_length;
            type.iter = user_iter;
            type.next = user_next;
            type.contains = user_contains;
            type.index = user_index;
            type.to_float = user_to_float;
            type.to_int = user_to_int;
            return type;
        }

        // --- classes

        object* class_repr(neos_py_context*, object* aSelf)
        {
            return make_str("<class '__main__." + as<class_object>(aSelf)->name + "'>");
        }

        object* class_call(neos_py_context*, object* aSelf, object* aArgs, object* aKwargs)
        {
            auto& cls = *as<class_object>(aSelf);
            object* self;
            if (!cls.exceptionBase.empty())
            {
                // (an exception: its args are the call's arguments)
                auto args = tuple_items(aArgs);
                for (auto a : args)
                    retain(a);
                self = make_exception(cls.name, std::move(args));
                retain(aSelf);
                as<exception_object>(self)->cls = aSelf;
            }
            else
            {
                retain(aSelf);
                self = &(new instance_object{ { { &sInstanceType.base } }, aSelf, {} })->h.base;
            }
            ref result{ self };
            if (auto const init = class_lookup(aSelf, "__init__"))
            {
                ref const method{ bind_attribute(init, self, aSelf) };
                ref const r{ call(method.get(), aArgs, aKwargs) };
                if (!r)
                    return nullptr;
                if (kind_of(r.get()) != NEOS_PY_NONE)
                    return error("TypeError", "__init__() should return None, not '" + std::string{ type_name(r.get()) } + "'");
            }
            else if (cls.exceptionBase.empty() && (tuple_items(aArgs).size() != 0u || (aKwargs != nullptr && !as<dict_object>(aKwargs)->items.empty())))
                return error("TypeError", cls.name + "() takes no arguments");
            return result.release_ownership();
        }

        object* class_getattr(neos_py_context*, object* aSelf, char const* aName)
        {
            auto const& cls = *as<class_object>(aSelf);
            std::string_view const name{ aName };
            if (name == "__name__" || name == "__qualname__")
                return make_str(cls.name);
            if (name == "__module__")
                return make_str("__main__");
            if (name == "__dict__")
                return attributes_dict(cls.attributes);
            if (name == "__bases__")
            {
                std::vector<object*> bases;
                for (auto b : cls.bases)
                    bases.push_back((retain(b), b));
                if (bases.empty())
                    bases.push_back(cls.exceptionBase.empty() ? make_type_object("object", "object") : exception_class(cls.exceptionBase));
                return make_tuple(std::move(bases));
            }
            if (name == "__mro__")
            {
                std::vector<object*> mro{ (retain(aSelf), aSelf) };
                for (auto m : cls.mro)
                    mro.push_back((retain(m), m));
                if (!cls.exceptionBase.empty())
                    mro.push_back(exception_class(cls.exceptionBase));
                mro.push_back(make_type_object("object", "object"));
                return make_tuple(std::move(mro));
            }
            if (auto const attribute = class_lookup(aSelf, name))
                return bind_attribute(attribute, nullptr, aSelf);
            return error("AttributeError", "type object '" + cls.name + "' has no attribute '" + aName + "'");
        }

        int class_setattr(neos_py_context*, object* aSelf, char const* aName, object* aValue)
        {
            auto& attributes = as<class_object>(aSelf)->attributes;
            for (auto& a : attributes)
                if (a.first == aName)
                {
                    retain(aValue);
                    release(a.second);
                    a.second = aValue;
                    return 0;
                }
            retain(aValue);
            attributes.emplace_back(aName, aValue);
            return 0;
        }

        object* class_compare(neos_py_context*, int aOperation, object* aLhs, object* aRhs)
        {
            if (aOperation != NEOS_PY_EQ && aOperation != NEOS_PY_NE)
                return not_implemented();
            return make_bool((aLhs == aRhs) == (aOperation == NEOS_PY_EQ));
        }

        neos_py_type make_class_type()
        {
            auto type = make_type("python.class", "type", retain_object, release_object<class_object>);
            type.repr = class_repr;
            type.call = class_call;
            type.getattr = class_getattr;
            type.setattr = class_setattr;
            type.compare = class_compare;
            return type;
        }

        // (C3 linearization of a class's bases)
        bool linearize(std::vector<object*> const& aBases, std::vector<object*>& aResult)
        {
            std::vector<std::vector<object*>> sequences;
            for (auto b : aBases)
            {
                std::vector<object*> s{ b };
                s.insert(s.end(), as<class_object>(b)->mro.begin(), as<class_object>(b)->mro.end());
                sequences.push_back(std::move(s));
            }
            sequences.push_back(aBases);
            for (;;)
            {
                sequences.erase(std::remove_if(sequences.begin(), sequences.end(), [](auto const& s) { return s.empty(); }), sequences.end());
                if (sequences.empty())
                    return true;
                object* candidate = nullptr;
                for (auto const& s : sequences)
                {
                    auto const head = s.front();
                    bool inTail = false;
                    for (auto const& t : sequences)
                        if (std::find(t.begin() + 1, t.end(), head) != t.end())
                            inTail = true;
                    if (!inTail)
                    {
                        candidate = head;
                        break;
                    }
                }
                if (candidate == nullptr)
                {
                    error("TypeError", "Cannot create a consistent method resolution order (MRO)");
                    return false;
                }
                aResult.push_back(candidate);
                for (auto& s : sequences)
                    if (!s.empty() && s.front() == candidate)
                        s.erase(s.begin());
            }
        }

        std::map<std::string, std::vector<std::string>, std::less<>>& user_exception_bases()
        {
            static std::map<std::string, std::vector<std::string>, std::less<>> sBases;
            return sBases;
        }

        // __class(name, bases, namespace): a class statement
        object* fn_class(char const* aName, object* aBases, object* aNamespace)
        {
            auto* const cls = new class_object{ { { &sClassType.base } }, aName, {}, {}, {}, {} };
            ref result{ &cls->h.base };
            for (auto b : tuple_items(aBases))
            {
                if (is_class(b))
                {
                    retain(b);
                    cls->bases.push_back(b);
                    if (!as<class_object>(b)->exceptionBase.empty() && cls->exceptionBase.empty())
                        cls->exceptionBase = as<class_object>(b)->name;
                }
                else if (is_type_object(b) && as<type_object>(b)->name == "object")
                    ;
                else if (is_type_object(b) && is_exception_class(as<type_object>(b)->name))
                {
                    if (cls->exceptionBase.empty())
                        cls->exceptionBase = as<type_object>(b)->name;
                }
                else
                    return error("TypeError", std::string{ "cannot derive class '" } + aName + "' from '" + class_name(b) + "' (not supported)");
            }
            std::vector<object*> mro;
            if (!linearize(cls->bases, mro))
                return nullptr;
            for (auto m : mro)
                cls->mro.push_back((retain(m), m));
            if (!cls->exceptionBase.empty())
                user_exception_bases()[aName] = { cls->exceptionBase };
            for (auto const& item : as<dict_object>(aNamespace)->items)
            {
                retain(item.second);
                cls->attributes.emplace_back(as<str_object>(item.first)->value, item.second);
            }
            return result.release_ownership();
        }

        // --- bound methods, staticmethod, classmethod, property and super

        neos_py_type make_bound_method_type()
        {
            auto type = make_type("python.method", "method", retain_object, release_object<bound_method_object>);
            type.repr = [](neos_py_context*, object* aSelf) -> object*
            {
                auto const& m = *as<bound_method_object>(aSelf);
                std::string name;
                ref const n{ getattr(m.function, "__name__") };
                if (n && kind_of(n.get()) == NEOS_PY_STR)
                    name = as<str_object>(n.get())->value;
                else
                    clear_error();
                std::string self;
                if (!repr_of(m.self, self))
                    return nullptr;
                auto const cls = is_class(m.self) ? m.self : user_class_of(m.self);
                return make_str("<bound method " + (cls != nullptr ? as<class_object>(cls)->name + "." : std::string{}) + name + " of " + self + ">");
            };
            type.call = [](neos_py_context*, object* aSelf, object* aArgs, object* aKwargs) -> object*
            {
                auto const& m = *as<bound_method_object>(aSelf);
                std::vector<object*> items{ (retain(m.self), m.self) };
                for (auto a : tuple_items(aArgs))
                    items.push_back((retain(a), a));
                ref const args{ make_tuple(std::move(items)) };
                return call(m.function, args.get(), aKwargs);
            };
            type.getattr = [](neos_py_context*, object* aSelf, char const* aName) -> object*
            {
                auto const& m = *as<bound_method_object>(aSelf);
                if (std::strcmp(aName, "__self__") == 0)
                    return (retain(m.self), m.self);
                if (std::strcmp(aName, "__func__") == 0)
                    return (retain(m.function), m.function);
                return getattr(m.function, aName);
            };
            return type;
        }

        neos_py_type make_wrapper_type()
        {
            auto type = make_type("python.wrapper", "property", retain_object, release_object<wrapper_object>);
            type.repr = [](neos_py_context*, object* aSelf) -> object*
            {
                static char const* const sNames[] = { "staticmethod", "classmethod", "property" };
                return make_str("<" + std::string{ sNames[as<wrapper_object>(aSelf)->kind] } + " object at " + object_address(aSelf) + ">");
            };
            type.getattr = [](neos_py_context*, object* aSelf, char const* aName) -> object*
            {
                auto& w = *as<wrapper_object>(aSelf);
                if (w.kind == Property && std::strcmp(aName, "setter") == 0)
                {
                    // (property.setter(f): a property with a setter)
                    retain(aSelf);
                    return make_function("setter", [](neos_py_context*, void*, object* aArgs, object*) -> object*
                    {
                        auto const& args = tuple_items(aArgs);
                        if (args.size() != 2u)
                            return error("TypeError", "setter() takes exactly one argument");
                        auto const& p = *as<wrapper_object>(args[0]);
                        retain(p.function);
                        retain(args[1]);
                        return &(new wrapper_object{ { { &sWrapperType.base } }, Property, p.function, args[1] })->h.base;
                    }, nullptr, aSelf);
                }
                if (std::strcmp(aName, "__func__") == 0 || std::strcmp(aName, "fget") == 0)
                    return (retain(w.function), w.function);
                if (std::strcmp(aName, "fset") == 0)
                    return w.setter != nullptr ? (retain(w.setter), w.setter) : none();
                return error("AttributeError", "'property' object has no attribute '" + std::string{ aName } + "'");
            };
            type.call = [](neos_py_context*, object* aSelf, object* aArgs, object* aKwargs) -> object*
            {
                auto const& w = *as<wrapper_object>(aSelf);
                if (w.kind != StaticMethod)
                    return error("TypeError", "'property' object is not callable");
                return call(w.function, aArgs, aKwargs);
            };
            return type;
        }

        object* make_wrapper(int aKind, std::vector<object*> const& aArgs, char const* aName)
        {
            if (aArgs.size() != 1u && !(aKind == Property && aArgs.size() == 2u))
                return error("TypeError", std::string{ aName } + " expected 1 argument, got " + std::to_string(aArgs.size()));
            retain(aArgs[0]);
            object* setter = aArgs.size() == 2u ? (retain(aArgs[1]), aArgs[1]) : nullptr;
            return &(new wrapper_object{ { { &sWrapperType.base } }, aKind, aArgs[0], setter })->h.base;
        }

        object* b_staticmethod(std::vector<object*> const& aArgs, object*) { return make_wrapper(StaticMethod, aArgs, "staticmethod"); }
        object* b_classmethod(std::vector<object*> const& aArgs, object*) { return make_wrapper(ClassMethod, aArgs, "classmethod"); }
        object* b_property(std::vector<object*> const& aArgs, object*) { return make_wrapper(Property, aArgs, "property"); }

        neos_py_type make_super_type()
        {
            auto type = make_type("python.super", "super", retain_object, release_object<super_object>);
            type.repr = [](neos_py_context*, object* aSelf) -> object*
            {
                return make_str("<super: <class '" + as<class_object>(as<super_object>(aSelf)->cls)->name + "'>, <" + type_name(as<super_object>(aSelf)->self) + " object>>");
            };
            type.getattr = [](neos_py_context*, object* aSelf, char const* aName) -> object*
            {
                auto const& s = *as<super_object>(aSelf);
                auto const selfClass = is_class(s.self) ? s.self : user_class_of(s.self);
                // (the classes after cls in the MRO of self's class)
                std::vector<object*> mro{ selfClass };
                mro.insert(mro.end(), as<class_object>(selfClass)->mro.begin(), as<class_object>(selfClass)->mro.end());
                auto position = std::find(mro.begin(), mro.end(), s.cls);
                if (position != mro.end())
                    ++position;
                for (; position != mro.end(); ++position)
                    for (auto const& a : as<class_object>(*position)->attributes)
                        if (a.first == aName)
                            return bind_attribute(a.second, is_class(s.self) ? nullptr : s.self, selfClass);
                // (object's and the builtin exceptions' __init__)
                if (std::strcmp(aName, "__init__") == 0)
                {
                    retain(s.self);
                    return make_function("__init__", [](neos_py_context*, void*, object* aArgs, object*) -> object*
                    {
                        auto const& args = tuple_items(aArgs);
                        if (!args.empty() && args[0]->type == &sExceptionType.base)
                        {
                            auto& e = *as<exception_object>(args[0]);
                            for (auto a : e.args)
                                release(a);
                            e.args.assign(args.begin() + 1, args.end());
                            for (auto a : e.args)
                                retain(a);
                        }
                        return none();
                    }, nullptr, s.self);
                }
                if (std::strcmp(aName, "__str__") == 0 || std::strcmp(aName, "__repr__") == 0)
                {
                    retain(s.self);
                    bool const isStr = std::strcmp(aName, "__str__") == 0;
                    return make_function(aName, isStr ?
                        static_cast<native_function>([](neos_py_context*, void*, object* aArgs, object*) -> object*
                        {
                            auto const self = tuple_items(aArgs).at(0);
                            if (self->type == &sExceptionType.base)
                            {
                                std::string text;
                                return exception_text(self, text) ? make_str(text) : nullptr;
                            }
                            return make_str("<__main__." + as<class_object>(user_class_of(self))->name + " object at " + object_address(self) + ">");
                        }) :
                        static_cast<native_function>([](neos_py_context*, void*, object* aArgs, object*) -> object*
                        {
                            auto const self = tuple_items(aArgs).at(0);
                            if (self->type == &sExceptionType.base)
                                return exception_repr(nullptr, self);
                            return make_str("<__main__." + as<class_object>(user_class_of(self))->name + " object at " + object_address(self) + ">");
                        }), nullptr, s.self);
                }
                return error("AttributeError", "'super' object has no attribute '" + std::string{ aName } + "'");
            };
            return type;
        }

        object* b_super(std::vector<object*> const& aArgs, object*)
        {
            if (aArgs.size() != 2u || !is_class(aArgs[0]))
                return error("RuntimeError", "super(): no arguments");
            retain(aArgs[0]);
            retain(aArgs[1]);
            return &(new super_object{ { { &sSuperType.base } }, aArgs[0], aArgs[1] })->h.base;
        }

        object* b_issubclass(std::vector<object*> const& aArgs, object* aKwargs)
        {
            if (!no_keywords(aKwargs, "issubclass") || !arity(aArgs, 2u, 2u, "issubclass"))
                return nullptr;
            std::vector<object*> classes;
            if (kind_of(aArgs[1]) == NEOS_PY_TUPLE)
                classes = as<tuple_object>(aArgs[1])->items;
            else
                classes.push_back(aArgs[1]);
            for (auto c : classes)
            {
                if (is_class(aArgs[0]) && is_class(c) && is_subclass(aArgs[0], c))
                    return make_bool(true);
                if (is_type_object(c) && as<type_object>(c)->name == "object")
                    return make_bool(true);
                auto const name = is_class(aArgs[0]) ? as<class_object>(aArgs[0])->name : is_type_object(aArgs[0]) ? as<type_object>(aArgs[0])->name : std::string{};
                if (is_type_object(c) && is_exception_class(as<type_object>(c)->name) && is_exception_class(name) && is_exception_subclass(name, as<type_object>(c)->name))
                    return make_bool(true);
                if (is_type_object(aArgs[0]) && is_type_object(c) && as<type_object>(aArgs[0])->qualifiedName == as<type_object>(c)->qualifiedName)
                    return make_bool(true);
            }
            if (!is_class(aArgs[0]) && !is_type_object(aArgs[0]))
                return error("TypeError", "issubclass() arg 1 must be a class");
            return make_bool(false);
        }

        char const* instance_class_name(object* aObject)
        {
            return is_instance(aObject) ? as<class_object>(as<instance_object>(aObject)->cls)->name.c_str() : nullptr;
        }

        std::string const& as_class_name(object* aClass)
        {
            return as<class_object>(aClass)->name;
        }

        object* b_class(std::vector<object*> const& aArgs, object*)
        {
            if (aArgs.size() != 3u || kind_of(aArgs[0]) != NEOS_PY_STR || kind_of(aArgs[1]) != NEOS_PY_TUPLE || kind_of(aArgs[2]) != NEOS_PY_DICT)
                return error("TypeError", "__class(name, bases, namespace)");
            return fn_class(as<str_object>(aArgs[0])->value.c_str(), aArgs[1], aArgs[2]);
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
            auto const userClass = user_class_of(aArgs[0]);
            for (auto c : classes)
                if (is_class(c) && userClass != nullptr && is_subclass(userClass, c))
                    return make_bool(true);
            if (userClass != nullptr && aArgs[0]->type != &sExceptionType.base)
            {
                for (auto c : classes)
                    if (is_type_object(c) && as<type_object>(c)->qualifiedName == "object")
                        return make_bool(true);
                return make_bool(false);
            }
            ref const actual{ class_of(aArgs[0]) };
            auto const& name = is_type_object(actual.get()) ? as<type_object>(actual.get())->qualifiedName : as_class_name(actual.get());
            for (auto c : classes)
            {
                if (is_class(c))
                    continue;
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

        object* b_format(std::vector<object*> const& aArgs, object* aKwargs)
        {
            if (!no_keywords(aKwargs, "format") || !arity(aArgs, 1u, 2u, "format"))
                return nullptr;
            std::string spec;
            if (aArgs.size() == 2u)
            {
                if (kind_of(aArgs[1]) != NEOS_PY_STR)
                    return error("TypeError", std::string{ "format() argument 2 must be str, not " } + type_name(aArgs[1]));
                spec = as<str_object>(aArgs[1])->value;
            }
            std::string text;
            return format_value(aArgs[0], spec, text) ? make_str(std::move(text)) : nullptr;
        }

        builtin_entry const sBuiltinFunctions[] =
        {
            { "abs", b_abs }, { "all", b_all }, { "any", b_any }, { "bin", b_bin }, { "callable", b_callable }, { "chr", b_chr },
            { "divmod", b_divmod }, { "enumerate", b_enumerate }, { "filter", b_filter }, { "format", b_format }, { "getattr", b_getattr }, { "hasattr", b_hasattr }, 
            { "hex", b_hex }, { "input", b_input }, { "isinstance", b_isinstance }, { "iter", b_iter }, { "len", b_len }, { "map", b_map },
            { "max", b_max }, { "min", b_min }, { "next", b_next }, { "oct", b_oct }, { "ord", b_ord }, { "pow", b_pow }, { "print", b_print },
            { "repr", b_repr }, { "reversed", b_reversed }, { "round", b_round }, { "setattr", b_setattr }, { "sorted", b_sorted }, { "sum", b_sum },
            { "zip", b_zip }, { "__class", b_class }, { "staticmethod", b_staticmethod }, { "classmethod", b_classmethod }, { "property", b_property },
            { "super", b_super }, { "issubclass", b_issubclass }
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

        // --- modules of Python's standard library (import math, sys): python::import

        struct module_object
        {
            header h;
            std::string name;
            std::vector<std::pair<std::string, object*>> attributes;
            ~module_object() { for (auto& a : attributes) release(a.second); }
        };

        neos_py_type make_module_type()
        {
            auto type = make_type("python.module", "module", retain_object, release_object<module_object>);
            type.repr = [](neos_py_context*, object* aSelf) -> object*
            {
                return make_str("<module '" + as<module_object>(aSelf)->name + "' (built-in)>");
            };
            type.getattr = [](neos_py_context*, object* aSelf, char const* aName) -> object*
            {
                auto const& m = *as<module_object>(aSelf);
                if (std::strcmp(aName, "__name__") == 0)
                    return make_str(m.name);
                for (auto const& a : m.attributes)
                    if (a.first == aName)
                        return (retain(a.second), a.second);
                return error("AttributeError", "module '" + m.name + "' has no attribute '" + aName + "'");
            };
            return type;
        }

        neos_py_type sModuleType = make_module_type();

        // (math: the algorithms of CPython's mathmodule.c (Python Software Foundation License))
        namespace math_module
        {
            double constexpr pi = 3.141592653589793238462643383279502884197;
            double constexpr logpi = 1.144729885849400174143427351353058711647;

            enum class math_error { None, Domain, Range };

            object* raise_math(math_error aError, double aResult)
            {
                if (aError == math_error::Domain)
                    return error("ValueError", "math domain error");
                if (aError == math_error::Range && std::fabs(aResult) >= 1.5)
                    return error("OverflowError", "math range error");
                return make_float(aResult);
            }

            bool arg_double(std::vector<object*> const& aArgs, std::size_t aIndex, double& aResult)
            {
                return to_double(aArgs[aIndex], aResult);
            }

            // (math_1: a NaN result from a non-NaN argument is a domain error; an infinite result from a finite argument is
            // a range error if the function can overflow, else a domain error)
            object* math_1(std::vector<object*> const& aArgs, char const* aName, double (*aFunction)(double), bool aCanOverflow)
            {
                if (!arity(aArgs, 1u, 1u, aName))
                    return nullptr;
                double x = 0.0;
                if (!arg_double(aArgs, 0, x))
                    return nullptr;
                auto const r = aFunction(x);
                if (std::isnan(r) && !std::isnan(x))
                    return error("ValueError", "math domain error");
                if (std::isinf(r) && std::isfinite(x))
                    return error(aCanOverflow ? "OverflowError" : "ValueError", aCanOverflow ? "math range error" : "math domain error");
                return make_float(r);
            }

            // (math_2: two arguments)
            object* math_2(std::vector<object*> const& aArgs, char const* aName, double (*aFunction)(double, double))
            {
                if (!arity(aArgs, 2u, 2u, aName))
                    return nullptr;
                double x = 0.0, y = 0.0;
                if (!arg_double(aArgs, 0, x) || !arg_double(aArgs, 1, y))
                    return nullptr;
                auto const r = aFunction(x, y);
                if (std::isnan(r) && !std::isnan(x) && !std::isnan(y))
                    return error("ValueError", "math domain error");
                if (std::isinf(r) && std::isfinite(x) && std::isfinite(y))
                    return raise_math(math_error::Range, r);
                return make_float(r);
            }

            double m_sinpi(double x)
            {
                double const y = std::fmod(std::fabs(x), 2.0);
                int const n = static_cast<int>(std::round(2.0 * y));
                double r = 0.0;
                switch (n)
                {
                case 0: r = std::sin(pi * y); break;
                case 1: r = std::cos(pi * (y - 0.5)); break;
                case 2: r = std::sin(pi * (1.0 - y)); break;
                case 3: r = -std::cos(pi * (y - 1.5)); break;
                default: r = std::sin(pi * (y - 2.0)); break;
                }
                return std::copysign(1.0, x) * r;
            }

            int constexpr LanczosN = 13;
            double constexpr lanczos_g = 6.024680040776729583740234375;
            double constexpr lanczos_g_minus_half = 5.524680040776729583740234375;
            double constexpr lanczos_num_coeffs[LanczosN] =
            {
                23531376880.410759688572007674451636754734846804940, 42919803642.649098768957899047001988850926355848959,
                35711959237.355668049440185451547166705960488635843, 17921034426.037209699919755754458931112671403265390,
                6039542586.3520280050642916443072979210699388420708, 1439720407.3117216736632230727949123939715485786772,
                248874557.86205415651146038641322942321632125127801, 31426415.585400194380614231628318205362874684987640,
                2876370.6289353724412254090516208496135991145378768, 186056.26539522349504029498971604569928220784236328,
                8071.6720023658162106380029022722506138218516325024, 210.82427775157934587250973392071336271166969580291,
                2.5066282746310002701649081771338373386264310793408
            };
            double constexpr lanczos_den_coeffs[LanczosN] =
            {
                0.0, 39916800.0, 120543840.0, 150917976.0, 105258076.0, 45995730.0, 13339535.0, 2637558.0, 357423.0, 32670.0, 1925.0, 66.0, 1.0
            };
            int constexpr NGammaIntegral = 23;
            double constexpr gamma_integral[NGammaIntegral] =
            {
                1.0, 1.0, 2.0, 6.0, 24.0, 120.0, 720.0, 5040.0, 40320.0, 362880.0, 3628800.0, 39916800.0, 479001600.0, 6227020800.0,
                87178291200.0, 1307674368000.0, 20922789888000.0, 355687428096000.0, 6402373705728000.0, 121645100408832000.0,
                2432902008176640000.0, 51090942171709440000.0, 1124000727777607680000.0
            };

            double lanczos_sum(double x)
            {
                double num = 0.0, den = 0.0;
                if (x < 5.0)
                    for (int i = LanczosN; --i >= 0;)
                    {
                        num = num * x + lanczos_num_coeffs[i];
                        den = den * x + lanczos_den_coeffs[i];
                    }
                else
                    for (int i = 0; i < LanczosN; i++)
                    {
                        num = num / x + lanczos_num_coeffs[i];
                        den = den / x + lanczos_den_coeffs[i];
                    }
                return num / den;
            }

            double m_tgamma(double x, math_error& aError)
            {
                if (!std::isfinite(x))
                {
                    if (std::isnan(x) || x > 0.0)
                        return x;
                    aError = math_error::Domain;
                    return std::numeric_limits<double>::quiet_NaN();
                }
                if (x == 0.0)
                {
                    aError = math_error::Domain;
                    return std::copysign(std::numeric_limits<double>::infinity(), x);
                }
                if (x == std::floor(x))
                {
                    if (x < 0.0)
                    {
                        aError = math_error::Domain;
                        return std::numeric_limits<double>::quiet_NaN();
                    }
                    if (x <= NGammaIntegral)
                        return gamma_integral[static_cast<int>(x) - 1];
                }
                double const absx = std::fabs(x);
                if (absx < 1e-20)
                {
                    double const r = 1.0 / x;
                    if (std::isinf(r))
                        aError = math_error::Range;
                    return r;
                }
                if (absx > 200.0)
                {
                    if (x < 0.0)
                        return 0.0 / m_sinpi(x);
                    aError = math_error::Range;
                    return std::numeric_limits<double>::infinity();
                }
                double const y = absx + lanczos_g_minus_half;
                double z;
                if (absx > lanczos_g_minus_half)
                {
                    double const q = y - absx;
                    z = q - lanczos_g_minus_half;
                }
                else
                {
                    double const q = y - lanczos_g_minus_half;
                    z = q - absx;
                }
                z = z * lanczos_g / y;
                double r;
                if (x < 0.0)
                {
                    r = -pi / m_sinpi(absx) / absx * std::exp(y) / lanczos_sum(absx);
                    r -= z * r;
                    if (absx < 140.0)
                        r /= std::pow(y, absx - 0.5);
                    else
                    {
                        double const sqrtpow = std::pow(y, absx / 2.0 - 0.25);
                        r /= sqrtpow;
                        r /= sqrtpow;
                    }
                }
                else
                {
                    r = lanczos_sum(absx) / std::exp(y);
                    r += z * r;
                    if (absx < 140.0)
                        r *= std::pow(y, absx - 0.5);
                    else
                    {
                        double const sqrtpow = std::pow(y, absx / 2.0 - 0.25);
                        r *= sqrtpow;
                        r *= sqrtpow;
                    }
                }
                if (std::isinf(r))
                    aError = math_error::Range;
                return r;
            }

            double m_lgamma(double x, math_error& aError)
            {
                if (!std::isfinite(x))
                    return std::isnan(x) ? x : std::numeric_limits<double>::infinity();
                if (x == std::floor(x) && x <= 2.0)
                {
                    if (x <= 0.0)
                    {
                        aError = math_error::Domain;
                        return std::numeric_limits<double>::infinity();
                    }
                    return 0.0;
                }
                double const absx = std::fabs(x);
                if (absx < 1e-20)
                    return -std::log(absx);
                double r = std::log(lanczos_sum(absx)) - lanczos_g;
                r += (absx - 0.5) * (std::log(absx + lanczos_g - 0.5) - 1);
                if (x < 0.0)
                    r = logpi - std::log(std::fabs(m_sinpi(absx))) - std::log(absx) - r;
                if (std::isinf(r))
                    aError = math_error::Range;
                return r;
            }

            double m_atan2(double y, double x)
            {
                if (std::isnan(x) || std::isnan(y))
                    return std::numeric_limits<double>::quiet_NaN();
                if (std::isinf(y))
                {
                    if (std::isinf(x))
                        return std::copysign(std::copysign(1.0, x) == 1.0 ? 0.25 * pi : 0.75 * pi, y);
                    return std::copysign(0.5 * pi, y);
                }
                if (std::isinf(x) || y == 0.0)
                    return std::copysign(1.0, x) == 1.0 ? std::copysign(0.0, y) : std::copysign(pi, y);
                return std::atan2(y, x);
            }

            double m_remainder(double x, double y)
            {
                if (std::isfinite(x) && std::isfinite(y))
                {
                    if (y == 0.0)
                        return std::numeric_limits<double>::quiet_NaN();
                    double const absx = std::fabs(x), absy = std::fabs(y);
                    double const m = std::fmod(absx, absy);
                    double const c = absy - m;
                    double r;
                    if (m < c)
                        r = m;
                    else if (m > c)
                        r = -c;
                    else
                        r = m - 2.0 * std::fmod(0.5 * (absx - m), absy);
                    return std::copysign(1.0, x) * r;
                }
                if (std::isnan(x))
                    return x;
                if (std::isnan(y))
                    return y;
                if (std::isinf(x))
                    return std::numeric_limits<double>::quiet_NaN();
                return x;
            }

            double m_log(double x)
            {
                if (std::isfinite(x))
                {
                    if (x > 0.0)
                        return std::log(x);
                    return x == 0.0 ? -std::numeric_limits<double>::infinity() : std::numeric_limits<double>::quiet_NaN();
                }
                if (std::isnan(x) || x > 0.0)
                    return x;
                return std::numeric_limits<double>::quiet_NaN();
            }

            double m_log2(double x)
            {
                if (!std::isfinite(x))
                    return std::isnan(x) || x > 0.0 ? x : std::numeric_limits<double>::quiet_NaN();
                if (x > 0.0)
                    return std::log2(x);
                return x == 0.0 ? -std::numeric_limits<double>::infinity() : std::numeric_limits<double>::quiet_NaN();
            }

            double m_log10(double x)
            {
                if (std::isfinite(x))
                {
                    if (x > 0.0)
                        return std::log10(x);
                    return x == 0.0 ? -std::numeric_limits<double>::infinity() : std::numeric_limits<double>::quiet_NaN();
                }
                if (std::isnan(x) || x > 0.0)
                    return x;
                return std::numeric_limits<double>::quiet_NaN();
            }

            double m_log1p(double x)
            {
                // (as C99: log1p(-1) is -inf (divide-by-zero))
                return std::log1p(x);
            }

            struct double_length { double hi; double lo; };

            double_length dl_fast_sum(double a, double b)
            {
                double const x = a + b;
                double const y = (a - x) + b;
                return { x, y };
            }

            double_length dl_sum(double a, double b)
            {
                double const x = a + b;
                double const z = x - a;
                double const y = (a - (x - z)) + (b - z);
                return { x, y };
            }

            double_length dl_mul(double x, double y)
            {
                double const z = x * y;
                double const zz = std::fma(x, y, -z);
                return { z, zz };
            }

            struct triple_length { double hi; double lo; double tiny; };

            triple_length tl_fma(double x, double y, triple_length total)
            {
                auto const pr = dl_mul(x, y);
                auto const sm = dl_sum(total.hi, pr.hi);
                auto const r1 = dl_sum(total.lo, pr.lo);
                auto const r2 = dl_sum(r1.hi, sm.lo);
                return { sm.hi, r2.hi, total.tiny + r1.lo + r2.lo };
            }

            double tl_to_d(triple_length total)
            {
                auto const last = dl_sum(total.lo, total.hi);
                return total.tiny + last.lo + last.hi;
            }

            double vector_norm(std::vector<double>& vec, double max, bool foundNan)
            {
                auto const n = vec.size();
                if (std::isinf(max))
                    return max;
                if (foundNan)
                    return std::numeric_limits<double>::quiet_NaN();
                if (max == 0.0 || n <= 1u)
                    return max;
                int maxE = 0;
                std::frexp(max, &maxE);
                if (maxE < -1023)
                {
                    for (auto& v : vec)
                        v /= std::numeric_limits<double>::min();
                    return std::numeric_limits<double>::min() * vector_norm(vec, max / std::numeric_limits<double>::min(), foundNan);
                }
                double const scale = std::ldexp(1.0, -maxE);
                double csum = 1.0, frac1 = 0.0, frac2 = 0.0;
                for (auto x : vec)
                {
                    x *= scale;
                    auto const pr = dl_mul(x, x);
                    auto const sm = dl_fast_sum(csum, pr.hi);
                    csum = sm.hi;
                    frac1 += pr.lo;
                    frac2 += sm.lo;
                }
                double h = std::sqrt(csum - 1.0 + (frac1 + frac2));
                auto const pr = dl_mul(-h, h);
                auto const sm = dl_fast_sum(csum, pr.hi);
                csum = sm.hi;
                frac1 += pr.lo;
                frac2 += sm.lo;
                double const x = csum - 1.0 + (frac1 + frac2);
                h += x / (2.0 * h);
                return h / scale;
            }

            // (int(floor(x)) etc.: a float as an int)
            object* int_from_double(double x)
            {
                if (std::isnan(x))
                    return error("ValueError", "cannot convert float NaN to integer");
                if (std::isinf(x))
                    return error("OverflowError", "cannot convert float infinity to integer");
                if (x >= 9223372036854775808.0 || x < -9223372036854775808.0)
                    return int_overflow();
                return make_int(static_cast<long long>(x));
            }

            object* rounding(std::vector<object*> const& aArgs, char const* aName, char const* aSpecial, double (*aFunction)(double))
            {
                if (!arity(aArgs, 1u, 1u, aName))
                    return nullptr;
                auto const x = aArgs[0];
                if (is_int_like(x))
                    return make_int(int_value(x));
                if (kind_of(x) != NEOS_PY_FLOAT)
                    if (ref const method{ special_method(x, aSpecial) })
                    {
                        ref const args{ make_tuple() };
                        return call(method.get(), args.get(), nullptr);
                    }
                double d = 0.0;
                if (kind_of(x) == NEOS_PY_OTHER && std::string_view{ aSpecial } == "__trunc__" && python_type(x) != nullptr && python_type(x)->to_float == nullptr)
                    return error("TypeError", std::string{ "type " } + type_name(x) + " doesn't define __trunc__ method");
                if (!to_double(x, d))
                {
                    if (std::string_view{ aSpecial } == "__trunc__")
                    {
                        clear_error();
                        return error("TypeError", std::string{ "type " } + type_name(x) + " doesn't define __trunc__ method");
                    }
                    return nullptr;
                }
                return int_from_double(aFunction(d));
            }

            bool arg_index(std::vector<object*> const& aArgs, std::size_t aIndex, long long& aResult)
            {
                return to_index(aArgs[aIndex], aResult);
            }

            object* m_floor(std::vector<object*> const& a, object*) { return rounding(a, "floor", "__floor__", [](double x) { return std::floor(x); }); }
            object* m_ceil(std::vector<object*> const& a, object*) { return rounding(a, "ceil", "__ceil__", [](double x) { return std::ceil(x); }); }
            object* m_trunc(std::vector<object*> const& a, object*) { return rounding(a, "trunc", "__trunc__", [](double x) { return std::trunc(x); }); }
            object* m_sqrt(std::vector<object*> const& a, object*) { return math_1(a, "sqrt", [](double x) { return std::sqrt(x); }, false); }
            object* m_cbrt(std::vector<object*> const& a, object*) { return math_1(a, "cbrt", [](double x) { return std::cbrt(x); }, false); }
            object* m_exp(std::vector<object*> const& a, object*) { return math_1(a, "exp", [](double x) { return std::exp(x); }, true); }
            object* m_exp2(std::vector<object*> const& a, object*) { return math_1(a, "exp2", [](double x) { return std::exp2(x); }, true); }
            object* m_expm1(std::vector<object*> const& a, object*) { return math_1(a, "expm1", [](double x) { return std::expm1(x); }, true); }
            object* m_log1p_(std::vector<object*> const& a, object*) { return math_1(a, "log1p", m_log1p, false); }
            object* m_fabs(std::vector<object*> const& a, object*) { return math_1(a, "fabs", [](double x) { return std::fabs(x); }, false); }
            object* m_sin(std::vector<object*> const& a, object*) { return math_1(a, "sin", [](double x) { return std::sin(x); }, false); }
            object* m_cos(std::vector<object*> const& a, object*) { return math_1(a, "cos", [](double x) { return std::cos(x); }, false); }
            object* m_tan(std::vector<object*> const& a, object*) { return math_1(a, "tan", [](double x) { return std::tan(x); }, false); }
            object* m_asin(std::vector<object*> const& a, object*) { return math_1(a, "asin", [](double x) { return std::asin(x); }, false); }
            object* m_acos(std::vector<object*> const& a, object*) { return math_1(a, "acos", [](double x) { return std::acos(x); }, false); }
            object* m_atan(std::vector<object*> const& a, object*) { return math_1(a, "atan", [](double x) { return std::atan(x); }, false); }
            object* m_sinh(std::vector<object*> const& a, object*) { return math_1(a, "sinh", [](double x) { return std::sinh(x); }, true); }
            object* m_cosh(std::vector<object*> const& a, object*) { return math_1(a, "cosh", [](double x) { return std::cosh(x); }, true); }
            object* m_tanh(std::vector<object*> const& a, object*) { return math_1(a, "tanh", [](double x) { return std::tanh(x); }, false); }
            object* m_asinh(std::vector<object*> const& a, object*) { return math_1(a, "asinh", [](double x) { return std::asinh(x); }, false); }
            object* m_acosh(std::vector<object*> const& a, object*) { return math_1(a, "acosh", [](double x) { return std::acosh(x); }, false); }
            object* m_atanh(std::vector<object*> const& a, object*) { return math_1(a, "atanh", [](double x) { return std::atanh(x); }, false); }
            object* m_erf(std::vector<object*> const& a, object*) { return math_1(a, "erf", [](double x) { return std::erf(x); }, false); }
            object* m_erfc(std::vector<object*> const& a, object*) { return math_1(a, "erfc", [](double x) { return std::erfc(x); }, false); }
            object* m_log2_(std::vector<object*> const& a, object*) { return math_1(a, "log2", m_log2, false); }
            object* m_log10_(std::vector<object*> const& a, object*) { return math_1(a, "log10", m_log10, false); }
            object* m_degrees(std::vector<object*> const& a, object*) { return math_1(a, "degrees", [](double x) { return x * (180.0 / pi); }, true); }
            object* m_radians(std::vector<object*> const& a, object*) { return math_1(a, "radians", [](double x) { return x * (pi / 180.0); }, false); }
            object* m_atan2_(std::vector<object*> const& a, object*) { return math_2(a, "atan2", m_atan2); }
            object* m_copysign(std::vector<object*> const& a, object*) { return math_2(a, "copysign", [](double x, double y) { return std::copysign(x, y); }); }
            object* m_remainder_(std::vector<object*> const& a, object*) { return math_2(a, "remainder", m_remainder); }

            object* gamma_function(std::vector<object*> const& aArgs, char const* aName, double (*aFunction)(double, math_error&))
            {
                if (!arity(aArgs, 1u, 1u, aName))
                    return nullptr;
                double x = 0.0;
                if (!arg_double(aArgs, 0, x))
                    return nullptr;
                auto e = math_error::None;
                auto const r = aFunction(x, e);
                return raise_math(e, r);
            }

            object* m_gamma(std::vector<object*> const& a, object*) { return gamma_function(a, "gamma", m_tgamma); }
            object* m_lgamma_(std::vector<object*> const& a, object*) { return gamma_function(a, "lgamma", m_lgamma); }

            object* m_fmod(std::vector<object*> const& aArgs, object*)
            {
                if (!arity(aArgs, 2u, 2u, "fmod"))
                    return nullptr;
                double x = 0.0, y = 0.0;
                if (!arg_double(aArgs, 0, x) || !arg_double(aArgs, 1, y))
                    return nullptr;
                if (std::isinf(y) && std::isfinite(x))
                    return make_float(x);
                auto r = std::fmod(x, y);
                if (r == 0.0 && y != 0.0)
                    r = std::copysign(r, x);
                if (std::isnan(r) && !std::isnan(x) && !std::isnan(y))
                    return error("ValueError", "math domain error");
                return make_float(r);
            }

            object* m_pow(std::vector<object*> const& aArgs, object*)
            {
                if (!arity(aArgs, 2u, 2u, "pow"))
                    return nullptr;
                double x = 0.0, y = 0.0;
                if (!arg_double(aArgs, 0, x) || !arg_double(aArgs, 1, y))
                    return nullptr;
                double r = 0.0;
                auto e = math_error::None;
                if (!std::isfinite(x) || !std::isfinite(y))
                {
                    if (std::isnan(x))
                        r = y == 0.0 ? 1.0 : x;
                    else if (std::isnan(y))
                        r = x == 1.0 ? 1.0 : y;
                    else if (std::isinf(x))
                    {
                        bool const oddY = std::isfinite(y) && std::fmod(std::fabs(y), 2.0) == 1.0;
                        if (y > 0.0)
                            r = oddY ? x : std::fabs(x);
                        else if (y == 0.0)
                            r = 1.0;
                        else
                            r = oddY ? std::copysign(0.0, x) : 0.0;
                    }
                    else if (std::fabs(x) == 1.0)
                        r = 1.0;
                    else if (y > 0.0 && std::fabs(x) > 1.0)
                        r = y;
                    else if (y < 0.0 && std::fabs(x) < 1.0)
                        r = -y;
                    else
                        r = 0.0;
                }
                else
                {
                    r = std::pow(x, y);
                    if (std::isnan(r))
                        e = math_error::Domain;
                    else if (std::isinf(r))
                        e = x == 0.0 ? math_error::Domain : math_error::Range;
                }
                return raise_math(e, r);
            }

            // (log of an int or a float: a domain error if it is not positive)
            bool loghelper(object* aArg, double (*aFunction)(double), double& aResult)
            {
                if (is_int_like(aArg) && int_value(aArg) <= 0)
                {
                    error("ValueError", "math domain error");
                    return false;
                }
                double x = 0.0;
                if (!to_double(aArg, x))
                    return false;
                auto const r = aFunction(x);
                if (std::isnan(r) && !std::isnan(x))
                {
                    error("ValueError", "math domain error");
                    return false;
                }
                if (std::isinf(r) && std::isfinite(x))
                {
                    error("ValueError", "math domain error");
                    return false;
                }
                aResult = r;
                return true;
            }

            object* m_log_(std::vector<object*> const& aArgs, object*)
            {
                if (!arity(aArgs, 1u, 2u, "log"))
                    return nullptr;
                double num = 0.0;
                if (!loghelper(aArgs[0], m_log, num))
                    return nullptr;
                if (aArgs.size() == 1u)
                    return make_float(num);
                double den = 0.0;
                if (!loghelper(aArgs[1], m_log, den))
                    return nullptr;
                if (den == 0.0)
                    return error("ZeroDivisionError", "float division by zero");
                return make_float(num / den);
            }

            object* m_frexp(std::vector<object*> const& aArgs, object*)
            {
                if (!arity(aArgs, 1u, 1u, "frexp"))
                    return nullptr;
                double x = 0.0;
                if (!arg_double(aArgs, 0, x))
                    return nullptr;
                int e = 0;
                double m = x;
                if (!(std::isnan(x) || std::isinf(x) || x == 0.0))
                    m = std::frexp(x, &e);
                return make_tuple({ make_float(m), make_int(e) });
            }

            object* m_ldexp(std::vector<object*> const& aArgs, object*)
            {
                if (!arity(aArgs, 2u, 2u, "ldexp"))
                    return nullptr;
                double x = 0.0;
                if (!arg_double(aArgs, 0, x))
                    return nullptr;
                if (!is_int_like(aArgs[1]))
                    return error("TypeError", "Expected an int as second argument to ldexp.");
                auto const e = int_value(aArgs[1]);
                double r;
                if (x == 0.0 || !std::isfinite(x))
                    r = x;
                else if (e > std::numeric_limits<int>::max())
                    return error("OverflowError", "math range error");
                else if (e < std::numeric_limits<int>::min())
                    r = std::copysign(0.0, x);
                else
                {
                    r = std::ldexp(x, static_cast<int>(e));
                    if (std::isinf(r))
                        return error("OverflowError", "math range error");
                }
                return make_float(r);
            }

            object* m_modf(std::vector<object*> const& aArgs, object*)
            {
                if (!arity(aArgs, 1u, 1u, "modf"))
                    return nullptr;
                double x = 0.0;
                if (!arg_double(aArgs, 0, x))
                    return nullptr;
                if (std::isinf(x))
                    return make_tuple({ make_float(std::copysign(0.0, x)), make_float(x) });
                double i = 0.0;
                double const f = std::modf(x, &i);
                return make_tuple({ make_float(f), make_float(i) });
            }

            object* m_hypot(std::vector<object*> const& aArgs, object*)
            {
                std::vector<double> coordinates;
                double max = 0.0;
                bool foundNan = false;
                for (auto a : aArgs)
                {
                    double x = 0.0;
                    if (!to_double(a, x))
                        return nullptr;
                    x = std::fabs(x);
                    coordinates.push_back(x);
                    foundNan = foundNan || std::isnan(x);
                    if (x > max)
                        max = x;
                }
                return make_float(vector_norm(coordinates, max, foundNan));
            }

            object* m_dist(std::vector<object*> const& aArgs, object*)
            {
                if (!arity(aArgs, 2u, 2u, "dist"))
                    return nullptr;
                std::vector<ref> p, q;
                if (!items_of(aArgs[0], p) || !items_of(aArgs[1], q))
                    return nullptr;
                if (p.size() != q.size())
                    return error("ValueError", "both points must have the same number of dimensions");
                std::vector<double> diffs;
                double max = 0.0;
                bool foundNan = false;
                for (std::size_t i = 0u; i < p.size(); ++i)
                {
                    double px = 0.0, qx = 0.0;
                    if (!to_double(p[i].get(), px) || !to_double(q[i].get(), qx))
                        return nullptr;
                    auto const x = std::fabs(px - qx);
                    diffs.push_back(x);
                    foundNan = foundNan || std::isnan(x);
                    if (x > max)
                        max = x;
                }
                return make_float(vector_norm(diffs, max, foundNan));
            }

            object* m_fsum(std::vector<object*> const& aArgs, object*)
            {
                if (!arity(aArgs, 1u, 1u, "fsum"))
                    return nullptr;
                std::vector<ref> items;
                if (!items_of(aArgs[0], items))
                    return nullptr;
                std::vector<double> p;
                double specialSum = 0.0, infSum = 0.0, lo = 0.0;
                for (auto const& item : items)
                {
                    double x = 0.0;
                    if (!to_double(item.get(), x))
                        return nullptr;
                    double const xsave = x;
                    std::size_t i = 0u;
                    for (std::size_t j = 0u; j < p.size(); ++j)
                    {
                        double y = p[j];
                        if (std::fabs(x) < std::fabs(y))
                            std::swap(x, y);
                        double const hi = x + y;
                        double const yr = hi - x;
                        lo = y - yr;
                        if (lo != 0.0)
                            p[i++] = lo;
                        x = hi;
                    }
                    p.resize(i);
                    if (x != 0.0)
                    {
                        if (!std::isfinite(x))
                        {
                            if (std::isfinite(xsave))
                                return error("OverflowError", "intermediate overflow in fsum");
                            if (std::isinf(xsave))
                                infSum += xsave;
                            specialSum += xsave;
                            p.clear();
                        }
                        else
                            p.push_back(x);
                    }
                }
                if (specialSum != 0.0)
                {
                    if (std::isnan(infSum))
                        return error("ValueError", "-inf + inf in fsum");
                    return make_float(specialSum);
                }
                double hi = 0.0;
                if (!p.empty())
                {
                    auto n = p.size();
                    hi = p[--n];
                    while (n > 0u)
                    {
                        double const x = hi;
                        double const y = p[--n];
                        hi = x + y;
                        double const yr = hi - x;
                        lo = y - yr;
                        if (lo != 0.0)
                            break;
                    }
                    if (n > 0u && ((lo < 0.0 && p[n - 1u] < 0.0) || (lo > 0.0 && p[n - 1u] > 0.0)))
                    {
                        double const y = lo * 2.0;
                        double const x = hi + y;
                        double const yr = x - hi;
                        if (y == yr)
                            hi = x;
                    }
                }
                return make_float(hi);
            }

            object* m_prod(std::vector<object*> const& aArgs, object* aKwargs)
            {
                object* start = nullptr;
                if (!keywords(aKwargs, { { "start", &start } }, "prod") || !arity(aArgs, 1u, 1u, "prod"))
                    return nullptr;
                std::vector<ref> items;
                if (!items_of(aArgs[0], items))
                    return nullptr;
                ref result{ start != nullptr ? (retain(start), start) : make_int(1) };
                for (auto const& item : items)
                {
                    result = ref{ binary(NEOS_PY_MULTIPLY, result.get(), item.get(), false) };
                    if (!result)
                        return nullptr;
                }
                return result.release_ownership();
            }

            object* m_sumprod(std::vector<object*> const& aArgs, object*)
            {
                if (!arity(aArgs, 2u, 2u, "sumprod"))
                    return nullptr;
                std::vector<ref> p, q;
                if (!items_of(aArgs[0], p) || !items_of(aArgs[1], q))
                    return nullptr;
                if (p.size() != q.size())
                    return error("ValueError", "Inputs are not the same length");
                ref total{ make_int(0) };
                bool intEnabled = true, intInUse = false, fltEnabled = true, fltInUse = false;
                long long intTotal = 0;
                triple_length fltTotal{ 0.0, 0.0, 0.0 };
                for (std::size_t i = 0u; i <= p.size(); ++i)
                {
                    bool const finished = i == p.size();
                    object* const pi = finished ? nullptr : p[i].get();
                    object* const qi = finished ? nullptr : q[i].get();
                    if (intEnabled)
                    {
                        if (!finished && kind_of(pi) == NEOS_PY_INT && kind_of(qi) == NEOS_PY_INT)
                        {
                            long long product = 0, sum = 0;
                            if (!multiply_overflow(int_value(pi), int_value(qi), &product) && !add_overflow(intTotal, product, &sum))
                            {
                                intTotal = sum;
                                intInUse = true;
                                continue;
                            }
                        }
                        intEnabled = false;
                        if (intInUse)
                        {
                            ref const term{ make_int(intTotal) };
                            total = ref{ binary(NEOS_PY_ADD, total.get(), term.get(), false) };
                            if (!total)
                                return nullptr;
                            intInUse = false;
                        }
                    }
                    if (fltEnabled)
                    {
                        if (!finished)
                        {
                            bool const pf = kind_of(pi) == NEOS_PY_FLOAT, qf = kind_of(qi) == NEOS_PY_FLOAT;
                            bool const pInt = kind_of(pi) == NEOS_PY_INT || kind_of(pi) == NEOS_PY_BOOL;
                            bool const qInt = kind_of(qi) == NEOS_PY_INT || kind_of(qi) == NEOS_PY_BOOL;
                            if ((pf && qf) || (pf && qInt) || (qf && pInt))
                            {
                                double fp = 0.0, fq = 0.0;
                                to_double(pi, fp);
                                to_double(qi, fq);
                                auto const next = tl_fma(fp, fq, fltTotal);
                                if (std::isfinite(next.hi))
                                {
                                    fltTotal = next;
                                    fltInUse = true;
                                    continue;
                                }
                            }
                        }
                        fltEnabled = false;
                        if (fltInUse)
                        {
                            ref const term{ make_float(tl_to_d(fltTotal)) };
                            total = ref{ binary(NEOS_PY_ADD, total.get(), term.get(), false) };
                            if (!total)
                                return nullptr;
                            fltInUse = false;
                        }
                    }
                    if (finished)
                        break;
                    ref const term{ binary(NEOS_PY_MULTIPLY, pi, qi, false) };
                    if (!term)
                        return nullptr;
                    total = ref{ binary(NEOS_PY_ADD, total.get(), term.get(), false) };
                    if (!total)
                        return nullptr;
                }
                return total.release_ownership();
            }

            object* m_isclose(std::vector<object*> const& aArgs, object* aKwargs)
            {
                object* relTolObject = nullptr;
                object* absTolObject = nullptr;
                if (!keywords(aKwargs, { { "rel_tol", &relTolObject }, { "abs_tol", &absTolObject } }, "isclose") || !arity(aArgs, 2u, 2u, "isclose"))
                    return nullptr;
                double a = 0.0, b = 0.0, relTol = 1e-09, absTol = 0.0;
                if (!to_double(aArgs[0], a) || !to_double(aArgs[1], b) || (relTolObject != nullptr && !to_double(relTolObject, relTol)) ||
                    (absTolObject != nullptr && !to_double(absTolObject, absTol)))
                    return nullptr;
                if (relTol < 0.0 || absTol < 0.0)
                    return error("ValueError", "tolerances must be non-negative");
                if (a == b)
                    return make_bool(true);
                if (std::isinf(a) || std::isinf(b))
                    return make_bool(false);
                double const diff = std::fabs(b - a);
                return make_bool((diff <= std::fabs(relTol * b)) || (diff <= std::fabs(relTol * a)) || (diff <= absTol));
            }

            object* predicate(std::vector<object*> const& aArgs, char const* aName, bool (*aTest)(double))
            {
                if (!arity(aArgs, 1u, 1u, aName))
                    return nullptr;
                double x = 0.0;
                if (!arg_double(aArgs, 0, x))
                    return nullptr;
                return make_bool(aTest(x));
            }

            object* m_isnan(std::vector<object*> const& a, object*) { return predicate(a, "isnan", [](double x) { return std::isnan(x); }); }
            object* m_isinf(std::vector<object*> const& a, object*) { return predicate(a, "isinf", [](double x) { return std::isinf(x); }); }
            object* m_isfinite(std::vector<object*> const& a, object*) { return predicate(a, "isfinite", [](double x) { return std::isfinite(x); }); }

            object* m_factorial(std::vector<object*> const& aArgs, object*)
            {
                if (!arity(aArgs, 1u, 1u, "factorial"))
                    return nullptr;
                long long n = 0;
                if (!arg_index(aArgs, 0, n))
                    return nullptr;
                if (n < 0)
                    return error("ValueError", "factorial() not defined for negative values");
                long long r = 1;
                for (long long i = 2; i <= n; ++i)
                    if (multiply_overflow(r, i, &r))
                        return int_overflow();
                return make_int(r);
            }

            object* m_isqrt(std::vector<object*> const& aArgs, object*)
            {
                if (!arity(aArgs, 1u, 1u, "isqrt"))
                    return nullptr;
                long long n = 0;
                if (!arg_index(aArgs, 0, n))
                    return nullptr;
                if (n < 0)
                    return error("ValueError", "isqrt() argument must be nonnegative");
                auto r = static_cast<unsigned long long>(std::sqrt(static_cast<double>(n)));
                while (r * r > static_cast<unsigned long long>(n))
                    --r;
                while ((r + 1u) * (r + 1u) <= static_cast<unsigned long long>(n))
                    ++r;
                return make_int(static_cast<long long>(r));
            }

            long long gcd_of(long long a, long long b)
            {
                a = a < 0 ? -a : a;
                b = b < 0 ? -b : b;
                while (b != 0)
                {
                    auto const t = a % b;
                    a = b;
                    b = t;
                }
                return a;
            }

            object* m_gcd(std::vector<object*> const& aArgs, object*)
            {
                long long r = 0;
                for (std::size_t i = 0u; i < aArgs.size(); ++i)
                {
                    long long v = 0;
                    if (!arg_index(aArgs, i, v))
                        return nullptr;
                    r = gcd_of(r, v);
                }
                return make_int(r);
            }

            object* m_lcm(std::vector<object*> const& aArgs, object*)
            {
                long long r = 1;
                for (std::size_t i = 0u; i < aArgs.size(); ++i)
                {
                    long long v = 0;
                    if (!arg_index(aArgs, i, v))
                        return nullptr;
                    if (r == 0 || v == 0)
                    {
                        r = 0;
                        continue;
                    }
                    auto const g = gcd_of(r, v);
                    long long m = 0;
                    if (multiply_overflow(r / g, v, &m))
                        return int_overflow();
                    r = m < 0 ? -m : m;
                }
                return make_int(r);
            }

            object* combinatorial(std::vector<object*> const& aArgs, char const* aName, bool aComb)
            {
                if (!arity(aArgs, aComb ? 2u : 1u, 2u, aName))
                    return nullptr;
                long long n = 0, k = 0;
                if (!arg_index(aArgs, 0, n))
                    return nullptr;
                bool const all = aArgs.size() == 1u || kind_of(aArgs[1]) == NEOS_PY_NONE;
                if (!all && !arg_index(aArgs, 1, k))
                    return nullptr;
                if (n < 0)
                    return error("ValueError", "n must be a non-negative integer");
                if (all)
                    k = n;
                if (k < 0)
                    return error("ValueError", "k must be a non-negative integer");
                if (k > n)
                    return make_int(0);
                if (aComb)
                {
                    k = std::min(k, n - k);
                    // (r * (n - k + i) is divisible by i: divided by their common factors first)
                    long long r = 1;
                    for (long long i = 1; i <= k; ++i)
                    {
                        auto const g = gcd_of(r, i);
                        auto const divisor = i / g;
                        long long product = 0;
                        if (multiply_overflow(r / g, (n - k + i) / divisor, &product))
                            return int_overflow();
                        r = product;
                    }
                    return make_int(r);
                }
                long long r = 1;
                for (long long i = 0; i < k; ++i)
                    if (multiply_overflow(r, n - i, &r))
                        return int_overflow();
                return make_int(r);
            }

            object* m_comb(std::vector<object*> const& a, object*) { return combinatorial(a, "comb", true); }
            object* m_perm(std::vector<object*> const& a, object*) { return combinatorial(a, "perm", false); }

            object* m_nextafter(std::vector<object*> const& aArgs, object* aKwargs)
            {
                object* steps = nullptr;
                if (!keywords(aKwargs, { { "steps", &steps } }, "nextafter") || !arity(aArgs, 2u, 2u, "nextafter"))
                    return nullptr;
                double x = 0.0, y = 0.0;
                if (!arg_double(aArgs, 0, x) || !arg_double(aArgs, 1, y))
                    return nullptr;
                if (steps == nullptr || kind_of(steps) == NEOS_PY_NONE)
                    return make_float(std::nextafter(x, y));
                long long n = 0;
                if (!to_index(steps, n))
                    return nullptr;
                if (n < 0)
                    return error("ValueError", "steps must be a non-negative integer");
                for (long long i = 0; i < n && x != y && !std::isnan(x) && !std::isnan(y); ++i)
                    x = std::nextafter(x, y);
                if (n > 0 && std::isnan(y))
                    x = y;
                return make_float(x);
            }

            object* m_ulp(std::vector<object*> const& aArgs, object*)
            {
                if (!arity(aArgs, 1u, 1u, "ulp"))
                    return nullptr;
                double x = 0.0;
                if (!arg_double(aArgs, 0, x))
                    return nullptr;
                if (std::isnan(x))
                    return make_float(x);
                x = std::fabs(x);
                if (std::isinf(x))
                    return make_float(x);
                double const x2 = std::nextafter(x, std::numeric_limits<double>::infinity());
                if (std::isinf(x2))
                    return make_float(x - std::nextafter(x, -std::numeric_limits<double>::infinity()));
                return make_float(x2 - x);
            }

            builtin_entry const sFunctions[] =
            {
                { "acos", m_acos }, { "acosh", m_acosh }, { "asin", m_asin }, { "asinh", m_asinh }, { "atan", m_atan }, { "atan2", m_atan2_ },
                { "atanh", m_atanh }, { "cbrt", m_cbrt }, { "ceil", m_ceil }, { "comb", m_comb }, { "copysign", m_copysign }, { "cos", m_cos },
                { "cosh", m_cosh }, { "degrees", m_degrees }, { "dist", m_dist }, { "erf", m_erf }, { "erfc", m_erfc }, { "exp", m_exp },
                { "exp2", m_exp2 }, { "expm1", m_expm1 }, { "fabs", m_fabs }, { "factorial", m_factorial }, { "floor", m_floor }, { "fmod", m_fmod },
                { "frexp", m_frexp }, { "fsum", m_fsum }, { "gamma", m_gamma }, { "gcd", m_gcd }, { "hypot", m_hypot }, { "isclose", m_isclose },
                { "isfinite", m_isfinite }, { "isinf", m_isinf }, { "isnan", m_isnan }, { "isqrt", m_isqrt }, { "lcm", m_lcm }, { "ldexp", m_ldexp },
                { "lgamma", m_lgamma_ }, { "log", m_log_ }, { "log10", m_log10_ }, { "log1p", m_log1p_ }, { "log2", m_log2_ }, { "modf", m_modf },
                { "nextafter", m_nextafter }, { "perm", m_perm }, { "pow", m_pow }, { "prod", m_prod }, { "radians", m_radians },
                { "remainder", m_remainder_ }, { "sin", m_sin }, { "sinh", m_sinh }, { "sqrt", m_sqrt }, { "sumprod", m_sumprod }, { "tan", m_tan },
                { "tanh", m_tanh }, { "trunc", m_trunc }, { "ulp", m_ulp }
            };
        }

        object* make_module(std::string const& aName)
        {
            auto* const m = new module_object{ { { &sModuleType.base } }, aName, {} };
            ref result{ &m->h.base };
            if (aName == "math")
            {
                for (auto const& f : math_module::sFunctions)
                    m->attributes.emplace_back(f.name, make_function(f.name, call_builtin, const_cast<builtin_entry*>(&f)));
                m->attributes.emplace_back("pi", make_float(3.141592653589793));
                m->attributes.emplace_back("e", make_float(2.718281828459045));
                m->attributes.emplace_back("tau", make_float(6.283185307179586));
                m->attributes.emplace_back("inf", make_float(std::numeric_limits<double>::infinity()));
                m->attributes.emplace_back("nan", make_float(std::numeric_limits<double>::quiet_NaN()));
            }
            else if (aName == "sys")
            {
                m->attributes.emplace_back("maxsize", make_int(std::numeric_limits<long long>::max()));
                m->attributes.emplace_back("byteorder", make_str("little"));
                m->attributes.emplace_back("argv", make_list({ make_str("") }));
#if defined(_WIN32)
                m->attributes.emplace_back("platform", make_str("win32"));
#elif defined(__APPLE__)
                m->attributes.emplace_back("platform", make_str("darwin"));
#else
                m->attributes.emplace_back("platform", make_str("linux"));
#endif
                m->attributes.emplace_back("version", make_str("3.13 (neos)"));
                m->attributes.emplace_back("version_info", make_tuple({ make_int(3), make_int(13), make_int(0), make_str("final"), make_int(0) }));
                m->attributes.emplace_back("float_repr_style", make_str("short"));
            }
            else
                return error("ModuleNotFoundError", "No module named '" + aName + "'");
            return result.release_ownership();
        }

        // python::import(name)
        object* fn_import(char const* aName)
        {
            static std::map<std::string, object*> sModules;
            auto& m = sModules[aName];
            if (m == nullptr)
            {
                m = make_module(aName);
                if (m == nullptr)
                {
                    sModules.erase(aName);
                    return nullptr;
                }
            }
            retain(m);
            return m;
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
            if (is_exception_class(name))
                return exception_class(std::string{ name });
            return error("NameError", "name '" + std::string{ name } + "' is not defined");
        }

        // a, b = value
        // (calls with *iterable and **mapping arguments)
        void fn_args_extend(object* aList, object* aIterable)
        {
            auto const k = kind_of(aIterable);
            if (k == NEOS_PY_OTHER ? python_type(aIterable) == nullptr || (python_type(aIterable)->iter == nullptr && python_type(aIterable)->getitem == nullptr) :
                (k != NEOS_PY_LIST && k != NEOS_PY_TUPLE && k != NEOS_PY_STR && k != NEOS_PY_DICT && k != NEOS_PY_RANGE && k != NEOS_PY_ITERATOR))
            {
                error("TypeError", std::string{ "Value after * must be an iterable, not " } + type_name(aIterable));
                return;
            }
            std::vector<object*> items;
            if (!collect(aIterable, items))
            {
                for (auto i : items)
                    release(i);
                return;
            }
            for (auto i : items)
                as<list_object>(aList)->items.push_back(i);
        }
        void fn_kwargs_update(object* aDict, object* aMapping)
        {
            if (kind_of(aMapping) != NEOS_PY_DICT)
            {
                error("TypeError", std::string{ "argument after ** must be a mapping, not " } + type_name(aMapping));
                return;
            }
            for (auto const& item : as<dict_object>(aMapping)->items)
            {
                if (kind_of(item.first) != NEOS_PY_STR)
                {
                    error("TypeError", "keywords must be strings");
                    return;
                }
                for (auto const& existing : as<dict_object>(aDict)->items)
                    if (as<str_object>(existing.first)->value == as<str_object>(item.first)->value)
                    {
                        error("TypeError", "got multiple values for keyword argument '" + as<str_object>(item.first)->value + "'");
                        return;
                    }
                dict_set(aDict, item.first, item.second);
            }
        }
        object* fn_list_tuple(object* aList)
        {
            auto items = as<list_object>(aList)->items;
            for (auto i : items)
                retain(i);
            return make_tuple(std::move(items));
        }

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
            { "from_complex", NEOS_RT_FUNCTION(fn_from_complex), OBJ, 2u, { F64, F64 }, 0u },
            { "truth", NEOS_RT_FUNCTION(fn_truth), BOOL, 1u, { OBJ }, 0u },
            { "str", NEOS_RT_FUNCTION(fn_str), STR, 1u, { OBJ }, 0u },
            { "repr", NEOS_RT_FUNCTION(fn_repr), STR, 1u, { OBJ }, 0u },
            { "format_field", NEOS_RT_FUNCTION(fn_format_field), STR, 3u, { OBJ, I32, STR }, 0u },
            { "__fstring(object,i64,string)", NEOS_RT_FUNCTION(fn_fstring), STR, 3u, { OBJ, I64, STR }, 0u },
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
            { "function", NEOS_RT_FUNCTION(fn_function), OBJ, 4u, { STR, I32, STR, OBJ }, 0u },
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
            { "args_extend", NEOS_RT_FUNCTION(fn_args_extend), VOID, 2u, { OBJ, OBJ }, 0u },
            { "kwargs_update", NEOS_RT_FUNCTION(fn_kwargs_update), VOID, 2u, { OBJ, OBJ }, 0u },
            { "list_tuple", NEOS_RT_FUNCTION(fn_list_tuple), OBJ, 1u, { OBJ }, 0u },
            { "raise", NEOS_RT_FUNCTION(fn_raise), VOID, 1u, { OBJ }, 0u },
            { "zero_division", NEOS_RT_FUNCTION(fn_zero_division), VOID, 1u, { I32 }, 0u },
            { "import", NEOS_RT_FUNCTION(fn_import), OBJ, 1u, { STR }, 0u },
            { "reraise", NEOS_RT_FUNCTION(fn_reraise), VOID, 0u, {}, 0u },
            { "handled", NEOS_RT_FUNCTION(fn_handled), VOID, 0u, {}, 0u },
            { "with_enter", NEOS_RT_FUNCTION(fn_with_enter), OBJ, 1u, { OBJ }, 0u },
            { "with_exit", NEOS_RT_FUNCTION(fn_with_exit), VOID, 1u, { OBJ }, 0u },
            { "with_exit_exception", NEOS_RT_FUNCTION(fn_with_exit_exception), BOOL, 2u, { OBJ, STR }, 0u },
            { "caught", NEOS_RT_FUNCTION(fn_caught), OBJ, 1u, { STR }, 0u },
            { "exception_matches", NEOS_RT_FUNCTION(fn_exception_matches), BOOL, 2u, { OBJ, OBJ }, 0u },
            { "assertion", NEOS_RT_FUNCTION(fn_assertion), VOID, 1u, { OBJ }, 0u },
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
