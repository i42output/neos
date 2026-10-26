/*
  builtins_array.cpp

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

// Array: the Array constructor and Array.prototype

#include <algorithm>
#include <functional>
#include "js.hpp"
#include "api.hpp"
#include "intl.hpp"

namespace neos::javascript_runtime
{
    value object_prototype_to_string_value(value aObject);

    namespace
    {
        constexpr double MaxSafeInteger = 9007199254740991.0;

        value key(double aIndex)
        {
            if (aIndex < 4294967295.0)
                return index_key(static_cast<std::uint64_t>(aIndex));
            return intern(number_to_string(aIndex));
        }

        value callback_argument(arguments aArguments, char const* aMethod)
        {
            auto const f = argument(aArguments, 0u);
            if (!is_callable(f))
                throw_type_error(describe(f) + " is not a function (in Array.prototype." + aMethod + ")");
            return f;
        }

        double relative_index(value aValue, double aLength, double aDefault)
        {
            if (aValue == Undefined)
                return aDefault;
            auto const relative = to_integer_or_infinity(aValue);
            if (relative == -std::numeric_limits<double>::infinity())
                return 0.0;
            if (relative < 0.0)
                return std::max(aLength + relative, 0.0);
            return std::min(relative, aLength);
        }

        // (ArraySpeciesCreate)
        value array_species_create(value aOriginal, double aLength)
        {
            if (!is_array(aOriginal))
            {
                if (aLength > 4294967295.0)
                    throw_range_error("Invalid array length");
                return make_array(static_cast<std::size_t>(aLength));
            }
            auto c = get(aOriginal, the_names().constructor);
            if (is_constructor(c))
            {
                // (another realm's %Array%: an array of this realm)
                auto& realmC = function_realm(c);
                if (&realmC != &the_realm() && c == realmC.array)
                    c = Undefined;
            }
            if (is_object(c))
            {
                c = get(c, the_realm().symSpecies);
                if (c == Null)
                    c = Undefined;
            }
            if (c == Undefined)
            {
                if (aLength > 4294967295.0)
                    throw_range_error("Invalid array length");
                return make_array(static_cast<std::size_t>(aLength));
            }
            if (!is_constructor(c))
                throw_type_error("object.constructor[Symbol.species] is not a constructor");
            value const argumentList[] = { number(aLength) };
            return construct(c, argumentList);
        }

        void set_length(value aObject, double aLength)
        {
            set(aObject, the_names().length, number(aLength), true);
        }

        // --- the constructor

        value array_constructor(value, arguments aArguments, value aNewTarget, function_object& aCallee)
        {
            auto const newTarget = aNewTarget == Undefined ? value_of(aCallee) : aNewTarget;
            auto const prototype = get_prototype_from_constructor(newTarget, &realm::arrayPrototype);
            auto& a = the_heap().make<array_object>(prototype);
            auto const result = value_of(a);
            if (aArguments.size() == 1u)
            {
                auto const len = aArguments[0];
                if (!is_number(len))
                {
                    a.elements.push_back(len);
                    a.length = 1u;
                }
                else
                {
                    auto const intLen = to_uint32(as_number(len));
                    if (static_cast<double>(intLen) != as_number(len))
                        throw_range_error("Invalid array length");
                    a.length = intLen;
                }
                return result;
            }
            a.elements.assign(aArguments.begin(), aArguments.end());
            a.length = static_cast<std::uint32_t>(aArguments.size());
            return result;
        }

        value array_is_array(value, arguments aArguments, value, function_object&)
        {
            return boolean(is_array(argument(aArguments, 0u)));
        }

        value array_of(value aThis, arguments aArguments, value, function_object&)
        {
            value a;
            if (is_constructor(aThis))
            {
                value const len[] = { number(static_cast<double>(aArguments.size())) };
                a = construct(aThis, len);
            }
            else
                a = make_array(aArguments.size());
            for (std::size_t k = 0u; k < aArguments.size(); ++k)
                create_data_property_or_throw(a, key(static_cast<double>(k)), aArguments[k]);
            set_length(a, static_cast<double>(aArguments.size()));
            return a;
        }

        value array_from(value aThis, arguments aArguments, value, function_object&)
        {
            auto const items = argument(aArguments, 0u);
            auto const mapfn = argument(aArguments, 1u);
            auto const thisArg = argument(aArguments, 2u);
            bool const mapping = mapfn != Undefined;
            if (mapping && !is_callable(mapfn))
                throw_type_error(describe(mapfn) + " is not a function");
            auto const usingIterator = get_method(items, the_realm().symIterator);
            if (usingIterator != Undefined)
            {
                auto const a = is_constructor(aThis) ? construct(aThis) : make_array();
                auto record = get_iterator_from_method(items, usingIterator);
                double k = 0.0;
                for (;;)
                {
                    if (k >= MaxSafeInteger)
                    {
                        iterator_close_on_throw(record);
                        throw_type_error("Array.from: too many elements");
                    }
                    auto const next = iterator_step_value(record);
                    if (!next)
                    {
                        set_length(a, k);
                        return a;
                    }
                    try
                    {
                        value mapped = *next;
                        if (mapping)
                        {
                            value const args[] = { *next, number(k) };
                            mapped = call(mapfn, thisArg, args);
                        }
                        create_data_property_or_throw(a, key(k), mapped);
                    }
                    catch (...)
                    {
                        iterator_close_on_throw(record);
                        throw;
                    }
                    ++k;
                }
            }
            auto const arrayLike = to_object(items);
            auto const len = length_of_array_like(arrayLike);
            value a;
            if (is_constructor(aThis))
            {
                value const args[] = { number(len) };
                a = construct(aThis, args);
            }
            else
                a = make_array(static_cast<std::size_t>(len));
            for (double k = 0.0; k < len; ++k)
            {
                auto v = get(arrayLike, key(k));
                if (mapping)
                {
                    value const args[] = { v, number(k) };
                    v = call(mapfn, thisArg, args);
                }
                create_data_property_or_throw(a, key(k), v);
            }
            set_length(a, len);
            return a;
        }

        value species_getter(value aThis, arguments, value, function_object&)
        {
            return aThis;
        }

        // --- Array.prototype

        value array_at(value aThis, arguments aArguments, value, function_object&)
        {
            auto const o = to_object(aThis);
            auto const len = length_of_array_like(o);
            auto const relative = to_integer_or_infinity(argument(aArguments, 0u));
            auto const k = relative >= 0.0 ? relative : len + relative;
            if (k < 0.0 || k >= len)
                return Undefined;
            return get(o, key(k));
        }

        bool is_concat_spreadable(value aObject)
        {
            if (!is_object(aObject))
                return false;
            auto const spreadable = get(aObject, the_realm().symIsConcatSpreadable);
            if (spreadable != Undefined)
                return to_boolean(spreadable);
            return is_array(aObject);
        }

        value array_concat(value aThis, arguments aArguments, value, function_object&)
        {
            auto const o = to_object(aThis);
            auto const a = array_species_create(o, 0.0);
            double n = 0.0;
            value_list items;
            items.push_back(o);
            items.insert(items.end(), aArguments.begin(), aArguments.end());
            for (auto const e : items)
            {
                if (is_concat_spreadable(e))
                {
                    auto const len = length_of_array_like(e);
                    if (n + len > MaxSafeInteger)
                        throw_type_error("Array.prototype.concat: too many elements");
                    for (double k = 0.0; k < len; ++k, ++n)
                    {
                        auto const p = key(k);
                        if (has_property(e, p))
                            create_data_property_or_throw(a, key(n), get(e, p));
                    }
                }
                else
                {
                    if (n >= MaxSafeInteger)
                        throw_type_error("Array.prototype.concat: too many elements");
                    create_data_property_or_throw(a, key(n), e);
                    ++n;
                }
            }
            set_length(a, n);
            return a;
        }

        value array_copy_within(value aThis, arguments aArguments, value, function_object&)
        {
            auto const o = to_object(aThis);
            auto const len = length_of_array_like(o);
            auto to = relative_index(argument(aArguments, 0u), len, 0.0);
            auto from = relative_index(argument(aArguments, 1u), len, 0.0);
            auto const final_ = relative_index(argument(aArguments, 2u), len, len);
            auto count = std::min(final_ - from, len - to);
            double direction = 1.0;
            if (from < to && to < from + count)
            {
                direction = -1.0;
                from = from + count - 1.0;
                to = to + count - 1.0;
            }
            while (count > 0.0)
            {
                auto const fromKey = key(from);
                auto const toKey = key(to);
                if (has_property(o, fromKey))
                    set(o, toKey, get(o, fromKey), true);
                else
                    delete_property_or_throw(o, toKey);
                from += direction;
                to += direction;
                --count;
            }
            return o;
        }

        value array_entries(value aThis, arguments, value, function_object&)
        {
            return make_array_iterator(to_object(aThis), 2);
        }

        value array_keys(value aThis, arguments, value, function_object&)
        {
            return make_array_iterator(to_object(aThis), 0);
        }

        value array_values(value aThis, arguments, value, function_object&)
        {
            return make_array_iterator(to_object(aThis), 1);
        }

        // (every, some, forEach, map, filter: a callback for each element that exists)
        value iterate(value aThis, arguments aArguments, char const* aMethod, int aKind)
        {
            auto const o = to_object(aThis);
            auto const len = length_of_array_like(o);
            auto const f = callback_argument(aArguments, aMethod);
            auto const thisArg = argument(aArguments, 1u);
            value a = Undefined;
            if (aKind == 3)
                a = array_species_create(o, len);
            else if (aKind == 4)
                a = array_species_create(o, 0.0);
            double to = 0.0;
            for (double k = 0.0; k < len; ++k)
            {
                auto const pk = key(k);
                if (!has_property(o, pk))
                    continue;
                auto const kValue = get(o, pk);
                value const args[] = { kValue, number(k), o };
                auto const result = call(f, thisArg, args);
                switch (aKind)
                {
                case 0: // (every)
                    if (!to_boolean(result))
                        return False;
                    break;
                case 1: // (some)
                    if (to_boolean(result))
                        return True;
                    break;
                case 3: // (map)
                    create_data_property_or_throw(a, pk, result);
                    break;
                case 4: // (filter)
                    if (to_boolean(result))
                        create_data_property_or_throw(a, key(to++), kValue);
                    break;
                default:
                    break;
                }
            }
            switch (aKind)
            {
            case 0: return True;
            case 1: return False;
            case 3: case 4: return a;
            default: return Undefined;
            }
        }

        value array_every(value aThis, arguments aArguments, value, function_object&) { return iterate(aThis, aArguments, "every", 0); }
        value array_some(value aThis, arguments aArguments, value, function_object&) { return iterate(aThis, aArguments, "some", 1); }
        value array_for_each(value aThis, arguments aArguments, value, function_object&) { return iterate(aThis, aArguments, "forEach", 2); }
        value array_map(value aThis, arguments aArguments, value, function_object&) { return iterate(aThis, aArguments, "map", 3); }
        value array_filter(value aThis, arguments aArguments, value, function_object&) { return iterate(aThis, aArguments, "filter", 4); }

        value array_fill(value aThis, arguments aArguments, value, function_object&)
        {
            auto const o = to_object(aThis);
            auto const len = length_of_array_like(o);
            auto const v = argument(aArguments, 0u);
            auto k = relative_index(argument(aArguments, 1u), len, 0.0);
            auto const final_ = relative_index(argument(aArguments, 2u), len, len);
            for (; k < final_; ++k)
                set(o, key(k), v, true);
            return o;
        }

        // (find, findIndex, findLast, findLastIndex)
        value find(value aThis, arguments aArguments, char const* aMethod, bool aLast, bool aIndex)
        {
            auto const o = to_object(aThis);
            auto const len = length_of_array_like(o);
            auto const predicate = callback_argument(aArguments, aMethod);
            auto const thisArg = argument(aArguments, 1u);
            for (double i = 0.0; i < len; ++i)
            {
                double const k = aLast ? len - 1.0 - i : i;
                auto const kValue = get(o, key(k));
                value const args[] = { kValue, number(k), o };
                if (to_boolean(call(predicate, thisArg, args)))
                    return aIndex ? number(k) : kValue;
            }
            return aIndex ? number(-1.0) : Undefined;
        }

        value array_find(value aThis, arguments aArguments, value, function_object&) { return find(aThis, aArguments, "find", false, false); }
        value array_find_index(value aThis, arguments aArguments, value, function_object&) { return find(aThis, aArguments, "findIndex", false, true); }
        value array_find_last(value aThis, arguments aArguments, value, function_object&) { return find(aThis, aArguments, "findLast", true, false); }
        value array_find_last_index(value aThis, arguments aArguments, value, function_object&) { return find(aThis, aArguments, "findLastIndex", true, true); }

        double flatten_into_array(value aTarget, value aSource, double aSourceLength, double aStart, double aDepth, value aMapper = Undefined, value aThis = Undefined)
        {
            auto targetIndex = aStart;
            for (double sourceIndex = 0.0; sourceIndex < aSourceLength; ++sourceIndex)
            {
                auto const p = key(sourceIndex);
                if (!has_property(aSource, p))
                    continue;
                auto element = get(aSource, p);
                if (aMapper != Undefined)
                {
                    value const args[] = { element, number(sourceIndex), aSource };
                    element = call(aMapper, aThis, args);
                }
                bool shouldFlatten = false;
                if (aDepth > 0.0)
                    shouldFlatten = is_array(element);
                if (shouldFlatten)
                {
                    auto const newDepth = aDepth == std::numeric_limits<double>::infinity() ? aDepth : aDepth - 1.0;
                    targetIndex = flatten_into_array(aTarget, element, length_of_array_like(element), targetIndex, newDepth);
                }
                else
                {
                    if (targetIndex >= MaxSafeInteger)
                        throw_type_error("Array.prototype.flat: too many elements");
                    create_data_property_or_throw(aTarget, key(targetIndex), element);
                    ++targetIndex;
                }
            }
            return targetIndex;
        }

        value array_flat(value aThis, arguments aArguments, value, function_object&)
        {
            auto const o = to_object(aThis);
            auto const sourceLen = length_of_array_like(o);
            double depthNum = 1.0;
            if (argument(aArguments, 0u) != Undefined)
            {
                depthNum = to_integer_or_infinity(argument(aArguments, 0u));
                if (depthNum < 0.0)
                    depthNum = 0.0;
            }
            auto const a = array_species_create(o, 0.0);
            flatten_into_array(a, o, sourceLen, 0.0, depthNum);
            return a;
        }

        value array_flat_map(value aThis, arguments aArguments, value, function_object&)
        {
            auto const o = to_object(aThis);
            auto const sourceLen = length_of_array_like(o);
            auto const mapper = callback_argument(aArguments, "flatMap");
            auto const a = array_species_create(o, 0.0);
            flatten_into_array(a, o, sourceLen, 0.0, 1.0, mapper, argument(aArguments, 1u));
            return a;
        }

        value array_includes(value aThis, arguments aArguments, value, function_object&)
        {
            auto const o = to_object(aThis);
            auto const len = length_of_array_like(o);
            if (len == 0.0)
                return False;
            auto n = to_integer_or_infinity(argument(aArguments, 1u));
            if (n == std::numeric_limits<double>::infinity())
                return False;
            if (n == -std::numeric_limits<double>::infinity())
                n = 0.0;
            double k = n >= 0.0 ? n : std::max(len + n, 0.0);
            auto const target = argument(aArguments, 0u);
            for (; k < len; ++k)
                if (same_value_zero(get(o, key(k)), target))
                    return True;
            return False;
        }

        value array_index_of(value aThis, arguments aArguments, value, function_object&)
        {
            auto const o = to_object(aThis);
            auto const len = length_of_array_like(o);
            if (len == 0.0)
                return number(-1.0);
            auto n = to_integer_or_infinity(argument(aArguments, 1u));
            if (n == std::numeric_limits<double>::infinity())
                return number(-1.0);
            if (n == -std::numeric_limits<double>::infinity())
                n = 0.0;
            double k = n >= 0.0 ? n : std::max(len + n, 0.0);
            auto const target = argument(aArguments, 0u);
            for (; k < len; ++k)
            {
                auto const p = key(k);
                if (has_property(o, p) && strictly_equal(get(o, p), target))
                    return number(k);
            }
            return number(-1.0);
        }

        value array_last_index_of(value aThis, arguments aArguments, value, function_object&)
        {
            auto const o = to_object(aThis);
            auto const len = length_of_array_like(o);
            if (len == 0.0)
                return number(-1.0);
            double n = aArguments.size() > 1u ? to_integer_or_infinity(aArguments[1]) : len - 1.0;
            if (n == -std::numeric_limits<double>::infinity())
                return number(-1.0);
            double k = n >= 0.0 ? std::min(n, len - 1.0) : len + n;
            auto const target = argument(aArguments, 0u);
            for (; k >= 0.0; --k)
            {
                auto const p = key(k);
                if (has_property(o, p) && strictly_equal(get(o, p), target))
                    return number(k);
            }
            return number(-1.0);
        }

        value array_join(value aThis, arguments aArguments, value, function_object&)
        {
            auto const o = to_object(aThis);
            auto const len = length_of_array_like(o);
            auto const separator = argument(aArguments, 0u);
            std::u16string const sep = separator == Undefined ? std::u16string{ u"," } : std::u16string{ str(to_string(separator)) };
            // (cycles: an array being joined joins as the empty string)
            static thread_local value_list tJoining;
            if (std::find(tJoining.begin(), tJoining.end(), o) != tJoining.end())
                return the_names().empty;
            tJoining.push_back(o);
            std::u16string result;
            try
            {
                for (double k = 0.0; k < len; ++k)
                {
                    if (k > 0.0)
                        result += sep;
                    auto const element = get(o, key(k));
                    if (!is_nullish(element))
                        result += str(to_string(element));
                    if (result.size() > MaxStringLength)
                        throw_range_error("Invalid string length");
                }
            }
            catch (...)
            {
                tJoining.pop_back();
                throw;
            }
            tJoining.pop_back();
            return make_string(std::move(result));
        }

        value array_to_string(value aThis, arguments, value, function_object&)
        {
            auto const o = to_object(aThis);
            auto const f = get(o, the_names().join);
            if (!is_callable(f))
                return object_prototype_to_string_value(o);
            return call(f, o);
        }

        value array_to_locale_string(value aThis, arguments aArguments, value, function_object&)
        {
            // (ECMA-402's: the elements' toLocaleString with the locales and options; ECMA-262's: with none)
#if defined(NEOS_JS_ICU)
            value const icuArguments[] = { argument(aArguments, 0u), argument(aArguments, 1u) };
            arguments const localeArguments = intl::icu_available() ? arguments{ icuArguments } : arguments{};
#else
            (void)aArguments;
            arguments const localeArguments{};
#endif
            auto const o = to_object(aThis);
            auto const len = length_of_array_like(o);
            std::u16string result;
            for (double k = 0.0; k < len; ++k)
            {
                if (k > 0.0)
                    result += u",";
                auto const element = get(o, key(k));
                if (!is_nullish(element))
                    result += str(to_string(invoke(element, ascii("toLocaleString"), localeArguments)));
            }
            return make_string(std::move(result));
        }

        value array_pop(value aThis, arguments, value, function_object&)
        {
            auto const o = to_object(aThis);
            auto const len = length_of_array_like(o);
            if (len == 0.0)
            {
                set_length(o, 0.0);
                return Undefined;
            }
            auto const index = key(len - 1.0);
            auto const element = get(o, index);
            delete_property_or_throw(o, index);
            set_length(o, len - 1.0);
            return element;
        }

        // (Array.prototype and Object.prototype have no index properties (that a plain array's [[Set]] of an index would find))
        bool plain_prototypes()
        {
            return plain_array_prototypes();
        }
    }

    bool plain_array_prototypes()
    {
        {
            auto const& r = the_realm();
            auto* ap = as_array(r.arrayPrototype);
            if (ap == nullptr || ap->sparse || !ap->elements.empty() || obj(r.arrayPrototype).proto != r.objectPrototype)
                return false;
            struct plain_cache
            {
                std::size_t entries = static_cast<std::size_t>(-1);
                bool plain = false;
            };
            static isolate_local<plain_cache> sCache;
            auto& cache = *sCache;
            auto const& properties = obj(r.objectPrototype).properties;
            if (properties.slots() != cache.entries)
            {
                cache.entries = properties.slots();
                cache.plain = true;
                properties.for_each([&](property const& e)
                {
                    if (is_string(e.key) && array_index(e.key))
                        cache.plain = false;
                });
            }
            return cache.plain;
        }
    }

    namespace
    {
        value array_push(value aThis, arguments aArguments, value, function_object&)
        {
            auto const o = to_object(aThis);
            if (auto* a = as_array(o); a != nullptr && !a->sparse && a->extensible && a->lengthWritable && a->elements.size() == a->length &&
                a->length + aArguments.size() < 0xFFFFFFFFull && a->proto == the_realm().arrayPrototype && plain_prototypes())
            {
                // (a plain array: its elements)
                a->elements.insert(a->elements.end(), aArguments.begin(), aArguments.end());
                a->length = static_cast<std::uint32_t>(a->elements.size());
                return number(a->length);
            }
            auto len = length_of_array_like(o);
            if (len + static_cast<double>(aArguments.size()) > MaxSafeInteger)
                throw_type_error("Pushing " + std::to_string(aArguments.size()) + " elements on an array-like of length " + std::to_string(len) + " is disallowed");
            for (auto const e : aArguments)
            {
                set(o, key(len), e, true);
                ++len;
            }
            set_length(o, len);
            return number(len);
        }

        value reduce(value aThis, arguments aArguments, bool aRight)
        {
            auto const o = to_object(aThis);
            auto const len = length_of_array_like(o);
            auto const f = callback_argument(aArguments, aRight ? "reduceRight" : "reduce");
            if (len == 0.0 && aArguments.size() < 2u)
                throw_type_error("Reduce of empty array with no initial value");
            double k = 0.0;
            auto const index = [&](double aK) { return aRight ? len - 1.0 - aK : aK; };
            value accumulator = Undefined;
            if (aArguments.size() >= 2u)
                accumulator = aArguments[1];
            else
            {
                bool present = false;
                for (; !present && k < len; ++k)
                {
                    auto const p = key(index(k));
                    present = has_property(o, p);
                    if (present)
                        accumulator = get(o, p);
                }
                if (!present)
                    throw_type_error("Reduce of empty array with no initial value");
            }
            for (; k < len; ++k)
            {
                auto const p = key(index(k));
                if (has_property(o, p))
                {
                    value const args[] = { accumulator, get(o, p), number(index(k)), o };
                    accumulator = call(f, Undefined, args);
                }
            }
            return accumulator;
        }

        value array_reduce(value aThis, arguments aArguments, value, function_object&) { return reduce(aThis, aArguments, false); }
        value array_reduce_right(value aThis, arguments aArguments, value, function_object&) { return reduce(aThis, aArguments, true); }

        value array_reverse(value aThis, arguments, value, function_object&)
        {
            auto const o = to_object(aThis);
            auto const len = length_of_array_like(o);
            auto const middle = std::floor(len / 2.0);
            for (double lower = 0.0; lower != middle; ++lower)
            {
                auto const upper = len - lower - 1.0;
                auto const upperP = key(upper);
                auto const lowerP = key(lower);
                bool const lowerExists = has_property(o, lowerP);
                value lowerValue = Undefined, upperValue = Undefined;
                if (lowerExists)
                    lowerValue = get(o, lowerP);
                bool const upperExists = has_property(o, upperP);
                if (upperExists)
                    upperValue = get(o, upperP);
                if (lowerExists && upperExists)
                {
                    set(o, lowerP, upperValue, true);
                    set(o, upperP, lowerValue, true);
                }
                else if (!lowerExists && upperExists)
                {
                    set(o, lowerP, upperValue, true);
                    delete_property_or_throw(o, upperP);
                }
                else if (lowerExists && !upperExists)
                {
                    delete_property_or_throw(o, lowerP);
                    set(o, upperP, lowerValue, true);
                }
            }
            return o;
        }

        value array_shift(value aThis, arguments, value, function_object&)
        {
            auto const o = to_object(aThis);
            auto const len = length_of_array_like(o);
            if (len == 0.0)
            {
                set_length(o, 0.0);
                return Undefined;
            }
            auto const first = get(o, key(0.0));
            if (auto* a = as_array(o); a != nullptr && !a->sparse && a->elements.size() == a->length && a->lengthWritable &&
                std::find(a->elements.begin(), a->elements.end(), Empty) == a->elements.end() && a->proto == the_realm().arrayPrototype)
            {
                a->elements.erase(a->elements.begin());
                --a->length;
                return first;
            }
            for (double k = 1.0; k < len; ++k)
            {
                auto const from = key(k);
                auto const to = key(k - 1.0);
                if (has_property(o, from))
                    set(o, to, get(o, from), true);
                else
                    delete_property_or_throw(o, to);
            }
            delete_property_or_throw(o, key(len - 1.0));
            set_length(o, len - 1.0);
            return first;
        }

        value array_unshift(value aThis, arguments aArguments, value, function_object&)
        {
            auto const o = to_object(aThis);
            auto const len = length_of_array_like(o);
            auto const argCount = static_cast<double>(aArguments.size());
            if (argCount > 0.0)
            {
                if (len + argCount > MaxSafeInteger)
                    throw_type_error("Unshift: too many elements");
                for (double k = len; k > 0.0; --k)
                {
                    auto const from = key(k - 1.0);
                    auto const to = key(k + argCount - 1.0);
                    if (has_property(o, from))
                        set(o, to, get(o, from), true);
                    else
                        delete_property_or_throw(o, to);
                }
                for (std::size_t j = 0u; j < aArguments.size(); ++j)
                    set(o, key(static_cast<double>(j)), aArguments[j], true);
            }
            set_length(o, len + argCount);
            return number(len + argCount);
        }

        value array_slice(value aThis, arguments aArguments, value, function_object&)
        {
            auto const o = to_object(aThis);
            auto const len = length_of_array_like(o);
            auto k = relative_index(argument(aArguments, 0u), len, 0.0);
            auto const final_ = relative_index(argument(aArguments, 1u), len, len);
            auto const count = std::max(final_ - k, 0.0);
            auto const a = array_species_create(o, count);
            // (a plain array's slice made by %Array%: its elements (a hole is one in the slice too))
            if (auto* const source = as_array(o), *const result = as_array(a); source != nullptr && result != nullptr && source != result &&
                !source->sparse && !result->sparse && source->proto == the_realm().arrayPrototype && result->proto == the_realm().arrayPrototype &&
                result->extensible && result->lengthWritable && result->elements.empty() && result->properties.size() == 0u && 
                result->length == static_cast<std::uint32_t>(count) && plain_prototypes())
            {
                auto const first = static_cast<std::size_t>(k);
                auto const last = std::min(static_cast<std::size_t>(final_), static_cast<std::size_t>(source->elements.size()));
                if (first < last)
                    result->elements.assign(source->elements.begin() + static_cast<std::ptrdiff_t>(first), source->elements.begin() + static_cast<std::ptrdiff_t>(last));
                return a;
            }
            double n = 0.0;
            for (; k < final_; ++k, ++n)
            {
                auto const pk = key(k);
                if (has_property(o, pk))
                    create_data_property_or_throw(a, key(n), get(o, pk));
            }
            set_length(a, n);
            return a;
        }

        // (SortCompare: undefined last; a comparator, or the elements' strings)
        bool sort_less(value aComparator, value x, value y)
        {
            if (x == Undefined && y == Undefined)
                return false;
            if (x == Undefined)
                return false;
            if (y == Undefined)
                return true;
            if (aComparator != Undefined)
            {
                value const args[] = { x, y };
                auto const v = to_number(call(aComparator, Undefined, args));
                return v < 0.0;
            }
            if (is_number(x) && is_number(y))
            {
                // (as their strings compare)
                return str(to_string(x)) < str(to_string(y));
            }
            return str(to_string(x)) < str(to_string(y));
        }

        void sort_values(value_list& aValues, value aComparator)
        {
            // (a stable merge sort: the comparator can be inconsistent)
            value_list buffer(aValues.size());
            std::function<void(std::size_t, std::size_t)> sort = [&](std::size_t aLow, std::size_t aHigh)
            {
                if (aHigh - aLow < 2u)
                    return;
                auto const middle = aLow + (aHigh - aLow) / 2u;
                sort(aLow, middle);
                sort(middle, aHigh);
                std::size_t i = aLow, j = middle, k = aLow;
                while (i < middle && j < aHigh)
                    buffer[k++] = sort_less(aComparator, aValues[j], aValues[i]) ? aValues[j++] : aValues[i++];
                while (i < middle)
                    buffer[k++] = aValues[i++];
                while (j < aHigh)
                    buffer[k++] = aValues[j++];
                std::copy(buffer.begin() + static_cast<std::ptrdiff_t>(aLow), buffer.begin() + static_cast<std::ptrdiff_t>(aHigh),
                    aValues.begin() + static_cast<std::ptrdiff_t>(aLow));
            };
            sort(0u, aValues.size());
        }

        value_list sort_indexed_properties(value aObject, double aLength, value aComparator, bool aSkipHoles)
        {
            value_list items;
            for (double k = 0.0; k < aLength; ++k)
            {
                auto const pk = key(k);
                if (aSkipHoles)
                {
                    if (has_property(aObject, pk))
                        items.push_back(get(aObject, pk));
                }
                else
                    items.push_back(get(aObject, pk));
            }
            sort_values(items, aComparator);
            return items;
        }

        value array_sort(value aThis, arguments aArguments, value, function_object&)
        {
            auto const comparator = argument(aArguments, 0u);
            if (comparator != Undefined && !is_callable(comparator))
                throw_type_error("The comparison function must be either a function or undefined");
            auto const o = to_object(aThis);
            auto const len = length_of_array_like(o);
            auto const sorted = sort_indexed_properties(o, len, comparator, true);
            double j = 0.0;
            for (; j < static_cast<double>(sorted.size()); ++j)
                set(o, key(j), sorted[static_cast<std::size_t>(j)], true);
            for (; j < len; ++j)
                delete_property_or_throw(o, key(j));
            return o;
        }

        value array_splice(value aThis, arguments aArguments, value, function_object&)
        {
            auto const o = to_object(aThis);
            auto const len = length_of_array_like(o);
            auto const actualStart = relative_index(argument(aArguments, 0u), len, 0.0);
            double itemCount = 0.0;
            double actualDeleteCount = 0.0;
            if (aArguments.empty())
                actualDeleteCount = 0.0;
            else if (aArguments.size() == 1u)
                actualDeleteCount = len - actualStart;
            else
            {
                itemCount = static_cast<double>(aArguments.size() - 2u);
                auto const dc = to_integer_or_infinity(aArguments[1]);
                actualDeleteCount = std::clamp(dc, 0.0, len - actualStart);
            }
            if (len + itemCount - actualDeleteCount > MaxSafeInteger)
                throw_type_error("Splice: too many elements");
            auto const a = array_species_create(o, actualDeleteCount);
            for (double k = 0.0; k < actualDeleteCount; ++k)
            {
                auto const from = key(actualStart + k);
                if (has_property(o, from))
                    create_data_property_or_throw(a, key(k), get(o, from));
            }
            set_length(a, actualDeleteCount);
            if (itemCount < actualDeleteCount)
            {
                for (double k = actualStart; k < len - actualDeleteCount; ++k)
                {
                    auto const from = key(k + actualDeleteCount);
                    auto const to = key(k + itemCount);
                    if (has_property(o, from))
                        set(o, to, get(o, from), true);
                    else
                        delete_property_or_throw(o, to);
                }
                for (double k = len; k > len - actualDeleteCount + itemCount; --k)
                    delete_property_or_throw(o, key(k - 1.0));
            }
            else if (itemCount > actualDeleteCount)
            {
                for (double k = len - actualDeleteCount; k > actualStart; --k)
                {
                    auto const from = key(k + actualDeleteCount - 1.0);
                    auto const to = key(k + itemCount - 1.0);
                    if (has_property(o, from))
                        set(o, to, get(o, from), true);
                    else
                        delete_property_or_throw(o, to);
                }
            }
            for (std::size_t i = 2u; i < aArguments.size(); ++i)
                set(o, key(actualStart + static_cast<double>(i - 2u)), aArguments[i], true);
            set_length(o, len - actualDeleteCount + itemCount);
            return a;
        }

        value array_to_reversed(value aThis, arguments, value, function_object&)
        {
            auto const o = to_object(aThis);
            auto const len = length_of_array_like(o);
            if (len > 4294967295.0)
                throw_range_error("Invalid array length");
            value_list elements;
            for (double k = 0.0; k < len; ++k)
                elements.push_back(get(o, key(len - k - 1.0)));
            return make_array(std::move(elements));
        }

        value array_to_sorted(value aThis, arguments aArguments, value, function_object&)
        {
            auto const comparator = argument(aArguments, 0u);
            if (comparator != Undefined && !is_callable(comparator))
                throw_type_error("The comparison function must be either a function or undefined");
            auto const o = to_object(aThis);
            auto const len = length_of_array_like(o);
            if (len > 4294967295.0)
                throw_range_error("Invalid array length");
            auto sorted = sort_indexed_properties(o, len, comparator, false);
            return make_array(std::move(sorted));
        }

        value array_to_spliced(value aThis, arguments aArguments, value, function_object&)
        {
            auto const o = to_object(aThis);
            auto const len = length_of_array_like(o);
            auto const actualStart = relative_index(argument(aArguments, 0u), len, 0.0);
            double insertCount = 0.0;
            double actualSkipCount = 0.0;
            if (aArguments.empty())
                actualSkipCount = 0.0;
            else if (aArguments.size() == 1u)
                actualSkipCount = len - actualStart;
            else
            {
                insertCount = static_cast<double>(aArguments.size() - 2u);
                actualSkipCount = std::clamp(to_integer_or_infinity(aArguments[1]), 0.0, len - actualStart);
            }
            auto const newLen = len + insertCount - actualSkipCount;
            if (newLen > MaxSafeInteger)
                throw_type_error("Invalid array length");
            if (newLen > 4294967295.0)
                throw_range_error("Invalid array length");
            value_list elements;
            double i = 0.0;
            for (; i < actualStart; ++i)
                elements.push_back(get(o, key(i)));
            for (std::size_t j = 2u; j < aArguments.size(); ++j)
                elements.push_back(aArguments[j]);
            for (double r = actualStart + actualSkipCount; r < len; ++r)
                elements.push_back(get(o, key(r)));
            return make_array(std::move(elements));
        }

        value array_with(value aThis, arguments aArguments, value, function_object&)
        {
            auto const o = to_object(aThis);
            auto const len = length_of_array_like(o);
            auto const relative = to_integer_or_infinity(argument(aArguments, 0u));
            auto const actual = relative >= 0.0 ? relative : len + relative;
            if (actual >= len || actual < 0.0)
                throw_range_error("Invalid index");
            if (len > 4294967295.0)
                throw_range_error("Invalid array length");
            value_list elements;
            for (double k = 0.0; k < len; ++k)
                elements.push_back(k == actual ? argument(aArguments, 1u) : get(o, key(k)));
            return make_array(std::move(elements));
        }
    }

    void init_array(realm& aRealm)
    {
        auto& p = the_heap().make<array_object>(aRealm.objectPrototype);
        aRealm.arrayPrototype = value_of(p);
        auto const c = make_native("Array", 1u, array_constructor, true);
        aRealm.array = c;
        obj(c).put(the_names().prototype, aRealm.arrayPrototype, 0u);
        p.put(the_names().constructor, c);
        method(c, "from", 1u, array_from);
        method(c, "isArray", 1u, array_is_array);
        method(c, "of", 0u, array_of);
        getter(c, aRealm.symSpecies, species_getter);
        auto const pr = aRealm.arrayPrototype;
        method(pr, "at", 1u, array_at);
        method(pr, "concat", 1u, array_concat);
        method(pr, "copyWithin", 2u, array_copy_within);
        method(pr, "entries", 0u, array_entries);
        method(pr, "every", 1u, array_every);
        method(pr, "fill", 1u, array_fill);
        method(pr, "filter", 1u, array_filter);
        method(pr, "find", 1u, array_find);
        method(pr, "findIndex", 1u, array_find_index);
        method(pr, "findLast", 1u, array_find_last);
        method(pr, "findLastIndex", 1u, array_find_last_index);
        method(pr, "flat", 0u, array_flat);
        method(pr, "flatMap", 1u, array_flat_map);
        method(pr, "forEach", 1u, array_for_each);
        method(pr, "includes", 1u, array_includes);
        method(pr, "indexOf", 1u, array_index_of);
        method(pr, "join", 1u, array_join);
        method(pr, "keys", 0u, array_keys);
        method(pr, "lastIndexOf", 1u, array_last_index_of);
        method(pr, "map", 1u, array_map);
        method(pr, "pop", 0u, array_pop);
        method(pr, "push", 1u, array_push);
        method(pr, "reduce", 1u, array_reduce);
        method(pr, "reduceRight", 1u, array_reduce_right);
        method(pr, "reverse", 0u, array_reverse);
        method(pr, "shift", 0u, array_shift);
        method(pr, "slice", 2u, array_slice);
        method(pr, "some", 1u, array_some);
        method(pr, "sort", 1u, array_sort);
        method(pr, "splice", 2u, array_splice);
        method(pr, "toLocaleString", 0u, array_to_locale_string);
        method(pr, "toReversed", 0u, array_to_reversed);
        method(pr, "toSorted", 1u, array_to_sorted);
        method(pr, "toSpliced", 2u, array_to_spliced);
        method(pr, "toString", 0u, array_to_string);
        method(pr, "unshift", 1u, array_unshift);
        auto const values = method(pr, "values", 0u, array_values);
        method(pr, "with", 2u, array_with);
        aRealm.arrayProtoValues = values;
        p.put(aRealm.symIterator, values);
        auto const unscopables = make_object(Null);
        for (auto const name : { "at", "copyWithin", "entries", "fill", "find", "findIndex", "findLast", "findLastIndex", "flat", "flatMap",
            "includes", "keys", "toReversed", "toSorted", "toSpliced", "values" })
            obj(unscopables).put(ascii(name), True, AttributesDefault);
        p.put(aRealm.symUnscopables, unscopables, AttributeConfigurable);
        obj(aRealm.global).put(ascii("Array"), c);
    }
}
