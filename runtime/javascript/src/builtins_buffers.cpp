/*
  builtins_buffers.cpp

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

// ArrayBuffer, SharedArrayBuffer and DataView; the conversions between numeric values and the bytes of elements

#include <algorithm>
#include <new>
#include "js.hpp"
#include "api.hpp"
#include "typed_arrays.hpp"

namespace neos::javascript_runtime
{
    namespace
    {
        constexpr double MaxDataBlock = 4294967296.0 * 2.0;     ///< (the greatest data block that is allocated (8 GiB): CreateByteDataBlock throws a RangeError beyond it)

        template <typename T>
        T load(std::uint8_t const* aBytes, bool aLittleEndian)
        {
            std::uint8_t bytes[sizeof(T)];
            std::memcpy(bytes, aBytes, sizeof(T));
            if (aLittleEndian != (std::endian::native == std::endian::little))
                std::reverse(std::begin(bytes), std::end(bytes));
            T result;
            std::memcpy(&result, bytes, sizeof(T));
            return result;
        }

        template <typename T>
        void save(T aValue, std::uint8_t* aBytes, bool aLittleEndian)
        {
            std::uint8_t bytes[sizeof(T)];
            std::memcpy(bytes, &aValue, sizeof(T));
            if (aLittleEndian != (std::endian::native == std::endian::little))
                std::reverse(std::begin(bytes), std::end(bytes));
            std::memcpy(aBytes, bytes, sizeof(T));
        }

        // (round to nearest, ties to even: aValue is not negative)
        double round_half_even(double aValue)
        {
            auto const f = std::floor(aValue);
            auto const difference = aValue - f;
            if (difference > 0.5)
                return f + 1.0;
            if (difference < 0.5)
                return f;
            return std::fmod(f, 2.0) == 0.0 ? f : f + 1.0;
        }
    }

    char const* element_type_name(element_type aType)
    {
        static char const* const sNames[ElementTypes] = { "Int8Array", "Uint8Array", "Uint8ClampedArray", "Int16Array", "Uint16Array",
            "Int32Array", "Uint32Array", "BigInt64Array", "BigUint64Array", "Float16Array", "Float32Array", "Float64Array" };
        return sNames[static_cast<std::size_t>(aType)];
    }

    // --- conversions

    std::uint16_t double_to_float16(double aValue)
    {
        std::uint16_t const sign = std::signbit(aValue) ? 0x8000u : 0u;
        if (std::isnan(aValue))
            return 0x7E00u;
        auto const magnitude = std::fabs(aValue);
        // (65520 is halfway between the greatest finite binary16 value (65504) and 2^16: it rounds (to even) to infinity)
        if (magnitude >= 65520.0)
            return static_cast<std::uint16_t>(sign | 0x7C00u);
        if (magnitude == 0.0)
            return sign;
        int e = 0;
        std::frexp(magnitude, &e);
        int exponent = std::max(e - 1, -14);
        // (the significand in units of the exponent's least significant bit: scaling by a power of two is exact)
        auto significand = round_half_even(std::ldexp(magnitude, 10 - exponent));
        if (significand >= 2048.0)
        {
            significand /= 2.0;
            ++exponent;
        }
        auto const bits = static_cast<std::uint16_t>(significand);
        if (bits < 1024u)
            return static_cast<std::uint16_t>(sign | bits);     // (subnormal)
        return static_cast<std::uint16_t>(sign | ((exponent + 15) << 10) | (bits - 1024u));
    }

    double float16_to_double(std::uint16_t aBits)
    {
        auto const exponent = (aBits >> 10) & 0x1Fu;
        auto const fraction = aBits & 0x3FFu;
        double result;
        if (exponent == 0u)
            result = std::ldexp(static_cast<double>(fraction), -24);
        else if (exponent == 31u)
            result = fraction != 0u ? std::numeric_limits<double>::quiet_NaN() : std::numeric_limits<double>::infinity();
        else
            result = std::ldexp(static_cast<double>(fraction + 1024u), static_cast<int>(exponent) - 25);
        return (aBits & 0x8000u) != 0u ? -result : result;
    }

    value element_value(element_type aType, std::uint8_t const* aBytes, bool aLittleEndian)
    {
        switch (aType)
        {
        case element_type::Int8:
            return number(static_cast<std::int8_t>(aBytes[0]));
        case element_type::Uint8:
        case element_type::Uint8Clamped:
            return number(aBytes[0]);
        case element_type::Int16:
            return number(load<std::int16_t>(aBytes, aLittleEndian));
        case element_type::Uint16:
            return number(load<std::uint16_t>(aBytes, aLittleEndian));
        case element_type::Int32:
            return number(load<std::int32_t>(aBytes, aLittleEndian));
        case element_type::Uint32:
            return number(load<std::uint32_t>(aBytes, aLittleEndian));
        case element_type::BigInt64:
            return make_bigint(load<std::int64_t>(aBytes, aLittleEndian));
        case element_type::BigUint64:
            return make_bigint_u64(load<std::uint64_t>(aBytes, aLittleEndian));
        case element_type::Float16:
            return number(float16_to_double(load<std::uint16_t>(aBytes, aLittleEndian)));
        case element_type::Float32:
            return number(static_cast<double>(load<float>(aBytes, aLittleEndian)));
        case element_type::Float64:
            return number(load<double>(aBytes, aLittleEndian));
        }
        return Undefined;
    }

    void store_element(element_type aType, value aNumeric, std::uint8_t* aBytes, bool aLittleEndian)
    {
        switch (aType)
        {
        case element_type::Int8:
        case element_type::Uint8:
            aBytes[0] = static_cast<std::uint8_t>(to_uint32(as_number(aNumeric)));
            break;
        case element_type::Uint8Clamped:
            {
                auto const n = as_number(aNumeric);
                if (!(n > 0.0))
                    aBytes[0] = 0u;
                else if (n >= 255.0)
                    aBytes[0] = 255u;
                else
                    aBytes[0] = static_cast<std::uint8_t>(round_half_even(n));
            }
            break;
        case element_type::Int16:
        case element_type::Uint16:
            save(static_cast<std::uint16_t>(to_uint32(as_number(aNumeric))), aBytes, aLittleEndian);
            break;
        case element_type::Int32:
        case element_type::Uint32:
            save(to_uint32(as_number(aNumeric)), aBytes, aLittleEndian);
            break;
        case element_type::BigInt64:
            save(bigint_to_int64(aNumeric), aBytes, aLittleEndian);
            break;
        case element_type::BigUint64:
            save(bigint_to_uint64(aNumeric), aBytes, aLittleEndian);
            break;
        case element_type::Float16:
            save(double_to_float16(as_number(aNumeric)), aBytes, aLittleEndian);
            break;
        case element_type::Float32:
            save(static_cast<float>(as_number(aNumeric)), aBytes, aLittleEndian);
            break;
        case element_type::Float64:
            save(as_number(aNumeric), aBytes, aLittleEndian);
            break;
        }
    }

    value to_element_numeric(element_type aType, value aValue)
    {
        if (is_bigint_type(aType))
            return to_bigint(aValue);
        if (is_number(aValue))
            return aValue;
        return number(to_number(aValue));
    }

    // --- array buffers

    value allocate_array_buffer(value aConstructor, double aByteLength, std::optional<double> aMaxByteLength, bool aShared)
    {
        if (aMaxByteLength && aByteLength > *aMaxByteLength)
            throw_range_error("Invalid array buffer length: it is greater than maxByteLength");
        auto const prototype = get_prototype_from_constructor(aConstructor, aShared ? &realm::sharedArrayBufferPrototype : &realm::arrayBufferPrototype);
        if (aByteLength > MaxDataBlock || (aMaxByteLength && *aMaxByteLength > MaxDataBlock))
            throw_range_error("Array buffer allocation failed");
        auto& b = the_heap().make<array_buffer_object>(prototype, aShared);
        try
        {
            b.data.resize(static_cast<std::size_t>(aByteLength));
        }
        catch (std::bad_alloc const&)
        {
            throw_range_error("Array buffer allocation failed");
        }
        if (aMaxByteLength)
        {
            b.resizable = true;
            b.maxByteLength = static_cast<std::size_t>(*aMaxByteLength);
        }
        return value_of(b);
    }

    value clone_array_buffer(array_buffer_object& aSource, std::size_t aByteOffset, std::size_t aByteLength)
    {
        auto const result = allocate_array_buffer(the_realm().arrayBuffer, static_cast<double>(aByteLength));
        auto& target = static_cast<array_buffer_object&>(obj(result));
        if (aByteLength != 0u)
            std::memcpy(target.bytes(), aSource.bytes() + aByteOffset, aByteLength);
        return result;
    }

    void detach_array_buffer(value aBuffer, value aKey)
    {
        auto* b = as_array_buffer(aBuffer);
        if (b == nullptr || b->shared())
            throw_type_error("not an ArrayBuffer");
        if (!same_value(b->detachKey, aKey))
            throw_type_error("ArrayBuffer detach key mismatch");
        b->data.clear();
        b->data.shrink_to_fit();
        b->detached = true;
    }

    namespace
    {
        array_buffer_object& this_buffer(value aThis, bool aShared, char const* aMethod)
        {
            auto* b = as_array_buffer(aThis);
            if (b == nullptr || b->shared() != aShared)
                throw_type_error(std::string{ "Method " } + (aShared ? "SharedArrayBuffer" : "ArrayBuffer") + ".prototype." + aMethod +
                    " called on incompatible receiver " + describe(aThis));
            return *b;
        }

        // (GetArrayBufferMaxByteLengthOption)
        std::optional<double> max_byte_length_option(value aOptions)
        {
            if (!is_object(aOptions))
                return {};
            auto const maxByteLength = get(aOptions, ascii("maxByteLength"));
            if (maxByteLength == Undefined)
                return {};
            return to_index(maxByteLength);
        }

        double relative(value aValue, double aLength, double aDefault)
        {
            if (aValue == Undefined)
                return aDefault;
            auto const r = to_integer_or_infinity(aValue);
            if (r < 0.0)
                return std::max(aLength + r, 0.0);
            return std::min(r, aLength);
        }

        value species_getter(value aThis, arguments, value, function_object&)
        {
            return aThis;
        }

        // --- ArrayBuffer

        value array_buffer_constructor(value, arguments aArguments, value aNewTarget, function_object&)
        {
            if (aNewTarget == Undefined)
                throw_type_error("Constructor ArrayBuffer requires 'new'");
            auto const byteLength = to_index(argument(aArguments, 0u));
            auto const maxByteLength = max_byte_length_option(argument(aArguments, 1u));
            return allocate_array_buffer(aNewTarget, byteLength, maxByteLength);
        }

        value array_buffer_is_view(value, arguments aArguments, value, function_object&)
        {
            auto const arg = argument(aArguments, 0u);
            if (!is_object(arg))
                return False;
            auto const c = obj(arg).cls;
            return boolean(c == object_class::TypedArray || c == object_class::DataView);
        }

        value array_buffer_byte_length(value aThis, arguments, value, function_object&)
        {
            return number(static_cast<double>(this_buffer(aThis, false, "byteLength").byte_length()));
        }

        value array_buffer_max_byte_length(value aThis, arguments, value, function_object&)
        {
            auto& b = this_buffer(aThis, false, "maxByteLength");
            if (b.detached)
                return number(0.0);
            return number(static_cast<double>(b.resizable ? b.maxByteLength : b.byte_length()));
        }

        value array_buffer_resizable(value aThis, arguments, value, function_object&)
        {
            return boolean(this_buffer(aThis, false, "resizable").resizable);
        }

        value array_buffer_detached(value aThis, arguments, value, function_object&)
        {
            return boolean(this_buffer(aThis, false, "detached").detached);
        }

        value array_buffer_resize(value aThis, arguments aArguments, value, function_object&)
        {
            auto& b = this_buffer(aThis, false, "resize");
            if (!b.resizable)
                throw_type_error("Method ArrayBuffer.prototype.resize called on a fixed length ArrayBuffer");
            auto const newByteLength = to_index(argument(aArguments, 0u));
            if (b.detached)
                throw_type_error("Cannot resize a detached ArrayBuffer");
            if (newByteLength > static_cast<double>(b.maxByteLength))
                throw_range_error("Invalid length: it is greater than maxByteLength");
            try
            {
                b.data.resize(static_cast<std::size_t>(newByteLength), 0u);
            }
            catch (std::bad_alloc const&)
            {
                throw_range_error("Array buffer allocation failed");
            }
            return Undefined;
        }

        value array_buffer_slice(value aThis, arguments aArguments, value, function_object&)
        {
            auto& b = this_buffer(aThis, false, "slice");
            if (b.detached)
                throw_type_error("Cannot perform ArrayBuffer.prototype.slice on a detached ArrayBuffer");
            auto const len = static_cast<double>(b.byte_length());
            auto const first = relative(argument(aArguments, 0u), len, 0.0);
            auto const final = relative(argument(aArguments, 1u), len, len);
            auto const newLen = std::max(final - first, 0.0);
            auto const ctor = species_constructor(aThis, the_realm().arrayBuffer);
            value const lengthArgument[] = { number(newLen) };
            auto const result = construct(ctor, lengthArgument);
            auto* n = as_array_buffer(result);
            if (n == nullptr || n->shared())
                throw_type_error("ArrayBuffer subclass returned this from species constructor");
            if (n->detached)
                throw_type_error("species constructor returned a detached ArrayBuffer");
            if (result == aThis)
                throw_type_error("ArrayBuffer subclass returned this from species constructor");
            if (static_cast<double>(n->byte_length()) < newLen)
                throw_type_error("species constructor returned a too small ArrayBuffer");
            if (b.detached)
                throw_type_error("Cannot perform ArrayBuffer.prototype.slice on a detached ArrayBuffer");
            auto const currentLen = static_cast<double>(b.byte_length());
            if (first < currentLen)
            {
                auto const count = static_cast<std::size_t>(std::min(newLen, currentLen - first));
                if (count != 0u)
                    std::memmove(n->bytes(), b.bytes() + static_cast<std::size_t>(first), count);
            }
            return result;
        }

        // (ArrayBufferCopyAndDetach)
        value array_buffer_copy_and_detach(value aThis, value aNewLength, bool aPreserveResizability, char const* aMethod)
        {
            auto& b = this_buffer(aThis, false, aMethod);
            double newByteLength;
            if (aNewLength == Undefined)
                newByteLength = static_cast<double>(b.byte_length());
            else
                newByteLength = to_index(aNewLength);
            if (b.detached)
                throw_type_error(std::string{ "Cannot perform ArrayBuffer.prototype." } + aMethod + " on a detached ArrayBuffer");
            std::optional<double> newMaxByteLength;
            if (aPreserveResizability && b.resizable)
                newMaxByteLength = static_cast<double>(b.maxByteLength);
            if (b.detachKey != Undefined)
                throw_type_error("Cannot transfer a non-detachable ArrayBuffer");
            auto const result = allocate_array_buffer(the_realm().arrayBuffer, newByteLength, newMaxByteLength);
            auto& n = static_cast<array_buffer_object&>(obj(result));
            auto const copyLength = std::min(n.byte_length(), b.byte_length());
            if (copyLength != 0u)
                std::memcpy(n.bytes(), b.bytes(), copyLength);
            detach_array_buffer(aThis, Undefined);
            return result;
        }

        value array_buffer_transfer(value aThis, arguments aArguments, value, function_object&)
        {
            return array_buffer_copy_and_detach(aThis, argument(aArguments, 0u), true, "transfer");
        }

        value array_buffer_transfer_to_fixed_length(value aThis, arguments aArguments, value, function_object&)
        {
            return array_buffer_copy_and_detach(aThis, argument(aArguments, 0u), false, "transferToFixedLength");
        }

        // --- SharedArrayBuffer

        value shared_array_buffer_constructor(value, arguments aArguments, value aNewTarget, function_object&)
        {
            if (aNewTarget == Undefined)
                throw_type_error("Constructor SharedArrayBuffer requires 'new'");
            auto const byteLength = to_index(argument(aArguments, 0u));
            auto const maxByteLength = max_byte_length_option(argument(aArguments, 1u));
            return allocate_array_buffer(aNewTarget, byteLength, maxByteLength, true);
        }

        value shared_array_buffer_byte_length(value aThis, arguments, value, function_object&)
        {
            return number(static_cast<double>(this_buffer(aThis, true, "byteLength").byte_length()));
        }

        value shared_array_buffer_growable(value aThis, arguments, value, function_object&)
        {
            return boolean(this_buffer(aThis, true, "growable").resizable);
        }

        value shared_array_buffer_max_byte_length(value aThis, arguments, value, function_object&)
        {
            auto& b = this_buffer(aThis, true, "maxByteLength");
            return number(static_cast<double>(b.resizable ? b.maxByteLength : b.byte_length()));
        }

        value shared_array_buffer_grow(value aThis, arguments aArguments, value, function_object&)
        {
            auto& b = this_buffer(aThis, true, "grow");
            if (!b.resizable)
                throw_type_error("Method SharedArrayBuffer.prototype.grow called on a fixed length SharedArrayBuffer");
            auto const newByteLength = to_index(argument(aArguments, 0u));
            if (newByteLength > static_cast<double>(b.maxByteLength))
                throw_range_error("Invalid length: it is greater than maxByteLength");
            if (newByteLength < static_cast<double>(b.byte_length()))
                throw_range_error("Invalid length: a SharedArrayBuffer cannot shrink");
            try
            {
                b.data.resize(static_cast<std::size_t>(newByteLength), 0u);
            }
            catch (std::bad_alloc const&)
            {
                throw_range_error("Array buffer allocation failed");
            }
            return Undefined;
        }

        value shared_array_buffer_slice(value aThis, arguments aArguments, value, function_object&)
        {
            auto& b = this_buffer(aThis, true, "slice");
            auto const len = static_cast<double>(b.byte_length());
            auto const first = relative(argument(aArguments, 0u), len, 0.0);
            auto const final = relative(argument(aArguments, 1u), len, len);
            auto const newLen = std::max(final - first, 0.0);
            auto const ctor = species_constructor(aThis, the_realm().sharedArrayBuffer);
            value const lengthArgument[] = { number(newLen) };
            auto const result = construct(ctor, lengthArgument);
            auto* n = as_array_buffer(result);
            if (n == nullptr || !n->shared())
                throw_type_error("species constructor did not return a SharedArrayBuffer");
            if (n == &b)
                throw_type_error("SharedArrayBuffer subclass returned this from species constructor");
            if (static_cast<double>(n->byte_length()) < newLen)
                throw_type_error("species constructor returned a too small SharedArrayBuffer");
            auto const count = static_cast<std::size_t>(newLen);
            if (count != 0u)
                std::memmove(n->bytes(), b.bytes() + static_cast<std::size_t>(first), count);
            return result;
        }

        // --- DataView

        data_view_object& this_view(value aThis, char const* aMethod)
        {
            if (!is_object(aThis) || obj(aThis).cls != object_class::DataView)
                throw_type_error(std::string{ "Method DataView.prototype." } + aMethod + " called on incompatible receiver " + describe(aThis));
            return static_cast<data_view_object&>(obj(aThis));
        }

        // (IsViewOutOfBounds)
        bool view_out_of_bounds(data_view_object const& aView)
        {
            auto const& b = aView.buffer_object();
            if (b.detached)
                return true;
            auto const bufferByteLength = b.byte_length();
            if (aView.byteOffset > bufferByteLength)
                return true;
            return !aView.lengthTracking && aView.byteOffset + aView.byteLength > bufferByteLength;
        }

        // (GetViewByteLength)
        std::size_t view_byte_length(data_view_object const& aView)
        {
            if (!aView.lengthTracking)
                return aView.byteLength;
            return aView.buffer_object().byte_length() - aView.byteOffset;
        }

        value data_view_constructor(value, arguments aArguments, value aNewTarget, function_object&)
        {
            if (aNewTarget == Undefined)
                throw_type_error("Constructor DataView requires 'new'");
            auto const buffer = argument(aArguments, 0u);
            auto* b = as_array_buffer(buffer);
            if (b == nullptr)
                throw_type_error("First argument to DataView constructor must be an ArrayBuffer");
            auto const offset = to_index(argument(aArguments, 1u));
            if (b->detached)
                throw_type_error("Cannot construct a DataView on a detached ArrayBuffer");
            auto bufferByteLength = static_cast<double>(b->byte_length());
            if (offset > bufferByteLength)
                throw_range_error("Start offset " + utf8(number_to_string(offset)) + " is outside the bounds of the buffer");
            bool const bufferIsFixedLength = is_fixed_length(*b);
            auto const byteLength = argument(aArguments, 2u);
            double viewByteLength = 0.0;
            bool lengthTracking = false;
            if (byteLength == Undefined)
            {
                if (bufferIsFixedLength)
                    viewByteLength = bufferByteLength - offset;
                else
                    lengthTracking = true;
            }
            else
            {
                viewByteLength = to_index(byteLength);
                if (offset + viewByteLength > bufferByteLength)
                    throw_range_error("Invalid DataView length " + utf8(number_to_string(viewByteLength)));
            }
            auto const prototype = get_prototype_from_constructor(aNewTarget, &realm::dataViewPrototype);
            if (b->detached)
                throw_type_error("Cannot construct a DataView on a detached ArrayBuffer");
            bufferByteLength = static_cast<double>(b->byte_length());
            if (offset > bufferByteLength)
                throw_range_error("Start offset " + utf8(number_to_string(offset)) + " is outside the bounds of the buffer");
            if (byteLength != Undefined && offset + viewByteLength > bufferByteLength)
                throw_range_error("Invalid DataView length " + utf8(number_to_string(viewByteLength)));
            auto& v = the_heap().make<data_view_object>(prototype);
            v.buffer = buffer;
            v.byteOffset = static_cast<std::size_t>(offset);
            v.byteLength = static_cast<std::size_t>(viewByteLength);
            v.lengthTracking = lengthTracking;
            return value_of(v);
        }

        value data_view_buffer(value aThis, arguments, value, function_object&)
        {
            return this_view(aThis, "buffer").buffer;
        }

        value data_view_byte_length(value aThis, arguments, value, function_object&)
        {
            auto& v = this_view(aThis, "byteLength");
            if (view_out_of_bounds(v))
                throw_type_error("DataView is out of bounds");
            return number(static_cast<double>(view_byte_length(v)));
        }

        value data_view_byte_offset(value aThis, arguments, value, function_object&)
        {
            auto& v = this_view(aThis, "byteOffset");
            if (view_out_of_bounds(v))
                throw_type_error("DataView is out of bounds");
            return number(static_cast<double>(v.byteOffset));
        }

        // (GetViewValue: the callee's nativeId is the element type)
        value data_view_get(value aThis, arguments aArguments, value, function_object& aCallee)
        {
            auto const type = static_cast<element_type>(aCallee.nativeId);
            auto& v = this_view(aThis, "get");
            auto const getIndex = to_index(argument(aArguments, 0u));
            bool const littleEndian = to_boolean(argument(aArguments, 1u));
            if (view_out_of_bounds(v))
                throw_type_error("DataView is out of bounds");
            auto const viewSize = static_cast<double>(view_byte_length(v));
            auto const size = element_size(type);
            if (getIndex + static_cast<double>(size) > viewSize)
                throw_range_error("Offset is outside the bounds of the DataView");
            auto const bufferIndex = static_cast<std::size_t>(getIndex) + v.byteOffset;
            return element_value(type, v.buffer_object().bytes() + bufferIndex, littleEndian);
        }

        // (SetViewValue)
        value data_view_set(value aThis, arguments aArguments, value, function_object& aCallee)
        {
            auto const type = static_cast<element_type>(aCallee.nativeId);
            auto& v = this_view(aThis, "set");
            auto const getIndex = to_index(argument(aArguments, 0u));
            auto const numeric = to_element_numeric(type, argument(aArguments, 1u));
            bool const littleEndian = to_boolean(argument(aArguments, 2u));
            if (view_out_of_bounds(v))
                throw_type_error("DataView is out of bounds");
            auto const viewSize = static_cast<double>(view_byte_length(v));
            auto const size = element_size(type);
            if (getIndex + static_cast<double>(size) > viewSize)
                throw_range_error("Offset is outside the bounds of the DataView");
            auto const bufferIndex = static_cast<std::size_t>(getIndex) + v.byteOffset;
            store_element(type, numeric, v.buffer_object().bytes() + bufferIndex, littleEndian);
            return Undefined;
        }

        value make_constructor(realm& aRealm, char const* aName, std::uint32_t aLength, native_function aFunction, value aPrototype)
        {
            auto const c = make_native(aName, aLength, aFunction, true);
            obj(c).put(the_names().prototype, aPrototype, 0u);
            obj(aPrototype).put(the_names().constructor, c);
            obj(aPrototype).put(aRealm.symToStringTag, ascii(aName), AttributeConfigurable);
            obj(aRealm.global).put(ascii(aName), c);
            return c;
        }
    }

    void init_buffers(realm& aRealm)
    {
        // ArrayBuffer
        aRealm.arrayBufferPrototype = make_object();
        auto const ab = make_constructor(aRealm, "ArrayBuffer", 1u, array_buffer_constructor, aRealm.arrayBufferPrototype);
        aRealm.arrayBuffer = ab;
        method(ab, "isView", 1u, array_buffer_is_view);
        getter(ab, aRealm.symSpecies, species_getter);
        auto const abp = aRealm.arrayBufferPrototype;
        getter(abp, ascii("byteLength"), array_buffer_byte_length);
        getter(abp, ascii("detached"), array_buffer_detached);
        getter(abp, ascii("maxByteLength"), array_buffer_max_byte_length);
        getter(abp, ascii("resizable"), array_buffer_resizable);
        method(abp, "resize", 1u, array_buffer_resize);
        method(abp, "slice", 2u, array_buffer_slice);
        method(abp, "transfer", 0u, array_buffer_transfer);
        method(abp, "transferToFixedLength", 0u, array_buffer_transfer_to_fixed_length);
        // SharedArrayBuffer
        aRealm.sharedArrayBufferPrototype = make_object();
        auto const sab = make_constructor(aRealm, "SharedArrayBuffer", 1u, shared_array_buffer_constructor, aRealm.sharedArrayBufferPrototype);
        aRealm.sharedArrayBuffer = sab;
        getter(sab, aRealm.symSpecies, species_getter);
        auto const sabp = aRealm.sharedArrayBufferPrototype;
        getter(sabp, ascii("byteLength"), shared_array_buffer_byte_length);
        method(sabp, "grow", 1u, shared_array_buffer_grow);
        getter(sabp, ascii("growable"), shared_array_buffer_growable);
        getter(sabp, ascii("maxByteLength"), shared_array_buffer_max_byte_length);
        method(sabp, "slice", 2u, shared_array_buffer_slice);
        // DataView
        aRealm.dataViewPrototype = make_object();
        make_constructor(aRealm, "DataView", 1u, data_view_constructor, aRealm.dataViewPrototype);
        auto const dvp = aRealm.dataViewPrototype;
        getter(dvp, ascii("buffer"), data_view_buffer);
        getter(dvp, ascii("byteLength"), data_view_byte_length);
        getter(dvp, ascii("byteOffset"), data_view_byte_offset);
        struct accessor { char const* name; element_type type; };
        static accessor const sAccessors[] = {
            { "BigInt64", element_type::BigInt64 }, { "BigUint64", element_type::BigUint64 }, { "Float16", element_type::Float16 },
            { "Float32", element_type::Float32 }, { "Float64", element_type::Float64 }, { "Int8", element_type::Int8 },
            { "Int16", element_type::Int16 }, { "Int32", element_type::Int32 }, { "Uint8", element_type::Uint8 },
            { "Uint16", element_type::Uint16 }, { "Uint32", element_type::Uint32 } };
        for (auto const& a : sAccessors)
            as_function(method(dvp, (std::string{ "get" } + a.name).c_str(), 1u, data_view_get))->nativeId = static_cast<std::uint32_t>(a.type);
        for (auto const& a : sAccessors)
            as_function(method(dvp, (std::string{ "set" } + a.name).c_str(), 2u, data_view_set))->nativeId = static_cast<std::uint32_t>(a.type);
    }
}
