/*
  array.cpp

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

  Portions of this file are derived from CPython 3.13 (Modules/arraymodule.c), Copyright (c) 2001-2024 Python Software
  Foundation; All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from C to C++ for the neos Python runtime plugin and adapted to its
  object model (see runtime/python/NOTICE).
*/

// The module array (CPython 3.13's arraymodule.c: array.array, its behaviour and messages, its buffer (memoryview) and pickling
// (_array_reconstructor))

#include <algorithm>
#include <bit>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <vector>
#include "objects.hpp"

namespace neos::python_runtime
{
    namespace detail
    {
        namespace
        {
            struct array_descriptor
            {
                char code;
                std::size_t size;
                char const* format;     ///< (its buffer's)
                bool integer;
                bool isSigned;
            };

            // (as CPython's descriptors: their sizes are the C types' ('u': wchar_t; 'l' and 'L': long))
            array_descriptor const sDescriptors[] =
            {
                { 'b', 1u, "b", true, true },
                { 'B', 1u, "B", true, false },
                { 'u', sizeof(wchar_t), sizeof(wchar_t) == 4u ? "w" : "u", false, false },
                { 'w', 4u, "w", false, false },
                { 'h', sizeof(short), "h", true, true },
                { 'H', sizeof(short), "H", true, false },
                { 'i', sizeof(int), "i", true, true },
                { 'I', sizeof(int), "I", true, false },
                { 'l', sizeof(long), "l", true, true },
                { 'L', sizeof(long), "L", true, false },
                { 'q', sizeof(long long), "q", true, true },
                { 'Q', sizeof(long long), "Q", true, false },
                { 'f', sizeof(float), "f", false, false },
                { 'd', sizeof(double), "d", false, false }
            };

            array_descriptor const* descriptor_of(char32_t aCode)
            {
                for (auto const& d : sDescriptors)
                    if (static_cast<char32_t>(d.code) == aCode)
                        return &d;
                return nullptr;
            }

            struct array_object
            {
                header h;
                array_descriptor const* descriptor;
                std::string data;
                long long exports = 0;      ///< (the memoryviews of it)
                std::size_t size() const { return data.size() / descriptor->size; }
                char* item(std::size_t aIndex) { return data.data() + aIndex * descriptor->size; }
            };

            struct array_iterator_object
            {
                header h;
                object* array;              ///< (null: exhausted)
                std::size_t index = 0u;
                ~array_iterator_object() { release(array); }
            };

            neos_py_type make_array_type();
            neos_py_type make_array_iterator_type();
            neos_py_type sArrayType = make_array_type();
            neos_py_type sArrayIteratorType = make_array_iterator_type();

            bool is_array(object* aObject) { return aObject != nullptr && aObject->type == &sArrayType.base; }
            array_object& as_array(object* aObject) { return *reinterpret_cast<array_object*>(aObject); }

            object* make_array(array_descriptor const* aDescriptor, std::string aData = {})
            {
                return &(new array_object{ { { &sArrayType.base } }, aDescriptor, std::move(aData) })->h.base;
            }

            bool resizable(array_object const& aArray, std::size_t aSize)
            {
                if (aArray.exports > 0 && aSize != aArray.size())
                {
                    error("BufferError", "cannot resize an array that is exporting buffers");
                    return false;
                }
                return true;
            }

            bool resize(array_object& aArray, std::size_t aSize)
            {
                if (!resizable(aArray, aSize))
                    return false;
                aArray.data.resize(aSize * aArray.descriptor->size);
                return true;
            }

            // --- items

            object* code_point_item(char32_t aCharacter)
            {
                if (aCharacter > 0x10FFFFu)
                    return error("ValueError", "chr() arg not in range(0x110000)");
                std::string text;
                append_code_point(text, aCharacter);
                return make_str(std::move(text));
            }

            object* get_item(array_object& aArray, std::size_t aIndex)
            {
                char const* const p = aArray.item(aIndex);
                auto const get = [p](auto aValue) { std::memcpy(&aValue, p, sizeof aValue); return aValue; };
                switch (aArray.descriptor->code)
                {
                case 'b': return make_int(static_cast<signed char>(*p));
                case 'B': return make_int(static_cast<unsigned char>(*p));
                case 'u': return code_point_item(static_cast<char32_t>(get(wchar_t{})));
                case 'w': return code_point_item(get(char32_t{}));
                case 'h': return make_int(get(short{}));
                case 'H': return make_int(get(static_cast<unsigned short>(0u)));
                case 'i': return make_int(get(int{}));
                case 'I': return make_unsigned(get(0u));
                case 'l': return make_int(get(0l));
                case 'L': return make_unsigned(get(0ul));
                case 'q': return make_int(get(0ll));
                case 'Q': return make_unsigned(get(0ull));
                case 'f': return make_float(get(0.0f));
                default: return make_float(get(0.0));
                }
            }

            bool overflow(char const* aMessage) { return (error("OverflowError", aMessage), false); }

