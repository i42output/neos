/*
  locale.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/locale.py), Copyright (c) 2001-2024 Python Software Foundation;
  All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from Python to C++ for the neos Python runtime plugin and adapted to
  its object model (see runtime/python/NOTICE).
*/

// The module locale (CPython 3.13's behaviour and messages): the C library's locales

#include <algorithm>
#include <cctype>
#include <climits>
#include <clocale>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <string>
#include <vector>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <langinfo.h>
#endif
#include "objects.hpp"
#include "locale_tables.hpp"

namespace neos::python_runtime
{
    namespace detail
    {
        namespace
        {
            constexpr int kCharMax = 127;

            std::string lower(std::string aText)
            {
                for (auto& c : aText)
                    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                return aText;
            }

            std::string remove_all(std::string aText, char const* aCharacters)
            {
                aText.erase(std::remove_if(aText.begin(), aText.end(), [&](char c) { return std::strchr(aCharacters, c) != nullptr; }), aText.end());
                return aText;
            }

            std::optional<std::string> alias(std::string const& aName)
            {
                for (auto const& a : locale_tables::sLocaleAlias)
                    if (aName == a.first)
                        return std::string{ a.second };
                return std::nullopt;
            }

            // (encodings.normalize_encoding: runs of characters other than alphanumerics and '.' become '_')
            std::string normalize_encoding(std::string const& aEncoding)
            {
                std::string result;
                bool punctuation = false;
                for (auto c : aEncoding)
                {
                    if (std::isalnum(static_cast<unsigned char>(c)) || c == '.')
                    {
                        if (punctuation && !result.empty())
                            result += '_';
                        result += c;
                        punctuation = false;
                    }
                    else
                        punctuation = true;
                }
                return result;
            }

            std::string replace_encoding(std::string const& aCode, std::string const& aEncoding)
            {
                auto const langname = aCode.substr(0u, aCode.find('.'));
                auto encoding = normalize_encoding(aEncoding);
                auto normalized = lower(encoding);
                bool found = false;
                for (auto const& e : locale_tables::sEncodingAlias)
                    if (normalized == e.first)
                    {
                        encoding = e.second;
                        found = true;
                    }
                if (!found)
                {
                    normalized = remove_all(normalized, "_-");
                    for (auto const& e : locale_tables::sEncodingAlias)
                        if (normalized == e.first)
                            encoding = e.second;
                }
                return langname + "." + encoding;
            }

            std::string append_modifier(std::string const& aCode, std::string const& aModifier)
            {
                if (aModifier == "euro")
                {
                    auto const dot = aCode.find('.');
                    if (dot == std::string::npos)
                        return aCode + ".ISO8859-15";
                    auto const encoding = aCode.substr(dot + 1u);
                    if (encoding == "ISO8859-15" || encoding == "UTF-8")
                        return aCode;
                    if (encoding == "ISO8859-1")
                        return replace_encoding(aCode, "ISO8859-15");
                }
                return aCode + "@" + aModifier;
            }

            // (locale.normalize)
            std::string normalize(std::string const& aName)
            {
                auto code = lower(aName);
                std::replace(code.begin(), code.end(), ':', '.');
                std::string modifier;
                if (auto const at = code.find('@'); at != std::string::npos)
                {
                    modifier = code.substr(at + 1u);
                    code = code.substr(0u, at);
                }
                std::string langname = code, encoding;
                if (auto const dot = code.find('.'); dot != std::string::npos)
                {
                    langname = code.substr(0u, dot);
                    encoding = code.substr(dot + 1u);
                    encoding = encoding.substr(0u, encoding.find('.'));
                }
                auto langEncoding = langname;
                if (!encoding.empty())
                    langEncoding += "." + remove_all(encoding, "-_");
                auto lookup = langEncoding;
                if (!modifier.empty())
                    lookup += "@" + modifier;
                if (auto const c = alias(lookup))
                    return *c;
                if (!modifier.empty())
                    if (auto const c = alias(langEncoding))
                    {
                        if (c->find('@') == std::string::npos)
                            return append_modifier(*c, modifier);
                        if (lower(c->substr(c->find('@') + 1u)) == modifier)
                            return *c;
                    }
                if (!encoding.empty())
                {
                    lookup = langname;
                    if (!modifier.empty())
                        lookup += "@" + modifier;
                    if (auto const c = alias(lookup))
                    {
                        if (c->find('@') == std::string::npos)
                            return replace_encoding(*c, encoding);
                        auto const at = c->find('@');
                        return replace_encoding(c->substr(0u, at), encoding) + "@" + c->substr(at + 1u);
                    }
                    if (!modifier.empty())
                        if (auto const c = alias(langname))
                        {
                            if (c->find('@') == std::string::npos)
                                return append_modifier(replace_encoding(*c, encoding), modifier);
                            auto const at = c->find('@');
                            auto const defaultModifier = c->substr(at + 1u);
                            if (lower(defaultModifier) == modifier)
                                return replace_encoding(c->substr(0u, at), encoding) + "@" + defaultModifier;
                        }
                }
                return aName;
            }

