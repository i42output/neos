/*
  codecs.cpp

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

  Portions of this file are derived from CPython 3.13 (Objects/unicodeobject.c, Python/codecs.c and Lib/encodings),
  Copyright (c) 2001-2024 Python Software Foundation; All Rights Reserved. They are used under the Python Software
  Foundation License Version 2 (see runtime/python/LICENSE.PSF). Changes: translated from C and Python to C++ for the neos
  Python runtime plugin and adapted to its object model (see runtime/python/NOTICE).
*/

// The codecs (CPython 3.13's: unicodeobject.c's encoders and decoders, codecs.c's error handlers and registry), str.encode's and
// bytes.decode's, and the modules _codecs and codecs (codecs.py's classes) and the encodings' codecs (encodings.*)

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>
#include "objects.hpp"
#include "codec_tables.hpp"

namespace neos::python_runtime
{
    namespace detail
    {
        namespace
        {
            namespace ct = codec_tables;

            // --- helpers

            std::u32string code_points(std::string_view aText) { return to_code_points(aText); }

            void append_char(std::string& aText, char32_t aCharacter) { append_code_point(aText, aCharacter); }

            constexpr char sHexDigits[] = "0123456789abcdef";

            bool is_bytes(object* aObject)     // (bytes (or a subclass's instance))
            {
                return aObject != nullptr && std::strcmp(type_name(builtin_value(aObject)), "bytes") == 0;
            }

            bool is_surrogate(char32_t c) { return c >= 0xD800u && c <= 0xDFFFu; }

            // (the backslash escape of a character: \xhh, \uhhhh or \Uhhhhhhhh)
            void append_backslash_escape(std::u32string& aText, char32_t c)
            {
                char buffer[16];
                std::snprintf(buffer, sizeof(buffer), c < 0x100u ? "\\x%02x" : c < 0x10000u ? "\\u%04x" : "\\U%08x", static_cast<unsigned>(c));
                for (auto b = buffer; *b != '\0'; ++b)
                    aText += static_cast<char32_t>(*b);
            }

            // --- the error handlers' registry (codecs.register_error): the builtin handlers' functions are made when the module is

            std::map<std::string, object*, std::less<>>& error_registry()
            {
                static std::map<std::string, object*, std::less<>> sRegistry;
                return sRegistry;
            }

            std::map<std::string, object*, std::less<>>& builtin_error_handlers()
            {
                static std::map<std::string, object*, std::less<>> sHandlers;
                return sHandlers;
            }

            bool is_builtin_handler(std::string_view aName)
            {
                auto const r = error_registry().find(aName);
                if (r == error_registry().end())
                    return aName == "strict" || aName == "ignore" || aName == "replace" || aName == "backslashreplace" || aName == "xmlcharrefreplace" ||
                        aName == "namereplace" || aName == "surrogateescape" || aName == "surrogatepass";
                auto const b = builtin_error_handlers().find(aName);
                return b != builtin_error_handlers().end() && b->second == r->second;
            }

            object* lookup_error_handler(std::string_view aName)    // (a new reference)
            {
                auto const r = error_registry().find(aName);
                if (r == error_registry().end())
                {
                    if (aName.empty())
                        return lookup_error_handler("strict");
                    return error("LookupError", "unknown error handler name '" + std::string{ aName } + "'");
                }
                return new_reference(r->second);
            }

            // (the standard encodings of surrogatepass: CPython's get_standard_encoding (0: unknown))
            enum standard_encoding { ENC_UNKNOWN, ENC_UTF8, ENC_UTF16LE, ENC_UTF16BE, ENC_UTF32LE, ENC_UTF32BE };
            standard_encoding standard_encoding_of(std::string_view aEncoding, int& aByteLength)
            {
                auto lower = [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); };
                if (aEncoding.size() >= 3u && lower(aEncoding[0]) == 'u' && lower(aEncoding[1]) == 't' && lower(aEncoding[2]) == 'f')
                {
                    auto e = aEncoding.substr(3u);
                    if (!e.empty() && (e[0] == '-' || e[0] == '_'))
                        e.remove_prefix(1u);
                    if (e == "8")
                    {
                        aByteLength = 3;
                        return ENC_UTF8;
                    }
                    for (auto const& [digits, length, le, be] : { std::tuple{ "16", 2, ENC_UTF16LE, ENC_UTF16BE }, std::tuple{ "32", 4, ENC_UTF32LE, ENC_UTF32BE } })
                        if (e.starts_with(digits))
                        {
                            e.remove_prefix(2u);
                            aByteLength = length;
                            if (e.empty())
                                return le;
                            if (e[0] == '-' || e[0] == '_')
                                e.remove_prefix(1u);
                            if (e.size() == 2u && lower(e[1]) == 'e')
                            {
                                if (lower(e[0]) == 'b')
                                    return be;
                                if (lower(e[0]) == 'l')
                                    return le;
                            }
                            return ENC_UNKNOWN;
                        }
                }
                else if (aEncoding == "CP_UTF8")
                {
                    aByteLength = 3;
                    return ENC_UTF8;
                }
                return ENC_UNKNOWN;
            }

            // --- decoding errors (unicode_decode_call_errorhandler_writer and the builtin handlers)

            struct decode_errors
            {
                char const* encoding;
                std::string_view data;
                std::string_view errors;
                ref exception;

                object* make_exception_object(std::size_t aStart, std::size_t aEnd, char const* aReason)
                {
                    if (!exception)
                        exception = ref{ make_exception("UnicodeDecodeError", { make_str(encoding), make_bytes(std::string{ data }),
                            make_int(static_cast<long long>(aStart)), make_int(static_cast<long long>(aEnd)), make_str(aReason) }) };
                    else
                    {
                        ref const start{ make_int(static_cast<long long>(aStart)) };
                        ref const end{ make_int(static_cast<long long>(aEnd)) };
                        ref const reason{ make_str(aReason) };
                        setattr(exception.get(), "start", start.get());
                        setattr(exception.get(), "end", end.get());
                        setattr(exception.get(), "reason", reason.get());
                    }
                    return exception.get();
                }

                bool raise(std::size_t aStart, std::size_t aEnd, char const* aReason)
                {
                    raise_exception(make_exception_object(aStart, aEnd, aReason));
                    return false;
                }

                // (the replacement (appended to aOutput) and the position to continue at)
                bool failure(std::size_t aStart, std::size_t aEnd, char const* aReason, std::string& aOutput, std::size_t& aNext)
                {
                    auto const name = errors.empty() ? std::string_view{ "strict" } : errors;
                    if (is_builtin_handler(name))
                    {
                        if (name == "strict")
                            return raise(aStart, aEnd, aReason);
                        if (name == "ignore")
                        {
                            aNext = aEnd;
                            return true;
                        }
                        if (name == "replace")
                        {
                            append_char(aOutput, 0xFFFDu);
                            aNext = aEnd;
                            return true;
                        }
                        if (name == "backslashreplace")
                        {
                            for (auto i = aStart; i < aEnd; ++i)
                            {
                                auto const b = static_cast<unsigned char>(data[i]);
                                aOutput += "\\x";
                                aOutput += sHexDigits[b >> 4];
                                aOutput += sHexDigits[b & 15u];
                            }
                            aNext = aEnd;
                            return true;
                        }
                        if (name == "surrogateescape")
                        {
                            std::size_t consumed = 0u;
                            while (consumed < 4u && consumed < aEnd - aStart && static_cast<unsigned char>(data[aStart + consumed]) >= 128u)
                            {
                                append_char(aOutput, 0xDC00u + static_cast<unsigned char>(data[aStart + consumed]));
                                ++consumed;
                            }
                            if (consumed == 0u)
                                return raise(aStart, aEnd, aReason);
                            aNext = aStart + consumed;
                            return true;
                        }
                        if (name == "surrogatepass")
                        {
                            int length = 0;
                            auto const code = standard_encoding_of(encoding, length);
                            if (code == ENC_UNKNOWN)
                                return raise(aStart, aEnd, aReason);
                            char32_t ch = 0u;
                            auto const p = reinterpret_cast<unsigned char const*>(data.data()) + aStart;
                            if (data.size() - aStart >= static_cast<std::size_t>(length))
                                switch (code)
                                {
                                case ENC_UTF8:
                                    if ((p[0] & 0xF0u) == 0xE0u && (p[1] & 0xC0u) == 0x80u && (p[2] & 0xC0u) == 0x80u)
                                        ch = ((p[0] & 0x0Fu) << 12) + ((p[1] & 0x3Fu) << 6) + (p[2] & 0x3Fu);
                                    break;
                                case ENC_UTF16LE: ch = static_cast<char32_t>(p[1] << 8 | p[0]); break;
                                case ENC_UTF16BE: ch = static_cast<char32_t>(p[0] << 8 | p[1]); break;
                                case ENC_UTF32LE: ch = (static_cast<char32_t>(p[3]) << 24) | (p[2] << 16) | (p[1] << 8) | p[0]; break;
                                case ENC_UTF32BE: ch = (static_cast<char32_t>(p[0]) << 24) | (p[1] << 16) | (p[2] << 8) | p[3]; break;
                                default: break;
                                }
                            if (!is_surrogate(ch))
                                return raise(aStart, aEnd, aReason);
                            append_char(aOutput, ch);
                            aNext = aStart + static_cast<std::size_t>(length);
                            return true;
                        }
                        // (xmlcharrefreplace and namereplace)
                        return (error("TypeError", "don't know how to handle UnicodeDecodeError in error callback"), false);
                    }
                    ref const handler{ lookup_error_handler(name) };
                    if (!handler)
                        return false;
                    auto const e = make_exception_object(aStart, aEnd, aReason);
                    ref const result{ invoke(handler.get(), { new_reference(e) }) };
                    if (!result)
                        return false;
                    if (kind_of(result.get()) != NEOS_PY_TUPLE || as<tuple_object>(result.get())->items.size() != 2u ||
                        kind_of(as<tuple_object>(result.get())->items[0]) != NEOS_PY_STR || !is_int_like(as<tuple_object>(result.get())->items[1]))
                        return (error("TypeError", "decoding error handler must return (str, int) tuple"), false);
                    long long position = 0;
                    if (!to_index(as<tuple_object>(result.get())->items[1], position))
                        return false;
                    auto const size = static_cast<long long>(data.size());
                    if (position < 0)
                        position += size;
                    if (position < 0 || position > size)
                        return (error("IndexError", "position " + std::to_string(position) + " from error handler out of bounds"), false);
                    aOutput += as<str_object>(as<tuple_object>(result.get())->items[0])->value;
                    aNext = static_cast<std::size_t>(position);
                    return true;
                }
            };

            // --- encoding errors (unicode_encode_call_errorhandler and the builtin handlers)

            struct encode_errors
            {
                char const* encoding;
                std::u32string const& text;
                std::string_view errors;
                ref exception;

                object* make_exception_object(std::size_t aStart, std::size_t aEnd, char const* aReason)
                {
                    if (!exception)
                        exception = ref{ make_exception("UnicodeEncodeError", { make_str(encoding), make_str(from_code_points(text)),
                            make_int(static_cast<long long>(aStart)), make_int(static_cast<long long>(aEnd)), make_str(aReason) }) };
                    else
                    {
                        ref const start{ make_int(static_cast<long long>(aStart)) };
                        ref const end{ make_int(static_cast<long long>(aEnd)) };
                        ref const reason{ make_str(aReason) };
                        setattr(exception.get(), "start", start.get());
                        setattr(exception.get(), "end", end.get());
                        setattr(exception.get(), "reason", reason.get());
                    }
                    return exception.get();
                }

                bool raise(std::size_t aStart, std::size_t aEnd, char const* aReason)
                {
                    raise_exception(make_exception_object(aStart, aEnd, aReason));
                    return false;
                }

                // (the handler's replacement: text (aIsBytes false) or bytes, and the position to continue at)
                bool failure(std::size_t aStart, std::size_t aEnd, char const* aReason, std::u32string& aText, std::string& aBytes, bool& aIsBytes, std::size_t& aNext)
                {
                    aText.clear();
                    aBytes.clear();
                    aIsBytes = false;
                    aNext = aEnd;
                    auto const name = errors.empty() ? std::string_view{ "strict" } : errors;
                    if (is_builtin_handler(name))
                    {
                        if (name == "strict")
                            return raise(aStart, aEnd, aReason);
                        if (name == "ignore")
                            return true;
                        if (name == "replace")
                        {
                            aText.assign(aEnd - aStart, U'?');
                            return true;
                        }
                        if (name == "backslashreplace")
                        {
                            for (auto i = aStart; i < aEnd; ++i)
                                append_backslash_escape(aText, text[i]);
                            return true;
                        }
                        if (name == "xmlcharrefreplace")
                        {
                            for (auto i = aStart; i < aEnd; ++i)
                                for (auto c : "&#" + std::to_string(static_cast<unsigned long>(text[i])) + ";")
                                    aText += static_cast<char32_t>(c);
                            return true;
                        }
                        if (name == "namereplace")
                        {
                            for (auto i = aStart; i < aEnd; ++i)
                            {
                                std::string n;
                                if (unicode_name(text[i], n))
                                {
                                    for (auto c : "\\N{" + n + "}")
                                        aText += static_cast<char32_t>(static_cast<unsigned char>(c));
                                }
                                else
                                    append_backslash_escape(aText, text[i]);
                            }
                            return true;
                        }
                        if (name == "surrogateescape")
                        {
                            for (auto i = aStart; i < aEnd; ++i)
                            {
                                if (text[i] < 0xDC80u || text[i] > 0xDCFFu)
                                    return raise(aStart, aEnd, aReason);
                                aBytes += static_cast<char>(text[i] - 0xDC00u);
                            }
                            aIsBytes = true;
                            return true;
                        }
                        // (surrogatepass)
                        int length = 0;
                        auto const code = standard_encoding_of(encoding, length);
                        if (code == ENC_UNKNOWN)
                            return raise(aStart, aEnd, aReason);
                        for (auto i = aStart; i < aEnd; ++i)
                        {
                            auto const ch = static_cast<std::uint32_t>(text[i]);
                            if (!is_surrogate(ch))
                                return raise(aStart, aEnd, aReason);
                            switch (code)
                            {
                            case ENC_UTF8:
                                aBytes += static_cast<char>(0xE0u | (ch >> 12));
                                aBytes += static_cast<char>(0x80u | ((ch >> 6) & 0x3Fu));
                                aBytes += static_cast<char>(0x80u | (ch & 0x3Fu));
                                break;
                            case ENC_UTF16LE: aBytes += static_cast<char>(ch); aBytes += static_cast<char>(ch >> 8); break;
                            case ENC_UTF16BE: aBytes += static_cast<char>(ch >> 8); aBytes += static_cast<char>(ch); break;
                            case ENC_UTF32LE: for (int k = 0; k < 4; ++k) aBytes += static_cast<char>(ch >> (8 * k)); break;
                            case ENC_UTF32BE: for (int k = 3; k >= 0; --k) aBytes += static_cast<char>(ch >> (8 * k)); break;
                            default: break;
                            }
                        }
                        aIsBytes = true;
                        return true;
                    }
                    ref const handler{ lookup_error_handler(name) };
                    if (!handler)
                        return false;
                    auto const e = make_exception_object(aStart, aEnd, aReason);
                    ref const result{ invoke(handler.get(), { new_reference(e) }) };
                    if (!result)
                        return false;
                    std::string_view bytes;
                    if (kind_of(result.get()) != NEOS_PY_TUPLE || as<tuple_object>(result.get())->items.size() != 2u ||
                        (kind_of(as<tuple_object>(result.get())->items[0]) != NEOS_PY_STR && !is_bytes(as<tuple_object>(result.get())->items[0])) ||
                        !is_int_like(as<tuple_object>(result.get())->items[1]))
                        return (error("TypeError", "encoding error handler must return (str/bytes, int) tuple"), false);
                    long long position = 0;
                    if (!to_index(as<tuple_object>(result.get())->items[1], position))
                        return false;
                    auto const size = static_cast<long long>(text.size());
                    if (position < 0)
                        position += size;
                    if (position < 0 || position > size)
                        return (error("IndexError", "position " + std::to_string(position) + " from error handler out of bounds"), false);
                    auto const replacement = as<tuple_object>(result.get())->items[0];
                    if (kind_of(replacement) == NEOS_PY_STR)
                        aText = to_code_points(as<str_object>(replacement)->value);
                    else
                    {
                        bytes_like(replacement, bytes);
                        aBytes = std::string{ bytes };
                        aIsBytes = true;
                    }
                    aNext = static_cast<std::size_t>(position);
                    return true;
                }
            };

            bool all_below(std::u32string const& aText, char32_t aLimit)
            {
                return std::all_of(aText.begin(), aText.end(), [&](char32_t c) { return c < aLimit; });
            }

            // --- UTF-8

            // (stringlib's utf8_decode: the characters before an error; its kind (0: the end (an incomplete sequence perhaps), 1: an invalid
            // start byte, 2..4: an invalid continuation byte after 1..3 bytes))
            int utf8_decode_run(std::string_view aData, std::size_t& i, std::string& aOutput)
            {
                auto const end = aData.size();
                auto const at = [&](std::size_t j) { return static_cast<unsigned char>(aData[j]); };
                auto const continuation = [](unsigned char c) { return c >= 0x80u && c < 0xC0u; };
                while (i < end)
                {
                    auto const ch = at(i);
                    if (ch < 0x80u)
                    {
                        auto j = i + 1u;
                        while (j < end && at(j) < 0x80u)
                            ++j;
                        aOutput.append(aData.substr(i, j - i));
                        i = j;
                        continue;
                    }
                    if (ch < 0xE0u)
                    {
                        if (ch < 0xC2u)
                            return 1;
                        if (end - i < 2u)
                            return 0;
                        if (!continuation(at(i + 1u)))
                            return 2;
                        aOutput.append(aData.substr(i, 2u));
                        i += 2u;
                        continue;
                    }
                    if (ch < 0xF0u)
                    {
                        if (end - i < 3u)
                        {
                            if (end - i < 2u)
                                return 0;
                            auto const ch2 = at(i + 1u);
                            if (!continuation(ch2) || (ch2 < 0xA0u ? ch == 0xE0u : ch == 0xEDu))
                                return 2;
                            return 0;
                        }
                        auto const ch2 = at(i + 1u), ch3 = at(i + 2u);
                        if (!continuation(ch2))
                            return 2;
                        if (ch == 0xE0u ? ch2 < 0xA0u : (ch == 0xEDu && ch2 >= 0xA0u))
                            return 2;
                        if (!continuation(ch3))
                            return 3;
                        aOutput.append(aData.substr(i, 3u));
                        i += 3u;
                        continue;
                    }
                    if (ch < 0xF5u)
                    {
                        if (end - i < 4u)
                        {
                            if (end - i < 2u)
                                return 0;
                            auto const ch2 = at(i + 1u);
                            if (!continuation(ch2) || (ch2 < 0x90u ? ch == 0xF0u : ch == 0xF4u))
                                return 2;
                            if (end - i < 3u)
                                return 0;
                            if (!continuation(at(i + 2u)))
                                return 3;
                            return 0;
                        }
                        auto const ch2 = at(i + 1u), ch3 = at(i + 2u), ch4 = at(i + 3u);
                        if (!continuation(ch2))
                            return 2;
                        if (ch == 0xF0u ? ch2 < 0x90u : (ch == 0xF4u && ch2 >= 0x90u))
                            return 2;
                        if (!continuation(ch3))
                            return 3;
                        if (!continuation(ch4))
                            return 4;
                        aOutput.append(aData.substr(i, 4u));
                        i += 4u;
                        continue;
                    }
                    return 1;
                }
                return 0;
            }

            bool decode_utf8(std::string_view aData, std::string_view aErrors, bool aFinal, std::string& aOutput, std::size_t& aConsumed, char const* aEncoding = "utf-8")
            {
                decode_errors errors{ aEncoding, aData, aErrors };
                std::size_t i = 0u;
                while (i < aData.size())
                {
                    auto const kind = utf8_decode_run(aData, i, aOutput);
                    std::size_t start = i, end = i;
                    char const* reason = nullptr;
                    switch (kind)
                    {
                    case 0:
                        if (i == aData.size() || !aFinal)
                        {
                            aConsumed = i;
                            return true;
                        }
                        reason = "unexpected end of data";
                        end = aData.size();
                        break;
                    case 1:
                        reason = "invalid start byte";
                        end = start + 1u;
                        break;
                    default:
                        if (kind == 2 && !aFinal && static_cast<unsigned char>(aData[i]) == 0xEDu && aData.size() - i == 2u &&
                            static_cast<unsigned char>(aData[i + 1u]) >= 0xA0u && static_cast<unsigned char>(aData[i + 1u]) <= 0xBFu)
                        {
                            // (a truncated surrogate: the next data may complete it (surrogatepass))
                            aConsumed = i;
                            return true;
                        }
                        reason = "invalid continuation byte";
                        end = start + static_cast<std::size_t>(kind) - 1u;
                        break;
                    }
                    std::size_t next = end;
                    if (aErrors == "surrogateescape" && is_builtin_handler("surrogateescape"))
                        for (auto k = start; k < end; ++k)
                            append_char(aOutput, 0xDC00u + static_cast<unsigned char>(aData[k]));
                    else if (!errors.failure(start, end, reason, aOutput, next))
                        return false;
                    i = next;
                }
                aConsumed = i;
                return true;
            }

            bool encode_utf8(std::u32string const& aText, std::string_view aErrors, std::string& aOutput, char const* aEncoding = "utf-8")
            {
                encode_errors errors{ aEncoding, aText, aErrors };
                std::u32string replacementText;
                std::string replacementBytes;
                for (std::size_t i = 0u; i < aText.size();)
                {
                    auto const ch = aText[i];
                    if (!is_surrogate(ch))
                    {
                        append_char(aOutput, ch);
                        ++i;
                        continue;
                    }
                    auto start = i, end = i + 1u;
                    while (end < aText.size() && is_surrogate(aText[end]))
                        ++end;
                    if (aErrors == "surrogateescape" && is_builtin_handler("surrogateescape"))
                    {
                        auto k = start;
                        for (; k < end && aText[k] >= 0xDC80u && aText[k] <= 0xDCFFu; ++k)
                            aOutput += static_cast<char>(aText[k] & 0xFFu);
                        if (k >= end)
                        {
                            i = end;
                            continue;
                        }
                        start = k;
                    }
                    bool isBytes = false;
                    std::size_t next = end;
                    if (!errors.failure(start, end, "surrogates not allowed", replacementText, replacementBytes, isBytes, next))
                        return false;
                    if (isBytes)
                        aOutput += replacementBytes;
                    else
                    {
                        if (!all_below(replacementText, 0x80u))
                            return errors.raise(start, end, "surrogates not allowed");
                        for (auto c : replacementText)
                            aOutput += static_cast<char>(c);
                    }
                    i = next;
                }
                return true;
            }

            // --- UTF-16 and UTF-32 (aByteOrder: -1: little-endian, 1: big-endian, 0: a byte order mark (decoding: detected (and set)))