            // (an item's value as CPython's setitem functions convert it: aItem (null: only checked))
            bool set_item(array_descriptor const& aDescriptor, char* aItem, object* aValue)
            {
                auto const put = [aItem](auto aValue) { if (aItem != nullptr) std::memcpy(aItem, &aValue, sizeof aValue); return true; };
                auto const code = aDescriptor.code;
                if (code == 'f' || code == 'd')
                {
                    double d = 0.0;
                    if (!to_double(aValue, d))
                        return false;
                    return code == 'f' ? put(static_cast<float>(d)) : put(d);
                }
                if (code == 'u' || code == 'w')
                {
                    auto const text = str_of_value(builtin_value(aValue));
                    std::u32string characters;
                    if (text != nullptr)
                        characters = to_code_points(*text);
                    if (text == nullptr || characters.size() != 1u || (code == 'u' && sizeof(wchar_t) == 2u && characters[0] > 0xFFFFu))
                    {
                        error("TypeError", "array item must be unicode character");
                        return false;
                    }
                    return code == 'u' ? put(static_cast<wchar_t>(characters[0])) : put(characters[0]);
                }
                long long s = 0;
                std::uint64_t u = 0u;
                auto const r = integer_value(aValue, s, u);
                if (r < 0)
                    return false;
                // (PyLong_AsLong: a C long)
                auto const as_long = [&]() -> bool
                {
                    if (r != 0 || s < LONG_MIN || s > LONG_MAX)
                        return overflow("Python int too large to convert to C long");
                    return true;
                };
                // (PyLong_AsUnsignedLong: a C unsigned long)
                auto const as_unsigned_long = [&]() -> bool
                {
                    if (r == 3 || (r == 0 && s < 0))
                        return overflow("can't convert negative value to unsigned int");
                    if (r == 2 || u > ULONG_MAX)
                        return overflow("Python int too large to convert to C unsigned long");
                    return true;
                };
                switch (code)
                {
                case 'b':
                    if (!as_long())
                        return false;
                    if (s < SHRT_MIN)
                        return overflow("signed short integer is less than minimum");
                    if (s > SHRT_MAX)
                        return overflow("signed short integer is greater than maximum");
                    if (s < -128)
                        return overflow("signed char is less than minimum");
                    if (s > 127)
                        return overflow("signed char is greater than maximum");
                    return put(static_cast<signed char>(s));
                case 'B':
                    if (!as_long())
                        return false;
                    if (s < 0)
                        return overflow("unsigned byte integer is less than minimum");
                    if (s > UCHAR_MAX)
                        return overflow("unsigned byte integer is greater than maximum");
                    return put(static_cast<unsigned char>(s));
                case 'h':
                    if (!as_long())
                        return false;
                    if (s < SHRT_MIN)
                        return overflow("signed short integer is less than minimum");
                    if (s > SHRT_MAX)
                        return overflow("signed short integer is greater than maximum");
                    return put(static_cast<short>(s));
                case 'H':
                case 'i':
                    if (!as_long())
                        return false;
                    if (s < INT_MIN)
                        return overflow("signed integer is less than minimum");
                    if (s > INT_MAX)
                        return overflow("signed integer is greater than maximum");
                    if (code == 'i')
                        return put(static_cast<int>(s));
                    if (s < 0)
                        return overflow("unsigned short is less than minimum");
                    if (s > USHRT_MAX)
                        return overflow("unsigned short is greater than maximum");
                    return put(static_cast<unsigned short>(s));
                case 'I':
                    if (!as_unsigned_long())
                        return false;
                    if (u > UINT_MAX)
                        return overflow("unsigned int is greater than maximum");
                    return put(static_cast<unsigned int>(u));
                case 'l':
                    return as_long() && put(static_cast<long>(s));
                case 'L':
                    return as_unsigned_long() && put(static_cast<unsigned long>(u));
                case 'q':
                    if (r != 0)
                        return overflow("int too big to convert");
                    return put(s);
                default:    // ('Q')
                    if (r == 3 || (r == 0 && s < 0))
                        return overflow("can't convert negative int to unsigned");
                    if (r == 2)
                        return overflow("int too big to convert");
                    return put(static_cast<unsigned long long>(u));
                }
            }

            // (its items, as a list)
            object* array_list(array_object& aArray)
            {
                std::vector<object*> items;
                for (std::size_t i = 0u; i < aArray.size(); ++i)
                {
                    auto const item = get_item(aArray, i);
                    if (item == nullptr)
                    {
                        for (auto v : items)
                            release(v);
                        return nullptr;
                    }
                    items.push_back(item);
                }
                return make_list(std::move(items));
            }

            object* array_unicode(array_object& aArray, char const* aFunction)
            {
                auto const code = aArray.descriptor->code;
                if (code != 'u' && code != 'w')
                    return error("ValueError", std::string{ aFunction } + "() may only be called on unicode type arrays ('u' or 'w')");
                std::u32string characters;
                for (std::size_t i = 0u; i < aArray.size(); ++i)
                {
                    char32_t c = 0u;
                    if (code == 'u')
                    {
                        wchar_t w = 0;
                        std::memcpy(&w, aArray.item(i), sizeof w);
                        c = static_cast<char32_t>(w);
                        if (sizeof(wchar_t) == 2u && c >= 0xD800u && c < 0xDC00u && i + 1u < aArray.size())
                        {
                            // (a surrogate pair (wchar_t is UTF-16))
                            wchar_t low = 0;
                            std::memcpy(&low, aArray.item(i + 1u), sizeof low);
                            if (static_cast<char32_t>(low) >= 0xDC00u && static_cast<char32_t>(low) < 0xE000u)
                            {
                                c = 0x10000u + ((c - 0xD800u) << 10) + (static_cast<char32_t>(low) - 0xDC00u);
                                ++i;
                            }
                        }
                    }
                    else
                        std::memcpy(&c, aArray.item(i), sizeof c);
                    if (c > 0x10FFFFu)
                    {
                        char text[64];
                        std::snprintf(text, sizeof text, "character U+%x is not in range [U+0000; U+10ffff]", static_cast<unsigned>(c));
                        return error("ValueError", text);
                    }
                    characters.push_back(c);
                }
                return make_str(from_code_points(characters));
            }

            bool append_unicode(array_object& aArray, std::string const& aText)
            {
                std::u32string const characters = to_code_points(aText);
                std::string bytes;
                for (auto c : characters)
                {
                    if (aArray.descriptor->code == 'w')
                        bytes.append(reinterpret_cast<char const*>(&c), 4u);
                    else if (sizeof(wchar_t) == 2u && c > 0xFFFFu)
                    {
                        wchar_t const pair[] = { static_cast<wchar_t>(0xD800u + ((c - 0x10000u) >> 10)), static_cast<wchar_t>(0xDC00u + ((c - 0x10000u) & 0x3FFu)) };
                        bytes.append(reinterpret_cast<char const*>(pair), sizeof pair);
                    }
                    else
                    {
                        auto const w = static_cast<wchar_t>(c);
                        bytes.append(reinterpret_cast<char const*>(&w), sizeof w);
                    }
                }
                if (!resizable(aArray, aArray.size() + bytes.size() / aArray.descriptor->size))
                    return false;
                aArray.data += bytes;
                return true;
            }

            bool append_bytes(array_object& aArray, object* aBuffer)
            {
                buffer_info buffer;
                if (!buffer_of(aBuffer, buffer))
                {
                    error("TypeError", std::string{ "a bytes-like object is required, not '" } + type_name(aBuffer) + "'");
                    return false;
                }
                if (buffer.itemsize != 1u)
                {
                    error("TypeError", "a bytes-like object is required");
                    return false;
                }
                if (buffer.size % aArray.descriptor->size != 0u)
                {
                    error("ValueError", "bytes length not a multiple of item size");
                    return false;
                }
                if (buffer.size == 0u)
                    return true;
                std::string const bytes{ buffer.data, buffer.size };
                if (!resizable(aArray, aArray.size() + bytes.size() / aArray.descriptor->size))
                    return false;
                aArray.data += bytes;
                return true;
            }