            // (locale._parse_localename: (language code, encoding), None for C)
            bool parse_localename(std::string const& aName, object*& aResult)
            {
                auto code = normalize(aName);
                if (auto const at = code.find('@'); at != std::string::npos)
                {
                    auto const modifier = code.substr(at + 1u);
                    code = code.substr(0u, at);
                    if (modifier == "euro" && code.find('.') == std::string::npos)
                    {
                        aResult = make_tuple({ make_str(code), make_str("iso-8859-15") });
                        return true;
                    }
                }
                if (auto const dot = code.find('.'); dot != std::string::npos)
                {
                    aResult = make_tuple({ make_str(code.substr(0u, dot)), make_str(code.substr(dot + 1u, code.find('.', dot + 1u) - dot - 1u)) });
                    return true;
                }
                if (code == "C")
                {
                    aResult = make_tuple({ new_reference(none()), new_reference(none()) });
                    return true;
                }
                if (code == "UTF-8")
                {
                    // (macOS's LC_CTYPE=UTF-8)
                    aResult = make_tuple({ new_reference(none()), make_str("UTF-8") });
                    return true;
                }
                error("ValueError", "unknown locale: " + aName);
                return false;
            }

            object* locale_error(std::string const& aMessage)
            {
                ref const cls{ module_exception("locale.Error", { "Exception" }) };
                ref const e{ cls ? invoke(cls.get(), { make_str(aMessage) }) : nullptr };
                if (e)
                    raise_exception(e.get());
                return nullptr;
            }

            bool category_of(object* aValue, int& aCategory)
            {
                long long c = 0;
                if (!to_index(aValue, c))
                    return false;
                aCategory = static_cast<int>(c);
                return true;
            }

            object* grouping_list(char const* aGrouping)
            {
                std::vector<object*> items;
                for (auto p = aGrouping; p != nullptr && *p != '\0'; ++p)
                {
                    items.push_back(make_int(static_cast<unsigned char>(*p) == CHAR_MAX ? kCharMax : *p));
                    if (static_cast<unsigned char>(*p) == CHAR_MAX)
                        break;
                }
                // (a final 0 repeats the last group)
                if (aGrouping != nullptr && *aGrouping != '\0' && static_cast<unsigned char>(aGrouping[std::strlen(aGrouping) - 1u]) != CHAR_MAX)
                    items.push_back(make_int(0));
                return make_list(std::move(items));
            }

            object* localeconv_dict()
            {
                auto const* lc = std::localeconv();
                ref result{ make_dict() };
                auto const set = [&](char const* aKey, object* aValue)
                {
                    ref const key{ make_str(aKey) };
                    ref const value{ aValue };
                    dict_set(result.get(), key.get(), value.get());
                };
                auto const number = [](char aValue) { return make_int(static_cast<unsigned char>(aValue) == CHAR_MAX ? kCharMax : aValue); };
                set("int_curr_symbol", make_str(lc->int_curr_symbol));
                set("currency_symbol", make_str(lc->currency_symbol));
                set("mon_decimal_point", make_str(lc->mon_decimal_point));
                set("mon_thousands_sep", make_str(lc->mon_thousands_sep));
                set("mon_grouping", grouping_list(lc->mon_grouping));
                set("positive_sign", make_str(lc->positive_sign));
                set("negative_sign", make_str(lc->negative_sign));
                set("int_frac_digits", number(lc->int_frac_digits));
                set("frac_digits", number(lc->frac_digits));
                set("p_cs_precedes", number(lc->p_cs_precedes));
                set("p_sep_by_space", number(lc->p_sep_by_space));
                set("n_cs_precedes", number(lc->n_cs_precedes));
                set("n_sep_by_space", number(lc->n_sep_by_space));
                set("p_sign_posn", number(lc->p_sign_posn));
                set("n_sign_posn", number(lc->n_sign_posn));
                set("decimal_point", make_str(lc->decimal_point));
                set("thousands_sep", make_str(lc->thousands_sep));
                set("grouping", grouping_list(lc->grouping));
                return result.release_ownership();
            }

