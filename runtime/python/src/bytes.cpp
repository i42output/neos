/*
  bytes.cpp

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

  Portions of this file are derived from CPython 3.13 (Objects/bytesobject.c, Objects/bytearrayobject.c and
  Objects/bytes_methods.c), Copyright (c) 2001-2024 Python Software Foundation; All Rights Reserved. They are used under
  the Python Software Foundation License Version 2 (see runtime/python/LICENSE.PSF). Changes: translated from C to C++ for
  the neos Python runtime plugin and adapted to its object model (see runtime/python/NOTICE).
*/

// Python's bytes and bytearray (CPython 3.13's Objects/bytesobject.c, bytearrayobject.c and bytes_methods.c: the same
// methods, results and messages), and the codecs of str.encode and bytes.decode (UTF-8, ASCII and Latin-1)

#include <algorithm>
#include <cctype>
#include <climits>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <map>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>
#include "objects.hpp"

namespace neos::python_runtime
{
    namespace detail
    {
        namespace
        {
            struct bytes_object
            {
                header h;
                std::string data;
                long long hash = -1;
            };

            struct bytes_iterator_object
            {
                header h;
                object* sequence;
                std::size_t index = 0u;
                ~bytes_iterator_object() { release(sequence); }
            };

            neos_py_type make_bytes_type(bool aMutable);
            neos_py_type make_bytes_iterator_type(char const* aName);
            neos_py_type sBytesType = make_bytes_type(false);
            neos_py_type sByteArrayType = make_bytes_type(true);
            neos_py_type sBytesIteratorType = make_bytes_iterator_type("bytes_iterator");
            neos_py_type sByteArrayIteratorType = make_bytes_iterator_type("bytearray_iterator");

            bool is_bytes(object* aObject) { return aObject != nullptr && aObject->type == &sBytesType.base; }
            bool is_bytearray(object* aObject) { return aObject != nullptr && aObject->type == &sByteArrayType.base; }
            bytes_object& as_bytes(object* aObject) { return *reinterpret_cast<bytes_object*>(aObject); }
            std::string& data_of(object* aObject) { return as_bytes(aObject).data; }
            char const* name_of(object* aObject) { return is_bytearray(aObject) ? "bytearray" : "bytes"; }

            // (a result of the same type as aSelf)
            object* same_type(object* aSelf, std::string aData)
            {
                return is_bytearray(aSelf) ? make_bytearray(std::move(aData)) : make_bytes(std::move(aData));
            }

            constexpr std::string_view kWhitespace = " \t\n\r\x0b\x0c";

            bool is_space(unsigned char c) { return kWhitespace.find(static_cast<char>(c)) != std::string_view::npos && c != 0u; }
            bool is_lower(unsigned char c) { return c >= 'a' && c <= 'z'; }
            bool is_upper(unsigned char c) { return c >= 'A' && c <= 'Z'; }
            bool is_alpha(unsigned char c) { return is_lower(c) || is_upper(c); }
            bool is_digit(unsigned char c) { return c >= '0' && c <= '9'; }
            unsigned char to_lower(unsigned char c) { return is_upper(c) ? static_cast<unsigned char>(c + 32) : c; }
            unsigned char to_upper(unsigned char c) { return is_lower(c) ? static_cast<unsigned char>(c - 32) : c; }

            std::string bytes_repr(std::string_view aData, bool aSmartQuotes = true)
            {
                char quote = '\'';
                if (aSmartQuotes && aData.find('\'') != std::string_view::npos && aData.find('"') == std::string_view::npos)
                    quote = '"';
                std::string result = "b";
                result += quote;
                for (auto ch : aData)
                {
                    auto const c = static_cast<unsigned char>(ch);
                    if (c == static_cast<unsigned char>(quote) || c == '\\')
                    {
                        result += '\\';
                        result += static_cast<char>(c);
                    }
                    else if (c == '\t')
                        result += "\\t";
                    else if (c == '\n')
                        result += "\\n";
                    else if (c == '\r')
                        result += "\\r";
                    else if (c < ' ' || c >= 0x7f)
                    {
                        static char const digits[] = "0123456789abcdef";
                        result += "\\x";
                        result += digits[c >> 4];
                        result += digits[c & 15];
                    }
                    else
                        result += static_cast<char>(c);
                }
                result += quote;
                return result;
            }

            // (a byte value: an int in range(0, 256))
            bool byte_value(object* aObject, unsigned char& aResult, char const* aMessage = "byte must be in range(0, 256)")
            {
                long long v = 0;
                if (!to_index(aObject, v))
                    return false;
                if (v < 0 || v > 255)
                {
                    error("ValueError", aMessage);
                    return false;
                }
                aResult = static_cast<unsigned char>(v);
                return true;
            }

            // (the bytes of bytes(x) and bytearray(x): an int (zeros), a bytes-like object, or an iterable of ints)
            bool bytes_from(object* aSource, std::string& aResult, char const* aName)
            {
                std::string_view data;
                if (bytes_like(aSource, data))
                {
                    aResult = std::string{ data };
                    return true;
                }
                auto const k = kind_of(aSource);
                if (k == NEOS_PY_STR)
                {
                    error("TypeError", "string argument without an encoding");
                    return false;
                }
                if (k == NEOS_PY_INT || k == NEOS_PY_BOOL)
                {
                    long long n = 0;
                    if (!to_index(aSource, n))
                        return false;
                    if (n < 0)
                    {
                        error("ValueError", "negative count");
                        return false;
                    }
                    aResult.assign(static_cast<std::size_t>(n), '\0');
                    return true;
                }
                auto const type = python_type(aSource);
                bool const iterable = k == NEOS_PY_LIST || k == NEOS_PY_TUPLE || k == NEOS_PY_DICT || k == NEOS_PY_RANGE || k == NEOS_PY_ITERATOR ||
                    (k == NEOS_PY_OTHER && type != nullptr && (type->iter != nullptr || type->next != nullptr || (type->getitem != nullptr && type->length != nullptr)));
                if (!iterable)
                {
                    if (k == NEOS_PY_OTHER && type != nullptr && type->index != nullptr)
                    {
                        // (an object with __index__: a count of zeros)
                        long long n = 0;
                        if (!to_index(aSource, n))
                            return false;
                        if (n < 0)
                        {
                            error("ValueError", "negative count");
                            return false;
                        }
                        aResult.assign(static_cast<std::size_t>(n), '\0');
                        return true;
                    }
                    error("TypeError", std::string{ "cannot convert '" } + type_name(aSource) + "' object to " + aName);
                    return false;
                }
                ref const it{ iter(aSource) };
                if (!it)
                    return false;
                for (;;)
                {
                    ref const item{ next(it.get()) };
                    if (!item)
                        return !failed();
                    if (!is_int_like(item.get()) && (python_type(item.get()) == nullptr || python_type(item.get())->index == nullptr))
                    {
                        error("TypeError", std::string{ "'" } + type_name(item.get()) + "' object cannot be interpreted as an integer");
                        return false;
                    }
                    unsigned char b = 0;
                    if (!byte_value(item.get(), b, "bytes must be in range(0, 256)"))
                        return false;
                    aResult += static_cast<char>(b);
                }
            }

            // --- helpers of bytes' methods

            std::string hex2(unsigned v)
            {
                static char const digits[] = "0123456789abcdef";
                return std::string{ digits[(v >> 4) & 15u] } + digits[v & 15u];
            }

            std::vector<char32_t> code_points_of(std::string_view aText)
            {
                std::vector<char32_t> result;
                for (std::size_t i = 0u; i < aText.size();)
                {
                    auto const c = static_cast<unsigned char>(aText[i]);
                    std::size_t const n = c < 0x80u ? 1u : c < 0xE0u ? 2u : c < 0xF0u ? 3u : 4u;
                    char32_t cp = n == 1u ? c : n == 2u ? (c & 0x1Fu) : n == 3u ? (c & 0x0Fu) : (c & 0x07u);
                    for (std::size_t j = 1u; j < n && i + j < aText.size(); ++j)
                        cp = (cp << 6) | (static_cast<unsigned char>(aText[i + j]) & 0x3Fu);
                    result.push_back(cp);
                    i += n;
                }
                return result;
            }

        }

        object* make_bytes(std::string aData)
        {
            return &(new bytes_object{ { { &sBytesType.base } }, std::move(aData) })->h.base;
        }

        object* make_bytearray(std::string aData)
        {
            return &(new bytes_object{ { { &sByteArrayType.base } }, std::move(aData) })->h.base;
        }

        bool memoryview_data(object* aObject, std::string_view& aData);

        std::map<neos_rt_object_type const*, buffer_provider>& buffer_providers()
        {
            static std::map<neos_rt_object_type const*, buffer_provider> sProviders;
            return sProviders;
        }

        namespace
        {
            buffer_provider const* provider_of(object* aObject)
            {
                if (aObject == nullptr)
                    return nullptr;
                auto const& providers = buffer_providers();
                auto const p = providers.find(aObject->type);
                return p != providers.end() ? &p->second : nullptr;
            }

            // (the buffer of an exporter: bytes, bytearray or a provider's object)
            bool exporter_buffer(object* aObject, buffer_info& aResult)
            {
                if (is_bytes(aObject) || is_bytearray(aObject))
                {
                    auto& data = data_of(aObject);
                    aResult = buffer_info{ data.data(), data.size(), is_bytes(aObject), "B", 1u };
                    return true;
                }
                auto const p = provider_of(aObject);
                return p != nullptr && p->get(aObject, aResult);
            }
        }

        bool bytes_like(object* aObject, std::string_view& aData)
        {
            if (memoryview_data(aObject, aData))
                return true;
            buffer_info buffer;
            if (!exporter_buffer(aObject, buffer))
                return false;
            aData = std::string_view{ buffer.data, buffer.size };
            return true;
        }

        object* make_unsigned(std::uint64_t aValue)
        {
            if (aValue <= static_cast<std::uint64_t>(std::numeric_limits<long long>::max()))
                return make_int(static_cast<long long>(aValue));
            ref const high{ make_int(static_cast<long long>(aValue >> 1)) };
            ref const two{ make_int(2) };
            ref const doubled{ binary(NEOS_PY_MULTIPLY, high.get(), two.get(), false) };
            ref const low{ make_int(static_cast<long long>(aValue & 1u)) };
            return binary(NEOS_PY_ADD, doubled.get(), low.get(), false);
        }

        int integer_value(object* aValue, long long& aSigned, std::uint64_t& aUnsigned)
        {
            long long n = 0;
            if (to_index(aValue, n))
            {
                aSigned = n;
                aUnsigned = static_cast<std::uint64_t>(n);
                return 0;
            }
            auto const value = builtin_value(aValue);
            if (!is_int_like(value))
                return -1;
            // (beyond 64 bits (signed))
            clear_error();
            auto const big = big_value(value);
            auto const digits = (big_negative(big) ? "-" : "") + big_digits(big, 10);   // (its magnitude's digits)
            errno = 0;
            if (big_negative(big))
            {
                auto const n = std::strtoll(digits.c_str(), nullptr, 10);
                if (errno == ERANGE)
                    return 3;
                aSigned = n;
                aUnsigned = static_cast<std::uint64_t>(n);
                return 0;
            }
            auto const u = std::strtoull(digits.c_str(), nullptr, 10);
            if (errno == ERANGE)
                return 2;
            aUnsigned = u;
            if (u <= static_cast<std::uint64_t>(std::numeric_limits<long long>::max()))
            {
                aSigned = static_cast<long long>(u);
                return 0;
            }
            return 1;
        }

