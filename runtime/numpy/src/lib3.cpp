/*
  lib3.cpp

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

// type information (finfo, iinfo, issubdtype, ...), index objects (mgrid, ogrid, r_, c_, s_, index_exp), functional
// programming (apply_along_axis, vectorize, ...), pad, packbits and other functions of the numpy module

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include "numpy.hpp"
#include "cast.hpp"
#include "functions.hpp"
#include "py.hpp"

namespace neos::numpy
{
    namespace
    {
        using namespace py;

        value arg(arguments const& aArgs, std::size_t aIndex)
        {
            return value::borrow(aArgs.get(aIndex));
        }

        value arg(arguments const& aArgs, std::size_t aIndex, value const& aDefault)
        {
            return aArgs.has(aIndex) ? value::borrow(aArgs.get(aIndex)) : aDefault;
        }

        value asarray(value const& aValue)
        {
            return np("asarray")(aValue);
        }

        value int_tuple(std::vector<long long> const& aValues)
        {
            std::vector<value> items;
            for (auto v : aValues)
                items.emplace_back(v);
            return tuple(items);
        }

        long long normalize_axis_index(long long aAxis, long long aNdim)
        {
            if (aAxis < -aNdim || aAxis >= aNdim)
                raise("AxisError", "axis " + std::to_string(aAxis) + " is out of bounds for array of dimension " + std::to_string(aNdim));
            return aAxis < 0 ? aAxis + aNdim : aAxis;
        }

        dtype type_of(value const& aArray)
        {
            return aArray.array()->type;
        }

        value dtype_value(dtype aType)
        {
            return value::borrow(dtype_object(aType));
        }

        value scalar_value_of(dtype aType, double aValue)
        {
            return np(info(aType).scalarName)(aValue);
        }

        // --- instances: objects with attributes, a repr and (optionally) item access and calls

        struct instance
        {
            neos_rt_object base;
            std::atomic<long> references{ 1 };
            std::string typeName;
            std::string reprText;
            std::string strText;
            std::vector<std::pair<std::string, object*>> attributes;
            std::function<value(value const&)> getitem;
            std::function<value(value const&, value const&)> call;
            std::function<long long()> length;
            std::function<void(value const&, value const&)> setitem;
            std::function<value()> iter;
            std::function<value(char const*)> getattr;     // (attributes computed when read; empty: none)
            std::function<value()> next;                    // (empty: exhausted)
        };

        void instance_retain(neos_rt_object* aObject) { ++reinterpret_cast<instance*>(aObject)->references; }
        void instance_release(neos_rt_object* aObject)
        {
            auto* const i = reinterpret_cast<instance*>(aObject);
            if (--i->references == 0)
            {
                for (auto& a : i->attributes)
                    ctx->release(a.second);
                delete i;
            }
        }

        template <typename F>
        object* guarded(F&& aFunction)
        {
            try
            {
                return aFunction();
            }
            catch (raised const&)
            {
                return nullptr;
            }
        }

        object* instance_repr(neos_py_context*, object* aSelf)
        {
            auto const* i = reinterpret_cast<instance const*>(aSelf);
            return make_str(i->reprText);
        }

        object* instance_str(neos_py_context*, object* aSelf)
        {
            auto const* i = reinterpret_cast<instance const*>(aSelf);
            return make_str(i->strText.empty() ? i->reprText : i->strText);
        }

        object* instance_getattr(neos_py_context*, object* aSelf, char const* aName)
        {
            auto const* i = reinterpret_cast<instance const*>(aSelf);
            for (auto const& a : i->attributes)
                if (a.first == aName)
                {
                    ctx->retain(a.second);
                    return a.second;
                }
            if (i->getattr)
            {
                auto const result = guarded([&]() { auto const v = i->getattr(aName); return v.empty() ? nullptr : v.release(); });
                if (result != nullptr || failed())
                    return result;
            }
            return error("AttributeError", "'" + i->typeName + "' object has no attribute '" + aName + "'");
        }

        object* instance_getitem(neos_py_context*, object* aSelf, object* aKey)
        {
            auto const* i = reinterpret_cast<instance const*>(aSelf);
            if (!i->getitem)
                return error("TypeError", "'" + i->typeName + "' object is not subscriptable");
            return guarded([&]() { return i->getitem(value::borrow(aKey)).release(); });
        }

        object* instance_call(neos_py_context*, object* aSelf, object* aArgs, object* aKwargs)
        {
            auto const* i = reinterpret_cast<instance const*>(aSelf);
            if (!i->call)
                return error("TypeError", "'" + i->typeName + "' object is not callable");
            return guarded([&]() { return i->call(value::borrow(aArgs), value::borrow(aKwargs)).release(); });
        }

        long long instance_length(neos_py_context*, object* aSelf)
        {
            auto const* i = reinterpret_cast<instance const*>(aSelf);
            if (!i->length)
            {
                error("TypeError", "object of type '" + i->typeName + "' has no len()");
                return -1;
            }
            return i->length();
        }

        int instance_setitem(neos_py_context*, object* aSelf, object* aKey, object* aValue)
        {
            auto const* i = reinterpret_cast<instance const*>(aSelf);
            if (!i->setitem)
            {
                error("TypeError", "'" + i->typeName + "' object does not support item assignment");
                return -1;
            }
            try
            {
                i->setitem(value::borrow(aKey), value::borrow(aValue));
                return 0;
            }
            catch (raised const&)
            {
                return -1;
            }
        }

        object* instance_iter(neos_py_context*, object* aSelf)
        {
            auto const* i = reinterpret_cast<instance const*>(aSelf);
            if (!i->iter)
                return error("TypeError", "'" + i->typeName + "' object is not iterable");
            return guarded([&]() { return i->iter().release(); });
        }

        object* instance_next(neos_py_context*, object* aSelf)
        {
            auto const* i = reinterpret_cast<instance const*>(aSelf);
            if (!i->next)
                return error("TypeError", "'" + i->typeName + "' object is not an iterator");
            return guarded([&]() -> object* { auto const v = i->next(); return v.empty() ? nullptr : v.release(); });
        }

        neos_py_type make_instance_type()
        {
            neos_py_type type{};
            type.base = neos_rt_object_type{ "numpy.instance", instance_retain, instance_release };
            type.magic = NEOS_PY_TYPE_MAGIC;
            type.name = "object";
            type.repr = instance_repr;
            type.str = instance_str;
            type.getattr = instance_getattr;
            type.getitem = instance_getitem;
            type.call = instance_call;
            type.length = instance_length;
            type.setitem = instance_setitem;
            type.iter = instance_iter;
            type.next = instance_next;
            return type;
        }

        neos_py_type const sInstanceType = make_instance_type();

        instance* new_instance(std::string const& aTypeName)
        {
            return new instance{ { &sInstanceType.base }, 1, aTypeName, {}, {}, {}, {}, {}, {}, {}, {} };
        }

        void add_attribute(instance* aInstance, std::string const& aName, value const& aValue)
        {
            aInstance->attributes.emplace_back(aName, aValue.release());
        }

        value instance_value(instance* aInstance)
        {
            return value::steal(&aInstance->base);
        }

        // --- finfo and iinfo

        std::string printf_e(double aValue, int aPrecision)
        {
            char buffer[64];
            std::snprintf(buffer, sizeof(buffer), "%.*e", aPrecision, aValue);
            return buffer;
        }

        object* f_finfo(object* aArgs, object* aKwargs)
        {
            arguments args{ "finfo", aArgs, aKwargs, { "dtype" } };
            if (!args.ok())
                return nullptr;
            auto const d = arg(args, 0);
            std::optional<dtype> t;
            if (is_array(d.get()))
                t = type_of(d);
            else if (kind(d.get()) == NEOS_PY_FLOAT)
                t = dtype::Float64;
            else
            {
                t = parse_dtype(d.get());
                if (!t)
                    throw raised{};
            }
            if (is_complex(*t))
                t = real_for(*t);
            if (!is_float(*t))
                raise("ValueError", std::string{ "data type <class 'numpy." } + info(*t).scalarName + "'> not inexact");
            struct parameters { int bits, nexp, nmant, machep, negep, minexp, maxexp, precision; double max, tiny, subnormal; int fmt; };
            parameters const p = *t == dtype::Float16 ? parameters{ 16, 5, 10, -10, -11, -14, 16, 3, 65504.0, std::ldexp(1.0, -14), std::ldexp(1.0, -24), 5 } :
                *t == dtype::Float32 ? parameters{ 32, 8, 23, -23, -24, -126, 128, 6, static_cast<double>(std::numeric_limits<float>::max()),
                    static_cast<double>(std::numeric_limits<float>::min()), static_cast<double>(std::numeric_limits<float>::denorm_min()), 7 } :
                parameters{ 64, 11, 52, -52, -53, -1022, 1024, 15, std::numeric_limits<double>::max(), std::numeric_limits<double>::min(), std::numeric_limits<double>::denorm_min(), 16 };
            auto const s = [&](double aValue) { return scalar_value_of(*t, aValue); };
            auto const eps = s(std::ldexp(1.0, p.machep));
            auto const epsneg = s(std::ldexp(1.0, p.negep));
            auto const maximum = s(p.max);
            auto const minimum = s(-p.max);
            auto const tiny = s(p.tiny);
            auto const subnormal = s(p.subnormal);
            auto const resolution = s(std::pow(10.0, -p.precision));
            auto* const i = new_instance("finfo");
            auto const result = instance_value(i);
            add_attribute(i, "bits", p.bits);
            add_attribute(i, "dtype", dtype_value(*t));
            add_attribute(i, "eps", eps);
            add_attribute(i, "epsneg", epsneg);
            add_attribute(i, "iexp", p.nexp);
            add_attribute(i, "machep", p.machep);
            add_attribute(i, "max", maximum);
            add_attribute(i, "maxexp", p.maxexp);
            add_attribute(i, "min", minimum);
            add_attribute(i, "minexp", p.minexp);
            add_attribute(i, "negep", p.negep);
            add_attribute(i, "nexp", p.nexp);
            add_attribute(i, "nmant", p.nmant);
            add_attribute(i, "precision", p.precision);
            add_attribute(i, "resolution", resolution);
            add_attribute(i, "smallest_normal", tiny);
            add_attribute(i, "smallest_subnormal", subnormal);
            add_attribute(i, "tiny", tiny);
            std::string const name = info(*t).name;
            auto const strMax = printf_e(p.max, p.fmt);
            i->reprText = "finfo(resolution=" + resolution.str() + ", min=-" + strMax + ", max=" + strMax + ", dtype=" + name + ")";
            std::string const line = "---------------------------------------------------------------\n";
            i->strText = "Machine parameters for " + name + "\n" + line +
                "precision = " + std::to_string(p.precision) + "   resolution = " + resolution.str() + "\n" +
                "machep = " + std::to_string(p.machep) + "   eps =        " + eps.str() + "\n" +
                "negep =  " + std::to_string(p.negep) + "   epsneg =     " + epsneg.str() + "\n" +
                "minexp = " + std::to_string(p.minexp) + "   tiny =       " + tiny.str() + "\n" +
                "maxexp = " + std::to_string(p.maxexp) + "   max =        " + maximum.str() + "\n" +
                "nexp =   " + std::to_string(p.nexp) + "   min =        -max\n" +
                "smallest_normal = " + tiny.str() + "   smallest_subnormal = " + subnormal.str() + "\n" + line;
            return result.release();
        }

        object* f_iinfo(object* aArgs, object* aKwargs)
        {
            arguments args{ "iinfo", aArgs, aKwargs, { "int_type" } };
            if (!args.ok())
                return nullptr;
            auto const d = arg(args, 0);
            std::optional<dtype> t;
            if (is_array(d.get()))
                t = type_of(d);
            else if (kind(d.get()) == NEOS_PY_INT)
                t = dtype::Int64;
            else
            {
                t = parse_dtype(d.get());
                if (!t)
                    throw raised{};
            }
            if (!is_integer(*t))
                raise("ValueError", std::string{ "Invalid integer data type '" } + info(*t).kind + "'.");
            auto const bits = static_cast<int>(info(*t).itemsize * 8u);
            long long minimum = 0, maximum = 0;
            std::string minText, maxText;
            if (is_signed(*t))
            {
                minimum = bits == 64 ? std::numeric_limits<long long>::min() : -(1ll << (bits - 1));
                maximum = bits == 64 ? std::numeric_limits<long long>::max() : (1ll << (bits - 1)) - 1;
                minText = std::to_string(minimum);
                maxText = std::to_string(maximum);
            }
            else
            {
                maximum = bits == 64 ? -1 : static_cast<long long>((1ull << bits) - 1);
                minText = "0";
                maxText = bits == 64 ? "18446744073709551615" : std::to_string(maximum);
            }
            auto* const i = new_instance("iinfo");
            auto const result = instance_value(i);
            add_attribute(i, "bits", bits);
            add_attribute(i, "dtype", dtype_value(*t));
            add_attribute(i, "kind", std::string(1u, info(*t).kind));
            add_attribute(i, "min", minimum);
            add_attribute(i, "max", maximum);
            std::string const name = info(*t).name;
            i->reprText = "iinfo(min=" + minText + ", max=" + maxText + ", dtype=" + name + ")";
            std::string const line = "---------------------------------------------------------------\n";
            i->strText = "Machine parameters for " + name + "\n" + line + "min = " + minText + "\n" + "max = " + maxText + "\n" + line;
            return result.release();
        }

        // --- type functions

        // (a class object's name, e.g. "floating"; empty if it is not one of ours)
        std::string class_name(value const& aValue)
        {
            if (type_name(aValue.get()) != "type")
                return {};
            auto const module = value::steal(ctx->getattr(ctx, aValue.get(), "__module__"));
            if (as_str(module.get()) != std::optional<std::string>{ "numpy" })
                return {};
            return aValue.attr("__name__").to_str();
        }

        std::optional<bool> abstract_member(std::string const& aClass, dtype aType)
        {
            if (aClass == "generic") return true;
            if (aClass == "number") return !is_bool(aType);
            if (aClass == "integer") return is_integer(aType);
            if (aClass == "signedinteger") return is_signed(aType);
            if (aClass == "unsignedinteger") return is_unsigned(aType);
            if (aClass == "inexact") return is_inexact(aType);
            if (aClass == "floating") return is_float(aType);
            if (aClass == "complexfloating") return is_complex(aType);
            return {};
        }

        object* f_issubdtype(object* aArgs, object* aKwargs)
        {
            arguments args{ "issubdtype", aArgs, aKwargs, { "arg1", "arg2" } };
            if (!args.ok())
                return nullptr;
            auto const a1 = arg(args, 0), a2 = arg(args, 1);
            auto const abstract1 = class_name(a1);
            auto const abstract2 = class_name(a2);
            if (!abstract1.empty() && abstract_member(abstract1, dtype::Bool).has_value())
            {
                // (an abstract class: a subclass of another abstract class?)
                static std::map<std::string, std::string> const sParents =
                {
                    { "number", "generic" }, { "integer", "number" }, { "signedinteger", "integer" }, { "unsignedinteger", "integer" },
                    { "inexact", "number" }, { "floating", "inexact" }, { "complexfloating", "inexact" }
                };
                for (std::string c = abstract1; !c.empty(); c = sParents.contains(c) ? sParents.at(c) : std::string{})
                    if (c == abstract2)
                        return make_bool(true);
                return make_bool(false);
            }
            auto const t1 = parse_dtype(a1.get());
            if (!t1)
                throw raised{};
            if (!abstract2.empty())
                if (auto const member = abstract_member(abstract2, *t1))
                    return make_bool(*member);
            auto const t2 = parse_dtype(a2.get());
            if (!t2)
                throw raised{};
            return make_bool(*t1 == *t2);
        }

        object* f_isdtype(object* aArgs, object* aKwargs)
        {
            arguments args{ "isdtype", aArgs, aKwargs, { "dtype", "kind" } };
            if (!args.ok())
                return nullptr;
            auto const d = arg(args, 0);
            if (type_name(d.get()) != "dtype" && class_name(d).empty())
                raise("TypeError", "dtype argument must be a NumPy dtype, but it is a " + type_name(d.get()) + ".");
            auto const t = parse_dtype(d.get());
            if (!t)
                throw raised{};
            auto const k = arg(args, 1);
            std::vector<value> kinds = is_tuple(k) ? k.items() : std::vector<value>{ k };
            for (auto const& x : kinds)
            {
                if (is_str(x))
                {
                    auto const s = x.to_str();
                    bool match;
                    if (s == "bool") match = is_bool(*t);
                    else if (s == "signed integer") match = is_signed(*t);
                    else if (s == "unsigned integer") match = is_unsigned(*t);
                    else if (s == "integral") match = is_integer(*t);
                    else if (s == "real floating") match = is_float(*t);
                    else if (s == "complex floating") match = is_complex(*t);
                    else if (s == "numeric") match = !is_bool(*t);
                    else
                        raise("ValueError", "kind argument is a string, but '" + s + "' is not a known kind name.");
                    if (match)
                        return make_bool(true);
                }
                else
                {
                    auto const other = parse_dtype(x.get());
                    if (!other)
                        throw raised{};
                    if (*other == *t)
                        return make_bool(true);
                }
            }
            return make_bool(false);
        }

        object* f_min_scalar_type(object* aArgs, object* aKwargs)
        {
            arguments args{ "min_scalar_type", aArgs, aKwargs, { "a" } };
            if (!args.ok())
                return nullptr;
            auto const a = arg(args, 0);
            auto const x = asarray(a);
            auto const t = type_of(x);
            if (x.ndim() != 0)
                return dtype_value(t).release();
            if (is_bool(t))
                return dtype_value(dtype::Bool).release();
            if (is_integer(t))
            {
                auto const v = read_as<std::int64_t>(x.array()->pointer(), t);
                if (is_unsigned(t) || v >= 0)
                {
                    auto const u = is_unsigned(t) ? read_as<std::uint64_t>(x.array()->pointer(), t) : static_cast<std::uint64_t>(v);
                    return dtype_value(u <= 0xFFu ? dtype::UInt8 : u <= 0xFFFFu ? dtype::UInt16 : u <= 0xFFFFFFFFu ? dtype::UInt32 : dtype::UInt64).release();
                }
                return dtype_value(v >= -128 ? dtype::Int8 : v >= -32768 ? dtype::Int16 : v >= -2147483648ll ? dtype::Int32 : dtype::Int64).release();
            }
            if (is_float(t))
            {
                auto const v = std::fabs(read_as<double>(x.array()->pointer(), t));
                if (!std::isfinite(v) || v <= 65000.0)
                    return dtype_value(dtype::Float16).release();
                if (v <= static_cast<double>(std::numeric_limits<float>::max()))
                    return dtype_value(promote(t, dtype::Float32) == dtype::Float32 ? dtype::Float32 : dtype::Float32).release();
                return dtype_value(dtype::Float64).release();
            }
            auto const c = read_as<std::complex<double>>(x.array()->pointer(), t);
            auto const m = std::max(std::fabs(c.real()), std::fabs(c.imag()));
            return dtype_value(m <= static_cast<double>(std::numeric_limits<float>::max()) || !std::isfinite(m) ? dtype::Complex64 : dtype::Complex128).release();
        }

        object* f_common_type(object* aArgs, object* aKwargs)
        {
            arguments args{ "common_type", aArgs, aKwargs, { "..." } };
            if (!args.ok())
                return nullptr;
            bool complex = false;
            int precision = 0;
            for (auto x : args.extra())
            {
                auto const t = type_of(value::borrow(x));
                if (is_complex(t))
                    complex = true;
                int p;
                if (is_integer(t))
                    p = 2;
                else if (t == dtype::Float16)
                    p = 0;
                else if (t == dtype::Float32 || t == dtype::Complex64)
                    p = 1;
                else if (t == dtype::Float64 || t == dtype::Complex128)
                    p = 2;
                else
                    raise("TypeError", "can't get common type for non-numeric array");
                precision = std::max(precision, p);
            }
            if (complex)
                return np(precision <= 1 ? "complex64" : "complex128").release();
            return np(precision == 0 ? "float16" : precision == 1 ? "float32" : "float64").release();
        }

        object* f_mintypecode(object* aArgs, object* aKwargs)
        {
            arguments args{ "mintypecode", aArgs, aKwargs, { "typechars", "typeset", "default" } };
            if (!args.ok())
                return nullptr;
            auto const typeset = arg(args, 1, "GDFgdf").to_str();
            auto const defaultCode = arg(args, 2, "d").to_str();
            std::set<char> intersection;
            auto const add = [&](std::string const& aCode) { for (auto c : aCode) if (typeset.find(c) != std::string::npos) intersection.insert(c); };
            auto const chars = arg(args, 0);
            if (is_str(chars))
                for (auto c : chars.to_str())
                    add(std::string(1u, c));
            else
                for (auto const& t : chars.items())
                {
                    if (is_str(t))
                    {
                        auto const s = t.to_str();
                        if (!s.empty() && typeset.find(s) != std::string::npos)
                            intersection.insert(s[0]);
                    }
                    else
                        add(std::string(1u, info(type_of(asarray(t))).character));
                }
            if (intersection.empty())
                return make_str(defaultCode);
            if (intersection.contains('F') && intersection.contains('d'))
                return make_str("D");
            std::string const bySize = "GDFgdfQqLlIiHhBb?";
            for (auto c : bySize)
                if (intersection.contains(c))
                    return make_str(std::string(1u, c));
            return make_str(std::string(1u, *intersection.begin()));
        }

        object* f_typename(object* aArgs, object* aKwargs)
        {
            arguments args{ "typename", aArgs, aKwargs, { "char" } };
            if (!args.ok())
                return nullptr;
            static std::map<std::string, std::string> const sNames =
            {
                { "S1", "character" }, { "?", "bool" }, { "b", "signed char" }, { "B", "unsigned char" }, { "h", "short" }, { "H", "unsigned short" },
                { "i", "integer" }, { "I", "unsigned integer" }, { "l", "long integer" }, { "L", "unsigned long integer" }, { "q", "long long integer" },
                { "Q", "unsigned long long integer" }, { "f", "single precision" }, { "d", "double precision" }, { "g", "long precision" },
                { "F", "complex single precision" }, { "D", "complex double precision" }, { "G", "complex long double precision" }, { "S", "string" },
                { "U", "unicode" }, { "V", "void" }, { "O", "object" }
            };
            auto const c = arg(args, 0).to_str();
            auto const found = sNames.find(c);
            if (found == sNames.end())
                raise("KeyError", "'" + c + "'");
            return make_str(found->second);
        }

        object* f_iterable(object* aArgs, object* aKwargs)
        {
            arguments args{ "iterable", aArgs, aKwargs, { "y" } };
            if (!args.ok())
                return nullptr;
            ref const i{ ctx->iter(ctx, args[0]) };
            if (!i)
            {
                ctx->clear(ctx);
                return make_bool(false);
            }
            return make_bool(true);
        }

        object* f_base_repr(object* aArgs, object* aKwargs)
        {
            arguments args{ "base_repr", aArgs, aKwargs, { "number", "base", "padding" } };
            if (!args.ok())
                return nullptr;
            auto const number = arg(args, 0).to_int();
            auto const base = arg(args, 1, 2).to_int();
            auto const padding = arg(args, 2, 0).to_int();
            if (base > 36)
                raise("ValueError", "Bases greater than 36 not handled in base_repr.");
            if (base < 2)
                raise("ValueError", "Bases less than 2 not handled in base_repr.");
            std::string const digits = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
            unsigned long long n = number < 0 ? 0ull - static_cast<unsigned long long>(number) : static_cast<unsigned long long>(number);
            std::string res;
            while (n)
            {
                res += digits[n % static_cast<unsigned long long>(base)];
                n /= static_cast<unsigned long long>(base);
            }
            if (padding > 0)
                res += std::string(static_cast<std::size_t>(padding), '0');
            if (number < 0)
                res += '-';
            std::reverse(res.begin(), res.end());
            return make_str(res.empty() ? "0" : res);
        }

        object* f_binary_repr(object* aArgs, object* aKwargs)
        {
            arguments args{ "binary_repr", aArgs, aKwargs, { "num", "width" } };
            if (!args.ok())
                return nullptr;
            auto const num = arg(args, 0).to_int();
            std::optional<long long> width;
            if (args.has(1))
                width = arg(args, 1).to_int();
            auto const binary = [](unsigned long long n) { std::string s; while (n) { s += static_cast<char>('0' + (n & 1u)); n >>= 1; } std::reverse(s.begin(), s.end()); return s; };
            if (num == 0)
                return make_str(std::string(static_cast<std::size_t>(std::max(width.value_or(1), 1ll)), '0'));
            if (num > 0)
            {
                auto s = binary(static_cast<unsigned long long>(num));
                auto const n = static_cast<long long>(s.size());
                if (width && *width < n)
                    raise("ValueError", "Insufficient bit width provided. This behavior will raise an error in the future.");
                if (width)
                    s = std::string(static_cast<std::size_t>(*width - n), '0') + s;
                return make_str(s);
            }
            if (!width)
                return make_str("-" + binary(0ull - static_cast<unsigned long long>(num)));
            auto const poswidth = static_cast<long long>(binary(0ull - static_cast<unsigned long long>(num)).size());
            auto const twocomp = (1ll << (std::max(poswidth, *width))) + num;
            auto s = binary(static_cast<unsigned long long>(twocomp));
            auto const outwidth = std::max(static_cast<long long>(s.size()), *width);
            if (*width < poswidth + 1)
                raise("ValueError", "Insufficient bit width provided. This behavior will raise an error in the future.");
            return make_str(std::string(static_cast<std::size_t>(outwidth - static_cast<long long>(s.size())), '1') + s);
        }

        // (the byte ranges an array's elements occupy)
        std::pair<std::int64_t, std::int64_t> extent(ndarray const& aArray)
        {
            std::int64_t low = static_cast<std::int64_t>(aArray.offset), high = low;
            if (aArray.size() == 0)
                return { low, low };
            for (std::size_t d = 0u; d < aArray.ndim(); ++d)
            {
                auto const span = (aArray.shape[d] - 1) * aArray.strides[d];
                if (span < 0) low += span; else high += span;
            }
            return { low, high + static_cast<std::int64_t>(aArray.itemsize()) };
        }

        bool overlap(ndarray const& a, ndarray const& b, bool aExact)
        {
            if (a.data != b.data || a.size() == 0 || b.size() == 0)
                return false;
            auto const ea = extent(a), eb = extent(b);
            if (ea.second <= eb.first || eb.second <= ea.first)
                return false;
            if (!aExact)
                return true;
            std::set<std::int64_t> bytes;
            for_each(a, [&](unsigned char* p) { for (std::size_t i = 0u; i < a.itemsize(); ++i) bytes.insert(p - a.data->data() + static_cast<std::int64_t>(i)); });
            bool shared = false;
            for_each(b, [&](unsigned char* p) { for (std::size_t i = 0u; i < b.itemsize(); ++i) if (bytes.contains(p - b.data->data() + static_cast<std::int64_t>(i))) shared = true; });
            return shared;
        }

        object* f_may_share_memory(object* aArgs, object* aKwargs)
        {
            arguments args{ "may_share_memory", aArgs, aKwargs, { "a", "b", "max_work" } };
            if (!args.ok())
                return nullptr;
            if (!is_array(args[0]) || !is_array(args[1]))
                return make_bool(false);
            return make_bool(overlap(*as_array(args[0]), *as_array(args[1]), false));
        }

        object* f_shares_memory(object* aArgs, object* aKwargs)
        {
            arguments args{ "shares_memory", aArgs, aKwargs, { "a", "b", "max_work" } };
            if (!args.ok())
                return nullptr;
            if (!is_array(args[0]) || !is_array(args[1]))
                return make_bool(false);
            return make_bool(overlap(*as_array(args[0]), *as_array(args[1]), true));
        }

        object* f_asfortranarray(object* aArgs, object* aKwargs)
        {
            arguments args{ "asfortranarray", aArgs, aKwargs, { "a", "dtype", "*like" } };
            if (!args.ok())
                return nullptr;
            auto const a = np("asarray")(arg(args, 0), arg(args, 1, py::none()));
            auto* const x = a.array();
            if (x->ndim() == 0)
                return np("reshape")(a, 1).release();
            if (x->f_contiguous())
                return a.release();
            return array_value(copy_array(*x, {}, true)).release();
        }

        object* f_asarray_chkfinite(object* aArgs, object* aKwargs)
        {
            arguments args{ "asarray_chkfinite", aArgs, aKwargs, { "a", "dtype", "order" } };
            if (!args.ok())
                return nullptr;
            auto const a = np("asarray")(arg(args, 0), arg(args, 1, py::none()));
            if (is_inexact(type_of(a)) && !np("isfinite")(a).method("all").truth())
                raise("ValueError", "array must not contain infs or NaNs");
            return a.release();
        }

        object* f_isfortran(object* aArgs, object* aKwargs)
        {
            arguments args{ "isfortran", aArgs, aKwargs, { "a" } };
            if (!args.ok())
                return nullptr;
            auto const* a = arg(args, 0).array();
            return make_bool(a->f_contiguous() && !a->c_contiguous());
        }

        object* f_require(object* aArgs, object* aKwargs)
        {
            arguments args{ "require", aArgs, aKwargs, { "a", "dtype", "requirements", "*like" } };
            if (!args.ok())
                return nullptr;
            auto const dt = arg(args, 1, py::none());
            if (!args.has(2))
                return np("asanyarray")(arg(args, 0), dt).release();
            std::set<char> requirements;
            auto const r = arg(args, 2);
            std::vector<value> items = is_str(r) ? std::vector<value>{ r } : r.items();
            for (auto const& x : items)
            {
                auto s = x.to_str();
                for (auto& c : s)
                    c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
                static std::map<std::string, char> const sFlags =
                {
                    { "C", 'C' }, { "C_CONTIGUOUS", 'C' }, { "CONTIGUOUS", 'C' }, { "F", 'F' }, { "F_CONTIGUOUS", 'F' }, { "FORTRAN", 'F' },
                    { "A", 'A' }, { "ALIGNED", 'A' }, { "W", 'W' }, { "WRITEABLE", 'W' }, { "O", 'O' }, { "OWNDATA", 'O' }, { "E", 'E' }, { "ENSUREARRAY", 'E' }
                };
                auto const f = sFlags.find(s);
                if (f == sFlags.end())
                    raise("KeyError", "'" + s + "'");
                requirements.insert(f->second);
            }
            requirements.erase('E');
            if (requirements.contains('C') && requirements.contains('F'))
                raise("ValueError", "Cannot specify both \"C\" and \"F\" order");
            auto arr = np("asarray")(arg(args, 0), dt);
            if (requirements.contains('F'))
                arr = np("asfortranarray")(arr);
            else if (requirements.contains('C'))
                arr = np("ascontiguousarray")(arr);
            if ((requirements.contains('O') && arr.array()->view) || (requirements.contains('W') && !arr.array()->writeable))
                arr = arr.method("copy");
            return arr.release();
        }

        // --- floating point error handling

        std::map<std::string, std::string>& error_modes()
        {
            static std::map<std::string, std::string> sModes = { { "divide", "warn" }, { "over", "warn" }, { "under", "ignore" }, { "invalid", "warn" } };
            return sModes;
        }

        value error_dict()
        {
            auto& m = error_modes();
            return dict({ { "divide", value{ m["divide"] } }, { "over", value{ m["over"] } }, { "under", value{ m["under"] } }, { "invalid", value{ m["invalid"] } } });
        }

        object* f_geterr(object* aArgs, object* aKwargs)
        {
            arguments args{ "geterr", aArgs, aKwargs, {} };
            if (!args.ok())
                return nullptr;
            return error_dict().release();
        }

        object* f_seterr(object* aArgs, object* aKwargs)
        {
            arguments args{ "seterr", aArgs, aKwargs, { "all", "divide", "over", "under", "invalid" } };
            if (!args.ok())
                return nullptr;
            auto const old = error_dict();
            auto& m = error_modes();
            static std::set<std::string> const sValid = { "ignore", "warn", "raise", "call", "print", "log" };
            auto const set = [&](char const* aName, std::size_t aIndex)
            {
                if (!args.has(aIndex))
                    return;
                auto const v = arg(args, aIndex).to_str();
                if (!sValid.contains(v))
                    raise("ValueError", "invalid error mode: '" + v + "'");
                m[aName] = v;
            };
            if (args.has(0))
                for (auto n : { "divide", "over", "under", "invalid" })
                    set(n, 0);
            set("divide", 1);
            set("over", 2);
            set("under", 3);
            set("invalid", 4);
            return old.release();
        }

        long long& buffer_size()
        {
            static long long sSize = 8192;
            return sSize;
        }

        object* f_getbufsize(object* aArgs, object* aKwargs)
        {
            arguments args{ "getbufsize", aArgs, aKwargs, {} };
            if (!args.ok())
                return nullptr;
            return make_int(buffer_size());
        }

        // --- index objects

        // (np.mgrid/np.ogrid: nd_grid.__getitem__)
        value nd_grid(value const& aKey, bool aSparse)
        {
            auto const is_complex_step = [](value const& aStep) { return kind(aStep.get()) == NEOS_PY_COMPLEX || (is_array(aStep.get()) && is_complex(aStep.array()->type)); };
            auto const parts = [](value const& aSlice, value& aStart, value& aStop, value& aStep)
            {
                object *start = nullptr, *stop = nullptr, *step = nullptr;
                ctx->slice_parts(ctx, aSlice.get(), &start, &stop, &step);
                aStart = value::borrow(start);
                aStop = value::borrow(stop);
                aStep = value::borrow(step);
                if (aStart.is_none())
                    aStart = value{ 0 };
                if (aStep.is_none())
                    aStep = value{ 1 };
            };
            if (kind(aKey.get()) == NEOS_PY_SLICE)
            {
                value start, stop, step;
                parts(aKey, start, stop, step);
                if (is_complex_step(step))
                {
                    auto const stepFloat = abs(step);
                    auto const length = static_cast<long long>(stepFloat.to_double());
                    value s = value{ length };
                    if (length != 1)
                        s = (stop - start) / static_cast<double>(length - 1);
                    auto const typ = np("result_type")(start, stop, stepFloat);
                    return np("arange")(0, length, 1, kw{ "dtype", typ }) * s + start;
                }
                return np("arange")(start, stop, step);
            }
            auto const key = aKey.items();
            std::vector<long long> size;
            std::vector<value> numList{ value{ 0 } };
            for (auto const& k : key)
            {
                value start, stop, step;
                parts(k, start, stop, step);
                if (is_complex_step(step))
                    size.push_back(static_cast<long long>(abs(step).to_double()));
                else
                    size.push_back(static_cast<long long>(std::ceil((stop - start).to_double() / (step.to_double() * 1.0))));
                numList.push_back(start);
                numList.push_back(stop);
                numList.push_back(step);
            }
            auto const typ = np("result_type").call(numList);
            std::vector<value> nn;
            if (aSparse)
                for (auto s : size)
                    nn.push_back(np("arange")(s, kw{ "dtype", typ }));
            else
                nn = np("indices")(int_tuple(size), typ).items();
            for (std::size_t k = 0u; k < key.size(); ++k)
            {
                value start, stop, step;
                parts(key[k], start, stop, step);
                if (is_complex_step(step))
                {
                    auto const n = static_cast<long long>(abs(step).to_double());
                    step = value{ n };
                    if (n != 1)
                        step = (stop - start) / static_cast<double>(n - 1);
                }
                nn[k] = nn[k] * step + start;
            }
            if (aSparse)
            {
                for (std::size_t k = 0u; k < size.size(); ++k)
                {
                    std::vector<value> slobj(size.size(), py::none());
                    slobj[k] = all();
                    nn[k] = nn[k][tuple(slobj)];
                }
                return tuple(nn);
            }
            return np("array")(list(nn));
        }

        value axis_concatenate(value const& aKey, long long aAxis, long long aNdmin, long long aTrans1d)
        {
            std::vector<value> key = is_tuple(aKey) ? aKey.items() : std::vector<value>{ aKey };
            auto axis = aAxis, ndmin = aNdmin, trans1d = aTrans1d;
            std::vector<value> objs, resultTypeObjs;
            for (std::size_t k = 0u; k < key.size(); ++k)
            {
                auto const& item = key[k];
                bool scalar = false;
                value newobj;
                if (kind(item.get()) == NEOS_PY_SLICE)
                {
                    object *start = nullptr, *stop = nullptr, *step = nullptr;
                    ctx->slice_parts(ctx, item.get(), &start, &stop, &step);
                    value s = value::borrow(start), e = value::borrow(stop), st = value::borrow(step);
                    if (s.is_none()) s = value{ 0 };
                    if (st.is_none()) st = value{ 1 };
                    if (kind(st.get()) == NEOS_PY_COMPLEX)
                        newobj = np("linspace")(s, e, kw{ "num", value{ static_cast<long long>(abs(st).to_double()) } });
                    else
                        newobj = np("arange")(s, e, st);
                    if (ndmin > 1)
                    {
                        newobj = np("array")(newobj, kw{ "ndmin", value{ ndmin } });
                        if (trans1d != -1)
                            newobj = newobj.method("swapaxes", -1, trans1d);
                    }
                }
                else if (is_str(item))
                {
                    if (k != 0u)
                        raise("ValueError", "special directives must be the first entry.");
                    auto const s = item.to_str();
                    if (s == "r" || s == "c")
                        raise("NotImplementedError", "matrix directives are not supported");
                    if (s.find(',') != std::string::npos)
                    {
                        std::vector<long long> vec;
                        std::size_t p = 0u;
                        try
                        {
                            while (p <= s.size())
                            {
                                auto const q = s.find(',', p);
                                vec.push_back(std::stoll(s.substr(p, q == std::string::npos ? std::string::npos : q - p)));
                                if (q == std::string::npos)
                                    break;
                                p = q + 1u;
                            }
                        }
                        catch (...)
                        {
                            raise("ValueError", "unknown special directive '" + s + "'");
                        }
                        if (vec.size() < 2u)
                            raise("ValueError", "unknown special directive '" + s + "'");
                        axis = vec[0];
                        ndmin = vec[1];
                        if (vec.size() == 3u)
                            trans1d = vec[2];
                        continue;
                    }
                    try
                    {
                        axis = std::stoll(s);
                    }
                    catch (...)
                    {
                        raise("ValueError", "unknown special directive");
                    }
                    continue;
                }
                else if (is_python_scalar(item.get()))
                {
                    scalar = true;
                    newobj = item;
                }
                else
                {
                    auto const itemNdim = np("ndim")(item).to_int();
                    newobj = np("array")(item, kw{ "ndmin", value{ ndmin } });
                    if (trans1d != -1 && itemNdim < ndmin)
                    {
                        auto const k2 = ndmin - itemNdim;
                        auto k1 = trans1d;
                        if (k1 < 0)
                            k1 += k2 + 1;
                        std::vector<long long> axes;
                        for (long long i = 0; i < k1; ++i) axes.push_back(i);
                        for (long long i = k2; i < ndmin; ++i) axes.push_back(i);
                        for (long long i = k1; i < k2; ++i) axes.push_back(i);
                        newobj = newobj.method("transpose", int_tuple(axes));
                    }
                }
                objs.push_back(newobj);
                resultTypeObjs.push_back(scalar ? item : dtype_of(newobj));
            }
            if (!resultTypeObjs.empty())
            {
                auto const finalDtype = np("result_type").call(resultTypeObjs);
                for (auto& o : objs)
                    o = np("array")(o, kw{ "ndmin", value{ ndmin } }, kw{ "dtype", finalDtype });
            }
            return np("concatenate")(tuple(objs), kw{ "axis", value{ axis } });
        }

        value index_object(std::string const& aName)
        {
            auto* const i = new_instance(aName);
            auto const result = instance_value(i);
            if (aName == "mgrid" || aName == "ogrid")
            {
                bool const sparse = aName == "ogrid";
                i->reprText = std::string{ "<numpy.lib._index_tricks_impl." } + (sparse ? "OGridClass" : "MGridClass") + " object>";
                i->getitem = [sparse](value const& aKey) { return nd_grid(aKey, sparse); };
            }
            else if (aName == "r_" || aName == "c_")
            {
                bool const c = aName == "c_";
                i->reprText = std::string{ "<numpy.lib._index_tricks_impl." } + (c ? "CClass" : "RClass") + " object>";
                i->getitem = [c](value const& aKey) { return c ? axis_concatenate(aKey, -1, 2, 0) : axis_concatenate(aKey, 0, 1, -1); };
                i->length = []() { return 0ll; };
            }
            else
            {
                bool const maketuple = aName == "index_exp";
                i->reprText = "<numpy.lib._index_tricks_impl.IndexExpression object>";
                i->getitem = [maketuple](value const& aKey) { return maketuple && !is_tuple(aKey) ? tuple({ aKey }) : aKey; };
            }
            return result;
        }

        // --- functional programming

        object* f_apply_along_axis(object* aArgs, object* aKwargs)
        {
            arguments args{ "apply_along_axis", aArgs, aKwargs, { "func1d", "axis", "arr", "..." } };
            if (!args.ok())
                return nullptr;
            auto const func = arg(args, 0);
            auto const arr = np("asanyarray")(arg(args, 2));
            auto const nd = arr.ndim();
            auto const axis = normalize_axis_index(arg(args, 1).to_int(), nd);
            std::vector<long long> dims;
            for (long long i = 0; i < nd; ++i)
                if (i != axis)
                    dims.push_back(i);
            dims.push_back(axis);
            auto const view = np("transpose")(arr, int_tuple(dims));
            auto const outer = shape_t{ view.array()->shape.begin(), view.array()->shape.end() - 1 };
            auto const count = element_count(outer);
            if (count == 0)
                raise("ValueError", "Cannot apply_along_axis when any iteration dimensions are 0");
            std::vector<value> extra;
            for (auto x : args.extra())
                extra.push_back(value::borrow(x));
            auto const index_of = [&](std::int64_t aFlat)
            {
                std::vector<value> index(outer.size());
                for (std::size_t d = outer.size(); d-- > 0u;)
                {
                    index[d] = value{ static_cast<long long>(aFlat % outer[d]) };
                    aFlat /= outer[d];
                }
                index.push_back(ellipsis());
                return tuple(index);
            };
            auto const apply = [&](value const& aIndex)
            {
                std::vector<value> callArgs{ view[aIndex] };
                callArgs.insert(callArgs.end(), extra.begin(), extra.end());
                return np("asanyarray")(func.call(callArgs));
            };
            auto const ind0 = index_of(0);
            auto res = apply(ind0);
            std::vector<long long> buffShape(outer.begin(), outer.end());
            for (auto d : res.array()->shape)
                buffShape.push_back(d);
            auto const buff = np("zeros")(int_tuple(buffShape), kw{ "dtype", dtype_of(res) });
            auto const buffNdim = static_cast<long long>(buffShape.size());
            auto const resNdim = res.ndim();
            std::vector<long long> permute;
            for (long long i = 0; i < axis; ++i) permute.push_back(i);
            for (long long i = buffNdim - resNdim; i < buffNdim; ++i) permute.push_back(i);
            for (long long i = axis; i < buffNdim - resNdim; ++i) permute.push_back(i);
            buff.set(ind0, res);
            for (std::int64_t i = 1; i < count; ++i)
            {
                auto const index = index_of(i);
                buff.set(index, apply(index));
            }
            return np("transpose")(buff, int_tuple(permute)).release();
        }

        object* f_apply_over_axes(object* aArgs, object* aKwargs)
        {
            arguments args{ "apply_over_axes", aArgs, aKwargs, { "func", "a", "axes" } };
            if (!args.ok())
                return nullptr;
            auto const func = arg(args, 0);
            auto val = asarray(arg(args, 1));
            auto const N = val.ndim();
            auto const axesArg = arg(args, 2);
            std::vector<value> axes = np("array")(axesArg).ndim() == 0 ? std::vector<value>{ axesArg } : axesArg.items();
            for (auto const& a : axes)
            {
                auto axis = a.to_int();
                if (axis < 0)
                    axis += N;
                auto res = func(val, axis);
                if (np("ndim")(res).to_int() == val.ndim())
                    val = res;
                else
                {
                    res = np("expand_dims")(res, axis);
                    if (res.ndim() == val.ndim())
                        val = res;
                    else
                        raise("ValueError", "function is not returning an array of the correct shape");
                }
            }
            return val.release();
        }

        object* f_fromfunction(object* aArgs, object* aKwargs)
        {
            arguments args{ "fromfunction", aArgs, aKwargs, { "function", "shape", "*dtype", "*like" } };
            if (!args.ok())
                return nullptr;
            auto const indices = np("indices")(arg(args, 1), kw{ "dtype", arg(args, 2, np("float64")) });
            return arg(args, 0).call(indices.items()).release();
        }

        object* f_fromiter(object* aArgs, object* aKwargs)
        {
            arguments args{ "fromiter", aArgs, aKwargs, { "iter", "dtype", "count", "*like" } };
            if (!args.ok())
                return nullptr;
            auto const items = arg(args, 0).items();
            auto const count = arg(args, 2, -1).to_int();
            std::vector<value> values;
            if (count >= 0)
            {
                if (static_cast<long long>(items.size()) < count)
                    raise("ValueError", "iterator too short: Expected " + std::to_string(count) + " but iterator had only " + std::to_string(items.size()) + " items.");
                values.assign(items.begin(), items.begin() + count);
            }
            else
                values = items;
            if (values.empty())
                return np("empty")(0, kw{ "dtype", arg(args, 1) }).release();
            return np("array")(list(values), kw{ "dtype", arg(args, 1) }).release();
        }

        // (np.vectorize: the function applied to each element of the broadcast arguments)
        value vectorize_call(value const& aFunction, value const& aOtypes, value const& aExcluded, value const& aArgs, value const& aKwargs)
        {
            auto const argsList = aArgs.items();
            if (argsList.empty())
                return aFunction.call({});
            std::vector<value> arrays;
            for (auto const& a : argsList)
                arrays.push_back(asarray(a));
            auto const broadcast = np("broadcast_arrays").call(arrays).items();
            auto const shape = broadcast[0].array()->shape;
            auto const n = element_count(shape);
            std::vector<kw> kwargs;
            std::vector<value> kwargValues;
            std::vector<std::string> kwargNames;
            if (!aKwargs.empty() && !aKwargs.is_none())
            {
                auto const size = ctx->dict_size(ctx, aKwargs.get());
                kwargValues.reserve(size);
                kwargNames.reserve(size);
                for (std::size_t i = 0u; i < size; ++i)
                {
                    object *key = nullptr, *v = nullptr;
                    ctx->dict_item(ctx, aKwargs.get(), i, &key, &v);
                    kwargNames.push_back(*as_str(key));
                    kwargValues.push_back(value::borrow(v));
                }
                for (std::size_t i = 0u; i < size; ++i)
                    kwargs.emplace_back(kwargNames[i].c_str(), kwargValues[i]);
            }
            auto const element = [&](std::size_t aArg, std::int64_t aFlat, bool aNumpyScalar)
            {
                auto const* a = broadcast[aArg].array();
                std::int64_t offset = 0, rest = aFlat;
                for (std::size_t d = a->ndim(); d-- > 0u;)
                {
                    offset += (rest % a->shape[d]) * a->strides[d];
                    rest /= a->shape[d];
                }
                auto const s = value::steal(scalar_from(*a, a->pointer() + offset));
                return aNumpyScalar ? s : value::steal(to_python(*s.array()));
            };
            value otypes = aOtypes;
            if (otypes.empty() || otypes.is_none())
            {
                if (n == 0)
                    raise("ValueError", "cannot call `vectorize` on size 0 inputs unless `otypes` is set");
                // (the first element (a NumPy scalar) decides the output type: NumPy calls the function on it)
                std::vector<value> first;
                for (std::size_t i = 0u; i < broadcast.size(); ++i)
                    first.push_back(element(i, 0, true));
                auto const output = aFunction.call(first, kwargs);
                otypes = dtype_of(asarray(output));
            }
            else if (is_str(otypes))
                otypes = value{ otypes.to_str().substr(0, 1) };
            else if (is_sequence(otypes))
                otypes = otypes.items().at(0);
            std::vector<value> results;
            for (std::int64_t i = 0; i < n; ++i)
            {
                std::vector<value> callArgs;
                for (std::size_t a = 0u; a < broadcast.size(); ++a)
                    callArgs.push_back(element(a, i, false));
                results.push_back(aFunction.call(callArgs, kwargs));
            }
            std::vector<long long> s(shape.begin(), shape.end());
            auto const t = np("dtype")(otypes);
            auto const r = np("empty")(int_tuple(s), kw{ "dtype", t });
            auto const flat = r.method("reshape", -1);
            for (std::int64_t i = 0; i < n; ++i)
                flat.set(value{ static_cast<long long>(i) }, results[static_cast<std::size_t>(i)]);
            if (r.ndim() == 0)
                return r[tuple({})];
            return r;
        }

        object* f_vectorize(object* aArgs, object* aKwargs)
        {
            arguments args{ "vectorize", aArgs, aKwargs, { "pyfunc", "otypes", "doc", "excluded", "cache", "signature" } };
            if (!args.ok())
                return nullptr;
            auto* const i = new_instance("vectorize");
            auto const result = instance_value(i);
            auto const function = arg(args, 0);
            auto const otypes = arg(args, 1);
            add_attribute(i, "pyfunc", function);
            add_attribute(i, "otypes", otypes.empty() ? py::none() : otypes);
            i->reprText = "<numpy.vectorize object>";
            i->call = [function, otypes](value const& aCallArgs, value const& aCallKwargs) { return vectorize_call(function, otypes, {}, aCallArgs, aCallKwargs); };
            return result.release();
        }

        object* iteration(std::vector<value> const& aItems)
        {
            auto const l = list(aItems);
            return ctx->iter(ctx, l.get());
        }

        object* f_ndindex(object* aArgs, object* aKwargs)
        {
            arguments args{ "ndindex", aArgs, aKwargs, { "..." } };
            if (!args.ok())
                return nullptr;
            shape_t shape;
            if (args.extra().size() == 1u && is_sequence(args.extra()[0]))
            {
                if (!to_shape(args.extra()[0], shape))
                    throw raised{};
            }
            else
                for (auto x : args.extra())
                {
                    std::int64_t d = 0;
                    if (!to_int(x, d))
                        throw raised{};
                    shape.push_back(d);
                }
            std::vector<value> items;
            auto const n = element_count(shape);
            for (std::int64_t i = 0; i < n; ++i)
            {
                std::vector<long long> index(shape.size());
                auto rest = i;
                for (std::size_t d = shape.size(); d-- > 0u;)
                {
                    index[d] = rest % shape[d];
                    rest /= shape[d];
                }
                items.push_back(int_tuple(index));
            }
            return iteration(items);
        }

        object* f_ndenumerate(object* aArgs, object* aKwargs)
        {
            arguments args{ "ndenumerate", aArgs, aKwargs, { "arr" } };
            if (!args.ok())
                return nullptr;
            auto const a = asarray(arg(args, 0));
            auto const* x = a.array();
            std::vector<value> items;
            auto const n = x->size();
            for (std::int64_t i = 0; i < n; ++i)
            {
                std::vector<long long> index(x->ndim());
                std::int64_t offset = 0;
                auto rest = i;
                for (std::size_t d = x->ndim(); d-- > 0u;)
                {
                    index[d] = rest % x->shape[d];
                    offset += index[d] * x->strides[d];
                    rest /= x->shape[d];
                }
                items.push_back(tuple({ int_tuple(index), value::steal(scalar_from(*x, x->pointer() + offset)) }));
            }
            return iteration(items);
        }

        // --- pad (numpy/lib/_arraypad_impl.py)

        value slice_at_axis(value const& aSlice, long long aAxis)
        {
            std::vector<value> s(static_cast<std::size_t>(aAxis), all());
            s.push_back(aSlice);
            s.push_back(ellipsis());
            return tuple(s);
        }

        value view_roi(value const& aArray, std::vector<value> const& aOriginalArea, long long aAxis)
        {
            ++aAxis;
            std::vector<value> s(static_cast<std::size_t>(aAxis), all());
            for (std::size_t i = static_cast<std::size_t>(aAxis); i < aOriginalArea.size(); ++i)
                s.push_back(aOriginalArea[i]);
            return aArray[tuple(s)];
        }

        void set_pad_area(value const& aPadded, long long aAxis, std::pair<long long, long long> aWidth, std::pair<value, value> const& aValues)
        {
            aPadded.set(slice_at_axis(slice({}, aWidth.first), aAxis), aValues.first);
            aPadded.set(slice_at_axis(slice(aPadded.array()->shape[static_cast<std::size_t>(aAxis)] - aWidth.second, {}), aAxis), aValues.second);
        }

        std::pair<value, value> get_edges(value const& aPadded, long long aAxis, std::pair<long long, long long> aWidth)
        {
            auto const left = aWidth.first;
            auto const right = aPadded.array()->shape[static_cast<std::size_t>(aAxis)] - aWidth.second;
            return { aPadded[slice_at_axis(slice(left, left + 1), aAxis)], aPadded[slice_at_axis(slice(right - 1, right), aAxis)] };
        }

        std::vector<std::pair<value, value>> as_pairs(value const& x, long long aNdim, bool aAsIndex)
        {
            if (x.empty() || x.is_none())
                return std::vector<std::pair<value, value>>(static_cast<std::size_t>(aNdim), { py::none(), py::none() });
            auto a = np("array")(x);
            if (aAsIndex)
                a = np("round")(a).method("astype", np("intp"), kw{ "copy", value{ false } });
            if (a.ndim() < 3)
            {
                if (a.size() == 1)
                {
                    a = a.method("ravel");
                    if (aAsIndex && (a < 0).truth())
                        raise("ValueError", "index can't contain negative values");
                    return std::vector<std::pair<value, value>>(static_cast<std::size_t>(aNdim), { a[0], a[0] });
                }
                if (a.size() == 2 && !(a.ndim() == 2 && a.array()->shape[0] == 2 && a.array()->shape[1] == 1))
                {
                    a = a.method("ravel");
                    if (aAsIndex && ((a[0] < 0).truth() || (a[1] < 0).truth()))
                        raise("ValueError", "index can't contain negative values");
                    return std::vector<std::pair<value, value>>(static_cast<std::size_t>(aNdim), { a[0], a[1] });
                }
            }
            if (aAsIndex && (a.method("min") < 0).truth())
                raise("ValueError", "index can't contain negative values");
            auto const b = np("broadcast_to")(a, int_tuple({ aNdim, 2 }));
            std::vector<std::pair<value, value>> result;
            for (long long i = 0; i < aNdim; ++i)
                result.emplace_back(value::steal(to_python(*b[tuple({ value{ i }, value{ 0 } })].array())), value::steal(to_python(*b[tuple({ value{ i }, value{ 1 } })].array())));
            return result;
        }

        std::pair<long long, long long> set_reflect_both(value const& aPadded, long long aAxis, std::pair<long long, long long> aWidth, std::string const& aMethod,
            long long aOriginalPeriod, bool aIncludeEdge)
        {
            auto [leftPad, rightPad] = aWidth;
            auto const size = aPadded.array()->shape[static_cast<std::size_t>(aAxis)];
            auto oldLength = size - rightPad - leftPad;
            long long edgeOffset;
            if (aIncludeEdge)
            {
                oldLength = oldLength / aOriginalPeriod * aOriginalPeriod;
                edgeOffset = 1;
            }
            else
            {
                oldLength = (oldLength - 1) / (aOriginalPeriod - 1) * (aOriginalPeriod - 1) + 1;
                edgeOffset = 0;
                oldLength -= 1;
            }
            if (leftPad > 0)
            {
                auto const chunkLength = std::min(oldLength, leftPad);
                auto const stop = leftPad - edgeOffset;
                auto const start = stop + chunkLength;
                auto leftChunk = aPadded[slice_at_axis(slice(start, stop, -1), aAxis)];
                if (aMethod == "odd")
                    leftChunk = value{ 2 } * aPadded[slice_at_axis(slice(leftPad, leftPad + 1), aAxis)] - leftChunk;
                aPadded.set(slice_at_axis(slice(leftPad - chunkLength, leftPad), aAxis), leftChunk);
                leftPad -= chunkLength;
            }
            if (rightPad > 0)
            {
                auto const chunkLength = std::min(oldLength, rightPad);
                auto const start = -rightPad + edgeOffset - 2;
                auto const stop = start - chunkLength;
                auto rightChunk = aPadded[slice_at_axis(slice(start, stop, -1), aAxis)];
                if (aMethod == "odd")
                    rightChunk = value{ 2 } * aPadded[slice_at_axis(slice(-rightPad - 1, -rightPad), aAxis)] - rightChunk;
                auto const s = size - rightPad;
                aPadded.set(slice_at_axis(slice(s, s + chunkLength), aAxis), rightChunk);
                rightPad -= chunkLength;
            }
            return { leftPad, rightPad };
        }

        std::pair<long long, long long> set_wrap_both(value const& aPadded, long long aAxis, std::pair<long long, long long> aWidth, long long aOriginalPeriod)
        {
            auto const [leftPad, rightPad] = aWidth;
            auto period = aPadded.array()->shape[static_cast<std::size_t>(aAxis)] - rightPad - leftPad;
            period = period / aOriginalPeriod * aOriginalPeriod;
            long long newLeft = 0, newRight = 0;
            if (leftPad > 0)
            {
                auto const sliceEnd = leftPad + period;
                auto const sliceStart = sliceEnd - std::min(period, leftPad);
                auto const rightChunk = aPadded[slice_at_axis(slice(sliceStart, sliceEnd), aAxis)];
                value padArea;
                if (leftPad > period)
                {
                    padArea = slice_at_axis(slice(leftPad - period, leftPad), aAxis);
                    newLeft = leftPad - period;
                }
                else
                    padArea = slice_at_axis(slice({}, leftPad), aAxis);
                aPadded.set(padArea, rightChunk);
            }
            if (rightPad > 0)
            {
                auto const sliceStart = -rightPad - period;
                auto const sliceEnd = sliceStart + std::min(period, rightPad);
                auto const leftChunk = aPadded[slice_at_axis(slice(sliceStart, sliceEnd), aAxis)];
                value padArea;
                if (rightPad > period)
                {
                    padArea = slice_at_axis(slice(-rightPad, -rightPad + period), aAxis);
                    newRight = rightPad - period;
                }
                else
                    padArea = slice_at_axis(slice(-rightPad, {}), aAxis);
                aPadded.set(padArea, leftChunk);
            }
            return { newLeft, newRight };
        }

        object* f_pad(object* aArgs, object* aKwargs)
        {
            arguments args{ "pad", aArgs, aKwargs, { "array", "pad_width", "mode", "*constant_values", "*end_values", "*stat_length", "*reflect_type" } };
            if (!args.ok())
                return nullptr;
            auto const array = asarray(arg(args, 0));
            auto padWidthArray = asarray(arg(args, 1));
            if (!is_integer(type_of(padWidthArray)))
                raise("TypeError", "`pad_width` must be of integral type.");
            auto const ndim = array.ndim();
            std::vector<std::pair<long long, long long>> padWidth;
            for (auto const& p : as_pairs(padWidthArray, ndim, true))
                padWidth.emplace_back(p.first.to_int(), p.second.to_int());
            auto const mode = arg(args, 2, "constant").to_str();
            static std::map<std::string, std::vector<std::size_t>> const sAllowed =
            {
                { "empty", {} }, { "edge", {} }, { "wrap", {} }, { "constant", { 3 } }, { "linear_ramp", { 4 } }, { "maximum", { 5 } }, { "mean", { 5 } },
                { "median", { 5 } }, { "minimum", { 5 } }, { "reflect", { 6 } }, { "symmetric", { 6 } }
            };
            auto const allowed = sAllowed.find(mode);
            if (allowed == sAllowed.end())
                raise("ValueError", "mode '" + mode + "' is not supported");
            static char const* const sKeywordNames[] = { "", "", "", "constant_values", "end_values", "stat_length", "reflect_type" };
            std::string unsupported;
            for (std::size_t k = 3u; k <= 6u; ++k)
                if (args.get(k) != nullptr && std::find(allowed->second.begin(), allowed->second.end(), k) == allowed->second.end())
                    unsupported += std::string{ unsupported.empty() ? "" : ", " } + "'" + sKeywordNames[k] + "'";
            if (!unsupported.empty())
                raise("ValueError", "unsupported keyword arguments for mode '" + mode + "': {" + unsupported + "}");
            // (_pad_simple)
            std::vector<long long> newShape;
            std::vector<value> originalArea;
            for (long long i = 0; i < ndim; ++i)
            {
                auto const size = array.array()->shape[static_cast<std::size_t>(i)];
                auto const [left, right] = padWidth[static_cast<std::size_t>(i)];
                newShape.push_back(left + size + right);
                originalArea.push_back(slice(left, left + size));
            }
            auto const padded = np("empty")(int_tuple(newShape), kw{ "dtype", dtype_of(array) });
            padded.set(tuple(originalArea), array);
            if (mode == "constant")
            {
                auto const values = as_pairs(arg(args, 3, 0), ndim, false);
                for (long long axis = 0; axis < ndim; ++axis)
                    set_pad_area(view_roi(padded, originalArea, axis), axis, padWidth[static_cast<std::size_t>(axis)], values[static_cast<std::size_t>(axis)]);
            }
            else if (mode == "empty")
                ;
            else if (array.size() == 0)
            {
                for (long long axis = 0; axis < ndim; ++axis)
                    if (array.array()->shape[static_cast<std::size_t>(axis)] == 0 && (padWidth[static_cast<std::size_t>(axis)].first != 0 || padWidth[static_cast<std::size_t>(axis)].second != 0))
                        raise("ValueError", "can't extend empty axis " + std::to_string(axis) + " using modes other than 'constant' or 'empty'");
            }
            else if (mode == "edge")
            {
                for (long long axis = 0; axis < ndim; ++axis)
                {
                    auto const roi = view_roi(padded, originalArea, axis);
                    set_pad_area(roi, axis, padWidth[static_cast<std::size_t>(axis)], get_edges(roi, axis, padWidth[static_cast<std::size_t>(axis)]));
                }
            }
            else if (mode == "linear_ramp")
            {
                auto const endValues = as_pairs(arg(args, 4, 0), ndim, false);
                for (long long axis = 0; axis < ndim; ++axis)
                {
                    auto const roi = view_roi(padded, originalArea, axis);
                    auto const width = padWidth[static_cast<std::size_t>(axis)];
                    auto const edges = get_edges(roi, axis, width);
                    auto const ramp = [&](value const& aEnd, value const& aEdge, long long aWidth)
                    {
                        return np("linspace")(kw{ "start", aEnd }, kw{ "stop", aEdge.method("squeeze", axis) }, kw{ "num", value{ aWidth } }, kw{ "endpoint", value{ false } },
                            kw{ "dtype", dtype_of(roi) }, kw{ "axis", value{ axis } });
                    };
                    auto const left = ramp(endValues[static_cast<std::size_t>(axis)].first, edges.first, width.first);
                    auto right = ramp(endValues[static_cast<std::size_t>(axis)].second, edges.second, width.second);
                    right = right[slice_at_axis(slice({}, {}, -1), axis)];
                    set_pad_area(roi, axis, width, { left, right });
                }
            }
            else if (mode == "maximum" || mode == "minimum" || mode == "mean" || mode == "median")
            {
                auto const lengths = as_pairs(arg(args, 5), ndim, true);
                char const* const function = mode == "maximum" ? "amax" : mode == "minimum" ? "amin" : mode == "mean" ? "mean" : "median";
                for (long long axis = 0; axis < ndim; ++axis)
                {
                    auto const roi = view_roi(padded, originalArea, axis);
                    auto const width = padWidth[static_cast<std::size_t>(axis)];
                    auto const leftIndex = width.first;
                    auto const rightIndex = roi.array()->shape[static_cast<std::size_t>(axis)] - width.second;
                    auto const maxLength = rightIndex - leftIndex;
                    auto const& lp = lengths[static_cast<std::size_t>(axis)];
                    auto leftLength = lp.first.is_none() ? maxLength : std::min(maxLength, lp.first.to_int());
                    auto rightLength = lp.second.is_none() ? maxLength : std::min(maxLength, lp.second.to_int());
                    if ((leftLength == 0 || rightLength == 0) && (mode == "maximum" || mode == "minimum"))
                        raise("ValueError", "stat_length of 0 yields no value for padding");
                    auto const stat = [&](long long aStart, long long aStop)
                    {
                        auto s = np(function)(roi[slice_at_axis(slice(aStart, aStop), axis)], kw{ "axis", value{ axis } }, kw{ "keepdims", value{ true } });
                        if (is_integer(type_of(roi)))
                            s = np("round")(s);
                        return s;
                    };
                    auto const leftStat = stat(leftIndex, leftIndex + leftLength);
                    if (leftLength == rightLength && rightLength == maxLength)
                        set_pad_area(roi, axis, width, { leftStat, leftStat });
                    else
                        set_pad_area(roi, axis, width, { leftStat, stat(rightIndex - rightLength, rightIndex) });
                }
            }
            else if (mode == "reflect" || mode == "symmetric")
            {
                auto const method = arg(args, 6, "even").to_str();
                bool const includeEdge = mode == "symmetric";
                for (long long axis = 0; axis < ndim; ++axis)
                {
                    auto width = padWidth[static_cast<std::size_t>(axis)];
                    if (array.array()->shape[static_cast<std::size_t>(axis)] == 1 && (width.first > 0 || width.second > 0))
                    {
                        set_pad_area(padded, axis, width, get_edges(padded, axis, width));
                        continue;
                    }
                    auto const roi = view_roi(padded, originalArea, axis);
                    while (width.first > 0 || width.second > 0)
                        width = set_reflect_both(roi, axis, width, method, array.array()->shape[static_cast<std::size_t>(axis)], includeEdge);
                }
            }
            else if (mode == "wrap")
            {
                for (long long axis = 0; axis < ndim; ++axis)
                {
                    auto width = padWidth[static_cast<std::size_t>(axis)];
                    auto const roi = view_roi(padded, originalArea, axis);
                    auto const originalPeriod = padded.array()->shape[static_cast<std::size_t>(axis)] - width.second - width.first;
                    while (width.first > 0 || width.second > 0)
                        width = set_wrap_both(roi, axis, width, originalPeriod);
                }
            }
            return padded.release();
        }

        // --- packbits and unpackbits

        object* f_packbits(object* aArgs, object* aKwargs)
        {
            arguments args{ "packbits", aArgs, aKwargs, { "a", "axis", "bitorder" } };
            if (!args.ok())
                return nullptr;
            auto a = asarray(arg(args, 0));
            if (!is_integer(type_of(a)) && !is_bool(type_of(a)))
                raise("TypeError", "Expected an input array of integer or boolean data type");
            bool const little = arg(args, 2, "big").to_str() == "little";
            long long axis = 0;
            if (!args.has(1))
                a = a.method("ravel");
            else
                axis = normalize_axis_index(arg(args, 1).to_int(), a.ndim());
            a = np("moveaxis")(a, axis, -1);
            auto const* x = a.array();
            auto const n = x->ndim() == 0 ? 1 : x->shape.back();
            shape_t outShape{ x->shape.begin(), x->shape.end() - (x->ndim() == 0 ? 0 : 1) };
            auto const bytes = (n + 7) / 8;
            outShape.push_back(bytes);
            auto* const r = new_array(dtype::UInt8, outShape);
            auto const result = array_value(r);
            auto const outer = element_count(shape_t{ outShape.begin(), outShape.end() - 1 });
            for (std::int64_t o = 0; o < outer; ++o)
            {
                std::int64_t offset = 0, rest = o;
                for (std::size_t d = x->ndim() - 1u; d-- > 0u;)
                {
                    offset += (rest % x->shape[d]) * x->strides[d];
                    rest /= x->shape[d];
                }
                for (std::int64_t i = 0; i < n; ++i)
                    if (read_as<bool>(x->pointer() + offset + i * x->strides.back(), x->type))
                        r->pointer()[o * bytes + i / 8] |= static_cast<unsigned char>(little ? (1u << (i % 8)) : (0x80u >> (i % 8)));
            }
            return np("moveaxis")(result, -1, axis).release();
        }

        object* f_unpackbits(object* aArgs, object* aKwargs)
        {
            arguments args{ "unpackbits", aArgs, aKwargs, { "a", "axis", "count", "bitorder" } };
            if (!args.ok())
                return nullptr;
            auto a = asarray(arg(args, 0));
            if (type_of(a) != dtype::UInt8)
                raise("TypeError", "Expected an input array of unsigned byte data type");
            bool const little = arg(args, 3, "big").to_str() == "little";
            long long axis = 0;
            if (!args.has(1))
                a = a.method("ravel");
            else
                axis = normalize_axis_index(arg(args, 1).to_int(), a.ndim());
            a = np("moveaxis")(a, axis, -1);
            auto const* x = a.array();
            auto const n = x->shape.back();
            auto bits = n * 8;
            if (args.has(2))
            {
                auto const count = arg(args, 2).to_int();
                bits = count >= 0 ? count : std::max(0ll, bits + count);
            }
            shape_t outShape{ x->shape.begin(), x->shape.end() - 1 };
            outShape.push_back(bits);
            auto* const r = new_array(dtype::UInt8, outShape);
            auto const result = array_value(r);
            auto const outer = element_count(shape_t{ outShape.begin(), outShape.end() - 1 });
            for (std::int64_t o = 0; o < outer; ++o)
            {
                std::int64_t offset = 0, rest = o;
                for (std::size_t d = x->ndim() - 1u; d-- > 0u;)
                {
                    offset += (rest % x->shape[d]) * x->strides[d];
                    rest /= x->shape[d];
                }
                for (std::int64_t i = 0; i < bits && i < n * 8; ++i)
                {
                    auto const byte = x->pointer()[offset + (i / 8) * x->strides.back()];
                    r->pointer()[o * bits + i] = (byte >> (little ? (i % 8) : (7 - i % 8))) & 1u;
                }
            }
            return np("moveaxis")(result, -1, axis).release();
        }

        object* f_bitwise_count(object* aArgs, object* aKwargs)
        {
            arguments args{ "bitwise_count", aArgs, aKwargs, { "x", "out", "*where", "*casting", "*order", "*dtype", "*subok" } };
            if (!args.ok())
                return nullptr;
            auto const a = asarray(arg(args, 0));
            auto const* x = a.array();
            if (!is_integer(x->type) && !is_bool(x->type))
                raise("TypeError", "ufunc 'bitwise_count' not supported for the input types, and the inputs could not be safely coerced to any supported types according to the casting rule ''safe''");
            auto* const r = new_array(dtype::UInt8, x->shape);
            auto const result = array_value(r);
            std::int64_t i = 0;
            for_each(*x, [&](unsigned char* p)
            {
                std::uint64_t v;
                if (is_signed(x->type))
                {
                    auto const s = read_as<std::int64_t>(p, x->type);
                    v = s < 0 ? 0ull - static_cast<std::uint64_t>(s) : static_cast<std::uint64_t>(s);
                }
                else
                    v = read_as<std::uint64_t>(p, x->type);
                r->pointer()[i++] = static_cast<unsigned char>(std::popcount(v));
            });
            return result_object((ctx->retain(result.get()), r));
        }
    }

    // (the array's flags)
    object* array_flags(ndarray* aArray)
    {
        return guarded([&]()
        {
            auto* const i = new_instance("flagsobj");
            auto const result = instance_value(i);
            bool const c = aArray->c_contiguous(), f = aArray->f_contiguous();
            bool const own = !aArray->view;
            add_attribute(i, "c_contiguous", c);
            add_attribute(i, "f_contiguous", f);
            add_attribute(i, "contiguous", c);
            add_attribute(i, "fortran", f);
            add_attribute(i, "owndata", own);
            add_attribute(i, "writeable", aArray->writeable);
            add_attribute(i, "aligned", true);
            add_attribute(i, "writebackifcopy", false);
            add_attribute(i, "fnc", f && !c);
            add_attribute(i, "forc", f || c);
            add_attribute(i, "behaved", aArray->writeable);
            add_attribute(i, "carray", c && aArray->writeable);
            add_attribute(i, "farray", f && !c && aArray->writeable);
            auto const b = [](bool x) { return std::string{ x ? "True" : "False" }; };
            i->reprText = "  C_CONTIGUOUS : " + b(c) + "\n  F_CONTIGUOUS : " + b(f) + "\n  OWNDATA : " + b(own) + "\n  WRITEABLE : " + b(aArray->writeable) +
                "\n  ALIGNED : True\n  WRITEBACKIFCOPY : False\n";
            auto const flags = std::vector<std::pair<std::string, bool>>{ { "C", c }, { "C_CONTIGUOUS", c }, { "CONTIGUOUS", c }, { "F", f }, { "F_CONTIGUOUS", f },
                { "FORTRAN", f }, { "O", own }, { "OWNDATA", own }, { "W", aArray->writeable }, { "WRITEABLE", aArray->writeable }, { "A", true }, { "ALIGNED", true },
                { "X", false }, { "WRITEBACKIFCOPY", false }, { "FNC", f && !c }, { "FORC", f || c }, { "B", aArray->writeable }, { "BEHAVED", aArray->writeable },
                { "CA", c && aArray->writeable }, { "CARRAY", c && aArray->writeable }, { "FA", f && !c && aArray->writeable }, { "FARRAY", f && !c && aArray->writeable } };
            i->getitem = [flags](value const& aKey)
            {
                auto const key = aKey.to_str();
                for (auto const& flag : flags)
                    if (flag.first == key)
                        return value{ flag.second };
                raise("KeyError", "Unknown flag");
            };
            return result.release();
        });
    }


    object* index_object_attribute(std::string_view aName)
    {
        static std::map<std::string, object*, std::less<>> sObjects;
        auto existing = sObjects.find(aName);
        if (existing == sObjects.end())
        {
            auto const o = guarded([&]() { return index_object(std::string{ aName }).release(); });
            if (o == nullptr)
                return nullptr;
            existing = sObjects.emplace(std::string{ aName }, o).first;
        }
        ctx->retain(existing->second);
        return existing->second;
    }

    // --- iterators: flatiter, nditer, broadcast; and other module functions

    namespace
    {
        std::string object_address(void const* aObject)
        {
            char buffer[32];
            std::snprintf(buffer, sizeof(buffer), "%p", aObject);
            return buffer;
        }

        // (a callable object: a method of an instance)
        value callable(std::string const& aName, std::function<value(value const&, value const&)> aFunction)
        {
            auto* const i = new_instance("builtin_function_or_method");
            i->reprText = "<built-in method " + aName + ">";
            i->call = std::move(aFunction);
            return instance_value(i);
        }

        std::vector<long long> unravel(std::int64_t aIndex, shape_t const& aShape)
        {
            std::vector<long long> result(aShape.size());
            for (std::size_t d = aShape.size(); d-- > 0u;)
            {
                result[d] = aShape[d] != 0 ? aIndex % aShape[d] : 0;
                aIndex = aShape[d] != 0 ? aIndex / aShape[d] : 0;
            }
            return result;
        }

        std::int64_t offset_of(ndarray const& aArray, std::vector<long long> const& aIndex)
        {
            std::int64_t offset = 0;
            for (std::size_t d = 0u; d < aIndex.size(); ++d)
                offset += aIndex[d] * aArray.strides[d];
            return offset;
        }

        value make_flatiter_value(value const& aArray)
        {
            auto* const i = new_instance("flatiter");
            auto* const self = &i->base;
            i->reprText = "<numpy.flatiter object at " + object_address(self) + ">";
            auto const position = std::make_shared<std::int64_t>(0);
            auto const array = aArray;
            i->getattr = [array, position](char const* aName) -> value
            {
                std::string_view const name{ aName };
                auto const* a = array.array();
                if (name == "base")
                    return array;
                if (name == "index")
                    return value{ static_cast<long long>(*position) };
                if (name == "coords")
                    return int_tuple(unravel(*position, a->shape));
                if (name == "copy")
                    return callable("copy", [array](value const&, value const&) { return np("ravel")(array).method("copy"); });
                if (name == "__array__")
                    return callable("__array__", [array](value const&, value const&) { return np("ravel")(array).method("copy"); });
                return {};
            };
            i->getitem = [array](value const& aKey) -> value
            {
                auto const result = np("ravel")(array)[aKey];
                return isinstance_array(result) ? result.method("copy") : result;
            };
            i->setitem = [array](value const& aKey, value const& aValue)
            {
                auto const indices = np("arange")(array.size())[aKey];
                np("put")(array, indices, aValue);
            };
            i->length = [array]() { return array.size(); };
            i->iter = [self]() { return value::borrow(self); };
            i->next = [array, position]() -> value
            {
                auto const* a = array.array();
                if (*position >= a->size())
                    return {};
                auto const offset = offset_of(*a, unravel((*position)++, a->shape));
                return value::steal(scalar_from(*a, a->pointer() + offset));
            };
            return instance_value(i);
        }

        // np.nditer (the common uses: elementwise iteration of broadcast operands in memory order ('K'), C or F order,
        // multi_index, c_index/f_index, external_loop and writing through the elements' views)
        struct nditer_state
        {
            std::vector<value> operands;    // (broadcast views)
            std::vector<value> original;
            shape_t shape;
            std::vector<std::size_t> perm;  // (iteration axes, outermost first)
            std::vector<bool> flipped;      // (by axis)
            bool multiIndex = false;
            bool externalLoop = false;
            int indexOrder = 0;             // (1: c_index, 2: f_index)
            std::int64_t inner = 1;         // (external loop: the length of the inner loop)
            std::int64_t itersize = 0;
            std::int64_t position = 0;
            bool started = false;
        };

        // (the coordinates of iteration position aPosition)
        std::vector<long long> nditer_coords(nditer_state const& aState, std::int64_t aPosition)
        {
            std::vector<long long> coords(aState.shape.size(), 0);
            for (std::size_t k = aState.perm.size(); k-- > 0u;)
            {
                auto const axis = aState.perm[k];
                auto const n = aState.shape[axis];
                auto c = n != 0 ? aPosition % n : 0;
                aPosition = n != 0 ? aPosition / n : 0;
                coords[axis] = aState.flipped[axis] ? n - 1 - c : c;
            }
            return coords;
        }

        value nditer_value(nditer_state const& aState)
        {
            auto const coords = nditer_coords(aState, aState.position);
            std::vector<value> items;
            for (auto const& op : aState.operands)
            {
                auto* const a = op.array();
                auto const offset = static_cast<std::int64_t>(a->offset) + offset_of(*a, coords);
                if (aState.externalLoop)
                {
                    auto const axis = aState.perm.empty() ? 0u : aState.perm.back();
                    auto const stride = aState.perm.empty() ? 0 : (aState.flipped[axis] ? -a->strides[axis] : a->strides[axis]);
                    items.push_back(array_value(new_view(a, a->type, static_cast<std::size_t>(offset), { aState.inner }, { stride })));
                }
                else
                    items.push_back(array_value(new_view(a, a->type, static_cast<std::size_t>(offset), {}, {})));
            }
            return items.size() == 1u ? items[0] : tuple(items);
        }

        object* f_nditer(object* aArgs, object* aKwargs)
        {
            arguments args{ "nditer", aArgs, aKwargs, { "op", "flags", "op_flags", "op_dtypes", "order", "casting", "op_axes", "itershape", "buffersize" } };
            if (!args.ok())
                return nullptr;
            auto const state = std::make_shared<nditer_state>();
            auto const op = arg(args, 0);
            bool const several = is_list(op) || is_tuple(op);
            if (several)
                for (auto const& x : op.items())
                    state->original.push_back(isinstance_array(x) ? x : asarray(x));
            else
                state->original.push_back(isinstance_array(op) ? op : asarray(op));
            if (args.has(3))
            {
                auto const dtypes = arg(args, 3);
                auto const types = is_list(dtypes) || is_tuple(dtypes) ? dtypes.items() : std::vector<value>{ dtypes };
                for (std::size_t k = 0u; k < types.size() && k < state->original.size(); ++k)
                    if (!types[k].is_none())
                        state->original[k] = state->original[k].method("astype", types[k]);
            }
            std::set<std::string> flags;
            if (args.has(1))
                for (auto const& f : arg(args, 1).items())
                    flags.insert(f.to_str());
            state->multiIndex = flags.contains("multi_index");
            state->externalLoop = flags.contains("external_loop");
            state->indexOrder = flags.contains("c_index") ? 1 : flags.contains("f_index") ? 2 : 0;
            if (state->multiIndex && state->externalLoop)
                raise("ValueError", "Iterator flag EXTERNAL_LOOP cannot be used if an index or multi-index is being tracked");
            std::vector<shape_t const*> shapes;
            for (auto const& o : state->original)
                shapes.push_back(&o.array()->shape);
            if (!broadcast_shapes(shapes, state->shape))
                throw raised{};
            for (auto const& o : state->original)
            {
                auto* const a = o.array();
                state->operands.push_back(array_value(new_view(a, a->type, a->offset, state->shape, broadcast_strides(*a, state->shape))));
            }
            auto const ndim = state->shape.size();
            auto const order = arg(args, 4, "K").to_str();
            state->flipped.assign(ndim, false);
            for (std::size_t d = 0u; d < ndim; ++d)
                state->perm.push_back(d);
            if (order == "F")
                std::reverse(state->perm.begin(), state->perm.end());
            else if (order == "K" || order == "A")
            {
                // (memory order: axes with larger strides outermost (insertion sort, as NumPy); negative strides flipped)
                for (std::size_t d = 0u; d < ndim; ++d)
                {
                    bool allNegative = true, anyNonZero = false;
                    for (auto const& o : state->operands)
                    {
                        auto const s = o.array()->strides[d];
                        if (s > 0) allNegative = false;
                        if (s != 0) anyNonZero = true;
                    }
                    state->flipped[d] = anyNonZero && allNegative && state->shape[d] > 1;
                }
                // (NumPy's npyiter_find_best_axis_ordering: from the innermost axis, each axis moves outward past the
                // axes whose strides are all smaller (ambiguous axes keep their order))
                std::vector<std::size_t> axes(state->perm.rbegin(), state->perm.rend());   // (innermost first)
                for (std::size_t i = 1u; i < ndim; ++i)
                {
                    std::size_t const ax = axes[i];
                    std::size_t j = i;
                    for (std::size_t k = i; k-- > 0u;)
                    {
                        bool shouldSwap = false, ambiguous = true;
                        auto const other = axes[k];
                        for (auto const& o : state->operands)
                        {
                            auto const sa = std::llabs(o.array()->strides[ax]);
                            auto const so = std::llabs(o.array()->strides[other]);
                            if (sa != 0 && so != 0)
                            {
                                if (sa < so)
                                {
                                    shouldSwap = true;
                                    ambiguous = false;
                                }
                                else
                                {
                                    shouldSwap = false;
                                    ambiguous = false;
                                    break;
                                }
                            }
                        }
                        if (!ambiguous)
                        {
                            if (shouldSwap)
                                j = k;
                            else
                                break;
                        }
                    }
                    if (j != i)
                    {
                        axes.erase(axes.begin() + static_cast<std::ptrdiff_t>(i));
                        axes.insert(axes.begin() + static_cast<std::ptrdiff_t>(j), ax);
                    }
                }
                state->perm.assign(axes.rbegin(), axes.rend());
            }
            state->itersize = element_count(state->shape);
            if (state->externalLoop)
            {
                // (the inner loop: the innermost axes that coalesce)
                state->inner = ndim == 0 ? 1 : state->shape[state->perm.back()];
                while (state->perm.size() > 1u)
                {
                    auto const innerAxis = state->perm.back();
                    auto const outerAxis = state->perm[state->perm.size() - 2u];
                    bool coalesce = state->flipped[innerAxis] == state->flipped[outerAxis];
                    for (auto const& o : state->operands)
                        coalesce = coalesce && o.array()->strides[outerAxis] == state->inner * o.array()->strides[innerAxis];
                    if (!coalesce)
                        break;
                    state->inner *= state->shape[outerAxis];
                    state->perm.erase(state->perm.end() - 2);
                    // (the coalesced axes iterate as the inner axis)
                    state->shape[innerAxis] = state->inner;
                }
            }
            auto* const i = new_instance("nditer");
            auto* const self = &i->base;
            i->reprText = "<numpy.nditer object at " + object_address(self) + ">";
            auto const step = [state]() { return state->externalLoop ? state->inner : 1; };
            auto const finished = [state, step]() { return state->position >= state->itersize; };
            i->iter = [self]() { return value::borrow(self); };
            i->next = [state, finished, step]() -> value
            {
                // (as NumPy: the first next() is the current element; later ones advance first)
                if (state->started && !finished())
                    state->position += step();
                state->started = true;
                if (finished())
                    return {};
                return nditer_value(*state);
            };
            i->getitem = [state](value const& aKey) -> value
            {
                auto const k = aKey.to_int();
                if (k < 0 || k >= static_cast<long long>(state->operands.size()))
                    raise("IndexError", "Iterator operand index " + std::to_string(k) + " is out of bounds");
                auto const v = nditer_value(*state);
                return state->operands.size() == 1u ? v : v[k];
            };
            i->length = [state]() { return state->itersize; };
            i->getattr = [state, finished, step, self](char const* aName) -> value
            {
                std::string_view const name{ aName };
                if (name == "finished")
                    return value{ finished() };
                if (name == "iternext")
                    return callable("iternext", [state, finished, step](value const&, value const&) { state->position += step(); return value{ !finished() }; });
                if (name == "reset")
                    return callable("reset", [state](value const&, value const&) { state->position = 0; state->started = false; return py::none(); });
                if (name == "close" || name == "__exit__")
                    return callable(aName, [](value const&, value const&) { return py::none(); });
                if (name == "__enter__")
                    return callable(aName, [self](value const&, value const&) { return value::borrow(self); });
                if (name == "multi_index")
                {
                    if (!state->multiIndex)
                        raise("ValueError", "Iterator is not tracking a multi-index");
                    if (finished())
                        raise("ValueError", "Iterator is past the end");
                    return int_tuple(nditer_coords(*state, state->position));
                }
                if (name == "index")
                {
                    if (state->indexOrder == 0)
                        raise("ValueError", "Iterator does not have an index");
                    auto const coords = nditer_coords(*state, state->position);
                    std::int64_t index = 0;
                    if (state->indexOrder == 1)
                        for (std::size_t d = 0u; d < coords.size(); ++d)
                            index = index * state->shape[d] + coords[d];
                    else
                        for (std::size_t d = coords.size(); d-- > 0u;)
                            index = index * state->shape[d] + coords[d];
                    return value{ static_cast<long long>(index) };
                }
                if (name == "value")
                    return nditer_value(*state);
                if (name == "operands")
                    return tuple(state->original);
                if (name == "itviews")
                    return tuple(state->operands);
                if (name == "shape")
                    return int_tuple(std::vector<long long>(state->shape.begin(), state->shape.end()));
                if (name == "ndim")
                    return value{ static_cast<long long>(state->shape.size()) };
                if (name == "nop")
                    return value{ static_cast<long long>(state->operands.size()) };
                if (name == "itersize")
                    return value{ static_cast<long long>(state->itersize) };
                if (name == "iterindex")
                    return value{ static_cast<long long>(state->position) };
                if (name == "has_multi_index")
                    return value{ state->multiIndex };
                if (name == "has_index")
                    return value{ state->indexOrder != 0 };
                if (name == "dtypes")
                {
                    std::vector<value> types;
                    for (auto const& o : state->operands)
                        types.push_back(dtype_of(o));
                    return tuple(types);
                }
                return {};
            };
            return instance_value(i).release();
        }

        object* f_broadcast(object* aArgs, object* aKwargs)
        {
            arguments args{ "broadcast", aArgs, aKwargs, { "..." } };
            if (!args.ok())
                return nullptr;
            std::vector<value> arrays;
            for (auto x : args.extra())
                arrays.push_back(asarray(value::borrow(x)));
            if (arrays.size() > 64u)
                raise("ValueError", "Need at least 0 and at most 64 array objects.");
            std::vector<shape_t const*> shapes;
            for (auto const& a : arrays)
                shapes.push_back(&a.array()->shape);
            shape_t shape;
            if (!broadcast_shapes(shapes, shape))
                throw raised{};
            std::vector<value> views;
            for (auto const& a : arrays)
                views.push_back(np("broadcast_to")(a, int_tuple(std::vector<long long>(shape.begin(), shape.end()))));
            auto* const i = new_instance("broadcast");
            auto* const self = &i->base;
            i->reprText = "<numpy.broadcast object at " + object_address(self) + ">";
            auto const position = std::make_shared<std::int64_t>(0);
            auto const size = element_count(shape);
            i->iter = [self]() { return value::borrow(self); };
            i->next = [views, position, size, shape]() -> value
            {
                if (*position >= size)
                    return {};
                auto const coords = unravel((*position)++, shape);
                std::vector<value> items;
                for (auto const& v : views)
                {
                    auto const* a = v.array();
                    items.push_back(value::steal(scalar_from(*a, a->pointer() + offset_of(*a, coords))));
                }
                return tuple(items);
            };
            i->getattr = [views, position, size, shape](char const* aName) -> value
            {
                std::string_view const name{ aName };
                if (name == "shape")
                    return int_tuple(std::vector<long long>(shape.begin(), shape.end()));
                if (name == "nd" || name == "ndim")
                    return value{ static_cast<long long>(shape.size()) };
                if (name == "size")
                    return value{ static_cast<long long>(size) };
                if (name == "numiter")
                    return value{ static_cast<long long>(views.size()) };
                if (name == "index")
                    return value{ static_cast<long long>(*position) };
                if (name == "iters")
                {
                    std::vector<value> iters;
                    for (auto const& v : views)
                        iters.push_back(make_flatiter_value(v));
                    return tuple(iters);
                }
                if (name == "reset")
                    return callable("reset", [position](value const&, value const&) { *position = 0; return py::none(); });
                return {};
            };
            return instance_value(i).release();
        }

        object* f_astype(object* aArgs, object* aKwargs)
        {
            arguments args{ "astype", aArgs, aKwargs, { "x", "dtype", "*copy", "*device" } };
            if (!args.ok())
                return nullptr;
            auto const x = arg(args, 0);
            if (!is_array(x.get()))
                raise("TypeError", "Input should be a NumPy array or scalar. It is a <class '" + type_name(x.get()) + "'> instead.");
            bool const copy = arg(args, 2, true).truth();
            auto const t = parse_dtype(args[1]);
            if (!t)
                throw raised{};
            if (!copy && x.array()->type == *t)
                return value{ x }.release();
            return x.method("astype", arg(args, 1)).release();
        }

        object* f_mask_indices(object* aArgs, object* aKwargs)
        {
            arguments args{ "mask_indices", aArgs, aKwargs, { "n", "mask_func", "k" } };
            if (!args.ok())
                return nullptr;
            auto const n = arg(args, 0);
            auto const m = np("ones")(tuple({ n, n }), np("int64"));
            auto const a = arg(args, 1)(m, arg(args, 2, 0));
            return np("nonzero")(a != 0).release();
        }

        value& error_call()
        {
            static value sCall;
            return sCall;
        }

        object* f_geterrcall(object* aArgs, object* aKwargs)
        {
            arguments args{ "geterrcall", aArgs, aKwargs, {} };
            if (!args.ok())
                return nullptr;
            return error_call().empty() ? new_none() : error_call().release();
        }

        object* f_seterrcall(object* aArgs, object* aKwargs)
        {
            arguments args{ "seterrcall", aArgs, aKwargs, { "func" } };
            if (!args.ok())
                return nullptr;
            auto const func = arg(args, 0);
            if (!func.is_none() && !is_callable(func))
                raise("ValueError", "Only callable can be used as callback");
            auto const old = error_call();
            error_call() = func.is_none() ? value{} : func;
            return old.empty() ? new_none() : old.release();
        }

        object* f_setbufsize(object* aArgs, object* aKwargs)
        {
            arguments args{ "setbufsize", aArgs, aKwargs, { "size" } };
            if (!args.ok())
                return nullptr;
            auto const size = arg(args, 0).to_int();
            if (size > 10000000)
                raise("ValueError", "Buffer size, " + std::to_string(size) + ", is too big");
            if (size < 5)
                raise("ValueError", "Buffer size, " + std::to_string(size) + ", is too small");
            if (size % 16 != 0)
                raise("ValueError", "Buffer size, " + std::to_string(size) + ", is not a multiple of 16");
            auto const old = buffer_size();
            buffer_size() = size;
            return make_int(old);
        }

        int ufunc_instancecheck(neos_py_context*, void*, object* aObject)
        {
            return type_name(aObject) == "ufunc" ? 1 : 0;
        }

        object* ufunc_construct(neos_py_context*, void*, object*, object*)
        {
            return error("TypeError", "cannot create 'numpy.ufunc' instances");
        }
    }

    // (a.flat)
    object* array_flat(object* aArray)
    {
        return guarded([&]() { return make_flatiter_value(value::borrow(aArray)).release(); });
    }

    // (a.flat = value)
    int array_set_flat(object* aArray, object* aValue)
    {
        try
        {
            auto const a = value::borrow(aArray);
            np("put")(a, np("arange")(a.array()->size()), value::borrow(aValue));
            return 0;
        }
        catch (raised const&)
        {
            return -1;
        }
    }

    object* module_constant(std::string_view aName)
    {
        return guarded([&]() -> object*
        {
            if (aName == "mgrid" || aName == "ogrid" || aName == "r_" || aName == "c_" || aName == "s_" || aName == "index_exp")
                return index_object_attribute(aName);
            if (aName == "little_endian")
                return make_bool(true);
            if (aName == "ufunc")
            {
                static object* sUfunc = ctx->type_new(ctx, "ufunc", "numpy.ufunc", ufunc_construct, nullptr, ufunc_instancecheck);
                ctx->retain(sUfunc);
                return sUfunc;
            }
            if (aName == "typecodes")
                return dict({ { "Character", value{ "c" } }, { "Integer", value{ "bhilqnp" } }, { "UnsignedInteger", value{ "BHILQNP" } }, { "Float", value{ "efdg" } },
                    { "Complex", value{ "FDG" } }, { "AllInteger", value{ "bBhHiIlLqQnNpP" } }, { "AllFloat", value{ "efdgFDG" } }, { "Datetime", value{ "Mm" } },
                    { "All", value{ "?bhilqnpBHILQNPefdgFDGSUVOMm" } } }).release();
            return nullptr;
        });
    }

    void register_lib3()
    {
        register_functions({
            { "finfo", f_finfo }, { "iinfo", f_iinfo }, { "issubdtype", f_issubdtype }, { "isdtype", f_isdtype }, { "min_scalar_type", f_min_scalar_type },
            { "common_type", f_common_type }, { "mintypecode", f_mintypecode }, { "typename", f_typename }, { "iterable", f_iterable },
            { "base_repr", f_base_repr }, { "binary_repr", f_binary_repr }, { "may_share_memory", f_may_share_memory }, { "shares_memory", f_shares_memory },
            { "asfortranarray", f_asfortranarray }, { "asarray_chkfinite", f_asarray_chkfinite }, { "isfortran", f_isfortran }, { "require", f_require },
            { "geterr", f_geterr }, { "seterr", f_seterr }, { "getbufsize", f_getbufsize }, { "apply_along_axis", f_apply_along_axis },
            { "apply_over_axes", f_apply_over_axes }, { "fromfunction", f_fromfunction }, { "fromiter", f_fromiter }, { "vectorize", f_vectorize },
            { "ndindex", f_ndindex }, { "ndenumerate", f_ndenumerate }, { "pad", f_pad }, { "packbits", f_packbits }, { "unpackbits", f_unpackbits },
            { "bitwise_count", f_bitwise_count }, { "nditer", f_nditer }, { "broadcast", f_broadcast }, { "astype", f_astype },
            { "mask_indices", f_mask_indices }, { "geterrcall", f_geterrcall }, { "seterrcall", f_seterrcall }, { "setbufsize", f_setbufsize }
        });
    }
}