            std::string conv_text(char const* aKey)
            {
                ref const conv{ localeconv_dict() };
                ref const key{ make_str(aKey) };
                auto const at = dict_find(conv.get(), key.get());
                return at < 0 ? std::string{} : text_of(as<dict_object>(conv.get())->items[static_cast<std::size_t>(at)].second);
            }

            std::vector<long long> conv_grouping(bool aMonetary)
            {
                ref const conv{ localeconv_dict() };
                ref const key{ make_str(aMonetary ? "mon_grouping" : "grouping") };
                auto const at = dict_find(conv.get(), key.get());
                std::vector<long long> result;
                if (at >= 0)
                    for (auto i : as<list_object>(as<dict_object>(conv.get())->items[static_cast<std::size_t>(at)].second)->items)
                    {
                        long long n = 0;
                        to_index(i, n);
                        result.push_back(n);
                    }
                return result;
            }

            // (locale._group: the digits grouped from the right (and the number of separators added))
            bool group(std::string aText, bool aMonetary, std::string& aResult, std::size_t& aSeparators)
            {
                auto const separator = conv_text(aMonetary ? "mon_thousands_sep" : "thousands_sep");
                auto const grouping = conv_grouping(aMonetary);
                aSeparators = 0u;
                if (grouping.empty())
                {
                    aResult = aText;
                    return true;
                }
                std::string rightSpaces, leftSpaces;
                if (!aText.empty() && aText.back() == ' ')
                {
                    auto const end = aText.find_last_not_of(' ');
                    rightSpaces = aText.substr(end == std::string::npos ? 0u : end + 1u);
                    aText = aText.substr(0u, end == std::string::npos ? 0u : end + 1u);
                }
                std::vector<std::string> groups;
                std::optional<long long> last;
                std::size_t gi = 0u;
                for (;;)
                {
                    long long interval = 0;
                    if (gi < grouping.size())
                    {
                        interval = grouping[gi++];
                        if (interval == kCharMax)
                            break;
                        if (interval == 0)
                        {
                            if (!last)
                                return (error("ValueError", "invalid grouping"), false);
                            interval = *last;
                            --gi;
                        }
                    }
                    else
                        break;
                    if (aText.empty() || !std::isdigit(static_cast<unsigned char>(aText.back())))
                    {
                        leftSpaces = aText;
                        aText.clear();
                        break;
                    }
                    auto const n = std::min(static_cast<std::size_t>(interval), aText.size());
                    groups.push_back(aText.substr(aText.size() - n));
                    aText.erase(aText.size() - n);
                    last = interval;
                }
                if (!aText.empty())
                    groups.push_back(aText);
                std::reverse(groups.begin(), groups.end());
                std::string joined;
                for (std::size_t i = 0u; i < groups.size(); ++i)
                    joined += (i == 0u ? "" : separator) + groups[i];
                aResult = leftSpaces + joined + rightSpaces;
                aSeparators = separator.size() * (groups.empty() ? 0u : groups.size() - 1u);
                return true;
            }

            std::string strip_padding(std::string const& aText, std::size_t aAmount)
            {
                std::size_t left = 0u;
                while (aAmount > 0u && left < aText.size() && aText[left] == ' ')
                {
                    ++left;
                    --aAmount;
                }
                auto right = aText.size();
                while (aAmount > 0u && right > left && aText[right - 1u] == ' ')
                {
                    --right;
                    --aAmount;
                }
                return aText.substr(left, right - left);
            }

            // (locale._localize)
            bool localize(std::string const& aFormatted, bool aGrouping, bool aMonetary, std::string& aResult)
            {
                std::size_t separators = 0u;
                if (auto const dot = aFormatted.find('.'); dot != std::string::npos)
                {
                    auto whole = aFormatted.substr(0u, dot);
                    if (aGrouping && !group(whole, aMonetary, whole, separators))
                        return false;
                    // (split('.'): the parts joined with the decimal point)
                    auto const point = conv_text(aMonetary ? "mon_decimal_point" : "decimal_point");
                    std::string rest = aFormatted.substr(dot + 1u);
                    std::string joined = whole;
                    std::size_t start = 0u;
                    for (;;)
                    {
                        auto const next = rest.find('.', start);
                        joined += point + rest.substr(start, next == std::string::npos ? std::string::npos : next - start);
                        if (next == std::string::npos)
                            break;
                        start = next + 1u;
                    }
                    aResult = separators > 0u ? strip_padding(joined, separators) : joined;
                    return true;
                }
                aResult = aFormatted;
                if (aGrouping && !group(aFormatted, aMonetary, aResult, separators))
                    return false;
                if (separators > 0u)
                    aResult = strip_padding(aResult, separators);
                return true;
            }