        // --- memoryview (CPython 3.13's memoryobject.c: a one-dimensional view of an exporter's buffer (bytes, bytearray or a provider's object): its 
        // items [start, start + count * stride), of a native struct format)

        namespace
        {
            object* m_hex(object* aSelf, std::vector<object*> const& aArgs, object* aKwargs);

            struct memoryview_object
            {
                header h;
                object* base;               ///< (the exporter)
                std::size_t start = 0u;     ///< (the offset of its first item, in bytes)
                std::size_t count = 0u;     ///< (its items)
                long long stride = 1;       ///< (in bytes)
                std::string format = "B";
                std::size_t itemsize = 1u;
                bool readonly = true;
                bool released = false;
                std::string gathered;       ///< (the bytes of a view that is not contiguous, as bytes_like has them)
                void unexport()
                {
                    if (released)
                        return;
                    released = true;
                    if (auto const p = provider_of(base); p != nullptr && p->exports != nullptr)
                        p->exports(base, -1);
                }
                ~memoryview_object()
                {
                    unexport();
                    release(base);
                }
            };

            neos_py_type make_memoryview_type();
            neos_py_type sMemoryviewType = make_memoryview_type();

            bool is_memoryview(object* aObject) { return aObject != nullptr && aObject->type == &sMemoryviewType.base; }
            memoryview_object& as_memoryview(object* aObject) { return *reinterpret_cast<memoryview_object*>(aObject); }

            bool view_checked(memoryview_object const& aView)
            {
                if (aView.released)
                {
                    error("ValueError", "operation forbidden on released memoryview object");
                    return false;
                }
                return true;
            }

            // (a native single character format, prefixed with an optional '@' (get_native_fmtchar): its size (0: not one))
            std::size_t native_format(std::string_view aFormat, char& aCode)
            {
                if (!aFormat.empty() && aFormat[0] == '@')
                    aFormat.remove_prefix(1u);
                if (aFormat.size() != 1u)
                    return 0u;
                aCode = aFormat[0];
                switch (aCode)
                {
                case 'c': case 'b': case 'B': return 1u;
                case 'h': case 'H': return sizeof(short);
                case 'i': case 'I': return sizeof(int);
                case 'l': case 'L': return sizeof(long);
                case 'q': case 'Q': return sizeof(long long);
                case 'n': case 'N': return sizeof(std::size_t);
                case 'f': return sizeof(float);
                case 'd': return sizeof(double);
                case 'e': return 2u;
                case '?': return sizeof(bool);
                case 'P': return sizeof(void*);
                default: return 0u;
                }
            }

            bool is_byte_format(std::string_view aFormat)
            {
                char code = 0;
                return native_format(aFormat, code) != 0u && (code == 'B' || code == 'b' || code == 'c');
            }

            // (the view's exporter's buffer, and the number of the view's items that are in it (a bytearray can have been resized))
            std::size_t view_buffer(memoryview_object const& aView, buffer_info& aBuffer)
            {
                if (!exporter_buffer(aView.base, aBuffer))
                    return 0u;
                if (aView.count == 0u || aView.start + aView.itemsize > aBuffer.size)
                    return 0u;
                if (aView.stride > 0)
                    return std::min(aView.count, (aBuffer.size - aView.start - aView.itemsize) / static_cast<std::size_t>(aView.stride) + 1u);
                return std::min(aView.count, aView.start / static_cast<std::size_t>(-aView.stride) + 1u);
            }

            bool view_contiguous(memoryview_object const& aView)
            {
                return aView.count <= 1u || aView.stride == static_cast<long long>(aView.itemsize);
            }

            // (its bytes (C order))
            std::string_view view_data(memoryview_object& aView)
            {
                buffer_info buffer;
                auto const n = view_buffer(aView, buffer);
                if (view_contiguous(aView))
                    return n == 0u ? std::string_view{} : std::string_view{ buffer.data + aView.start, n * aView.itemsize };
                aView.gathered.clear();
                for (std::size_t i = 0u; i < n; ++i)
                    aView.gathered.append(buffer.data + static_cast<long long>(aView.start) + static_cast<long long>(i) * aView.stride, aView.itemsize);
                return aView.gathered;
            }

            object* make_view(object* aBase, std::size_t aStart, std::size_t aCount, long long aStride, std::string aFormat, std::size_t aItemsize, bool aReadonly)
            {
                retain(aBase);
                if (auto const p = provider_of(aBase); p != nullptr && p->exports != nullptr)
                    p->exports(aBase, 1);
                auto const view = new memoryview_object{ { { &sMemoryviewType.base } }, aBase, aStart, aCount, aStride, std::move(aFormat), aItemsize, aReadonly };
                return &view->h.base;
            }

            object* make_view(memoryview_object const& aView, std::size_t aStart, std::size_t aCount, long long aStride)
            {
                return make_view(aView.base, aStart, aCount, aStride, aView.format, aView.itemsize, aView.readonly);
            }

            // (an item (unpack_single))
            object* unpack_item(char const* aItem, std::string const& aFormat)
            {
                char code = 0;
                if (native_format(aFormat, code) == 0u)
                {
                    std::string_view const f = !aFormat.empty() && aFormat[0] == '@' ? std::string_view{ aFormat }.substr(1u) : std::string_view{ aFormat };
                    if (f.size() != 1u)
                        return error("NotImplementedError", "memoryview: unsupported format " + aFormat);
                    return error("NotImplementedError", "memoryview: format " + std::string{ f } + " not supported");
                }
                auto const get = [aItem](auto aValue) { std::memcpy(&aValue, aItem, sizeof aValue); return aValue; };
                switch (code)
                {
                case 'B': return make_int(static_cast<unsigned char>(*aItem));
                case 'b': return make_int(static_cast<signed char>(*aItem));
                case 'h': return make_int(get(short{}));
                case 'i': return make_int(get(int{}));
                case 'l': return make_int(get(long{}));
                case '?': return make_bool(*aItem != 0);
                case 'H': return make_int(get(static_cast<unsigned short>(0u)));
                case 'I': return make_unsigned(get(0u));
                case 'L': return make_unsigned(get(0ul));
                case 'q': return make_int(get(0ll));
                case 'Q': return make_unsigned(get(0ull));
                case 'n': return make_int(static_cast<long long>(get(std::ptrdiff_t{})));
                case 'N': return make_unsigned(get(std::size_t{}));
                case 'f': return make_float(get(0.0f));
                case 'd': return make_float(get(0.0));
                case 'e': return make_float(from_half(get(std::uint16_t{})));
                case 'c': return make_bytes(std::string(1u, *aItem));
                default: return make_unsigned(reinterpret_cast<std::uintptr_t>(get(static_cast<void*>(nullptr))));   // ('P')
                }
            }

            // (an item's value (pack_single): the item's bytes (aItem))
            bool pack_item(char* aItem, object* aValue, std::string const& aFormat)
            {
                char code = 0;
                std::string const f = !aFormat.empty() && aFormat[0] == '@' ? aFormat.substr(1u) : aFormat;
                if (native_format(aFormat, code) == 0u)
                    return (error("NotImplementedError", "memoryview: format " + f + " not supported"), false);
                auto const type_error = [&]() { return (error("TypeError", "memoryview: invalid type for format '" + f + "'"), false); };
                auto const value_error = [&]() { return (error("ValueError", "memoryview: invalid value for format '" + f + "'"), false); };
                auto const put = [aItem](auto aValue) { std::memcpy(aItem, &aValue, sizeof aValue); return true; };
                switch (code)
                {
                case 'f': case 'd': case 'e':
                    {
                        double d = 0.0;
                        if (!to_double(aValue, d))
                        {
                            clear_error();
                            return type_error();
                        }
                        if (code == 'f')
                            return put(static_cast<float>(d));
                        if (code == 'd')
                            return put(d);
                        if (std::isfinite(d) && std::fabs(d) >= 65520.0)
                            return value_error();
                        return put(to_half(d));
                    }
                case '?':
                    {
                        auto const t = truth(aValue);
                        return t < 0 ? false : put(t == 1);
                    }
                case 'c':
                    {
                        if (!is_bytes(aValue))
                            return type_error();
                        if (data_of(aValue).size() != 1u)
                            return value_error();
                        *aItem = data_of(aValue)[0];
                        return true;
                    }
                default:
                    break;
                }
                long long s = 0;
                std::uint64_t u = 0u;
                auto const r = integer_value(aValue, s, u);
                if (r < 0)
                {
                    clear_error();
                    return type_error();
                }
                bool const isUnsigned = code == 'B' || code == 'H' || code == 'I' || code == 'L' || code == 'Q' || code == 'N' || code == 'P';
                if (isUnsigned ? (r >= 2 || (r == 0 && s < 0)) : r != 0)
                    return value_error();
                auto const in = [&](auto aMinimum, auto aMaximum)
                {
                    return isUnsigned ? u <= static_cast<std::uint64_t>(aMaximum) : (s >= static_cast<long long>(aMinimum) && s <= static_cast<long long>(aMaximum));
                };
                switch (code)
                {
                case 'b': return in(SCHAR_MIN, SCHAR_MAX) ? put(static_cast<signed char>(s)) : value_error();
                case 'h': return in(SHRT_MIN, SHRT_MAX) ? put(static_cast<short>(s)) : value_error();
                case 'i': return in(INT_MIN, INT_MAX) ? put(static_cast<int>(s)) : value_error();
                case 'l': return in(LONG_MIN, LONG_MAX) ? put(static_cast<long>(s)) : value_error();
                case 'q': return put(s);
                case 'n': return put(static_cast<std::ptrdiff_t>(s));
                case 'B': return in(0, UCHAR_MAX) ? put(static_cast<unsigned char>(u)) : value_error();
                case 'H': return in(0, USHRT_MAX) ? put(static_cast<unsigned short>(u)) : value_error();
                case 'I': return in(0, UINT_MAX) ? put(static_cast<unsigned int>(u)) : value_error();
                case 'L': return in(0, ULONG_MAX) ? put(static_cast<unsigned long>(u)) : value_error();
                case 'Q': return put(u);
                case 'N': return in(0, SIZE_MAX) ? put(static_cast<std::size_t>(u)) : value_error();
                default: return in(0, UINTPTR_MAX) ? put(reinterpret_cast<void*>(static_cast<std::uintptr_t>(u))) : value_error();   // ('P')
                }
            }

            bool view_items(memoryview_object& aView, std::vector<object*>& aItems)
            {
                buffer_info buffer;
                auto const n = view_buffer(aView, buffer);
                for (std::size_t i = 0u; i < n; ++i)
                {
                    auto const item = unpack_item(buffer.data + static_cast<long long>(aView.start) + static_cast<long long>(i) * aView.stride, aView.format);
                    if (item == nullptr)
                    {
                        for (auto v : aItems)
                            release(v);
                        aItems.clear();
                        return false;
                    }
                    aItems.push_back(item);
                }
                return true;
            }