            // (ins1: v inserted before position aWhere)
            bool insert_item(array_object& aArray, long long aWhere, object* aValue)
            {
                std::string item(aArray.descriptor->size, '\0');
                if (!set_item(*aArray.descriptor, item.data(), aValue))
                    return false;
                auto const n = static_cast<long long>(aArray.size());
                if (!resizable(aArray, aArray.size() + 1u))
                    return false;
                if (aWhere < 0)
                    aWhere = std::max(0LL, aWhere + n);
                aWhere = std::min(aWhere, n);
                aArray.data.insert(static_cast<std::size_t>(aWhere) * aArray.descriptor->size, item);
                return true;
            }

            bool extend_array(array_object& aArray, object* aOther, bool aIterate = false)
            {
                if (!is_array(aOther) || aIterate)
                {
                    ref const iterator{ iter(aOther) };
                    if (!iterator)
                        return false;
                    for (;;)
                    {
                        ref const item{ next(iterator.get()) };
                        if (!item)
                            return !failed();
                        if (!insert_item(aArray, static_cast<long long>(aArray.size()), item.get()))
                            return false;
                    }
                }
                auto& other = as_array(aOther);
                if (other.descriptor != aArray.descriptor)
                {
                    error("TypeError", "can only extend with array of same kind");
                    return false;
                }
                std::string const bytes = other.data;
                if (!resizable(aArray, aArray.size() + other.size()))
                    return false;
                aArray.data += bytes;
                return true;
            }

            // (array_del_slice: [aLow, aHigh))
            bool delete_items(array_object& aArray, long long aLow, long long aHigh)
            {
                auto const n = static_cast<long long>(aArray.size());
                aLow = std::clamp(aLow, 0LL, n);
                aHigh = std::clamp(aHigh, aLow, n);
                if (aHigh == aLow)
                    return true;
                if (!resizable(aArray, static_cast<std::size_t>(n - (aHigh - aLow))))
                    return false;
                auto const size = aArray.descriptor->size;
                aArray.data.erase(static_cast<std::size_t>(aLow) * size, static_cast<std::size_t>(aHigh - aLow) * size);
                return true;
            }

            long long slice_length(long long aStart, long long aStop, long long aStep)
            {
                if (aStep > 0)
                    return aStop > aStart ? (aStop - aStart + aStep - 1) / aStep : 0;
                return aStart > aStop ? (aStart - aStop - aStep - 1) / -aStep : 0;
            }

            // (array_ass_subscr: aValue null: del)
            int assign_items(object* aSelf, object* aKey, object* aValue)
            {
                auto& self = as_array(aSelf);
                auto const n = static_cast<long long>(self.size());
                long long start = 0, stop = 0, step = 1, length = 0;
                if (kind_of(aKey) == NEOS_PY_SLICE)
                {
                    if (!slice_indices(aKey, n, start, stop, step))
                        return -1;
                    length = slice_length(start, stop, step);
                }
                else
                {
                    long long i = 0;
                    if (!is_int_like(aKey) && (python_type(aKey) == nullptr || python_type(aKey)->index == nullptr))
                    {
                        error("TypeError", "array indices must be integers");
                        return -1;
                    }
                    if (!to_index(aKey, i))
                    {
                        clear_error();
                        error("IndexError", "cannot fit 'int' into an index-sized integer");
                        return -1;
                    }
                    if (i < 0)
                        i += n;
                    if (i < 0 || i >= n)
                    {
                        error("IndexError", "array assignment index out of range");
                        return -1;
                    }
                    if (aValue != nullptr)
                        return set_item(*self.descriptor, self.item(static_cast<std::size_t>(i)), aValue) ? 0 : -1;
                    start = i;
                    stop = i + 1;
                    step = 1;
                    length = 1;
                }
                std::string items;
                long long needed = 0;
                if (aValue != nullptr)
                {
                    if (!is_array(aValue))
                    {
                        error("TypeError", std::string{ "can only assign array (not \"" } + type_name(aValue) + "\") to array slice");
                        return -1;
                    }
                    auto& other = as_array(aValue);
                    if (other.descriptor != self.descriptor)
                    {
                        error("TypeError", "bad argument type for built-in operation");
                        return -1;
                    }
                    items = other.data;
                    needed = static_cast<long long>(other.size());
                }
                auto const size = self.descriptor->size;
                if ((step > 0 && stop < start) || (step < 0 && stop > start))
                    stop = start;
                if ((needed == 0 || length != needed) && self.exports > 0)
                {
                    error("BufferError", "cannot resize an array that is exporting buffers");
                    return -1;
                }
                if (step == 1)
                {
                    self.data.replace(static_cast<std::size_t>(start) * size, static_cast<std::size_t>(length) * size, items);
                    return 0;
                }
                if (needed == 0)
                {
                    // (an extended slice deleted)
                    std::vector<bool> deleted(static_cast<std::size_t>(n), false);
                    for (long long i = 0, k = start; i < length; ++i, k += step)
                        deleted[static_cast<std::size_t>(k)] = true;
                    std::string rest;
                    for (long long i = 0; i < n; ++i)
                        if (!deleted[static_cast<std::size_t>(i)])
                            rest.append(self.item(static_cast<std::size_t>(i)), size);
                    self.data = std::move(rest);
                    return 0;
                }
                if (needed != length)
                {
                    error("ValueError", "attempt to assign array of size " + std::to_string(needed) + " to extended slice of size " + std::to_string(length));
                    return -1;
                }
                for (long long i = 0, k = start; i < length; ++i, k += step)
                    std::memcpy(self.item(static_cast<std::size_t>(k)), items.data() + i * static_cast<long long>(size), size);
                return 0;
            }