            object* percent(std::string const& aFormat, object* aValues)
            {
                ref const format{ make_str(aFormat) };
                return binary(NEOS_PY_REMAINDER, format.get(), aValues, false);
            }

            // (locale.format_string(f, val, grouping=False, monetary=False))
            object* format_string(std::string const& aFormat, object* aValue, bool aGrouping, bool aMonetary)
            {
                // (the conversions: %[(key)][modifiers]type)
                struct conversion { std::string text; std::size_t stars = 0u; };
                std::vector<conversion> conversions;
                std::string pattern;
                for (std::size_t i = 0u; i < aFormat.size();)
                {
                    if (aFormat[i] != '%')
                    {
                        pattern += aFormat[i++];
                        continue;
                    }
                    auto j = i + 1u;
                    if (j < aFormat.size() && aFormat[j] == '(')
                    {
                        auto const close = aFormat.find(')', j);
                        if (close == std::string::npos)
                        {
                            pattern += aFormat.substr(i);
                            break;
                        }
                        j = close + 1u;
                    }
                    std::size_t stars = 0u;
                    while (j < aFormat.size() && std::strchr("-#0123456789 +*.hlL", aFormat[j]) != nullptr)
                        stars += aFormat[j++] == '*' ? 1u : 0u;
                    if (j < aFormat.size() && std::strchr("eEfFgGdiouxXcrs%", aFormat[j]) != nullptr)
                    {
                        conversions.push_back({ aFormat.substr(i, j + 1u - i), stars });
                        pattern += "%s";
                        i = j + 1u;
                    }
                    else
                        pattern += aFormat[i++];
                }
                std::vector<object*> values;
                auto const formatOne = [&](conversion const& c, object* aArgument) -> bool
                {
                    ref const formatted{ percent(c.text, aArgument) };
                    if (!formatted)
                        return false;
                    std::string text = text_of(formatted.get());
                    if (std::strchr("eEfFgGdiu", c.text.back()) != nullptr && !localize(text, aGrouping, aMonetary, text))
                        return false;
                    values.push_back(make_str(text));
                    return true;
                };
                bool const mapping = kind_of(aValue) == NEOS_PY_DICT || (kind_of(aValue) == NEOS_PY_OTHER && python_type(aValue) != nullptr &&
                    python_type(aValue)->getitem != nullptr && kind_of(aValue) != NEOS_PY_TUPLE);
                if (mapping)
                {
                    for (auto const& c : conversions)
                        if (c.text.back() == '%')
                            values.push_back(make_str("%"));
                        else if (!formatOne(c, aValue))
                            return (std::for_each(values.begin(), values.end(), release), nullptr);
                }
                else
                {
                    std::vector<object*> const single{ aValue };
                    auto const& items = kind_of(aValue) == NEOS_PY_TUPLE ? as<tuple_object>(aValue)->items : single;
                    std::size_t i = 0u;
                    for (auto const& c : conversions)
                    {
                        if (c.text.back() == '%')
                        {
                            values.push_back(make_str("%"));
                            continue;
                        }
                        if (i >= items.size())
                        {
                            std::for_each(values.begin(), values.end(), release);
                            return error("IndexError", "tuple index out of range");
                        }
                        ref argument;
                        if (c.stars == 0u)
                            argument = ref{ new_reference(items[i]) };
                        else
                        {
                            std::vector<object*> group{ new_reference(items[i]) };
                            for (std::size_t s2 = 0u; s2 < c.stars && i + 1u + s2 < items.size(); ++s2)
                                group.push_back(new_reference(items[i + 1u + s2]));
                            // (the * values first, then the value: as % takes them)
                            std::rotate(group.begin(), group.begin() + 1, group.end());
                            argument = ref{ make_tuple(std::move(group)) };
                        }
                        if (!formatOne(c, argument.get()))
                            return (std::for_each(values.begin(), values.end(), release), nullptr);
                        i += 1u + c.stars;
                    }
                }
                ref const tuple{ make_tuple(std::move(values)) };
                return percent(pattern, tuple.get());
            }

            // (delocalize: the thousands separators removed, the decimal point made '.')
            std::string delocalize(std::string aText)
            {
                auto const separator = conv_text("thousands_sep");
                if (!separator.empty())
                    for (std::size_t at; (at = aText.find(separator)) != std::string::npos;)
                        aText.erase(at, separator.size());
                auto const point = conv_text("decimal_point");
                if (!point.empty() && point != ".")
                    for (std::size_t at; (at = aText.find(point)) != std::string::npos;)
                        aText.replace(at, point.size(), ".");
                return aText;
            }