            object* view_cast(memoryview_object& aView, object* aFormat, object* aShape)
            {
                if (kind_of(aFormat) != NEOS_PY_STR)
                    return error("TypeError", std::string{ "cast() argument 'format' must be str, not " } + type_name(aFormat));
                if (!view_contiguous(aView))
                    return error("TypeError", "memoryview: casts are restricted to C-contiguous views");
                std::size_t dimensions = 1u;
                if (aShape != nullptr)
                {
                    if (kind_of(aShape) != NEOS_PY_LIST && kind_of(aShape) != NEOS_PY_TUPLE)
                        return error("TypeError", "shape must be a list or a tuple");
                    dimensions = static_cast<std::size_t>(length(aShape));
                    if (dimensions > 64u)
                        return error("ValueError", "memoryview: number of dimensions must not exceed 64");
                }
                std::string const format = as<str_object>(aFormat)->value;
                char destination = 0;
                auto const itemsize = native_format(format, destination);
                if (itemsize == 0u)
                    return error("ValueError", "memoryview: destination format must be a native single character format prefixed with an optional '@'");
                if (!is_byte_format(aView.format) && !is_byte_format(format))
                    return error("TypeError", "memoryview: cannot cast between two non-byte formats");
                buffer_info buffer;
                auto const bytes = view_buffer(aView, buffer) * aView.itemsize;
                if (bytes % itemsize != 0u)
                    return error("TypeError", "memoryview: length is not a multiple of itemsize");
                if (aShape != nullptr)
                {
                    std::vector<object*> items;
                    if (!collect(aShape, items))
                        return nullptr;
                    std::size_t total = itemsize;
                    for (auto i : items)
                    {
                        ref const owner{ i };
                        long long x = 0;
                        if (kind_of(i) != NEOS_PY_INT)
                            return error("TypeError", "memoryview.cast(): elements of shape must be integers");
                        if (!to_index(i, x))
                            return nullptr;
                        if (x <= 0)
                            return error("ValueError", "memoryview.cast(): elements of shape must be integers > 0");
                        total *= static_cast<std::size_t>(x);
                    }
                    if (dimensions == 0u)
                        total = itemsize;
                    if (total != bytes)
                        return error("TypeError", "memoryview: product(shape) * itemsize != buffer size");
                    if (dimensions != 1u)
                        return error("NotImplementedError", "memoryview: only one-dimensional views are supported");
                }
                return make_view(aView.base, aView.start, bytes / itemsize, static_cast<long long>(itemsize), format, itemsize, aView.readonly);
            }

            object* view_method(neos_py_context*, void* aData, object* aArgs, object* aKwargs)
            {
                auto const& items = as<tuple_object>(aArgs)->items;
                auto& view = as_memoryview(items.at(0u));
                std::vector<object*> const rest{ items.begin() + 1, items.end() };
                std::string_view const name{ static_cast<char const*>(aData) };
                std::string const qualified = "memoryview." + std::string{ name };
                std::vector<object*> a;
                if (name == "release" || name == "__exit__")
                {
                    if (name == "release" ? !parse_arguments(qualified.c_str(), rest, aKwargs, {}, 0u, 0u, a) : !no_keywords(aKwargs, qualified.c_str()))
                        return nullptr;
                    view.unexport();
                    return name == "__exit__" ? make_bool(false) : (retain(none()), none());
                }
                if (!view_checked(view))
                    return nullptr;
                if (name == "__enter__")
                    return (retain(items[0]), items[0]);
                if (name == "tobytes")
                {
                    if (!parse_arguments(qualified.c_str(), rest, aKwargs, { "order" }, 0u, 0u, a))
                        return nullptr;
                    if (a[0] != nullptr && a[0] != none())
                    {
                        auto const order = str_of_value(a[0]);
                        if (order == nullptr)
                            return error("TypeError", std::string{ "tobytes() argument 'order' must be str or None, not " } + type_name(a[0]));
                        if (*order != "C" && *order != "F" && *order != "A")
                            return error("ValueError", "order must be 'C', 'F' or 'A'");
                    }
                    return make_bytes(std::string{ view_data(view) });
                }
                if (name == "tolist")
                {
                    if (!parse_arguments(qualified.c_str(), rest, aKwargs, {}, 0u, 0u, a))
                        return nullptr;
                    std::vector<object*> values;
                    if (!view_items(view, values))
                        return nullptr;
                    return make_list(std::move(values));
                }
                if (name == "hex")
                {
                    ref const data{ make_bytes(std::string{ view_data(view) }) };
                    return m_hex(data.get(), rest, aKwargs);
                }
                if (name == "toreadonly")
                {
                    if (!parse_arguments(qualified.c_str(), rest, aKwargs, {}, 0u, 0u, a))
                        return nullptr;
                    return make_view(view.base, view.start, view.count, view.stride, view.format, view.itemsize, true);
                }
                // (cast)
                if (!parse_arguments(qualified.c_str(), rest, aKwargs, { "format", "shape" }, 1u, 0u, a))
                    return nullptr;
                return view_cast(view, a[0], a[1]);
            }

            // (another object's buffer as a view's: its format, items and their bytes)
            bool other_buffer(object* aObject, std::string& aFormat, std::size_t& aItemsize, std::size_t& aCount, std::string& aBytes)
            {
                if (is_memoryview(aObject))
                {
                    auto& other = as_memoryview(aObject);
                    if (!view_checked(other))
                        return false;
                    buffer_info buffer;
                    aCount = view_buffer(other, buffer);
                    aFormat = other.format;
                    aItemsize = other.itemsize;
                    aBytes = std::string{ view_data(other) };
                    return true;
                }
                buffer_info buffer;
                if (!exporter_buffer(aObject, buffer))
                    return false;
                aFormat = buffer.format;
                aItemsize = buffer.itemsize;
                aCount = buffer.size / buffer.itemsize;
                aBytes = std::string{ buffer.data == nullptr ? std::string_view{} : std::string_view{ buffer.data, buffer.size } };
                return true;
            }

            std::string bare_format(std::string const& aFormat) { return !aFormat.empty() && aFormat[0] == '@' ? aFormat.substr(1u) : aFormat; }

            neos_py_type make_memoryview_type()
            {
                auto type = make_type("memoryview", "memoryview", retain_object, release_object<memoryview_object>);
                type.repr = [](neos_py_context*, object* aSelf) -> object*
                {
                    return make_str((as_memoryview(aSelf).released ? "<released memory at " : "<memory at ") + object_address(aSelf) + ">");
                };
                type.getattr = [](neos_py_context*, object* aSelf, char const* aName) -> object*
                {
                    auto& view = as_memoryview(aSelf);
                    static char const* const sMethods[] = { "tobytes", "tolist", "hex", "release", "toreadonly", "cast", "__enter__", "__exit__" };
                    for (auto m : sMethods)
                        if (std::strcmp(m, aName) == 0)
                            return make_function(m, view_method, const_cast<char*>(m), aSelf);
                    std::string_view const name{ aName };
                    if (!view_checked(view))
                        return nullptr;
                    if (name == "obj")
                        return (retain(view.base), view.base);
                    if (name == "readonly")
                        return make_bool(view.readonly);
                    buffer_info buffer;
                    auto const n = static_cast<long long>(view_buffer(view, buffer));
                    if (name == "nbytes")
                        return make_int(n * static_cast<long long>(view.itemsize));
                    if (name == "itemsize")
                        return make_int(static_cast<long long>(view.itemsize));
                    if (name == "ndim")
                        return make_int(1);
                    if (name == "format")
                        return make_str(view.format);
                    if (name == "shape")
                        return make_tuple({ make_int(n) });
                    if (name == "strides")
                        return make_tuple({ make_int(view.stride) });
                    if (name == "suboffsets")
                        return make_tuple();
                    if (name == "c_contiguous" || name == "f_contiguous" || name == "contiguous")
                        return make_bool(view_contiguous(view));
                    return error("AttributeError", std::string{ "'memoryview' object has no attribute '" } + aName + "'");
                };
                type.length = [](neos_py_context*, object* aSelf) -> long long
                {
                    auto const& view = as_memoryview(aSelf);
                    if (!view_checked(view))
                        return -1;
                    buffer_info buffer;
                    return static_cast<long long>(view_buffer(view, buffer));
                };
                type.getitem = [](neos_py_context*, object* aSelf, object* aKey) -> object*
                {
                    auto const& view = as_memoryview(aSelf);
                    if (!view_checked(view))
                        return nullptr;
                    buffer_info buffer;
                    auto const n = static_cast<long long>(view_buffer(view, buffer));
                    if (kind_of(aKey) == NEOS_PY_SLICE)
                    {
                        long long start = 0, stop = 0, step = 0;
                        if (!slice_indices(aKey, n, start, stop, step))
                            return nullptr;
                        long long const count = step > 0 ? (stop > start ? (stop - start + step - 1) / step : 0) : (start > stop ? (start - stop - step - 1) / -step : 0);
                        return make_view(view, static_cast<std::size_t>(static_cast<long long>(view.start) + start * view.stride), static_cast<std::size_t>(count), 
                            view.stride * step);
                    }
                    if (kind_of(aKey) == NEOS_PY_TUPLE)
                        return error("TypeError", "memoryview: invalid slice key");
                    long long index = 0;
                    if (!to_index(aKey, index))
                    {
                        clear_error();
                        return error("TypeError", "memoryview: invalid slice key");
                    }
                    if (index < 0)
                        index += n;
                    if (index < 0 || index >= n)
                        return error("IndexError", "index out of bounds on dimension 1");
                    return unpack_item(buffer.data + static_cast<long long>(view.start) + index * view.stride, view.format);
                };
                type.setitem = [](neos_py_context*, object* aSelf, object* aKey, object* aValue) -> int
                {
                    auto& view = as_memoryview(aSelf);
                    if (!view_checked(view))
                        return -1;
                    if (view.readonly)
                    {
                        error("TypeError", "cannot modify read-only memory");
                        return -1;
                    }
                    if (aValue == nullptr)
                    {
                        error("TypeError", "cannot delete memory");
                        return -1;
                    }
                    buffer_info buffer;
                    auto const n = static_cast<long long>(view_buffer(view, buffer));
                    auto const item = [&](long long aIndex) { return buffer.data + static_cast<long long>(view.start) + aIndex * view.stride; };
                    if (kind_of(aKey) == NEOS_PY_SLICE)
                    {
                        long long start = 0, stop = 0, step = 0;
                        if (!slice_indices(aKey, n, start, stop, step))
                            return -1;
                        long long const count = step > 0 ? (stop > start ? (stop - start + step - 1) / step : 0) : (start > stop ? (start - stop - step - 1) / -step : 0);
                        std::string format, bytes;
                        std::size_t itemsize = 0u, items = 0u;
                        if (!other_buffer(aValue, format, itemsize, items, bytes))
                        {
                            if (!failed())
                                error("TypeError", std::string{ "a bytes-like object is required, not '" } + type_name(aValue) + "'");
                            return -1;
                        }
                        if (bare_format(format) != bare_format(view.format) || itemsize != view.itemsize || static_cast<long long>(items) != count)
                        {
                            error("ValueError", "memoryview assignment: lvalue and rvalue have different structures");
                            return -1;
                        }
                        for (long long i = 0; i < count; ++i)
                            std::memcpy(item(start + i * step), bytes.data() + i * static_cast<long long>(itemsize), itemsize);
                        return 0;
                    }
                    long long index = 0;
                    if (!to_index(aKey, index))
                    {
                        clear_error();
                        error("TypeError", "memoryview: invalid slice key");
                        return -1;
                    }
                    if (index < 0)
                        index += n;
                    if (index < 0 || index >= n)
                    {
                        error("IndexError", "index out of bounds on dimension 1");
                        return -1;
                    }
                    std::string value(view.itemsize, '\0');
                    if (!pack_item(value.data(), aValue, view.format))
                        return -1;
                    std::memcpy(item(index), value.data(), view.itemsize);
                    return 0;
                };
                type.iter = [](neos_py_context*, object* aSelf) -> object*
                {
                    auto& view = as_memoryview(aSelf);
                    if (!view_checked(view))
                        return nullptr;
                    std::vector<object*> values;
                    if (!view_items(view, values))
                        return nullptr;
                    ref const list{ make_list(std::move(values)) };
                    return iter(list.get());
                };
                type.compare = [](neos_py_context*, int aOperation, object* aLhs, object* aRhs) -> object*
                {
                    if (aOperation != NEOS_PY_EQ && aOperation != NEOS_PY_NE)
                        return not_implemented();
                    if (!is_memoryview(aLhs))
                        std::swap(aLhs, aRhs);
                    auto& view = as_memoryview(aLhs);
                    bool const eq = aOperation == NEOS_PY_EQ;
                    if (view.released || (is_memoryview(aRhs) && as_memoryview(aRhs).released))
                        return make_bool((aLhs == aRhs) == eq);
                    std::string format, bytes;
                    std::size_t itemsize = 0u, count = 0u;
                    if (!other_buffer(aRhs, format, itemsize, count, bytes))
                    {
                        clear_error();
                        return not_implemented();
                    }
                    buffer_info buffer;
                    auto const n = view_buffer(view, buffer);
                    if (n != count)
                        return make_bool(!eq);
                    std::vector<object*> mine;
                    if (!view_items(view, mine))
                        return nullptr;
                    std::vector<object*> theirs;
                    for (std::size_t i = 0u; i < count; ++i)
                    {
                        auto const item = unpack_item(bytes.data() + i * itemsize, format);
                        if (item == nullptr)
                            break;
                        theirs.push_back(item);
                    }
                    ref const a{ make_list(std::move(mine)) };
                    ref const b{ make_list(std::move(theirs)) };
                    if (failed())
                        return nullptr;
                    auto const& x = as<list_object>(a.get())->items;
                    auto const& y = as<list_object>(b.get())->items;
                    for (std::size_t i = 0u; i < count; ++i)
                    {
                        // (items are compared as values (a NaN is not equal to itself))
                        ref const r{ compare(NEOS_PY_EQ, x[i], y[i]) };
                        if (!r)
                            return nullptr;
                        auto const t = truth(r.get());
                        if (t < 0)
                            return nullptr;
                        if (t == 0)
                            return make_bool(!eq);
                    }
                    return make_bool(eq);
                };
                type.hash = [](neos_py_context*, object* aSelf, long long* aResult) -> int
                {
                    auto& view = as_memoryview(aSelf);
                    if (!view_checked(view))
                        return -1;
                    if (!view.readonly)
                    {
                        error("ValueError", "cannot hash writable memoryview object");
                        return -1;
                    }
                    if (!is_byte_format(view.format))
                    {
                        error("ValueError", "memoryview: hashing is restricted to formats 'B', 'b' or 'c'");
                        return -1;
                    }
                    long long h = 0;
                    if (!hash_of(view.base, h))
                        return -1;
                    *aResult = bytes_hash(view_data(view));
                    return 0;
                };
                return type;
            }
        }