            // (array_new: array(typecode[, initializer]))
            object* new_array(array_descriptor const* aDescriptor, object* aInitial)
            {
                auto const code = aDescriptor->code;
                bool const isUnicode = code == 'u' || code == 'w';
                if (aInitial != nullptr && !isUnicode)
                {
                    if (kind_of(builtin_value(aInitial)) == NEOS_PY_STR)
                        return error("TypeError", std::string{ "cannot use a str to initialize an array with typecode '" } + code + "'");
                    if (is_array(aInitial) && (as_array(aInitial).descriptor->code == 'u' || as_array(aInitial).descriptor->code == 'w'))
                        return error("TypeError", std::string{ "cannot use a unicode array to initialize an array with typecode '" } + code + "'");
                }
                ref result{ make_array(aDescriptor) };
                auto& self = as_array(result.get());
                if (aInitial == nullptr)
                    return result.release_ownership();
                auto const kind = kind_of(aInitial);
                if (kind == NEOS_PY_LIST || kind == NEOS_PY_TUPLE)
                {
                    auto const& items = kind == NEOS_PY_LIST ? as<list_object>(aInitial)->items : as<tuple_object>(aInitial)->items;
                    self.data.resize(items.size() * aDescriptor->size);
                    for (std::size_t i = 0u; i < items.size(); ++i)
                        if (!set_item(*aDescriptor, self.item(i), items[i]))
                            return nullptr;
                    return result.release_ownership();
                }
                if (is_array(aInitial) && as_array(aInitial).descriptor == aDescriptor)
                {
                    self.data = as_array(aInitial).data;
                    return result.release_ownership();
                }
                if (type_name(aInitial) == std::string_view{ "bytes" } || type_name(aInitial) == std::string_view{ "bytearray" })
                    return append_bytes(self, aInitial) ? result.release_ownership() : nullptr;
                if (isUnicode && kind == NEOS_PY_STR)
                    return append_unicode(self, as<str_object>(aInitial)->value) ? result.release_ownership() : nullptr;
                return extend_array(self, aInitial, true) ? result.release_ownership() : nullptr;
            }

            object* array_type_object()
            {
                return module_attribute("array", "array");
            }

            // --- machine format codes (pickling)

            enum machine_format : int
            {
                UNKNOWN_FORMAT = -1, UNSIGNED_INT8 = 0, SIGNED_INT8 = 1, UNSIGNED_INT16_LE = 2, UNSIGNED_INT16_BE = 3, SIGNED_INT16_LE = 4, SIGNED_INT16_BE = 5,
                UNSIGNED_INT32_LE = 6, UNSIGNED_INT32_BE = 7, SIGNED_INT32_LE = 8, SIGNED_INT32_BE = 9, UNSIGNED_INT64_LE = 10, UNSIGNED_INT64_BE = 11,
                SIGNED_INT64_LE = 12, SIGNED_INT64_BE = 13, IEEE_754_FLOAT_LE = 14, IEEE_754_FLOAT_BE = 15, IEEE_754_DOUBLE_LE = 16, IEEE_754_DOUBLE_BE = 17,
                UTF16_LE = 18, UTF16_BE = 19, UTF32_LE = 20, UTF32_BE = 21
            };

            struct machine_format_descriptor { std::size_t size; bool isSigned; bool bigEndian; };
            machine_format_descriptor const sMachineFormats[] =
            {
                { 1u, false, false }, { 1u, true, false }, { 2u, false, false }, { 2u, false, true }, { 2u, true, false }, { 2u, true, true },
                { 4u, false, false }, { 4u, false, true }, { 4u, true, false }, { 4u, true, true }, { 8u, false, false }, { 8u, false, true },
                { 8u, true, false }, { 8u, true, true }, { 4u, false, false }, { 4u, false, true }, { 8u, false, false }, { 8u, false, true },
                { 4u, false, false }, { 4u, false, true }, { 8u, false, false }, { 8u, false, true }
            };

            bool big_endian()
            {
                std::uint16_t const one = 1u;
                unsigned char first = 0u;
                std::memcpy(&first, &one, 1u);
                return first == 0u;
            }

            int machine_format_of(char aCode)
            {
                int const be = big_endian() ? 1 : 0;
                std::size_t size = 0u;
                bool isSigned = false;
                switch (aCode)
                {
                case 'b': return SIGNED_INT8;
                case 'B': return UNSIGNED_INT8;
                case 'u': return sizeof(wchar_t) == 2u ? UTF16_LE + be : UTF32_LE + be;
                case 'w': return UTF32_LE + be;
                case 'f': return IEEE_754_FLOAT_LE + be;
                case 'd': return IEEE_754_DOUBLE_LE + be;
                case 'h': size = sizeof(short); isSigned = true; break;
                case 'H': size = sizeof(short); break;
                case 'i': size = sizeof(int); isSigned = true; break;
                case 'I': size = sizeof(int); break;
                case 'l': size = sizeof(long); isSigned = true; break;
                case 'L': size = sizeof(long); break;
                case 'q': size = sizeof(long long); isSigned = true; break;
                case 'Q': size = sizeof(long long); break;
                default: return UNKNOWN_FORMAT;
                }
                int const s = isSigned ? 2 : 0;
                switch (size)
                {
                case 2u: return UNSIGNED_INT16_LE + be + s;
                case 4u: return UNSIGNED_INT32_LE + be + s;
                case 8u: return UNSIGNED_INT64_LE + be + s;
                default: return UNKNOWN_FORMAT;
                }
            }

            std::uint64_t read_integer(unsigned char const* aBytes, std::size_t aSize, bool aBigEndian)
            {
                std::uint64_t value = 0u;
                for (std::size_t i = 0u; i < aSize; ++i)
                    value |= static_cast<std::uint64_t>(aBytes[aBigEndian ? aSize - 1u - i : i]) << (8u * i);
                return value;
            }