            // (setlocale(category, locale=None))
            object* set_locale(int aCategory, object* aLocale)
            {
                char const* result = nullptr;
                if (aLocale == nullptr || kind_of(aLocale) == NEOS_PY_NONE)
                    result = std::setlocale(aCategory, nullptr);
                else
                {
                    std::string name;
                    if (auto const s = str_of_value(aLocale))
                        name = *s;
                    else
                    {
                        // (an iterable of (language code, encoding): locale._build_localename)
                        std::vector<object*> items;
                        if (!collect(aLocale, items))
                            return nullptr;
                        if (items.size() != 2u)
                        {
                            std::for_each(items.begin(), items.end(), release);
                            return error("TypeError", "Locale must be None, a string, or an iterable of two strings -- language code, encoding.");
                        }
                        ref const language{ items[0] };
                        ref const encoding{ items[1] };
                        if (kind_of(language.get()) == NEOS_PY_NONE)
                            name = "C";
                        else
                        {
                            name = text_of(language.get());
                            if (kind_of(encoding.get()) != NEOS_PY_NONE)
                            {
                                auto const at = name.find('@');
                                name = at == std::string::npos ? name + "." + text_of(encoding.get()) :
                                    name.substr(0u, at) + "." + text_of(encoding.get()) + name.substr(at);
                            }
                        }
                        name = normalize(name);
                    }
                    if (aCategory < 0 || aCategory > 12)
                        return locale_error("unsupported locale setting");
                    result = std::setlocale(aCategory, name.c_str());
                }
                if (result == nullptr)
                    return locale_error(aLocale == nullptr || kind_of(aLocale) == NEOS_PY_NONE ? "locale query failed" : "unsupported locale setting");
                return make_str(result);
            }