            bool decode_utf16(std::string_view aData, std::string_view aErrors, int& aByteOrder, bool aFinal, std::string& aOutput, std::size_t& aConsumed)
            {
                auto const q0 = reinterpret_cast<unsigned char const*>(aData.data());
                std::size_t q = 0u;
                auto const size = aData.size();
                int bo = aByteOrder;
                if (bo == 0 && size >= 2u)
                {
                    auto const bom = (q0[1] << 8) | q0[0];
                    if (bom == 0xFEFF)
                    {
                        q += 2u;
                        bo = -1;
                    }
                    else if (bom == 0xFFFE)
                    {
                        q += 2u;
                        bo = 1;
                    }
                    aByteOrder = bo;
                }
                if (q == size)
                {
                    aConsumed = size;
                    return true;
                }
                bool const little = bo <= 0;
                decode_errors errors{ little ? "utf-16-le" : "utf-16-be", aData, aErrors };
                auto const unit = [&](std::size_t aAt) -> char32_t { return little ? static_cast<char32_t>(q0[aAt] | (q0[aAt + 1u] << 8)) : static_cast<char32_t>((q0[aAt] << 8) | q0[aAt + 1u]); };
                for (;;)
                {
                    int code = 0;
                    std::size_t errorStart = q;
                    // (stringlib's utf16_decode)
                    while (size - q >= 2u)
                    {
                        auto const ch = unit(q);
                        q += 2u;
                        if (!is_surrogate(ch))
                        {
                            append_char(aOutput, ch);
                            continue;
                        }
                        if (ch >= 0xDC00u)
                        {
                            code = 2;
                            break;
                        }
                        if (size - q < 2u)
                        {
                            code = 1;
                            break;
                        }
                        auto const ch2 = unit(q);
                        q += 2u;
                        if (ch2 < 0xDC00u || ch2 > 0xDFFFu)
                        {
                            code = 3;
                            break;
                        }
                        append_char(aOutput, 0x10000u + ((ch - 0xD800u) << 10) + (ch2 - 0xDC00u));
                    }
                    char const* reason = nullptr;
                    std::size_t start = 0u, end = 0u;
                    switch (code)
                    {
                    case 0:
                        if (q == size || !aFinal)
                        {
                            aConsumed = q;
                            return true;
                        }
                        reason = "truncated data";
                        start = q;
                        end = size;
                        break;
                    case 1:
                        q -= 2u;
                        if (!aFinal)
                        {
                            aConsumed = q;
                            return true;
                        }
                        reason = "unexpected end of data";
                        start = q;
                        end = size;
                        break;
                    case 2:
                        reason = "illegal encoding";
                        start = q - 2u;
                        end = start + 2u;
                        break;
                    default:
                        reason = "illegal UTF-16 surrogate";
                        start = q - 4u;
                        end = start + 2u;
                        break;
                    }
                    (void)errorStart;
                    std::size_t next = end;
                    if (!errors.failure(start, end, reason, aOutput, next))
                        return false;
                    q = next;
                }
            }

            bool decode_utf32(std::string_view aData, std::string_view aErrors, int& aByteOrder, bool aFinal, std::string& aOutput, std::size_t& aConsumed)
            {
                auto const q0 = reinterpret_cast<unsigned char const*>(aData.data());
                std::size_t q = 0u;
                auto const size = aData.size();
                int bo = aByteOrder;
                if (bo == 0 && size >= 4u)
                {
                    auto const bom = (static_cast<std::uint32_t>(q0[3]) << 24) | (q0[2] << 16) | (q0[1] << 8) | q0[0];
                    if (bom == 0x0000FEFFu)
                    {
                        bo = -1;
                        q += 4u;
                    }
                    else if (bom == 0xFFFE0000u)
                    {
                        bo = 1;
                        q += 4u;
                    }
                    aByteOrder = bo;
                }
                if (q == size)
                {
                    aConsumed = size;
                    return true;
                }
                bool const little = bo <= 0;
                decode_errors errors{ little ? "utf-32-le" : "utf-32-be", aData, aErrors };
                for (;;)
                {
                    if (size - q < 4u)
                    {
                        if (q == size || !aFinal)
                            break;
                        std::size_t next = size;
                        if (!errors.failure(q, size, "truncated data", aOutput, next))
                            return false;
                        q = next;
                        continue;
                    }
                    std::uint32_t const ch = little ? (static_cast<std::uint32_t>(q0[q + 3u]) << 24) | (q0[q + 2u] << 16) | (q0[q + 1u] << 8) | q0[q] :
                        (static_cast<std::uint32_t>(q0[q]) << 24) | (q0[q + 1u] << 16) | (q0[q + 2u] << 8) | q0[q + 3u];
                    char const* reason = nullptr;
                    if (is_surrogate(ch))
                        reason = "code point in surrogate code point range(0xd800, 0xe000)";
                    else if (ch >= 0x110000u)
                        reason = "code point not in range(0x110000)";
                    else
                    {
                        append_char(aOutput, ch);
                        q += 4u;
                        continue;
                    }
                    std::size_t next = q + 4u;
                    if (!errors.failure(q, q + 4u, reason, aOutput, next))
                        return false;
                    q = next;
                }
                aConsumed = q;
                return true;
            }

            bool encode_utf16_32(std::u32string const& aText, std::string_view aErrors, int aByteOrder, bool aWide, std::string& aOutput)
            {
                bool const little = aByteOrder <= 0;
                char const* const encoding = aWide ? (aByteOrder < 0 ? "utf-32-le" : aByteOrder > 0 ? "utf-32-be" : "utf-32") :
                    (aByteOrder < 0 ? "utf-16-le" : aByteOrder > 0 ? "utf-16-be" : "utf-16");
                encode_errors errors{ encoding, aText, aErrors };
                std::size_t const width = aWide ? 4u : 2u;
                auto const unit = [&](std::uint32_t u)
                {
                    for (std::size_t k = 0u; k < width; ++k)
                        aOutput += static_cast<char>(u >> (8u * (little ? k : width - 1u - k)));
                };
                if (aByteOrder == 0)
                    unit(0xFEFFu);
                std::u32string replacementText;
                std::string replacementBytes;
                for (std::size_t i = 0u; i < aText.size();)
                {
                    auto const ch = static_cast<std::uint32_t>(aText[i]);
                    if (!is_surrogate(ch))
                    {
                        if (!aWide && ch >= 0x10000u)
                        {
                            unit(0xD800u + ((ch - 0x10000u) >> 10));
                            unit(0xDC00u + ((ch - 0x10000u) & 0x3FFu));
                        }
                        else
                            unit(ch);
                        ++i;
                        continue;
                    }
                    bool isBytes = false;
                    std::size_t next = i + 1u;
                    if (!errors.failure(i, i + 1u, "surrogates not allowed", replacementText, replacementBytes, isBytes, next))
                        return false;
                    if (isBytes)
                    {
                        if (replacementBytes.size() % width != 0u)
                            return errors.raise(i, i + 1u, "surrogates not allowed");
                        aOutput += replacementBytes;
                    }
                    else
                    {
                        if (!all_below(replacementText, 0x80u))
                            return errors.raise(i, i + 1u, "surrogates not allowed");
                        for (auto c : replacementText)
                            unit(c);
                    }
                    i = next;
                }
                return true;
            }

            // --- latin-1 and ascii

            bool decode_latin1(std::string_view aData, std::string& aOutput)
            {
                for (auto c : aData)
                    append_char(aOutput, static_cast<unsigned char>(c));
                return true;
            }

            bool decode_ascii(std::string_view aData, std::string_view aErrors, std::string& aOutput)
            {
                decode_errors errors{ "ascii", aData, aErrors };
                for (std::size_t i = 0u; i < aData.size();)
                {
                    auto const b = static_cast<unsigned char>(aData[i]);
                    if (b < 0x80u)
                    {
                        aOutput += static_cast<char>(b);
                        ++i;
                        continue;
                    }
                    std::size_t next = i + 1u;
                    if (!errors.failure(i, i + 1u, "ordinal not in range(128)", aOutput, next))
                        return false;
                    i = next;
                }
                return true;
            }

            // (unicode_encode_ucs1)
            bool encode_ucs1(std::u32string const& aText, std::string_view aErrors, char32_t aLimit, std::string& aOutput)
            {
                char const* const encoding = aLimit == 256u ? "latin-1" : "ascii";
                char const* const reason = aLimit == 256u ? "ordinal not in range(256)" : "ordinal not in range(128)";
                encode_errors errors{ encoding, aText, aErrors };
                std::u32string replacementText;
                std::string replacementBytes;
                for (std::size_t pos = 0u; pos < aText.size();)
                {
                    if (aText[pos] < aLimit)
                    {
                        aOutput += static_cast<char>(aText[pos++]);
                        continue;
                    }
                    auto collstart = pos;
                    auto collend = pos + 1u;
                    while (collend < aText.size() && aText[collend] >= aLimit)
                        ++collend;
                    if (aErrors == "surrogateescape" && is_builtin_handler("surrogateescape"))
                    {
                        auto i = collstart;
                        for (; i < collend && aText[i] >= 0xDC80u && aText[i] <= 0xDCFFu; ++i)
                            aOutput += static_cast<char>(aText[i] - 0xDC00u);
                        pos = i;
                        if (i >= collend)
                            continue;
                        collstart = pos;
                    }
                    bool isBytes = false;
                    std::size_t next = collend;
                    if (!errors.failure(collstart, collend, reason, replacementText, replacementBytes, isBytes, next))
                        return false;
                    if (isBytes)
                        aOutput += replacementBytes;
                    else
                    {
                        if (!all_below(replacementText, aLimit))
                            return errors.raise(collstart, collend, reason);
                        for (auto c : replacementText)
                            aOutput += static_cast<char>(c);
                    }
                    pos = next;
                }
                return true;
            }

            // --- charmap (a table: the encodings' single-byte codecs; or a Python mapping)

            struct charmap_codec
            {
                ct::charmap const* table = nullptr;
                object* decodingMapping = nullptr;      // (charmap_decode's mapping: a str or a mapping)
                object* encodingMapping = nullptr;      // (charmap_encode's: an EncodingMap's dict or a mapping)
            };

            bool decode_charmap(std::string_view aData, std::string_view aErrors, charmap_codec const& aCodec, std::string& aOutput)
            {
                decode_errors errors{ "charmap", aData, aErrors };
                std::u32string mapString;
                if (aCodec.decodingMapping != nullptr && kind_of(aCodec.decodingMapping) == NEOS_PY_STR)
                    mapString = to_code_points(as<str_object>(aCodec.decodingMapping)->value);
                for (std::size_t i = 0u; i < aData.size();)
                {
                    auto const b = static_cast<unsigned char>(aData[i]);
                    bool undefined = false;
                    if (aCodec.table != nullptr)
                    {
                        auto const x = aCodec.table->table[b];
                        if (x == 0xFFFEu)
                            undefined = true;
                        else
                            append_char(aOutput, x);
                    }
                    else if (kind_of(aCodec.decodingMapping) == NEOS_PY_STR)
                    {
                        auto const x = b < mapString.size() ? mapString[b] : char32_t{ 0xFFFEu };
                        if (x == 0xFFFEu)
                            undefined = true;
                        else
                            append_char(aOutput, x);
                    }
                    else
                    {
                        ref const key{ make_int(b) };
                        ref item{ getitem(aCodec.decodingMapping, key.get()) };
                        if (!item)
                        {
                            ref const e{ take_exception() };
                            if (!exception_is(e.get(), "LookupError"))
                                return (raise_exception(e.get()), false);
                            undefined = true;
                        }
                        else if (kind_of(item.get()) == NEOS_PY_NONE)
                            undefined = true;
                        else if (is_int_like(item.get()))
                        {
                            long long value = 0;
                            if (!to_index(item.get(), value))
                                return false;
                            if (value == 0xFFFE)
                                undefined = true;
                            else if (value < 0 || value > 0x10FFFF)
                                return (error("TypeError", "character mapping must be in range(0x110000)"), false);
                            else
                                append_char(aOutput, static_cast<char32_t>(value));
                        }
                        else if (kind_of(item.get()) == NEOS_PY_STR)
                        {
                            auto const& s = as<str_object>(item.get())->value;
                            auto const points = to_code_points(s);
                            if (points.size() == 1u && points[0] == 0xFFFEu)
                                undefined = true;
                            else
                                aOutput += s;
                        }
                        else
                            return (error("TypeError", "character mapping must return integer, None or str"), false);
                    }
                    if (!undefined)
                    {
                        ++i;
                        continue;
                    }
                    std::size_t next = i + 1u;
                    if (!errors.failure(i, i + 1u, "character maps to <undefined>", aOutput, next))
                        return false;
                    i = next;
                }
                return true;
            }

            // (a character's encoding (charmapencode_output): 1: encoded, 0: undefined, -1: an exception)
            int charmap_encode_char(char32_t aCharacter, charmap_codec const& aCodec, std::string& aOutput)
            {
                if (aCodec.table != nullptr)
                {
                    auto const first = ct::sEncodings + aCodec.table->offset;
                    auto const last = first + aCodec.table->count;
                    auto const i = std::lower_bound(first, last, static_cast<std::uint32_t>(aCharacter), [](ct::encoding const& e, std::uint32_t c) { return e.codePoint < c; });
                    if (i == last || i->codePoint != aCharacter)
                        return 0;
                    aOutput += static_cast<char>(i->byte);
                    return 1;
                }
                ref const key{ make_int(static_cast<long long>(aCharacter)) };
                ref rep{ getitem(aCodec.encodingMapping, key.get()) };
                if (!rep)
                {
                    ref const e{ take_exception() };
                    if (!exception_is(e.get(), "LookupError"))
                        return (raise_exception(e.get()), -1);
                    return 0;
                }
                if (kind_of(rep.get()) == NEOS_PY_NONE)
                    return 0;
                if (is_int_like(rep.get()))
                {
                    long long value = 0;
                    if (!to_index(rep.get(), value))
                        return -1;
                    if (value < 0 || value > 255)
                        return (error("TypeError", "character mapping must be in range(256)"), -1);
                    aOutput += static_cast<char>(value);
                    return 1;
                }
                std::string_view bytes;
                if (is_bytes(rep.get()) && bytes_like(rep.get(), bytes))
                {
                    aOutput += bytes;
                    return 1;
                }
                return (error("TypeError", "character mapping must return integer, bytes or None, not " + std::string{ type_name(rep.get()) }), -1);
            }

            bool encode_charmap(std::u32string const& aText, std::string_view aErrors, charmap_codec const& aCodec, std::string& aOutput)
            {
                encode_errors errors{ "charmap", aText, aErrors };
                char const* const reason = "character maps to <undefined>";
                std::u32string replacementText;
                std::string replacementBytes;
                for (std::size_t pos = 0u; pos < aText.size();)
                {
                    auto const r = charmap_encode_char(aText[pos], aCodec, aOutput);
                    if (r < 0)
                        return false;
                    if (r == 1)
                    {
                        ++pos;
                        continue;
                    }
                    auto const collstart = pos;
                    auto collend = pos + 1u;
                    std::string scratch;
                    while (collend < aText.size())
                    {
                        auto const x = charmap_encode_char(aText[collend], aCodec, scratch);
                        if (x < 0)
                            return false;
                        if (x == 1)
                            break;
                        ++collend;
                    }
                    auto const name = aErrors.empty() ? std::string_view{ "strict" } : aErrors;
                    std::size_t next = collend;
                    if (is_builtin_handler(name) && (name == "strict" || name == "replace" || name == "ignore" || name == "xmlcharrefreplace"))
                    {
                        if (name == "strict")
                            return errors.raise(collstart, collend, reason);
                        std::u32string replacement;
                        if (name == "replace")
                            replacement.assign(collend - collstart, U'?');
                        else if (name == "xmlcharrefreplace")
                            for (auto i = collstart; i < collend; ++i)
                                for (auto c : "&#" + std::to_string(static_cast<unsigned long>(aText[i])) + ";")
                                    replacement += static_cast<char32_t>(c);
                        for (auto c : replacement)
                        {
                            auto const x = charmap_encode_char(c, aCodec, aOutput);
                            if (x < 0)
                                return false;
                            if (x == 0)
                                return errors.raise(collstart, collend, reason);
                        }
                        pos = collend;
                        continue;
                    }
                    bool isBytes = false;
                    if (!errors.failure(collstart, collend, reason, replacementText, replacementBytes, isBytes, next))
                        return false;
                    if (isBytes)
                        aOutput += replacementBytes;
                    else
                        for (auto c : replacementText)
                        {
                            auto const x = charmap_encode_char(c, aCodec, aOutput);
                            if (x < 0)
                                return false;
                            if (x == 0)
                                return errors.raise(collstart, collend, reason);
                        }
                    pos = next;
                }
                return true;
            }

            // --- UTF-7

            int utf7_category(unsigned c)
            {
                static unsigned char const sCategory[128] = {
                    3, 3, 3, 3, 3, 3, 3, 3, 3, 2, 2, 3, 3, 2, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
                    2, 1, 1, 1, 1, 1, 1, 0, 0, 0, 1, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 0,
                    1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 3, 1, 1, 1,
                    1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 3, 3 };
                return sCategory[c];
            }
            bool is_base64(unsigned c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '+' || c == '/'; }
            unsigned from_base64(unsigned c) { return c >= 'A' && c <= 'Z' ? c - 'A' : c >= 'a' && c <= 'z' ? c - 'a' + 26 : c >= '0' && c <= '9' ? c - '0' + 52 : c == '+' ? 62 : 63; }
            char to_base64(unsigned n) { return "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"[n & 0x3Fu]; }

            bool decode_utf7(std::string_view aData, std::string_view aErrors, bool aFinal, std::string& aOutput, std::size_t& aConsumed)
            {
                decode_errors errors{ "utf7", aData, aErrors };
                // (the output as code points: a shift sequence's characters are backed off if it is incomplete)
                std::u32string out;
                bool inShift = false;
                std::size_t shiftOutStart = 0u, startinpos = 0u;
                unsigned base64bits = 0u;
                unsigned long base64buffer = 0u;
                char32_t surrogate = 0u;
                std::size_t s = 0u;
                auto const e = aData.size();
                auto const fail = [&](char const* aReason, std::size_t aEnd) -> bool
                {
                    std::string replacement;
                    std::size_t next = aEnd;
                    if (!errors.failure(startinpos, aEnd, aReason, replacement, next))
                        return false;
                    out += to_code_points(replacement);
                    s = next;
                    return true;
                };
                while (s < e)
                {
                    unsigned const ch = static_cast<unsigned char>(aData[s]);
                    if (inShift)
                    {
                        if (is_base64(ch))
                        {
                            base64buffer = (base64buffer << 6) | from_base64(ch);
                            base64bits += 6u;
                            ++s;
                            if (base64bits >= 16u)
                            {
                                auto const outCh = static_cast<char32_t>(base64buffer >> (base64bits - 16u));
                                base64bits -= 16u;
                                base64buffer &= (1ul << base64bits) - 1u;
                                if (surrogate != 0u)
                                {
                                    if (outCh >= 0xDC00u && outCh <= 0xDFFFu)
                                    {
                                        out += 0x10000u + ((surrogate - 0xD800u) << 10) + (outCh - 0xDC00u);
                                        surrogate = 0u;
                                        continue;
                                    }
                                    out += surrogate;
                                    surrogate = 0u;
                                }
                                if (outCh >= 0xD800u && outCh <= 0xDBFFu)
                                    surrogate = outCh;
                                else
                                    out += outCh;
                            }
                        }
                        else
                        {
                            inShift = false;
                            if (base64bits > 0u)
                            {
                                if (base64bits >= 6u)
                                {
                                    ++s;
                                    if (!fail("partial character in shift sequence", s))
                                        return false;
                                    continue;
                                }
                                if (base64buffer != 0u)
                                {
                                    ++s;
                                    if (!fail("non-zero padding bits in shift sequence", s))
                                        return false;
                                    continue;
                                }
                            }
                            if (surrogate != 0u && ch <= 127u && ch != '+')
                                out += surrogate;
                            surrogate = 0u;
                            if (ch == '-')
                                ++s;
                        }
                    }
                    else if (ch == '+')
                    {
                        startinpos = s;
                        ++s;
                        if (s < e && aData[s] == '-')
                        {
                            ++s;
                            out += U'+';
                        }
                        else if (s < e && !is_base64(static_cast<unsigned char>(aData[s])))
                        {
                            ++s;
                            if (!fail("ill-formed sequence", s))
                                return false;
                        }
                        else
                        {
                            inShift = true;
                            surrogate = 0u;
                            shiftOutStart = out.size();
                            base64bits = 0u;
                            base64buffer = 0u;
                        }
                    }
                    else if (ch <= 127u)
                    {
                        ++s;
                        out += static_cast<char32_t>(ch);
                    }
                    else
                    {
                        startinpos = s;
                        ++s;
                        if (!fail("unexpected special character", s))
                            return false;
                    }
                }
                if (inShift && aFinal)
                {
                    inShift = false;
                    if (surrogate != 0u || base64bits >= 6u || (base64bits > 0u && base64buffer != 0u))
                    {
                        // (CPython then continues (restart) if the handler resumes before the end: its position is the end here)
                        if (!fail("unterminated shift sequence", e))
                            return false;
                        if (s < e)
                        {
                            std::string rest;
                            std::size_t restConsumed = 0u;
                            if (!decode_utf7(aData.substr(s), aErrors, aFinal, rest, restConsumed))
                                return false;
                            aOutput += from_code_points(out) + rest;
                            aConsumed = s + restConsumed;
                            return true;
                        }
                    }
                }
                if (!aFinal && inShift)
                {
                    aConsumed = startinpos;
                    out.resize(shiftOutStart);
                }
                else
                    aConsumed = s;
                aOutput += from_code_points(out);
                return true;
            }

            void encode_utf7(std::u32string const& aText, std::string& aOutput)
            {
                bool inShift = false;
                unsigned base64bits = 0u;
                unsigned long base64buffer = 0u;
                auto const direct = [](char32_t c) { return c < 128u && c > 0u && (utf7_category(c) == 0 || utf7_category(c) == 2 || utf7_category(c) == 1); };
                for (auto ch : aText)
                {
                    bool encodeChar = false;
                    if (inShift)
                    {
                        if (direct(ch))
                        {
                            if (base64bits != 0u)
                            {
                                aOutput += to_base64(static_cast<unsigned>(base64buffer << (6u - base64bits)));
                                base64buffer = 0u;
                                base64bits = 0u;
                            }
                            inShift = false;
                            if (is_base64(ch) || ch == '-')
                                aOutput += '-';
                            aOutput += static_cast<char>(ch);
                        }
                        else
                            encodeChar = true;
                    }
                    else if (ch == '+')
                        aOutput += "+-";
                    else if (direct(ch))
                        aOutput += static_cast<char>(ch);
                    else
                    {
                        aOutput += '+';
                        inShift = true;
                        encodeChar = true;
                    }
                    if (!encodeChar)
                        continue;
                    if (ch >= 0x10000u)
                    {
                        base64bits += 16u;
                        base64buffer = (base64buffer << 16) | (0xD800u + ((ch - 0x10000u) >> 10));
                        while (base64bits >= 6u)
                        {
                            aOutput += to_base64(static_cast<unsigned>(base64buffer >> (base64bits - 6u)));
                            base64bits -= 6u;
                        }
                        ch = 0xDC00u + ((ch - 0x10000u) & 0x3FFu);
                    }
                    base64bits += 16u;
                    base64buffer = (base64buffer << 16) | ch;
                    while (base64bits >= 6u)
                    {
                        aOutput += to_base64(static_cast<unsigned>(base64buffer >> (base64bits - 6u)));
                        base64bits -= 6u;
                    }
                }
                if (base64bits != 0u)
                    aOutput += to_base64(static_cast<unsigned>(base64buffer << (6u - base64bits)));
                if (inShift)
                    aOutput += '-';
            }

            // --- unicode_escape and raw_unicode_escape