        bool memoryview_data(object* aObject, std::string_view& aData)
        {
            if (!is_memoryview(aObject))
                return false;
            aData = view_data(as_memoryview(aObject));
            return true;
        }

        bool buffer_of(object* aObject, buffer_info& aResult)
        {
            if (!is_memoryview(aObject))
                return exporter_buffer(aObject, aResult);
            auto& view = as_memoryview(aObject);
            if (view.released || !view_contiguous(view))
                return false;
            buffer_info buffer;
            auto const n = view_buffer(view, buffer);
            aResult = buffer_info{ n == 0u ? buffer.data : buffer.data + view.start, n * view.itemsize, view.readonly, view.format.c_str(), view.itemsize };
            return true;
        }

        object* b_memoryview(std::vector<object*> const& aArgs, object* aKwargs)
        {
            std::vector<object*> a;
            if (!parse_arguments("memoryview", aArgs, aKwargs, { "object" }, 1u, 0u, a))
                return nullptr;
            if (is_memoryview(a[0]))
            {
                auto const& view = as_memoryview(a[0]);
                if (!view_checked(view))
                    return nullptr;
                return make_view(view, view.start, view.count, view.stride);
            }
            buffer_info buffer;
            if (!exporter_buffer(a[0], buffer))
                return error("TypeError", std::string{ "memoryview: a bytes-like object is required, not '" } + type_name(a[0]) + "'");
            return make_view(a[0], 0u, buffer.size / buffer.itemsize, static_cast<long long>(buffer.itemsize), buffer.format, buffer.itemsize, buffer.readonly);
        }

        namespace
        {
            // (a bytes-like argument: TypeError if it is not one)
            bool bytes_argument(object* aObject, std::string_view& aData)
            {
                if (bytes_like(aObject, aData))
                    return true;
                error("TypeError", std::string{ "a bytes-like object is required, not '" } + type_name(aObject) + "'");
                return false;
            }

            // (the subsequence argument of count, find, index, ...: a bytes-like object or a byte (an int))
            bool subsequence_argument(object* aObject, std::string& aResult)
            {
                std::string_view data;
                if (bytes_like(aObject, data))
                {
                    aResult = std::string{ data };
                    return true;
                }
                if (is_int_like(aObject) || (python_type(aObject) != nullptr && python_type(aObject)->index != nullptr))
                {
                    unsigned char b = 0;
                    if (!byte_value(aObject, b))
                        return false;
                    aResult = std::string(1u, static_cast<char>(b));
                    return true;
                }
                error("TypeError", std::string{ "argument should be integer or bytes-like object, not '" } + type_name(aObject) + "'");
                return false;
            }

            // (the start and end arguments of count, find, ...: as slice bounds)
            bool slice_bounds(object* aStart, object* aEnd, std::size_t aLength, std::size_t& aBegin, std::size_t& aStop)
            {
                auto const bound = [&](object* aValue, long long aDefault, long long& aResult)
                {
                    if (aValue == nullptr || kind_of(aValue) == NEOS_PY_NONE)
                    {
                        aResult = aDefault;
                        return true;
                    }
                    if (!is_int_like(aValue) && (python_type(aValue) == nullptr || python_type(aValue)->index == nullptr))
                    {
                        error("TypeError", "slice indices must be integers or None or have an __index__ method");
                        return false;
                    }
                    if (!to_index(aValue, aResult))
                        return false;
                    if (aResult < 0)
                        aResult = std::max(0ll, aResult + static_cast<long long>(aLength));
                    aResult = std::min(aResult, static_cast<long long>(aLength));
                    return true;
                };
                long long s = 0, e = 0;
                if (!bound(aStart, 0, s) || !bound(aEnd, static_cast<long long>(aLength), e))
                    return false;
                aBegin = static_cast<std::size_t>(s);
                aStop = static_cast<std::size_t>(e);
                return true;
            }

            // (find (aReverse: rfind): the position, or -1)
            long long find_in(std::string_view aData, std::string_view aSub, std::size_t aBegin, std::size_t aEnd, bool aReverse)
            {
                if (aBegin > aEnd || aEnd - aBegin < aSub.size())
                    return aBegin <= aEnd && aSub.empty() ? static_cast<long long>(aReverse ? aEnd : aBegin) : -1;
                auto const window = aData.substr(aBegin, aEnd - aBegin);
                auto const i = aReverse ? window.rfind(aSub) : window.find(aSub);
                return i == std::string_view::npos ? -1 : static_cast<long long>(aBegin + i);
            }

            using method_function = object* (*)(object* aSelf, std::vector<object*> const& aArgs, object* aKwargs);

            struct bytes_method
            {
                char const* name;
                bool mutating;  ///< (a bytearray's)
                method_function function;
            };

            std::string qualified(object* aSelf, char const* aName) { return std::string{ name_of(aSelf) } + "." + aName; }

            bool arguments(object* aSelf, char const* aName, std::vector<object*> const& aArgs, object* aKwargs, std::vector<char const*> const& aNames,
                std::size_t aRequired, std::size_t aPositionalOnly, std::vector<object*>& aResult)
            {
                return parse_arguments(qualified(aSelf, aName).c_str(), aArgs, aKwargs, aNames, aRequired, aPositionalOnly, aResult);
            }

            object* m_decode(object* aSelf, std::vector<object*> const& aArgs, object* aKwargs)
            {
                std::vector<object*> a;
                if (!arguments(aSelf, "decode", aArgs, aKwargs, { "encoding", "errors" }, 0u, 0u, a))
                    return nullptr;
                std::string encoding = "utf-8", errors = "strict";
                char const* const names[] = { "encoding", "errors" };
                for (std::size_t i = 0u; i < 2u; ++i)
                    if (a[i] != nullptr)
                    {
                        if (kind_of(a[i]) != NEOS_PY_STR)
                            return error("TypeError", std::string{ "decode() argument '" } + names[i] + "' must be str, not " + type_name(a[i]));
                        (i == 0u ? encoding : errors) = as<str_object>(a[i])->value;
                    }
                std::string text;
                if (!decode(data_of(aSelf), encoding, errors, text))
                    return nullptr;
                return make_str(std::move(text));
            }

            object* m_hex(object* aSelf, std::vector<object*> const& aArgs, object* aKwargs)
            {
                std::vector<object*> a;
                if (!arguments(aSelf, "hex", aArgs, aKwargs, { "sep", "bytes_per_sep" }, 0u, 0u, a))
                    return nullptr;
                std::string separator;
                if (a[0] != nullptr)
                {
                    std::string_view data;
                    if (kind_of(a[0]) == NEOS_PY_STR)
                        separator = as<str_object>(a[0])->value;
                    else if (bytes_like(a[0], data))
                        separator = std::string{ data };
                    else
                        return error("TypeError", std::string{ "object of type '" } + type_name(a[0]) + "' has no len()");
                    if (code_points_of(separator).size() != 1u)
                        return error("ValueError", "sep must be length 1.");
                    if (static_cast<unsigned char>(separator[0]) >= 0x80u)
                        return error("ValueError", "sep must be ASCII.");
                }
                long long per = 1;
                if (a[1] != nullptr && !to_index(a[1], per))
                    return nullptr;
                auto const& data = data_of(aSelf);
                std::string result;
                auto const group = static_cast<std::size_t>(per < 0 ? -per : per);
                for (std::size_t i = 0u; i < data.size(); ++i)
                {
                    if (!separator.empty() && group != 0u && i != 0u)
                    {
                        // (groups of bytes_per_sep bytes: from the right if it is positive, else from the left)
                        bool const boundary = per > 0 ? (data.size() - i) % group == 0u : i % group == 0u;
                        if (boundary)
                            result += separator;
                    }
                    result += hex2(static_cast<unsigned char>(data[i]));
                }
                return make_str(std::move(result));
            }

