/*
  core.cpp

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

// dtypes, element access, arrays, conversion of Python objects to arrays, broadcasting and argument parsing

#include <algorithm>
#include <cmath>
#include <map>
#include "numpy.hpp"
#include "cast.hpp"

namespace neos::numpy
{
    neos_py_context* ctx = nullptr;

    // --- Python objects

    std::nullptr_t error(char const* aType, std::string const& aMessage)
    {
        ctx->raise(ctx, aType, aMessage.c_str());
        return nullptr;
    }

    bool failed() { return ctx->failed(ctx) != 0; }
    int kind(object* aObject) { return ctx->kind(ctx, aObject); }
    std::string type_name(object* aObject) { return ctx->type_name(ctx, aObject); }
    object* none() { return ctx->none(ctx); }
    object* new_none() { return ctx->none(ctx); }
    object* make_bool(bool aValue) { return ctx->from_bool(ctx, aValue ? 1 : 0); }
    object* make_int(long long aValue) { return ctx->from_int(ctx, aValue); }
    object* make_float(double aValue) { return ctx->from_float(ctx, aValue); }
    object* make_complex(std::complex<double> aValue) { return ctx->from_complex(ctx, aValue.real(), aValue.imag()); }
    object* make_str(std::string const& aValue) { return ctx->from_str(ctx, aValue.data(), aValue.size()); }

    object* make_tuple(std::vector<object*> aItems)
    {
        auto const result = ctx->tuple_new(ctx, aItems.size());
        for (std::size_t i = 0u; i < aItems.size(); ++i)
            ctx->tuple_set(ctx, result, i, aItems[i]);
        return result;
    }

    object* make_list(std::vector<object*> aItems)
    {
        auto const result = ctx->list_new(ctx);
        for (auto item : aItems)
        {
            ctx->list_append(ctx, result, item);
            ctx->release(item);
        }
        return result;
    }

    std::optional<std::string> as_str(object* aObject)
    {
        std::size_t length = 0u;
        auto const text = ctx->as_str(ctx, aObject, &length);
        if (text == nullptr)
            return {};
        return std::string{ text, length };
    }

    bool is_sequence(object* aObject)
    {
        auto const k = kind(aObject);
        return k == NEOS_PY_LIST || k == NEOS_PY_TUPLE;
    }

    std::vector<object*> sequence_items(object* aObject)
    {
        std::vector<object*> result;
        auto const n = ctx->sequence_size(ctx, aObject);
        for (std::size_t i = 0u; i < n; ++i)
            result.push_back(ctx->sequence_item(ctx, aObject, i));
        return result;
    }

    bool is_python_scalar(object* aObject)
    {
        auto const k = kind(aObject);
        return k == NEOS_PY_BOOL || k == NEOS_PY_INT || k == NEOS_PY_FLOAT || k == NEOS_PY_COMPLEX;
    }

    // --- dtypes

    namespace
    {
        dtype_info const sInfo[] =
        {
            { "bool", 'b', '?', 1u, "|b1", "bool" },
            { "int8", 'i', 'b', 1u, "|i1", "int8" },
            { "uint8", 'u', 'B', 1u, "|u1", "uint8" },
            { "int16", 'i', 'h', 2u, "<i2", "int16" },
            { "uint16", 'u', 'H', 2u, "<u2", "uint16" },
            { "int32", 'i', 'i', 4u, "<i4", "int32" },
            { "uint32", 'u', 'I', 4u, "<u4", "uint32" },
            { "int64", 'i', 'l', 8u, "<i8", "int64" },
            { "uint64", 'u', 'L', 8u, "<u8", "uint64" },
            { "float16", 'f', 'e', 2u, "<f2", "float16" },
            { "float32", 'f', 'f', 4u, "<f4", "float32" },
            { "float64", 'f', 'd', 8u, "<f8", "float64" },
            { "complex64", 'c', 'F', 8u, "<c8", "complex64" },
            { "complex128", 'c', 'D', 16u, "<c16", "complex128" }
        };
    }

    dtype_info const& info(dtype aType)
    {
        return sInfo[static_cast<int>(aType)];
    }

    std::optional<dtype> dtype_from_name(std::string_view aName)
    {
        static std::map<std::string, dtype, std::less<>> const sNames =
        {
            { "bool", dtype::Bool }, { "bool_", dtype::Bool }, { "?", dtype::Bool }, { "b1", dtype::Bool }, { "|b1", dtype::Bool },
            { "int8", dtype::Int8 }, { "i1", dtype::Int8 }, { "b", dtype::Int8 }, { "byte", dtype::Int8 }, { "|i1", dtype::Int8 },
            { "uint8", dtype::UInt8 }, { "u1", dtype::UInt8 }, { "B", dtype::UInt8 }, { "ubyte", dtype::UInt8 }, { "|u1", dtype::UInt8 },
            { "int16", dtype::Int16 }, { "i2", dtype::Int16 }, { "h", dtype::Int16 }, { "short", dtype::Int16 }, { "<i2", dtype::Int16 },
            { "uint16", dtype::UInt16 }, { "u2", dtype::UInt16 }, { "H", dtype::UInt16 }, { "ushort", dtype::UInt16 }, { "<u2", dtype::UInt16 },
            { "int32", dtype::Int32 }, { "i4", dtype::Int32 }, { "i", dtype::Int32 }, { "intc", dtype::Int32 }, { "<i4", dtype::Int32 },
            { "uint32", dtype::UInt32 }, { "u4", dtype::UInt32 }, { "I", dtype::UInt32 }, { "uintc", dtype::UInt32 }, { "<u4", dtype::UInt32 },
            { "int64", dtype::Int64 }, { "i8", dtype::Int64 }, { "l", dtype::Int64 }, { "q", dtype::Int64 }, { "int", dtype::Int64 },
            { "int_", dtype::Int64 }, { "intp", dtype::Int64 }, { "long", dtype::Int64 }, { "longlong", dtype::Int64 }, { "<i8", dtype::Int64 },
            { "p", dtype::Int64 },
            { "uint64", dtype::UInt64 }, { "u8", dtype::UInt64 }, { "L", dtype::UInt64 }, { "Q", dtype::UInt64 }, { "uint", dtype::UInt64 },
            { "uintp", dtype::UInt64 }, { "ulong", dtype::UInt64 }, { "ulonglong", dtype::UInt64 }, { "<u8", dtype::UInt64 }, { "P", dtype::UInt64 },
            { "float16", dtype::Float16 }, { "f2", dtype::Float16 }, { "e", dtype::Float16 }, { "half", dtype::Float16 }, { "<f2", dtype::Float16 },
            { "float32", dtype::Float32 }, { "f4", dtype::Float32 }, { "f", dtype::Float32 }, { "single", dtype::Float32 }, { "<f4", dtype::Float32 },
            { "float64", dtype::Float64 }, { "f8", dtype::Float64 }, { "d", dtype::Float64 }, { "double", dtype::Float64 }, { "float", dtype::Float64 },
            { "float_", dtype::Float64 }, { "<f8", dtype::Float64 },
            { "complex64", dtype::Complex64 }, { "c8", dtype::Complex64 }, { "F", dtype::Complex64 }, { "csingle", dtype::Complex64 }, { "<c8", dtype::Complex64 },
            { "complex128", dtype::Complex128 }, { "c16", dtype::Complex128 }, { "D", dtype::Complex128 }, { "cdouble", dtype::Complex128 },
            { "complex", dtype::Complex128 }, { "complex_", dtype::Complex128 }, { "<c16", dtype::Complex128 }
        };
        auto const found = sNames.find(aName);
        if (found == sNames.end())
            return {};
        return found->second;
    }

    std::optional<dtype> parse_dtype(object* aObject)
    {
        if (aObject == nullptr || kind(aObject) == NEOS_PY_NONE)
            return {};
        if (auto const t = dtype_of_object(aObject))
            return t;
        if (auto const text = as_str(aObject))
        {
            if (auto const t = dtype_from_name(*text))
                return t;
            error("TypeError", "data type '" + *text + "' not understood");
            return {};
        }
        // a Python type (int, float, bool, complex)
        if (type_name(aObject) == "type")
        {
            ref const name{ ctx->getattr(ctx, aObject, "__name__") };
            if (name)
                if (auto const text = as_str(name.get()))
                {
                    if (*text == "int") return dtype::Int64;
                    if (*text == "float") return dtype::Float64;
                    if (*text == "bool") return dtype::Bool;
                    if (*text == "complex") return dtype::Complex128;
                }
        }
        error("TypeError", "Cannot interpret '" + std::string{ type_name(aObject) } + "' as a data type");
        return {};
    }

    namespace
    {
        int rank_of(dtype t)
        {
            switch (t)
            {
            case dtype::Int8: case dtype::UInt8: return 1;
            case dtype::Int16: case dtype::UInt16: return 2;
            case dtype::Int32: case dtype::UInt32: return 4;
            case dtype::Int64: case dtype::UInt64: return 8;
            default: return 0;
            }
        }

        dtype signed_of_size(int aSize)
        {
            switch (aSize)
            {
            case 1: return dtype::Int8;
            case 2: return dtype::Int16;
            case 4: return dtype::Int32;
            default: return dtype::Int64;
            }
        }

        dtype unsigned_of_size(int aSize)
        {
            switch (aSize)
            {
            case 1: return dtype::UInt8;
            case 2: return dtype::UInt16;
            case 4: return dtype::UInt32;
            default: return dtype::UInt64;
            }
        }
    }

    dtype smallest_float_for(dtype aType)
    {
        switch (aType)
        {
        case dtype::Bool: case dtype::Int8: case dtype::UInt8: return dtype::Float16;
        case dtype::Int16: case dtype::UInt16: return dtype::Float32;
        default: return dtype::Float64;
        }
    }

    dtype complex_for(dtype aFloat)
    {
        return aFloat == dtype::Float64 ? dtype::Complex128 : dtype::Complex64;
    }

    dtype real_for(dtype aComplex)
    {
        return aComplex == dtype::Complex128 ? dtype::Float64 : dtype::Float32;
    }

    dtype promote(dtype a, dtype b)
    {
        if (a == b)
            return a;
        if (a == dtype::Bool)
            return b;
        if (b == dtype::Bool)
            return a;
        if (is_integer(a) && is_integer(b))
        {
            if (is_signed(a) == is_signed(b))
                return rank_of(a) >= rank_of(b) ? a : b;
            auto const s = is_signed(a) ? a : b;
            auto const u = is_signed(a) ? b : a;
            if (rank_of(s) > rank_of(u))
                return s;
            if (rank_of(u) < 8)
                return signed_of_size(rank_of(u) * 2);
            return dtype::Float64;
        }
        if (is_complex(a) || is_complex(b))
        {
            // (the complex type with at least the precision of both)
            auto const precision = [](dtype t) -> dtype
            {
                if (is_complex(t)) return real_for(t);
                if (is_float(t)) return t;
                return smallest_float_for(t);
            };
            auto const p = promote(precision(a), precision(b));
            return p == dtype::Float64 ? dtype::Complex128 : dtype::Complex64;
        }
        // (a float and an integer or float)
        auto const fa = is_float(a) ? a : smallest_float_for(a);
        auto const fb = is_float(b) ? b : smallest_float_for(b);
        return static_cast<int>(fa) >= static_cast<int>(fb) ? fa : fb;
    }

    bool can_cast(dtype aFrom, dtype aTo, std::string_view aCasting)
    {
        if (aCasting == "unsafe")
            return true;
        if (aCasting == "no" || aCasting == "equiv")
            return aFrom == aTo;
        bool const safe = (promote(aFrom, aTo) == aTo);
        if (aCasting == "safe")
            return safe;
        // same_kind
        if (safe)
            return true;
        auto const order = [](dtype t) { return is_bool(t) ? 0 : is_integer(t) ? (is_unsigned(t) ? 1 : 2) : is_float(t) ? 3 : 4; };
        if (is_unsigned(aFrom) && is_signed(aTo))
            return true;
        return order(aFrom) >= order(aTo) ? (order(aFrom) == order(aTo) || (order(aFrom) == 2 && order(aTo) == 1)) : true;
    }

    // --- half

    namespace
    {
        std::uint16_t float_to_half_bits(double aValue)
        {
            // (round to nearest, ties to even; directly from the double so there is no double rounding)
            std::uint64_t bits;
            std::memcpy(&bits, &aValue, sizeof(bits));
            std::uint16_t const sign = static_cast<std::uint16_t>((bits >> 48) & 0x8000u);
            int const exponent = static_cast<int>((bits >> 52) & 0x7FFu);
            std::uint64_t mantissa = bits & 0xFFFFFFFFFFFFFull;
            if (exponent == 0x7FF)
            {
                if (mantissa != 0u)
                    return static_cast<std::uint16_t>(sign | 0x7E00u | (mantissa >> 42)); // (NaN: quiet, payload kept)
                return static_cast<std::uint16_t>(sign | 0x7C00u);
            }
            int e = exponent - 1023 + 15;
            if (e >= 31)
                return static_cast<std::uint16_t>(sign | 0x7C00u);
            if (e <= 0)
            {
                // subnormal (or zero)
                if (e < -10)
                    return sign;
                mantissa |= 0x10000000000000ull;
                int const shift = 42 + 1 - e;
                std::uint64_t const halfMantissa = mantissa >> shift;
                std::uint64_t const remainder = mantissa & ((std::uint64_t{ 1 } << shift) - 1u);
                std::uint64_t const halfway = std::uint64_t{ 1 } << (shift - 1);
                std::uint64_t result = halfMantissa;
                if (remainder > halfway || (remainder == halfway && (halfMantissa & 1u) != 0u))
                    ++result;
                return static_cast<std::uint16_t>(sign | result);
            }
            std::uint64_t halfMantissa = mantissa >> 42;
            std::uint64_t const remainder = mantissa & ((std::uint64_t{ 1 } << 42) - 1u);
            std::uint64_t const halfway = std::uint64_t{ 1 } << 41;
            std::uint32_t result = static_cast<std::uint32_t>((static_cast<std::uint32_t>(e) << 10) | halfMantissa);
            if (remainder > halfway || (remainder == halfway && (halfMantissa & 1u) != 0u))
                ++result; // (may carry into the exponent: correct, including to infinity)
            return static_cast<std::uint16_t>(sign | result);
        }

        float half_bits_to_float(std::uint16_t aBits)
        {
            std::uint32_t const sign = static_cast<std::uint32_t>(aBits & 0x8000u) << 16;
            std::uint32_t const exponent = (aBits >> 10) & 0x1Fu;
            std::uint32_t mantissa = aBits & 0x3FFu;
            std::uint32_t bits;
            if (exponent == 0u)
            {
                if (mantissa == 0u)
                    bits = sign;
                else
                {
                    int e = -1;
                    do { ++e; mantissa <<= 1; } while ((mantissa & 0x400u) == 0u);
                    bits = sign | (static_cast<std::uint32_t>(127 - 15 - e) << 23) | ((mantissa & 0x3FFu) << 13);
                }
            }
            else if (exponent == 31u)
                bits = sign | 0x7F800000u | (mantissa << 13);
            else
                bits = sign | ((exponent + 127u - 15u) << 23) | (mantissa << 13);
            float result;
            std::memcpy(&result, &bits, sizeof(result));
            return result;
        }
    }

    half::half(float aValue) : bits{ float_to_half_bits(static_cast<double>(aValue)) } {}
    half::half(double aValue) : bits{ float_to_half_bits(aValue) } {}
    half::operator float() const { return half_bits_to_float(bits); }

    // --- element values

    scalar_value load(unsigned char const* aPointer, dtype aType)
    {
        scalar_value result;
        result.type = aType;
        dispatch(aType, [&](auto tag)
        {
            using T = decltype(tag);
            T value;
            std::memcpy(&value, aPointer, sizeof(T));
            if constexpr (is_bool_v<T>)
                result.b = value;
            else if constexpr (std::is_integral_v<T> && std::is_signed_v<T>)
                result.i = value;
            else if constexpr (std::is_integral_v<T>)
                result.u = value;
            else if constexpr (is_complex_v<T>)
                result.c = std::complex<double>{ value.real(), value.imag() };
            else
                result.f = static_cast<double>(static_cast<float>(value));
            if constexpr (std::is_same_v<T, double>)
                result.f = value;
        });
        return result;
    }

    void store(unsigned char* aPointer, dtype aType, scalar_value const& aValue)
    {
        dispatch(aValue.type, [&](auto tag)
        {
            using T = decltype(tag);
            T value;
            if constexpr (is_bool_v<T>)
                value = aValue.b;
            else if constexpr (std::is_integral_v<T> && std::is_signed_v<T>)
                value = static_cast<T>(aValue.i);
            else if constexpr (std::is_integral_v<T>)
                value = static_cast<T>(aValue.u);
            else if constexpr (is_complex_v<T>)
                value = T{ static_cast<typename T::value_type>(aValue.c.real()), static_cast<typename T::value_type>(aValue.c.imag()) };
            else if constexpr (is_half_v<T>)
                value = half{ aValue.f };
            else
                value = static_cast<T>(aValue.f);
            write_as(aPointer, aType, value);
        });
    }

    void cast_element(unsigned char const* aFrom, dtype aFromType, unsigned char* aTo, dtype aToType)
    {
        dispatch(aFromType, [&](auto tag)
        {
            using T = decltype(tag);
            T value;
            std::memcpy(&value, aFrom, sizeof(T));
            write_as(aTo, aToType, value);
        });
    }

    // --- arrays

    std::int64_t element_count(shape_t const& aShape)
    {
        std::int64_t n = 1;
        for (auto d : aShape)
            n *= d;
        return n;
    }

    std::int64_t ndarray::size() const
    {
        return element_count(shape);
    }

    bool ndarray::c_contiguous() const
    {
        std::int64_t expected = static_cast<std::int64_t>(itemsize());
        for (std::size_t i = shape.size(); i-- > 0u;)
        {
            if (shape[i] == 0)
                return true;
            if (shape[i] != 1 && strides[i] != expected)
                return false;
            expected *= shape[i];
        }
        return true;
    }

    bool ndarray::f_contiguous() const
    {
        std::int64_t expected = static_cast<std::int64_t>(itemsize());
        for (std::size_t i = 0u; i < shape.size(); ++i)
        {
            if (shape[i] == 0)
                return true;
            if (shape[i] != 1 && strides[i] != expected)
                return false;
            expected *= shape[i];
        }
        return true;
    }

    shape_t contiguous_strides(shape_t const& aShape, std::size_t aItemsize, bool aFortran)
    {
        shape_t strides(aShape.size());
        std::int64_t stride = static_cast<std::int64_t>(aItemsize);
        if (aFortran)
            for (std::size_t i = 0u; i < aShape.size(); ++i)
            {
                strides[i] = stride;
                stride *= std::max<std::int64_t>(aShape[i], 1);
            }
        else
            for (std::size_t i = aShape.size(); i-- > 0u;)
            {
                strides[i] = stride;
                stride *= std::max<std::int64_t>(aShape[i], 1);
            }
        return strides;
    }

    namespace
    {
        void retain_array(neos_rt_object* aObject)
        {
            reinterpret_cast<ndarray*>(aObject)->references.fetch_add(1, std::memory_order_relaxed);
        }

        void release_array(neos_rt_object* aObject)
        {
            auto* const array = reinterpret_cast<ndarray*>(aObject);
            if (array->references.fetch_sub(1, std::memory_order_acq_rel) == 1)
                delete array;
        }
    }

    extern void (*const sRetainArray)(neos_rt_object*);
    extern void (*const sReleaseArray)(neos_rt_object*);
    void (*const sRetainArray)(neos_rt_object*) = retain_array;
    void (*const sReleaseArray)(neos_rt_object*) = release_array;

    ndarray* new_array(dtype aType, shape_t const& aShape, bool aFortran)
    {
        auto* const result = new ndarray{};
        result->base.type = &array_type()->base;
        result->type = aType;
        result->shape = aShape;
        result->strides = contiguous_strides(aShape, info(aType).itemsize, aFortran);
        result->data = std::make_shared<storage>(static_cast<std::size_t>(std::max<std::int64_t>(element_count(aShape), 1)) * info(aType).itemsize);
        return result;
    }

    ndarray* new_unfilled_array(dtype aType, shape_t const& aShape, bool aFortran)
    {
        auto* const result = new ndarray{};
        result->base.type = &array_type()->base;
        result->type = aType;
        result->shape = aShape;
        result->strides = contiguous_strides(aShape, info(aType).itemsize, aFortran);
        result->data = std::make_shared<storage>(static_cast<std::size_t>(std::max<std::int64_t>(element_count(aShape), 1)) * info(aType).itemsize, false);
        return result;
    }

    ndarray* new_view(ndarray* aBase, dtype aType, std::size_t aOffset, shape_t aShape, shape_t aStrides)
    {
        auto* const result = new ndarray{};
        result->base.type = &array_type()->base;
        result->type = aType;
        result->data = aBase->data;
        result->offset = aOffset;
        result->shape = std::move(aShape);
        result->strides = std::move(aStrides);
        result->writeable = aBase->writeable;
        result->view = true;
        return result;
    }

    ndarray* new_scalar(dtype aType, scalar_value const& aValue)
    {
        auto* const result = new_array(aType, {});
        result->scalar = true;
        result->base.type = &scalar_type(aType)->base;
        store(result->pointer(), aType, aValue);
        return result;
    }

    bool is_array(object* aObject)
    {
        if (aObject == nullptr)
            return false;
        if (aObject->type == &array_type()->base)
            return true;
        for (int t = 0; t < static_cast<int>(dtype::Count); ++t)
            if (aObject->type == &scalar_type(static_cast<dtype>(t))->base)
                return true;
        return false;
    }

    bool is_ndarray(object* aObject)
    {
        return aObject != nullptr && aObject->type == &array_type()->base;
    }

    bool is_numpy_scalar(object* aObject)
    {
        return is_array(aObject) && !is_ndarray(aObject);
    }

    ndarray* as_array(object* aObject)
    {
        return reinterpret_cast<ndarray*>(aObject);
    }

    object* as_object(ndarray* aArray)
    {
        return &aArray->base;
    }

    ndarray* copy_array(ndarray const& aArray, std::optional<dtype> aType, bool aFortran)
    {
        auto const t = aType.value_or(aArray.type);
        auto* const result = new_unfilled_array(t, aArray.shape, aFortran);
        iterate(aArray.shape, { aArray.pointer(), result->pointer() }, { aArray.strides, result->strides },
            [&](std::int64_t aCount, unsigned char** aPointers, std::int64_t const* aStrides)
            {
                auto from = aPointers[0];
                auto to = aPointers[1];
                if (aArray.type == t)
                {
                    auto const size = info(t).itemsize;
                    if (aStrides[0] == static_cast<std::int64_t>(size) && aStrides[1] == static_cast<std::int64_t>(size))
                        std::memcpy(to, from, static_cast<std::size_t>(aCount) * size);
                    else
                        for (std::int64_t i = 0; i < aCount; ++i, from += aStrides[0], to += aStrides[1])
                            std::memcpy(to, from, size);
                }
                else
                    for (std::int64_t i = 0; i < aCount; ++i, from += aStrides[0], to += aStrides[1])
                        cast_element(from, aArray.type, to, t);
            });
        return result;
    }

    object* to_python(ndarray const& aArray)
    {
        auto const value = load(aArray.pointer(), aArray.type);
        switch (aArray.type)
        {
        case dtype::Bool: return make_bool(value.b);
        case dtype::Int8: case dtype::Int16: case dtype::Int32: case dtype::Int64: return make_int(value.i);
        case dtype::UInt8: case dtype::UInt16: case dtype::UInt32: case dtype::UInt64: return make_int(static_cast<long long>(value.u));
        case dtype::Complex64: case dtype::Complex128: return make_complex(value.c);
        default: return make_float(value.f);
        }
    }

    object* scalar_from(ndarray const& aArray, unsigned char const* aElement)
    {
        return as_object(new_scalar(aArray.type, load(aElement, aArray.type)));
    }

    object* result_object(ndarray* aResult)
    {
        if (aResult->ndim() == 0 && !aResult->scalar)
        {
            auto const result = scalar_from(*aResult, aResult->pointer());
            release_array(&aResult->base);
            return result;
        }
        return as_object(aResult);
    }

    // --- Python objects to arrays

    namespace
    {
        struct discovery
        {
            shape_t shape;
            std::optional<dtype> type;
            bool ragged = false;
            std::size_t raggedAfter = 0u;
            bool unsupported = false;
            std::string unsupportedType;
        };

        dtype python_scalar_dtype(int aKind)
        {
            switch (aKind)
            {
            case NEOS_PY_BOOL: return dtype::Bool;
            case NEOS_PY_INT: return dtype::Int64;
            case NEOS_PY_FLOAT: return dtype::Float64;
            default: return dtype::Complex128;
            }
        }

        void discover(object* aObject, std::size_t aDepth, discovery& aResult)
        {
            auto const merge_type = [&](dtype t) { aResult.type = aResult.type ? promote(*aResult.type, t) : t; };
            std::optional<shape_t> itemShape;
            std::vector<object*> items;
            ndarray const* array = nullptr;
            if (is_array(aObject))
            {
                array = as_array(aObject);
                merge_type(array->type);
                itemShape = array->shape;
            }
            else if (is_sequence(aObject))
            {
                items = sequence_items(aObject);
                itemShape = shape_t{ static_cast<std::int64_t>(items.size()) };
            }
            else if (is_python_scalar(aObject))
            {
                merge_type(python_scalar_dtype(kind(aObject)));
                itemShape = shape_t{};
            }
            else
            {
                aResult.unsupported = true;
                aResult.unsupportedType = type_name(aObject);
                return;
            }
            // the shape at this depth must agree with that found so far
            auto const& s = *itemShape;
            if (aResult.ragged)
                return;
            if (aResult.shape.size() < aDepth + s.size() && aDepth == aResult.shape.size())
            {
                // (the first item to reach this depth decides it)
            }
            for (std::size_t i = 0u; i < s.size() && !aResult.ragged; ++i)
            {
                auto const d = aDepth + i;
                if (d < aResult.shape.size())
                {
                    if (aResult.shape[d] != s[i])
                    {
                        aResult.ragged = true;
                        aResult.raggedAfter = d;
                    }
                }
                else
                    aResult.shape.push_back(s[i]);
            }
            if (array != nullptr || items.empty())
            {
                // (a leaf at a depth shallower than that of other leaves)
                if (aDepth + s.size() < aResult.shape.size() && !aResult.ragged)
                {
                    aResult.ragged = true;
                    aResult.raggedAfter = aDepth + s.size();
                }
                return;
            }
            for (auto item : items)
            {
                discover(item, aDepth + 1u, aResult);
                if (aResult.ragged || aResult.unsupported)
                    return;
            }
        }

        // (a leaf deeper than the shape: checked when filling)
        bool fill(object* aObject, ndarray& aArray, unsigned char* aPointer, std::size_t aDepth)
        {
            if (is_array(aObject))
            {
                auto const* source = as_array(aObject);
                std::size_t const ndim = aArray.ndim() - aDepth;
                if (source->ndim() != ndim)
                    return false;
                shape_t strides{ aArray.strides.begin() + static_cast<std::ptrdiff_t>(aDepth), aArray.strides.end() };
                iterate(source->shape, { source->pointer(), aPointer }, { source->strides, strides },
                    [&](std::int64_t aCount, unsigned char** aPointers, std::int64_t const* aStrides)
                    {
                        auto from = aPointers[0];
                        auto to = aPointers[1];
                        for (std::int64_t i = 0; i < aCount; ++i, from += aStrides[0], to += aStrides[1])
                            cast_element(from, source->type, to, aArray.type);
                    });
                return true;
            }
            if (is_sequence(aObject))
            {
                if (aDepth >= aArray.ndim())
                    return false;
                auto const items = sequence_items(aObject);
                for (std::size_t i = 0u; i < items.size(); ++i)
                    if (!fill(items[i], aArray, aPointer + static_cast<std::int64_t>(i) * aArray.strides[aDepth], aDepth + 1u))
                        return false;
                return true;
            }
            if (aDepth != aArray.ndim())
                return false;
            scalar_value value;
            switch (kind(aObject))
            {
            case NEOS_PY_BOOL:
                {
                    long long i = 0;
                    ctx->as_int(ctx, aObject, &i);
                    value.type = dtype::Bool;
                    value.b = i != 0;
                }
                break;
            case NEOS_PY_INT:
                {
                    long long i = 0;
                    ctx->as_int(ctx, aObject, &i);
                    value.type = dtype::Int64;
                    value.i = i;
                    if (is_integer(aArray.type) && aArray.type != dtype::Int64 && aArray.type != dtype::UInt64)
                    {
                        // (NEP 50: a Python integer must fit)
                        auto const& n = info(aArray.type);
                        long long const bits = static_cast<long long>(n.itemsize) * 8;
                        long long const low = is_signed(aArray.type) ? -(1ll << (bits - 1)) : 0;
                        long long const high = is_signed(aArray.type) ? (1ll << (bits - 1)) - 1 : (1ll << bits) - 1;
                        if (i < low || i > high)
                        {
                            error("OverflowError", "Python integer " + std::to_string(i) + " out of bounds for " + n.name);
                            return true;
                        }
                    }
                    if (aArray.type == dtype::UInt64 && i < 0)
                    {
                        error("OverflowError", "Python integer " + std::to_string(i) + " out of bounds for uint64");
                        return true;
                    }
                }
                break;
            case NEOS_PY_FLOAT:
                value.type = dtype::Float64;
                ctx->as_float(ctx, aObject, &value.f);
                break;
            default:
                {
                    double re = 0.0, im = 0.0;
                    ctx->as_complex(ctx, aObject, &re, &im);
                    value.type = dtype::Complex128;
                    value.c = { re, im };
                }
                break;
            }
            store(aPointer, aArray.type, value);
            return true;
        }
    }

    ndarray* to_array(object* aObject, array_options const& aOptions)
    {
        ndarray* result = nullptr;
        if (is_array(aObject))
        {
            auto* const source = as_array(aObject);
            if (!aOptions.copy && (!aOptions.type || *aOptions.type == source->type) && !source->scalar)
            {
                retain_array(&source->base);
                result = source;
            }
            else
            {
                result = copy_array(*source, aOptions.type);
                result->scalar = false;
            }
        }
        else
        {
            discovery d;
            discover(aObject, 0u, d);
            if (d.unsupported)
                return error("TypeError", "unsupported array element type '" + d.unsupportedType + "'");
            if (d.ragged)
            {
                shape_t detected{ d.shape.begin(), d.shape.begin() + static_cast<std::ptrdiff_t>(d.raggedAfter) };
                return error("ValueError", "setting an array element with a sequence. The requested array has an inhomogeneous shape after " +
                    std::to_string(d.raggedAfter) + " dimensions. The detected shape was " + shape_repr(detected) + " + inhomogeneous part.");
            }
            auto const t = aOptions.type.value_or(d.type.value_or(dtype::Float64));
            result = new_array(t, d.shape);
            if (!fill(aObject, *result, result->pointer(), 0u))
            {
                release_array(&result->base);
                return error("ValueError", "setting an array element with a sequence.");
            }
            if (failed())
            {
                release_array(&result->base);
                return nullptr;
            }
        }
        if (static_cast<int>(result->ndim()) < aOptions.ndmin)
        {
            shape_t shape(static_cast<std::size_t>(aOptions.ndmin) - result->ndim(), 1);
            shape_t strides(shape.size(), 0);
            shape.insert(shape.end(), result->shape.begin(), result->shape.end());
            strides.insert(strides.end(), result->strides.begin(), result->strides.end());
            auto* const view = new_view(result, result->type, result->offset, shape, strides);
            view->view = result->view;
            release_array(&result->base);
            result = view;
        }
        return result;
    }

    bool to_operand(object* aObject, operand& aResult)
    {
        if (is_array(aObject))
        {
            aResult.holder = ref::borrowed(aObject);
            aResult.array = as_array(aObject);
            return true;
        }
        if (is_python_scalar(aObject))
        {
            aResult.weak = true;
            aResult.weakKind = kind(aObject);
        }
        auto* const array = to_array(aObject);
        if (array == nullptr)
            return false;
        aResult.holder = ref{ as_object(array) };
        aResult.array = array;
        return true;
    }

    // --- broadcasting and iteration

    bool broadcast_shapes(std::vector<shape_t const*> const& aShapes, shape_t& aResult)
    {
        std::size_t ndim = 0u;
        for (auto s : aShapes)
            ndim = std::max(ndim, s->size());
        aResult.assign(ndim, 1);
        for (auto s : aShapes)
            for (std::size_t i = 0u; i < s->size(); ++i)
            {
                auto const d = ndim - s->size() + i;
                auto const n = (*s)[i];
                if (aResult[d] == 1)
                    aResult[d] = n;
                else if (n != 1 && n != aResult[d])
                {
                    std::string message = "operands could not be broadcast together with shapes";
                    for (auto t : aShapes)
                    {
                        std::string text = shape_repr(*t);
                        // (NumPy shows (3,) as (3,) and () as ())
                        message += " " + text;
                    }
                    message += " ";
                    error("ValueError", message);
                    return false;
                }
            }
        return true;
    }

    shape_t broadcast_strides(ndarray const& aArray, shape_t const& aShape)
    {
        shape_t strides(aShape.size(), 0);
        for (std::size_t i = 0u; i < aArray.ndim(); ++i)
        {
            auto const d = aShape.size() - aArray.ndim() + i;
            strides[d] = aArray.shape[i] == 1 ? 0 : aArray.strides[i];
        }
        return strides;
    }

    void iterate(shape_t const& aShape, std::vector<unsigned char*> aPointers, std::vector<shape_t> const& aStrides,
        std::function<void(std::int64_t, unsigned char**, std::int64_t const*)> const& aInner)
    {
        auto const n = aPointers.size();
        std::vector<std::int64_t> innerStrides(n, 0);
        if (aShape.empty())
        {
            aInner(1, aPointers.data(), innerStrides.data());
            return;
        }
        for (auto d : aShape)
            if (d == 0)
                return;
        auto const ndim = aShape.size();
        for (std::size_t k = 0u; k < n; ++k)
            innerStrides[k] = aStrides[k][ndim - 1u];
        std::vector<std::int64_t> index(ndim, 0);
        std::vector<unsigned char*> pointers = aPointers;
        for (;;)
        {
            aInner(aShape[ndim - 1u], pointers.data(), innerStrides.data());
            // (the next index of the outer dimensions)
            std::size_t d = ndim - 1u;
            for (;;)
            {
                if (d == 0u)
                    return;
                --d;
                ++index[d];
                for (std::size_t k = 0u; k < n; ++k)
                    pointers[k] += aStrides[k][d];
                if (index[d] < aShape[d])
                    break;
                for (std::size_t k = 0u; k < n; ++k)
                    pointers[k] -= aStrides[k][d] * aShape[d];
                index[d] = 0;
            }
        }
    }

    // --- arguments

    arguments::arguments(char const* aFunction, object* aArgs, object* aKwargs, std::initializer_list<char const*> aNames) :
        iValues(aNames.size(), nullptr)
    {
        std::vector<std::string> names;
        std::vector<bool> keywordOnly;
        bool varargs = false;
        for (auto name : aNames)
        {
            std::string n = name;
            bool k = false;
            if (n.starts_with("*"))
            {
                k = true;
                n.erase(0, 1);
            }
            if (n == "...")
            {
                varargs = true;
                n.clear();
            }
            names.push_back(n);
            keywordOnly.push_back(k);
        }
        auto const args = aArgs != nullptr ? sequence_items(aArgs) : std::vector<object*>{};
        iPositional = args.size();
        std::size_t p = 0u;
        for (auto a : args)
        {
            while (p < names.size() && (keywordOnly[p] || names[p].empty()))
                ++p;
            if (p >= names.size())
            {
                if (varargs)
                {
                    iExtra.push_back(a);
                    continue;
                }
                std::size_t positional = 0u;
                for (std::size_t i = 0u; i < names.size(); ++i)
                    if (!keywordOnly[i] && !names[i].empty())
                        ++positional;
                error("TypeError", std::string{ aFunction } + "() takes at most " + std::to_string(positional) + " positional argument" +
                    (positional == 1u ? "" : "s") + " (" + std::to_string(args.size()) + " given)");
                iOk = false;
                return;
            }
            iValues[p++] = a;
        }
        auto const n = ctx->dict_size(ctx, aKwargs);
        for (std::size_t i = 0u; i < n; ++i)
        {
            object* key = nullptr;
            object* value = nullptr;
            ctx->dict_item(ctx, aKwargs, i, &key, &value);
            auto const name = as_str(key).value_or("");
            bool found = false;
            for (std::size_t j = 0u; j < names.size(); ++j)
                if (names[j] == name)
                {
                    if (iValues[j] != nullptr)
                    {
                        error("TypeError", std::string{ aFunction } + "() got multiple values for argument '" + name + "'");
                        iOk = false;
                        return;
                    }
                    iValues[j] = value;
                    found = true;
                }
            if (!found)
            {
                error("TypeError", std::string{ aFunction } + "() got an unexpected keyword argument '" + name + "'");
                iOk = false;
                return;
            }
        }
    }

    bool to_int(object* aObject, std::int64_t& aResult)
    {
        if (is_array(aObject))
        {
            auto const* a = as_array(aObject);
            if (a->ndim() == 0 && (is_integer(a->type) || is_bool(a->type)))
            {
                aResult = read_as<std::int64_t>(a->pointer(), a->type);
                return true;
            }
            error("TypeError", "only integer scalar arrays can be converted to a scalar index");
            return false;
        }
        long long value = 0;
        if (ctx->as_int(ctx, aObject, &value) != 0)
            return false;
        aResult = value;
        return true;
    }

    bool to_double(object* aObject, double& aResult)
    {
        if (is_array(aObject))
        {
            auto const* a = as_array(aObject);
            if (a->size() == 1)
            {
                aResult = read_as<double>(a->pointer(), a->type);
                return true;
            }
            error("TypeError", "only length-1 arrays can be converted to Python scalars");
            return false;
        }
        return ctx->as_float(ctx, aObject, &aResult) == 0;
    }

    bool to_bool(object* aObject, bool& aResult)
    {
        auto const t = ctx->truth(ctx, aObject);
        if (t < 0)
            return false;
        aResult = t == 1;
        return true;
    }

    bool to_shape(object* aObject, shape_t& aResult)
    {
        aResult.clear();
        if (is_sequence(aObject))
        {
            for (auto item : sequence_items(aObject))
            {
                std::int64_t n = 0;
                if (!to_int(item, n))
                    return false;
                aResult.push_back(n);
            }
            return true;
        }
        if (is_ndarray(aObject) && as_array(aObject)->ndim() == 1)
        {
            auto const* a = as_array(aObject);
            for (std::int64_t i = 0; i < a->shape[0]; ++i)
                aResult.push_back(read_as<std::int64_t>(a->pointer() + i * a->strides[0], a->type));
            return true;
        }
        std::int64_t n = 0;
        if (!to_int(aObject, n))
            return false;
        aResult.push_back(n);
        return true;
    }

    bool normalize_axis(std::int64_t aAxis, std::size_t aNdim, std::size_t& aResult)
    {
        auto const n = static_cast<std::int64_t>(aNdim);
        if (aAxis < -n || aAxis >= n)
        {
            error("AxisError", "axis " + std::to_string(aAxis) + " is out of bounds for array of dimension " + std::to_string(aNdim));
            return false;
        }
        aResult = static_cast<std::size_t>(aAxis < 0 ? aAxis + n : aAxis);
        return true;
    }

    bool to_axes(object* aObject, std::size_t aNdim, std::vector<std::size_t>& aResult, bool aAllowDuplicates)
    {
        aResult.clear();
        if (aObject == nullptr || kind(aObject) == NEOS_PY_NONE)
        {
            for (std::size_t i = 0u; i < aNdim; ++i)
                aResult.push_back(i);
            return true;
        }
        shape_t axes;
        if (!to_shape(aObject, axes))
            return false;
        for (auto a : axes)
        {
            std::size_t axis = 0u;
            if (!normalize_axis(a, aNdim, axis))
                return false;
            if (!aAllowDuplicates && std::find(aResult.begin(), aResult.end(), axis) != aResult.end())
            {
                error("ValueError", "duplicate value in 'axis'");
                return false;
            }
            aResult.push_back(axis);
        }
        return true;
    }
}