            bool decode_unicode_escape(std::string_view aData, std::string_view aErrors, bool aFinal, bool aRaw, std::string& aOutput, std::size_t& aConsumed)
            {
                decode_errors errors{ aRaw ? "rawunicodeescape" : "unicodeescape", aData, aErrors };
                auto const end = aData.size();
                std::size_t s = 0u;
                auto const hex = [](unsigned char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; };
                while (s < end)
                {
                    unsigned char c = static_cast<unsigned char>(aData[s++]);
                    if (c != '\\' || (aRaw && s >= end && aFinal))
                    {
                        append_char(aOutput, c);
                        continue;
                    }
                    auto const startinpos = s - 1u;
                    char const* message = nullptr;
                    bool incomplete = false;
                    if (s >= end)
                    {
                        message = "\\ at end of string";
                        incomplete = true;
                    }
                    else
                    {
                        c = static_cast<unsigned char>(aData[s++]);
                        int count = 0;
                        if (!aRaw)
                            switch (c)
                            {
                            case '\n': continue;
                            case '\\': aOutput += '\\'; continue;
                            case '\'': aOutput += '\''; continue;
                            case '"': aOutput += '"'; continue;
                            case 'b': aOutput += '\b'; continue;
                            case 'f': aOutput += '\f'; continue;
                            case 't': aOutput += '\t'; continue;
                            case 'n': aOutput += '\n'; continue;
                            case 'r': aOutput += '\r'; continue;
                            case 'v': aOutput += '\v'; continue;
                            case 'a': aOutput += '\a'; continue;
                            case '0': case '1': case '2': case '3': case '4': case '5': case '6': case '7':
                                {
                                    char32_t ch = c - '0';
                                    if (s < end && aData[s] >= '0' && aData[s] <= '7')
                                    {
                                        ch = (ch << 3) + static_cast<char32_t>(aData[s++] - '0');
                                        if (s < end && aData[s] >= '0' && aData[s] <= '7')
                                            ch = (ch << 3) + static_cast<char32_t>(aData[s++] - '0');
                                    }
                                    append_char(aOutput, ch);
                                }
                                continue;
                            case 'x': count = 2; message = "truncated \\xXX escape"; break;
                            case 'u': count = 4; message = "truncated \\uXXXX escape"; break;
                            case 'U': count = 8; message = "truncated \\UXXXXXXXX escape"; break;
                            case 'N':
                                message = "malformed \\N character escape";
                                if (s >= end)
                                    incomplete = true;
                                else if (aData[s] == '{')
                                {
                                    auto const start = ++s;
                                    while (s < end && aData[s] != '}')
                                        ++s;
                                    if (s >= end)
                                        incomplete = true;
                                    else if (s > start)
                                    {
                                        auto const name = aData.substr(start, s - start);
                                        ++s;
                                        char32_t ch = 0u;
                                        if (unicode_lookup(name, ch))
                                        {
                                            append_char(aOutput, ch);
                                            continue;
                                        }
                                        message = "unknown Unicode character name";
                                    }
                                }
                                count = -1;
                                break;
                            default:
                                aOutput += '\\';
                                append_char(aOutput, c);
                                continue;
                            }
                        else if (c == 'u')
                        {
                            count = 4;
                            message = "truncated \\uXXXX escape";
                        }
                        else if (c == 'U')
                        {
                            count = 8;
                            message = "truncated \\UXXXXXXXX escape";
                        }
                        else
                        {
                            aOutput += '\\';
                            append_char(aOutput, c);
                            continue;
                        }
                        if (count > 0)
                        {
                            char32_t ch = 0u;
                            bool bad = false;
                            for (; count != 0; ++s, --count)
                            {
                                if (s >= end)
                                {
                                    incomplete = true;
                                    break;
                                }
                                auto const h = hex(static_cast<unsigned char>(aData[s]));
                                if (h < 0)
                                {
                                    bad = true;
                                    break;
                                }
                                ch = (ch << 4) + static_cast<char32_t>(h);
                            }
                            if (!incomplete && !bad)
                            {
                                if (ch > 0x10FFFFu)
                                    message = aRaw ? "\\Uxxxxxxxx out of range" : "illegal Unicode character";
                                else
                                {
                                    append_char(aOutput, ch);
                                    continue;
                                }
                            }
                        }
                    }
                    if (incomplete && !aFinal)
                    {
                        aConsumed = startinpos;
                        return true;
                    }
                    std::size_t next = s;
                    if (!errors.failure(startinpos, s, message, aOutput, next))
                        return false;
                    s = next;
                }
                aConsumed = end;
                return true;
            }

            void encode_unicode_escape(std::u32string const& aText, bool aRaw, std::string& aOutput)
            {
                for (auto ch : aText)
                {
                    char buffer[16];
                    if (ch < 0x100u)
                    {
                        if (aRaw)
                            aOutput += static_cast<char>(ch);
                        else if (ch >= ' ' && ch < 127u)
                        {
                            if (ch == '\\')
                                aOutput += "\\\\";
                            else
                                aOutput += static_cast<char>(ch);
                        }
                        else if (ch == '\t')
                            aOutput += "\\t";
                        else if (ch == '\n')
                            aOutput += "\\n";
                        else if (ch == '\r')
                            aOutput += "\\r";
                        else
                        {
                            std::snprintf(buffer, sizeof(buffer), "\\x%02x", static_cast<unsigned>(ch));
                            aOutput += buffer;
                        }
                    }
                    else
                    {
                        std::snprintf(buffer, sizeof(buffer), ch < 0x10000u ? "\\u%04x" : "\\U%08x", static_cast<unsigned>(ch));
                        aOutput += buffer;
                    }
                }
            }

            // --- escape_decode and escape_encode (bytes' escapes (the codec "string_escape" of Python 2))

            bool escape_decode(std::string_view aData, std::string_view aErrors, std::string& aOutput)
            {
                auto const end = aData.size();
                auto const hex = [](unsigned char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; };
                for (std::size_t s = 0u; s < end;)
                {
                    if (aData[s] != '\\')
                    {
                        aOutput += aData[s++];
                        continue;
                    }
                    ++s;
                    if (s >= end)
                        return (error("ValueError", "Trailing \\ in string"), false);
                    auto const c = aData[s++];
                    switch (c)
                    {
                    case '\n': break;
                    case '\\': aOutput += '\\'; break;
                    case '\'': aOutput += '\''; break;
                    case '"': aOutput += '"'; break;
                    case 'b': aOutput += '\b'; break;
                    case 'f': aOutput += '\f'; break;
                    case 't': aOutput += '\t'; break;
                    case 'n': aOutput += '\n'; break;
                    case 'r': aOutput += '\r'; break;
                    case 'v': aOutput += '\v'; break;
                    case 'a': aOutput += '\a'; break;
                    case '0': case '1': case '2': case '3': case '4': case '5': case '6': case '7':
                        {
                            int v = c - '0';
                            if (s < end && aData[s] >= '0' && aData[s] <= '7')
                            {
                                v = (v << 3) + (aData[s++] - '0');
                                if (s < end && aData[s] >= '0' && aData[s] <= '7')
                                    v = (v << 3) + (aData[s++] - '0');
                            }
                            aOutput += static_cast<char>(v & 0xFF);
                        }
                        break;
                    case 'x':
                        if (s + 1u < end && hex(static_cast<unsigned char>(aData[s])) >= 0 && hex(static_cast<unsigned char>(aData[s + 1u])) >= 0)
                        {
                            aOutput += static_cast<char>(hex(static_cast<unsigned char>(aData[s])) * 16 + hex(static_cast<unsigned char>(aData[s + 1u])));
                            s += 2u;
                            break;
                        }
                        if (aErrors.empty() || aErrors == "strict")
                            return (error("ValueError", "invalid \\x escape at position " + std::to_string(s - 2u)), false);
                        if (aErrors == "replace")
                            aOutput += '?';
                        else if (aErrors != "ignore")
                            return (error("ValueError", "decoding error; unknown error handling code: " + std::string{ aErrors }), false);
                        if (s < end && hex(static_cast<unsigned char>(aData[s])) >= 0)
                            ++s;
                        break;
                    default:
                        aOutput += '\\';
                        aOutput += c;
                        break;
                    }
                }
                return true;
            }

            void escape_encode(std::string_view aData, std::string& aOutput)
            {
                for (auto c : aData)
                {
                    auto const b = static_cast<unsigned char>(c);
                    if (c == '\'' || c == '\\')
                        (aOutput += '\\') += c;
                    else if (c == '\t')
                        aOutput += "\\t";
                    else if (c == '\n')
                        aOutput += "\\n";
                    else if (c == '\r')
                        aOutput += "\\r";
                    else if (b < ' ' || b >= 0x7Fu)
                    {
                        aOutput += "\\x";
                        aOutput += sHexDigits[b >> 4];
                        aOutput += sHexDigits[b & 15u];
                    }
                    else
                        aOutput += c;
                }
            }

            // --- the codecs of the encodings package that this runtime implements

            enum class codec_id
            {
                Utf8, Utf8Sig, Utf16, Utf16Le, Utf16Be, Utf32, Utf32Le, Utf32Be, Ascii, Latin1, Utf7, UnicodeEscape, RawUnicodeEscape, Charmap,
                Table, Undefined, Hex, Base64, Rot13, Zlib, Unsupported
            };

            struct codec_entry
            {
                codec_id id = codec_id::Unsupported;
                ct::codec_module const* module = nullptr;
                ct::charmap const* table = nullptr;
            };

            // (encodings.normalize_encoding, as _Py_normalize_encoding: lower case; runs of other characters than ASCII letters, digits
            // and '.' are '_')
            std::string normalized_encoding(std::string_view aEncoding)
            {
                std::string result;
                bool punct = false;
                for (auto c : aEncoding)
                {
                    auto const u = static_cast<unsigned char>(c);
                    if (u < 0x80u && (std::isalnum(u) || c == '.'))
                    {
                        if (punct && !result.empty())
                            result += '_';
                        result += static_cast<char>(std::tolower(u));
                        punct = false;
                    }
                    else
                        punct = true;
                }
                return result;
            }

            // (the codec of a normalized name (encodings.search_function: its aliases, then its modules))
            codec_entry find_codec(std::string const& aNormalized)
            {
                static std::map<std::string, codec_entry, std::less<>> sCache;
                if (auto const c = sCache.find(aNormalized); c != sCache.end())
                    return c->second;
                auto const alias_of = [](std::string const& aName) -> char const*
                {
                    for (auto const& a : ct::sAliases)
                        if (aName == a.alias)
                            return a.module;
                    return nullptr;
                };
                std::vector<std::string> modules;
                auto aliased = alias_of(aNormalized);
                if (aliased == nullptr)
                {
                    auto dotted = aNormalized;
                    std::replace(dotted.begin(), dotted.end(), '.', '_');
                    aliased = alias_of(dotted);
                }
                if (aliased != nullptr)
                    modules.push_back(aliased);
                modules.push_back(aNormalized);
                codec_entry result;
                for (auto const& m : modules)
                {
                    if (m.empty() || m.find('.') != std::string::npos)
                        continue;
                    ct::codec_module const* module = nullptr;
                    for (auto const& x : ct::sModules)
                        if (m == x.module)
                            module = &x;
                    if (module == nullptr)
                        continue;
                    static std::pair<char const*, codec_id> const sIds[] = { { "utf_8", codec_id::Utf8 }, { "utf_8_sig", codec_id::Utf8Sig },
                        { "utf_16", codec_id::Utf16 }, { "utf_16_le", codec_id::Utf16Le }, { "utf_16_be", codec_id::Utf16Be }, { "utf_32", codec_id::Utf32 },
                        { "utf_32_le", codec_id::Utf32Le }, { "utf_32_be", codec_id::Utf32Be }, { "ascii", codec_id::Ascii }, { "latin_1", codec_id::Latin1 },
                        { "utf_7", codec_id::Utf7 }, { "unicode_escape", codec_id::UnicodeEscape }, { "raw_unicode_escape", codec_id::RawUnicodeEscape },
                        { "charmap", codec_id::Charmap }, { "undefined", codec_id::Undefined }, { "hex_codec", codec_id::Hex }, { "base64_codec", codec_id::Base64 },
                        { "rot_13", codec_id::Rot13 }, { "zlib_codec", codec_id::Zlib } };
                    result.module = module;
                    for (auto const& [name, id] : sIds)
                        if (m == name)
                            result.id = id;
                    for (auto const& t : ct::sCharmaps)
                        if (m == t.module)
                        {
                            result.id = codec_id::Table;
                            result.table = &t;
                        }
                    break;
                }
                sCache.emplace(aNormalized, result);
                return result;
            }

            bool is_text_codec(codec_id aId)
            {
                return aId != codec_id::Hex && aId != codec_id::Base64 && aId != codec_id::Rot13 && aId != codec_id::Zlib && aId != codec_id::Unsupported;
            }

            // (the text encoding and decoding of the builtin codecs: false (no exception) if aId is not one)
            bool builtin_encode(codec_id aId, ct::charmap const* aTable, std::string_view aText, std::string_view aErrors, std::string& aResult, bool& aDone)
            {
                aDone = true;
                aResult.clear();
                // (UTF-8 without surrogates: the text itself)
                if (aId == codec_id::Utf8 || aId == codec_id::Utf8Sig)
                {
                    bool surrogates = false;
                    for (std::size_t i = 0u; i + 1u < aText.size(); ++i)
                        if (static_cast<unsigned char>(aText[i]) == 0xEDu && static_cast<unsigned char>(aText[i + 1u]) >= 0xA0u)
                            surrogates = true;
                    if (aId == codec_id::Utf8Sig)
                        aResult = "\xEF\xBB\xBF";
                    if (!surrogates)
                    {
                        aResult += aText;
                        return true;
                    }
                    return encode_utf8(code_points(aText), aErrors, aResult, aId == codec_id::Utf8Sig ? "utf-8" : "utf-8");
                }
                auto const text = code_points(aText);
                switch (aId)
                {
                case codec_id::Utf16: return encode_utf16_32(text, aErrors, 0, false, aResult);
                case codec_id::Utf16Le: return encode_utf16_32(text, aErrors, -1, false, aResult);
                case codec_id::Utf16Be: return encode_utf16_32(text, aErrors, 1, false, aResult);
                case codec_id::Utf32: return encode_utf16_32(text, aErrors, 0, true, aResult);
                case codec_id::Utf32Le: return encode_utf16_32(text, aErrors, -1, true, aResult);
                case codec_id::Utf32Be: return encode_utf16_32(text, aErrors, 1, true, aResult);
                case codec_id::Ascii: return encode_ucs1(text, aErrors, 128u, aResult);
                case codec_id::Latin1: return encode_ucs1(text, aErrors, 256u, aResult);
                case codec_id::Utf7: encode_utf7(text, aResult); return true;
                case codec_id::UnicodeEscape: encode_unicode_escape(text, false, aResult); return true;
                case codec_id::RawUnicodeEscape: encode_unicode_escape(text, true, aResult); return true;
                case codec_id::Charmap: return encode_ucs1(text, aErrors, 256u, aResult);
                case codec_id::Table:
                    {
                        charmap_codec codec;
                        codec.table = aTable;
                        return encode_charmap(text, aErrors, codec, aResult);
                    }
                case codec_id::Undefined: return (error("UnicodeError", "undefined encoding"), false);
                default:
                    aDone = false;
                    return false;
                }
            }

            bool builtin_decode(codec_id aId, ct::charmap const* aTable, std::string_view aData, std::string_view aErrors, std::string& aResult, bool& aDone)
            {
                aDone = true;
                aResult.clear();
                std::size_t consumed = 0u;
                int byteorder = 0;
                switch (aId)
                {
                case codec_id::Utf8: return decode_utf8(aData, aErrors, true, aResult, consumed);
                case codec_id::Utf8Sig:
                    if (aData.starts_with("\xEF\xBB\xBF"))
                        aData.remove_prefix(3u);
                    return decode_utf8(aData, aErrors, true, aResult, consumed);
                case codec_id::Utf16: return decode_utf16(aData, aErrors, byteorder, true, aResult, consumed);
                case codec_id::Utf16Le: byteorder = -1; return decode_utf16(aData, aErrors, byteorder, true, aResult, consumed);
                case codec_id::Utf16Be: byteorder = 1; return decode_utf16(aData, aErrors, byteorder, true, aResult, consumed);
                case codec_id::Utf32: return decode_utf32(aData, aErrors, byteorder, true, aResult, consumed);
                case codec_id::Utf32Le: byteorder = -1; return decode_utf32(aData, aErrors, byteorder, true, aResult, consumed);
                case codec_id::Utf32Be: byteorder = 1; return decode_utf32(aData, aErrors, byteorder, true, aResult, consumed);
                case codec_id::Ascii: return decode_ascii(aData, aErrors, aResult);
                case codec_id::Latin1: case codec_id::Charmap: return decode_latin1(aData, aResult);
                case codec_id::Utf7: return decode_utf7(aData, aErrors, true, aResult, consumed);
                case codec_id::UnicodeEscape: return decode_unicode_escape(aData, aErrors, true, false, aResult, consumed);
                case codec_id::RawUnicodeEscape: return decode_unicode_escape(aData, aErrors, true, true, aResult, consumed);
                case codec_id::Table:
                    {
                        charmap_codec codec;
                        codec.table = aTable;
                        return decode_charmap(aData, aErrors, codec, aResult);
                    }
                case codec_id::Undefined: return (error("UnicodeError", "undefined encoding"), false);
                default:
                    aDone = false;
                    return false;
                }
            }

            object* registry_lookup(std::string_view aEncoding);   // (codecs.lookup: a CodecInfo (a new reference))

            // (a codec's exception: its note "encoding with 'x' codec failed" (CPython's _PyErr_FormatNote))
            bool codec_failure_note(std::string_view aEncoding, bool aEncode)
            {
                ref const e{ take_exception() };
                ref const addNote{ getattr(e.get(), "add_note") };
                if (addNote)
                    ref{ invoke(addNote.get(), { make_str(std::string{ aEncode ? "encoding" : "decoding" } + " with '" + std::string{ aEncoding } + "' codec failed") }) };
                clear_error();
                raise_exception(e.get());
                return false;
            }
        }

        // --- str.encode and bytes.decode (and the other encodings of text: open's, str()'s, ...)

        bool encode(std::string_view aText, std::string_view aEncoding, std::string_view aErrors, std::string& aResult)
        {
            auto const normalized = normalized_encoding(aEncoding);
            if (normalized == "utf_8" || normalized == "utf8")
            {
                bool done = false;
                return builtin_encode(codec_id::Utf8, nullptr, aText, aErrors, aResult, done);
            }
            auto const codec = find_codec(normalized);
            if (codec.id != codec_id::Unsupported && is_text_codec(codec.id))
            {
                bool done = false;
                return builtin_encode(codec.id, codec.table, aText, aErrors, aResult, done);
            }
            // (a codec of codecs.register's search functions)
            ref const info{ registry_lookup(aEncoding) };
            if (!info)
                return false;
            ref const textEncoding{ getattr(info.get(), "_is_text_encoding") };
            if (!textEncoding)
                clear_error();
            else if (truth(textEncoding.get()) == 0)
                return (error("LookupError", "'" + std::string{ aEncoding } + "' is not a text encoding; use codecs.encode() to handle arbitrary codecs"), false);
            ref const encoder{ getattr(info.get(), "encode") };
            if (!encoder)
                return false;
            ref const result{ invoke(encoder.get(), { make_str(std::string{ aText }), make_str(aErrors.empty() ? "strict" : std::string{ aErrors }) }) };
            if (!result)
                return codec_failure_note(aEncoding, true);
            if (kind_of(result.get()) != NEOS_PY_TUPLE || as<tuple_object>(result.get())->items.size() != 2u)
                return (error("TypeError", "encoder must return a tuple (object, integer)"), false);
            std::string_view data;
            auto const v = as<tuple_object>(result.get())->items[0];
            if (!is_bytes(v) || !bytes_like(v, data))
                return (error("TypeError", "'" + std::string{ aEncoding } + "' encoder returned '" + type_name(v) + "' instead of 'bytes'; use codecs.encode() to encode to arbitrary types"), false);
            aResult = std::string{ data };
            return true;
        }

        bool decode(std::string_view aData, std::string_view aEncoding, std::string_view aErrors, std::string& aResult)
        {
            auto const normalized = normalized_encoding(aEncoding);
            if (normalized == "utf_8" || normalized == "utf8")
            {
                bool done = false;
                return builtin_decode(codec_id::Utf8, nullptr, aData, aErrors, aResult, done);
            }
            auto const codec = find_codec(normalized);
            if (codec.id != codec_id::Unsupported && is_text_codec(codec.id))
            {
                bool done = false;
                return builtin_decode(codec.id, codec.table, aData, aErrors, aResult, done);
            }
            ref const info{ registry_lookup(aEncoding) };
            if (!info)
                return false;
            ref const textEncoding{ getattr(info.get(), "_is_text_encoding") };
            if (!textEncoding)
                clear_error();
            else if (truth(textEncoding.get()) == 0)
                return (error("LookupError", "'" + std::string{ aEncoding } + "' is not a text encoding; use codecs.decode() to handle arbitrary codecs"), false);
            ref const decoder{ getattr(info.get(), "decode") };
            if (!decoder)
                return false;
            // (the decoder's input: a memoryview (CPython's))
            ref const data{ make_bytes(std::string{ aData }) };
            ref const view{ b_memoryview({ data.get() }, nullptr) };
            ref const result{ view ? invoke(decoder.get(), { new_reference(view.get()), make_str(aErrors.empty() ? "strict" : std::string{ aErrors }) }) : nullptr };
            if (!result)
                return codec_failure_note(aEncoding, false);
            if (kind_of(result.get()) != NEOS_PY_TUPLE || as<tuple_object>(result.get())->items.size() != 2u)
                return (error("TypeError", "decoder must return a tuple (object,integer)"), false);
            auto const v = as<tuple_object>(result.get())->items[0];
            if (kind_of(v) != NEOS_PY_STR)
                return (error("TypeError", "'" + std::string{ aEncoding } + "' decoder returned '" + type_name(v) + "' instead of 'str'; use codecs.decode() to decode to arbitrary types"), false);
            aResult = as<str_object>(v)->value;
            return true;
        }
        namespace
        {
            // --- helpers of the modules' functions

            bool text_argument(char const* aFunction, object* aArgument, std::size_t aIndex, std::string_view& aText)
            {
                if (aArgument != nullptr && kind_of(aArgument) == NEOS_PY_STR)
                {
                    aText = as<str_object>(aArgument)->value;
                    return true;
                }
                return (error("TypeError", std::string{ aFunction } + "() argument " + std::to_string(aIndex) + " must be str, not " +
                    (aArgument != nullptr ? type_name(aArgument) : "None")), false);
            }

            bool data_argument(object* aArgument, std::string_view& aData, bool aStr = false)
            {
                if (aStr && kind_of(aArgument) == NEOS_PY_STR)
                {
                    aData = as<str_object>(aArgument)->value;
                    return true;
                }
                if (bytes_like(aArgument, aData))
                    return true;
                return (error("TypeError", std::string{ "a bytes-like object is required, not '" } + type_name(aArgument) + "'"), false);
            }

            bool errors_argument(char const* aFunction, object* aArgument, std::size_t aIndex, std::string& aErrors)
            {
                aErrors.clear();
                if (aArgument == nullptr || kind_of(aArgument) == NEOS_PY_NONE)
                    return true;
                if (kind_of(aArgument) != NEOS_PY_STR)
                    return (error("TypeError", std::string{ aFunction } + "() argument " + std::to_string(aIndex) + " must be str or None, not " + type_name(aArgument)), false);
                aErrors = as<str_object>(aArgument)->value;
                return true;
            }

            object* result_pair(object* aFirst, long long aSecond)  // (steals aFirst)
            {
                return aFirst != nullptr ? make_tuple({ aFirst, make_int(aSecond) }) : nullptr;
            }

            long long text_length(std::string_view aText) { return static_cast<long long>(code_point_count(std::string{ aText })); }

            object* attr(object* aObject, char const* aName) { return getattr(aObject, aName); }

            object* method(object* aObject, char const* aName, std::vector<object*> aArgs) { return call_method_named(aObject, aName, std::move(aArgs)); }

            object* slice_of(object* aObject, object* aStart, object* aStop)
            {
                ref const slice{ make_slice(aStart, aStop, nullptr) };
                return getitem(aObject, slice.get());
            }

            bool unpack_pair(object* aResult, char const* aMessage, ref& aFirst, ref& aSecond)
            {
                if (aResult == nullptr)
                    return false;
                std::vector<object*> items;
                if (!collect(aResult, items))
                    return false;
                if (items.size() != 2u)
                {
                    for (auto i : items)
                        release(i);
                    return (error(aMessage != nullptr ? "TypeError" : "ValueError", aMessage != nullptr ? std::string{ aMessage } : items.size() > 2u ?
                        "too many values to unpack (expected 2)" : "not enough values to unpack (expected 2, got " + std::to_string(items.size()) + ")"), false);
                }
                aFirst = ref{ items[0] };
                aSecond = ref{ items[1] };
                return true;
            }

            // --- the engine's encoders and decoders as functions of objects

            struct codec_descriptor
            {
                codec_id id;
                ct::charmap const* table;
                ct::codec_module const* module;
            };

            codec_descriptor const* descriptor_of(codec_id aId)
            {
                static std::map<codec_id, codec_descriptor> sDescriptors;
                auto& d = sDescriptors[aId];
                d.id = aId;
                return &d;
            }

            object* engine_encode(codec_descriptor const& aCodec, object* aInput, char const* aFunction, std::string_view aErrors)
            {
                std::string_view text;
                if (!text_argument(aFunction, aInput, 1u, text))
                    return nullptr;
                std::string result;
                bool done = false;
                if (!builtin_encode(aCodec.id, aCodec.table, text, aErrors, result, done))
                    return done || failed() ? nullptr : error("LookupError", "unknown encoding");
                return result_pair(make_bytes(std::move(result)), text_length(text));
            }

