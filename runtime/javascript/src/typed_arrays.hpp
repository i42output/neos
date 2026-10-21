/*
  typed_arrays.hpp

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

// Array buffers, typed arrays and data views: the objects and the operations on their data that builtins_buffers.cpp,
// builtins_typed_arrays.cpp and builtins_atomics.cpp share

#pragma once

#include "js.hpp"

namespace neos::javascript_runtime
{
    // (the element types of typed arrays, in the order of the realm's typedArrayPrototypes and typedArrayConstructors (ECMAScript's
    // table of the TypedArray constructors))
    enum class element_type : std::uint8_t
    {
        Int8,
        Uint8,
        Uint8Clamped,
        Int16,
        Uint16,
        Int32,
        Uint32,
        BigInt64,
        BigUint64,
        Float16,
        Float32,
        Float64
    };

    inline constexpr std::size_t ElementTypes = 12u;

    inline std::size_t element_size(element_type aType)
    {
        static constexpr std::uint8_t sSizes[ElementTypes] = { 1u, 1u, 1u, 2u, 2u, 4u, 4u, 8u, 8u, 2u, 4u, 8u };
        return sSizes[static_cast<std::size_t>(aType)];
    }

    inline bool is_bigint_type(element_type aType)
    {
        return aType == element_type::BigInt64 || aType == element_type::BigUint64;
    }

    char const* element_type_name(element_type aType);     ///< (its typed array's name, e.g. "Int8Array")

    // (an ArrayBuffer or a SharedArrayBuffer: its data block is a vector of bytes; a resizable (growable) one's data is resized)
    class array_buffer_object : public object
    {
    public:
        array_buffer_object(value aPrototype, bool aShared) :
            object{ aPrototype, aShared ? object_class::SharedArrayBuffer : object_class::ArrayBuffer } {}
    public:
        bool shared() const { return cls == object_class::SharedArrayBuffer; }
        std::size_t byte_length() const { return data.size(); }
        std::uint8_t* bytes() { return data.data(); }
    public:
        std::vector<std::uint8_t> data;
        bool detached = false;
        bool resizable = false;             ///< (a resizable ArrayBuffer or a growable SharedArrayBuffer)
        std::size_t maxByteLength = 0u;     ///< ([[ArrayBufferMaxByteLength]] (resizable))
        value detachKey = Undefined;        ///< ([[ArrayBufferDetachKey]])
    };

    inline array_buffer_object* as_array_buffer(value aValue)
    {
        if (!is_object(aValue))
            return nullptr;
        auto& o = obj(aValue);
        return o.cls == object_class::ArrayBuffer || o.cls == object_class::SharedArrayBuffer ? static_cast<array_buffer_object*>(&o) : nullptr;
    }

    inline bool is_fixed_length(array_buffer_object const& aBuffer)
    {
        return !aBuffer.resizable;
    }

    // (AllocateArrayBuffer / AllocateSharedArrayBuffer: aMaxByteLength none for a fixed length buffer)
    value allocate_array_buffer(value aConstructor, double aByteLength, std::optional<double> aMaxByteLength = {}, bool aShared = false);
    // (CloneArrayBuffer: a new %ArrayBuffer% with a copy of the bytes)
    value clone_array_buffer(array_buffer_object& aSource, std::size_t aByteOffset, std::size_t aByteLength);

    // (a typed array: an integer-indexed exotic object)
    class typed_array_object : public object
    {
    public:
        typed_array_object(value aPrototype, element_type aType) : object{ aPrototype, object_class::TypedArray }, type{ aType } {}
    public:
        std::optional<property_descriptor> get_own_property(value aKey) override;
        bool define_own_property(value aKey, property_descriptor const& aDescriptor) override;
        bool has_property(value aKey) override;
        value get(value aKey, value aReceiver) override;
        bool set(value aKey, value aValue, value aReceiver) override;
        bool delete_property(value aKey) override;
        std::vector<value> own_property_keys() override;
        bool prevent_extensions() override;
    public:
        array_buffer_object& buffer_object() const { return static_cast<array_buffer_object&>(obj(buffer)); }
        std::size_t element_size() const { return javascript_runtime::element_size(type); }
        bool bigint() const { return is_bigint_type(type); }
        // (IsTypedArrayOutOfBounds (a detached buffer's views are out of bounds))
        bool out_of_bounds() const
        {
            auto const& b = buffer_object();
            if (b.detached)
                return true;
            auto const bufferByteLength = b.data.size();
            if (byteOffset > bufferByteLength)
                return true;
            return !lengthTracking && byteOffset + arrayLength * element_size() > bufferByteLength;
        }
        // (TypedArrayLength (0 if it is out of bounds))
        std::size_t length() const
        {
            if (out_of_bounds())
                return 0u;
            if (!lengthTracking)
                return arrayLength;
            return (buffer_object().data.size() - byteOffset) / element_size();
        }
        std::size_t byte_length() const { return length() * element_size(); }
        // (IsValidIntegerIndex)
        bool valid_index(double aIndex) const
        {
            if (!(aIndex >= 0.0) || std::trunc(aIndex) != aIndex || (aIndex == 0.0 && std::signbit(aIndex)))
                return false;
            return aIndex < static_cast<double>(length());
        }
        std::uint8_t* element(std::size_t aIndex) const { return buffer_object().bytes() + byteOffset + aIndex * element_size(); }
        // (IsTypedArrayFixedLength)
        bool fixed_length() const
        {
            if (lengthTracking)
                return false;
            auto const& b = buffer_object();
            return is_fixed_length(b) || b.shared();
        }
    public:
        element_type type;
        value buffer = Undefined;           ///< ([[ViewedArrayBuffer]])
        std::size_t byteOffset = 0u;
        std::size_t arrayLength = 0u;       ///< ([[ArrayLength]], unless it tracks its buffer's length (auto))
        bool lengthTracking = false;
    };

    inline typed_array_object* as_typed_array(value aValue)
    {
        if (!is_object(aValue))
            return nullptr;
        auto& o = obj(aValue);
        return o.cls == object_class::TypedArray ? static_cast<typed_array_object*>(&o) : nullptr;
    }

    // (ValidateTypedArray: a TypeError if it is not a typed array or it is out of bounds)
    typed_array_object& validate_typed_array(value aValue);
    // (TypedArrayGetElement: undefined if the index is not a valid integer index)
    value typed_array_get_element(typed_array_object const& aTypedArray, double aIndex);
    // (TypedArraySetElement: the value is converted (ToNumber or ToBigInt), then stored if the index is a valid integer index)
    void typed_array_set_element(typed_array_object& aTypedArray, double aIndex, value aValue);
    // (the element of a typed array that a Number property key (ToString(aKey) is a canonical numeric string) refers to: compiled
    // code's ta[i])
    inline value typed_array_get_number_key(typed_array_object const& aTypedArray, double aKey)
    {
        return typed_array_get_element(aTypedArray, aKey == 0.0 ? 0.0 : aKey);
    }
    inline void typed_array_set_number_key(typed_array_object& aTypedArray, double aKey, value aValue)
    {
        typed_array_set_element(aTypedArray, aKey == 0.0 ? 0.0 : aKey, aValue);
    }

    void init_buffers(realm& aRealm);       ///< (builtins_buffers.cpp: ArrayBuffer, SharedArrayBuffer and DataView)
    void init_atomics(realm& aRealm);       ///< (builtins_atomics.cpp)

    // (a DataView)
    class data_view_object : public object
    {
    public:
        explicit data_view_object(value aPrototype) : object{ aPrototype, object_class::DataView } {}
    public:
        array_buffer_object& buffer_object() const { return static_cast<array_buffer_object&>(obj(buffer)); }
    public:
        value buffer = Undefined;
        std::size_t byteOffset = 0u;
        std::size_t byteLength = 0u;
        bool lengthTracking = false;
    };

    // --- conversions between numeric values and the bytes of elements (native byte order is little endian)

    std::uint16_t double_to_float16(double aValue);     ///< (rounded to nearest, ties to even)
    double float16_to_double(std::uint16_t aBits);
    // (the value of an element (a Number or a BigInt) from its bytes (GetValueFromBuffer: RawBytesToNumeric))
    value element_value(element_type aType, std::uint8_t const* aBytes, bool aLittleEndian = true);
    // (an element's bytes (SetValueInBuffer: NumericToRawBytes) from a Number (or a BigInt for the BigInt types))
    void store_element(element_type aType, value aNumeric, std::uint8_t* aBytes, bool aLittleEndian = true);
    // (ToNumber or ToBigInt as the type's content is)
    value to_element_numeric(element_type aType, value aValue);
}