            object* m_count_find(object* aSelf, std::vector<object*> const& aArgs, object* aKwargs, char const* aName, int aWhich)
            {
                // (aWhich: 0 count, 1 find, 2 rfind, 3 index, 4 rindex)
                std::vector<object*> a;
                if (!arguments(aSelf, aName, aArgs, aKwargs, { "sub", "start", "end" }, 1u, 3u, a))
                    return nullptr;
                std::string sub;
                if (!subsequence_argument(a[0], sub))
                    return nullptr;
                auto const& data = data_of(aSelf);
                std::size_t begin = 0u, end = 0u;
                if (!slice_bounds(a[1], a[2], data.size(), begin, end))
                    return nullptr;
                if (aWhich == 0)
                {
                    if (begin > end)
                        return make_int(0);
                    if (sub.empty())
                        return make_int(static_cast<long long>(end - begin + 1u));
                    long long n = 0;
                    for (auto i = data.find(sub, begin); i != std::string::npos && i + sub.size() <= end; i = data.find(sub, i + sub.size()))
                        ++n;
                    return make_int(n);
                }
                auto const position = find_in(data, sub, begin, end, aWhich == 2 || aWhich == 4);
                if (position < 0 && aWhich >= 3)
                    return error("ValueError", "subsection not found");
                return make_int(position);
            }

            object* m_count(object* s, std::vector<object*> const& a, object* k) { return m_count_find(s, a, k, "count", 0); }
            object* m_find(object* s, std::vector<object*> const& a, object* k) { return m_count_find(s, a, k, "find", 1); }
            object* m_rfind(object* s, std::vector<object*> const& a, object* k) { return m_count_find(s, a, k, "rfind", 2); }
            object* m_index(object* s, std::vector<object*> const& a, object* k) { return m_count_find(s, a, k, "index", 3); }
            object* m_rindex(object* s, std::vector<object*> const& a, object* k) { return m_count_find(s, a, k, "rindex", 4); }

            object* m_affix(object* aSelf, std::vector<object*> const& aArgs, object* aKwargs, bool aStart)
            {
                char const* const name = aStart ? "startswith" : "endswith";
                std::vector<object*> a;
                if (!arguments(aSelf, name, aArgs, aKwargs, { "prefix", "start", "end" }, 1u, 3u, a))
                    return nullptr;
                auto const& data = data_of(aSelf);
                std::size_t begin = 0u, end = 0u;
                if (!slice_bounds(a[1], a[2], data.size(), begin, end))
                    return nullptr;
                std::vector<object*> affixes;
                if (kind_of(a[0]) == NEOS_PY_TUPLE)
                    affixes = as<tuple_object>(a[0])->items;
                else
                    affixes.push_back(a[0]);
                for (auto affix : affixes)
                {
                    std::string_view sub;
                    if (!bytes_like(affix, sub))
                        return error("TypeError", std::string{ name } + (kind_of(a[0]) == NEOS_PY_TUPLE ? " first arg must be bytes or a tuple of bytes, not " :
                            " first arg must be bytes or a tuple of bytes, not ") + type_name(affix));
                    if (begin > data.size() || (begin > end))
                        continue;
                    if (end - begin < sub.size())
                        continue;
                    auto const window = std::string_view{ data }.substr(begin, end - begin);
                    if (aStart ? window.starts_with(sub) : window.ends_with(sub))
                        return make_bool(true);
                }
                return make_bool(false);
            }
            object* m_startswith(object* s, std::vector<object*> const& a, object* k) { return m_affix(s, a, k, true); }
            object* m_endswith(object* s, std::vector<object*> const& a, object* k) { return m_affix(s, a, k, false); }

            object* m_join(object* aSelf, std::vector<object*> const& aArgs, object* aKwargs)
            {
                std::vector<object*> a;
                if (!arguments(aSelf, "join", aArgs, aKwargs, { "iterable_of_bytes" }, 1u, 1u, a))
                    return nullptr;
                std::vector<object*> items;
                if (!collect(a[0], items))
                    return nullptr;
                std::string result;
                object* failure = nullptr;
                for (std::size_t i = 0u; i < items.size() && failure == nullptr; ++i)
                {
                    std::string_view data;
                    if (!bytes_like(items[i], data))
                    {
                        error("TypeError", "sequence item " + std::to_string(i) + ": expected a bytes-like object, " + type_name(items[i]) + " found");
                        failure = items[i];
                        break;
                    }
                    if (i != 0u)
                        result += data_of(aSelf);
                    result += data;
                }
                for (auto item : items)
                    release(item);
                if (failure != nullptr)
                    return nullptr;
                return same_type(aSelf, std::move(result));
            }

            object* m_split(object* aSelf, std::vector<object*> const& aArgs, object* aKwargs, bool aReverse)
            {
                std::vector<object*> a;
                if (!arguments(aSelf, aReverse ? "rsplit" : "split", aArgs, aKwargs, { "sep", "maxsplit" }, 0u, 0u, a))
                    return nullptr;
                long long maxsplit = -1;
                if (a[1] != nullptr && !to_index(a[1], maxsplit))
                    return nullptr;
                if (maxsplit < 0)
                    maxsplit = std::numeric_limits<long long>::max();
                std::string_view const data = data_of(aSelf);
                std::vector<std::string> parts;
                if (a[0] == nullptr || kind_of(a[0]) == NEOS_PY_NONE)
                {
                    // (runs of whitespace)
                    if (!aReverse)
                    {
                        std::size_t i = 0u;
                        while (i < data.size())
                        {
                            while (i < data.size() && is_space(static_cast<unsigned char>(data[i])))
                                ++i;
                            if (i == data.size())
                                break;
                            if (static_cast<long long>(parts.size()) == maxsplit)
                            {
                                auto end = data.size();
                                parts.emplace_back(data.substr(i, end - i));
                                break;
                            }
                            auto j = i;
                            while (j < data.size() && !is_space(static_cast<unsigned char>(data[j])))
                                ++j;
                            parts.emplace_back(data.substr(i, j - i));
                            i = j;
                        }
                    }
                    else
                    {
                        std::size_t i = data.size();
                        while (i > 0u)
                        {
                            while (i > 0u && is_space(static_cast<unsigned char>(data[i - 1u])))
                                --i;
                            if (i == 0u)
                                break;
                            if (static_cast<long long>(parts.size()) == maxsplit)
                            {
                                parts.emplace_back(data.substr(0u, i));
                                break;
                            }
                            auto j = i;
                            while (j > 0u && !is_space(static_cast<unsigned char>(data[j - 1u])))
                                --j;
                            parts.emplace_back(data.substr(j, i - j));
                            i = j;
                        }
                        std::reverse(parts.begin(), parts.end());
                    }
                }
                else
                {
                    std::string_view separator;
                    if (!bytes_argument(a[0], separator))
                        return nullptr;
                    if (separator.empty())
                        return error("ValueError", "empty separator");
                    if (!aReverse)
                    {
                        std::size_t i = 0u;
                        for (long long n = 0; ; ++n)
                        {
                            auto const j = n < maxsplit ? data.find(separator, i) : std::string_view::npos;
                            if (j == std::string_view::npos)
                            {
                                parts.emplace_back(data.substr(i));
                                break;
                            }
                            parts.emplace_back(data.substr(i, j - i));
                            i = j + separator.size();
                        }
                    }
                    else
                    {
                        std::size_t end = data.size();
                        for (long long n = 0; ; ++n)
                        {
                            auto const j = n < maxsplit && end >= separator.size() ? data.substr(0u, end).rfind(separator) : std::string_view::npos;
                            if (j == std::string_view::npos)
                            {
                                parts.emplace_back(data.substr(0u, end));
                                break;
                            }
                            parts.emplace_back(data.substr(j + separator.size(), end - j - separator.size()));
                            end = j;
                        }
                        std::reverse(parts.begin(), parts.end());
                    }
                }
                std::vector<object*> items;
                for (auto& part : parts)
                    items.push_back(same_type(aSelf, std::move(part)));
                return make_list(std::move(items));
            }
            object* m_split_(object* s, std::vector<object*> const& a, object* k) { return m_split(s, a, k, false); }
            object* m_rsplit(object* s, std::vector<object*> const& a, object* k) { return m_split(s, a, k, true); }

            object* m_strip(object* aSelf, std::vector<object*> const& aArgs, object* aKwargs, int aWhich)
            {
                // (aWhich: 0 strip, 1 lstrip, 2 rstrip)
                char const* const names[] = { "strip", "lstrip", "rstrip" };
                std::vector<object*> a;
                if (!arguments(aSelf, names[aWhich], aArgs, aKwargs, { "bytes" }, 0u, 1u, a))
                    return nullptr;
                std::string characters{ kWhitespace };
                if (a[0] != nullptr && kind_of(a[0]) != NEOS_PY_NONE)
                {
                    std::string_view data;
                    if (!bytes_argument(a[0], data))
                        return nullptr;
                    characters = std::string{ data };
                }
                std::string_view const data = data_of(aSelf);
                std::size_t begin = 0u, end = data.size();
                if (aWhich != 2)
                    while (begin < end && characters.find(data[begin]) != std::string::npos)
                        ++begin;
                if (aWhich != 1)
                    while (end > begin && characters.find(data[end - 1u]) != std::string::npos)
                        --end;
                return same_type(aSelf, std::string{ data.substr(begin, end - begin) });
            }
            object* m_strip_(object* s, std::vector<object*> const& a, object* k) { return m_strip(s, a, k, 0); }
            object* m_lstrip(object* s, std::vector<object*> const& a, object* k) { return m_strip(s, a, k, 1); }
            object* m_rstrip(object* s, std::vector<object*> const& a, object* k) { return m_strip(s, a, k, 2); }

            object* m_replace(object* aSelf, std::vector<object*> const& aArgs, object* aKwargs)
            {
                std::vector<object*> a;
                if (!arguments(aSelf, "replace", aArgs, aKwargs, { "old", "new", "count" }, 2u, 3u, a))
                    return nullptr;
                std::string_view from, to;
                if (!bytes_argument(a[0], from) || !bytes_argument(a[1], to))
                    return nullptr;
                long long count = -1;
                if (a[2] != nullptr && !to_index(a[2], count))
                    return nullptr;
                std::string_view const data = data_of(aSelf);
                if (count < 0)
                    count = std::numeric_limits<long long>::max();
                std::string result;
                if (from.empty())
                {
                    // (the new bytes before each byte and at the end)
                    long long n = 0;
                    for (std::size_t i = 0u; i <= data.size(); ++i)
                    {
                        if (n < count)
                        {
                            result += to;
                            ++n;
                        }
                        if (i < data.size())
                            result += data[i];
                    }
                    return same_type(aSelf, std::move(result));
                }
                std::size_t i = 0u;
                for (long long n = 0; n < count; ++n)
                {
                    auto const j = data.find(from, i);
                    if (j == std::string_view::npos)
                        break;
                    result += data.substr(i, j - i);
                    result += to;
                    i = j + from.size();
                }
                result += data.substr(i);
                return same_type(aSelf, std::move(result));
            }