            // (the decoding of data that may be incomplete (aFinal false): the text and the number of bytes decoded)
            bool engine_decode(codec_descriptor const& aCodec, std::string_view aData, std::string_view aErrors, bool aFinal, std::string& aResult,
                std::size_t& aConsumed, int* aByteOrder = nullptr)
            {
                aResult.clear();
                aConsumed = aData.size();
                int byteorder = aByteOrder != nullptr ? *aByteOrder : 0;
                bool ok = true;
                switch (aCodec.id)
                {
                case codec_id::Utf8: ok = decode_utf8(aData, aErrors, aFinal, aResult, aConsumed); break;
                case codec_id::Utf16: ok = decode_utf16(aData, aErrors, byteorder, aFinal, aResult, aConsumed); break;
                case codec_id::Utf16Le: byteorder = -1; ok = decode_utf16(aData, aErrors, byteorder, aFinal, aResult, aConsumed); break;
                case codec_id::Utf16Be: byteorder = 1; ok = decode_utf16(aData, aErrors, byteorder, aFinal, aResult, aConsumed); break;
                case codec_id::Utf32: ok = decode_utf32(aData, aErrors, byteorder, aFinal, aResult, aConsumed); break;
                case codec_id::Utf32Le: byteorder = -1; ok = decode_utf32(aData, aErrors, byteorder, aFinal, aResult, aConsumed); break;
                case codec_id::Utf32Be: byteorder = 1; ok = decode_utf32(aData, aErrors, byteorder, aFinal, aResult, aConsumed); break;
                case codec_id::Utf7: ok = decode_utf7(aData, aErrors, aFinal, aResult, aConsumed); break;
                case codec_id::UnicodeEscape: ok = decode_unicode_escape(aData, aErrors, aFinal, false, aResult, aConsumed); break;
                case codec_id::RawUnicodeEscape: ok = decode_unicode_escape(aData, aErrors, aFinal, true, aResult, aConsumed); break;
                default:
                    {
                        bool done = false;
                        ok = builtin_decode(aCodec.id, aCodec.table, aData, aErrors, aResult, done);
                        if (!done && !failed())
                            return (error("LookupError", "unknown encoding"), false);
                    }
                    break;
                }
                if (aByteOrder != nullptr)
                    *aByteOrder = byteorder;
                return ok;
            }

            object* engine_decode_pair(codec_descriptor const& aCodec, object* aInput, std::string_view aErrors, bool aFinal)
            {
                std::string_view data;
                if (!data_argument(aInput, data))
                    return nullptr;
                std::string result;
                std::size_t consumed = 0u;
                if (!engine_decode(aCodec, data, aErrors, aFinal, result, consumed))
                    return nullptr;
                return result_pair(make_str(std::move(result)), static_cast<long long>(consumed));
            }

            // --- _codecs's functions (CPython's: builtins)

            object* codec_encode_function(char const* aName, codec_id aId, std::vector<object*> const& aArgs, object* aKwargs, bool aByteOrder = false)
            {
                std::vector<object*> a;
                if (!parse_arguments(aName, aArgs, aKwargs, aByteOrder ? std::vector<char const*>{ "str", "errors", "byteorder" } : std::vector<char const*>{ "str", "errors" },
                    1u, aByteOrder ? 3u : 2u, a))
                    return nullptr;
                std::string errors;
                if (!errors_argument(aName, a[1], 2u, errors))
                    return nullptr;
                auto id = aId;
                if (aByteOrder && a[2] != nullptr)
                {
                    long long byteorder = 0;
                    if (!to_index(a[2], byteorder))
                        return nullptr;
                    bool const wide = aId == codec_id::Utf32;
                    id = byteorder < 0 ? (wide ? codec_id::Utf32Le : codec_id::Utf16Le) : byteorder > 0 ? (wide ? codec_id::Utf32Be : codec_id::Utf16Be) : aId;
                }
                return engine_encode(*descriptor_of(id), a[0], aName, errors);
            }

            object* codec_decode_function(char const* aName, codec_id aId, std::vector<object*> const& aArgs, object* aKwargs, bool aFinalDefault = false, bool aStr = false)
            {
                std::vector<object*> a;
                if (!parse_arguments(aName, aArgs, aKwargs, { "data", "errors", "final" }, 1u, 3u, a))
                    return nullptr;
                std::string errors;
                if (!errors_argument(aName, a[1], 2u, errors))
                    return nullptr;
                bool final = aFinalDefault;
                if (a[2] != nullptr)
                {
                    auto const t = truth(a[2]);
                    if (t < 0)
                        return nullptr;
                    final = t == 1;
                }
                std::string_view data;
                if (!data_argument(a[0], data, aStr))
                    return nullptr;
                std::string result;
                std::size_t consumed = 0u;
                if (!engine_decode(*descriptor_of(aId), data, errors, final, result, consumed))
                    return nullptr;
                return result_pair(make_str(std::move(result)), static_cast<long long>(consumed));
            }

            object* ex_decode_function(char const* aName, codec_id aId, std::vector<object*> const& aArgs, object* aKwargs)
            {
                std::vector<object*> a;
                if (!parse_arguments(aName, aArgs, aKwargs, { "data", "errors", "byteorder", "final" }, 1u, 4u, a))
                    return nullptr;
                std::string errors;
                if (!errors_argument(aName, a[1], 2u, errors))
                    return nullptr;
                long long byteorder = 0;
                if (a[2] != nullptr && !to_index(a[2], byteorder))
                    return nullptr;
                bool final = false;
                if (a[3] != nullptr)
                {
                    auto const t = truth(a[3]);
                    if (t < 0)
                        return nullptr;
                    final = t == 1;
                }
                std::string_view data;
                if (!data_argument(a[0], data))
                    return nullptr;
                std::string result;
                std::size_t consumed = 0u;
                int bo = byteorder < 0 ? -1 : byteorder > 0 ? 1 : 0;
                if (!engine_decode(*descriptor_of(aId), data, errors, final, result, consumed, &bo))
                    return nullptr;
                return make_tuple({ make_str(std::move(result)), make_int(static_cast<long long>(consumed)), make_int(bo) });
            }

            // (an EncodingMap (charmap_build's): its map of characters to bytes)
            object* sEncodingMapClass = nullptr;

            bool charmap_mapping(object* aMapping, bool aEncode, charmap_codec& aCodec)
            {
                if (aEncode)
                {
                    if (sEncodingMapClass != nullptr && is_instance(aMapping) && user_class_of(aMapping) == sEncodingMapClass)
                    {
                        ref const map{ getattr(aMapping, "_map") };
                        if (!map)
                            return false;
                        aCodec.encodingMapping = map.get();     // (the instance keeps it)
                    }
                    else
                        aCodec.encodingMapping = aMapping;
                }
                else
                    aCodec.decodingMapping = aMapping;
                return true;
            }

            builtin_entry const sEncodingMapMethods[] =
            {
                { "size", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        // (CPython's struct encoding_map's size: its header and its level 2 and 3 tables)
                        ref const map{ getattr(aArgs[0], "_map") };
                        if (!map)
                            return nullptr;
                        std::set<long long> level2, level3;
                        for (auto const& item : as<dict_object>(map.get())->items)
                        {
                            long long c = 0;
                            if (to_index(item.first, c) && c != 0)
                            {
                                level2.insert(c >> 11);
                                level3.insert(c >> 7);
                            }
                        }
                        return make_int(63 + 16 * static_cast<long long>(level2.size()) + 128 * static_cast<long long>(level3.size()));
                    } }
            };

            object* charmap_build(object* aMap)
            {
                std::string_view text;
                if (!text_argument("charmap_build", aMap, 1u, text))
                    return nullptr;
                ref const map{ make_dict() };
                auto const points = to_code_points(text);
                // (an EncodingMap of 256 characters (the first NUL), else a dict (CPython's PyUnicode_BuildEncodingMap))
                bool needDict = points.size() != 256u || points[0] != 0u;
                for (std::size_t i = 1u; i < points.size() && !needDict; ++i)
                    if (points[i] == 0u || points[i] > 0xFFFFu)
                        needDict = true;
                for (std::size_t i = 0u; i < points.size(); ++i)
                {
                    if (!needDict && points[i] == 0xFFFEu)
                        continue;
                    ref const key{ make_int(points[i]) };
                    ref const value{ make_int(static_cast<long long>(i)) };
                    dict_set(map.get(), key.get(), value.get());
                }
                if (needDict)
                    return new_reference(map.get());
                ref const instance{ call(sEncodingMapClass, nullptr, nullptr) };
                if (!instance || setattr(instance.get(), "_map", map.get()) < 0)
                    return nullptr;
                return new_reference(instance.get());
            }

            // (the builtin error handlers as functions (codecs.strict_errors etc.))
            object* builtin_handler_call(char const* aName, std::vector<object*> const& aArgs)
            {
                if (aArgs.size() != 1u)
                    return error("TypeError", std::string{ aName } + "_errors() takes exactly one argument (" + std::to_string(aArgs.size()) + " given)");
                auto const e = aArgs[0];
                auto const type = exception_type_name(e);
                if (type == nullptr)
                {
                    if (std::strcmp(aName, "strict") == 0)
                        return error("TypeError", "codec must pass exception instance");
                    return error("TypeError", std::string{ "don't know how to handle " } + type_name(e) + " in error callback");
                }
                if (std::strcmp(aName, "strict") == 0)
                {
                    raise_exception(e);
                    return nullptr;
                }
                bool const encodeError = exception_is(e, "UnicodeEncodeError");
                bool const decodeError = exception_is(e, "UnicodeDecodeError");
                bool const translateError = exception_is(e, "UnicodeTranslateError");
                if (!encodeError && !decodeError && !translateError)
                    return error("TypeError", std::string{ "don't know how to handle " } + type + " in error callback");
                long long start = 0, end = 0;
                if (!attribute_int(e, "start", start) || !attribute_int(e, "end", end))
                    return nullptr;
                ref const objectValue{ getattr(e, "object") };
                if (!objectValue)
                    return nullptr;
                if (decodeError)
                {
                    std::string_view data;
                    if (!bytes_like(objectValue.get(), data))
                        return error("TypeError", "object attribute must be bytes");
                    start = std::clamp(start, 0LL, static_cast<long long>(data.size()));
                    end = std::clamp(end, start, static_cast<long long>(data.size()));
                    ref const encoding{ getattr(e, "encoding") };
                    ref const reason{ encoding ? getattr(e, "reason") : nullptr };
                    if (!reason)
                        return nullptr;
                    decode_errors handler{ nullptr, data, aName };
                    std::string encodingText = text_of(encoding.get());
                    handler.encoding = encodingText.c_str();
                    handler.exception = ref{ new_reference(e) };
                    std::string output;
                    std::size_t next = static_cast<std::size_t>(end);
                    std::string const reasonText = text_of(reason.get());
                    if (!handler.failure(static_cast<std::size_t>(start), static_cast<std::size_t>(end), reasonText.c_str(), output, next))
                        return nullptr;
                    return make_tuple({ make_str(output), make_int(static_cast<long long>(next)) });
                }
                if (kind_of(objectValue.get()) != NEOS_PY_STR)
                    return error("TypeError", "object attribute must be unicode");
                auto const text = to_code_points(as<str_object>(objectValue.get())->value);
                start = std::clamp(start, 0LL, static_cast<long long>(text.size()));
                end = std::clamp(end, start, static_cast<long long>(text.size()));
                std::string const name{ aName };
                if (translateError && (name == "xmlcharrefreplace" || name == "namereplace" || name == "surrogateescape" || name == "surrogatepass"))
                    return error("TypeError", "don't know how to handle UnicodeTranslateError in error callback");
                if (translateError && name == "replace")
                    return make_tuple({ make_str(from_code_points(std::u32string(static_cast<std::size_t>(end - start), char32_t{ 0xFFFDu }))), make_int(end) });
                ref const encoding{ encodeError ? getattr(e, "encoding") : make_str("") };
                ref const reason{ encoding ? getattr(e, "reason") : nullptr };
                if (!reason)
                    return nullptr;
                std::string const encodingText = text_of(encoding.get());
                std::string const reasonText = text_of(reason.get());
                encode_errors handler{ encodingText.c_str(), text, aName };
                handler.exception = ref{ new_reference(e) };
                std::u32string replacementText;
                std::string replacementBytes;
                bool isBytes = false;
                std::size_t next = static_cast<std::size_t>(end);
                if (!handler.failure(static_cast<std::size_t>(start), static_cast<std::size_t>(end), reasonText.c_str(), replacementText, replacementBytes, isBytes, next))
                    return nullptr;
                return make_tuple({ isBytes ? make_bytes(replacementBytes) : make_str(from_code_points(replacementText)), make_int(static_cast<long long>(next)) });
            }

            builtin_entry const sErrorHandlers[] =
            {
                { "strict_errors", [](std::vector<object*> const& aArgs, object*) -> object* { return builtin_handler_call("strict", aArgs); } },
                { "ignore_errors", [](std::vector<object*> const& aArgs, object*) -> object* { return builtin_handler_call("ignore", aArgs); } },
                { "replace_errors", [](std::vector<object*> const& aArgs, object*) -> object* { return builtin_handler_call("replace", aArgs); } },
                { "xmlcharrefreplace_errors", [](std::vector<object*> const& aArgs, object*) -> object* { return builtin_handler_call("xmlcharrefreplace", aArgs); } },
                { "backslashreplace_errors", [](std::vector<object*> const& aArgs, object*) -> object* { return builtin_handler_call("backslashreplace", aArgs); } },
                { "namereplace_errors", [](std::vector<object*> const& aArgs, object*) -> object* { return builtin_handler_call("namereplace", aArgs); } },
                { "surrogateescape", [](std::vector<object*> const& aArgs, object*) -> object* { return builtin_handler_call("surrogateescape", aArgs); } },
                { "surrogatepass", [](std::vector<object*> const& aArgs, object*) -> object* { return builtin_handler_call("surrogatepass", aArgs); } }
            };

            void init_error_handlers()
            {
                if (!builtin_error_handlers().empty())
                    return;
                static char const* const sNames[] = { "strict", "ignore", "replace", "xmlcharrefreplace", "backslashreplace", "namereplace", "surrogateescape", "surrogatepass" };
                for (std::size_t i = 0u; i < std::size(sNames); ++i)
                {
                    auto const f = make_builtin(sErrorHandlers[i]);
                    as<function_object>(f)->binds = false;
                    builtin_error_handlers()[sNames[i]] = f;
                    error_registry()[sNames[i]] = new_reference(f);
                }
            }

            // --- the registry (codecs.register and lookup)

            std::vector<object*>& search_functions()
            {
                static std::vector<object*> sFunctions;
                return sFunctions;
            }

            std::map<std::string, object*, std::less<>>& lookup_cache()
            {
                static std::map<std::string, object*, std::less<>> sCache;
                return sCache;
            }

            object* builtin_search(std::string const& aNormalized);     // (encodings.search_function: a CodecInfo, None or null (an exception))

            object* registry_lookup(std::string_view aEncoding)
            {
                auto const normalized = normalized_encoding(aEncoding);
                if (auto const c = lookup_cache().find(normalized); c != lookup_cache().end())
                    return new_reference(c->second);
                ref result{ builtin_search(normalized) };
                if (!result)
                    return nullptr;
                if (kind_of(result.get()) == NEOS_PY_NONE)
                {
                    result = ref{};
                    ref const name{ make_str(normalized) };
                    for (auto f : std::vector<object*>{ search_functions() })
                    {
                        ref r{ invoke(f, { new_reference(name.get()) }) };
                        if (!r)
                            return nullptr;
                        if (kind_of(r.get()) == NEOS_PY_NONE)
                            continue;
                        auto const k = kind_of(builtin_value(r.get()));
                        if (k != NEOS_PY_TUPLE || as<tuple_object>(builtin_value(r.get()))->items.size() != 4u)
                            return error("TypeError", "codec search functions must return 4-tuples");
                        result = std::move(r);
                        break;
                    }
                }
                if (!result)
                    return error("LookupError", "unknown encoding: " + std::string{ aEncoding });
                lookup_cache()[normalized] = new_reference(result.get());
                return new_reference(result.get());
            }

            // (a codec's function: info.encode or info.decode (CPython's codec_getitem))
            object* codec_function(object* aEncoding, char const* aWhich)
            {
                std::string_view encoding;
                if (!text_argument(aWhich, aEncoding, 1u, encoding))
                    return nullptr;
                ref const info{ registry_lookup(encoding) };
                return info ? getattr(info.get(), aWhich) : nullptr;
            }

            // (codecs.encode and codecs.decode: any codec; the codec's exception has a note)
            object* codec_call(object* aObject, object* aEncoding, object* aErrors, bool aEncode)
            {
                std::string_view encoding = "utf-8";
                if (aEncoding != nullptr && !text_argument(aEncode ? "encode" : "decode", aEncoding, 2u, encoding))
                    return nullptr;
                ref const info{ registry_lookup(encoding) };
                if (!info)
                    return nullptr;
                ref const function{ getattr(info.get(), aEncode ? "encode" : "decode") };
                if (!function)
                    return nullptr;
                ref const errors{ aErrors != nullptr ? new_reference(aErrors) : make_str("strict") };
                ref const result{ invoke(function.get(), { new_reference(aObject), new_reference(errors.get()) }) };
                if (!result)
                {
                    ref const e{ take_exception() };
                    ref const addNote{ getattr(e.get(), "add_note") };
                    if (addNote)
                        ref{ invoke(addNote.get(), { make_str(std::string{ aEncode ? "encoding" : "decoding" } + " with '" + std::string{ encoding } + "' codec failed") }) };
                    clear_error();
                    raise_exception(e.get());
                    return nullptr;
                }
                if (kind_of(builtin_value(result.get())) != NEOS_PY_TUPLE || as<tuple_object>(builtin_value(result.get()))->items.size() != 2u)
                    return error("TypeError", aEncode ? "encoder must return a tuple (object, integer)" : "decoder must return a tuple (object,integer)");
                return new_reference(as<tuple_object>(builtin_value(result.get()))->items[0]);
            }