            // (array._array_reconstructor(arraytype, typecode, mformat_code, items))
            object* array_reconstructor(std::vector<object*> const& aArgs, object* aKwargs)
            {
                std::vector<object*> a;
                if (!parse_arguments("_array_reconstructor", aArgs, aKwargs, { "arraytype", "typecode", "mformat_code", "items" }, 4u, 4u, a))
                    return nullptr;
                ref const arrayType{ array_type_object() };
                if (!arrayType)
                    return nullptr;
                ref const isType{ b_isinstance({ a[0], ref{ fn_builtin("type") }.get() }, nullptr) };
                if (!isType)
                    return nullptr;
                if (truth(isType.get()) != 1)
                    return error("TypeError", std::string{ "first argument must be a type object, not " } + type_name(a[0]));
                if (a[0] != arrayType.get())
                {
                    ref const sub{ b_issubclass({ a[0], arrayType.get() }, nullptr) };
                    if (!sub)
                        return nullptr;
                    if (truth(sub.get()) != 1)
                        return error("TypeError", repr_text(a[0]) + " is not a subtype of array.array");
                }
                auto const typecodeText = str_of_value(a[1]);
                if (typecodeText == nullptr)
                    return error("TypeError", std::string{ "_array_reconstructor() argument 2 must be a unicode character, not " } + type_name(a[1]));
                auto const characters = to_code_points(*typecodeText);
                if (characters.size() != 1u)
                    return error("TypeError", std::string{ "_array_reconstructor() argument 2 must be a unicode character, not a string of length " } +
                        std::to_string(characters.size()));
                auto descriptor = descriptor_of(characters[0]);
                if (descriptor == nullptr)
                    return error("ValueError", "second argument must be a valid type code");
                long long mformat = 0;
                if (!to_index(a[2], mformat))
                    return nullptr;
                if (mformat < UNSIGNED_INT8 || mformat > UTF32_BE)
                    return error("ValueError", "third argument must be a valid machine format code.");
                std::string_view items;
                if (type_name(a[3]) != std::string_view{ "bytes" } || !bytes_like(a[3], items))
                    return error("TypeError", std::string{ "fourth argument should be bytes, not " } + type_name(a[3]));
                if (mformat == machine_format_of(descriptor->code))
                    return make_array(descriptor, std::string{ items });
                auto const& mf = sMachineFormats[mformat];
                if (items.size() % mf.size != 0u)
                    return error("ValueError", "string length not a multiple of item size");
                auto const bytes = reinterpret_cast<unsigned char const*>(items.data());
                auto const count = items.size() / mf.size;
                ref converted;
                switch (mformat)
                {
                case IEEE_754_FLOAT_LE:
                case IEEE_754_FLOAT_BE:
                case IEEE_754_DOUBLE_LE:
                case IEEE_754_DOUBLE_BE:
                    {
                        std::vector<object*> values;
                        for (std::size_t i = 0u; i < count; ++i)
                        {
                            auto const raw = read_integer(bytes + i * mf.size, mf.size, mf.bigEndian);
                            values.push_back(make_float(mf.size == 4u ? static_cast<double>(std::bit_cast<float>(static_cast<std::uint32_t>(raw))) :
                                std::bit_cast<double>(raw)));
                        }
                        converted = ref{ make_list(std::move(values)) };
                        break;
                    }
                case UTF16_LE:
                case UTF16_BE:
                case UTF32_LE:
                case UTF32_BE:
                    {
                        std::string text;
                        if (!decode(items, mformat == UTF16_LE ? "utf-16-le" : mformat == UTF16_BE ? "utf-16-be" : mformat == UTF32_LE ? "utf-32-le" : "utf-32-be",
                            "strict", text))
                            return nullptr;
                        converted = ref{ make_str(std::move(text)) };
                        break;
                    }
                default:
                    {
                        // (an integer type of the same size and signedness)
                        for (auto const& d : sDescriptors)
                            if (d.integer && d.size == mf.size && d.isSigned == mf.isSigned)
                                descriptor = &d;
                        std::vector<object*> values;
                        for (std::size_t i = 0u; i < count; ++i)
                        {
                            auto const raw = read_integer(bytes + i * mf.size, mf.size, mf.bigEndian);
                            if (mf.isSigned)
                            {
                                auto value = static_cast<long long>(raw);
                                if (mf.size < 8u && (raw & (std::uint64_t{ 1 } << (8u * mf.size - 1u))) != 0u)
                                    value -= static_cast<long long>(std::uint64_t{ 1 } << (8u * mf.size));
                                values.push_back(make_int(value));
                            }
                            else
                                values.push_back(make_unsigned(raw));
                        }
                        converted = ref{ make_list(std::move(values)) };
                        break;
                    }
                }
                return new_array(descriptor, converted.get());
            }

            // --- methods

            char const* const sMethods[] =
            {
                "append", "buffer_info", "byteswap", "clear", "__copy__", "count", "__deepcopy__", "extend", "fromfile", "fromlist", "frombytes", "fromunicode",
                "index", "insert", "pop", "__reduce_ex__", "remove", "reverse", "tofile", "tolist", "tobytes", "tounicode", "__sizeof__"
            };

            // (the first index of an item equal to aValue in [aStart, aStop): -1: none; -2: an exception)
            long long find_item(object* aSelf, object* aValue, long long aStart, long long aStop)
            {
                auto& self = as_array(aSelf);
                for (long long i = aStart; i < aStop && i < static_cast<long long>(self.size()); ++i)
                {
                    ref const item{ get_item(self, static_cast<std::size_t>(i)) };
                    if (!item)
                        return -2;
                    auto const e = equal(item.get(), aValue);
                    if (e < 0)
                        return -2;
                    if (e == 1)
                        return i;
                }
                return -1;
            }

            bool slice_index_argument(object* aValue, long long& aResult)
            {
                if (aValue == nullptr)
                    return true;
                if (!is_int_like(aValue) && (python_type(aValue) == nullptr || python_type(aValue)->index == nullptr))
                {
                    error("TypeError", "slice indices must be integers or have an __index__ method");
                    return false;
                }
                long long s = 0;
                std::uint64_t u = 0u;
                auto const r = integer_value(aValue, s, u);
                if (r < 0)
                    return false;
                aResult = r == 0 ? s : r == 3 ? std::numeric_limits<long long>::min() : std::numeric_limits<long long>::max();
                return true;
            }

