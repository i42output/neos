/*
  builtins_typed_arrays.cpp

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

// Typed arrays: the integer-indexed exotic objects, %TypedArray% and its prototype, the TypedArray constructors and
// Uint8Array's base64 and hex methods

#include <algorithm>
#include "js.hpp"
#include "api.hpp"
#include "typed_arrays.hpp"

namespace neos::javascript_runtime
{
    // --- the exotic object

    namespace
    {
        // (CanonicalNumericIndexString of a property key: none if it is not a canonical numeric string)
        std::optional<double> numeric_key(value aKey)
        {
            if (!is_string(aKey))
                return {};
            auto& s = cell_of<string>(aKey);
            auto const index = s.array_index();
            if (index >= 0)
                return static_cast<double>(index);
            auto const& text = s.flat();
            if (text.empty())
                return {};
            // (a canonical numeric string starts with a digit, a minus sign, "Infinity" or "NaN")
            auto const c = text[0];
            if (!((c >= u'0' && c <= u'9') || c == u'-' || c == u'I' || c == u'N'))
                return {};
            auto const n = canonical_numeric_index_string(aKey);
            if (n == Undefined)
                return {};
            return as_number(n);
        }
    }

    value typed_array_get_element(typed_array_object const& aTypedArray, double aIndex)
    {
        if (!aTypedArray.valid_index(aIndex))
            return Undefined;
        return element_value(aTypedArray.type, aTypedArray.element(static_cast<std::size_t>(aIndex)));
    }

    void typed_array_set_element(typed_array_object& aTypedArray, double aIndex, value aValue)
    {
        auto const numeric = to_element_numeric(aTypedArray.type, aValue);
        if (aTypedArray.valid_index(aIndex))
            store_element(aTypedArray.type, numeric, aTypedArray.element(static_cast<std::size_t>(aIndex)));
    }

    std::optional<property_descriptor> typed_array_object::get_own_property(value aKey)
    {
        if (auto const index = numeric_key(aKey))
        {
            auto const v = typed_array_get_element(*this, *index);
            if (v == Undefined)
                return {};
            return property_descriptor::data(v, AttributesDefault);
        }
        return ordinary_get_own_property(aKey);
    }

    bool typed_array_object::define_own_property(value aKey, property_descriptor const& aDescriptor)
    {
        if (auto const index = numeric_key(aKey))
        {
            if (!valid_index(*index))
                return false;
            if (aDescriptor.configurable && !*aDescriptor.configurable)
                return false;
            if (aDescriptor.enumerable && !*aDescriptor.enumerable)
                return false;
            if (aDescriptor.is_accessor())
                return false;
            if (aDescriptor.writable && !*aDescriptor.writable)
                return false;
            if (aDescriptor.valueField)
                typed_array_set_element(*this, *index, *aDescriptor.valueField);
            return true;
        }
        return object::define_own_property(aKey, aDescriptor);
    }

    bool typed_array_object::has_property(value aKey)
    {
        if (auto const index = numeric_key(aKey))
            return valid_index(*index);
        return object::has_property(aKey);
    }

    value typed_array_object::get(value aKey, value aReceiver)
    {
        if (auto const index = numeric_key(aKey))
            return typed_array_get_element(*this, *index);
        return object::get(aKey, aReceiver);
    }

    bool typed_array_object::set(value aKey, value aValue, value aReceiver)
    {
        if (auto const index = numeric_key(aKey))
        {
            if (aReceiver == value_of(*this))
            {
                typed_array_set_element(*this, *index, aValue);
                return true;
            }
            if (!valid_index(*index))
                return true;
            return ordinary_set(aKey, aValue, aReceiver);
        }
        return object::set(aKey, aValue, aReceiver);
    }

    bool typed_array_object::delete_property(value aKey)
    {
        if (auto const index = numeric_key(aKey))
            return !valid_index(*index);
        return object::delete_property(aKey);
    }

    value_list typed_array_object::own_property_keys()
    {
        value_list result;
        if (!out_of_bounds())
        {
            auto const n = length();
            result.reserve(n + properties.size());
            for (std::size_t i = 0u; i < n; ++i)
                result.push_back(index_key(i));
        }
        auto const rest = ordinary_own_property_keys();
        result.insert(result.end(), rest.begin(), rest.end());
        return result;
    }

    bool typed_array_object::prevent_extensions()
    {
        if (!fixed_length())
            return false;
        extensible = false;
        return true;
    }

    typed_array_object& validate_typed_array(value aValue)
    {
        auto* t = as_typed_array(aValue);
        if (t == nullptr)
            throw_type_error("this is not a typed array.");
        if (t->out_of_bounds())
            throw_type_error(t->buffer_object().detached ? "Cannot perform this operation on a detached ArrayBuffer" : "typed array is out of bounds");
        return *t;
    }

    double typed_array_length_checked(value aTypedArray)
    {
        return static_cast<double>(validate_typed_array(aTypedArray).length());
    }

    namespace
    {
        constexpr double MaxSafeInteger = 9007199254740991.0;

        value key(double aIndex)
        {
            if (aIndex < 4294967295.0)
                return index_key(static_cast<std::uint64_t>(aIndex));
            return intern(number_to_string(aIndex));
        }

        // (ToClampedIndex)
        double clamped_index(value aValue, double aLength)
        {
            auto const i = to_integer_or_infinity(aValue) + 0.0;    // (+ 0: not -0)
            if (i < 0.0)
                return std::max(aLength + i, 0.0);
            return std::min(i, aLength);
        }

        // (ToAbsoluteIndex)
        double absolute_index(value aValue, double aLength)
        {
            auto const i = to_integer_or_infinity(aValue) + 0.0;    // (+ 0: not -0)
            if (std::isfinite(i) && i < 0.0)
                return aLength + i;
            return i;
        }

        typed_array_object& this_typed_array(value aThis)
        {
            auto* t = as_typed_array(aThis);
            if (t == nullptr)
                throw_type_error("this is not a typed array.");
            return *t;
        }

        // (ValidateTypedArrayBounds)
        void validate_bounds(typed_array_object& aTypedArray)
        {
            if (aTypedArray.out_of_bounds())
                throw_type_error(aTypedArray.buffer_object().detached ? "Cannot perform this operation on a detached ArrayBuffer" : "typed array is out of bounds");
        }

        value callback_argument(arguments aArguments, char const* aMethod)
        {
            auto const f = argument(aArguments, 0u);
            if (!is_callable(f))
                throw_type_error(describe(f) + " is not a function (in %TypedArray%.prototype." + aMethod + ")");
            return f;
        }

        // (AllocateTypedArray: the object (its prototype from the new target), without its buffer)
        typed_array_object& allocate_typed_array(element_type aType, value aNewTarget)
        {
            auto prototype = get(aNewTarget, the_names().prototype);
            if (!is_object(prototype))
                prototype = function_realm(aNewTarget).typedArrayPrototypes[static_cast<std::size_t>(aType)];
            return the_heap().make<typed_array_object>(prototype, aType);
        }

        // (AllocateTypedArrayBuffer)
        void allocate_typed_array_buffer(typed_array_object& aTypedArray, double aLength)
        {
            auto const byteLength = static_cast<double>(aTypedArray.element_size()) * aLength;
            aTypedArray.buffer = allocate_array_buffer(the_realm().arrayBuffer, byteLength);
            aTypedArray.byteOffset = 0u;
            aTypedArray.arrayLength = static_cast<std::size_t>(aLength);
            aTypedArray.lengthTracking = false;
        }

        value make_typed_array(element_type aType, double aLength)
        {
            auto& t = the_heap().make<typed_array_object>(the_realm().typedArrayPrototypes[static_cast<std::size_t>(aType)], aType);
            allocate_typed_array_buffer(t, aLength);
            return value_of(t);
        }

        // (TypedArrayCreateFromConstructor)
        typed_array_object& typed_array_create(value aConstructor, arguments aArguments)
        {
            auto const result = construct(aConstructor, aArguments);
            auto& t = validate_typed_array(result);
            if (aArguments.size() == 1u && is_number(aArguments[0]) && static_cast<double>(t.length()) < as_number(aArguments[0]))
                throw_type_error("the typed array that the constructor created is too short");
            return t;
        }

        typed_array_object& typed_array_create(value aConstructor, double aLength)
        {
            value const lengthArgument[] = { number(aLength) };
            return typed_array_create(aConstructor, lengthArgument);
        }

        // (TypedArraySpeciesCreate)
        typed_array_object& typed_array_species_create(typed_array_object& aExemplar, arguments aArguments)
        {
            auto const defaultConstructor = the_realm().typedArrayConstructors[static_cast<std::size_t>(aExemplar.type)];
            auto const c = species_constructor(value_of(aExemplar), defaultConstructor);
            auto& result = typed_array_create(c, aArguments);
            if (result.bigint() != aExemplar.bigint())
                throw_type_error("the typed array that the species constructor created has a different content type");
            return result;
        }

        typed_array_object& typed_array_species_create(typed_array_object& aExemplar, double aLength)
        {
            value const lengthArgument[] = { number(aLength) };
            return typed_array_species_create(aExemplar, lengthArgument);
        }

        // (TypedArrayCreateSameType)
        typed_array_object& typed_array_create_same_type(typed_array_object& aExemplar, double aLength)
        {
            return typed_array_create(the_realm().typedArrayConstructors[static_cast<std::size_t>(aExemplar.type)], aLength);
        }

        // (whether converting an element of one type to another keeps its bits)
        bool same_bits(element_type aSource, element_type aTarget)
        {
            if (aSource == aTarget)
                return true;
            auto const integral = [](element_type aType) { return aType != element_type::Float16 && aType != element_type::Float32 && aType != element_type::Float64; };
            if (!integral(aSource) || !integral(aTarget) || element_size(aSource) != element_size(aTarget))
                return false;
            return aTarget != element_type::Uint8Clamped || aSource == element_type::Uint8;
        }

        // (copy elements from one buffer to another (that do not overlap), converting them)
        void copy_elements(element_type aSourceType, std::uint8_t const* aSource, element_type aTargetType, std::uint8_t* aTarget, std::size_t aCount)
        {
            if (aCount == 0u)
                return;
            if (same_bits(aSourceType, aTargetType))
            {
                std::memmove(aTarget, aSource, aCount * element_size(aSourceType));
                return;
            }
            auto const sourceSize = element_size(aSourceType);
            auto const targetSize = element_size(aTargetType);
            for (std::size_t i = 0u; i < aCount; ++i)
                store_element(aTargetType, element_value(aSourceType, aSource + i * sourceSize), aTarget + i * targetSize);
        }

        // --- the constructors

        value typed_array_abstract_constructor(value, arguments, value, function_object&)
        {
            throw_type_error("Abstract class TypedArray not directly constructable");
        }

        // (InitializeTypedArrayFromTypedArray)
        void initialize_from_typed_array(typed_array_object& aTypedArray, typed_array_object& aSource)
        {
            validate_bounds(aSource);
            auto const elementLength = aSource.length();
            auto const byteLength = aTypedArray.element_size() * elementLength;
            value data;
            if (aTypedArray.type == aSource.type)
                data = clone_array_buffer(aSource.buffer_object(), aSource.byteOffset, byteLength);
            else
            {
                data = allocate_array_buffer(the_realm().arrayBuffer, static_cast<double>(byteLength));
                if (aSource.bigint() != aTypedArray.bigint())
                    throw_type_error("Content type of the source typed array differs from the typed array's");
                copy_elements(aSource.type, aSource.element(0u), aTypedArray.type, static_cast<array_buffer_object&>(obj(data)).bytes(), elementLength);
            }
            aTypedArray.buffer = data;
            aTypedArray.byteOffset = 0u;
            aTypedArray.arrayLength = elementLength;
        }

        // (InitializeTypedArrayFromArrayBuffer)
        void initialize_from_array_buffer(typed_array_object& aTypedArray, value aBuffer, value aByteOffset, value aLength)
        {
            auto& b = static_cast<array_buffer_object&>(obj(aBuffer));
            auto const elementSize = static_cast<double>(aTypedArray.element_size());
            auto const offset = to_index(aByteOffset);
            if (std::fmod(offset, elementSize) != 0.0)
                throw_range_error("start offset of " + std::string{ element_type_name(aTypedArray.type) } + " should be a multiple of " +
                    std::to_string(aTypedArray.element_size()));
            bool const bufferIsFixedLength = is_fixed_length(b);
            double newLength = 0.0;
            if (aLength != Undefined)
                newLength = to_index(aLength);
            if (b.detached)
                throw_type_error("Cannot construct a typed array on a detached ArrayBuffer");
            auto const bufferByteLength = static_cast<double>(b.byte_length());
            if (aLength == Undefined && !bufferIsFixedLength)
            {
                if (offset > bufferByteLength)
                    throw_range_error("Start offset " + utf8(number_to_string(offset)) + " is outside the bounds of the buffer");
                aTypedArray.lengthTracking = true;
                aTypedArray.arrayLength = 0u;
            }
            else
            {
                double newByteLength;
                if (aLength == Undefined)
                {
                    if (std::fmod(bufferByteLength, elementSize) != 0.0)
                        throw_range_error("byte length of " + std::string{ element_type_name(aTypedArray.type) } + " should be a multiple of " +
                            std::to_string(aTypedArray.element_size()));
                    newByteLength = bufferByteLength - offset;
                    if (newByteLength < 0.0)
                        throw_range_error("Start offset " + utf8(number_to_string(offset)) + " is outside the bounds of the buffer");
                }
                else
                {
                    newByteLength = newLength * elementSize;
                    if (offset + newByteLength > bufferByteLength)
                        throw_range_error("Invalid typed array length: " + utf8(number_to_string(newLength)));
                }
                aTypedArray.arrayLength = static_cast<std::size_t>(newByteLength / elementSize);
            }
            aTypedArray.buffer = aBuffer;
            aTypedArray.byteOffset = static_cast<std::size_t>(offset);
        }

        value typed_array_constructor(value, arguments aArguments, value aNewTarget, function_object& aCallee)
        {
            auto const type = static_cast<element_type>(aCallee.nativeId);
            if (aNewTarget == Undefined)
                throw_type_error(std::string{ "Constructor " } + element_type_name(type) + " requires 'new'");
            if (aArguments.empty())
            {
                auto& t = allocate_typed_array(type, aNewTarget);
                allocate_typed_array_buffer(t, 0.0);
                return value_of(t);
            }
            auto const first = aArguments[0];
            if (is_object(first))
            {
                auto& t = allocate_typed_array(type, aNewTarget);
                auto& f = obj(first);
                if (f.cls == object_class::TypedArray)
                    initialize_from_typed_array(t, static_cast<typed_array_object&>(f));
                else if (f.cls == object_class::ArrayBuffer || f.cls == object_class::SharedArrayBuffer)
                    initialize_from_array_buffer(t, first, argument(aArguments, 1u), argument(aArguments, 2u));
                else
                {
                    auto const usingIterator = get_method(first, the_realm().symIterator);
                    if (usingIterator != Undefined)
                    {
                        value_list values;
                        auto record = get_iterator_from_method(first, usingIterator);
                        while (auto const next = iterator_step_value(record))
                            values.push_back(*next);
                        allocate_typed_array_buffer(t, static_cast<double>(values.size()));
                        for (std::size_t k = 0u; k < values.size(); ++k)
                            typed_array_set_element(t, static_cast<double>(k), values[k]);
                    }
                    else
                    {
                        auto const length = length_of_array_like(first);
                        allocate_typed_array_buffer(t, length);
                        auto* a = as_array(first);
                        for (double k = 0.0; k < length; ++k)
                        {
                            value v;
                            if (a == nullptr || !a->dense_get(static_cast<std::uint32_t>(k), v))
                                v = get(first, key(k));
                            typed_array_set_element(t, k, v);
                        }
                    }
                }
                return value_of(t);
            }
            auto const elementLength = to_index(first);
            auto& t = allocate_typed_array(type, aNewTarget);
            allocate_typed_array_buffer(t, elementLength);
            return value_of(t);
        }

        value typed_array_from(value aThis, arguments aArguments, value, function_object&)
        {
            auto const c = aThis;
            if (!is_constructor(c))
                throw_type_error(describe(c) + " is not a constructor");
            auto const source = argument(aArguments, 0u);
            auto const mapper = argument(aArguments, 1u);
            auto const thisArg = argument(aArguments, 2u);
            bool const mapping = mapper != Undefined;
            if (mapping && !is_callable(mapper))
                throw_type_error(describe(mapper) + " is not a function");
            auto const usingIterator = get_method(source, the_realm().symIterator);
            if (usingIterator != Undefined)
            {
                value_list values;
                auto record = get_iterator_from_method(source, usingIterator);
                while (auto const next = iterator_step_value(record))
                    values.push_back(*next);
                auto& target = typed_array_create(c, static_cast<double>(values.size()));
                for (std::size_t k = 0u; k < values.size(); ++k)
                {
                    auto v = values[k];
                    if (mapping)
                    {
                        value const args[] = { v, number(static_cast<double>(k)) };
                        v = call(mapper, thisArg, args);
                    }
                    typed_array_set_element(target, static_cast<double>(k), v);
                }
                return value_of(target);
            }
            auto const arrayLike = to_object(source);
            auto const length = length_of_array_like(arrayLike);
            auto& target = typed_array_create(c, length);
            for (double k = 0.0; k < length; ++k)
            {
                auto v = get(arrayLike, key(k));
                if (mapping)
                {
                    value const args[] = { v, number(k) };
                    v = call(mapper, thisArg, args);
                }
                typed_array_set_element(target, k, v);
            }
            return value_of(target);
        }

        value typed_array_of(value aThis, arguments aArguments, value, function_object&)
        {
            if (!is_constructor(aThis))
                throw_type_error(describe(aThis) + " is not a constructor");
            auto& target = typed_array_create(aThis, static_cast<double>(aArguments.size()));
            for (std::size_t k = 0u; k < aArguments.size(); ++k)
                typed_array_set_element(target, static_cast<double>(k), aArguments[k]);
            return value_of(target);
        }

        value species_getter(value aThis, arguments, value, function_object&)
        {
            return aThis;
        }

        // --- %TypedArray%.prototype

        value typed_array_buffer(value aThis, arguments, value, function_object&)
        {
            return this_typed_array(aThis).buffer;
        }

        value typed_array_byte_length(value aThis, arguments, value, function_object&)
        {
            return number(static_cast<double>(this_typed_array(aThis).byte_length()));
        }

        value typed_array_byte_offset(value aThis, arguments, value, function_object&)
        {
            auto& t = this_typed_array(aThis);
            if (t.out_of_bounds())
                return number(0.0);
            return number(static_cast<double>(t.byteOffset));
        }

        value typed_array_length(value aThis, arguments, value, function_object&)
        {
            return number(static_cast<double>(this_typed_array(aThis).length()));
        }

        value typed_array_to_string_tag(value aThis, arguments, value, function_object&)
        {
            auto* t = as_typed_array(aThis);
            if (t == nullptr)
                return Undefined;
            return ascii(element_type_name(t->type));
        }

        value typed_array_at(value aThis, arguments aArguments, value, function_object&)
        {
            auto& t = validate_typed_array(aThis);
            auto const length = static_cast<double>(t.length());
            auto const k = absolute_index(argument(aArguments, 0u), length);
            if (k < 0.0 || k >= length)
                return Undefined;
            return typed_array_get_element(t, k);
        }

        value typed_array_copy_within(value aThis, arguments aArguments, value, function_object&)
        {
            auto& t = validate_typed_array(aThis);
            auto length = static_cast<double>(t.length());
            auto const targetIndex = clamped_index(argument(aArguments, 0u), length);
            auto const startIndex = clamped_index(argument(aArguments, 1u), length);
            auto const end = argument(aArguments, 2u);
            auto const endIndex = end == Undefined ? length : clamped_index(end, length);
            auto count = std::min(endIndex - startIndex, length - targetIndex);
            if (count > 0.0)
            {
                validate_bounds(t);
                length = static_cast<double>(t.length());
                count = std::min({ count, length - startIndex, length - targetIndex });
                if (count > 0.0)
                {
                    auto const elementSize = t.element_size();
                    auto* const base = t.buffer_object().bytes() + t.byteOffset;
                    std::memmove(base + static_cast<std::size_t>(targetIndex) * elementSize, base + static_cast<std::size_t>(startIndex) * elementSize,
                        static_cast<std::size_t>(count) * elementSize);
                }
            }
            return aThis;
        }

        value typed_array_entries(value aThis, arguments, value, function_object&)
        {
            validate_typed_array(aThis);
            return make_array_iterator(aThis, 2);
        }

        value typed_array_keys(value aThis, arguments, value, function_object&)
        {
            validate_typed_array(aThis);
            return make_array_iterator(aThis, 0);
        }

        value typed_array_values(value aThis, arguments, value, function_object&)
        {
            validate_typed_array(aThis);
            return make_array_iterator(aThis, 1);
        }

        // (every (0), some (1) and forEach (2))
        value typed_array_every(value aThis, arguments aArguments, value, function_object& aCallee)
        {
            static char const* const sNames[] = { "every", "some", "forEach" };
            auto& t = validate_typed_array(aThis);
            auto const length = static_cast<double>(t.length());
            auto const callback = callback_argument(aArguments, sNames[aCallee.nativeId]);
            auto const thisArg = argument(aArguments, 1u);
            for (double k = 0.0; k < length; ++k)
            {
                value const args[] = { typed_array_get_element(t, k), number(k), aThis };
                auto const result = call(callback, thisArg, args);
                if (aCallee.nativeId == 0u && !to_boolean(result))
                    return False;
                if (aCallee.nativeId == 1u && to_boolean(result))
                    return True;
            }
            return aCallee.nativeId == 0u ? True : aCallee.nativeId == 1u ? False : Undefined;
        }

        value typed_array_fill(value aThis, arguments aArguments, value, function_object&)
        {
            auto& t = validate_typed_array(aThis);
            auto length = static_cast<double>(t.length());
            auto const v = to_element_numeric(t.type, argument(aArguments, 0u));
            auto const startIndex = clamped_index(argument(aArguments, 1u), length);
            auto const end = argument(aArguments, 2u);
            auto endIndex = end == Undefined ? length : clamped_index(end, length);
            validate_bounds(t);
            length = static_cast<double>(t.length());
            endIndex = std::min(endIndex, length);
            if (startIndex < endIndex)
            {
                std::uint8_t bytes[8];
                store_element(t.type, v, bytes);
                auto const size = t.element_size();
                for (auto k = static_cast<std::size_t>(startIndex); k < static_cast<std::size_t>(endIndex); ++k)
                    std::memcpy(t.element(k), bytes, size);
            }
            return aThis;
        }

        value typed_array_filter(value aThis, arguments aArguments, value, function_object&)
        {
            auto& t = validate_typed_array(aThis);
            auto const length = static_cast<double>(t.length());
            auto const callback = callback_argument(aArguments, "filter");
            auto const thisArg = argument(aArguments, 1u);
            value_list kept;
            for (double k = 0.0; k < length; ++k)
            {
                auto const kValue = typed_array_get_element(t, k);
                value const args[] = { kValue, number(k), aThis };
                if (to_boolean(call(callback, thisArg, args)))
                    kept.push_back(kValue);
            }
            auto& result = typed_array_species_create(t, static_cast<double>(kept.size()));
            for (std::size_t n = 0u; n < kept.size(); ++n)
                typed_array_set_element(result, static_cast<double>(n), kept[n]);
            return value_of(result);
        }

        // (find (0), findIndex (1), findLast (2), findLastIndex (3))
        value typed_array_find(value aThis, arguments aArguments, value, function_object& aCallee)
        {
            static char const* const sNames[] = { "find", "findIndex", "findLast", "findLastIndex" };
            auto& t = validate_typed_array(aThis);
            auto const length = static_cast<double>(t.length());
            auto const predicate = callback_argument(aArguments, sNames[aCallee.nativeId]);
            auto const thisArg = argument(aArguments, 1u);
            bool const descending = aCallee.nativeId >= 2u;
            bool const wantIndex = (aCallee.nativeId & 1u) != 0u;
            for (double i = 0.0; i < length; ++i)
            {
                auto const k = descending ? length - 1.0 - i : i;
                auto const kValue = typed_array_get_element(t, k);
                value const args[] = { kValue, number(k), aThis };
                if (to_boolean(call(predicate, thisArg, args)))
                    return wantIndex ? number(k) : kValue;
            }
            return wantIndex ? number(-1.0) : Undefined;
        }

        value typed_array_includes(value aThis, arguments aArguments, value, function_object&)
        {
            auto& t = validate_typed_array(aThis);
            auto const length = static_cast<double>(t.length());
            if (length == 0.0)
                return False;
            auto const searchElement = argument(aArguments, 0u);
            for (auto k = clamped_index(argument(aArguments, 1u), length); k < length; ++k)
                if (same_value_zero(searchElement, typed_array_get_element(t, k)))
                    return True;
            return False;
        }

        value typed_array_index_of(value aThis, arguments aArguments, value, function_object&)
        {
            auto& t = validate_typed_array(aThis);
            auto const length = static_cast<double>(t.length());
            if (length == 0.0)
                return number(-1.0);
            auto const searchElement = argument(aArguments, 0u);
            for (auto k = clamped_index(argument(aArguments, 1u), length); k < length; ++k)
                if (t.valid_index(k) && strictly_equal(searchElement, typed_array_get_element(t, k)))
                    return number(k);
            return number(-1.0);
        }

        value typed_array_last_index_of(value aThis, arguments aArguments, value, function_object&)
        {
            auto& t = validate_typed_array(aThis);
            auto const length = static_cast<double>(t.length());
            if (length == 0.0)
                return number(-1.0);
            auto k = aArguments.size() < 2u ? length - 1.0 : std::min(absolute_index(aArguments[1], length), length - 1.0);
            auto const searchElement = argument(aArguments, 0u);
            for (; k >= 0.0; --k)
                if (t.valid_index(k) && strictly_equal(searchElement, typed_array_get_element(t, k)))
                    return number(k);
            return number(-1.0);
        }

        value typed_array_join(value aThis, arguments aArguments, value, function_object&)
        {
            auto& t = validate_typed_array(aThis);
            auto const length = static_cast<double>(t.length());
            auto const separator = argument(aArguments, 0u);
            std::u16string const separatorString = separator == Undefined ? std::u16string{ u"," } : to_u16string(separator);
            std::u16string result;
            for (double k = 0.0; k < length; ++k)
            {
                if (k > 0.0)
                    result += separatorString;
                auto const element = typed_array_get_element(t, k);
                if (element == Undefined)
                    continue;
                if (is_number(element))
                    result += number_to_u16string(as_number(element));
                else
                    result += to_u16string(element);
                if (result.size() > MaxStringLength)
                    throw_range_error("invalid string length");
            }
            return make_string(std::move(result));
        }

        value typed_array_map(value aThis, arguments aArguments, value, function_object&)
        {
            auto& t = validate_typed_array(aThis);
            auto const length = static_cast<double>(t.length());
            auto const callback = callback_argument(aArguments, "map");
            auto const thisArg = argument(aArguments, 1u);
            auto& result = typed_array_species_create(t, length);
            for (double k = 0.0; k < length; ++k)
            {
                value const args[] = { typed_array_get_element(t, k), number(k), aThis };
                typed_array_set_element(result, k, call(callback, thisArg, args));
            }
            return value_of(result);
        }

        // (reduce (0) and reduceRight (1))
        value typed_array_reduce(value aThis, arguments aArguments, value, function_object& aCallee)
        {
            bool const right = aCallee.nativeId == 1u;
            auto& t = validate_typed_array(aThis);
            auto const length = static_cast<double>(t.length());
            auto const callback = callback_argument(aArguments, right ? "reduceRight" : "reduce");
            if (length == 0.0 && aArguments.size() < 2u)
                throw_type_error("Reduce of empty array with no initial value");
            double k = right ? length - 1.0 : 0.0;
            double const step = right ? -1.0 : 1.0;
            value accumulator;
            if (aArguments.size() >= 2u)
                accumulator = aArguments[1];
            else
            {
                accumulator = typed_array_get_element(t, k);
                k += step;
            }
            for (; k >= 0.0 && k < length; k += step)
            {
                value const args[] = { accumulator, typed_array_get_element(t, k), number(k), aThis };
                accumulator = call(callback, Undefined, args);
            }
            return accumulator;
        }

        value typed_array_reverse(value aThis, arguments, value, function_object&)
        {
            auto& t = validate_typed_array(aThis);
            auto const length = t.length();
            auto const size = t.element_size();
            std::uint8_t temporary[8];
            for (std::size_t lower = 0u, middle = length / 2u; lower != middle; ++lower)
            {
                auto* l = t.element(lower);
                auto* u = t.element(length - lower - 1u);
                std::memcpy(temporary, l, size);
                std::memcpy(l, u, size);
                std::memcpy(u, temporary, size);
            }
            return aThis;
        }

        value typed_array_set(value aThis, arguments aArguments, value, function_object&)
        {
            auto& target = this_typed_array(aThis);
            auto const targetOffset = to_integer_or_infinity(argument(aArguments, 1u));
            if (targetOffset < 0.0)
                throw_range_error("offset is out of bounds");
            auto const source = argument(aArguments, 0u);
            if (auto* s = as_typed_array(source))
            {
                // (SetTypedArrayFromTypedArray)
                validate_bounds(target);
                auto const targetLength = static_cast<double>(target.length());
                validate_bounds(*s);
                auto const sourceLength = s->length();
                if (std::isinf(targetOffset) || static_cast<double>(sourceLength) + targetOffset > targetLength)
                    throw_range_error("offset is out of bounds");
                if (target.bigint() != s->bigint())
                    throw_type_error("Content type of the source typed array differs from the typed array's");
                std::vector<std::uint8_t> copy;
                std::uint8_t const* from = s->element(0u);
                if (s->buffer == target.buffer)
                {
                    copy.assign(from, from + sourceLength * s->element_size());
                    from = copy.data();
                }
                copy_elements(s->type, from, target.type, target.element(static_cast<std::size_t>(targetOffset)), sourceLength);
                return Undefined;
            }
            // (SetTypedArrayFromArrayLike)
            validate_bounds(target);
            auto const targetLength = static_cast<double>(target.length());
            auto const src = to_object(source);
            auto const sourceLength = length_of_array_like(src);
            if (std::isinf(targetOffset) || sourceLength + targetOffset > targetLength)
                throw_range_error("offset is out of bounds");
            auto* a = as_array(src);
            for (double k = 0.0; k < sourceLength; ++k)
            {
                value v;
                if (a == nullptr || !a->dense_get(static_cast<std::uint32_t>(k), v))
                    v = get(src, key(k));
                typed_array_set_element(target, targetOffset + k, v);
            }
            return Undefined;
        }

        value typed_array_slice(value aThis, arguments aArguments, value, function_object&)
        {
            auto& t = validate_typed_array(aThis);
            auto const sourceLength = static_cast<double>(t.length());
            auto const startIndex = clamped_index(argument(aArguments, 0u), sourceLength);
            auto const end = argument(aArguments, 1u);
            auto endIndex = end == Undefined ? sourceLength : clamped_index(end, sourceLength);
            auto count = std::max(endIndex - startIndex, 0.0);
            auto& result = typed_array_species_create(t, count);
            if (count > 0.0)
            {
                validate_bounds(t);
                endIndex = std::min(endIndex, static_cast<double>(t.length()));
                count = std::max(endIndex - startIndex, 0.0);
                if (t.type == result.type)
                {
                    // (byte by byte, in ascending order (the buffers can be the same))
                    auto const elementSize = t.element_size();
                    auto* sourceBytes = t.buffer_object().bytes();
                    auto& targetBuffer = result.buffer_object();
                    auto* targetBytes = targetBuffer.bytes();
                    auto sourceByteIndex = static_cast<std::size_t>(startIndex) * elementSize + t.byteOffset;
                    auto targetByteIndex = result.byteOffset;
                    auto const endByteIndex = std::min(targetByteIndex + static_cast<std::size_t>(count) * elementSize, targetBuffer.byte_length());
                    for (; targetByteIndex < endByteIndex; ++targetByteIndex, ++sourceByteIndex)
                        targetBytes[targetByteIndex] = sourceBytes[sourceByteIndex];
                }
                else
                {
                    double n = 0.0;
                    for (auto k = startIndex; k < endIndex; ++k, ++n)
                        typed_array_set_element(result, n, typed_array_get_element(t, k));
                }
            }
            return value_of(result);
        }

        // (CompareTypedArrayElements)
        double compare_elements(value aX, value aY, value aComparator)
        {
            if (aComparator != Undefined)
            {
                value const args[] = { aX, aY };
                auto const v = to_number(call(aComparator, Undefined, args));
                return std::isnan(v) ? 0.0 : v;
            }
            if (is_bigint(aX))
                return static_cast<double>(bigint_compare(aX, aY));
            auto const x = as_number(aX);
            auto const y = as_number(aY);
            if (std::isnan(x) && std::isnan(y))
                return 0.0;
            if (std::isnan(x))
                return 1.0;
            if (std::isnan(y))
                return -1.0;
            if (x < y)
                return -1.0;
            if (x > y)
                return 1.0;
            if (x == 0.0 && y == 0.0)
            {
                if (std::signbit(x) && !std::signbit(y))
                    return -1.0;
                if (!std::signbit(x) && std::signbit(y))
                    return 1.0;
            }
            return 0.0;
        }

        // (SortIndexedProperties for a typed array: its elements, sorted (a stable merge sort: the comparator can be inconsistent))
        value_list sorted_elements(typed_array_object& aTypedArray, std::size_t aLength, value aComparator)
        {
            value_list items;
            items.reserve(aLength);
            for (std::size_t k = 0u; k < aLength; ++k)
                items.push_back(typed_array_get_element(aTypedArray, static_cast<double>(k)));
            if (aComparator == Undefined && !aTypedArray.bigint())
            {
                // (numbers: no user code; std::stable_sort with a strict weak ordering (NaN last, -0 before +0))
                std::stable_sort(items.begin(), items.end(), [](value a, value b) { return compare_elements(a, b, Undefined) < 0.0; });
                return items;
            }
            value_list buffer(items.size());
            auto sort = [&](auto& aSelf, std::size_t aLow, std::size_t aHigh) -> void
            {
                if (aHigh - aLow < 2u)
                    return;
                auto const middle = aLow + (aHigh - aLow) / 2u;
                aSelf(aSelf, aLow, middle);
                aSelf(aSelf, middle, aHigh);
                std::size_t i = aLow, j = middle, k = aLow;
                while (i < middle && j < aHigh)
                    buffer[k++] = compare_elements(items[j], items[i], aComparator) < 0.0 ? items[j++] : items[i++];
                while (i < middle)
                    buffer[k++] = items[i++];
                while (j < aHigh)
                    buffer[k++] = items[j++];
                std::copy(buffer.begin() + static_cast<std::ptrdiff_t>(aLow), buffer.begin() + static_cast<std::ptrdiff_t>(aHigh),
                    items.begin() + static_cast<std::ptrdiff_t>(aLow));
            };
            sort(sort, 0u, items.size());
            return items;
        }

        value comparator_argument(arguments aArguments)
        {
            auto const comparator = argument(aArguments, 0u);
            if (comparator != Undefined && !is_callable(comparator))
                throw_type_error("The comparison function must be either a function or undefined");
            return comparator;
        }

        value typed_array_sort(value aThis, arguments aArguments, value, function_object&)
        {
            auto const comparator = comparator_argument(aArguments);
            auto& t = validate_typed_array(aThis);
            auto const length = t.length();
            auto const sorted = sorted_elements(t, length, comparator);
            for (std::size_t j = 0u; j < length; ++j)
                typed_array_set_element(t, static_cast<double>(j), sorted[j]);
            return aThis;
        }

        value typed_array_to_sorted(value aThis, arguments aArguments, value, function_object&)
        {
            auto const comparator = comparator_argument(aArguments);
            auto& t = validate_typed_array(aThis);
            auto const length = t.length();
            auto& result = typed_array_create_same_type(t, static_cast<double>(length));
            auto const sorted = sorted_elements(t, length, comparator);
            for (std::size_t j = 0u; j < length; ++j)
                typed_array_set_element(result, static_cast<double>(j), sorted[j]);
            return value_of(result);
        }

        value typed_array_subarray(value aThis, arguments aArguments, value, function_object&)
        {
            auto& t = this_typed_array(aThis);
            auto const buffer = t.buffer;
            auto const sourceLength = static_cast<double>(t.length());
            auto const startIndex = clamped_index(argument(aArguments, 0u), sourceLength);
            auto const beginByteOffset = static_cast<double>(t.byteOffset) + startIndex * static_cast<double>(t.element_size());
            auto const end = argument(aArguments, 1u);
            if (t.lengthTracking && end == Undefined)
            {
                value const args[] = { buffer, number(beginByteOffset) };
                return value_of(typed_array_species_create(t, args));
            }
            auto const endIndex = end == Undefined ? sourceLength : clamped_index(end, sourceLength);
            auto const newLength = std::max(endIndex - startIndex, 0.0);
            value const args[] = { buffer, number(beginByteOffset), number(newLength) };
            return value_of(typed_array_species_create(t, args));
        }

        value typed_array_to_locale_string(value aThis, arguments aArguments, value, function_object&)
        {
#if defined(NEOS_JS_ICU)
            value const localeArguments[] = { argument(aArguments, 0u), argument(aArguments, 1u) };
#else
            (void)aArguments;
            arguments const localeArguments{};
#endif
            auto& t = validate_typed_array(aThis);
            auto const length = static_cast<double>(t.length());
            std::u16string result;
            for (double k = 0.0; k < length; ++k)
            {
                if (k > 0.0)
                    result += u",";
                auto const element = typed_array_get_element(t, k);
                if (!is_nullish(element))
                    result += to_u16string(invoke(element, ascii("toLocaleString"), localeArguments));
            }
            return make_string(std::move(result));
        }

        value typed_array_to_reversed(value aThis, arguments, value, function_object&)
        {
            auto& t = validate_typed_array(aThis);
            auto const length = static_cast<double>(t.length());
            auto& result = typed_array_create_same_type(t, length);
            for (double k = 0.0; k < length; ++k)
                typed_array_set_element(result, k, typed_array_get_element(t, length - k - 1.0));
            return value_of(result);
        }

        value typed_array_with(value aThis, arguments aArguments, value, function_object&)
        {
            auto& t = validate_typed_array(aThis);
            auto const length = static_cast<double>(t.length());
            auto const actualIndex = absolute_index(argument(aArguments, 0u), length);
            auto const numericValue = to_element_numeric(t.type, argument(aArguments, 1u));
            if (!t.valid_index(actualIndex))
                throw_range_error("Invalid typed array index");
            auto& result = typed_array_create_same_type(t, length);
            for (double k = 0.0; k < length; ++k)
                typed_array_set_element(result, k, k == actualIndex ? numericValue : typed_array_get_element(t, k));
            return value_of(result);
        }

        // --- Uint8Array: base64 and hex

        typed_array_object& this_uint8_array(value aThis)
        {
            auto* t = as_typed_array(aThis);
            if (t == nullptr || t->type != element_type::Uint8)
                throw_type_error("this is not a Uint8Array");
            return *t;
        }

        // (GetOptionsObject)
        value options_object(value aOptions)
        {
            if (aOptions == Undefined)
                return make_object(Null);
            if (is_object(aOptions))
                return aOptions;
            throw_type_error("options must be an object");
        }

        // (the alphabet option: whether it is base64url)
        bool alphabet_option(value aOptions)
        {
            auto const alphabet = get(aOptions, ascii("alphabet"));
            if (alphabet == Undefined)
                return false;
            if (is_string(alphabet))
            {
                auto const& text = str(alphabet);
                if (text == u"base64")
                    return false;
                if (text == u"base64url")
                    return true;
            }
            throw_type_error("alphabet must be \"base64\" or \"base64url\"");
        }

        enum class last_chunk_handling { Loose, Strict, StopBeforePartial };

        last_chunk_handling last_chunk_handling_option(value aOptions)
        {
            auto const handling = get(aOptions, ascii("lastChunkHandling"));
            if (handling == Undefined)
                return last_chunk_handling::Loose;
            if (is_string(handling))
            {
                auto const& text = str(handling);
                if (text == u"loose")
                    return last_chunk_handling::Loose;
                if (text == u"strict")
                    return last_chunk_handling::Strict;
                if (text == u"stop-before-partial")
                    return last_chunk_handling::StopBeforePartial;
            }
            throw_type_error("lastChunkHandling must be \"loose\", \"strict\" or \"stop-before-partial\"");
        }

        struct decode_result
        {
            std::size_t read = 0u;
            std::vector<std::uint8_t> bytes;
            bool error = false;
        };

        int base64_digit(char16_t aCharacter)
        {
            if (aCharacter >= u'A' && aCharacter <= u'Z')
                return aCharacter - u'A';
            if (aCharacter >= u'a' && aCharacter <= u'z')
                return aCharacter - u'a' + 26;
            if (aCharacter >= u'0' && aCharacter <= u'9')
                return aCharacter - u'0' + 52;
            if (aCharacter == u'+')
                return 62;
            if (aCharacter == u'/')
                return 63;
            return -1;
        }

        bool ascii_whitespace(char16_t aCharacter)
        {
            return aCharacter == 0x09u || aCharacter == 0x0Au || aCharacter == 0x0Cu || aCharacter == 0x0Du || aCharacter == 0x20u;
        }

        // (FromBase64)
        decode_result from_base64(std::u16string const& aString, bool aUrl, last_chunk_handling aHandling, std::size_t aMaxLength)
        {
            decode_result result;
            if (aMaxLength == 0u)
                return result;
            int chunk[4] = {};
            std::size_t chunkLength = 0u;
            std::size_t index = 0u;
            auto const length = aString.size();
            auto const skip = [&](std::size_t aIndex)
            {
                while (aIndex < length && ascii_whitespace(aString[aIndex]))
                    ++aIndex;
                return aIndex;
            };
            auto const decode_chunk = [&](std::size_t aCount)
            {
                // (aCount digits (2, 3 or 4) -> aCount - 1 bytes; returns whether the bits that are not used are zero)
                std::uint32_t const bits = (static_cast<std::uint32_t>(chunk[0]) << 18) | (static_cast<std::uint32_t>(aCount > 1u ? chunk[1] : 0) << 12) |
                    (static_cast<std::uint32_t>(aCount > 2u ? chunk[2] : 0) << 6) | static_cast<std::uint32_t>(aCount > 3u ? chunk[3] : 0);
                std::uint8_t const bytes[3] = { static_cast<std::uint8_t>(bits >> 16), static_cast<std::uint8_t>(bits >> 8), static_cast<std::uint8_t>(bits) };
                for (std::size_t i = 0u; i + 1u < aCount; ++i)
                    result.bytes.push_back(bytes[i]);
                return aCount == 4u || bytes[aCount - 1u] == 0u;
            };
            for (;;)
            {
                index = skip(index);
                if (index == length)
                {
                    if (chunkLength > 0u)
                    {
                        if (aHandling == last_chunk_handling::StopBeforePartial)
                            return result;
                        if (aHandling == last_chunk_handling::Strict || chunkLength == 1u)
                        {
                            result.error = true;
                            return result;
                        }
                        decode_chunk(chunkLength);
                    }
                    result.read = length;
                    return result;
                }
                auto c = aString[index++];
                if (c == u'=')
                {
                    if (chunkLength < 2u)
                    {
                        result.error = true;
                        return result;
                    }
                    index = skip(index);
                    if (chunkLength == 2u)
                    {
                        if (index == length)
                        {
                            if (aHandling != last_chunk_handling::StopBeforePartial)
                                result.error = true;
                            return result;
                        }
                        if (aString[index] == u'=')
                            index = skip(index + 1u);
                    }
                    if (index < length)
                    {
                        result.error = true;
                        return result;
                    }
                    auto const read = result.read;
                    if (!decode_chunk(chunkLength) && aHandling == last_chunk_handling::Strict)
                    {
                        result.bytes.resize(result.bytes.size() - (chunkLength - 1u));
                        result.read = read;
                        result.error = true;
                        return result;
                    }
                    result.read = length;
                    return result;
                }
                if (aUrl)
                {
                    if (c == u'+' || c == u'/')
                    {
                        result.error = true;
                        return result;
                    }
                    if (c == u'-')
                        c = u'+';
                    else if (c == u'_')
                        c = u'/';
                }
                auto const digit = base64_digit(c);
                if (digit < 0)
                {
                    result.error = true;
                    return result;
                }
                auto const remaining = aMaxLength - result.bytes.size();
                if ((remaining == 1u && chunkLength == 2u) || (remaining == 2u && chunkLength == 3u))
                    return result;
                chunk[chunkLength++] = digit;
                if (chunkLength == 4u)
                {
                    decode_chunk(4u);
                    chunkLength = 0u;
                    result.read = index;
                    if (result.bytes.size() == aMaxLength)
                        return result;
                }
            }
        }

        int hex_digit(char16_t aCharacter)
        {
            if (aCharacter >= u'0' && aCharacter <= u'9')
                return aCharacter - u'0';
            if (aCharacter >= u'a' && aCharacter <= u'f')
                return aCharacter - u'a' + 10;
            if (aCharacter >= u'A' && aCharacter <= u'F')
                return aCharacter - u'A' + 10;
            return -1;
        }

        // (FromHex)
        decode_result from_hex(std::u16string const& aString, std::size_t aMaxLength)
        {
            decode_result result;
            auto const length = aString.size();
            if (length % 2u != 0u)
            {
                result.error = true;
                return result;
            }
            while (result.read < length && result.bytes.size() < aMaxLength)
            {
                auto const high = hex_digit(aString[result.read]);
                auto const low = hex_digit(aString[result.read + 1u]);
                if (high < 0 || low < 0)
                {
                    result.error = true;
                    return result;
                }
                result.read += 2u;
                result.bytes.push_back(static_cast<std::uint8_t>(high * 16 + low));
            }
            return result;
        }

        value string_argument(arguments aArguments)
        {
            auto const s = argument(aArguments, 0u);
            if (!is_string(s))
                throw_type_error("argument must be a string");
            return s;
        }

        value uint8_array_from_bytes(std::vector<std::uint8_t> const& aBytes)
        {
            auto const result = make_typed_array(element_type::Uint8, static_cast<double>(aBytes.size()));
            if (!aBytes.empty())
                std::memcpy(as_typed_array(result)->element(0u), aBytes.data(), aBytes.size());
            return result;
        }

        value uint8_array_from_base64(value, arguments aArguments, value, function_object&)
        {
            auto const s = string_argument(aArguments);
            auto const options = options_object(argument(aArguments, 1u));
            bool const url = alphabet_option(options);
            auto const handling = last_chunk_handling_option(options);
            auto const result = from_base64(str(s), url, handling, std::numeric_limits<std::size_t>::max());
            if (result.error)
                throw_syntax_error("invalid base64 string");
            return uint8_array_from_bytes(result.bytes);
        }

        value uint8_array_from_hex(value, arguments aArguments, value, function_object&)
        {
            auto const s = string_argument(aArguments);
            auto const result = from_hex(str(s), std::numeric_limits<std::size_t>::max());
            if (result.error)
                throw_syntax_error("invalid hex string");
            return uint8_array_from_bytes(result.bytes);
        }

        value set_from_result(typed_array_object& aInto, decode_result const& aResult, char const* aWhat)
        {
            if (!aResult.bytes.empty())
                std::memcpy(aInto.element(0u), aResult.bytes.data(), aResult.bytes.size());
            if (aResult.error)
                throw_syntax_error(std::string{ "invalid " } + aWhat + " string");
            auto const result = make_object();
            create_data_property_or_throw(result, ascii("read"), number(static_cast<double>(aResult.read)));
            create_data_property_or_throw(result, ascii("written"), number(static_cast<double>(aResult.bytes.size())));
            return result;
        }

        value uint8_array_set_from_base64(value aThis, arguments aArguments, value, function_object&)
        {
            auto& into = this_uint8_array(aThis);
            auto const s = string_argument(aArguments);
            auto const options = options_object(argument(aArguments, 1u));
            bool const url = alphabet_option(options);
            auto const handling = last_chunk_handling_option(options);
            validate_bounds(into);
            auto const result = from_base64(str(s), url, handling, into.length());
            return set_from_result(into, result, "base64");
        }

        value uint8_array_set_from_hex(value aThis, arguments aArguments, value, function_object&)
        {
            auto& into = this_uint8_array(aThis);
            auto const s = string_argument(aArguments);
            validate_bounds(into);
            auto const result = from_hex(str(s), into.length());
            return set_from_result(into, result, "hex");
        }

        value uint8_array_to_base64(value aThis, arguments aArguments, value, function_object&)
        {
            auto& t = this_uint8_array(aThis);
            auto const options = options_object(argument(aArguments, 0u));
            bool const url = alphabet_option(options);
            bool const omitPadding = to_boolean(get(options, ascii("omitPadding")));
            validate_bounds(t);
            auto const length = t.length();
            auto const* bytes = t.element(0u);
            char const* const digits = url ? "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_" :
                "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
            std::u16string result;
            result.reserve((length + 2u) / 3u * 4u);
            std::size_t i = 0u;
            for (; i + 3u <= length; i += 3u)
            {
                std::uint32_t const bits = (static_cast<std::uint32_t>(bytes[i]) << 16) | (static_cast<std::uint32_t>(bytes[i + 1u]) << 8) | bytes[i + 2u];
                result.push_back(static_cast<char16_t>(digits[(bits >> 18) & 63u]));
                result.push_back(static_cast<char16_t>(digits[(bits >> 12) & 63u]));
                result.push_back(static_cast<char16_t>(digits[(bits >> 6) & 63u]));
                result.push_back(static_cast<char16_t>(digits[bits & 63u]));
            }
            if (length - i == 1u)
            {
                std::uint32_t const bits = static_cast<std::uint32_t>(bytes[i]) << 16;
                result.push_back(static_cast<char16_t>(digits[(bits >> 18) & 63u]));
                result.push_back(static_cast<char16_t>(digits[(bits >> 12) & 63u]));
                if (!omitPadding)
                    result += u"==";
            }
            else if (length - i == 2u)
            {
                std::uint32_t const bits = (static_cast<std::uint32_t>(bytes[i]) << 16) | (static_cast<std::uint32_t>(bytes[i + 1u]) << 8);
                result.push_back(static_cast<char16_t>(digits[(bits >> 18) & 63u]));
                result.push_back(static_cast<char16_t>(digits[(bits >> 12) & 63u]));
                result.push_back(static_cast<char16_t>(digits[(bits >> 6) & 63u]));
                if (!omitPadding)
                    result += u"=";
            }
            return make_string(std::move(result));
        }

        value uint8_array_to_hex(value aThis, arguments, value, function_object&)
        {
            auto& t = this_uint8_array(aThis);
            validate_bounds(t);
            auto const length = t.length();
            auto const* bytes = t.element(0u);
            static char const sDigits[] = "0123456789abcdef";
            std::u16string result;
            result.reserve(length * 2u);
            for (std::size_t i = 0u; i < length; ++i)
            {
                result.push_back(static_cast<char16_t>(sDigits[bytes[i] >> 4]));
                result.push_back(static_cast<char16_t>(sDigits[bytes[i] & 15u]));
            }
            return make_string(std::move(result));
        }

        value method_with_id(value aObject, char const* aName, std::uint32_t aLength, native_function aFunction, std::uint32_t aId)
        {
            auto const f = method(aObject, aName, aLength, aFunction);
            as_function(f)->nativeId = aId;
            return f;
        }
    }

    void init_typed_arrays(realm& aRealm)
    {
        init_buffers(aRealm);
        // %TypedArray%
        auto const ta = make_native("TypedArray", 0u, typed_array_abstract_constructor, true);
        aRealm.typedArray = ta;
        aRealm.typedArrayPrototype = make_object();
        auto const tap = aRealm.typedArrayPrototype;
        obj(ta).put(the_names().prototype, tap, 0u);
        method(ta, "from", 1u, typed_array_from);
        method(ta, "of", 0u, typed_array_of);
        getter(ta, aRealm.symSpecies, species_getter);
        obj(tap).put(the_names().constructor, ta);
        method(tap, "at", 1u, typed_array_at);
        getter(tap, ascii("buffer"), typed_array_buffer);
        getter(tap, ascii("byteLength"), typed_array_byte_length);
        getter(tap, ascii("byteOffset"), typed_array_byte_offset);
        method(tap, "copyWithin", 2u, typed_array_copy_within);
        method(tap, "entries", 0u, typed_array_entries);
        method_with_id(tap, "every", 1u, typed_array_every, 0u);
        method(tap, "fill", 1u, typed_array_fill);
        method(tap, "filter", 1u, typed_array_filter);
        method_with_id(tap, "find", 1u, typed_array_find, 0u);
        method_with_id(tap, "findIndex", 1u, typed_array_find, 1u);
        method_with_id(tap, "findLast", 1u, typed_array_find, 2u);
        method_with_id(tap, "findLastIndex", 1u, typed_array_find, 3u);
        method_with_id(tap, "forEach", 1u, typed_array_every, 2u);
        method(tap, "includes", 1u, typed_array_includes);
        method(tap, "indexOf", 1u, typed_array_index_of);
        method(tap, "join", 1u, typed_array_join);
        method(tap, "keys", 0u, typed_array_keys);
        method(tap, "lastIndexOf", 1u, typed_array_last_index_of);
        getter(tap, the_names().length, typed_array_length);
        method(tap, "map", 1u, typed_array_map);
        method_with_id(tap, "reduce", 1u, typed_array_reduce, 0u);
        method_with_id(tap, "reduceRight", 1u, typed_array_reduce, 1u);
        method(tap, "reverse", 0u, typed_array_reverse);
        method(tap, "set", 1u, typed_array_set);
        method(tap, "slice", 2u, typed_array_slice);
        method_with_id(tap, "some", 1u, typed_array_every, 1u);
        method(tap, "sort", 1u, typed_array_sort);
        method(tap, "subarray", 2u, typed_array_subarray);
        method(tap, "toLocaleString", 0u, typed_array_to_locale_string);
        method(tap, "toReversed", 0u, typed_array_to_reversed);
        method(tap, "toSorted", 1u, typed_array_to_sorted);
        if (auto const* toString = obj(aRealm.arrayPrototype).own(the_names().toString))
            obj(tap).put(the_names().toString, toString->data);
        auto const values = method(tap, "values", 0u, typed_array_values);
        method(tap, "with", 2u, typed_array_with);
        obj(tap).put(aRealm.symIterator, values);
        getter(tap, aRealm.symToStringTag, typed_array_to_string_tag);
        // the TypedArray constructors
        for (std::size_t i = 0u; i < ElementTypes; ++i)
        {
            auto const type = static_cast<element_type>(i);
            auto const name = ascii(element_type_name(type));
            auto const c = make_native(name, 3u, typed_array_constructor, true, ta);
            as_function(c)->nativeId = static_cast<std::uint32_t>(i);
            auto const p = make_object(tap);
            aRealm.typedArrayConstructors[i] = c;
            aRealm.typedArrayPrototypes[i] = p;
            auto const bytesPerElement = number(static_cast<double>(element_size(type)));
            obj(c).put(ascii("BYTES_PER_ELEMENT"), bytesPerElement, 0u);
            obj(c).put(the_names().prototype, p, 0u);
            obj(p).put(ascii("BYTES_PER_ELEMENT"), bytesPerElement, 0u);
            obj(p).put(the_names().constructor, c);
            obj(aRealm.global).put(name, c);
        }
        // Uint8Array's base64 and hex
        auto const u8 = aRealm.typedArrayConstructors[static_cast<std::size_t>(element_type::Uint8)];
        auto const u8p = aRealm.typedArrayPrototypes[static_cast<std::size_t>(element_type::Uint8)];
        method(u8, "fromBase64", 1u, uint8_array_from_base64);
        method(u8, "fromHex", 1u, uint8_array_from_hex);
        method(u8p, "setFromBase64", 1u, uint8_array_set_from_base64);
        method(u8p, "setFromHex", 1u, uint8_array_set_from_hex);
        method(u8p, "toBase64", 0u, uint8_array_to_base64);
        method(u8p, "toHex", 0u, uint8_array_to_hex);
        init_atomics(aRealm);
    }
}