            builtin_entry const sLocaleFunctions[] =
            {
                { "setlocale", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        int category = 0;
                        if (!parse_arguments("setlocale", aArgs, aKwargs, { "category", "locale" }, 1u, 0u, a) || !category_of(a[0], category))
                            return nullptr;
                        return set_locale(category, a[1]);
                    } },
                { "getlocale", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        int category = LC_CTYPE;
                        if (!parse_arguments("getlocale", aArgs, aKwargs, { "category" }, 0u, 0u, a) || (a[0] != nullptr && !category_of(a[0], category)))
                            return nullptr;
                        if (category == LC_ALL)
                            return error("TypeError", "category LC_ALL is not supported");
                        auto const name = std::setlocale(category, nullptr);
                        object* result = nullptr;
                        return parse_localename(name != nullptr ? name : "C", result) ? result : nullptr;
                    } },
                { "getdefaultlocale", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("getdefaultlocale", aArgs, aKwargs, { "envvars" }, 0u, 0u, a))
                            return nullptr;
                        std::vector<std::string> names{ "LC_ALL", "LC_CTYPE", "LANG", "LANGUAGE" };
                        if (a[0] != nullptr)
                        {
                            names.clear();
                            std::vector<object*> items;
                            if (!collect(a[0], items))
                                return nullptr;
                            for (auto i : items)
                            {
                                names.push_back(text_of(i));
                                release(i);
                            }
                        }
                        std::string localename = "C";
                        for (auto const& n : names)
                            if (auto const v = std::getenv(n.c_str()); v != nullptr && *v != '\0')
                            {
                                localename = v;
                                if (n == "LANGUAGE")
                                    localename = localename.substr(0u, localename.find(':'));
                                break;
                            }
                        object* result = nullptr;
                        return parse_localename(localename, result) ? result : nullptr;
                    } },
                { "getencoding", [](std::vector<object*> const&, object*) -> object*
                    {
#if defined(_WIN32)
                        return make_str("cp" + std::to_string(::GetACP()));
#else
                        auto const codeset = ::nl_langinfo(CODESET);
                        std::string const c = codeset != nullptr && *codeset != '\0' ? codeset : "UTF-8";
                        return make_str(c == "ANSI_X3.4-1968" ? std::string{ "UTF-8" } : c);
#endif
                    } },
                { "getpreferredencoding", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("getpreferredencoding", aArgs, aKwargs, { "do_setlocale" }, 0u, 0u, a))
                            return nullptr;
                        return make_str("utf-8");   // (the UTF-8 mode)
                    } },
                { "localeconv", [](std::vector<object*> const&, object*) -> object* { return localeconv_dict(); } },
                { "normalize", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("normalize", aArgs, aKwargs, { "localename" }, 1u, 0u, a))
                            return nullptr;
                        auto const s = str_of_value(a[0]);
                        if (s == nullptr)
                            return error("AttributeError", std::string{ "'" } + type_name(a[0]) + "' object has no attribute 'lower'");
                        return make_str(normalize(*s));
                    } },
                { "format_string", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("format_string", aArgs, aKwargs, { "f", "val", "grouping", "monetary" }, 2u, 0u, a))
                            return nullptr;
                        auto const f = str_of_value(a[0]);
                        if (f == nullptr)
                            return error("TypeError", "expected string or bytes-like object");
                        return format_string(*f, a[1], a[2] != nullptr && truth(a[2]) == 1, a[3] != nullptr && truth(a[3]) == 1);
                    } },
                { "str", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("str", aArgs, aKwargs, { "val" }, 1u, 0u, a))
                            return nullptr;
                        return format_string("%.12g", a[0], false, false);
                    } },
                { "currency", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("currency", aArgs, aKwargs, { "val", "symbol", "grouping", "international" }, 1u, 0u, a))
                            return nullptr;
                        bool const symbol = a[1] == nullptr || truth(a[1]) == 1;
                        bool const grouping = a[2] != nullptr && truth(a[2]) == 1;
                        bool const international = a[3] != nullptr && truth(a[3]) == 1;
                        ref const conv{ localeconv_dict() };
                        auto const field = [&](char const* aKey) -> object*
                        {
                            ref const key{ make_str(aKey) };
                            auto const at = dict_find(conv.get(), key.get());
                            return at < 0 ? none() : as<dict_object>(conv.get())->items[static_cast<std::size_t>(at)].second;
                        };
                        auto const number = [&](char const* aKey) { long long n = 0; to_index(field(aKey), n); return n; };
                        auto const digits = number(international ? "int_frac_digits" : "frac_digits");
                        if (digits == kCharMax)
                            return error("ValueError", "Currency formatting is not possible using the 'C' locale.");
                        double value = 0.0;
                        if (!to_double(a[0], value))
                            return nullptr;
                        bool const negative = value < 0.0;
                        char buffer[64];
                        std::snprintf(buffer, sizeof(buffer), "%.*f", static_cast<int>(digits), std::fabs(value));
                        std::string s;
                        if (!localize(buffer, grouping, true, s))
                            return nullptr;
                        s = "<" + s + ">";
                        if (symbol)
                        {
                            auto smb = text_of(field(international ? "int_curr_symbol" : "currency_symbol"));
                            bool const precedes = number(negative ? "n_cs_precedes" : "p_cs_precedes") != 0;
                            bool const separated = number(negative ? "n_sep_by_space" : "p_sep_by_space") != 0;
                            if (precedes)
                                s = smb + (separated ? " " : "") + s;
                            else
                            {
                                if (international && !smb.empty() && smb.back() == ' ')
                                    smb.pop_back();
                                s = s + (separated ? " " : "") + smb;
                            }
                        }
                        auto const position = number(negative ? "n_sign_posn" : "p_sign_posn");
                        auto const sign = text_of(field(negative ? "negative_sign" : "positive_sign"));
                        auto const replace = [](std::string aText, char aFrom, std::string const& aTo)
                        {
                            for (std::size_t at; (at = aText.find(aFrom)) != std::string::npos;)
                                aText.replace(at, 1u, aTo);
                            return aText;
                        };
                        switch (position)
                        {
                        case 0: s = "(" + s + ")"; break;
                        case 1: s = sign + s; break;
                        case 2: s = s + sign; break;
                        case 3: s = replace(s, '<', sign); break;
                        case 4: s = replace(s, '>', sign); break;
                        default: s = sign + s; break;
                        }
                        return make_str(replace(replace(s, '<', ""), '>', ""));
                    } },
                { "delocalize", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("delocalize", aArgs, aKwargs, { "string" }, 1u, 0u, a))
                            return nullptr;
                        return make_str(delocalize(text_of(a[0])));
                    } },
                { "localize", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("localize", aArgs, aKwargs, { "string", "grouping", "monetary" }, 1u, 0u, a))
                            return nullptr;
                        std::string result;
                        return localize(text_of(a[0]), a[1] != nullptr && truth(a[1]) == 1, a[2] != nullptr && truth(a[2]) == 1, result) ? make_str(result) : nullptr;
                    } },
                { "atof", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("atof", aArgs, aKwargs, { "string", "func" }, 1u, 0u, a))
                            return nullptr;
                        ref const function{ a[1] != nullptr ? new_reference(a[1]) : fn_builtin("float") };
                        return function ? invoke(function.get(), { make_str(delocalize(text_of(a[0]))) }) : nullptr;
                    } },
                { "atoi", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("atoi", aArgs, aKwargs, { "string" }, 1u, 0u, a))
                            return nullptr;
                        ref const function{ fn_builtin("int") };
                        return function ? invoke(function.get(), { make_str(delocalize(text_of(a[0]))) }) : nullptr;
                    } },
                { "strcoll", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("strcoll", aArgs, aKwargs, { "os1", "os2" }, 2u, 2u, a))
                            return nullptr;
                        auto const x = str_of_value(a[0]);
                        auto const y = str_of_value(a[1]);
                        if (x == nullptr || y == nullptr)
                            return error("TypeError", std::string{ "strcoll() argument " } + (x == nullptr ? "1" : "2") + " must be str, not " + type_name(x == nullptr ? a[0] : a[1]));
                        auto const r = std::strcoll(x->c_str(), y->c_str());
                        return make_int(r < 0 ? -1 : r > 0 ? 1 : 0);
                    } },
                { "strxfrm", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("strxfrm", aArgs, aKwargs, { "string" }, 1u, 1u, a))
                            return nullptr;
                        auto const x = str_of_value(a[0]);
                        if (x == nullptr)
                            return error("TypeError", std::string{ "strxfrm() argument must be str, not " } + type_name(a[0]));
                        std::string buffer(std::strxfrm(nullptr, x->c_str(), 0u) + 1u, '\0');
                        auto const n = std::strxfrm(buffer.data(), x->c_str(), buffer.size());
                        buffer.resize(n);
                        return make_str(buffer);
                    } },
