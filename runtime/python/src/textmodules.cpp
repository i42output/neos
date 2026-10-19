/*
  textmodules.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/string.py (string.Template's pattern)), Copyright (c) 2001-2024
  Python Software Foundation; All Rights Reserved. They are used under the Python Software Foundation License Version 2
  (see runtime/python/LICENSE.PSF). Changes: translated from Python to C++ for the neos Python runtime plugin and adapted
  to its object model (see runtime/python/NOTICE).
*/

// The modules string, struct, binascii and base64 (CPython 3.13's behaviour and messages)

#include <algorithm>
#include <bit>
#include <cctype>
#include <cmath>
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
            object* borrowed(object* aObject) { retain(aObject); return aObject; }

            object* call_with(object* aCallable, std::vector<object*> aArgs, object* aKwargs = nullptr)    // (steals the arguments)
            {
                ref const args{ make_tuple(std::move(aArgs)) };
                return call(aCallable, args.get(), aKwargs);
            }

            // --- string

            std::string attribute_string(object* aObject, char const* aName, std::string const& aDefault)
            {
                ref const value{ getattr(aObject, aName) };
                if (!value || kind_of(value.get()) != NEOS_PY_STR)
                {
                    clear_error();
                    return aDefault;
                }
                return as<str_object>(value.get())->value;
            }

            bool identifier_start(char c) { return std::isalpha(static_cast<unsigned char>(c)) != 0 || c == '_'; }
            bool identifier_character(char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_'; }

            // (Template.substitute and safe_substitute)
            object* template_substitute(std::vector<object*> const& aArgs, object* aKwargs, bool aSafe)
            {
                if (aArgs.empty())
                    return error("TypeError", "substitute() missing 1 required positional argument: 'self'");
                if (aArgs.size() > 2u)
                    return error("TypeError", "Too many positional arguments");
                auto const self = aArgs[0];
                ref mapping{ make_dict() };
                if (aArgs.size() == 2u && !dict_update(mapping.get(), aArgs[1]))
                    return nullptr;
                if (aKwargs != nullptr && !dict_update(mapping.get(), aKwargs))
                    return nullptr;
                auto const text = attribute_string(self, "template", "");
                auto const delimiter = attribute_string(self, "delimiter", "$");
                std::string result;
                std::size_t i = 0u;
                while (i < text.size())
                {
                    if (text.compare(i, delimiter.size(), delimiter) != 0)
                    {
                        result += text[i++];
                        continue;
                    }
                    auto const start = i;
                    i += delimiter.size();
                    // ($$, $name, ${name})
                    if (text.compare(i, delimiter.size(), delimiter) == 0)
                    {
                        result += delimiter;
                        i += delimiter.size();
                        continue;
                    }
                    std::string name;
                    bool braced = false;
                    if (i < text.size() && text[i] == '{')
                    {
                        auto const close = text.find('}', i);
                        auto const candidate = close == std::string::npos ? std::string{} : text.substr(i + 1u, close - i - 1u);
                        if (!candidate.empty() && identifier_start(candidate[0]) && std::all_of(candidate.begin(), candidate.end(), identifier_character))
                        {
                            name = candidate;
                            braced = true;
                            i = close + 1u;
                        }
                    }
                    else if (i < text.size() && identifier_start(text[i]))
                    {
                        auto j = i;
                        while (j < text.size() && identifier_character(text[j]))
                            ++j;
                        name = text.substr(i, j - i);
                        i = j;
                    }
                    if (name.empty())
                    {
                        if (aSafe)
                        {
                            result += delimiter;
                            continue;
                        }
                        // (an invalid placeholder: its line and column)
                        std::size_t line = 1u, column = 1u;
                        for (std::size_t k = 0u; k < start; ++k)
                        {
                            if (text[k] == '\n')
                            {
                                ++line;
                                column = 1u;
                            }
                            else
                                ++column;
                        }
                        return error("ValueError", "Invalid placeholder in string: line " + std::to_string(line) + ", col " + std::to_string(column));
                    }
                    ref const key{ make_str(name) };
                    auto const position = dict_find(mapping.get(), key.get());
                    if (position < 0)
                    {
                        if (aSafe)
                        {
                            result += text.substr(start, i - start);
                            continue;
                        }
                        raise_key_error(key.get());
                        return nullptr;
                    }
                    std::string value;
                    if (!str_of(as<dict_object>(mapping.get())->items[static_cast<std::size_t>(position)].second, value))
                        return nullptr;
                    result += value;
                    (void)braced;
                }
                return make_str(result);
            }

            builtin_entry const sTemplateMethods[] =
            {
                { "__init__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        std::vector<object*> const rest{ aArgs.begin() + 1, aArgs.end() };
                        if (!parse_arguments("Template", rest, aKwargs, { "template" }, 1u, 0u, a))
                            return nullptr;
                        return setattr(aArgs[0], "template", a[0]) < 0 ? nullptr : borrowed(none());
                    } },
                { "substitute", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return template_substitute(aArgs, aKwargs, false); } },
                { "safe_substitute", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return template_substitute(aArgs, aKwargs, true); } },
                { "get_identifiers", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        auto const text = attribute_string(aArgs.at(0), "template", "");
                        auto const delimiter = attribute_string(aArgs[0], "delimiter", "$");
                        std::vector<std::string> names;
                        for (std::size_t i = 0u; i < text.size();)
                        {
                            if (text.compare(i, delimiter.size(), delimiter) != 0)
                            {
                                ++i;
                                continue;
                            }
                            i += delimiter.size();
                            if (text.compare(i, delimiter.size(), delimiter) == 0)
                            {
                                i += delimiter.size();
                                continue;
                            }
                            std::string name;
                            if (i < text.size() && text[i] == '{')
                            {
                                auto const close = text.find('}', i);
                                if (close != std::string::npos)
                                {
                                    name = text.substr(i + 1u, close - i - 1u);
                                    i = close + 1u;
                                }
                            }
                            else
                            {
                                auto j = i;
                                while (j < text.size() && identifier_character(text[j]))
                                    ++j;
                                name = text.substr(i, j - i);
                                i = j;
                            }
                            if (!name.empty() && std::find(names.begin(), names.end(), name) == names.end())
                                names.push_back(name);
                        }
                        std::vector<object*> items;
                        for (auto const& n : names)
                            items.push_back(make_str(n));
                        return make_list(std::move(items));
                    } },
                { "is_valid", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        // (as string.py's: no match of its pattern is of the invalid group)
                        ref const pattern{ getattr(aArgs.at(0), "pattern") };
                        ref const text{ pattern ? getattr(aArgs[0], "template") : nullptr };
                        ref const matches{ text ? call_method_named(pattern.get(), "finditer", { (retain(text.get()), text.get()) }) : nullptr };
                        ref const all{ matches ? b_list({ matches.get() }, nullptr) : nullptr };
                        if (!all)
                            return nullptr;
                        for (auto m : as<list_object>(all.get())->items)
                        {
                            ref const invalid{ call_method_named(m, "group", { make_str("invalid") }) };
                            if (!invalid)
                                return nullptr;
                            if (kind_of(invalid.get()) != NEOS_PY_NONE)
                                return make_bool(false);
                            bool other = true;
                            for (char const* group : { "named", "braced", "escaped" })
                            {
                                ref const g{ call_method_named(m, "group", { make_str(group) }) };
                                if (!g)
                                    return nullptr;
                                if (kind_of(g.get()) != NEOS_PY_NONE)
                                    other = false;
                            }
                            if (other)
                            {
                                std::string text;
                                if (!repr_of(pattern.get(), text))
                                    return nullptr;
                                return error("ValueError", "('Unrecognized named group in pattern', " + text + ")");
                            }
                        }
                        return make_bool(true);
                    } },
                // (as string.py's: a subclass's pattern, of its delimiter, idpattern, braceidpattern and flags (unless it has its own))
                { "__init_subclass__", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        auto const cls = aArgs.at(0);
                        ref const re{ fn_import("re") };
                        ref const dict{ re ? getattr(cls, "__dict__") : nullptr };
                        ref const own{ dict ? call_method_named(dict.get(), "__contains__", { make_str("pattern") }) : nullptr };
                        if (!own)
                            return nullptr;
                        ref source;
                        if (truth(own.get()) == 1)
                        {
                            source = ref{ getattr(cls, "pattern") };
                            ref const patternType{ source ? getattr(re.get(), "Pattern") : nullptr };
                            ref const compiled{ patternType ? b_isinstance({ source.get(), patternType.get() }, nullptr) : nullptr };
                            if (!compiled)
                                return nullptr;
                            if (truth(compiled.get()) == 1)
                                return borrowed(none());
                        }
                        else
                        {
                            ref const delimiter{ getattr(cls, "delimiter") };
                            ref const escape{ delimiter ? getattr(re.get(), "escape") : nullptr };
                            ref const escapeArgs{ make_tuple({ (retain(delimiter.get()), delimiter.get()) }) };
                            ref const delim{ escape ? call(escape.get(), escapeArgs.get(), nullptr) : nullptr };
                            ref const id{ delim ? getattr(cls, "idpattern") : nullptr };
                            ref braceId{ id ? getattr(cls, "braceidpattern") : nullptr };
                            if (!braceId)
                                return nullptr;
                            if (truth(braceId.get()) != 1)
                                braceId = ref{ (retain(id.get()), id.get()) };
                            std::string d, i, b;
                            if (!str_of(delim.get(), d) || !str_of(id.get(), i) || !str_of(braceId.get(), b))
                                return nullptr;
                            source = ref{ make_str("\n            " + d + "(?:\n              (?P<escaped>" + d + ")  |   # Escape sequence of two delimiters\n"
                                "              (?P<named>" + i + ")       |   # delimiter and a Python identifier\n"
                                "              {(?P<braced>" + b + ")} |   # delimiter and a braced identifier\n"
                                "              (?P<invalid>)             # Other ill-formed delimiter exprs\n            )\n            ") };
                        }
                        ref const flags{ getattr(cls, "flags") };
                        ref const verbose{ flags ? getattr(re.get(), "VERBOSE") : nullptr };
                        ref const allFlags{ verbose ? binary(NEOS_PY_OR, flags.get(), verbose.get(), false) : nullptr };
                        ref const compile{ allFlags ? getattr(re.get(), "compile") : nullptr };
                        ref const compileArgs{ allFlags ? make_tuple({ (retain(source.get()), source.get()), (retain(allFlags.get()), allFlags.get()) }) : nullptr };
                        ref const pattern{ compile ? call(compile.get(), compileArgs.get(), nullptr) : nullptr };
                        if (!pattern || setattr(cls, "pattern", pattern.get()) < 0)
                            return nullptr;
                        return borrowed(none());
                    } }
            };

            std::string capwords(std::string const& aText, object* aSeparator)
            {
                ref const text{ make_str(aText) };
                ref const parts{ aSeparator != nullptr && kind_of(aSeparator) != NEOS_PY_NONE ? call_method_named(text.get(), "split", { borrowed(aSeparator) }) :
                    call_method_named(text.get(), "split", {}) };
                if (!parts)
                    return {};
                std::string result;
                std::string const joiner = aSeparator != nullptr && kind_of(aSeparator) == NEOS_PY_STR ? as<str_object>(aSeparator)->value : " ";
                bool first = true;
                for (auto p : as<list_object>(parts.get())->items)
                {
                    ref const capitalized{ call_method_named(p, "capitalize", {}) };
                    if (!first)
                        result += joiner;
                    first = false;
                    result += capitalized ? as<str_object>(capitalized.get())->value : std::string{};
                }
                return result;
            }

            bool string_module(std::vector<std::pair<std::string, object*>>& aAttributes)
            {
                std::string const lower = "abcdefghijklmnopqrstuvwxyz";
                std::string const upper = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
                std::string const digits = "0123456789";
                std::string const punctuation = "!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~";
                std::string const whitespace = " \t\n\r\v\f";
                aAttributes.emplace_back("ascii_lowercase", make_str(lower));
                aAttributes.emplace_back("ascii_uppercase", make_str(upper));
                aAttributes.emplace_back("ascii_letters", make_str(lower + upper));
                aAttributes.emplace_back("digits", make_str(digits));
                aAttributes.emplace_back("hexdigits", make_str(digits + "abcdefABCDEF"));
                aAttributes.emplace_back("octdigits", make_str("01234567"));
                aAttributes.emplace_back("punctuation", make_str(punctuation));
                aAttributes.emplace_back("whitespace", make_str(whitespace));
                aAttributes.emplace_back("printable", make_str(digits + lower + upper + punctuation + whitespace));
                static builtin_entry const sCapwords{ "capwords", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    std::vector<object*> a;
                    if (!parse_arguments("capwords", aArgs, aKwargs, { "s", "sep" }, 1u, 0u, a))
                        return nullptr;
                    std::string text;
                    if (!str_of(a[0], text))
                        return nullptr;
                    auto const result = capwords(text, a[1]);
                    return failed() ? nullptr : make_str(result);
                } };
                aAttributes.emplace_back("capwords", make_builtin(sCapwords));
                std::vector<std::pair<std::string, object*>> ns;
                for (auto const& m : sTemplateMethods)
                    ns.emplace_back(m.name, make_builtin(m));
                ns.emplace_back("delimiter", make_str("$"));
                ns.emplace_back("idpattern", make_str("(?a:[_a-z][_a-z0-9]*)"));
                ns.emplace_back("braceidpattern", borrowed(none()));
                // (its pattern (Template.__init_subclass__'s: of its delimiter, idpattern and braceidpattern) and flags)
                {
                    ref const re{ fn_import("re") };
                    if (!re)
                        return false;
                    ref const ignoreCase{ getattr(re.get(), "IGNORECASE") };
                    ref const verbose{ getattr(re.get(), "VERBOSE") };
                    ref const compile{ getattr(re.get(), "compile") };
                    if (!ignoreCase || !verbose || !compile)
                        return false;
                    ref const flags{ binary(NEOS_PY_OR, ignoreCase.get(), verbose.get(), false) };
                    ref const source{ make_str("\n            \\$(?:\n              (?P<escaped>\\$)  |   # Escape sequence of two delimiters\n"
                        "              (?P<named>(?a:[_a-z][_a-z0-9]*))       |   # delimiter and a Python identifier\n"
                        "              {(?P<braced>(?a:[_a-z][_a-z0-9]*))}   |   # delimiter and a braced identifier\n"
                        "              (?P<invalid>)               # Other ill-formed delimiter exprs\n            )\n            ") };
                    ref const args{ make_tuple({ (retain(source.get()), source.get()), (retain(flags.get()), flags.get()) }) };
                    ref const pattern{ flags ? call(compile.get(), args.get(), nullptr) : nullptr };
                    if (!pattern)
                        return false;
                    ns.emplace_back("flags", (retain(ignoreCase.get()), ignoreCase.get()));
                    ns.emplace_back("pattern", (retain(pattern.get()), pattern.get()));
                }
                auto const templateClass = make_class("string", "Template", {}, ns);
                for (auto& n : ns)
                    release(n.second);
                if (templateClass == nullptr)
                    return false;
                aAttributes.emplace_back("Template", templateClass);
                if (auto const formatter = make_string_formatter())
                    aAttributes.emplace_back("Formatter", formatter);
                else
                    return false;
                return true;
            }

            // --- struct

            struct format_item
            {
                char code;
                std::size_t count;      ///< (s and p: the size)
                std::size_t offset;
            };

            struct struct_format
            {
                std::vector<format_item> items;
                std::size_t size = 0u;
                std::size_t values = 0u;    ///< (the number of values packed)
                bool little = true;
                bool native = true;         ///< (native sizes and alignment)
            };

            object* struct_error(std::string const& aMessage)
            {
                if (!is_exception_class("struct.error"))
                    user_exception_bases()["struct.error"] = { "Exception" };
                ref const exception{ make_exception("struct.error", { make_str(aMessage) }) };
                fn_raise(exception.get());
                return nullptr;
            }

            std::size_t native_size(char c)
            {
                switch (c)
                {
                case 'x': case 'c': case 'b': case 'B': case '?': case 's': case 'p': return 1u;
                case 'h': case 'H': case 'e': return 2u;
                case 'i': case 'I': case 'f': return 4u;
#if defined(_WIN32)
                case 'l': case 'L': return 4u;
#else
                case 'l': case 'L': return sizeof(long);
#endif
                case 'q': case 'Q': case 'd': return 8u;
                case 'n': case 'N': case 'P': return sizeof(void*);
                default: return 0u;
                }
            }

            std::size_t standard_size(char c)
            {
                switch (c)
                {
                case 'x': case 'c': case 'b': case 'B': case '?': case 's': case 'p': return 1u;
                case 'h': case 'H': case 'e': return 2u;
                case 'i': case 'I': case 'l': case 'L': case 'f': return 4u;
                case 'q': case 'Q': case 'd': return 8u;
                default: return 0u;
                }
            }

            bool parse_format(std::string const& aFormat, struct_format& aResult)
            {
                std::size_t i = 0u;
                if (!aFormat.empty() && std::strchr("@=<>!", aFormat[0]) != nullptr)
                {
                    switch (aFormat[0])
                    {
                    case '@': aResult.native = true; aResult.little = std::endian::native == std::endian::little; break;
                    case '=': aResult.native = false; aResult.little = std::endian::native == std::endian::little; break;
                    case '<': aResult.native = false; aResult.little = true; break;
                    default: aResult.native = false; aResult.little = false; break;
                    }
                    ++i;
                }
                else
                    aResult.little = std::endian::native == std::endian::little;
                std::size_t offset = 0u;
                while (i < aFormat.size())
                {
                    auto c = aFormat[i];
                    if (std::isspace(static_cast<unsigned char>(c)))
                    {
                        ++i;
                        continue;
                    }
                    std::size_t count = 1u;
                    bool hasCount = false;
                    if (std::isdigit(static_cast<unsigned char>(c)))
                    {
                        count = 0u;
                        while (i < aFormat.size() && std::isdigit(static_cast<unsigned char>(aFormat[i])))
                            count = count * 10u + static_cast<std::size_t>(aFormat[i++] - '0');
                        hasCount = true;
                        if (i >= aFormat.size())
                        {
                            struct_error("repeat count given without format specifier");
                            return false;
                        }
                        c = aFormat[i];
                    }
                    ++i;
                    auto const size = aResult.native ? native_size(c) : standard_size(c);
                    if (size == 0u || (!aResult.native && (c == 'n' || c == 'N' || c == 'P')))
                    {
                        struct_error("bad char in struct format");
                        return false;
                    }
                    // (native alignment)
                    if (aResult.native && size > 1u && c != 's' && c != 'p')
                        offset = (offset + size - 1u) / size * size;
                    if (c == 's' || c == 'p')
                    {
                        aResult.items.push_back({ c, hasCount ? count : 1u, offset });
                        offset += hasCount ? count : 1u;
                        ++aResult.values;
                    }
                    else if (c == 'x')
                        offset += count;
                    else
                        for (std::size_t k = 0u; k < count; ++k)
                        {
                            aResult.items.push_back({ c, 1u, offset });
                            offset += size;
                            ++aResult.values;
                        }
                }
                aResult.size = offset;
                return true;
            }

            bool format_of(object* aFormat, struct_format& aResult)
            {
                std::string text;
                std::string_view bytes;
                if (kind_of(aFormat) == NEOS_PY_STR)
                    text = as<str_object>(aFormat)->value;
                else if (bytes_like(aFormat, bytes))
                    text = std::string{ bytes };
                else
                {
                    error("TypeError", std::string{ "Struct() argument 1 must be a str or bytes object, not " } + type_name(aFormat));
                    return false;
                }
                return parse_format(text, aResult);
            }

            void put_integer(std::string& aBuffer, std::size_t aOffset, std::uint64_t aValue, std::size_t aSize, bool aLittle)
            {
                for (std::size_t i = 0u; i < aSize; ++i)
                    aBuffer[aOffset + (aLittle ? i : aSize - 1u - i)] = static_cast<char>(aValue >> (8u * i));
            }

            std::uint64_t get_integer(std::string_view aBuffer, std::size_t aOffset, std::size_t aSize, bool aLittle)
            {
                std::uint64_t value = 0u;
                for (std::size_t i = 0u; i < aSize; ++i)
                    value |= static_cast<std::uint64_t>(static_cast<unsigned char>(aBuffer[aOffset + (aLittle ? i : aSize - 1u - i)])) << (8u * i);
                return value;
            }
        }

        // (IEEE 754 binary16 (struct's and memoryview's format 'e'))
        std::uint16_t to_half(double aValue)
        {
            // (IEEE 754 binary16, round half to even)
            std::uint64_t bits = std::bit_cast<std::uint64_t>(aValue);
            std::uint16_t const sign = static_cast<std::uint16_t>((bits >> 48) & 0x8000u);
            if (std::isnan(aValue))
                return static_cast<std::uint16_t>(sign | 0x7e00u);
            double const magnitude = std::fabs(aValue);
            if (std::isinf(aValue) || magnitude >= 65520.0)
                return static_cast<std::uint16_t>(sign | 0x7c00u);
            if (magnitude < std::ldexp(1.0, -14))
            {
                auto const m = std::nearbyint(magnitude / std::ldexp(1.0, -24));
                return static_cast<std::uint16_t>(sign | static_cast<std::uint16_t>(m));
            }
            int e = 0;
            double const f = std::frexp(magnitude, &e);    // (magnitude = f * 2^e, 0.5 <= f < 1)
            auto m = static_cast<std::uint32_t>(std::nearbyint((f * 2.0 - 1.0) * 1024.0));
            int exponent = e - 1 + 15;
            if (m == 1024u)
            {
                m = 0u;
                ++exponent;
            }
            if (exponent >= 31)
                return static_cast<std::uint16_t>(sign | 0x7c00u);
            return static_cast<std::uint16_t>(sign | (exponent << 10) | m);
        }

        double from_half(std::uint16_t aValue)
        {
            int const exponent = (aValue >> 10) & 0x1f;
            int const mantissa = aValue & 0x3ff;
            double value = 0.0;
            if (exponent == 0)
                value = std::ldexp(mantissa, -24);
            else if (exponent == 31)
                value = mantissa == 0 ? std::numeric_limits<double>::infinity() : std::numeric_limits<double>::quiet_NaN();
            else
                value = std::ldexp(mantissa + 1024, exponent - 25);
            return (aValue & 0x8000u) != 0u ? -value : value;
        }

        namespace
        {
            bool pack_into(struct_format const& aFormat, std::vector<object*> const& aValues, std::string& aBuffer, std::size_t aOffset)
            {
                if (aValues.size() != aFormat.values)
                {
                    struct_error("pack expected " + std::to_string(aFormat.values) + " items for packing (got " + std::to_string(aValues.size()) + ")");
                    return false;
                }
                for (std::size_t k = 0u; k < aFormat.items.size(); ++k)
                {
                    auto const& item = aFormat.items[k];
                    auto const value = aValues[k];
                    auto const offset = aOffset + item.offset;
                    auto const size = aFormat.native ? native_size(item.code) : standard_size(item.code);
                    switch (item.code)
                    {
                    case 'c':
                        {
                            std::string_view b;
                            if (!bytes_like(value, b) || b.size() != 1u)
                            {
                                struct_error("char format requires a bytes object of length 1");
                                return false;
                            }
                            aBuffer[offset] = b[0];
                            break;
                        }
                    case 's': case 'p':
                        {
                            std::string_view b;
                            if (!bytes_like(value, b))
                            {
                                struct_error(std::string{ "argument for '" } + item.code + "' must be a bytes object");
                                return false;
                            }
                            if (item.code == 's')
                                for (std::size_t i = 0u; i < item.count; ++i)
                                    aBuffer[offset + i] = i < b.size() ? b[i] : '\0';
                            else if (item.count > 0u)
                            {
                                auto const n = std::min<std::size_t>({ b.size(), item.count - 1u, 255u });
                                aBuffer[offset] = static_cast<char>(n);
                                for (std::size_t i = 1u; i < item.count; ++i)
                                    aBuffer[offset + i] = i - 1u < n ? b[i - 1u] : '\0';
                            }
                            break;
                        }
                    case '?':
                        {
                            auto const t = truth(value);
                            if (t < 0)
                                return false;
                            aBuffer[offset] = static_cast<char>(t);
                            break;
                        }
                    case 'e': case 'f': case 'd':
                        {
                            double d = 0.0;
                            if (!to_double(value, d))
                            {
                                clear_error();
                                struct_error("required argument is not a float");
                                return false;
                            }
                            if (item.code == 'd')
                                put_integer(aBuffer, offset, std::bit_cast<std::uint64_t>(d), 8u, aFormat.little);
                            else if (item.code == 'f')
                            {
                                if (std::isfinite(d) && std::fabs(d) > std::numeric_limits<float>::max())
                                {
                                    error("OverflowError", "float too large to pack with f format");
                                    return false;
                                }
                                put_integer(aBuffer, offset, std::bit_cast<std::uint32_t>(static_cast<float>(d)), 4u, aFormat.little);
                            }
                            else
                            {
                                if (std::isfinite(d) && std::fabs(d) >= 65520.0)
                                {
                                    error("OverflowError", "float too large to pack with e format");
                                    return false;
                                }
                                put_integer(aBuffer, offset, to_half(d), 2u, aFormat.little);
                            }
                            break;
                        }
                    default:
                        {
                            // (integers)
                            if (!is_int_like(value) && !(is_instance(value) && has_special_method(value, "__index__")) && kind_of(builtin_value(value)) != NEOS_PY_INT)
                            {
                                struct_error("required argument is not an integer");
                                return false;
                            }
                            bool const isUnsigned = std::isupper(static_cast<unsigned char>(item.code)) != 0;
                            ref const v{ borrowed(value) };
                            long long n = 0;
                            bool fits = to_index(value, n);
                            bool big = false;
                            if (!fits)
                            {
                                clear_error();
                                big = true;
                            }
                            std::uint64_t u = static_cast<std::uint64_t>(n);
                            if (isUnsigned && big && size == 8u)
                            {
                                // (an unsigned 64-bit value beyond a signed one's range: its two 32-bit halves)
                                ref const shift{ make_int(32) };
                                ref const mask{ make_int(0xFFFFFFFFll) };
                                ref const high{ binary(NEOS_PY_RSHIFT, value, shift.get(), false) };
                                ref const low{ binary(NEOS_PY_AND, value, mask.get(), false) };
                                long long h = 0, l = 0;
                                if (high && low && to_index(high.get(), h) && to_index(low.get(), l) && h >= 0 && h <= 0xFFFFFFFFll)
                                {
                                    u = (static_cast<std::uint64_t>(h) << 32) | static_cast<std::uint64_t>(l);
                                    big = false;
                                    n = 1;
                                }
                                clear_error();
                            }
                            auto const range_error = [&]() -> bool
                            {
                                std::string message;
                                auto const bits = 8u * size;
                                if (aFormat.native && (item.code == 'n' || item.code == 'N' || item.code == 'P'))
                                    message = "argument out of range";
                                else if (isUnsigned)
                                    message = std::string{ "'" } + item.code + "' format requires 0 <= number <= " + 
                                        std::to_string(bits == 64u ? std::numeric_limits<std::uint64_t>::max() : (std::uint64_t{ 1 } << bits) - 1u);
                                else if (bits == 64u)
                                    message = std::string{ "'" } + item.code + "' format requires -9223372036854775808 <= number <= 9223372036854775807";
                                else
                                    message = std::string{ "'" } + item.code + "' format requires " + std::to_string(-(std::int64_t{ 1 } << (bits - 1u))) + " <= number <= " +
                                        std::to_string((std::int64_t{ 1 } << (bits - 1u)) - 1);
                                struct_error(message);
                                return false;
                            };
                            if (big)
                                return range_error();
                            if (isUnsigned)
                            {
                                if (n < 0 || (size < 8u && u > (std::uint64_t{ 1 } << (8u * size)) - 1u))
                                    return range_error();
                            }
                            else if (size < 8u)
                            {
                                auto const limit = std::int64_t{ 1 } << (8u * size - 1u);
                                if (n < -limit || n >= limit)
                                    return range_error();
                            }
                            put_integer(aBuffer, offset, u, size, aFormat.little);
                        }
                    }
                }
                return true;
            }

            object* unpack_from(struct_format const& aFormat, std::string_view aBuffer, std::size_t aOffset)
            {
                std::vector<object*> values;
                for (auto const& item : aFormat.items)
                {
                    auto const offset = aOffset + item.offset;
                    auto const size = aFormat.native ? native_size(item.code) : standard_size(item.code);
                    switch (item.code)
                    {
                    case 'c': values.push_back(make_bytes(std::string(1u, aBuffer[offset]))); break;
                    case 's': values.push_back(make_bytes(std::string{ aBuffer.substr(offset, item.count) })); break;
                    case 'p':
                        {
                            auto const n = item.count == 0u ? 0u : std::min<std::size_t>(static_cast<unsigned char>(aBuffer[offset]), item.count - 1u);
                            values.push_back(make_bytes(std::string{ aBuffer.substr(offset + 1u, n) }));
                            break;
                        }
                    case '?': values.push_back(make_bool(aBuffer[offset] != 0)); break;
                    case 'e': values.push_back(make_float(from_half(static_cast<std::uint16_t>(get_integer(aBuffer, offset, 2u, aFormat.little))))); break;
                    case 'f': values.push_back(make_float(std::bit_cast<float>(static_cast<std::uint32_t>(get_integer(aBuffer, offset, 4u, aFormat.little))))); break;
                    case 'd': values.push_back(make_float(std::bit_cast<double>(get_integer(aBuffer, offset, 8u, aFormat.little)))); break;
                    default:
                        {
                            auto const raw = get_integer(aBuffer, offset, size, aFormat.little);
                            if (std::isupper(static_cast<unsigned char>(item.code)))
                            {
                                if (raw > static_cast<std::uint64_t>(std::numeric_limits<long long>::max()))
                                {
                                    ref const high{ make_int(static_cast<long long>(raw >> 1)) };
                                    ref const two{ make_int(2) };
                                    ref const doubled{ binary(NEOS_PY_MULTIPLY, high.get(), two.get(), false) };
                                    ref const low{ make_int(static_cast<long long>(raw & 1u)) };
                                    values.push_back(binary(NEOS_PY_ADD, doubled.get(), low.get(), false));
                                }
                                else
                                    values.push_back(make_int(static_cast<long long>(raw)));
                            }
                            else
                            {
                                auto value = static_cast<long long>(raw);
                                if (size < 8u && (raw & (std::uint64_t{ 1 } << (8u * size - 1u))) != 0u)
                                    value -= static_cast<long long>(std::uint64_t{ 1 } << (8u * size));
                                values.push_back(make_int(value));
                            }
                        }
                    }
                }
                return make_tuple(std::move(values));
            }

            bool buffer_of(object* aBuffer, std::string_view& aResult)
            {
                if (!bytes_like(aBuffer, aResult))
                {
                    error("TypeError", std::string{ "a bytes-like object is required, not '" } + type_name(aBuffer) + "'");
                    return false;
                }
                return true;
            }

            object* struct_pack(struct_format const& aFormat, std::vector<object*> const& aValues)
            {
                std::string buffer(aFormat.size, '\0');
                if (!pack_into(aFormat, aValues, buffer, 0u))
                    return nullptr;
                return make_bytes(buffer);
            }

            object* struct_unpack(struct_format const& aFormat, object* aBuffer)
            {
                std::string_view data;
                if (!buffer_of(aBuffer, data))
                    return nullptr;
                if (data.size() != aFormat.size)
                    return struct_error("unpack requires a buffer of " + std::to_string(aFormat.size) + " bytes");
                return unpack_from(aFormat, data, 0u);
            }

            object* struct_unpack_from(struct_format const& aFormat, object* aBuffer, long long aOffset)
            {
                std::string_view data;
                if (!buffer_of(aBuffer, data))
                    return nullptr;
                if (aOffset < 0)
                    aOffset += static_cast<long long>(data.size());
                if (aOffset < 0 || static_cast<std::size_t>(aOffset) + aFormat.size > data.size())
                    return struct_error("unpack_from requires a buffer of at least " + std::to_string(aFormat.size + static_cast<std::size_t>(std::max(0ll, aOffset))) +
                        " bytes for unpacking " + std::to_string(aFormat.size) + " bytes at offset " + std::to_string(aOffset) + " (actual buffer size is " +
                        std::to_string(data.size()) + ")");
                return unpack_from(aFormat, data, static_cast<std::size_t>(aOffset));
            }

            object* struct_iter_unpack(struct_format const& aFormat, object* aBuffer)
            {
                std::string_view data;
                if (!buffer_of(aBuffer, data))
                    return nullptr;
                if (aFormat.size == 0u)
                    return struct_error("cannot iteratively unpack with a struct of length 0");
                if (data.size() % aFormat.size != 0u)
                    return struct_error("iterative unpacking requires a buffer of a multiple of " + std::to_string(aFormat.size) + " bytes");
                std::vector<object*> items;
                for (std::size_t offset = 0u; offset < data.size(); offset += aFormat.size)
                    items.push_back(unpack_from(aFormat, data, offset));
                ref const list{ make_list(std::move(items)) };
                return iter(list.get());
            }

            object* struct_pack_into(struct_format const& aFormat, std::vector<object*> const& aArgs)
            {
                if (aArgs.size() < 2u)
                    return error("TypeError", "pack_into expected buffer argument");
                long long offset = 0;
                if (!to_index(aArgs[1], offset))
                    return nullptr;
                std::string_view data;
                if (!bytes_like(aArgs[0], data) || std::string_view{ python_type(aArgs[0])->name } != "bytearray")
                    return error("TypeError", std::string{ "argument must be read-write bytes-like object, not " } + type_name(aArgs[0]));
                if (offset < 0)
                    offset += static_cast<long long>(data.size());
                if (offset < 0 || static_cast<std::size_t>(offset) + aFormat.size > data.size())
                    return struct_error("pack_into requires a buffer of at least " + std::to_string(aFormat.size + static_cast<std::size_t>(std::max(0ll, offset))) +
                        " bytes for packing " + std::to_string(aFormat.size) + " bytes at offset " + std::to_string(offset) + " (actual buffer size is " +
                        std::to_string(data.size()) + ")");
                std::string buffer{ data };
                if (!pack_into(aFormat, std::vector<object*>{ aArgs.begin() + 2, aArgs.end() }, buffer, static_cast<std::size_t>(offset)))
                    return nullptr;
                // (the bytearray's data: replaced by a slice assignment)
                ref const whole{ make_slice(nullptr, nullptr, nullptr) };
                ref const value{ make_bytes(buffer) };
                if (setitem(aArgs[0], whole.get(), value.get()) < 0)
                    return nullptr;
                return borrowed(none());
            }

            // (struct.Struct)
            struct struct_object
            {
                header h;
                object* format;
                struct_format parsed;
                ~struct_object() { release(format); }
            };

            neos_py_type make_struct_type();
            neos_py_type sStructType = make_struct_type();

            neos_py_type make_struct_type()
            {
                auto type = make_type("_struct.Struct", "Struct", retain_object, release_object<struct_object>);
                type.repr = [](neos_py_context*, object* aSelf) -> object*
                {
                    std::string text;
                    if (!repr_of(reinterpret_cast<struct_object*>(aSelf)->format, text))
                        return nullptr;
                    return make_str("Struct(" + text + ")");
                };
                type.getattr = [](neos_py_context*, object* aSelf, char const* aName) -> object*
                {
                    auto& s = *reinterpret_cast<struct_object*>(aSelf);
                    std::string_view const name{ aName };
                    if (name == "format")
                        return borrowed(s.format);
                    if (name == "size")
                        return make_int(static_cast<long long>(s.parsed.size));
                    static char const* const sMethods[] = { "pack", "unpack", "unpack_from", "iter_unpack", "pack_into" };
                    for (std::size_t i = 0u; i < std::size(sMethods); ++i)
                        if (name == sMethods[i])
                            return make_function(sMethods[i], [](neos_py_context*, void* aData, object* aArgs, object* aKwargs) -> object*
                            {
                                auto const& args = as<tuple_object>(aArgs)->items;
                                auto const& s = *reinterpret_cast<struct_object*>(args[0]);
                                std::vector<object*> const rest{ args.begin() + 1, args.end() };
                                switch (reinterpret_cast<std::uintptr_t>(aData))
                                {
                                case 0: return struct_pack(s.parsed, rest);
                                case 1:
                                    if (rest.size() != 1u)
                                        return error("TypeError", "unpack() takes exactly one argument (" + std::to_string(rest.size()) + " given)");
                                    return struct_unpack(s.parsed, rest[0]);
                                case 2:
                                    {
                                        std::vector<object*> a;
                                        if (!parse_arguments("unpack_from", rest, aKwargs, { "buffer", "offset" }, 1u, 0u, a))
                                            return nullptr;
                                        long long offset = 0;
                                        if (a[1] != nullptr && !to_index(a[1], offset))
                                            return nullptr;
                                        return struct_unpack_from(s.parsed, a[0], offset);
                                    }
                                case 3:
                                    if (rest.size() != 1u)
                                        return error("TypeError", "iter_unpack() takes exactly one argument (" + std::to_string(rest.size()) + " given)");
                                    return struct_iter_unpack(s.parsed, rest[0]);
                                default:
                                    return struct_pack_into(s.parsed, rest);
                                }
                            }, reinterpret_cast<void*>(i), aSelf);
                    return error("AttributeError", std::string{ "'_struct.Struct' object has no attribute '" } + aName + "'");
                };
                return type;
            }

            object* new_struct(neos_py_context*, void*, object* aArgs, object* aKwargs)
            {
                std::vector<object*> a;
                if (!parse_arguments("Struct", as<tuple_object>(aArgs)->items, aKwargs, { "format" }, 1u, 0u, a))
                    return nullptr;
                struct_format parsed;
                if (!format_of(a[0], parsed))
                    return nullptr;
                return &(new struct_object{ { { &sStructType.base } }, borrowed(a[0]), std::move(parsed) })->h.base;
            }

            int struct_instancecheck(neos_py_context*, void*, object* aObject) { return aObject->type == &sStructType.base ? 1 : 0; }

            builtin_entry const sStructFunctions[] =
            {
                { "pack", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        if (aKwargs != nullptr && !as<dict_object>(aKwargs)->items.empty())
                            return error("TypeError", "pack() takes no keyword arguments");
                        if (aArgs.empty())
                            return error("TypeError", "pack expected at least 1 argument, got 0");
                        struct_format f;
                        if (!format_of(aArgs[0], f))
                            return nullptr;
                        return struct_pack(f, std::vector<object*>{ aArgs.begin() + 1, aArgs.end() });
                    } },
                { "unpack", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("unpack", aArgs, aKwargs, { "format", "buffer" }, 2u, 2u, a))
                            return nullptr;
                        struct_format f;
                        return format_of(a[0], f) ? struct_unpack(f, a[1]) : nullptr;
                    } },
                { "unpack_from", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("unpack_from", aArgs, aKwargs, { "format", "buffer", "offset" }, 2u, 1u, a))
                            return nullptr;
                        struct_format f;
                        long long offset = 0;
                        if (!format_of(a[0], f) || (a[2] != nullptr && !to_index(a[2], offset)))
                            return nullptr;
                        return struct_unpack_from(f, a[1], offset);
                    } },
                { "iter_unpack", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("iter_unpack", aArgs, aKwargs, { "format", "buffer" }, 2u, 2u, a))
                            return nullptr;
                        struct_format f;
                        return format_of(a[0], f) ? struct_iter_unpack(f, a[1]) : nullptr;
                    } },
                { "pack_into", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        if (aArgs.empty())
                            return error("TypeError", "pack_into expected format argument");
                        struct_format f;
                        return format_of(aArgs[0], f) ? struct_pack_into(f, std::vector<object*>{ aArgs.begin() + 1, aArgs.end() }) : nullptr;
                    } },
                { "calcsize", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("calcsize", aArgs, aKwargs, { "format" }, 1u, 1u, a))
                            return nullptr;
                        struct_format f;
                        return format_of(a[0], f) ? make_int(static_cast<long long>(f.size)) : nullptr;
                    } }
            };

            bool struct_module(std::vector<std::pair<std::string, object*>>& aAttributes)
            {
                for (auto const& f : sStructFunctions)
                    aAttributes.emplace_back(f.name, make_builtin(f));
                aAttributes.emplace_back("Struct", context()->type_new(context(), "Struct", "_struct.Struct", new_struct, nullptr, struct_instancecheck));
                if (!is_exception_class("struct.error"))
                    user_exception_bases()["struct.error"] = { "Exception" };
                aAttributes.emplace_back("error", exception_class("struct.error"));
                return true;
            }

            // --- binascii

            object* binascii_error(std::string const& aMessage)
            {
                if (!is_exception_class("binascii.Error"))
                    user_exception_bases()["binascii.Error"] = { "ValueError" };
                ref const exception{ make_exception("binascii.Error", { make_str(aMessage) }) };
                fn_raise(exception.get());
                return nullptr;
            }

            // (a binary argument: bytes-like, or (aAscii) an ASCII str)
            bool binary_argument(object* aValue, std::string& aResult, bool aAscii)
            {
                std::string_view b;
                if (bytes_like(aValue, b))
                {
                    aResult = std::string{ b };
                    return true;
                }
                if (aAscii && kind_of(aValue) == NEOS_PY_STR)
                {
                    auto const& text = as<str_object>(aValue)->value;
                    for (unsigned char c : text)
                        if (c >= 0x80u)
                        {
                            error("ValueError", "string argument should contain only ASCII characters");
                            return false;
                        }
                    aResult = text;
                    return true;
                }
                error("TypeError", aAscii ? std::string{ "argument should be bytes, buffer or ASCII string, not '" } + type_name(aValue) + "'" :
                    std::string{ "a bytes-like object is required, not '" } + type_name(aValue) + "'");
                return false;
            }

            std::string hexlify(std::string const& aData, std::string const& aSeparator, long long aBytesPerSep)
            {
                static char const sDigits[] = "0123456789abcdef";
                std::string result;
                auto const n = aData.size();
                for (std::size_t i = 0u; i < n; ++i)
                {
                    if (!aSeparator.empty() && i != 0u && aBytesPerSep != 0)
                    {
                        auto const group = static_cast<std::size_t>(std::llabs(aBytesPerSep));
                        // (positive: groups counted from the right)
                        bool const boundary = aBytesPerSep > 0 ? (n - i) % group == 0u : i % group == 0u;
                        if (boundary)
                            result += aSeparator;
                    }
                    auto const c = static_cast<unsigned char>(aData[i]);
                    result += sDigits[c >> 4];
                    result += sDigits[c & 15u];
                }
                return result;
            }

            bool unhexlify(std::string const& aText, std::string& aResult)
            {
                if (aText.size() % 2u != 0u)
                {
                    binascii_error("Odd-length string");
                    return false;
                }
                for (std::size_t i = 0u; i < aText.size(); i += 2u)
                {
                    auto const value = [](char c) -> int
                    {
                        if (c >= '0' && c <= '9') return c - '0';
                        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                        return -1;
                    };
                    auto const hi = value(aText[i]);
                    auto const lo = value(aText[i + 1u]);
                    if (hi < 0 || lo < 0)
                    {
                        binascii_error("Non-hexadecimal digit found");
                        return false;
                    }
                    aResult += static_cast<char>(hi * 16 + lo);
                }
                return true;
            }

            char const sBase64Alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

            std::string base64_encode(std::string const& aData)
            {
                std::string result;
                std::size_t i = 0u;
                for (; i + 2u < aData.size(); i += 3u)
                {
                    auto const v = (static_cast<unsigned>(static_cast<unsigned char>(aData[i])) << 16) | (static_cast<unsigned>(static_cast<unsigned char>(aData[i + 1u])) << 8) |
                        static_cast<unsigned char>(aData[i + 2u]);
                    result += sBase64Alphabet[(v >> 18) & 63u];
                    result += sBase64Alphabet[(v >> 12) & 63u];
                    result += sBase64Alphabet[(v >> 6) & 63u];
                    result += sBase64Alphabet[v & 63u];
                }
                if (i < aData.size())
                {
                    auto v = static_cast<unsigned>(static_cast<unsigned char>(aData[i])) << 16;
                    if (i + 1u < aData.size())
                        v |= static_cast<unsigned>(static_cast<unsigned char>(aData[i + 1u])) << 8;
                    result += sBase64Alphabet[(v >> 18) & 63u];
                    result += sBase64Alphabet[(v >> 12) & 63u];
                    result += i + 1u < aData.size() ? sBase64Alphabet[(v >> 6) & 63u] : '=';
                    result += '=';
                }
                return result;
            }

            // (binascii.a2b_base64: non-alphabet characters are discarded unless aStrict)
            bool base64_decode(std::string const& aText, std::string& aResult, bool aStrict)
            {
                auto const value = [](char c) -> int
                {
                    if (c >= 'A' && c <= 'Z') return c - 'A';
                    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
                    if (c >= '0' && c <= '9') return c - '0' + 52;
                    if (c == '+') return 62;
                    if (c == '/') return 63;
                    return -1;
                };
                unsigned accumulator = 0u;
                int bits = 0;
                std::size_t quad = 0u;      ///< (the position in the current group of 4)
                std::size_t pads = 0u;
                std::size_t characters = 0u;
                bool padding = false;
                for (std::size_t i = 0u; i < aText.size(); ++i)
                {
                    auto const c = aText[i];
                    if (c == '=')
                    {
                        if (aStrict && quad < 2u)
                        {
                            binascii_error(quad == 0u && i == 0u ? "Leading padding not allowed" : "Discontinuous padding not allowed");
                            return false;
                        }
                        if (quad >= 2u)
                        {
                            ++pads;
                            padding = true;
                            if (quad + pads >= 4u)
                            {
                                // (the end of the data)
                                if (aStrict && i + 1u < aText.size())
                                {
                                    binascii_error("Excess data after padding");
                                    return false;
                                }
                                quad = 0u;
                                accumulator = 0u;
                                bits = 0;
                                pads = 0u;
                                if (!aStrict)
                                    break;
                            }
                        }
                        continue;
                    }
                    auto const v = value(c);
                    if (v < 0)
                    {
                        if (aStrict)
                        {
                            binascii_error("Only base64 data is allowed");
                            return false;
                        }
                        continue;
                    }
                    if (padding && aStrict)
                    {
                        binascii_error("Discontinuous padding not allowed");
                        return false;
                    }
                    padding = false;
                    pads = 0u;
                    ++characters;
                    accumulator = (accumulator << 6) | static_cast<unsigned>(v);
                    bits += 6;
                    quad = (quad + 1u) % 4u;
                    if (bits >= 8)
                    {
                        bits -= 8;
                        aResult += static_cast<char>((accumulator >> bits) & 0xFFu);
                    }
                }
                if (quad != 0u)
                {
                    if (quad == 1u)
                        binascii_error("Invalid base64-encoded string: number of data characters (" + std::to_string(characters) + ") cannot be 1 more than a multiple of 4");
                    else
                        binascii_error("Incorrect padding");
                    return false;
                }
                return true;
            }

            std::uint32_t crc32(std::string const& aData, std::uint32_t aValue)
            {
                static std::uint32_t sTable[256];
                static bool sReady = false;
                if (!sReady)
                {
                    for (std::uint32_t i = 0u; i < 256u; ++i)
                    {
                        auto c = i;
                        for (int k = 0; k < 8; ++k)
                            c = (c & 1u) != 0u ? 0xEDB88320u ^ (c >> 1) : c >> 1;
                        sTable[i] = c;
                    }
                    sReady = true;
                }
                auto crc = ~aValue;
                for (unsigned char b : aData)
                    crc = sTable[(crc ^ b) & 0xFFu] ^ (crc >> 8);
                return ~crc;
            }

            builtin_entry const sBinasciiFunctions[] =
            {
                { "hexlify", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("hexlify", aArgs, aKwargs, { "data", "sep", "bytes_per_sep" }, 1u, 0u, a))
                            return nullptr;
                        std::string data, separator;
                        long long perSep = 1;
                        if (!binary_argument(a[0], data, false))
                            return nullptr;
                        if (a[1] != nullptr && !binary_argument(a[1], separator, true))
                            return nullptr;
                        if (a[2] != nullptr && !to_index(a[2], perSep))
                            return nullptr;
                        if (separator.size() > 1u)
                            return error("ValueError", "sep must be length 1.");
                        return make_bytes(hexlify(data, separator, perSep));
                    } },
                { "unhexlify", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        std::string text, result;
                        if (!parse_arguments("unhexlify", aArgs, aKwargs, { "hexstr" }, 1u, 1u, a) || !binary_argument(a[0], text, true) || !unhexlify(text, result))
                            return nullptr;
                        return make_bytes(result);
                    } },
                { "b2a_base64", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        std::string data;
                        if (!parse_arguments("b2a_base64", aArgs, aKwargs, { "data", "newline" }, 1u, 1u, a) || !binary_argument(a[0], data, false))
                            return nullptr;
                        bool const newline = a[1] == nullptr || truth(a[1]) == 1;
                        return make_bytes(base64_encode(data) + (newline ? "\n" : ""));
                    } },
                { "a2b_base64", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        std::string text, result;
                        if (!parse_arguments("a2b_base64", aArgs, aKwargs, { "data", "strict_mode" }, 1u, 1u, a) || !binary_argument(a[0], text, true))
                            return nullptr;
                        bool const strict = a[1] != nullptr && truth(a[1]) == 1;
                        if (!base64_decode(text, result, strict))
                            return nullptr;
                        return make_bytes(result);
                    } },
                { "crc32", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        std::string data;
                        if (!parse_arguments("crc32", aArgs, aKwargs, { "data", "crc" }, 1u, 2u, a) || !binary_argument(a[0], data, false))
                            return nullptr;
                        long long value = 0;
                        if (a[1] != nullptr && !to_index(a[1], value))
                            return nullptr;
                        return make_int(crc32(data, static_cast<std::uint32_t>(value)));
                    } },
                { "crc_hqx", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        std::string data;
                        if (!parse_arguments("crc_hqx", aArgs, aKwargs, { "data", "crc" }, 2u, 2u, a) || !binary_argument(a[0], data, false))
                            return nullptr;
                        long long value = 0;
                        if (!to_index(a[1], value))
                            return nullptr;
                        auto crc = static_cast<std::uint32_t>(value) & 0xFFFFu;
                        for (unsigned char b : data)
                        {
                            crc ^= static_cast<std::uint32_t>(b) << 8;
                            for (int k = 0; k < 8; ++k)
                                crc = (crc & 0x8000u) != 0u ? ((crc << 1) ^ 0x1021u) & 0xFFFFu : (crc << 1) & 0xFFFFu;
                        }
                        return make_int(crc);
                    } }
            };

            bool binascii_module(std::vector<std::pair<std::string, object*>>& aAttributes)
            {
                for (auto const& f : sBinasciiFunctions)
                    aAttributes.emplace_back(f.name, make_builtin(f));
                aAttributes.emplace_back("b2a_hex", make_builtin(sBinasciiFunctions[0]));
                aAttributes.emplace_back("a2b_hex", make_builtin(sBinasciiFunctions[1]));
                if (!is_exception_class("binascii.Error"))
                    user_exception_bases()["binascii.Error"] = { "ValueError" };
                aAttributes.emplace_back("Error", exception_class("binascii.Error"));
                if (!is_exception_class("binascii.Incomplete"))
                    user_exception_bases()["binascii.Incomplete"] = { "Exception" };
                aAttributes.emplace_back("Incomplete", exception_class("binascii.Incomplete"));
                return true;
            }

            // --- base64

            std::string translate(std::string aText, std::string const& aFrom, std::string const& aTo)
            {
                for (auto& c : aText)
                    if (auto const i = aFrom.find(c); i != std::string::npos)
                        c = aTo[i];
                return aText;
            }

            object* b64encode(object* aData, object* aAltchars, bool aUrlsafe)
            {
                std::string data;
                if (!binary_argument(aData, data, false))
                    return nullptr;
                auto encoded = base64_encode(data);
                if (aUrlsafe)
                    encoded = translate(encoded, "+/", "-_");
                else if (aAltchars != nullptr && kind_of(aAltchars) != NEOS_PY_NONE)
                {
                    std::string alt;
                    if (!binary_argument(aAltchars, alt, false))
                        return nullptr;
                    if (alt.size() != 2u)
                        return error("AssertionError", repr_of(aAltchars, alt) ? alt : "");
                    encoded = translate(encoded, "+/", alt);
                }
                return make_bytes(encoded);
            }

            object* b64decode(object* aData, object* aAltchars, bool aValidate, bool aUrlsafe)
            {
                std::string text, result;
                if (!binary_argument(aData, text, true))
                    return nullptr;
                if (aUrlsafe)
                    text = translate(text, "-_", "+/");
                else if (aAltchars != nullptr && kind_of(aAltchars) != NEOS_PY_NONE)
                {
                    std::string alt;
                    if (!binary_argument(aAltchars, alt, true))
                        return nullptr;
                    if (alt.size() != 2u)
                        return error("ValueError", "invalid altchars");
                    text = translate(text, alt, "+/");
                }
                if (!base64_decode(text, result, aValidate))
                    return nullptr;
                return make_bytes(result);
            }

            std::string base32_encode(std::string const& aData, bool aHex)
            {
                char const* const alphabet = aHex ? "0123456789ABCDEFGHIJKLMNOPQRSTUV" : "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
                std::string result;
                for (std::size_t i = 0u; i < aData.size(); i += 5u)
                {
                    std::uint64_t block = 0u;
                    std::size_t const n = std::min<std::size_t>(5u, aData.size() - i);
                    for (std::size_t k = 0u; k < 5u; ++k)
                        block = (block << 8) | (k < n ? static_cast<unsigned char>(aData[i + k]) : 0u);
                    static std::size_t const sCharacters[] = { 0u, 2u, 4u, 5u, 7u, 8u };
                    for (std::size_t k = 0u; k < 8u; ++k)
                        result += k < sCharacters[n] ? alphabet[(block >> (35u - 5u * k)) & 31u] : '=';
                }
                return result;
            }

            bool base32_decode(std::string const& aText, std::string& aResult, bool aCasefold, bool aHex)
            {
                std::string const alphabet = aHex ? "0123456789ABCDEFGHIJKLMNOPQRSTUV" : "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
                if (aText.size() % 8u != 0u)
                {
                    binascii_error("Incorrect padding");
                    return false;
                }
                auto text = aText;
                if (aCasefold)
                    for (auto& c : text)
                        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
                auto const stripped = text.find_last_not_of('=');
                auto const pads = stripped == std::string::npos ? text.size() : text.size() - stripped - 1u;
                static std::size_t const sBytesForPads[] = { 5u, 4u, 0u, 3u, 2u, 0u, 1u, 0u, 0u };
                if (pads > 6u || (pads != 0u && sBytesForPads[pads] == 0u && pads != 0u))
                {
                    binascii_error("Incorrect padding");
                    return false;
                }
                for (std::size_t i = 0u; i < text.size(); i += 8u)
                {
                    std::uint64_t block = 0u;
                    for (std::size_t k = 0u; k < 8u; ++k)
                    {
                        auto const c = text[i + k];
                        std::uint64_t v = 0u;
                        if (c != '=')
                        {
                            auto const position = alphabet.find(c);
                            if (position == std::string::npos)
                            {
                                binascii_error("Non-base32 digit found");
                                return false;
                            }
                            v = position;
                        }
                        block = (block << 5) | v;
                    }
                    std::size_t const bytes = i + 8u == text.size() ? sBytesForPads[pads] : 5u;
                    for (std::size_t k = 0u; k < bytes; ++k)
                        aResult += static_cast<char>(block >> (32u - 8u * k));
                }
                return true;
            }

            char const sBase85Alphabet[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz!#$%&()*+-;<=>?@^_`{|}~";

            std::string base85_encode(std::string const& aData, bool aPad, bool aAscii85)
            {
                std::string result;
                for (std::size_t i = 0u; i < aData.size(); i += 4u)
                {
                    std::size_t const n = std::min<std::size_t>(4u, aData.size() - i);
                    std::uint32_t word = 0u;
                    for (std::size_t k = 0u; k < 4u; ++k)
                        word = (word << 8) | (k < n ? static_cast<unsigned char>(aData[i + k]) : 0u);
                    if (aAscii85 && word == 0u && n == 4u)
                    {
                        result += 'z';
                        continue;
                    }
                    char chunk[5];
                    for (int k = 4; k >= 0; --k)
                    {
                        chunk[k] = aAscii85 ? static_cast<char>('!' + word % 85u) : sBase85Alphabet[word % 85u];
                        word /= 85u;
                    }
                    result.append(chunk, n < 4u && !aPad ? n + 1u : 5u);
                }
                return result;
            }

            bool base85_decode(std::string const& aText, std::string& aResult, bool aAscii85)
            {
                std::vector<int> values;
                for (std::size_t i = 0u; i < aText.size(); ++i)
                {
                    auto const c = aText[i];
                    if (aAscii85)
                    {
                        if (std::isspace(static_cast<unsigned char>(c)))
                            continue;
                        if (c == 'z')
                        {
                            if (values.size() % 5u != 0u)
                            {
                                error("ValueError", "z inside Ascii85 5-tuple");
                                return false;
                            }
                            aResult.append(4u, '\0');
                            continue;
                        }
                        if (c < '!' || c > 'u')
                        {
                            error("ValueError", std::string{ "Non-Ascii85 digit found: " } + c);
                            return false;
                        }
                        values.push_back(c - '!');
                    }
                    else
                    {
                        auto const position = std::strchr(sBase85Alphabet, c);
                        if (position == nullptr || c == '\0')
                        {
                            error("ValueError", "bad base85 character at position " + std::to_string(i));
                            return false;
                        }
                        values.push_back(static_cast<int>(position - sBase85Alphabet));
                    }
                    if (values.size() == 5u)
                    {
                        std::uint64_t word = 0u;
                        for (auto v : values)
                            word = word * 85u + static_cast<unsigned>(v);
                        if (word > 0xFFFFFFFFull)
                        {
                            error("ValueError", aAscii85 ? "Ascii85 overflow" : "base85 overflow in hunk starting at byte " + std::to_string(i - 4u));
                            return false;
                        }
                        for (int k = 3; k >= 0; --k)
                            aResult += static_cast<char>(word >> (8 * k));
                        values.clear();
                    }
                }
                if (!values.empty())
                {
                    auto const n = values.size();
                    while (values.size() < 5u)
                        values.push_back(84);
                    std::uint64_t word = 0u;
                    for (auto v : values)
                        word = word * 85u + static_cast<unsigned>(v);
                    for (std::size_t k = 0u; k + 1u < n; ++k)
                        aResult += static_cast<char>(word >> (24u - 8u * k));
                }
                return true;
            }

            builtin_entry const sBase64Functions[] =
            {
                { "b64encode", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        return parse_arguments("b64encode", aArgs, aKwargs, { "s", "altchars" }, 1u, 0u, a) ? b64encode(a[0], a[1], false) : nullptr;
                    } },
                { "b64decode", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("b64decode", aArgs, aKwargs, { "s", "altchars", "validate" }, 1u, 0u, a))
                            return nullptr;
                        return b64decode(a[0], a[1], a[2] != nullptr && truth(a[2]) == 1, false);
                    } },
                { "standard_b64encode", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    { std::vector<object*> a; return parse_arguments("standard_b64encode", aArgs, aKwargs, { "s" }, 1u, 0u, a) ? b64encode(a[0], nullptr, false) : nullptr; } },
                { "standard_b64decode", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    { std::vector<object*> a; return parse_arguments("standard_b64decode", aArgs, aKwargs, { "s" }, 1u, 0u, a) ? b64decode(a[0], nullptr, false, false) : nullptr; } },
                { "urlsafe_b64encode", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    { std::vector<object*> a; return parse_arguments("urlsafe_b64encode", aArgs, aKwargs, { "s" }, 1u, 0u, a) ? b64encode(a[0], nullptr, true) : nullptr; } },
                { "urlsafe_b64decode", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    { std::vector<object*> a; return parse_arguments("urlsafe_b64decode", aArgs, aKwargs, { "s" }, 1u, 0u, a) ? b64decode(a[0], nullptr, false, true) : nullptr; } },
                { "encodebytes", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        std::string data;
                        if (!parse_arguments("encodebytes", aArgs, aKwargs, { "s" }, 1u, 0u, a) || !binary_argument(a[0], data, false))
                            return nullptr;
                        std::string result;
                        for (std::size_t i = 0u; i < data.size(); i += 57u)
                            result += base64_encode(data.substr(i, 57u)) + "\n";
                        return make_bytes(result);
                    } },
                { "decodebytes", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        std::string text, result;
                        if (!parse_arguments("decodebytes", aArgs, aKwargs, { "s" }, 1u, 0u, a) || !binary_argument(a[0], text, false) || !base64_decode(text, result, false))
                            return nullptr;
                        return make_bytes(result);
                    } },
                { "b32encode", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        std::string data;
                        if (!parse_arguments("b32encode", aArgs, aKwargs, { "s" }, 1u, 0u, a) || !binary_argument(a[0], data, false))
                            return nullptr;
                        return make_bytes(base32_encode(data, false));
                    } },
                { "b32decode", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        std::string text, result;
                        if (!parse_arguments("b32decode", aArgs, aKwargs, { "s", "casefold", "map01" }, 1u, 0u, a) || !binary_argument(a[0], text, true))
                            return nullptr;
                        if (a[2] != nullptr && kind_of(a[2]) != NEOS_PY_NONE)
                        {
                            std::string map;
                            if (!binary_argument(a[2], map, true))
                                return nullptr;
                            text = translate(text, "01", "O" + map);
                        }
                        if (!base32_decode(text, result, a[1] != nullptr && truth(a[1]) == 1, false))
                            return nullptr;
                        return make_bytes(result);
                    } },
                { "b32hexencode", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        std::string data;
                        if (!parse_arguments("b32hexencode", aArgs, aKwargs, { "s" }, 1u, 0u, a) || !binary_argument(a[0], data, false))
                            return nullptr;
                        return make_bytes(base32_encode(data, true));
                    } },
                { "b32hexdecode", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        std::string text, result;
                        if (!parse_arguments("b32hexdecode", aArgs, aKwargs, { "s", "casefold" }, 1u, 0u, a) || !binary_argument(a[0], text, true) ||
                            !base32_decode(text, result, a[1] != nullptr && truth(a[1]) == 1, true))
                            return nullptr;
                        return make_bytes(result);
                    } },
                { "b16encode", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        std::string data;
                        if (!parse_arguments("b16encode", aArgs, aKwargs, { "s" }, 1u, 0u, a) || !binary_argument(a[0], data, false))
                            return nullptr;
                        auto text = hexlify(data, "", 1);
                        for (auto& c : text)
                            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
                        return make_bytes(text);
                    } },
                { "b16decode", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        std::string text, result;
                        if (!parse_arguments("b16decode", aArgs, aKwargs, { "s", "casefold" }, 1u, 0u, a) || !binary_argument(a[0], text, true))
                            return nullptr;
                        if (a[1] != nullptr && truth(a[1]) == 1)
                            for (auto& c : text)
                                c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
                        for (char c : text)
                            if (!(std::isdigit(static_cast<unsigned char>(c)) || (c >= 'A' && c <= 'F')))
                                return binascii_error("Non-base16 digit found");
                        if (!unhexlify(text, result))
                            return nullptr;
                        return make_bytes(result);
                    } },
                { "b85encode", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        std::string data;
                        if (!parse_arguments("b85encode", aArgs, aKwargs, { "b", "pad" }, 1u, 0u, a) || !binary_argument(a[0], data, false))
                            return nullptr;
                        return make_bytes(base85_encode(data, a[1] != nullptr && truth(a[1]) == 1, false));
                    } },
                { "b85decode", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        std::string text, result;
                        if (!parse_arguments("b85decode", aArgs, aKwargs, { "b" }, 1u, 0u, a) || !binary_argument(a[0], text, true) || !base85_decode(text, result, false))
                            return nullptr;
                        return make_bytes(result);
                    } },
                { "a85encode", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        std::string data;
                        if (!parse_arguments("a85encode", aArgs, aKwargs, { "b", "foldspaces", "wrapcol", "pad", "adobe" }, 1u, 0u, a) || !binary_argument(a[0], data, false))
                            return nullptr;
                        auto text = base85_encode(data, a[3] != nullptr && truth(a[3]) == 1, true);
                        if (a[4] != nullptr && truth(a[4]) == 1)
                            text = "<~" + text + "~>";
                        return make_bytes(text);
                    } },
                { "a85decode", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        std::string text, result;
                        if (!parse_arguments("a85decode", aArgs, aKwargs, { "b", "foldspaces", "adobe", "ignorechars" }, 1u, 0u, a) || !binary_argument(a[0], text, true))
                            return nullptr;
                        if (a[2] != nullptr && truth(a[2]) == 1)
                        {
                            if (!text.ends_with("~>"))
                                return error("ValueError", "Ascii85 encoded byte sequences must end with b'~>'");
                            text = text.substr(text.starts_with("<~") ? 2u : 0u);
                            text.resize(text.size() - 2u);
                        }
                        if (!base85_decode(text, result, true))
                            return nullptr;
                        return make_bytes(result);
                    } }
            };

            bool base64_module(std::vector<std::pair<std::string, object*>>& aAttributes)
            {
                for (auto const& f : sBase64Functions)
                    aAttributes.emplace_back(f.name, make_builtin(f));
                return true;
            }

            inline bool const sString = register_module("string", string_module);
            inline bool const sStruct = register_module("struct", struct_module);
            inline bool const sBinascii = register_module("binascii", binascii_module);
            inline bool const sBase64 = register_module("base64", base64_module);
        }
    }
}