            object* m_transform(object* aSelf, std::vector<object*> const& aArgs, object* aKwargs, int aWhich)
            {
                // (0 upper, 1 lower, 2 swapcase, 3 capitalize, 4 title)
                char const* const names[] = { "upper", "lower", "swapcase", "capitalize", "title" };
                std::vector<object*> a;
                if (!arguments(aSelf, names[aWhich], aArgs, aKwargs, {}, 0u, 0u, a))
                    return nullptr;
                std::string data = data_of(aSelf);
                bool previousCased = false;
                for (std::size_t i = 0u; i < data.size(); ++i)
                {
                    auto const c = static_cast<unsigned char>(data[i]);
                    unsigned char r = c;
                    switch (aWhich)
                    {
                    case 0: r = to_upper(c); break;
                    case 1: r = to_lower(c); break;
                    case 2: r = is_upper(c) ? to_lower(c) : to_upper(c); break;
                    case 3: r = i == 0u ? to_upper(c) : to_lower(c); break;
                    default:
                        r = previousCased ? to_lower(c) : to_upper(c);
                        previousCased = is_alpha(c);
                        break;
                    }
                    data[i] = static_cast<char>(r);
                }
                return same_type(aSelf, std::move(data));
            }
            object* m_upper(object* s, std::vector<object*> const& a, object* k) { return m_transform(s, a, k, 0); }
            object* m_lower(object* s, std::vector<object*> const& a, object* k) { return m_transform(s, a, k, 1); }
            object* m_swapcase(object* s, std::vector<object*> const& a, object* k) { return m_transform(s, a, k, 2); }
            object* m_capitalize(object* s, std::vector<object*> const& a, object* k) { return m_transform(s, a, k, 3); }
            object* m_title(object* s, std::vector<object*> const& a, object* k) { return m_transform(s, a, k, 4); }

            object* m_predicate(object* aSelf, std::vector<object*> const& aArgs, object* aKwargs, int aWhich)
            {
                // (0 isalnum, 1 isalpha, 2 isdigit, 3 islower, 4 isupper, 5 isspace, 6 istitle, 7 isascii)
                char const* const names[] = { "isalnum", "isalpha", "isdigit", "islower", "isupper", "isspace", "istitle", "isascii" };
                std::vector<object*> a;
                if (!arguments(aSelf, names[aWhich], aArgs, aKwargs, {}, 0u, 0u, a))
                    return nullptr;
                auto const& data = data_of(aSelf);
                if (aWhich == 7)
                    return make_bool(std::all_of(data.begin(), data.end(), [](char c) { return static_cast<unsigned char>(c) < 0x80u; }));
                if (data.empty())
                    return make_bool(false);
                bool cased = false;
                bool previousCased = false;
                for (auto ch : data)
                {
                    auto const c = static_cast<unsigned char>(ch);
                    switch (aWhich)
                    {
                    case 0: if (!is_alpha(c) && !is_digit(c)) return make_bool(false); break;
                    case 1: if (!is_alpha(c)) return make_bool(false); break;
                    case 2: if (!is_digit(c)) return make_bool(false); break;
                    case 3: if (is_upper(c)) return make_bool(false); cased = cased || is_lower(c); break;
                    case 4: if (is_lower(c)) return make_bool(false); cased = cased || is_upper(c); break;
                    case 5: if (!is_space(c)) return make_bool(false); break;
                    default:
                        if (is_upper(c))
                        {
                            if (previousCased)
                                return make_bool(false);
                            previousCased = cased = true;
                        }
                        else if (is_lower(c))
                        {
                            if (!previousCased)
                                return make_bool(false);
                            previousCased = cased = true;
                        }
                        else
                            previousCased = false;
                        break;
                    }
                }
                return make_bool(aWhich >= 3 && aWhich != 5 ? cased : true);
            }
            object* m_isalnum(object* s, std::vector<object*> const& a, object* k) { return m_predicate(s, a, k, 0); }
            object* m_isalpha(object* s, std::vector<object*> const& a, object* k) { return m_predicate(s, a, k, 1); }
            object* m_isdigit(object* s, std::vector<object*> const& a, object* k) { return m_predicate(s, a, k, 2); }
            object* m_islower(object* s, std::vector<object*> const& a, object* k) { return m_predicate(s, a, k, 3); }
            object* m_isupper(object* s, std::vector<object*> const& a, object* k) { return m_predicate(s, a, k, 4); }
            object* m_isspace(object* s, std::vector<object*> const& a, object* k) { return m_predicate(s, a, k, 5); }
            object* m_istitle(object* s, std::vector<object*> const& a, object* k) { return m_predicate(s, a, k, 6); }
            object* m_isascii(object* s, std::vector<object*> const& a, object* k) { return m_predicate(s, a, k, 7); }

            object* m_partition(object* aSelf, std::vector<object*> const& aArgs, object* aKwargs, bool aReverse)
            {
                std::vector<object*> a;
                if (!arguments(aSelf, aReverse ? "rpartition" : "partition", aArgs, aKwargs, { "sep" }, 1u, 1u, a))
                    return nullptr;
                std::string_view separator;
                if (!bytes_argument(a[0], separator))
                    return nullptr;
                if (separator.empty())
                    return error("ValueError", "empty separator");
                std::string_view const data = data_of(aSelf);
                auto const i = aReverse ? data.rfind(separator) : data.find(separator);
                if (i == std::string_view::npos)
                    return aReverse ? make_tuple({ same_type(aSelf, {}), same_type(aSelf, {}), same_type(aSelf, std::string{ data }) }) :
                        make_tuple({ same_type(aSelf, std::string{ data }), same_type(aSelf, {}), same_type(aSelf, {}) });
                return make_tuple({ same_type(aSelf, std::string{ data.substr(0u, i) }), same_type(aSelf, std::string{ separator }),
                    same_type(aSelf, std::string{ data.substr(i + separator.size()) }) });
            }
            object* m_partition_(object* s, std::vector<object*> const& a, object* k) { return m_partition(s, a, k, false); }
            object* m_rpartition(object* s, std::vector<object*> const& a, object* k) { return m_partition(s, a, k, true); }

            object* m_remove_affix(object* aSelf, std::vector<object*> const& aArgs, object* aKwargs, bool aPrefix)
            {
                std::vector<object*> a;
                if (!arguments(aSelf, aPrefix ? "removeprefix" : "removesuffix", aArgs, aKwargs, { aPrefix ? "prefix" : "suffix" }, 1u, 1u, a))
                    return nullptr;
                std::string_view affix;
                if (!bytes_argument(a[0], affix))
                    return nullptr;
                std::string_view data = data_of(aSelf);
                if (aPrefix && data.starts_with(affix))
                    data.remove_prefix(affix.size());
                else if (!aPrefix && !affix.empty() && data.ends_with(affix))
                    data.remove_suffix(affix.size());
                return same_type(aSelf, std::string{ data });
            }
            object* m_removeprefix(object* s, std::vector<object*> const& a, object* k) { return m_remove_affix(s, a, k, true); }
            object* m_removesuffix(object* s, std::vector<object*> const& a, object* k) { return m_remove_affix(s, a, k, false); }

            object* m_justify(object* aSelf, std::vector<object*> const& aArgs, object* aKwargs, int aWhich)
            {
                // (0 center, 1 ljust, 2 rjust, 3 zfill)
                char const* const names[] = { "center", "ljust", "rjust", "zfill" };
                std::vector<object*> a;
                if (aWhich == 3 ? !arguments(aSelf, names[aWhich], aArgs, aKwargs, { "width" }, 1u, 1u, a) :
                    !arguments(aSelf, names[aWhich], aArgs, aKwargs, { "width", "fillchar" }, 1u, 2u, a))
                    return nullptr;
                long long width = 0;
                if (!to_index(a[0], width))
                    return nullptr;
                char fill = aWhich == 3 ? '0' : ' ';
                if (aWhich != 3 && a[1] != nullptr)
                {
                    std::string_view f;
                    if (!bytes_like(a[1], f) || f.size() != 1u)
                        return error("TypeError", std::string{ names[aWhich] } + "() argument 2 must be a byte string of length 1, not " +
                            (bytes_like(a[1], f) ? std::string{ name_of(a[1]) } : std::string{ type_name(a[1]) }));
                    fill = f[0];
                }
                std::string const& data = data_of(aSelf);
                if (width <= static_cast<long long>(data.size()))
                    return same_type(aSelf, data);
                auto const padding = static_cast<std::size_t>(width) - data.size();
                switch (aWhich)
                {
                case 0:
                    {
                        // (CPython's centering: the extra padding on the left if the padding and the width are both odd)
                        auto const left = padding / 2u + (padding & static_cast<std::size_t>(width) & 1u);
                        return same_type(aSelf, std::string(left, fill) + data + std::string(padding - left, fill));
                    }
                case 1: return same_type(aSelf, data + std::string(padding, fill));
                case 2: return same_type(aSelf, std::string(padding, fill) + data);
                default:
                    {
                        std::string result = std::string(padding, '0') + data;
                        if (!data.empty() && (data[0] == '+' || data[0] == '-'))
                        {
                            result[0] = data[0];
                            result[padding] = '0';
                        }
                        return same_type(aSelf, std::move(result));
                    }
                }
            }
            object* m_center(object* s, std::vector<object*> const& a, object* k) { return m_justify(s, a, k, 0); }
            object* m_ljust(object* s, std::vector<object*> const& a, object* k) { return m_justify(s, a, k, 1); }
            object* m_rjust(object* s, std::vector<object*> const& a, object* k) { return m_justify(s, a, k, 2); }
            object* m_zfill(object* s, std::vector<object*> const& a, object* k) { return m_justify(s, a, k, 3); }

            object* m_splitlines(object* aSelf, std::vector<object*> const& aArgs, object* aKwargs)
            {
                std::vector<object*> a;
                if (!arguments(aSelf, "splitlines", aArgs, aKwargs, { "keepends" }, 0u, 0u, a))
                    return nullptr;
                bool keepends = false;
                if (a[0] != nullptr)
                {
                    auto const t = truth(a[0]);
                    if (t < 0)
                        return nullptr;
                    keepends = t != 0;
                }
                std::string_view const data = data_of(aSelf);
                std::vector<object*> lines;
                std::size_t i = 0u;
                while (i < data.size())
                {
                    auto j = i;
                    while (j < data.size() && data[j] != '\n' && data[j] != '\r')
                        ++j;
                    auto eol = j;
                    if (j < data.size())
                        eol = j + (data[j] == '\r' && j + 1u < data.size() && data[j + 1u] == '\n' ? 2u : 1u);
                    lines.push_back(same_type(aSelf, std::string{ data.substr(i, (keepends ? eol : j) - i) }));
                    i = eol;
                }
                return make_list(std::move(lines));
            }

            object* m_expandtabs(object* aSelf, std::vector<object*> const& aArgs, object* aKwargs)
            {
                std::vector<object*> a;
                if (!arguments(aSelf, "expandtabs", aArgs, aKwargs, { "tabsize" }, 0u, 0u, a))
                    return nullptr;
                long long tabsize = 8;
                if (a[0] != nullptr && !to_index(a[0], tabsize))
                    return nullptr;
                std::string result;
                std::size_t column = 0u;
                for (auto c : data_of(aSelf))
                {
                    if (c == '\t')
                    {
                        if (tabsize > 0)
                        {
                            auto const n = static_cast<std::size_t>(tabsize) - column % static_cast<std::size_t>(tabsize);
                            result.append(n, ' ');
                            column += n;
                        }
                    }
                    else
                    {
                        result += c;
                        column = c == '\n' || c == '\r' ? 0u : column + 1u;
                    }
                }
                return same_type(aSelf, std::move(result));
            }