            builtin_entry const sCodecsFunctions[] =
            {
                { "register", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("register", aArgs, aKwargs, { "search_function" }, 1u, 1u, a))
                            return nullptr;
                        if (!is_callable(a[0]))
                            return error("TypeError", "argument must be callable");
                        search_functions().push_back(new_reference(a[0]));
                        return new_reference(none());
                    } },
                { "unregister", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("unregister", aArgs, aKwargs, { "search_function" }, 1u, 1u, a))
                            return nullptr;
                        auto& functions = search_functions();
                        if (auto const f = std::find(functions.begin(), functions.end(), a[0]); f != functions.end())
                        {
                            release(*f);
                            functions.erase(f);
                            for (auto& c : lookup_cache())
                                release(c.second);
                            lookup_cache().clear();
                        }
                        return new_reference(none());
                    } },
                { "lookup", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        std::string_view encoding;
                        if (!parse_arguments("lookup", aArgs, aKwargs, { "encoding" }, 1u, 1u, a) || !text_argument("lookup", a[0], 1u, encoding))
                            return nullptr;
                        return registry_lookup(encoding);
                    } },
                { "encode", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("encode", aArgs, aKwargs, { "obj", "encoding", "errors" }, 1u, 0u, a))
                            return nullptr;
                        return codec_call(a[0], a[1], a[2], true);
                    } },
                { "decode", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("decode", aArgs, aKwargs, { "obj", "encoding", "errors" }, 1u, 0u, a))
                            return nullptr;
                        return codec_call(a[0], a[1], a[2], false);
                    } },
                { "_forget_codec", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        std::string_view encoding;
                        if (!parse_arguments("_forget_codec", aArgs, aKwargs, { "encoding" }, 1u, 1u, a) || !text_argument("_forget_codec", a[0], 1u, encoding))
                            return nullptr;
                        if (auto const c = lookup_cache().find(normalized_encoding(encoding)); c != lookup_cache().end())
                        {
                            release(c->second);
                            lookup_cache().erase(c);
                        }
                        return new_reference(none());
                    } },
                { "register_error", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        std::string_view name;
                        if (!parse_arguments("register_error", aArgs, aKwargs, { "errors", "handler" }, 2u, 2u, a) || !text_argument("register_error", a[0], 1u, name))
                            return nullptr;
                        if (!is_callable(a[1]))
                            return error("TypeError", "handler must be callable");
                        init_error_handlers();
                        auto& entry = error_registry()[std::string{ name }];
                        release(entry);
                        entry = new_reference(a[1]);
                        return new_reference(none());
                    } },
                { "lookup_error", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        std::string_view name;
                        if (!parse_arguments("lookup_error", aArgs, aKwargs, { "name" }, 1u, 1u, a) || !text_argument("lookup_error", a[0], 1u, name))
                            return nullptr;
                        init_error_handlers();
                        return lookup_error_handler(name);
                    } },
                { "_unregister_error", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        std::string_view name;
                        if (!parse_arguments("_unregister_error", aArgs, aKwargs, { "errors" }, 1u, 1u, a) || !text_argument("_unregister_error", a[0], 1u, name))
                            return nullptr;
                        if (builtin_error_handlers().contains(name))
                            return error("ValueError", "cannot un-register built-in error handler '" + std::string{ name } + "'");
                        auto const r = error_registry().find(name);
                        if (r == error_registry().end())
                            return make_bool(false);
                        release(r->second);
                        error_registry().erase(r);
                        return make_bool(true);
                    } },
                { "utf_8_encode", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return codec_encode_function("utf_8_encode", codec_id::Utf8, aArgs, aKwargs); } },
                { "utf_8_decode", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return codec_decode_function("utf_8_decode", codec_id::Utf8, aArgs, aKwargs); } },
                { "utf_7_encode", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return codec_encode_function("utf_7_encode", codec_id::Utf7, aArgs, aKwargs); } },
                { "utf_7_decode", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return codec_decode_function("utf_7_decode", codec_id::Utf7, aArgs, aKwargs); } },
                { "utf_16_encode", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return codec_encode_function("utf_16_encode", codec_id::Utf16, aArgs, aKwargs, true); } },
                { "utf_16_decode", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return codec_decode_function("utf_16_decode", codec_id::Utf16, aArgs, aKwargs); } },
                { "utf_16_le_encode", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return codec_encode_function("utf_16_le_encode", codec_id::Utf16Le, aArgs, aKwargs); } },
                { "utf_16_le_decode", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return codec_decode_function("utf_16_le_decode", codec_id::Utf16Le, aArgs, aKwargs); } },
                { "utf_16_be_encode", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return codec_encode_function("utf_16_be_encode", codec_id::Utf16Be, aArgs, aKwargs); } },
                { "utf_16_be_decode", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return codec_decode_function("utf_16_be_decode", codec_id::Utf16Be, aArgs, aKwargs); } },
                { "utf_16_ex_decode", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return ex_decode_function("utf_16_ex_decode", codec_id::Utf16, aArgs, aKwargs); } },
                { "utf_32_encode", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return codec_encode_function("utf_32_encode", codec_id::Utf32, aArgs, aKwargs, true); } },
                { "utf_32_decode", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return codec_decode_function("utf_32_decode", codec_id::Utf32, aArgs, aKwargs); } },
                { "utf_32_le_encode", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return codec_encode_function("utf_32_le_encode", codec_id::Utf32Le, aArgs, aKwargs); } },
                { "utf_32_le_decode", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return codec_decode_function("utf_32_le_decode", codec_id::Utf32Le, aArgs, aKwargs); } },
                { "utf_32_be_encode", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return codec_encode_function("utf_32_be_encode", codec_id::Utf32Be, aArgs, aKwargs); } },
                { "utf_32_be_decode", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return codec_decode_function("utf_32_be_decode", codec_id::Utf32Be, aArgs, aKwargs); } },
                { "utf_32_ex_decode", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return ex_decode_function("utf_32_ex_decode", codec_id::Utf32, aArgs, aKwargs); } },
                { "unicode_escape_encode", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return codec_encode_function("unicode_escape_encode", codec_id::UnicodeEscape, aArgs, aKwargs); } },
                { "unicode_escape_decode", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return codec_decode_function("unicode_escape_decode", codec_id::UnicodeEscape, aArgs, aKwargs, true, true); } },
                { "raw_unicode_escape_encode", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return codec_encode_function("raw_unicode_escape_encode", codec_id::RawUnicodeEscape, aArgs, aKwargs); } },
                { "raw_unicode_escape_decode", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return codec_decode_function("raw_unicode_escape_decode", codec_id::RawUnicodeEscape, aArgs, aKwargs, true, true); } },
                { "latin_1_encode", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return codec_encode_function("latin_1_encode", codec_id::Latin1, aArgs, aKwargs); } },
                { "latin_1_decode", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return codec_decode_function("latin_1_decode", codec_id::Latin1, aArgs, aKwargs); } },
                { "ascii_encode", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return codec_encode_function("ascii_encode", codec_id::Ascii, aArgs, aKwargs); } },
                { "ascii_decode", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return codec_decode_function("ascii_decode", codec_id::Ascii, aArgs, aKwargs); } },
                { "charmap_encode", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("charmap_encode", aArgs, aKwargs, { "str", "errors", "mapping" }, 1u, 3u, a))
                            return nullptr;
                        std::string errors;
                        std::string_view text;
                        if (!text_argument("charmap_encode", a[0], 1u, text) || !errors_argument("charmap_encode", a[1], 2u, errors))
                            return nullptr;
                        std::string result;
                        auto const points = to_code_points(text);
                        if (a[2] == nullptr || kind_of(a[2]) == NEOS_PY_NONE)
                        {
                            if (!encode_ucs1(points, errors, 256u, result))
                                return nullptr;
                        }
                        else
                        {
                            charmap_codec codec;
                            if (!charmap_mapping(a[2], true, codec) || !encode_charmap(points, errors, codec, result))
                                return nullptr;
                        }
                        return result_pair(make_bytes(std::move(result)), static_cast<long long>(points.size()));
                    } },
                { "charmap_decode", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("charmap_decode", aArgs, aKwargs, { "data", "errors", "mapping" }, 1u, 3u, a))
                            return nullptr;
                        std::string errors;
                        std::string_view data;
                        if (!errors_argument("charmap_decode", a[1], 2u, errors) || !data_argument(a[0], data))
                            return nullptr;
                        std::string result;
                        if (a[2] == nullptr || kind_of(a[2]) == NEOS_PY_NONE)
                            decode_latin1(data, result);
                        else
                        {
                            charmap_codec codec;
                            if (!charmap_mapping(a[2], false, codec) || !decode_charmap(data, errors, codec, result))
                                return nullptr;
                        }
                        return result_pair(make_str(std::move(result)), static_cast<long long>(data.size()));
                    } },
                { "charmap_build", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("charmap_build", aArgs, aKwargs, { "map" }, 1u, 1u, a))
                            return nullptr;
                        return charmap_build(a[0]);
                    } },
                { "readbuffer_encode", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        std::string errors;
                        std::string_view data;
                        if (!parse_arguments("readbuffer_encode", aArgs, aKwargs, { "data", "errors" }, 1u, 2u, a) || !errors_argument("readbuffer_encode", a[1], 2u, errors) ||
                            !data_argument(a[0], data, true))
                            return nullptr;
                        return result_pair(make_bytes(std::string{ data }), static_cast<long long>(data.size()));
                    } },
                { "escape_encode", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        std::string errors;
                        if (!parse_arguments("escape_encode", aArgs, aKwargs, { "data", "errors" }, 1u, 2u, a) || !errors_argument("escape_encode", a[1], 2u, errors))
                            return nullptr;
                        if (!is_bytes(a[0]))
                            return error("TypeError", std::string{ "escape_encode() argument 1 must be bytes, not " } + type_name(a[0]));
                        std::string_view data;
                        bytes_like(a[0], data);
                        std::string result;
                        escape_encode(data, result);
                        return result_pair(make_bytes(std::move(result)), static_cast<long long>(data.size()));
                    } },
                { "escape_decode", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        std::string errors;
                        std::string_view data;
                        if (!parse_arguments("escape_decode", aArgs, aKwargs, { "data", "errors" }, 1u, 2u, a) || !errors_argument("escape_decode", a[1], 2u, errors) ||
                            !data_argument(a[0], data, true))
                            return nullptr;
                        std::string result;
                        if (!escape_decode(data, errors, result))
                            return nullptr;
                        return result_pair(make_bytes(std::move(result)), static_cast<long long>(data.size()));
                    } }
            };

            // --- codecs.py's classes

            object* sCodecInfo = nullptr;
            object* sCodec = nullptr;
            object* sIncrementalEncoder = nullptr;
            object* sBufferedIncrementalEncoder = nullptr;
            object* sIncrementalDecoder = nullptr;
            object* sBufferedIncrementalDecoder = nullptr;
            object* sStreamWriter = nullptr;
            object* sStreamReader = nullptr;
            object* sStreamReaderWriter = nullptr;
            object* sStreamRecoder = nullptr;

            object* not_implemented_error() { return error("NotImplementedError", ""); }

            bool set_attributes(object* aObject, std::initializer_list<std::pair<char const*, object*>> aAttributes)
            {
                for (auto const& a : aAttributes)
                    if (setattr(aObject, a.first, a.second) < 0)
                        return false;
                return true;
            }

            builtin_entry const sCodecInfoMethods[] =
            {
                { "__new__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!self_arguments("CodecInfo", aArgs, aKwargs, { "encode", "decode", "streamreader", "streamwriter", "incrementalencoder",
                            "incrementaldecoder", "name", "_is_text_encoding" }, 2u, 0u, a))
                            return nullptr;
                        if (aArgs.size() > 8u)
                            return error("TypeError", "CodecInfo.__new__() takes from 3 to 8 positional arguments but " + std::to_string(aArgs.size()) + " were given");
                        auto const value = [&](std::size_t i) { return a[i] != nullptr ? a[i] : none(); };
                        ref const items{ make_tuple({ new_reference(value(0u)), new_reference(value(1u)), new_reference(value(2u)), new_reference(value(3u)) }) };
                        ref const args{ make_tuple({ new_reference(items.get()) }) };
                        ref const self{ new_instance(aArgs[0], args.get(), nullptr) };
                        if (!self || !set_attributes(self.get(), { { "name", value(6u) }, { "encode", value(0u) }, { "decode", value(1u) },
                            { "incrementalencoder", value(4u) }, { "incrementaldecoder", value(5u) }, { "streamwriter", value(3u) }, { "streamreader", value(2u) } }))
                            return nullptr;
                        if (a[7] != nullptr && kind_of(a[7]) != NEOS_PY_NONE && setattr(self.get(), "_is_text_encoding", a[7]) < 0)
                            return nullptr;
                        return new_reference(self.get());
                    } },
                { "__repr__", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        ref const cls{ class_of(aArgs[0]) };
                        ref const module{ getattr(cls.get(), "__module__") };
                        ref const qualname{ getattr(cls.get(), "__qualname__") };
                        ref const name{ getattr(aArgs[0], "name") };
                        if (!module || !qualname || !name)
                            return nullptr;
                        return make_str("<" + text_of(module.get()) + "." + text_of(qualname.get()) + " object for encoding " + text_of(name.get()) + " at " +
                            object_address(aArgs[0]) + ">");
                    } },
                { "__getnewargs__", [](std::vector<object*> const& aArgs, object*) -> object* { return fn_tuple_of(aArgs[0]); } }
            };

            builtin_entry const sCodecMethods[] =
            {
                { "encode", [](std::vector<object*> const&, object*) -> object* { return not_implemented_error(); } },
                { "decode", [](std::vector<object*> const&, object*) -> object* { return not_implemented_error(); } }
            };

            object* errors_init(std::vector<object*> const& aArgs, object* aKwargs, char const* aName, bool aBuffer, bool aBytes)
            {
                std::vector<object*> a;
                if (!self_arguments(aName, aArgs, aKwargs, { "errors" }, 0u, 0u, a))
                    return nullptr;
                ref const errors{ a[0] != nullptr ? new_reference(a[0]) : make_str("strict") };
                if (setattr(aArgs[0], "errors", errors.get()) < 0)
                    return nullptr;
                if (aBuffer)
                {
                    ref const buffer{ aBytes ? make_bytes("") : make_str("") };
                    if (setattr(aArgs[0], "buffer", buffer.get()) < 0)
                        return nullptr;
                }
                return new_reference(none());
            }

            builtin_entry const sIncrementalEncoderMethods[] =
            {
                { "__init__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return errors_init(aArgs, aKwargs, "IncrementalEncoder.__init__", true, false); } },
                { "encode", [](std::vector<object*> const&, object*) -> object* { return not_implemented_error(); } },
                { "reset", [](std::vector<object*> const&, object*) -> object* { return new_reference(none()); } },
                { "getstate", [](std::vector<object*> const&, object*) -> object* { return make_int(0); } },
                { "setstate", [](std::vector<object*> const&, object*) -> object* { return new_reference(none()); } }
            };

            builtin_entry const sBufferedIncrementalEncoderMethods[] =
            {
                { "__init__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return errors_init(aArgs, aKwargs, "BufferedIncrementalEncoder.__init__", true, false); } },
                { "_buffer_encode", [](std::vector<object*> const&, object*) -> object* { return not_implemented_error(); } },
                { "encode", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!self_arguments("encode", aArgs, aKwargs, { "input", "final" }, 1u, 0u, a))
                            return nullptr;
                        auto const self = aArgs[0];
                        ref const buffer{ attr(self, "buffer") };
                        ref const data{ buffer ? binary(NEOS_PY_ADD, buffer.get(), a[0], false) : nullptr };
                        ref const errors{ data ? attr(self, "errors") : nullptr };
                        if (!errors)
                            return nullptr;
                        ref result, consumed;
                        if (!unpack_pair(method(self, "_buffer_encode", { new_reference(data.get()), new_reference(errors.get()), a[1] != nullptr ? new_reference(a[1]) : make_bool(false) }),
                            nullptr, result, consumed))
                            return nullptr;
                        ref const rest{ slice_of(data.get(), consumed.get(), nullptr) };
                        if (!rest || setattr(self, "buffer", rest.get()) < 0)
                            return nullptr;
                        return new_reference(result.get());
                    } },
                { "reset", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        ref const empty{ make_str("") };
                        return setattr(aArgs[0], "buffer", empty.get()) < 0 ? nullptr : new_reference(none());
                    } },
                { "getstate", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        ref const buffer{ attr(aArgs[0], "buffer") };
                        if (!buffer)
                            return nullptr;
                        auto const t = truth(buffer.get());
                        return t < 0 ? nullptr : t == 1 ? new_reference(buffer.get()) : make_int(0);
                    } },
                { "setstate", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!self_arguments("setstate", aArgs, aKwargs, { "state" }, 1u, 0u, a))
                            return nullptr;
                        auto const t = truth(a[0]);
                        if (t < 0)
                            return nullptr;
                        ref const value{ t == 1 ? new_reference(a[0]) : make_str("") };
                        return setattr(aArgs[0], "buffer", value.get()) < 0 ? nullptr : new_reference(none());
                    } }
            };

            builtin_entry const sIncrementalDecoderMethods[] =
            {
                { "__init__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return errors_init(aArgs, aKwargs, "IncrementalDecoder.__init__", false, false); } },
                { "decode", [](std::vector<object*> const&, object*) -> object* { return not_implemented_error(); } },
                { "reset", [](std::vector<object*> const&, object*) -> object* { return new_reference(none()); } },
                { "getstate", [](std::vector<object*> const&, object*) -> object* { return make_tuple({ make_bytes(""), make_int(0) }); } },
                { "setstate", [](std::vector<object*> const&, object*) -> object* { return new_reference(none()); } }
            };

            builtin_entry const sBufferedIncrementalDecoderMethods[] =
            {
                { "__init__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return errors_init(aArgs, aKwargs, "BufferedIncrementalDecoder.__init__", true, true); } },
                { "_buffer_decode", [](std::vector<object*> const&, object*) -> object* { return not_implemented_error(); } },
                { "decode", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!self_arguments("decode", aArgs, aKwargs, { "input", "final" }, 1u, 0u, a))
                            return nullptr;
                        auto const self = aArgs[0];
                        ref const buffer{ attr(self, "buffer") };
                        ref const data{ buffer ? binary(NEOS_PY_ADD, buffer.get(), a[0], false) : nullptr };
                        ref const errors{ data ? attr(self, "errors") : nullptr };
                        if (!errors)
                            return nullptr;
                        ref result, consumed;
                        if (!unpack_pair(method(self, "_buffer_decode", { new_reference(data.get()), new_reference(errors.get()), a[1] != nullptr ? new_reference(a[1]) : make_bool(false) }),
                            nullptr, result, consumed))
                            return nullptr;
                        ref const rest{ slice_of(data.get(), consumed.get(), nullptr) };
                        if (!rest || setattr(self, "buffer", rest.get()) < 0)
                            return nullptr;
                        return new_reference(result.get());
                    } },
                { "reset", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        ref const empty{ make_bytes("") };
                        return setattr(aArgs[0], "buffer", empty.get()) < 0 ? nullptr : new_reference(none());
                    } },
                { "getstate", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        ref const buffer{ attr(aArgs[0], "buffer") };
                        return buffer ? make_tuple({ new_reference(buffer.get()), make_int(0) }) : nullptr;
                    } },
                { "setstate", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!self_arguments("setstate", aArgs, aKwargs, { "state" }, 1u, 0u, a))
                            return nullptr;
                        ref const zero{ make_int(0) };
                        ref const buffer{ getitem(a[0], zero.get()) };
                        return buffer && setattr(aArgs[0], "buffer", buffer.get()) >= 0 ? new_reference(none()) : nullptr;
                    } }
            };

            object* stream_getattr(std::vector<object*> const& aArgs, object* aKwargs)
            {
                std::vector<object*> a;
                if (!self_arguments("__getattr__", aArgs, aKwargs, { "name" }, 1u, 0u, a))
                    return nullptr;
                ref const stream{ attr(aArgs[0], "stream") };
                if (!stream || kind_of(a[0]) != NEOS_PY_STR)
                    return stream ? error("TypeError", "attribute name must be string") : nullptr;
                return getattr(stream.get(), as<str_object>(a[0])->value.c_str());
            }

            object* stream_exit(std::vector<object*> const& aArgs, object*)
            {
                ref const stream{ attr(aArgs[0], "stream") };
                ref const closed{ stream ? method(stream.get(), "close", {}) : nullptr };
                return closed ? new_reference(none()) : nullptr;
            }

            object* stream_reduce(std::vector<object*> const& aArgs, object*)
            {
                ref const cls{ class_of(aArgs[0]) };
                ref const name{ getattr(cls.get(), "__name__") };
                return error("TypeError", "can't serialize " + (name ? text_of(name.get()) : std::string{}));
            }

            object* stream_init(std::vector<object*> const& aArgs, object* aKwargs, char const* aName, bool aReader)
            {
                std::vector<object*> a;
                if (!self_arguments(aName, aArgs, aKwargs, { "stream", "errors" }, 1u, 0u, a))
                    return nullptr;
                auto const self = aArgs[0];
                ref const errors{ a[1] != nullptr ? new_reference(a[1]) : make_str("strict") };
                if (!set_attributes(self, { { "stream", a[0] }, { "errors", errors.get() } }))
                    return nullptr;
                if (aReader)
                {
                    ref const empty{ make_bytes("") };
                    ref const type{ attr(self, "charbuffertype") };
                    ref const emptyChars{ type ? call(type.get(), nullptr, nullptr) : nullptr };
                    if (!emptyChars || !set_attributes(self, { { "bytebuffer", empty.get() }, { "_empty_charbuffer", emptyChars.get() }, { "charbuffer", emptyChars.get() },
                        { "linebuffer", none() } }))
                        return nullptr;
                }
                return new_reference(none());
            }

            builtin_entry const sStreamWriterMethods[] =
            {
                { "__init__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return stream_init(aArgs, aKwargs, "StreamWriter.__init__", false); } },
                { "write", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!self_arguments("write", aArgs, aKwargs, { "object" }, 1u, 0u, a))
                            return nullptr;
                        auto const self = aArgs[0];
                        ref const errors{ attr(self, "errors") };
                        ref data, consumed;
                        if (!errors || !unpack_pair(method(self, "encode", { new_reference(a[0]), new_reference(errors.get()) }), nullptr, data, consumed))
                            return nullptr;
                        ref const stream{ attr(self, "stream") };
                        ref const written{ stream ? method(stream.get(), "write", { new_reference(data.get()) }) : nullptr };
                        return written ? new_reference(none()) : nullptr;
                    } },
                { "writelines", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!self_arguments("writelines", aArgs, aKwargs, { "list" }, 1u, 0u, a))
                            return nullptr;
                        ref const empty{ make_str("") };
                        ref const joined{ method(empty.get(), "join", { new_reference(a[0]) }) };
                        ref const written{ joined ? method(aArgs[0], "write", { new_reference(joined.get()) }) : nullptr };
                        return written ? new_reference(none()) : nullptr;
                    } },
                { "reset", [](std::vector<object*> const&, object*) -> object* { return new_reference(none()); } },
                { "seek", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!self_arguments("seek", aArgs, aKwargs, { "offset", "whence" }, 1u, 0u, a))
                            return nullptr;
                        ref const stream{ attr(aArgs[0], "stream") };
                        ref const whence{ a[1] != nullptr ? new_reference(a[1]) : make_int(0) };
                        ref const sought{ stream ? method(stream.get(), "seek", { new_reference(a[0]), new_reference(whence.get()) }) : nullptr };
                        if (!sought)
                            return nullptr;
                        ref const zero{ make_int(0) };
                        if (equal(whence.get(), zero.get()) == 1 && equal(a[0], zero.get()) == 1)
                        {
                            ref const r{ method(aArgs[0], "reset", {}) };
                            if (!r)
                                return nullptr;
                        }
                        return new_reference(none());
                    } },
                { "__getattr__", stream_getattr },
                { "__enter__", [](std::vector<object*> const& aArgs, object*) -> object* { return new_reference(aArgs[0]); } },
                { "__exit__", stream_exit },
                { "__reduce_ex__", stream_reduce }
            };

            // (StreamReader.read(size=-1, chars=-1, firstline=False))
            object* stream_read(object* aSelf, object* aSize, object* aChars, bool aFirstline)
            {
                ref const linebuffer{ attr(aSelf, "linebuffer") };
                if (!linebuffer)
                    return nullptr;
                auto const hasLines = truth(linebuffer.get());
                if (hasLines < 0)
                    return nullptr;
                if (hasLines == 1)
                {
                    ref const empty{ attr(aSelf, "_empty_charbuffer") };
                    ref const joined{ empty ? method(empty.get(), "join", { new_reference(linebuffer.get()) }) : nullptr };
                    if (!joined || !set_attributes(aSelf, { { "charbuffer", joined.get() }, { "linebuffer", none() } }))
                        return nullptr;
                }
                long long size = -1, chars = -1;
                if ((aSize != nullptr && !to_index(aSize, size)) || (aChars != nullptr && !to_index(aChars, chars)))
                    return nullptr;
                if (chars < 0)
                    chars = size;
                for (;;)
                {
                    if (chars >= 0)
                    {
                        ref const charbuffer{ attr(aSelf, "charbuffer") };
                        if (!charbuffer)
                            return nullptr;
                        auto const n = length(charbuffer.get());
                        if (n < 0)
                            return nullptr;
                        if (n >= chars)
                            break;
                    }
                    ref const stream{ attr(aSelf, "stream") };
                    ref const newdata{ stream ? (size < 0 ? method(stream.get(), "read", {}) : method(stream.get(), "read", { make_int(size) })) : nullptr };
                    ref const bytebuffer{ newdata ? attr(aSelf, "bytebuffer") : nullptr };
                    ref const data{ bytebuffer ? binary(NEOS_PY_ADD, bytebuffer.get(), newdata.get(), false) : nullptr };
                    if (!data)
                        return nullptr;
                    auto const any = truth(data.get());
                    if (any < 0)
                        return nullptr;
                    if (any == 0)
                        break;
                    ref const errors{ attr(aSelf, "errors") };
                    if (!errors)
                        return nullptr;
                    ref newchars, decodedbytes;
                    if (!unpack_pair(method(aSelf, "decode", { new_reference(data.get()), new_reference(errors.get()) }), nullptr, newchars, decodedbytes))
                    {
                        ref const e{ take_exception() };
                        if (!aFirstline || !exception_is(e.get(), "UnicodeDecodeError"))
                            return (raise_exception(e.get()), nullptr);
                        ref const start{ getattr(e.get(), "start") };
                        ref const head{ start ? slice_of(data.get(), nullptr, start.get()) : nullptr };
                        if (!head || !unpack_pair(method(aSelf, "decode", { new_reference(head.get()), new_reference(errors.get()) }), nullptr, newchars, decodedbytes))
                            return nullptr;
                        ref const yes{ make_bool(true) };
                        ref const kw{ keywords({ { "keepends", yes.get() } }) };
                        ref const splitlines{ getattr(newchars.get(), "splitlines") };
                        ref const lines{ splitlines ? call(splitlines.get(), ref{ make_tuple() }.get(), kw.get()) : nullptr };
                        if (!lines)
                            return nullptr;
                        if (length(lines.get()) <= 1)
                            return (raise_exception(e.get()), nullptr);
                    }
                    ref const rest{ slice_of(data.get(), decodedbytes.get(), nullptr) };
                    ref const charbuffer{ rest ? attr(aSelf, "charbuffer") : nullptr };
                    ref const added{ charbuffer ? binary(NEOS_PY_ADD, charbuffer.get(), newchars.get(), true) : nullptr };
                    if (!added || !set_attributes(aSelf, { { "bytebuffer", rest.get() }, { "charbuffer", added.get() } }))
                        return nullptr;
                    auto const more = truth(newdata.get());
                    if (more < 0)
                        return nullptr;
                    if (more == 0)
                        break;
                }
                ref const charbuffer{ attr(aSelf, "charbuffer") };
                if (!charbuffer)
                    return nullptr;
                if (chars < 0)
                {
                    ref const empty{ attr(aSelf, "_empty_charbuffer") };
                    if (!empty || setattr(aSelf, "charbuffer", empty.get()) < 0)
                        return nullptr;
                    return new_reference(charbuffer.get());
                }
                ref const count{ make_int(chars) };
                ref const result{ slice_of(charbuffer.get(), nullptr, count.get()) };
                ref const rest{ result ? slice_of(charbuffer.get(), count.get(), nullptr) : nullptr };
                if (!rest || setattr(aSelf, "charbuffer", rest.get()) < 0)
                    return nullptr;
                return new_reference(result.get());
            }

            object* split_lines(object* aText, bool aKeepends)
            {
                ref const keep{ make_bool(aKeepends) };
                ref const kw{ keywords({ { "keepends", keep.get() } }) };
                ref const splitlines{ getattr(aText, "splitlines") };
                return splitlines ? call(splitlines.get(), ref{ make_tuple() }.get(), kw.get()) : nullptr;
            }

            object* first_line(object* aLine)   // (line.splitlines(keepends=False)[0])
            {
                ref const lines{ split_lines(aLine, false) };
                ref const zero{ make_int(0) };
                return lines ? getitem(lines.get(), zero.get()) : nullptr;
            }

            // (StreamReader.readline(size=None, keepends=True))
            object* stream_readline(object* aSelf, object* aSize, bool aKeepends)
            {
                ref linebuffer{ attr(aSelf, "linebuffer") };
                if (!linebuffer)
                    return nullptr;
                auto const hasLines = truth(linebuffer.get());
                if (hasLines < 0)
                    return nullptr;
                ref const zero{ make_int(0) };
                if (hasLines == 1)
                {
                    ref line{ getitem(linebuffer.get(), zero.get()) };
                    if (!line)
                        return nullptr;
                    ref const removed{ method(linebuffer.get(), "pop", { make_int(0) }) };
                    if (!removed)
                        return nullptr;
                    if (length(linebuffer.get()) == 1)
                    {
                        ref const last{ getitem(linebuffer.get(), zero.get()) };
                        if (!last || !set_attributes(aSelf, { { "charbuffer", last.get() }, { "linebuffer", none() } }))
                            return nullptr;
                    }
                    if (!aKeepends)
                        line = ref{ first_line(line.get()) };
                    return line ? new_reference(line.get()) : nullptr;
                }
                bool const sizeNone = aSize == nullptr || kind_of(aSize) == NEOS_PY_NONE;
                long long readsize = 72;
                if (!sizeNone)
                {
                    auto const t = truth(aSize);
                    if (t < 0)
                        return nullptr;
                    if (t == 1 && !to_index(aSize, readsize))
                        return nullptr;
                }
                ref line{ attr(aSelf, "_empty_charbuffer") };
                if (!line)
                    return nullptr;
                for (;;)
                {
                    ref data{ stream_read(aSelf, ref{ make_int(readsize) }.get(), nullptr, true) };
                    if (!data)
                        return nullptr;
                    auto const any = truth(data.get());
                    if (any < 0)
                        return nullptr;
                    if (any == 1)
                    {
                        bool cr = false;
                        if (kind_of(data.get()) == NEOS_PY_STR)
                            cr = as<str_object>(data.get())->value.ends_with('\r');
                        else if (is_bytes(data.get()))
                        {
                            std::string_view bytes;
                            bytes_like(data.get(), bytes);
                            cr = bytes.ends_with('\r');
                        }
                        if (cr)
                        {
                            ref const one{ make_int(1) };
                            ref const more{ stream_read(aSelf, one.get(), one.get(), false) };
                            if (!more)
                                return nullptr;
                            data = ref{ binary(NEOS_PY_ADD, data.get(), more.get(), true) };
                            if (!data)
                                return nullptr;
                        }
                    }
                    line = ref{ binary(NEOS_PY_ADD, line.get(), data.get(), true) };
                    if (!line)
                        return nullptr;
                    ref lines{ split_lines(line.get(), true) };
                    if (!lines)
                        return nullptr;
                    auto const count = length(lines.get());
                    if (count > 0)
                    {
                        if (count > 1)
                        {
                            line = ref{ getitem(lines.get(), zero.get()) };
                            ref const removed{ line ? method(lines.get(), "pop", { make_int(0) }) : nullptr };
                            if (!removed)
                                return nullptr;
                            ref const charbuffer{ attr(aSelf, "charbuffer") };
                            if (!charbuffer)
                                return nullptr;
                            if (count - 1 > 1)
                            {
                                ref const lastIndex{ make_int(-1) };
                                ref const last{ getitem(lines.get(), lastIndex.get()) };
                                ref const joined{ last ? binary(NEOS_PY_ADD, last.get(), charbuffer.get(), false) : nullptr };
                                if (!joined || setitem(lines.get(), lastIndex.get(), joined.get()) < 0 ||
                                    !set_attributes(aSelf, { { "linebuffer", lines.get() }, { "charbuffer", none() } }))
                                    return nullptr;
                            }
                            else
                            {
                                ref const first{ getitem(lines.get(), zero.get()) };
                                ref const joined{ first ? binary(NEOS_PY_ADD, first.get(), charbuffer.get(), false) : nullptr };
                                if (!joined || setattr(aSelf, "charbuffer", joined.get()) < 0)
                                    return nullptr;
                            }
                            if (!aKeepends)
                                line = ref{ first_line(line.get()) };
                            break;
                        }
                        ref const withEnd{ getitem(lines.get(), zero.get()) };
                        ref const withoutEnd{ withEnd ? first_line(withEnd.get()) : nullptr };
                        if (!withoutEnd)
                            return nullptr;
                        auto const same = equal(withEnd.get(), withoutEnd.get());
                        if (same < 0)
                            return nullptr;
                        if (same == 0)
                        {
                            ref const empty{ attr(aSelf, "_empty_charbuffer") };
                            ref const one{ make_int(1) };
                            ref const restLines{ slice_of(lines.get(), one.get(), nullptr) };
                            ref const joined{ empty && restLines ? method(empty.get(), "join", { new_reference(restLines.get()) }) : nullptr };
                            ref const charbuffer{ joined ? attr(aSelf, "charbuffer") : nullptr };
                            ref const newBuffer{ charbuffer ? binary(NEOS_PY_ADD, joined.get(), charbuffer.get(), false) : nullptr };
                            if (!newBuffer || setattr(aSelf, "charbuffer", newBuffer.get()) < 0)
                                return nullptr;
                            line = ref{ new_reference(aKeepends ? withEnd.get() : withoutEnd.get()) };
                            break;
                        }
                    }
                    if (any == 0 || !sizeNone)
                    {
                        auto const t = truth(line.get());
                        if (t == 1 && !aKeepends)
                            line = ref{ first_line(line.get()) };
                        break;
                    }
                    if (readsize < 8000)
                        readsize *= 2;
                }
                return line ? new_reference(line.get()) : nullptr;
            }

            builtin_entry const sStreamReaderMethods[] =
            {
                { "__init__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return stream_init(aArgs, aKwargs, "StreamReader.__init__", true); } },
                { "decode", [](std::vector<object*> const&, object*) -> object* { return not_implemented_error(); } },
                { "read", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!self_arguments("read", aArgs, aKwargs, { "size", "chars", "firstline" }, 0u, 0u, a))
                            return nullptr;
                        auto const firstline = a[2] != nullptr ? truth(a[2]) : 0;
                        return firstline < 0 ? nullptr : stream_read(aArgs[0], a[0], a[1], firstline == 1);
                    } },
                { "readline", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!self_arguments("readline", aArgs, aKwargs, { "size", "keepends" }, 0u, 0u, a))
                            return nullptr;
                        auto const keepends = a[1] != nullptr ? truth(a[1]) : 1;
                        return keepends < 0 ? nullptr : stream_readline(aArgs[0], a[0], keepends == 1);
                    } },
                { "readlines", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!self_arguments("readlines", aArgs, aKwargs, { "sizehint", "keepends" }, 0u, 0u, a))
                            return nullptr;
                        ref const data{ method(aArgs[0], "read", {}) };
                        ref const keepends{ a[1] != nullptr ? new_reference(a[1]) : make_bool(true) };
                        return data ? method(data.get(), "splitlines", { new_reference(keepends.get()) }) : nullptr;
                    } },
                { "reset", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        ref const empty{ make_bytes("") };
                        ref const chars{ attr(aArgs[0], "_empty_charbuffer") };
                        if (!chars || !set_attributes(aArgs[0], { { "bytebuffer", empty.get() }, { "charbuffer", chars.get() }, { "linebuffer", none() } }))
                            return nullptr;
                        return new_reference(none());
                    } },
                { "seek", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!self_arguments("seek", aArgs, aKwargs, { "offset", "whence" }, 1u, 0u, a))
                            return nullptr;
                        ref const stream{ attr(aArgs[0], "stream") };
                        ref const sought{ stream ? method(stream.get(), "seek", { new_reference(a[0]), a[1] != nullptr ? new_reference(a[1]) : make_int(0) }) : nullptr };
                        ref const reset{ sought ? method(aArgs[0], "reset", {}) : nullptr };
                        return reset ? new_reference(none()) : nullptr;
                    } },
                { "__next__", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        ref const line{ method(aArgs[0], "readline", {}) };
                        if (!line)
                            return nullptr;
                        auto const t = truth(line.get());
                        if (t < 0)
                            return nullptr;
                        if (t == 0)
                            return error("StopIteration", "");
                        return new_reference(line.get());
                    } },
                { "__iter__", [](std::vector<object*> const& aArgs, object*) -> object* { return new_reference(aArgs[0]); } },
                { "__getattr__", stream_getattr },
                { "__enter__", [](std::vector<object*> const& aArgs, object*) -> object* { return new_reference(aArgs[0]); } },
                { "__exit__", stream_exit },
                { "__reduce_ex__", stream_reduce }
            };

            object* forward(object* aSelf, char const* aMember, char const* aMethod, std::vector<object*> aArgs)
            {
                ref const member{ attr(aSelf, aMember) };
                if (!member)
                {
                    for (auto a : aArgs)
                        release(a);
                    return nullptr;
                }
                return method(member.get(), aMethod, std::move(aArgs));
            }

            builtin_entry const sStreamReaderWriterMethods[] =
            {
                { "__init__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!self_arguments("StreamReaderWriter.__init__", aArgs, aKwargs, { "stream", "Reader", "Writer", "errors" }, 3u, 0u, a))
                            return nullptr;
                        ref const errors{ a[3] != nullptr ? new_reference(a[3]) : make_str("strict") };
                        ref const reader{ invoke(a[1], { new_reference(a[0]), new_reference(errors.get()) }) };
                        ref const writer{ reader ? invoke(a[2], { new_reference(a[0]), new_reference(errors.get()) }) : nullptr };
                        if (!writer || !set_attributes(aArgs[0], { { "stream", a[0] }, { "reader", reader.get() }, { "writer", writer.get() }, { "errors", errors.get() } }))
                            return nullptr;
                        return new_reference(none());
                    } },
                { "read", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!self_arguments("read", aArgs, aKwargs, { "size" }, 0u, 0u, a))
                            return nullptr;
                        return forward(aArgs[0], "reader", "read", { a[0] != nullptr ? new_reference(a[0]) : make_int(-1) });
                    } },
                { "readline", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!self_arguments("readline", aArgs, aKwargs, { "size", "keepends" }, 0u, 0u, a))
                            return nullptr;
                        return forward(aArgs[0], "reader", "readline", { new_reference(a[0] != nullptr ? a[0] : none()), a[1] != nullptr ? new_reference(a[1]) : make_bool(true) });
                    } },
                { "readlines", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!self_arguments("readlines", aArgs, aKwargs, { "sizehint", "keepends" }, 0u, 0u, a))
                            return nullptr;
                        return forward(aArgs[0], "reader", "readlines", { new_reference(a[0] != nullptr ? a[0] : none()), a[1] != nullptr ? new_reference(a[1]) : make_bool(true) });
                    } },
                { "__next__", [](std::vector<object*> const& aArgs, object*) -> object* { return forward(aArgs[0], "reader", "__next__", {}); } },
                { "__iter__", [](std::vector<object*> const& aArgs, object*) -> object* { return new_reference(aArgs[0]); } },
                { "write", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!self_arguments("write", aArgs, aKwargs, { "data" }, 1u, 0u, a))
                            return nullptr;
                        return forward(aArgs[0], "writer", "write", { new_reference(a[0]) });
                    } },
                { "writelines", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!self_arguments("writelines", aArgs, aKwargs, { "list" }, 1u, 0u, a))
                            return nullptr;
                        return forward(aArgs[0], "writer", "writelines", { new_reference(a[0]) });
                    } },
                { "reset", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        ref const r{ forward(aArgs[0], "reader", "reset", {}) };
                        ref const w{ r ? forward(aArgs[0], "writer", "reset", {}) : nullptr };
                        return w ? new_reference(none()) : nullptr;
                    } },
                { "seek", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!self_arguments("seek", aArgs, aKwargs, { "offset", "whence" }, 1u, 0u, a))
                            return nullptr;
                        ref const whence{ a[1] != nullptr ? new_reference(a[1]) : make_int(0) };
                        ref const sought{ forward(aArgs[0], "stream", "seek", { new_reference(a[0]), new_reference(whence.get()) }) };
                        ref const r{ sought ? forward(aArgs[0], "reader", "reset", {}) : nullptr };
                        if (!r)
                            return nullptr;
                        ref const zero{ make_int(0) };
                        if (equal(whence.get(), zero.get()) == 1 && equal(a[0], zero.get()) == 1)
                        {
                            ref const w{ forward(aArgs[0], "writer", "reset", {}) };
                            if (!w)
                                return nullptr;
                        }
                        return new_reference(none());
                    } },
                { "__getattr__", stream_getattr },
                { "__enter__", [](std::vector<object*> const& aArgs, object*) -> object* { return new_reference(aArgs[0]); } },
                { "__exit__", stream_exit },
                { "__reduce_ex__", stream_reduce }
            };

            // (StreamRecoder's results: (data, n) = self.encode or self.decode (data, self.errors))
            object* recode(object* aSelf, char const* aWhich, object* aData)   // (steals aData)
            {
                ref const data{ aData };
                if (!data)
                    return nullptr;
                ref const function{ attr(aSelf, aWhich) };
                ref const errors{ function ? attr(aSelf, "errors") : nullptr };
                ref result, n;
                if (!errors || !unpack_pair(invoke(function.get(), { new_reference(data.get()), new_reference(errors.get()) }), nullptr, result, n))
                    return nullptr;
                return new_reference(result.get());
            }

            builtin_entry const sStreamRecoderMethods[] =
            {
                { "__init__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!self_arguments("StreamRecoder.__init__", aArgs, aKwargs, { "stream", "encode", "decode", "Reader", "Writer", "errors" }, 5u, 0u, a))
                            return nullptr;
                        ref const errors{ a[5] != nullptr ? new_reference(a[5]) : make_str("strict") };
                        ref const reader{ invoke(a[3], { new_reference(a[0]), new_reference(errors.get()) }) };
                        ref const writer{ reader ? invoke(a[4], { new_reference(a[0]), new_reference(errors.get()) }) : nullptr };
                        if (!writer || !set_attributes(aArgs[0], { { "stream", a[0] }, { "encode", a[1] }, { "decode", a[2] }, { "reader", reader.get() },
                            { "writer", writer.get() }, { "errors", errors.get() } }))
                            return nullptr;
                        return new_reference(none());
                    } },
                { "read", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!self_arguments("read", aArgs, aKwargs, { "size" }, 0u, 0u, a))
                            return nullptr;
                        return recode(aArgs[0], "encode", forward(aArgs[0], "reader", "read", { a[0] != nullptr ? new_reference(a[0]) : make_int(-1) }));
                    } },
                { "readline", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!self_arguments("readline", aArgs, aKwargs, { "size" }, 0u, 0u, a))
                            return nullptr;
                        if (a[0] == nullptr || kind_of(a[0]) == NEOS_PY_NONE)
                            return recode(aArgs[0], "encode", forward(aArgs[0], "reader", "readline", {}));
                        return recode(aArgs[0], "encode", forward(aArgs[0], "reader", "readline", { new_reference(a[0]) }));
                    } },
                { "readlines", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        ref const data{ recode(aArgs[0], "encode", forward(aArgs[0], "reader", "read", {})) };
                        return data ? split_lines(data.get(), true) : nullptr;
                    } },
                { "__next__", [](std::vector<object*> const& aArgs, object*) -> object* { return recode(aArgs[0], "encode", forward(aArgs[0], "reader", "__next__", {})); } },
                { "__iter__", [](std::vector<object*> const& aArgs, object*) -> object* { return new_reference(aArgs[0]); } },
                { "write", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!self_arguments("write", aArgs, aKwargs, { "data" }, 1u, 0u, a))
                            return nullptr;
                        ref const data{ recode(aArgs[0], "decode", new_reference(a[0])) };
                        return data ? forward(aArgs[0], "writer", "write", { new_reference(data.get()) }) : nullptr;
                    } },
                { "writelines", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!self_arguments("writelines", aArgs, aKwargs, { "list" }, 1u, 0u, a))
                            return nullptr;
                        ref const empty{ make_bytes("") };
                        ref const joined{ method(empty.get(), "join", { new_reference(a[0]) }) };
                        ref const data{ joined ? recode(aArgs[0], "decode", new_reference(joined.get())) : nullptr };
                        return data ? forward(aArgs[0], "writer", "write", { new_reference(data.get()) }) : nullptr;
                    } },
                { "reset", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        ref const r{ forward(aArgs[0], "reader", "reset", {}) };
                        ref const w{ r ? forward(aArgs[0], "writer", "reset", {}) : nullptr };
                        return w ? new_reference(none()) : nullptr;
                    } },
                { "seek", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!self_arguments("seek", aArgs, aKwargs, { "offset", "whence" }, 1u, 0u, a))
                            return nullptr;
                        ref const whence{ a[1] != nullptr ? new_reference(a[1]) : make_int(0) };
                        ref const r{ forward(aArgs[0], "reader", "seek", { new_reference(a[0]), new_reference(whence.get()) }) };
                        ref const w{ r ? forward(aArgs[0], "writer", "seek", { new_reference(a[0]), new_reference(whence.get()) }) : nullptr };
                        return w ? new_reference(none()) : nullptr;
                    } },
                { "__getattr__", stream_getattr },
                { "__enter__", [](std::vector<object*> const& aArgs, object*) -> object* { return new_reference(aArgs[0]); } },
                { "__exit__", stream_exit },
                { "__reduce_ex__", stream_reduce }
            };
            // --- the encodings' codecs (encodings.utf_8 etc.): their functions and classes

            std::map<std::string, object*, std::less<>>& codecs_function_objects()   // (_codecs's functions: one object each)
            {
                static std::map<std::string, object*, std::less<>> sFunctions;
                if (sFunctions.empty())
                    for (auto const& f : sCodecsFunctions)
                    {
                        auto const function = make_builtin(f);
                        as<function_object>(function)->binds = false;
                        sFunctions[f.name] = function;
                    }
                return sFunctions;
            }

            object* codecs_function(char const* aName) { return new_reference(codecs_function_objects()[aName]); }

            std::vector<object*> argument_vector(object* aArgs) { return tuple_items(aArgs); }

            codec_descriptor const& descriptor(void* aData) { return *static_cast<codec_descriptor const*>(aData); }

            std::string errors_value(object* aErrors)
            {
                if (aErrors == nullptr || kind_of(aErrors) == NEOS_PY_NONE)
                    return "strict";
                return text_of(aErrors);
            }

            // (the name of the _codecs function that encodes or decodes with a codec)
            std::string codec_function_name(codec_descriptor const& aCodec, bool aEncode)
            {
                if (aCodec.id == codec_id::Table || aCodec.id == codec_id::Charmap)
                    return aEncode ? "charmap_encode" : "charmap_decode";
                std::string const module = aCodec.module != nullptr ? aCodec.module->module : "utf_8";
                return module + (aEncode ? "_encode" : "_decode");
            }

            bool is_buffered_codec(codec_id aId)
            {
                return aId == codec_id::Utf8 || aId == codec_id::Utf16 || aId == codec_id::Utf16Le || aId == codec_id::Utf16Be || aId == codec_id::Utf32 ||
                    aId == codec_id::Utf32Le || aId == codec_id::Utf32Be || aId == codec_id::Utf7 || aId == codec_id::UnicodeEscape || aId == codec_id::RawUnicodeEscape ||
                    aId == codec_id::Utf8Sig;
            }

            object* native_encode(codec_descriptor const& aCodec, object* aInput, std::string const& aErrors)    // ((bytes, length))
            {
                return engine_encode(aCodec, aInput, codec_function_name(aCodec, true).c_str(), aErrors);
            }

            object* native_decode(codec_descriptor const& aCodec, object* aInput, std::string const& aErrors, bool aFinal)    // ((str, consumed))
            {
                std::string_view data;
                if (!data_argument(aInput, data, aCodec.id == codec_id::UnicodeEscape || aCodec.id == codec_id::RawUnicodeEscape))
                    return nullptr;
                std::string result;
                std::size_t consumed = 0u;
                if (!engine_decode(aCodec, data, aErrors, aFinal, result, consumed))
                    return nullptr;
                return result_pair(make_str(std::move(result)), static_cast<long long>(consumed));
            }

            object* first_of(object* aPair)    // (steals aPair: its first item)
            {
                ref const pair{ aPair };
                if (!pair)
                    return nullptr;
                return new_reference(as<tuple_object>(pair.get())->items[0]);
            }

            // (the codec's module functions encode(input, errors='strict') and decode(input, errors='strict') (final))
            object* module_encode(neos_py_context*, void* aData, object* aArgs, object* aKwargs)
            {
                std::vector<object*> a;
                if (!parse_arguments("encode", argument_vector(aArgs), aKwargs, { "input", "errors" }, 1u, 0u, a))
                    return nullptr;
                return native_encode(descriptor(aData), a[0], errors_value(a[1]));
            }

            object* module_decode(neos_py_context*, void* aData, object* aArgs, object* aKwargs)
            {
                std::vector<object*> a;
                if (!parse_arguments("decode", argument_vector(aArgs), aKwargs, { "input", "errors" }, 1u, 0u, a))
                    return nullptr;
                return native_decode(descriptor(aData), a[0], errors_value(a[1]), true);
            }

            // (Codec.encode(self, input, errors='strict') and decode: a codec's Codec's (and StreamWriter's and StreamReader's) methods)
            object* method_encode(neos_py_context*, void* aData, object* aArgs, object* aKwargs)
            {
                std::vector<object*> a;
                if (!self_arguments("encode", argument_vector(aArgs), aKwargs, { "input", "errors" }, 1u, 0u, a))
                    return nullptr;
                return native_encode(descriptor(aData), a[0], errors_value(a[1]));
            }

            object* method_decode(neos_py_context*, void* aData, object* aArgs, object* aKwargs)
            {
                std::vector<object*> a;
                if (!self_arguments("decode", argument_vector(aArgs), aKwargs, { "input", "errors" }, 1u, 0u, a))
                    return nullptr;
                auto const& codec = descriptor(aData);
                return native_decode(codec, a[0], errors_value(a[1]), !is_buffered_codec(codec.id));
            }

            // (IncrementalEncoder.encode(self, input, final=False))
            object* incremental_encode(neos_py_context*, void* aData, object* aArgs, object* aKwargs)
            {
                std::vector<object*> a;
                auto const args = argument_vector(aArgs);
                if (!self_arguments("encode", args, aKwargs, { "input", "final" }, 1u, 0u, a))
                    return nullptr;
                ref const errors{ attr(args[0], "errors") };
                return errors ? first_of(native_encode(descriptor(aData), a[0], errors_value(errors.get()))) : nullptr;
            }

            // (IncrementalDecoder.decode(self, input, final=False) (an unbuffered codec's))
            object* incremental_decode(neos_py_context*, void* aData, object* aArgs, object* aKwargs)
            {
                std::vector<object*> a;
                auto const args = argument_vector(aArgs);
                if (!self_arguments("decode", args, aKwargs, { "input", "final" }, 1u, 0u, a))
                    return nullptr;
                ref const errors{ attr(args[0], "errors") };
                return errors ? first_of(native_decode(descriptor(aData), a[0], errors_value(errors.get()), true)) : nullptr;
            }

            // (BufferedIncrementalDecoder._buffer_decode(self, input, errors, final))
            object* buffer_decode(neos_py_context*, void* aData, object* aArgs, object* aKwargs)
            {
                std::vector<object*> a;
                auto const args = argument_vector(aArgs);
                if (!self_arguments("_buffer_decode", args, aKwargs, { "input", "errors", "final" }, 3u, 0u, a))
                    return nullptr;
                auto const final = truth(a[2]);
                return final < 0 ? nullptr : native_decode(descriptor(aData), a[0], errors_value(a[1]), final == 1);
            }

            // --- utf-16 and utf-32 (a byte order mark first) and utf-8-sig: their stateful classes

            bool is_wide(codec_descriptor const& aCodec) { return aCodec.id == codec_id::Utf32; }

            char const* endian_function(codec_descriptor const& aCodec, bool aLittle, bool aEncode)
            {
                if (is_wide(aCodec))
                    return aLittle ? (aEncode ? "utf_32_le_encode" : "utf_32_le_decode") : (aEncode ? "utf_32_be_encode" : "utf_32_be_decode");
                return aLittle ? (aEncode ? "utf_16_le_encode" : "utf_16_le_decode") : (aEncode ? "utf_16_be_encode" : "utf_16_be_decode");
            }

            constexpr bool sLittleEndian = true;    // (sys.byteorder: the platforms this runtime builds for)

            object* bom_encoder_init(neos_py_context*, void*, object* aArgs, object* aKwargs)
            {
                auto const args = argument_vector(aArgs);
                ref const r{ errors_init(args, aKwargs, "IncrementalEncoder.__init__", true, false) };
                return r && setattr(args[0], "encoder", none()) >= 0 ? new_reference(none()) : nullptr;
            }

            object* bom_encoder_encode(neos_py_context*, void* aData, object* aArgs, object* aKwargs)
            {
                std::vector<object*> a;
                auto const args = argument_vector(aArgs);
                if (!self_arguments("encode", args, aKwargs, { "input", "final" }, 1u, 0u, a))
                    return nullptr;
                auto const& codec = descriptor(aData);
                auto const self = args[0];
                ref const encoder{ attr(self, "encoder") };
                ref const errors{ encoder ? attr(self, "errors") : nullptr };
                if (!errors)
                    return nullptr;
                if (kind_of(encoder.get()) == NEOS_PY_NONE)
                {
                    ref const result{ native_encode(codec, a[0], errors_value(errors.get())) };
                    ref const next{ codecs_function(endian_function(codec, sLittleEndian, true)) };
                    if (!result || setattr(self, "encoder", next.get()) < 0)
                        return nullptr;
                    return new_reference(as<tuple_object>(result.get())->items[0]);
                }
                return first_of(invoke(encoder.get(), { new_reference(a[0]), new_reference(errors.get()) }));
            }

            object* bom_encoder_reset(neos_py_context*, void*, object* aArgs, object*)
            {
                return setattr(argument_vector(aArgs)[0], "encoder", none()) >= 0 ? new_reference(none()) : nullptr;
            }

            object* bom_encoder_getstate(neos_py_context*, void*, object* aArgs, object*)
            {
                ref const encoder{ attr(argument_vector(aArgs)[0], "encoder") };
                return encoder ? make_int(kind_of(encoder.get()) == NEOS_PY_NONE ? 2 : 0) : nullptr;
            }

            object* bom_encoder_setstate(neos_py_context*, void* aData, object* aArgs, object* aKwargs)
            {
                std::vector<object*> a;
                auto const args = argument_vector(aArgs);
                if (!self_arguments("setstate", args, aKwargs, { "state" }, 1u, 0u, a))
                    return nullptr;
                auto const t = truth(a[0]);
                if (t < 0)
                    return nullptr;
                ref const encoder{ t == 1 ? new_reference(none()) : codecs_function(endian_function(descriptor(aData), sLittleEndian, true)) };
                return setattr(args[0], "encoder", encoder.get()) >= 0 ? new_reference(none()) : nullptr;
            }

            object* bom_decoder_init(neos_py_context*, void*, object* aArgs, object* aKwargs)
            {
                auto const args = argument_vector(aArgs);
                ref const r{ errors_init(args, aKwargs, "IncrementalDecoder.__init__", true, true) };
                return r && setattr(args[0], "decoder", none()) >= 0 ? new_reference(none()) : nullptr;
            }

            // (utf_16_ex_decode(input, errors, 0, final): the decoder the byte order mark selects (else an error if there is data))
            object* bom_decode(codec_descriptor const& aCodec, object* aSelf, object* aInput, object* aErrors, bool aFinal, char const* aDecoderAttribute)
            {
                std::string_view data;
                if (!data_argument(aInput, data))
                    return nullptr;
                std::string result;
                std::size_t consumed = 0u;
                int byteorder = 0;
                if (!engine_decode(aCodec, data, errors_value(aErrors), aFinal, result, consumed, &byteorder))
                    return nullptr;
                std::size_t const width = is_wide(aCodec) ? 4u : 2u;
                if (byteorder != 0)
                {
                    ref const decoder{ codecs_function(endian_function(aCodec, byteorder < 0, false)) };
                    if (setattr(aSelf, aDecoderAttribute, decoder.get()) < 0)
                        return nullptr;
                }
                else if (consumed >= width)
                {
                    ref const e{ make_exception("UnicodeDecodeError", { make_str(is_wide(aCodec) ? "utf-32" : "utf-16"), make_bytes(std::string{ data }), make_int(0),
                        make_int(static_cast<long long>(width)), make_str("Stream does not start with BOM") }) };
                    raise_exception(e.get());
                    return nullptr;
                }
                return result_pair(make_str(std::move(result)), static_cast<long long>(consumed));
            }

            object* bom_decoder_buffer_decode(neos_py_context*, void* aData, object* aArgs, object* aKwargs)
            {
                std::vector<object*> a;
                auto const args = argument_vector(aArgs);
                if (!self_arguments("_buffer_decode", args, aKwargs, { "input", "errors", "final" }, 3u, 0u, a))
                    return nullptr;
                auto const self = args[0];
                ref const decoder{ attr(self, "decoder") };
                if (!decoder)
                    return nullptr;
                auto const final = truth(a[2]);
                if (final < 0)
                    return nullptr;
                if (kind_of(decoder.get()) == NEOS_PY_NONE)
                    return bom_decode(descriptor(aData), self, a[0], a[1], final == 1, "decoder");
                ref const errors{ attr(self, "errors") };
                return errors ? invoke(decoder.get(), { new_reference(a[0]), new_reference(errors.get()), new_reference(a[2]) }) : nullptr;
            }

            object* bom_decoder_reset(neos_py_context*, void*, object* aArgs, object*)
            {
                auto const self = argument_vector(aArgs)[0];
                ref const empty{ make_bytes("") };
                return setattr(self, "buffer", empty.get()) >= 0 && setattr(self, "decoder", none()) >= 0 ? new_reference(none()) : nullptr;
            }

            object* bom_decoder_getstate(neos_py_context*, void* aData, object* aArgs, object*)
            {
                auto const self = argument_vector(aArgs)[0];
                ref const buffer{ attr(self, "buffer") };
                ref const decoder{ buffer ? attr(self, "decoder") : nullptr };
                if (!decoder)
                    return nullptr;
                if (kind_of(decoder.get()) == NEOS_PY_NONE)
                    return make_tuple({ new_reference(buffer.get()), make_int(2) });
                ref const big{ codecs_function(endian_function(descriptor(aData), false, false)) };
                return make_tuple({ new_reference(buffer.get()), make_int((!sLittleEndian) != (decoder.get() == big.get()) ? 1 : 0) });
            }

            object* bom_decoder_setstate(neos_py_context*, void* aData, object* aArgs, object* aKwargs)
            {
                std::vector<object*> a;
                auto const args = argument_vector(aArgs);
                if (!self_arguments("setstate", args, aKwargs, { "state" }, 1u, 0u, a))
                    return nullptr;
                ref const zero{ make_int(0) };
                ref const one{ make_int(1) };
                ref const buffer{ getitem(a[0], zero.get()) };
                ref const state{ buffer ? getitem(a[0], one.get()) : nullptr };
                if (!state || setattr(args[0], "buffer", buffer.get()) < 0)
                    return nullptr;
                ref decoder{ new_reference(none()) };
                if (equal(state.get(), zero.get()) == 1)
                    decoder = ref{ codecs_function(endian_function(descriptor(aData), sLittleEndian, false)) };
                else if (equal(state.get(), one.get()) == 1)
                    decoder = ref{ codecs_function(endian_function(descriptor(aData), !sLittleEndian, false)) };
                return setattr(args[0], "decoder", decoder.get()) >= 0 ? new_reference(none()) : nullptr;
            }

            object* bom_writer_init(neos_py_context*, void*, object* aArgs, object* aKwargs)
            {
                auto const args = argument_vector(aArgs);
                ref const r{ stream_init(args, aKwargs, "StreamWriter.__init__", false) };
                return r && setattr(args[0], "encoder", none()) >= 0 ? new_reference(none()) : nullptr;
            }

            object* bom_writer_encode(neos_py_context*, void* aData, object* aArgs, object* aKwargs)
            {
                std::vector<object*> a;
                auto const args = argument_vector(aArgs);
                if (!self_arguments("encode", args, aKwargs, { "input", "errors" }, 1u, 0u, a))
                    return nullptr;
                auto const& codec = descriptor(aData);
                ref const encoder{ attr(args[0], "encoder") };
                if (!encoder)
                    return nullptr;
                ref const errors{ make_str(errors_value(a[1])) };
                if (kind_of(encoder.get()) == NEOS_PY_NONE)
                {
                    ref const result{ native_encode(codec, a[0], errors_value(a[1])) };
                    ref const next{ codecs_function(endian_function(codec, sLittleEndian, true)) };
                    if (!result || setattr(args[0], "encoder", next.get()) < 0)
                        return nullptr;
                    return new_reference(result.get());
                }
                return invoke(encoder.get(), { new_reference(a[0]), new_reference(errors.get()) });
            }

            object* bom_reader_reset(neos_py_context*, void*, object* aArgs, object*)
            {
                auto const self = argument_vector(aArgs)[0];
                ref const empty{ make_bytes("") };
                ref const chars{ attr(self, "_empty_charbuffer") };
                if (!chars || !set_attributes(self, { { "bytebuffer", empty.get() }, { "charbuffer", chars.get() }, { "linebuffer", none() } }))
                    return nullptr;
                if (ref const d{ make_str("decode") }; d)
                {
                    ref const dict{ getattr(self, "__dict__") };
                    if (dict && dict_find(dict.get(), d.get()) >= 0)
                    {
                        ref const deleted{ method(dict.get(), "pop", { new_reference(d.get()) }) };
                        if (!deleted)
                            return nullptr;
                    }
                    clear_error();
                }
                return new_reference(none());
            }

            object* bom_reader_decode(neos_py_context*, void* aData, object* aArgs, object* aKwargs)
            {
                std::vector<object*> a;
                auto const args = argument_vector(aArgs);
                if (!self_arguments("decode", args, aKwargs, { "input", "errors" }, 1u, 0u, a))
                    return nullptr;
                return bom_decode(descriptor(aData), args[0], a[0], a[1], false, "decode");
            }

            // (utf-8-sig)
            std::string const sBomUtf8 = "\xEF\xBB\xBF";

            object* sig_module_encode(neos_py_context*, void*, object* aArgs, object* aKwargs)
            {
                std::vector<object*> a;
                if (!parse_arguments("encode", argument_vector(aArgs), aKwargs, { "input", "errors" }, 1u, 0u, a))
                    return nullptr;
                return native_encode(*descriptor_of(codec_id::Utf8Sig), a[0], errors_value(a[1]));
            }

            object* sig_decode(object* aInput, std::string const& aErrors, bool aFinal, bool aFirst, bool& aBomSeen)
            {
                std::string_view data;
                if (!data_argument(aInput, data))
                    return nullptr;
                std::size_t prefix = 0u;
                aBomSeen = false;
                if (aFirst)
                {
                    if (data.size() < 3u && std::string_view{ sBomUtf8 }.starts_with(data))
                        return result_pair(make_str(""), 0);
                    aBomSeen = true;
                    if (data.starts_with(sBomUtf8))
                    {
                        data.remove_prefix(3u);
                        prefix = 3u;
                    }
                }
                std::string result;
                std::size_t consumed = 0u;
                if (!decode_utf8(data, aErrors, aFinal, result, consumed))
                    return nullptr;
                return result_pair(make_str(std::move(result)), static_cast<long long>(consumed + prefix));
            }

            object* sig_module_decode(neos_py_context*, void*, object* aArgs, object* aKwargs)
            {
                std::vector<object*> a;
                if (!parse_arguments("decode", argument_vector(aArgs), aKwargs, { "input", "errors" }, 1u, 0u, a))
                    return nullptr;
                std::string_view data;
                if (!data_argument(a[0], data))
                    return nullptr;
                std::size_t prefix = 0u;
                if (data.starts_with(sBomUtf8))
                {
                    data.remove_prefix(3u);
                    prefix = 3u;
                }
                std::string result;
                std::size_t consumed = 0u;
                if (!decode_utf8(data, errors_value(a[1]), true, result, consumed))
                    return nullptr;
                return result_pair(make_str(std::move(result)), static_cast<long long>(consumed + prefix));
            }

            object* sig_encoder_init(neos_py_context*, void*, object* aArgs, object* aKwargs)
            {
                auto const args = argument_vector(aArgs);
                ref const r{ errors_init(args, aKwargs, "IncrementalEncoder.__init__", true, false) };
                ref const one{ make_int(1) };
                return r && setattr(args[0], "first", one.get()) >= 0 ? new_reference(none()) : nullptr;
            }

            object* sig_encoder_encode(neos_py_context*, void*, object* aArgs, object* aKwargs)
            {
                std::vector<object*> a;
                auto const args = argument_vector(aArgs);
                if (!self_arguments("encode", args, aKwargs, { "input", "final" }, 1u, 0u, a))
                    return nullptr;
                auto const first = attribute_truth(args[0], "first");
                ref const errors{ first >= 0 ? attr(args[0], "errors") : nullptr };
                if (!errors)
                    return nullptr;
                ref const result{ first_of(native_encode(*descriptor_of(codec_id::Utf8), a[0], errors_value(errors.get()))) };
                if (!result || first == 0)
                    return result ? new_reference(result.get()) : nullptr;
                ref const zero{ make_int(0) };
                if (setattr(args[0], "first", zero.get()) < 0)
                    return nullptr;
                ref const bom{ make_bytes(sBomUtf8) };
                return binary(NEOS_PY_ADD, bom.get(), result.get(), false);
            }

            object* sig_first_reset(neos_py_context*, void*, object* aArgs, object*)
            {
                auto const self = argument_vector(aArgs)[0];
                ref const one{ make_int(1) };
                return setattr(self, "first", one.get()) >= 0 ? new_reference(none()) : nullptr;
            }

            object* sig_encoder_getstate(neos_py_context*, void*, object* aArgs, object*) { return attr(argument_vector(aArgs)[0], "first"); }

            object* sig_encoder_setstate(neos_py_context*, void*, object* aArgs, object* aKwargs)
            {
                std::vector<object*> a;
                auto const args = argument_vector(aArgs);
                if (!self_arguments("setstate", args, aKwargs, { "state" }, 1u, 0u, a))
                    return nullptr;
                return setattr(args[0], "first", a[0]) >= 0 ? new_reference(none()) : nullptr;
            }

            object* sig_decoder_init(neos_py_context*, void*, object* aArgs, object* aKwargs)
            {
                auto const args = argument_vector(aArgs);
                ref const r{ errors_init(args, aKwargs, "IncrementalDecoder.__init__", true, true) };
                ref const one{ make_int(1) };
                return r && setattr(args[0], "first", one.get()) >= 0 ? new_reference(none()) : nullptr;
            }

            object* sig_decoder_buffer_decode(neos_py_context*, void*, object* aArgs, object* aKwargs)
            {
                std::vector<object*> a;
                auto const args = argument_vector(aArgs);
                if (!self_arguments("_buffer_decode", args, aKwargs, { "input", "errors", "final" }, 3u, 0u, a))
                    return nullptr;
                auto const first = attribute_truth(args[0], "first");
                auto const final = truth(a[2]);
                if (first < 0 || final < 0)
                    return nullptr;
                bool bomSeen = false;
                ref const result{ sig_decode(a[0], errors_value(a[1]), final == 1, first == 1, bomSeen) };
                if (!result)
                    return nullptr;
                if (bomSeen)
                {
                    ref const zero{ make_int(0) };
                    if (setattr(args[0], "first", zero.get()) < 0)
                        return nullptr;
                }
                return new_reference(result.get());
            }

            object* sig_decoder_reset(neos_py_context*, void*, object* aArgs, object*)
            {
                auto const self = argument_vector(aArgs)[0];
                ref const empty{ make_bytes("") };
                ref const one{ make_int(1) };
                return setattr(self, "buffer", empty.get()) >= 0 && setattr(self, "first", one.get()) >= 0 ? new_reference(none()) : nullptr;
            }

            object* sig_decoder_getstate(neos_py_context*, void*, object* aArgs, object*)
            {
                auto const self = argument_vector(aArgs)[0];
                ref const buffer{ attr(self, "buffer") };
                ref const first{ buffer ? attr(self, "first") : nullptr };
                return first ? make_tuple({ new_reference(buffer.get()), new_reference(first.get()) }) : nullptr;
            }

            object* sig_decoder_setstate(neos_py_context*, void*, object* aArgs, object* aKwargs)
            {
                std::vector<object*> a;
                auto const args = argument_vector(aArgs);
                if (!self_arguments("setstate", args, aKwargs, { "state" }, 1u, 0u, a))
                    return nullptr;
                ref const zero{ make_int(0) };
                ref const one{ make_int(1) };
                ref const buffer{ getitem(a[0], zero.get()) };
                ref const first{ buffer ? getitem(a[0], one.get()) : nullptr };
                return first && setattr(args[0], "buffer", buffer.get()) >= 0 && setattr(args[0], "first", first.get()) >= 0 ? new_reference(none()) : nullptr;
            }

            object* sig_writer_encode(neos_py_context*, void*, object* aArgs, object* aKwargs)
            {
                std::vector<object*> a;
                auto const args = argument_vector(aArgs);
                if (!self_arguments("encode", args, aKwargs, { "input", "errors" }, 1u, 0u, a))
                    return nullptr;
                ref const next{ codecs_function("utf_8_encode") };
                if (setattr(args[0], "encode", next.get()) < 0)
                    return nullptr;
                return native_encode(*descriptor_of(codec_id::Utf8Sig), a[0], errors_value(a[1]));
            }

            object* sig_writer_reset(neos_py_context*, void*, object* aArgs, object*)
            {
                auto const self = argument_vector(aArgs)[0];
                ref const dict{ getattr(self, "__dict__") };
                ref const key{ make_str("encode") };
                if (dict && dict_find(dict.get(), key.get()) >= 0)
                {
                    ref const deleted{ method(dict.get(), "pop", { new_reference(key.get()) }) };
                    if (!deleted)
                        return nullptr;
                }
                clear_error();
                return new_reference(none());
            }

            object* sig_reader_decode(neos_py_context*, void*, object* aArgs, object* aKwargs)
            {
                std::vector<object*> a;
                auto const args = argument_vector(aArgs);
                if (!self_arguments("decode", args, aKwargs, { "input", "errors" }, 1u, 0u, a))
                    return nullptr;
                std::string_view data;
                if (!data_argument(a[0], data))
                    return nullptr;
                if (data.size() < 3u && std::string_view{ sBomUtf8 }.starts_with(data))
                    return result_pair(make_str(""), 0);
                ref const next{ codecs_function("utf_8_decode") };
                if (setattr(args[0], "decode", next.get()) < 0)
                    return nullptr;
                bool bomSeen = false;
                return sig_decode(a[0], errors_value(a[1]), false, true, bomSeen);
            }

            // --- the transform codecs: hex, base64 (bytes to bytes) and rot13 (str to str)

            object* transform(codec_descriptor const& aCodec, object* aInput, bool aEncode)
            {
                if (aCodec.id == codec_id::Rot13)
                {
                    std::string_view text;
                    if (!text_argument("translate", aInput, 1u, text))
                        return nullptr;
                    std::string result{ text };
                    for (auto& c : result)
                        if (c >= 'a' && c <= 'z')
                            c = static_cast<char>('a' + (c - 'a' + 13) % 26);
                        else if (c >= 'A' && c <= 'Z')
                            c = static_cast<char>('A' + (c - 'A' + 13) % 26);
                    return make_str(std::move(result));
                }
                char const* const module = aCodec.id == codec_id::Hex ? "binascii" : aCodec.id == codec_id::Zlib ? "zlib" : "base64";
                char const* const function = aCodec.id == codec_id::Hex ? (aEncode ? "b2a_hex" : "a2b_hex") : aCodec.id == codec_id::Zlib ?
                    (aEncode ? "compress" : "decompress") : (aEncode ? "encodebytes" : "decodebytes");
                ref const f{ module_attribute(module, function) };
                return f ? invoke(f.get(), { new_reference(aInput) }) : nullptr;
            }

            bool strict_assertion(object* aErrors)
            {
                if (aErrors == nullptr || (kind_of(aErrors) == NEOS_PY_STR && as<str_object>(aErrors)->value == "strict"))
                    return true;
                return (error("AssertionError", ""), false);
            }

            object* transform_module(neos_py_context*, void* aData, object* aArgs, object* aKwargs, bool aEncode, bool aMethod)
            {
                std::vector<object*> a;
                auto const args = argument_vector(aArgs);
                if (!(aMethod ? self_arguments(aEncode ? "encode" : "decode", args, aKwargs, { "input", "errors" }, 1u, 0u, a) :
                    parse_arguments(aEncode ? "encode" : "decode", args, aKwargs, { "input", "errors" }, 1u, 0u, a)))
                    return nullptr;
                auto const& codec = descriptor(aData);
                if (codec.id != codec_id::Rot13 && !strict_assertion(a[1]))
                    return nullptr;
                ref const result{ transform(codec, a[0], aEncode) };
                auto const n = length(a[0]);
                return result && n >= 0 ? make_tuple({ new_reference(result.get()), make_int(n) }) : nullptr;
            }

            object* transform_incremental(neos_py_context*, void* aData, object* aArgs, object* aKwargs, bool aEncode)
            {
                std::vector<object*> a;
                auto const args = argument_vector(aArgs);
                if (!self_arguments(aEncode ? "encode" : "decode", args, aKwargs, { "input", "final" }, 1u, 0u, a))
                    return nullptr;
                auto const& codec = descriptor(aData);
                if (codec.id != codec_id::Rot13)
                {
                    ref const errors{ attr(args[0], "errors") };
                    if (!errors || !strict_assertion(errors.get()))
                        return nullptr;
                }
                return transform(codec, a[0], aEncode);
            }

            // (zlib_codec's incremental encoder and decoder: a zlib.compressobj or decompressobj)
            template <bool Encode>
            object* zlib_coder_init(neos_py_context*, void*, object* aArgs, object* aKwargs)
            {
                std::vector<object*> a;
                auto const args = argument_vector(aArgs);
                if (!self_arguments("__init__", args, aKwargs, { "errors" }, 0u, 0u, a) || !strict_assertion(a[0]))
                    return nullptr;
                ref const errors{ a[0] != nullptr ? new_reference(a[0]) : make_str("strict") };
                ref const factory{ module_attribute("zlib", Encode ? "compressobj" : "decompressobj") };
                ref const coder{ factory ? call(factory.get(), nullptr, nullptr) : nullptr };
                if (!coder || setattr(args[0], "errors", errors.get()) < 0 || setattr(args[0], Encode ? "compressobj" : "decompressobj", coder.get()) < 0)
                    return nullptr;
                return new_reference(none());
            }

            template <bool Encode>
            object* zlib_coder_code(neos_py_context*, void*, object* aArgs, object* aKwargs)
            {
                std::vector<object*> a;
                auto const args = argument_vector(aArgs);
                if (!self_arguments(Encode ? "encode" : "decode", args, aKwargs, { "input", "final" }, 1u, 0u, a))
                    return nullptr;
                ref const coder{ attr(args[0], Encode ? "compressobj" : "decompressobj") };
                if (!coder)
                    return nullptr;
                ref const output{ method(coder.get(), Encode ? "compress" : "decompress", { new_reference(a[0]) }) };
                auto const final = a[1] != nullptr ? truth(a[1]) : 0;
                if (!output || final < 0)
                    return nullptr;
                if (final == 0)
                    return new_reference(output.get());
                ref const rest{ method(coder.get(), "flush", {}) };
                return rest ? binary(NEOS_PY_ADD, output.get(), rest.get(), false) : nullptr;
            }

            template <bool Encode>
            object* zlib_coder_reset(neos_py_context*, void*, object* aArgs, object*)
            {
                ref const factory{ module_attribute("zlib", Encode ? "compressobj" : "decompressobj") };
                ref const coder{ factory ? call(factory.get(), nullptr, nullptr) : nullptr };
                return coder && setattr(argument_vector(aArgs)[0], Encode ? "compressobj" : "decompressobj", coder.get()) >= 0 ? new_reference(none()) : nullptr;
            }

            // --- the codecs' CodecInfo (encodings.search_function's results)

            object* codec_class(std::string const& aModule, char const* aName, std::vector<object*> const& aBases,
                std::vector<std::pair<char const*, native_function>> const& aMethods, codec_descriptor const* aCodec, std::vector<std::pair<std::string, object*>> aExtra = {})
            {
                for (auto const& m : aMethods)
                    aExtra.emplace_back(m.first, make_function(m.first, m.second, const_cast<codec_descriptor*>(aCodec)));
                auto const result = make_class(aModule, aName, aBases, aExtra);
                for (auto& e : aExtra)
                    release(e.second);
                return result;
            }

            object* make_codec_info(codec_entry const& aEntry)
            {
                auto* const codec = new codec_descriptor{ aEntry.id, aEntry.table, aEntry.module };     // (it lives as long as the program)
                std::string const module = std::string{ "encodings." } + aEntry.module->module;
                ref encode, decode, incrementalEncoder, incrementalDecoder, streamWriter, streamReader;
                bool isText = true;
                auto const native = [&](char const* aName, native_function aFunction) { return ref{ make_function(aName, aFunction, codec) }; };
                switch (aEntry.id)
                {
                case codec_id::Utf16:
                case codec_id::Utf32:
                    encode = native("encode", module_encode);
                    decode = native("decode", module_decode);
                    incrementalEncoder = ref{ codec_class(module, "IncrementalEncoder", { sIncrementalEncoder }, { { "__init__", bom_encoder_init }, { "encode", bom_encoder_encode },
                        { "reset", bom_encoder_reset }, { "getstate", bom_encoder_getstate }, { "setstate", bom_encoder_setstate } }, codec) };
                    incrementalDecoder = ref{ codec_class(module, "IncrementalDecoder", { sBufferedIncrementalDecoder }, { { "__init__", bom_decoder_init },
                        { "_buffer_decode", bom_decoder_buffer_decode }, { "reset", bom_decoder_reset }, { "getstate", bom_decoder_getstate }, { "setstate", bom_decoder_setstate } }, codec) };
                    streamWriter = ref{ codec_class(module, "StreamWriter", { sStreamWriter }, { { "__init__", bom_writer_init }, { "reset", bom_encoder_reset },
                        { "encode", bom_writer_encode } }, codec) };
                    streamReader = ref{ codec_class(module, "StreamReader", { sStreamReader }, { { "reset", bom_reader_reset }, { "decode", bom_reader_decode } }, codec) };
                    break;
                case codec_id::Utf8Sig:
                    encode = native("encode", sig_module_encode);
                    decode = native("decode", sig_module_decode);
                    incrementalEncoder = ref{ codec_class(module, "IncrementalEncoder", { sIncrementalEncoder }, { { "__init__", sig_encoder_init }, { "encode", sig_encoder_encode },
                        { "reset", sig_first_reset }, { "getstate", sig_encoder_getstate }, { "setstate", sig_encoder_setstate } }, codec) };
                    incrementalDecoder = ref{ codec_class(module, "IncrementalDecoder", { sBufferedIncrementalDecoder }, { { "__init__", sig_decoder_init },
                        { "_buffer_decode", sig_decoder_buffer_decode }, { "reset", sig_decoder_reset }, { "getstate", sig_decoder_getstate }, { "setstate", sig_decoder_setstate } }, codec) };
                    streamWriter = ref{ codec_class(module, "StreamWriter", { sStreamWriter }, { { "reset", sig_writer_reset }, { "encode", sig_writer_encode } }, codec) };
                    streamReader = ref{ codec_class(module, "StreamReader", { sStreamReader }, { { "reset", bom_reader_reset }, { "decode", sig_reader_decode } }, codec) };
                    break;
                case codec_id::Hex:
                case codec_id::Base64:
                case codec_id::Rot13:
                case codec_id::Zlib:
                    {
                        isText = false;
                        encode = native("encode", [](neos_py_context* c, void* d, object* a, object* k) -> object* { return transform_module(c, d, a, k, true, false); });
                        decode = native("decode", [](neos_py_context* c, void* d, object* a, object* k) -> object* { return transform_module(c, d, a, k, false, false); });
                        ref const codecClass{ codec_class(module, "Codec", { sCodec }, {
                            { "encode", [](neos_py_context* c, void* d, object* a, object* k) -> object* { return transform_module(c, d, a, k, true, true); } },
                            { "decode", [](neos_py_context* c, void* d, object* a, object* k) -> object* { return transform_module(c, d, a, k, false, true); } } }, codec) };
                        if (aEntry.id == codec_id::Zlib)
                        {
                            incrementalEncoder = ref{ codec_class(module, "IncrementalEncoder", { sIncrementalEncoder }, { { "__init__", zlib_coder_init<true> },
                                { "encode", zlib_coder_code<true> }, { "reset", zlib_coder_reset<true> } }, codec) };
                            incrementalDecoder = ref{ codec_class(module, "IncrementalDecoder", { sIncrementalDecoder }, { { "__init__", zlib_coder_init<false> },
                                { "decode", zlib_coder_code<false> }, { "reset", zlib_coder_reset<false> } }, codec) };
                        }
                        else
                        {
                            incrementalEncoder = ref{ codec_class(module, "IncrementalEncoder", { sIncrementalEncoder }, {
                                { "encode", [](neos_py_context* c, void* d, object* a, object* k) -> object* { return transform_incremental(c, d, a, k, true); } } }, codec) };
                            incrementalDecoder = ref{ codec_class(module, "IncrementalDecoder", { sIncrementalDecoder }, {
                                { "decode", [](neos_py_context* c, void* d, object* a, object* k) -> object* { return transform_incremental(c, d, a, k, false); } } }, codec) };
                        }
                        std::vector<std::pair<std::string, object*>> extra;
                        if (aEntry.id != codec_id::Rot13)
                            extra.emplace_back("charbuffertype", type_object_for("bytes", "bytes"));
                        streamWriter = ref{ codec_class(module, "StreamWriter", { codecClass.get(), sStreamWriter }, {}, codec, extra) };
                        for (auto& e : extra)
                            retain(e.second);
                        streamReader = ref{ codec_class(module, "StreamReader", { codecClass.get(), sStreamReader }, {}, codec, extra) };
                    }
                    break;
                default:
                    {
                        // (the stateless codecs: _codecs's functions, or (single-byte tables) a Codec's methods)
                        auto const encodeName = codec_function_name(*codec, true);
                        auto const decodeName = codec_function_name(*codec, false);
                        bool const direct = aEntry.id == codec_id::Ascii || aEntry.id == codec_id::Latin1 || aEntry.id == codec_id::Charmap ||
                            aEntry.id == codec_id::UnicodeEscape || aEntry.id == codec_id::RawUnicodeEscape;
                        if (aEntry.id == codec_id::Table || aEntry.id == codec_id::Undefined)
                            encode = native("encode", module_encode);
                        else
                            encode = ref{ codecs_function(encodeName.c_str()) };
                        if (direct)
                            decode = ref{ codecs_function(decodeName.c_str()) };
                        else
                            decode = native("decode", module_decode);
                        incrementalEncoder = ref{ codec_class(module, "IncrementalEncoder", { sIncrementalEncoder }, { { "encode", incremental_encode } }, codec) };
                        if (is_buffered_codec(aEntry.id))
                            incrementalDecoder = ref{ codec_class(module, "IncrementalDecoder", { sBufferedIncrementalDecoder }, { { "_buffer_decode", buffer_decode } }, codec) };
                        else
                            incrementalDecoder = ref{ codec_class(module, "IncrementalDecoder", { sIncrementalDecoder }, { { "decode", incremental_decode } }, codec) };
                        streamWriter = ref{ codec_class(module, "StreamWriter", { sStreamWriter }, { { "encode", method_encode } }, codec) };
                        streamReader = ref{ codec_class(module, "StreamReader", { sStreamReader }, { { "decode", method_decode } }, codec) };
                    }
                    break;
                }
                if (!encode || !decode || !incrementalEncoder || !incrementalDecoder || !streamWriter || !streamReader)
                    return nullptr;
                ref const name{ make_str(aEntry.module->name) };
                ref const text{ make_bool(isText) };
                ref const kw{ keywords({ { "encode", encode.get() }, { "decode", decode.get() }, { "incrementalencoder", incrementalEncoder.get() },
                    { "incrementaldecoder", incrementalDecoder.get() }, { "streamreader", streamReader.get() }, { "streamwriter", streamWriter.get() }, { "name", name.get() } }) };
                if (!isText)
                    dict_set(kw.get(), ref{ make_str("_is_text_encoding") }.get(), text.get());
                ref const args{ make_tuple() };
                return call(sCodecInfo, args.get(), kw.get());
            }

            object* builtin_search(std::string const& aNormalized)
            {
                auto const entry = find_codec(normalized_encoding(aNormalized));
                if (entry.id == codec_id::Unsupported)
                    return new_reference(none());
                static std::map<std::string, object*> sInfos;
                auto& info = sInfos[entry.module->module];
                if (info == nullptr)
                    info = make_codec_info(entry);
                return info != nullptr ? new_reference(info) : nullptr;
            }

            // --- codecs.py's functions

            object* lookup_attribute(object* aEncoding, char const* aName, bool aRequired)
            {
                std::string_view encoding;
                if (kind_of(aEncoding) != NEOS_PY_STR)
                    return error("TypeError", std::string{ "lookup() argument must be str, not " } + type_name(aEncoding));
                encoding = as<str_object>(aEncoding)->value;
                ref const info{ registry_lookup(encoding) };
                ref const value{ info ? getattr(info.get(), aName) : nullptr };
                if (!value)
                    return nullptr;
                if (aRequired && kind_of(value.get()) == NEOS_PY_NONE)
                {
                    ref const e{ make_exception("LookupError", { new_reference(aEncoding) }) };
                    raise_exception(e.get());
                    return nullptr;
                }
                return new_reference(value.get());
            }

            object* iter_coded(std::vector<object*> const& aArgs, object* aKwargs, bool aEncode)
            {
                if (aArgs.size() < 2u)
                    return error("TypeError", std::string{ aEncode ? "iterencode" : "iterdecode" } + "() missing required arguments: 'iterator' and 'encoding'");
                ref const factory{ lookup_attribute(aArgs[1], aEncode ? "incrementalencoder" : "incrementaldecoder", true) };
                if (!factory)
                    return nullptr;
                ref kw{ aKwargs != nullptr ? b_dict({ aKwargs }, nullptr) : make_dict() };
                ref const errors{ aArgs.size() > 2u ? new_reference(aArgs[2]) : make_str("strict") };
                ref const args{ make_tuple({ new_reference(errors.get()) }) };
                ref const coder{ call(factory.get(), args.get(), kw.get()) };
                if (!coder)
                    return nullptr;
                std::vector<object*> inputs;
                if (!collect(aArgs[0], inputs))
                    return nullptr;
                std::vector<object*> outputs;
                bool ok = true;
                auto const add = [&](object* aOutput)
                {
                    if (aOutput == nullptr)
                        return false;
                    auto const t = truth(aOutput);
                    if (t == 1)
                        outputs.push_back(aOutput);
                    else
                        release(aOutput);
                    return t >= 0;
                };
                for (auto i : inputs)
                {
                    if (ok)
                        ok = add(method(coder.get(), aEncode ? "encode" : "decode", { new_reference(i) }));
                    release(i);
                }
                if (ok)
                    ok = add(method(coder.get(), aEncode ? "encode" : "decode", { aEncode ? make_str("") : make_bytes(""), make_bool(true) }));
                ref const list{ make_list(std::move(outputs)) };
                return ok ? iter(list.get()) : nullptr;
            }

            builtin_entry const sCodecsPyFunctions[] =
            {
                { "getencoder", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    { std::vector<object*> a; return parse_arguments("getencoder", aArgs, aKwargs, { "encoding" }, 1u, 0u, a) ? lookup_attribute(a[0], "encode", false) : nullptr; } },
                { "getdecoder", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    { std::vector<object*> a; return parse_arguments("getdecoder", aArgs, aKwargs, { "encoding" }, 1u, 0u, a) ? lookup_attribute(a[0], "decode", false) : nullptr; } },
                { "getincrementalencoder", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    { std::vector<object*> a; return parse_arguments("getincrementalencoder", aArgs, aKwargs, { "encoding" }, 1u, 0u, a) ? lookup_attribute(a[0], "incrementalencoder", true) : nullptr; } },
                { "getincrementaldecoder", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    { std::vector<object*> a; return parse_arguments("getincrementaldecoder", aArgs, aKwargs, { "encoding" }, 1u, 0u, a) ? lookup_attribute(a[0], "incrementaldecoder", true) : nullptr; } },
                { "getreader", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    { std::vector<object*> a; return parse_arguments("getreader", aArgs, aKwargs, { "encoding" }, 1u, 0u, a) ? lookup_attribute(a[0], "streamreader", false) : nullptr; } },
                { "getwriter", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    { std::vector<object*> a; return parse_arguments("getwriter", aArgs, aKwargs, { "encoding" }, 1u, 0u, a) ? lookup_attribute(a[0], "streamwriter", false) : nullptr; } },
                { "iterencode", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return iter_coded(aArgs, aKwargs, true); } },
                { "iterdecode", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return iter_coded(aArgs, aKwargs, false); } },
                { "open", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("open", aArgs, aKwargs, { "filename", "mode", "encoding", "errors", "buffering" }, 1u, 0u, a))
                            return nullptr;
                        ref mode{ a[1] != nullptr ? new_reference(a[1]) : make_str("r") };
                        bool const hasEncoding = a[2] != nullptr && kind_of(a[2]) != NEOS_PY_NONE;
                        if (hasEncoding && kind_of(mode.get()) == NEOS_PY_STR && as<str_object>(mode.get())->value.find('b') == std::string::npos)
                            mode = ref{ make_str(as<str_object>(mode.get())->value + "b") };
                        ref const buffering{ a[4] != nullptr ? new_reference(a[4]) : make_int(-1) };
                        ref const file{ b_open({ a[0], mode.get(), buffering.get() }, nullptr) };
                        if (!file || !hasEncoding)
                            return file ? new_reference(file.get()) : nullptr;
                        ref const reader{ lookup_attribute(a[2], "streamreader", false) };
                        ref const writer{ reader ? lookup_attribute(a[2], "streamwriter", false) : nullptr };
                        ref const errors{ a[3] != nullptr ? new_reference(a[3]) : make_str("strict") };
                        ref const srw{ writer ? invoke(sStreamReaderWriter, { new_reference(file.get()), new_reference(reader.get()), new_reference(writer.get()), new_reference(errors.get()) }) : nullptr };
                        if (!srw || setattr(srw.get(), "encoding", a[2]) < 0)
                        {
                            ref const e{ take_exception() };
                            ref const closed{ method(file.get(), "close", {}) };
                            clear_error();
                            raise_exception(e.get());
                            return nullptr;
                        }
                        return new_reference(srw.get());
                    } },
                { "EncodedFile", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("EncodedFile", aArgs, aKwargs, { "file", "data_encoding", "file_encoding", "errors" }, 2u, 0u, a))
                            return nullptr;
                        auto const fileEncoding = a[2] != nullptr && kind_of(a[2]) != NEOS_PY_NONE ? a[2] : a[1];
                        ref const dataEncode{ lookup_attribute(a[1], "encode", false) };
                        ref const dataDecode{ dataEncode ? lookup_attribute(a[1], "decode", false) : nullptr };
                        ref const reader{ dataDecode ? lookup_attribute(fileEncoding, "streamreader", false) : nullptr };
                        ref const writer{ reader ? lookup_attribute(fileEncoding, "streamwriter", false) : nullptr };
                        ref const errors{ a[3] != nullptr ? new_reference(a[3]) : make_str("strict") };
                        ref const sr{ writer ? invoke(sStreamRecoder, { new_reference(a[0]), new_reference(dataEncode.get()), new_reference(dataDecode.get()),
                            new_reference(reader.get()), new_reference(writer.get()), new_reference(errors.get()) }) : nullptr };
                        if (!sr || !set_attributes(sr.get(), { { "data_encoding", a[1] }, { "file_encoding", fileEncoding } }))
                            return nullptr;
                        return new_reference(sr.get());
                    } },
                { "make_identity_dict", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("make_identity_dict", aArgs, aKwargs, { "rng" }, 1u, 0u, a))
                            return nullptr;
                        std::vector<object*> items;
                        if (!collect(a[0], items))
                            return nullptr;
                        ref const result{ make_dict() };
                        for (auto i : items)
                        {
                            dict_set(result.get(), i, i);
                            release(i);
                        }
                        return new_reference(result.get());
                    } },
                { "make_encoding_map", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("make_encoding_map", aArgs, aKwargs, { "decoding_map" }, 1u, 0u, a))
                            return nullptr;
                        ref const items{ method(a[0], "items", {}) };
                        std::vector<object*> pairs;
                        if (!items || !collect(items.get(), pairs))
                            return nullptr;
                        ref const result{ make_dict() };
                        bool ok = true;
                        for (auto p : pairs)
                        {
                            if (ok)
                            {
                                ref k, v;
                                ok = unpack_pair(new_reference(p), nullptr, k, v);
                                if (ok)
                                {
                                    auto const present = dict_find(result.get(), v.get());
                                    ok = present != -2 && dict_set(result.get(), v.get(), present >= 0 ? none() : k.get()) >= 0;
                                }
                            }
                            release(p);
                        }
                        return ok ? new_reference(result.get()) : nullptr;
                    } }
            };

            // --- the modules

            bool make_codecs_classes()
            {
                if (sCodecInfo != nullptr)
                    return true;
                init_error_handlers();
                ref const tuple{ type_object_for("tuple", "tuple") };
                sCodecInfo = make_class_of("codecs", "CodecInfo", { tuple.get() }, sCodecInfoMethods, { { "_is_text_encoding", make_bool(true) } });
                sCodec = make_class_of("codecs", "Codec", {}, sCodecMethods);
                sIncrementalEncoder = make_class_of("codecs", "IncrementalEncoder", {}, sIncrementalEncoderMethods);
                if (!sCodecInfo || !sCodec || !sIncrementalEncoder)
                    return false;
                sBufferedIncrementalEncoder = make_class_of("codecs", "BufferedIncrementalEncoder", { sIncrementalEncoder }, sBufferedIncrementalEncoderMethods);
                sIncrementalDecoder = make_class_of("codecs", "IncrementalDecoder", {}, sIncrementalDecoderMethods);
                if (!sBufferedIncrementalEncoder || !sIncrementalDecoder)
                    return false;
                sBufferedIncrementalDecoder = make_class_of("codecs", "BufferedIncrementalDecoder", { sIncrementalDecoder }, sBufferedIncrementalDecoderMethods);
                sStreamWriter = make_class_of("codecs", "StreamWriter", { sCodec }, sStreamWriterMethods);
                sStreamReader = make_class_of("codecs", "StreamReader", { sCodec }, sStreamReaderMethods, { { "charbuffertype", type_object_for("str", "str") } });
                sStreamReaderWriter = make_class_of("codecs", "StreamReaderWriter", {}, sStreamReaderWriterMethods, { { "encoding", make_str("unknown") } });
                sStreamRecoder = make_class_of("codecs", "StreamRecoder", {}, sStreamRecoderMethods, { { "data_encoding", make_str("unknown") }, { "file_encoding", make_str("unknown") } });
                sEncodingMapClass = make_class_of("builtins", "EncodingMap", {}, sEncodingMapMethods);
                return sBufferedIncrementalDecoder && sStreamWriter && sStreamReader && sStreamReaderWriter && sStreamRecoder && sEncodingMapClass;
            }

            void add_codecs_functions(std::vector<std::pair<std::string, object*>>& aAttributes)
            {
                for (auto const& [name, function] : codecs_function_objects())
                    aAttributes.emplace_back(name, new_reference(function));
            }

            bool _codecs_module(std::vector<std::pair<std::string, object*>>& aAttributes)
            {
                if (!make_codecs_classes())
                    return false;
                add_codecs_functions(aAttributes);
                return true;
            }

            bool codecs_module(std::vector<std::pair<std::string, object*>>& aAttributes)
            {
                if (!make_codecs_classes())
                    return false;
                add_codecs_functions(aAttributes);
                for (auto const& f : sCodecsPyFunctions)
                    aAttributes.emplace_back(f.name, make_builtin(f));
                for (auto const& [name, value] : { std::pair{ "CodecInfo", sCodecInfo }, std::pair{ "Codec", sCodec }, std::pair{ "IncrementalEncoder", sIncrementalEncoder },
                    std::pair{ "BufferedIncrementalEncoder", sBufferedIncrementalEncoder }, std::pair{ "IncrementalDecoder", sIncrementalDecoder },
                    std::pair{ "BufferedIncrementalDecoder", sBufferedIncrementalDecoder }, std::pair{ "StreamWriter", sStreamWriter },
                    std::pair{ "StreamReader", sStreamReader }, std::pair{ "StreamReaderWriter", sStreamReaderWriter }, std::pair{ "StreamRecoder", sStreamRecoder } })
                    aAttributes.emplace_back(name, new_reference(value));
                for (auto const& [name, handler] : { std::pair{ "strict_errors", "strict" }, std::pair{ "ignore_errors", "ignore" }, std::pair{ "replace_errors", "replace" },
                    std::pair{ "xmlcharrefreplace_errors", "xmlcharrefreplace" }, std::pair{ "backslashreplace_errors", "backslashreplace" },
                    std::pair{ "namereplace_errors", "namereplace" } })
                    aAttributes.emplace_back(name, new_reference(builtin_error_handlers()[handler]));
                std::string const utf16le = "\xFF\xFE", utf16be = "\xFE\xFF";
                std::string const utf32le{ "\xFF\xFE\x00\x00", 4u }, utf32be{ "\x00\x00\xFE\xFF", 4u };
                for (auto const& [name, value] : { std::pair{ "BOM_UTF8", sBomUtf8 }, std::pair{ "BOM_LE", utf16le }, std::pair{ "BOM_UTF16_LE", utf16le },
                    std::pair{ "BOM_BE", utf16be }, std::pair{ "BOM_UTF16_BE", utf16be }, std::pair{ "BOM_UTF32_LE", utf32le }, std::pair{ "BOM_UTF32_BE", utf32be },
                    std::pair{ "BOM", utf16le }, std::pair{ "BOM_UTF16", utf16le }, std::pair{ "BOM_UTF32", utf32le }, std::pair{ "BOM32_LE", utf16le },
                    std::pair{ "BOM32_BE", utf16be }, std::pair{ "BOM64_LE", utf32le }, std::pair{ "BOM64_BE", utf32be } })
                    aAttributes.emplace_back(name, make_bytes(value));
                std::vector<object*> all;
                for (auto n : { "register", "lookup", "open", "EncodedFile", "BOM", "BOM_BE", "BOM_LE", "BOM32_BE", "BOM32_LE", "BOM64_BE", "BOM64_LE", "BOM_UTF8",
                    "BOM_UTF16", "BOM_UTF16_LE", "BOM_UTF16_BE", "BOM_UTF32", "BOM_UTF32_LE", "BOM_UTF32_BE", "CodecInfo", "Codec", "IncrementalEncoder",
                    "IncrementalDecoder", "StreamReader", "StreamWriter", "StreamReaderWriter", "StreamRecoder", "getencoder", "getdecoder", "getincrementalencoder",
                    "getincrementaldecoder", "getreader", "getwriter", "encode", "decode", "iterencode", "iterdecode", "strict_errors", "ignore_errors", "replace_errors",
                    "xmlcharrefreplace_errors", "backslashreplace_errors", "namereplace_errors", "register_error", "lookup_error" })
                    all.push_back(make_str(n));
                aAttributes.emplace_back("__all__", make_list(std::move(all)));
                return true;
            }

            builtin_entry const sEncodingsFunctions[] =
            {
                { "normalize_encoding", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("normalize_encoding", aArgs, aKwargs, { "encoding" }, 1u, 0u, a))
                            return nullptr;
                        std::string text;
                        std::string_view data;
                        if (kind_of(a[0]) == NEOS_PY_STR)
                            text = as<str_object>(a[0])->value;
                        else if (bytes_like(a[0], data))
                            text = std::string{ data };
                        else
                            return error("TypeError", std::string{ "normalize_encoding() argument must be str or bytes, not " } + type_name(a[0]));
                        // (encodings.normalize_encoding keeps the case)
                        std::string result;
                        bool punct = false;
                        for (auto c : text)
                        {
                            auto const u = static_cast<unsigned char>(c);
                            if (u < 0x80u && (std::isalnum(u) || c == '.'))
                            {
                                if (punct && !result.empty())
                                    result += '_';
                                result += c;
                                punct = false;
                            }
                            else
                                punct = true;
                        }
                        return make_str(result);
                    } },
                { "search_function", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        std::string_view encoding;
                        if (!parse_arguments("search_function", aArgs, aKwargs, { "encoding" }, 1u, 0u, a) || !text_argument("search_function", a[0], 1u, encoding))
                            return nullptr;
                        if (!make_codecs_classes())
                            return nullptr;
                        return builtin_search(normalized_encoding(encoding));
                    } }
            };

            bool encodings_module(std::vector<std::pair<std::string, object*>>& aAttributes)
            {
                for (auto const& f : sEncodingsFunctions)
                    aAttributes.emplace_back(f.name, make_builtin(f));
                ref const registryError{ module_exception("encodings.CodecRegistryError", { "LookupError", "SystemError" }) };
                if (!registryError)
                    return false;
                aAttributes.emplace_back("CodecRegistryError", new_reference(registryError.get()));
                ref const aliases{ fn_import("encodings.aliases") };
                if (!aliases)
                    return false;
                aAttributes.emplace_back("aliases", new_reference(aliases.get()));
                return true;
            }

            bool encodings_aliases_module(std::vector<std::pair<std::string, object*>>& aAttributes)
            {
                ref const aliases{ make_dict() };
                for (auto const& a : ct::sAliases)
                {
                    ref const key{ make_str(a.alias) };
                    ref const value{ make_str(a.module) };
                    dict_set(aliases.get(), key.get(), value.get());
                }
                aAttributes.emplace_back("aliases", new_reference(aliases.get()));
                return true;
            }

            inline bool const sCodecsRegistered = register_module("_codecs", _codecs_module) && register_module("codecs", codecs_module) &&
                register_module("encodings", encodings_module) && register_module("encodings.aliases", encodings_aliases_module);
        }
    }
}