            object* array_method(neos_py_context*, void* aData, object* aArgs, object* aKwargs)
            {
                auto const& items = as<tuple_object>(aArgs)->items;
                auto const selfObject = items.at(0u);
                auto& self = as_array(selfObject);
                std::vector<object*> const rest{ items.begin() + 1, items.end() };
                std::string_view const name{ static_cast<char const*>(aData) };
                std::string const qualified = "array." + std::string{ name };
                std::vector<object*> a;
                auto const none_result = []() { retain(none()); return none(); };
                // (METH_O, METH_NOARGS and positional parameters)
                auto const arguments = [&](std::vector<char const*> const& aNames, std::size_t aRequired)
                {
                    return parse_arguments(qualified.c_str(), rest, aKwargs, aNames, aRequired, aNames.size(), a);
                };
                if (name == "append")
                    return arguments({ "v" }, 1u) && insert_item(self, static_cast<long long>(self.size()), a[0]) ? none_result() : nullptr;
                if (name == "buffer_info")
                {
                    if (!arguments({}, 0u))
                        return nullptr;
                    return make_tuple({ make_unsigned(reinterpret_cast<std::uintptr_t>(self.data.data())), make_int(static_cast<long long>(self.size())) });
                }
                if (name == "byteswap")
                {
                    if (!arguments({}, 0u))
                        return nullptr;
                    auto const size = self.descriptor->size;
                    for (std::size_t i = 0u; i < self.size(); ++i)
                        std::reverse(self.item(i), self.item(i) + size);
                    return none_result();
                }
                if (name == "clear")
                    return arguments({}, 0u) && resize(self, 0u) ? none_result() : nullptr;
                if (name == "__copy__")
                    return arguments({}, 0u) ? make_array(self.descriptor, self.data) : nullptr;
                if (name == "__deepcopy__")
                    return arguments({ "unused" }, 1u) ? make_array(self.descriptor, self.data) : nullptr;
                if (name == "count")
                {
                    if (!arguments({ "v" }, 1u))
                        return nullptr;
                    long long count = 0;
                    for (std::size_t i = 0u; i < self.size(); ++i)
                    {
                        ref const item{ get_item(self, i) };
                        if (!item)
                            return nullptr;
                        auto const e = equal(item.get(), a[0]);
                        if (e < 0)
                            return nullptr;
                        count += e;
                    }
                    return make_int(count);
                }
                if (name == "extend")
                    return arguments({ "bb" }, 1u) && extend_array(self, a[0]) ? none_result() : nullptr;
                if (name == "fromfile")
                {
                    if (!arguments({ "f", "n" }, 2u))
                        return nullptr;
                    long long n = 0;
                    if (!to_index(a[1], n))
                        return nullptr;
                    if (n < 0)
                        return error("ValueError", "negative count");
                    auto const bytes = n * static_cast<long long>(self.descriptor->size);
                    ref const data{ call_method_named(a[0], "read", { make_int(bytes) }) };
                    if (!data)
                        return nullptr;
                    std::string_view view;
                    if (type_name(data.get()) != std::string_view{ "bytes" } || !bytes_like(data.get(), view))
                        return error("TypeError", "read() didn't return bytes");
                    bool const short_read = static_cast<long long>(view.size()) != bytes;
                    if (!append_bytes(self, data.get()))
                        return nullptr;
                    if (short_read)
                        return error("EOFError", "read() didn't return enough bytes");
                    return none_result();
                }
                if (name == "tofile")
                {
                    if (!arguments({ "f" }, 1u))
                        return nullptr;
                    std::size_t const block = 64u * 1024u;
                    for (std::size_t offset = 0u; offset < self.data.size(); offset += block)
                    {
                        ref const written{ call_method_named(a[0], "write", { make_bytes(self.data.substr(offset, block)) }) };
                        if (!written)
                            return nullptr;
                    }
                    return none_result();
                }
                if (name == "fromlist")
                {
                    if (!arguments({ "list" }, 1u))
                        return nullptr;
                    auto const list = builtin_value(a[0]);
                    if (kind_of(list) != NEOS_PY_LIST)
                        return error("TypeError", "arg must be list");
                    auto const& values = as<list_object>(list)->items;
                    std::string added(values.size() * self.descriptor->size, '\0');
                    for (std::size_t i = 0u; i < values.size(); ++i)
                        if (!set_item(*self.descriptor, added.data() + i * self.descriptor->size, values[i]))
                            return nullptr;
                    if (!resizable(self, self.size() + values.size()))
                        return nullptr;
                    self.data += added;
                    return none_result();
                }
                if (name == "frombytes")
                    return arguments({ "buffer" }, 1u) && append_bytes(self, a[0]) ? none_result() : nullptr;
                if (name == "fromunicode")
                {
                    if (!arguments({ "ustr" }, 1u))
                        return nullptr;
                    auto const text = str_of_value(builtin_value(a[0]));
                    if (text == nullptr)
                        return error("TypeError", std::string{ "fromunicode() argument must be str, not " } + type_name(a[0]));
                    if (self.descriptor->code != 'u' && self.descriptor->code != 'w')
                        return error("ValueError", "fromunicode() may only be called on unicode type arrays ('u' or 'w')");
                    return append_unicode(self, *text) ? none_result() : nullptr;
                }
                if (name == "index")
                {
                    if (!arguments({ "v", "start", "stop" }, 1u))
                        return nullptr;
                    long long start = 0, stop = std::numeric_limits<long long>::max();
                    if (!slice_index_argument(a[1], start) || !slice_index_argument(a[2], stop))
                        return nullptr;
                    auto const n = static_cast<long long>(self.size());
                    if (start < 0)
                        start = std::max(0LL, start + n);
                    if (stop < 0)
                        stop += n;
                    auto const i = find_item(selfObject, a[0], start, stop);
                    if (i == -2)
                        return nullptr;
                    if (i < 0)
                        return error("ValueError", "array.index(x): x not in array");
                    return make_int(i);
                }
                if (name == "insert")
                {
                    if (!arguments({ "i", "v" }, 2u))
                        return nullptr;
                    long long i = 0;
                    if (!to_index(a[0], i))
                        return nullptr;
                    return insert_item(self, i, a[1]) ? none_result() : nullptr;
                }
                if (name == "pop")
                {
                    if (!arguments({ "i" }, 0u))
                        return nullptr;
                    long long i = -1;
                    if (a[0] != nullptr && !to_index(a[0], i))
                        return nullptr;
                    auto const n = static_cast<long long>(self.size());
                    if (n == 0)
                        return error("IndexError", "pop from empty array");
                    if (i < 0)
                        i += n;
                    if (i < 0 || i >= n)
                        return error("IndexError", "pop index out of range");
                    ref value{ get_item(self, static_cast<std::size_t>(i)) };
                    if (!value || !delete_items(self, i, i + 1))
                        return nullptr;
                    return value.release_ownership();
                }
                if (name == "__reduce_ex__")
                {
                    if (!arguments({ "value" }, 1u))
                        return nullptr;
                    if (!is_int_like(a[0]))
                        return error("TypeError", "__reduce_ex__ argument should be an integer");
                    long long protocol = 0;
                    if (!to_index(a[0], protocol))
                        return nullptr;
                    ref const arrayType{ array_type_object() };
                    if (!arrayType)
                        return nullptr;
                    auto const mformat = machine_format_of(self.descriptor->code);
                    auto const code = std::string(1u, self.descriptor->code);
                    if (mformat == UNKNOWN_FORMAT || protocol < 3)
                    {
                        auto const list = array_list(self);
                        if (list == nullptr)
                            return nullptr;
                        return make_tuple({ new_reference(arrayType.get()), make_tuple({ make_str(code), list }), new_reference(none()) });
                    }
                    ref const reconstructor{ module_attribute("array", "_array_reconstructor") };
                    if (!reconstructor)
                        return nullptr;
                    return make_tuple({ new_reference(reconstructor.get()), make_tuple({ new_reference(arrayType.get()), make_str(code), make_int(mformat),
                        make_bytes(self.data) }), new_reference(none()) });
                }
                if (name == "remove")
                {
                    if (!arguments({ "v" }, 1u))
                        return nullptr;
                    auto const i = find_item(selfObject, a[0], 0, std::numeric_limits<long long>::max());
                    if (i == -2)
                        return nullptr;
                    if (i < 0)
                        return error("ValueError", "array.remove(x): x not in array");
                    return delete_items(self, i, i + 1) ? none_result() : nullptr;
                }
                if (name == "reverse")
                {
                    if (!arguments({}, 0u))
                        return nullptr;
                    auto const size = self.descriptor->size;
                    for (std::size_t i = 0u, j = self.size(); j > 0u && i < j - 1u; ++i, --j)
                        std::swap_ranges(self.item(i), self.item(i) + size, self.item(j - 1u));
                    return none_result();
                }
                if (name == "tolist")
                    return arguments({}, 0u) ? array_list(self) : nullptr;
                if (name == "tobytes")
                    return arguments({}, 0u) ? make_bytes(self.data) : nullptr;
                if (name == "tounicode")
                    return arguments({}, 0u) ? array_unicode(self, "tounicode") : nullptr;
                // (__sizeof__: its object's and its items')
                return arguments({}, 0u) ? make_int(static_cast<long long>(64u + self.data.capacity())) : nullptr;
            }