            object* m_translate(object* aSelf, std::vector<object*> const& aArgs, object* aKwargs)
            {
                std::vector<object*> a;
                if (!arguments(aSelf, "translate", aArgs, aKwargs, { "table", "delete" }, 1u, 1u, a))
                    return nullptr;
                std::string_view table, deletions;
                bool const mapped = kind_of(a[0]) != NEOS_PY_NONE;
                if (mapped)
                {
                    if (!bytes_argument(a[0], table))
                        return nullptr;
                    if (table.size() != 256u)
                        return error("ValueError", "translation table must be 256 characters long");
                }
                if (a[1] != nullptr && !bytes_argument(a[1], deletions))
                    return nullptr;
                std::string result;
                for (auto c : data_of(aSelf))
                    if (deletions.find(c) == std::string_view::npos)
                        result += mapped ? table[static_cast<unsigned char>(c)] : c;
                return same_type(aSelf, std::move(result));
            }

            object* m_copy(object* aSelf, std::vector<object*> const& aArgs, object* aKwargs)
            {
                std::vector<object*> a;
                if (!arguments(aSelf, "copy", aArgs, aKwargs, {}, 0u, 0u, a))
                    return nullptr;
                return make_bytearray(data_of(aSelf));
            }

            object* m_append(object* aSelf, std::vector<object*> const& aArgs, object* aKwargs)
            {
                std::vector<object*> a;
                if (!arguments(aSelf, "append", aArgs, aKwargs, { "item" }, 1u, 1u, a))
                    return nullptr;
                unsigned char b = 0;
                if (!byte_value(a[0], b))
                    return nullptr;
                data_of(aSelf) += static_cast<char>(b);
                return none();
            }

            object* m_extend(object* aSelf, std::vector<object*> const& aArgs, object* aKwargs)
            {
                std::vector<object*> a;
                if (!arguments(aSelf, "extend", aArgs, aKwargs, { "iterable_of_ints" }, 1u, 1u, a))
                    return nullptr;
                if (kind_of(a[0]) == NEOS_PY_STR)
                    return error("TypeError", "expected iterable of integers; got: 'str'");
                std::string data;
                if (!is_int_like(a[0]) && !bytes_from(a[0], data, "bytearray"))
                    return nullptr;
                if (is_int_like(a[0]))
                    return error("TypeError", std::string{ "can't extend bytearray with " } + type_name(a[0]));
                data_of(aSelf) += data;
                return none();
            }

            object* m_insert(object* aSelf, std::vector<object*> const& aArgs, object* aKwargs)
            {
                std::vector<object*> a;
                if (!arguments(aSelf, "insert", aArgs, aKwargs, { "index", "item" }, 2u, 2u, a))
                    return nullptr;
                long long index = 0;
                unsigned char b = 0;
                if (!to_index(a[0], index) || !byte_value(a[1], b))
                    return nullptr;
                auto& data = data_of(aSelf);
                auto const n = static_cast<long long>(data.size());
                if (index < 0)
                    index = std::max(0ll, index + n);
                index = std::min(index, n);
                data.insert(data.begin() + index, static_cast<char>(b));
                return none();
            }

            object* m_pop(object* aSelf, std::vector<object*> const& aArgs, object* aKwargs)
            {
                std::vector<object*> a;
                if (!arguments(aSelf, "pop", aArgs, aKwargs, { "index" }, 0u, 1u, a))
                    return nullptr;
                long long index = -1;
                if (a[0] != nullptr && !to_index(a[0], index))
                    return nullptr;
                auto& data = data_of(aSelf);
                if (data.empty())
                    return error("IndexError", "pop from empty bytearray");
                auto const n = static_cast<long long>(data.size());
                if (index < 0)
                    index += n;
                if (index < 0 || index >= n)
                    return error("IndexError", "pop index out of range");
                auto const b = static_cast<unsigned char>(data[static_cast<std::size_t>(index)]);
                data.erase(data.begin() + index);
                return make_int(b);
            }

            object* m_remove(object* aSelf, std::vector<object*> const& aArgs, object* aKwargs)
            {
                std::vector<object*> a;
                if (!arguments(aSelf, "remove", aArgs, aKwargs, { "value" }, 1u, 1u, a))
                    return nullptr;
                unsigned char b = 0;
                if (!byte_value(a[0], b))
                    return nullptr;
                auto& data = data_of(aSelf);
                auto const i = data.find(static_cast<char>(b));
                if (i == std::string::npos)
                    return error("ValueError", "value not found in bytearray");
                data.erase(i, 1u);
                return none();
            }

            object* m_reverse(object* aSelf, std::vector<object*> const& aArgs, object* aKwargs)
            {
                std::vector<object*> a;
                if (!arguments(aSelf, "reverse", aArgs, aKwargs, {}, 0u, 0u, a))
                    return nullptr;
                std::reverse(data_of(aSelf).begin(), data_of(aSelf).end());
                return none();
            }

            object* m_clear(object* aSelf, std::vector<object*> const& aArgs, object* aKwargs)
            {
                std::vector<object*> a;
                if (!arguments(aSelf, "clear", aArgs, aKwargs, {}, 0u, 0u, a))
                    return nullptr;
                data_of(aSelf).clear();
                return none();
            }

            bytes_method const sBytesMethods[] =
            {
                { "decode", false, m_decode }, { "hex", false, m_hex }, { "count", false, m_count }, { "find", false, m_find },
                { "rfind", false, m_rfind }, { "index", false, m_index }, { "rindex", false, m_rindex }, { "startswith", false, m_startswith },
                { "endswith", false, m_endswith }, { "join", false, m_join }, { "split", false, m_split_ }, { "rsplit", false, m_rsplit },
                { "strip", false, m_strip_ }, { "lstrip", false, m_lstrip }, { "rstrip", false, m_rstrip }, { "replace", false, m_replace },
                { "upper", false, m_upper }, { "lower", false, m_lower }, { "swapcase", false, m_swapcase }, { "capitalize", false, m_capitalize },
                { "title", false, m_title }, { "isalnum", false, m_isalnum }, { "isalpha", false, m_isalpha }, { "isdigit", false, m_isdigit },
                { "islower", false, m_islower }, { "isupper", false, m_isupper }, { "isspace", false, m_isspace }, { "istitle", false, m_istitle },
                { "isascii", false, m_isascii }, { "partition", false, m_partition_ }, { "rpartition", false, m_rpartition },
                { "removeprefix", false, m_removeprefix }, { "removesuffix", false, m_removesuffix }, { "center", false, m_center },
                { "ljust", false, m_ljust }, { "rjust", false, m_rjust }, { "zfill", false, m_zfill }, { "splitlines", false, m_splitlines },
                { "expandtabs", false, m_expandtabs }, { "translate", false, m_translate },
                { "copy", true, m_copy }, { "append", true, m_append }, { "extend", true, m_extend }, { "insert", true, m_insert },
                { "pop", true, m_pop }, { "remove", true, m_remove }, { "reverse", true, m_reverse }, { "clear", true, m_clear }
            };

            object* call_bytes_method(neos_py_context*, void* aData, object* aArgs, object* aKwargs)
            {
                auto const& method = *static_cast<bytes_method const*>(aData);
                auto const& items = as<tuple_object>(aArgs)->items;
                std::vector<object*> const args{ items.begin() + 1, items.end() };
                return method.function(items[0], args, aKwargs != nullptr && !as<dict_object>(aKwargs)->items.empty() ? aKwargs : nullptr);
            }

            // (a bytearray's item or slice assignment)
            int bytearray_setitem(object* aSelf, object* aKey, object* aValue)
            {
                auto& data = data_of(aSelf);
                if (kind_of(aKey) == NEOS_PY_SLICE)
                {
                    long long start = 0, stop = 0, step = 0;
                    if (!slice_indices(aKey, static_cast<long long>(data.size()), start, stop, step))
                        return -1;
                    std::string values;
                    if (aValue != nullptr)
                    {
                        if (aValue == aSelf)
                            values = data;
                        else if (is_int_like(aValue))
                        {
                            error("TypeError", "can assign only bytes, buffers, or iterables of ints in range(0, 256)");
                            return -1;
                        }
                        else if (!bytes_from(aValue, values, "bytearray"))
                            return -1;
                    }
                    if (step == 1)
                    {
                        if (stop < start)
                            stop = start;
                        data.replace(static_cast<std::size_t>(start), static_cast<std::size_t>(stop - start), values);
                        return 0;
                    }
                    std::vector<std::size_t> positions;
                    if (step > 0)
                        for (long long i = start; i < stop; i += step)
                            positions.push_back(static_cast<std::size_t>(i));
                    else
                        for (long long i = start; i > stop; i += step)
                            positions.push_back(static_cast<std::size_t>(i));
                    if (aValue == nullptr)
                    {
                        std::sort(positions.begin(), positions.end());
                        for (auto i = positions.rbegin(); i != positions.rend(); ++i)
                            data.erase(*i, 1u);
                        return 0;
                    }
                    if (values.size() != positions.size())
                    {
                        error("ValueError", "attempt to assign bytes of size " + std::to_string(values.size()) + " to extended slice of size " + std::to_string(positions.size()));
                        return -1;
                    }
                    for (std::size_t i = 0u; i < positions.size(); ++i)
                        data[positions[i]] = values[i];
                    return 0;
                }
                if (!is_int_like(aKey) && (python_type(aKey) == nullptr || python_type(aKey)->index == nullptr))
                {
                    error("TypeError", std::string{ "bytearray indices must be integers or slices, not " } + type_name(aKey));
                    return -1;
                }
                long long i = 0;
                if (!to_index(aKey, i))
                    return -1;
                if (i < 0)
                    i += static_cast<long long>(data.size());
                if (i < 0 || i >= static_cast<long long>(data.size()))
                {
                    error("IndexError", "bytearray index out of range");
                    return -1;
                }
                if (aValue == nullptr)
                {
                    data.erase(static_cast<std::size_t>(i), 1u);
                    return 0;
                }
                unsigned char b = 0;
                if (!byte_value(aValue, b))
                    return -1;
                data[static_cast<std::size_t>(i)] = static_cast<char>(b);
                return 0;
            }