#if !defined(_WIN32)
                { "nl_langinfo", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        long long key = 0;
                        if (!parse_arguments("nl_langinfo", aArgs, aKwargs, { "key" }, 1u, 1u, a) || !to_index(a[0], key))
                            return nullptr;
                        auto const r = ::nl_langinfo(static_cast<nl_item>(key));
                        return make_str(r != nullptr ? r : "");
                    } },
#endif
                { "gettext", [](std::vector<object*> const& aArgs, object*) -> object* { return aArgs.size() == 1u ? new_reference(aArgs[0]) : error("TypeError", "gettext() takes exactly 1 argument"); } },
                { "dgettext", [](std::vector<object*> const& aArgs, object*) -> object* { return aArgs.size() == 2u ? new_reference(aArgs[1]) : error("TypeError", "dgettext() takes exactly 2 arguments"); } },
                { "dcgettext", [](std::vector<object*> const& aArgs, object*) -> object* { return aArgs.size() == 3u ? new_reference(aArgs[1]) : error("TypeError", "dcgettext() takes exactly 3 arguments"); } },
                { "textdomain", [](std::vector<object*> const& aArgs, object*) -> object* { return make_str(aArgs.size() == 1u && kind_of(aArgs[0]) == NEOS_PY_STR ? text_of(aArgs[0]) : "messages"); } },
                { "bindtextdomain", [](std::vector<object*> const& aArgs, object*) -> object* { return make_str(aArgs.size() == 2u ? text_of(aArgs[1]) : ""); } },
                { "bind_textdomain_codeset", [](std::vector<object*> const&, object*) -> object* { return new_reference(none()); } }
            };

            bool locale_module(std::vector<std::pair<std::string, object*>>& aAttributes)
            {
                // (as CPython's start: LC_CTYPE is the user's locale)
                static bool const sSet = []()
                {
                    auto const set = std::setlocale(LC_CTYPE, "");
#if !defined(_WIN32)
                    // (the C locale is coerced to a UTF-8 locale (PEP 538))
                    if (set == nullptr || std::strcmp(set, "C") == 0 || std::strcmp(set, "POSIX") == 0)
                        for (auto target : { "C.UTF-8", "C.utf8", "UTF-8" })
                            if (std::setlocale(LC_CTYPE, target) != nullptr)
                                break;
#else
                    (void)set;
#endif
                    return true;
                }();
                (void)sSet;
                for (auto const& f : sLocaleFunctions)
                    aAttributes.emplace_back(f.name, make_builtin(f));
                aAttributes.emplace_back("Error", module_exception("locale.Error", { "Exception" }));
                aAttributes.emplace_back("CHAR_MAX", make_int(kCharMax));
                for (auto const& [name, value] : { std::pair{ "LC_CTYPE", LC_CTYPE }, std::pair{ "LC_COLLATE", LC_COLLATE }, std::pair{ "LC_TIME", LC_TIME },
                    std::pair{ "LC_MONETARY", LC_MONETARY }, std::pair{ "LC_NUMERIC", LC_NUMERIC }, std::pair{ "LC_ALL", LC_ALL } })
                    aAttributes.emplace_back(name, make_int(value));
#if !defined(_WIN32)
                aAttributes.emplace_back("LC_MESSAGES", make_int(LC_MESSAGES));
                std::pair<char const*, long long> const items[] = { { "CODESET", CODESET }, { "D_T_FMT", D_T_FMT }, { "D_FMT", D_FMT }, { "T_FMT", T_FMT },
                    { "T_FMT_AMPM", T_FMT_AMPM }, { "AM_STR", AM_STR }, { "PM_STR", PM_STR }, { "DAY_1", DAY_1 }, { "DAY_2", DAY_2 }, { "DAY_3", DAY_3 },
                    { "DAY_4", DAY_4 }, { "DAY_5", DAY_5 }, { "DAY_6", DAY_6 }, { "DAY_7", DAY_7 }, { "ABDAY_1", ABDAY_1 }, { "ABDAY_2", ABDAY_2 },
                    { "ABDAY_3", ABDAY_3 }, { "ABDAY_4", ABDAY_4 }, { "ABDAY_5", ABDAY_5 }, { "ABDAY_6", ABDAY_6 }, { "ABDAY_7", ABDAY_7 }, { "MON_1", MON_1 },
                    { "MON_2", MON_2 }, { "MON_3", MON_3 }, { "MON_4", MON_4 }, { "MON_5", MON_5 }, { "MON_6", MON_6 }, { "MON_7", MON_7 }, { "MON_8", MON_8 },
                    { "MON_9", MON_9 }, { "MON_10", MON_10 }, { "MON_11", MON_11 }, { "MON_12", MON_12 }, { "ABMON_1", ABMON_1 }, { "ABMON_2", ABMON_2 },
                    { "ABMON_3", ABMON_3 }, { "ABMON_4", ABMON_4 }, { "ABMON_5", ABMON_5 }, { "ABMON_6", ABMON_6 }, { "ABMON_7", ABMON_7 }, { "ABMON_8", ABMON_8 },
                    { "ABMON_9", ABMON_9 }, { "ABMON_10", ABMON_10 }, { "ABMON_11", ABMON_11 }, { "ABMON_12", ABMON_12 }, { "RADIXCHAR", RADIXCHAR },
                    { "THOUSEP", THOUSEP }, { "YESEXPR", YESEXPR }, { "NOEXPR", NOEXPR }, { "CRNCYSTR", CRNCYSTR }, { "ERA", ERA }, { "ERA_D_FMT", ERA_D_FMT },
                    { "ERA_D_T_FMT", ERA_D_T_FMT }, { "ERA_T_FMT", ERA_T_FMT }, { "ALT_DIGITS", ALT_DIGITS } };
                for (auto const& i : items)
                    aAttributes.emplace_back(i.first, make_int(i.second));
#endif
                // (the tables)
                ref const aliases{ make_dict() };
                for (auto const& a : locale_tables::sLocaleAlias)
                    dict_set(aliases.get(), ref{ make_str(a.first) }.get(), ref{ make_str(a.second) }.get());
                aAttributes.emplace_back("locale_alias", new_reference(aliases.get()));
                ref const encodings{ make_dict() };
                for (auto const& e : locale_tables::sEncodingAlias)
                    dict_set(encodings.get(), ref{ make_str(e.first) }.get(), ref{ make_str(e.second) }.get());
                aAttributes.emplace_back("locale_encoding_alias", new_reference(encodings.get()));
                ref const windows{ make_dict() };
                for (auto const& w : locale_tables::sWindowsLocale)
                    dict_set(windows.get(), ref{ make_int(w.first) }.get(), ref{ make_str(w.second) }.get());
                aAttributes.emplace_back("windows_locale", new_reference(windows.get()));
                aAttributes.emplace_back("__all__", make_list({ make_str("getlocale"), make_str("getdefaultlocale"), make_str("getpreferredencoding"), make_str("Error"),
                    make_str("setlocale"), make_str("localeconv"), make_str("strcoll"), make_str("strxfrm"), make_str("str"), make_str("atof"), make_str("atoi"),
                    make_str("format_string"), make_str("currency"), make_str("normalize"), make_str("LC_CTYPE"), make_str("LC_COLLATE"), make_str("LC_TIME"),
                    make_str("LC_MONETARY"), make_str("LC_NUMERIC"), make_str("LC_ALL"), make_str("CHAR_MAX"), make_str("getencoding") }));
                return true;
            }

            inline bool const sLocale = register_module("locale", locale_module);
        }
    }
}