            // --- the type

            object* array_item_at(object* aSelf, object* aKey)
            {
                auto& self = as_array(aSelf);
                auto const n = static_cast<long long>(self.size());
                if (kind_of(aKey) == NEOS_PY_SLICE)
                {
                    long long start = 0, stop = 0, step = 0;
                    if (!slice_indices(aKey, n, start, stop, step))
                        return nullptr;
                    auto const length = slice_length(start, stop, step);
                    std::string data;
                    auto const size = self.descriptor->size;
                    for (long long i = 0, k = start; i < length; ++i, k += step)
                        data.append(self.item(static_cast<std::size_t>(k)), size);
                    return make_array(self.descriptor, std::move(data));
                }
                if (!is_int_like(aKey) && (python_type(aKey) == nullptr || python_type(aKey)->index == nullptr))
                    return error("TypeError", "array indices must be integers");
                long long i = 0;
                if (!to_index(aKey, i))
                {
                    clear_error();
                    return error("IndexError", "cannot fit 'int' into an index-sized integer");
                }
                if (i < 0)
                    i += n;
                if (i < 0 || i >= n)
                    return error("IndexError", "array index out of range");
                return get_item(self, static_cast<std::size_t>(i));
            }

            neos_py_type make_array_type()
            {
                auto type = make_type("array.array", "array.array", retain_object, release_object<array_object>);
                type.repr = [](neos_py_context*, object* aSelf) -> object*
                {
                    auto& self = as_array(aSelf);
                    std::string const code(1u, self.descriptor->code);
                    if (self.size() == 0u)
                        return make_str("array('" + code + "')");
                    ref const value{ code == "u" || code == "w" ? array_unicode(self, "tounicode") : array_list(self) };
                    if (!value)
                        return nullptr;
                    std::string text;
                    if (!repr_of(value.get(), text))
                        return nullptr;
                    return make_str("array('" + code + "', " + text + ")");
                };
                type.getattr = [](neos_py_context*, object* aSelf, char const* aName) -> object*
                {
                    auto& self = as_array(aSelf);
                    for (auto m : sMethods)
                        if (std::strcmp(m, aName) == 0)
                            return make_function(m, array_method, const_cast<char*>(m), aSelf);
                    std::string_view const name{ aName };
                    if (name == "typecode")
                        return make_str(std::string(1u, self.descriptor->code));
                    if (name == "itemsize")
                        return make_int(static_cast<long long>(self.descriptor->size));
                    if (name == "__class__")
                        return array_type_object();
                    return nullptr;
                };
                type.length = [](neos_py_context*, object* aSelf) -> long long { return static_cast<long long>(as_array(aSelf).size()); };
                type.getitem = [](neos_py_context*, object* aSelf, object* aKey) -> object* { return array_item_at(aSelf, aKey); };
                type.setitem = [](neos_py_context*, object* aSelf, object* aKey, object* aValue) -> int { return assign_items(aSelf, aKey, aValue); };
                type.contains = [](neos_py_context*, object* aSelf, object* aItem) -> int
                {
                    auto const i = find_item(aSelf, aItem, 0, std::numeric_limits<long long>::max());
                    return i == -2 ? -1 : i >= 0 ? 1 : 0;
                };
                type.iter = [](neos_py_context*, object* aSelf) -> object*
                {
                    retain(aSelf);
                    return &(new array_iterator_object{ { { &sArrayIteratorType.base } }, aSelf })->h.base;
                };
                type.binary = [](neos_py_context*, int aOperation, object* aLhs, object* aRhs, int aInplace) -> object*
                {
                    if (aOperation == NEOS_PY_ADD)
                    {
                        if (!is_array(aLhs))
                            return not_implemented();
                        auto& self = as_array(aLhs);
                        if (aInplace != 0)
                        {
                            if (!is_array(aRhs))
                                return error("TypeError", std::string{ "can only extend array with array (not \"" } + type_name(aRhs) + "\")");
                            if (!extend_array(self, aRhs))
                                return nullptr;
                            return new_reference(aLhs);
                        }
                        if (!is_array(aRhs))
                            return error("TypeError", std::string{ "can only append array (not \"" } + type_name(aRhs) + "\") to array");
                        if (as_array(aRhs).descriptor != self.descriptor)
                            return error("TypeError", "bad argument type for built-in operation");
                        return make_array(self.descriptor, self.data + as_array(aRhs).data);
                    }
                    if (aOperation == NEOS_PY_MULTIPLY)
                    {
                        auto const selfObject = is_array(aLhs) ? aLhs : aRhs;
                        auto const count = selfObject == aLhs ? aRhs : aLhs;
                        if (!is_int_like(count) && (python_type(count) == nullptr || python_type(count)->index == nullptr))
                            return selfObject == aLhs && aInplace == 0 ?
                                error("TypeError", std::string{ "can't multiply sequence by non-int of type '" } + type_name(count) + "'") : not_implemented();
                        long long n = 0;
                        if (!to_index(count, n))
                            return nullptr;
                        auto& self = as_array(selfObject);
                        std::string data;
                        for (long long i = 0; i < n; ++i)
                            data += self.data;
                        if (aInplace != 0 && selfObject == aLhs)
                        {
                            if (n != 1 && self.size() > 0u)
                            {
                                if (!resizable(self, data.size() / self.descriptor->size))
                                    return nullptr;
                                self.data = std::move(data);
                            }
                            return new_reference(aLhs);
                        }
                        return make_array(self.descriptor, std::move(data));
                    }
                    return not_implemented();
                };
                type.compare = [](neos_py_context*, int aOperation, object* aLhs, object* aRhs) -> object*
                {
                    if (!is_array(aLhs) || !is_array(aRhs))
                        return not_implemented();
                    auto& a = as_array(aLhs);
                    auto& b = as_array(aRhs);
                    if (a.size() != b.size() && (aOperation == NEOS_PY_EQ || aOperation == NEOS_PY_NE))
                        return make_bool(aOperation == NEOS_PY_NE);
                    // (the first index at which the items differ)
                    for (std::size_t i = 0u; i < a.size() && i < b.size(); ++i)
                    {
                        ref const x{ get_item(a, i) };
                        ref const y{ get_item(b, i) };
                        if (!x || !y)
                            return nullptr;
                        auto const e = equal(x.get(), y.get());
                        if (e < 0)
                            return nullptr;
                        if (e == 0)
                        {
                            if (aOperation == NEOS_PY_EQ || aOperation == NEOS_PY_NE)
                                return make_bool(aOperation == NEOS_PY_NE);
                            return compare(aOperation, x.get(), y.get());
                        }
                    }
                    auto const vs = a.size();
                    auto const ws = b.size();
                    switch (aOperation)
                    {
                    case NEOS_PY_LT: return make_bool(vs < ws);
                    case NEOS_PY_LE: return make_bool(vs <= ws);
                    case NEOS_PY_EQ: return make_bool(true);
                    case NEOS_PY_NE: return make_bool(false);
                    case NEOS_PY_GT: return make_bool(vs > ws);
                    default: return make_bool(vs >= ws);
                    }
                };
                return type;
            }