            neos_py_type make_bytes_type(bool aMutable)
            {
                auto type = aMutable ?
                    make_type("bytearray", "bytearray", retain_object, release_object<bytes_object>) :
                    make_type("bytes", "bytes", retain_object, release_object<bytes_object>);
                type.repr = [](neos_py_context*, object* aSelf) -> object*
                {
                    if (is_bytearray(aSelf))
                        return make_str("bytearray(" + bytes_repr(data_of(aSelf)) + ")");
                    return make_str(bytes_repr(data_of(aSelf)));
                };
                type.getattr = [](neos_py_context*, object* aSelf, char const* aName) -> object*
                {
                    for (auto const& method : sBytesMethods)
                        if (std::strcmp(method.name, aName) == 0 && (!method.mutating || is_bytearray(aSelf)))
                            return make_function(method.name, call_bytes_method, const_cast<bytes_method*>(&method), aSelf);
                    return error("AttributeError", std::string{ "'" } + name_of(aSelf) + "' object has no attribute '" + aName + "'");
                };
                type.getitem = [](neos_py_context*, object* aSelf, object* aKey) -> object*
                {
                    auto const& data = data_of(aSelf);
                    if (kind_of(aKey) == NEOS_PY_SLICE)
                    {
                        long long start = 0, stop = 0, step = 0;
                        if (!slice_indices(aKey, static_cast<long long>(data.size()), start, stop, step))
                            return nullptr;
                        std::string result;
                        if (step > 0)
                            for (long long i = start; i < stop; i += step)
                                result += data[static_cast<std::size_t>(i)];
                        else
                            for (long long i = start; i > stop; i += step)
                                result += data[static_cast<std::size_t>(i)];
                        return same_type(aSelf, std::move(result));
                    }
                    if (!is_int_like(aKey) && (python_type(aKey) == nullptr || python_type(aKey)->index == nullptr))
                        return error("TypeError", std::string{ is_bytearray(aSelf) ? "bytearray" : "byte" } + " indices must be integers or slices, not " + type_name(aKey));
                    long long i = 0;
                    if (!to_index(aKey, i))
                        return nullptr;
                    if (i < 0)
                        i += static_cast<long long>(data.size());
                    if (i < 0 || i >= static_cast<long long>(data.size()))
                        return error("IndexError", is_bytearray(aSelf) ? "bytearray index out of range" : "index out of range");
                    return make_int(static_cast<unsigned char>(data[static_cast<std::size_t>(i)]));
                };
                if (aMutable)
                    type.setitem = [](neos_py_context*, object* aSelf, object* aKey, object* aValue) -> int { return bytearray_setitem(aSelf, aKey, aValue); };
                type.binary = [](neos_py_context*, int aOperation, object* aLhs, object* aRhs, int aInplace) -> object*
                {
                    std::string_view a, b;
                    if (aOperation == NEOS_PY_ADD)
                    {
                        if (!bytes_like(aLhs, a))
                            return not_implemented();
                        if (!bytes_like(aRhs, b))
                            return error("TypeError", std::string{ "can't concat " } + type_name(aRhs) + " to " + name_of(aLhs));
                        if (aInplace != 0 && is_bytearray(aLhs))
                        {
                            std::string const tail{ b };
                            data_of(aLhs) += tail;
                            retain(aLhs);
                            return aLhs;
                        }
                        return same_type(aLhs, std::string{ a } + std::string{ b });
                    }
                    if (aOperation == NEOS_PY_MULTIPLY)
                    {
                        auto const self = bytes_like(aLhs, a) ? aLhs : aRhs;
                        auto const count = self == aLhs ? aRhs : aLhs;
                        if (!bytes_like(self, a))
                            return not_implemented();
                        if (!is_int_like(count) && (python_type(count) == nullptr || python_type(count)->index == nullptr))
                            return error("TypeError", std::string{ "can't multiply sequence by non-int of type '" } + type_name(count) + "'");
                        long long n = 0;
                        if (!to_index(count, n))
                            return nullptr;
                        std::string result;
                        for (long long i = 0; i < n; ++i)
                            result += a;
                        if (aInplace != 0 && is_bytearray(aLhs) && self == aLhs)
                        {
                            data_of(aLhs) = std::move(result);
                            retain(aLhs);
                            return aLhs;
                        }
                        return same_type(self, std::move(result));
                    }
                    return not_implemented();
                };
                type.compare = [](neos_py_context*, int aOperation, object* aLhs, object* aRhs) -> object*
                {
                    // (bytes is compared with bytes, and bytearray with any buffer (bytewise); a memoryview compares its items)
                    std::string_view a, b;
                    if (is_memoryview(aLhs) || is_memoryview(aRhs) || (!is_bytearray(aLhs) && !is_bytearray(aRhs) && (!is_bytes(aLhs) || !is_bytes(aRhs))) ||
                        !bytes_like(aLhs, a) || !bytes_like(aRhs, b))
                        return not_implemented();
                    auto const c = a.compare(b);
                    switch (aOperation)
                    {
                    case NEOS_PY_LT: return make_bool(c < 0);
                    case NEOS_PY_LE: return make_bool(c <= 0);
                    case NEOS_PY_EQ: return make_bool(c == 0);
                    case NEOS_PY_NE: return make_bool(c != 0);
                    case NEOS_PY_GT: return make_bool(c > 0);
                    default: return make_bool(c >= 0);
                    }
                };
                type.length = [](neos_py_context*, object* aSelf) -> long long { return static_cast<long long>(data_of(aSelf).size()); };
                type.iter = [](neos_py_context*, object* aSelf) -> object*
                {
                    retain(aSelf);
                    return &(new bytes_iterator_object{ { { is_bytearray(aSelf) ? &sByteArrayIteratorType.base : &sBytesIteratorType.base } }, aSelf })->h.base;
                };
                type.contains = [](neos_py_context*, object* aSelf, object* aItem) -> int
                {
                    std::string sub;
                    std::string_view data;
                    if (bytes_like(aItem, data))
                        sub = std::string{ data };
                    else if (is_int_like(aItem) || (python_type(aItem) != nullptr && python_type(aItem)->index != nullptr))
                    {
                        unsigned char b = 0;
                        if (!byte_value(aItem, b))
                            return -1;
                        sub = std::string(1u, static_cast<char>(b));
                    }
                    else
                    {
                        error("TypeError", std::string{ "a bytes-like object is required, not '" } + type_name(aItem) + "'");
                        return -1;
                    }
                    return data_of(aSelf).find(sub) != std::string::npos ? 1 : 0;
                };
                if (!aMutable)
                    type.hash = [](neos_py_context*, object* aSelf, long long* aResult) -> int
                    {
                        auto& b = as_bytes(aSelf);
                        if (b.hash == -1)
                            b.hash = bytes_hash(b.data);
                        *aResult = b.hash;
                        return 0;
                    };
                return type;
            }

            neos_py_type make_bytes_iterator_type(char const* aName)
            {
                auto type = make_type(aName, aName, retain_object, release_object<bytes_iterator_object>);
                type.iter = [](neos_py_context*, object* aSelf) -> object* { retain(aSelf); return aSelf; };
                type.next = [](neos_py_context*, object* aSelf) -> object*
                {
                    auto& it = *reinterpret_cast<bytes_iterator_object*>(aSelf);
                    auto const& data = data_of(it.sequence);
                    if (it.index >= data.size())
                        return nullptr;
                    return make_int(static_cast<unsigned char>(data[it.index++]));
                };
                return type;
            }

            object* construct(std::vector<object*> const& aArgs, object* aKwargs, bool aMutable)
            {
                char const* const name = aMutable ? "bytearray" : "bytes";
                std::vector<object*> a;
                if (!parse_arguments(name, aArgs, aKwargs, { "source", "encoding", "errors" }, 0u, 0u, a))
                    return nullptr;
                if (a[0] == nullptr)
                {
                    if (a[1] != nullptr || a[2] != nullptr)
                        return error("TypeError", a[1] != nullptr ? "encoding without a string argument" : "errors without a string argument");
                    return aMutable ? make_bytearray({}) : make_bytes({});
                }
                if (a[1] != nullptr || a[2] != nullptr)
                {
                    if (kind_of(a[0]) != NEOS_PY_STR)
                        return error("TypeError", a[1] != nullptr ? "encoding without a string argument" : "errors without a string argument");
                    for (std::size_t i = 1u; i < 3u; ++i)
                        if (a[i] != nullptr && kind_of(a[i]) != NEOS_PY_STR)
                            return error("TypeError", std::string{ name } + "() argument '" + (i == 1u ? "encoding" : "errors") + "' must be str, not " + type_name(a[i]));
                    if (a[1] == nullptr)
                        return error("TypeError", "string argument without an encoding");
                    std::string data;
                    if (!encode(as<str_object>(a[0])->value, as<str_object>(a[1])->value, a[2] != nullptr ? std::string_view{ as<str_object>(a[2])->value } : "strict", data))
                        return nullptr;
                    return aMutable ? make_bytearray(std::move(data)) : make_bytes(std::move(data));
                }
                // (bytes(b) is b)
                if (!aMutable && is_bytes(a[0]))
                {
                    retain(a[0]);
                    return a[0];
                }
                // (an object's __bytes__)
                if (!aMutable)
                    if (ref const method{ special_method(a[0], "__bytes__") })
                    {
                        ref const empty{ make_tuple() };
                        ref result{ call(method.get(), empty.get(), nullptr) };
                        if (result && !is_bytes(result.get()))
                            return error("TypeError", std::string{ "__bytes__ returned non-bytes (type " } + type_name(result.get()) + ")");
                        return result.release_ownership();
                    }
                std::string data;
                if (!bytes_from(a[0], data, name))
                    return nullptr;
                return aMutable ? make_bytearray(std::move(data)) : make_bytes(std::move(data));
            }
        }

        object* b_bytes(std::vector<object*> const& aArgs, object* aKwargs) { return construct(aArgs, aKwargs, false); }
        object* b_bytearray(std::vector<object*> const& aArgs, object* aKwargs) { return construct(aArgs, aKwargs, true); }

        // (a bytes literal: the compiler's hexadecimal digits of its bytes)
        object* b_bytes_literal(std::vector<object*> const& aArgs, object*)
        {
            std::string data;
            auto const& digits = as<str_object>(aArgs[0])->value;
            for (std::size_t i = 0u; i + 1u < digits.size(); i += 2u)
                data += static_cast<char>(std::stoi(digits.substr(i, 2u), nullptr, 16));
            return make_bytes(std::move(data));
        }

        // (bytes.fromhex(s) and bytearray.fromhex(s))
        object* bytes_fromhex(object* aString, bool aMutable)
        {
            if (kind_of(aString) != NEOS_PY_STR)
                return error("TypeError", std::string{ "fromhex() argument must be str, not " } + type_name(aString));
            auto const& text = as<str_object>(aString)->value;
            std::string data;
            auto const digit = [](char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; };
            for (std::size_t i = 0u; i < text.size();)
            {
                if (is_space(static_cast<unsigned char>(text[i])))
                {
                    ++i;
                    continue;
                }
                auto const hi = digit(text[i]);
                if (hi < 0)
                    return error("ValueError", "non-hexadecimal number found in fromhex() arg at position " + std::to_string(i));
                auto const lo = i + 1u < text.size() ? digit(text[i + 1u]) : -1;
                if (lo < 0)
                    return error("ValueError", "non-hexadecimal number found in fromhex() arg at position " + std::to_string(i + 1u));
                data += static_cast<char>(hi * 16 + lo);
                i += 2u;
            }
            return aMutable ? make_bytearray(std::move(data)) : make_bytes(std::move(data));
        }

        // str.encode(encoding='utf-8', errors='strict')
        object* str_encode(object* aSelf, std::vector<object*> const& aArgs, object* aKwargs)
        {
            std::vector<object*> a;
            if (!parse_arguments("str.encode", aArgs, aKwargs, { "encoding", "errors" }, 0u, 0u, a))
                return nullptr;
            std::string encoding = "utf-8", errors = "strict";
            char const* const names[] = { "encoding", "errors" };
            for (std::size_t i = 0u; i < 2u; ++i)
                if (a[i] != nullptr)
                {
                    if (kind_of(a[i]) != NEOS_PY_STR)
                        return error("TypeError", std::string{ "encode() argument '" } + names[i] + "' must be str, not " + type_name(a[i]));
                    (i == 0u ? encoding : errors) = as<str_object>(a[i])->value;
                }
            std::string data;
            if (!encode(as<str_object>(aSelf)->value, encoding, errors, data))
                return nullptr;
            return make_bytes(std::move(data));
        }
    }
}