            neos_py_type make_array_iterator_type()
            {
                auto type = make_type("array.arrayiterator", "array.arrayiterator", retain_object, release_object<array_iterator_object>);
                type.iter = [](neos_py_context*, object* aSelf) -> object* { return new_reference(aSelf); };
                type.next = [](neos_py_context*, object* aSelf) -> object*
                {
                    auto& self = *reinterpret_cast<array_iterator_object*>(aSelf);
                    if (self.array == nullptr)
                        return nullptr;
                    auto& array = as_array(self.array);
                    if (self.index >= array.size())
                    {
                        release(self.array);
                        self.array = nullptr;
                        return nullptr;
                    }
                    return get_item(array, self.index++);
                };
                type.getattr = [](neos_py_context*, object*, char const*) -> object* { return nullptr; };
                return type;
            }

            // --- the module

            object* array_new(neos_py_context*, void*, object* aArgs, object* aKwargs)
            {
                auto const& args = as<tuple_object>(aArgs)->items;
                if (!no_keywords(aKwargs, "array.array"))
                    return nullptr;
                if (args.empty())
                    return error("TypeError", "array() takes at least 1 argument (0 given)");
                if (args.size() > 2u)
                    return error("TypeError", "array() takes at most 2 arguments (" + std::to_string(args.size()) + " given)");
                auto const text = str_of_value(builtin_value(args[0]));
                std::u32string code;
                if (text != nullptr)
                    code = to_code_points(*text);
                if (code.size() != 1u)
                    return error("TypeError", std::string{ "array() argument 1 must be a unicode character, not " } + type_name(args[0]));
                if (code[0] == U'u')
                {
                    ref const warn{ module_attribute("warnings", "warn") };
                    if (!warn)
                        return nullptr;
                    ref const warned{ invoke(warn.get(), { make_str("The 'u' type code is deprecated and will be removed in Python 3.16"),
                        exception_class("DeprecationWarning"), make_int(1) }) };
                    if (!warned)
                        return nullptr;
                }
                auto const descriptor = descriptor_of(code[0]);
                if (descriptor == nullptr)
                    return error("ValueError", "bad typecode (must be b, B, u, w, h, H, i, I, l, L, q, Q, f or d)");
                return new_array(descriptor, args.size() > 1u ? args[1] : nullptr);
            }

            int array_instancecheck(neos_py_context*, void*, object* aObject) { return is_array(aObject) ? 1 : 0; }

            bool array_buffer(object* aObject, buffer_info& aResult)
            {
                auto& self = as_array(aObject);
                aResult = buffer_info{ self.data.data(), self.data.size(), false, self.descriptor->format, self.descriptor->size };
                return true;
            }

            void array_exports(object* aObject, int aDelta) { as_array(aObject).exports += aDelta; }

            bool array_module(std::vector<std::pair<std::string, object*>>& aAttributes)
            {
                buffer_providers()[&sArrayType.base] = buffer_provider{ array_buffer, array_exports };
                auto const type = context()->type_new(context(), "array", "array.array", array_new, nullptr, array_instancecheck);
                aAttributes.emplace_back("array", type);
                aAttributes.emplace_back("ArrayType", new_reference(type));
                aAttributes.emplace_back("typecodes", make_str("bBuwhHiIlLqQfd"));
                static builtin_entry const sReconstructor{ "_array_reconstructor", array_reconstructor };
                aAttributes.emplace_back("_array_reconstructor", make_builtin(sReconstructor));
                return true;
            }

            inline bool const sArray = register_module("array", array_module);
        }
    }
}
