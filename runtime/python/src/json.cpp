/*
  json.cpp

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

  Portions of this file are derived from CPython 3.13 (Modules/_json.c), Copyright (c) 2001-2024 Python Software
  Foundation; All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from C to C++ for the neos Python runtime plugin and adapted to its
  object model (see runtime/python/NOTICE).
*/

// The module json (CPython 3.13's encoder and decoder: their output, options and messages)

#include <algorithm>
#include <cmath>
#include <cstring>
#include <set>
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

            // --- the encoder

            struct encoder_options
            {
                bool skipkeys = false;
                bool ensureAscii = true;
                bool checkCircular = true;
                bool allowNan = true;
                bool sortKeys = false;
                bool hasIndent = false;
                std::string indent;
                std::string itemSeparator = ", ";
                std::string keySeparator = ": ";
                object* defaultFunction = nullptr;  ///< (borrowed: called for an object that is not serializable)
            };

            std::string encode_string(std::string const& aText, bool aEnsureAscii)
            {
                std::string result = "\"";
                auto const append_escape = [&](char32_t c)
                {
                    char buffer[8];
                    std::snprintf(buffer, sizeof(buffer), "\\u%04x", static_cast<unsigned>(c));
                    result += buffer;
                };
                for (auto c : to_code_points(aText))
                {
                    switch (c)
                    {
                    case U'"': result += "\\\""; break;
                    case U'\\': result += "\\\\"; break;
                    case U'\n': result += "\\n"; break;
                    case U'\r': result += "\\r"; break;
                    case U'\t': result += "\\t"; break;
                    case U'\b': result += "\\b"; break;
                    case U'\f': result += "\\f"; break;
                    default:
                        if (c < 0x20u)
                            append_escape(c);
                        else if (aEnsureAscii && c >= 0x7Fu)
                        {
                            if (c >= 0x10000u)
                            {
                                auto const v = c - 0x10000u;
                                append_escape(0xD800u + (v >> 10));
                                append_escape(0xDC00u + (v & 0x3FFu));
                            }
                            else
                                append_escape(c);
                        }
                        else
                            append_code_point(result, c);
                    }
                }
                return result + "\"";
            }

            class encoder
            {
            public:
                explicit encoder(encoder_options const& aOptions, object* aSelf = nullptr) : iOptions{ aOptions }, iSelf{ aSelf } {}
            public:
                bool encode(object* aObject, std::string& aResult, std::size_t aLevel)
                {
                    auto const value = builtin_value(aObject);
                    switch (kind_of(value))
                    {
                    case NEOS_PY_NONE: aResult += "null"; return true;
                    case NEOS_PY_BOOL: aResult += value == make_bool(true) ? "true" : "false"; return true;
                    case NEOS_PY_STR: aResult += encode_string(as<str_object>(value)->value, iOptions.ensureAscii); return true;
                    case NEOS_PY_INT:
                        {
                            std::string text;
                            if (!repr_of(value, text))
                                return false;
                            aResult += text;
                            return true;
                        }
                    case NEOS_PY_FLOAT: return encode_float(as<float_object>(value)->value, aResult);
                    case NEOS_PY_LIST: case NEOS_PY_TUPLE:
                        return encode_list(aObject, kind_of(value) == NEOS_PY_LIST ? as<list_object>(value)->items : as<tuple_object>(value)->items, aResult, aLevel);
                    case NEOS_PY_DICT: return encode_dict(aObject, value, aResult, aLevel);
                    default:
                        break;
                    }
                    // (default(o): its result is encoded)
                    if (!enter(aObject))
                        return false;
                    ref const converted{ call_default(aObject) };
                    if (!converted)
                        return false;
                    auto const ok = encode(converted.get(), aResult, aLevel);
                    leave(aObject);
                    return ok;
                }
            private:
                object* call_default(object* aObject)
                {
                    if (iSelf != nullptr)
                    {
                        ref const method{ getattr(iSelf, "default") };
                        if (!method)
                            return nullptr;
                        return call_with(method.get(), { borrowed(aObject) });
                    }
                    if (iOptions.defaultFunction != nullptr && kind_of(iOptions.defaultFunction) != NEOS_PY_NONE)
                        return call_with(iOptions.defaultFunction, { borrowed(aObject) });
                    return not_serializable(aObject);
                }
            public:
                static object* not_serializable(object* aObject)
                {
                    ref const cls{ class_of(aObject) };
                    ref const name{ getattr(cls.get(), "__name__") };
                    clear_error();
                    std::string const n = name && kind_of(name.get()) == NEOS_PY_STR ? as<str_object>(name.get())->value : type_name(aObject);
                    return error("TypeError", "Object of type " + n + " is not JSON serializable");
                }
            private:
                bool encode_float(double aValue, std::string& aResult)
                {
                    if (std::isfinite(aValue))
                    {
                        std::string text;
                        ref const f{ make_float(aValue) };
                        if (!repr_of(f.get(), text))
                            return false;
                        aResult += text;
                        return true;
                    }
                    if (!iOptions.allowNan)
                    {
                        ref const f{ make_float(aValue) };
                        std::string text;
                        repr_of(f.get(), text);
                        error("ValueError", "Out of range float values are not JSON compliant: " + text);
                        return false;
                    }
                    aResult += std::isnan(aValue) ? "NaN" : aValue > 0.0 ? "Infinity" : "-Infinity";
                    return true;
                }
                bool enter(object* aObject)
                {
                    if (!iOptions.checkCircular)
                        return true;
                    if (!iMarkers.insert(aObject).second)
                    {
                        error("ValueError", "Circular reference detected");
                        return false;
                    }
                    return true;
                }
                void leave(object* aObject) { iMarkers.erase(aObject); }
                std::string newline(std::size_t aLevel) const
                {
                    std::string text = "\n";
                    for (std::size_t i = 0u; i < aLevel; ++i)
                        text += iOptions.indent;
                    return text;
                }
                bool encode_list(object* aObject, std::vector<object*> aItems, std::string& aResult, std::size_t aLevel)
                {
                    if (aItems.empty())
                    {
                        aResult += "[]";
                        return true;
                    }
                    if (!enter(aObject))
                        return false;
                    std::vector<ref> owned;
                    for (auto i : aItems)
                        owned.push_back(ref::borrowed(i));
                    aResult += "[";
                    for (std::size_t i = 0u; i < aItems.size(); ++i)
                    {
                        if (iOptions.hasIndent)
                            aResult += (i != 0u ? iOptions.itemSeparator : std::string{}) + newline(aLevel + 1u);
                        else if (i != 0u)
                            aResult += iOptions.itemSeparator;
                        if (!encode(aItems[i], aResult, aLevel + 1u))
                            return false;
                    }
                    if (iOptions.hasIndent)
                        aResult += newline(aLevel);
                    aResult += "]";
                    leave(aObject);
                    return true;
                }
                bool encode_dict(object* aObject, object* aDict, std::string& aResult, std::size_t aLevel)
                {
                    auto items = as<dict_object>(aDict)->items;
                    if (items.empty())
                    {
                        aResult += "{}";
                        return true;
                    }
                    if (!enter(aObject))
                        return false;
                    std::vector<ref> owned;
                    for (auto const& i : items)
                    {
                        owned.push_back(ref::borrowed(i.first));
                        owned.push_back(ref::borrowed(i.second));
                    }
                    if (iOptions.sortKeys)
                    {
                        ref const list{ call_method_named(aDict, "items", {}) };
                        ref const sorted{ list ? b_sorted({ list.get() }, nullptr) : nullptr };
                        if (!sorted)
                            return false;
                        items.clear();
                        for (auto pair : as<list_object>(sorted.get())->items)
                        {
                            auto const& p = as<tuple_object>(pair)->items;
                            items.emplace_back(p[0], p[1]);
                            owned.push_back(ref::borrowed(p[0]));
                            owned.push_back(ref::borrowed(p[1]));
                        }
                    }
                    aResult += "{";
                    bool first = true;
                    for (auto const& item : items)
                    {
                        std::string key;
                        auto const k = builtin_value(item.first);
                        switch (kind_of(k))
                        {
                        case NEOS_PY_STR: key = as<str_object>(k)->value; break;
                        case NEOS_PY_NONE: key = "null"; break;
                        case NEOS_PY_BOOL: key = k == make_bool(true) ? "true" : "false"; break;
                        case NEOS_PY_INT:
                            if (!repr_of(k, key))
                                return false;
                            break;
                        case NEOS_PY_FLOAT:
                            {
                                std::string text;
                                if (!encode_float(as<float_object>(k)->value, text))
                                    return false;
                                key = text;
                                break;
                            }
                        default:
                            if (iOptions.skipkeys)
                                continue;
                            {
                                ref const cls{ class_of(item.first) };
                                ref const name{ getattr(cls.get(), "__name__") };
                                clear_error();
                                error("TypeError", std::string{ "keys must be str, int, float, bool or None, not " } +
                                    (name && kind_of(name.get()) == NEOS_PY_STR ? as<str_object>(name.get())->value : std::string{ type_name(item.first) }));
                                return false;
                            }
                        }
                        if (iOptions.hasIndent)
                            aResult += (!first ? iOptions.itemSeparator : std::string{}) + newline(aLevel + 1u);
                        else if (!first)
                            aResult += iOptions.itemSeparator;
                        first = false;
                        aResult += encode_string(key, iOptions.ensureAscii) + iOptions.keySeparator;
                        if (!encode(item.second, aResult, aLevel + 1u))
                            return false;
                    }
                    if (iOptions.hasIndent)
                        aResult += newline(aLevel);
                    aResult += "}";
                    leave(aObject);
                    return true;
                }
            private:
                encoder_options const& iOptions;
                object* iSelf;
                std::set<object*> iMarkers;
            };

            // (the encoder's options from keyword arguments (dumps's or JSONEncoder's))
            bool options_from(object* aKwargs, encoder_options& aOptions, object** aCls, char const* aFunction, bool aAllowCls)
            {
                if (aKwargs == nullptr)
                    return true;
                object* separators = nullptr;
                for (auto const& item : as<dict_object>(aKwargs)->items)
                {
                    auto const& name = as<str_object>(item.first)->value;
                    auto const v = item.second;
                    if (name == "skipkeys") aOptions.skipkeys = truth(v) == 1;
                    else if (name == "ensure_ascii") aOptions.ensureAscii = truth(v) == 1;
                    else if (name == "check_circular") aOptions.checkCircular = truth(v) == 1;
                    else if (name == "allow_nan") aOptions.allowNan = truth(v) == 1;
                    else if (name == "sort_keys") aOptions.sortKeys = truth(v) == 1;
                    else if (name == "indent")
                    {
                        if (kind_of(v) == NEOS_PY_NONE)
                            continue;
                        aOptions.hasIndent = true;
                        if (kind_of(v) == NEOS_PY_STR)
                            aOptions.indent = as<str_object>(v)->value;
                        else
                        {
                            long long n = 0;
                            if (!to_index(v, n))
                                return false;
                            aOptions.indent = std::string(static_cast<std::size_t>(std::max(0ll, n)), ' ');
                        }
                    }
                    else if (name == "separators") separators = v;
                    else if (name == "default") aOptions.defaultFunction = v;
                    else if (name == "cls" && aAllowCls) { if (kind_of(v) != NEOS_PY_NONE) *aCls = v; }
                    else
                    {
                        error("TypeError", std::string{ aFunction } + "() got an unexpected keyword argument '" + name + "'");
                        return false;
                    }
                }
                if (aOptions.hasIndent)
                    aOptions.itemSeparator = ",";
                if (separators != nullptr && kind_of(separators) != NEOS_PY_NONE)
                {
                    std::vector<object*> parts;
                    if (!collect(separators, parts))
                        return false;
                    bool const ok = parts.size() == 2u && kind_of(parts[0]) == NEOS_PY_STR && kind_of(parts[1]) == NEOS_PY_STR;
                    if (ok)
                    {
                        aOptions.itemSeparator = as<str_object>(parts[0])->value;
                        aOptions.keySeparator = as<str_object>(parts[1])->value;
                    }
                    for (auto p : parts)
                        release(p);
                    if (!ok)
                    {
                        error("ValueError", "too many values to unpack (expected 2)");
                        return false;
                    }
                }
                return true;
            }

            object* dumps(object* aObject, object* aKwargs)
            {
                encoder_options options;
                object* cls = nullptr;
                if (!options_from(aKwargs, options, &cls, "dumps", true))
                    return nullptr;
                if (cls != nullptr)
                {
                    // (cls(**kw).encode(obj))
                    ref kwargs{ make_dict() };
                    if (aKwargs != nullptr)
                        for (auto const& item : as<dict_object>(aKwargs)->items)
                            if (as<str_object>(item.first)->value != "cls")
                                dict_set(kwargs.get(), item.first, item.second);
                    ref const empty{ make_tuple() };
                    ref const instance{ call(cls, empty.get(), kwargs.get()) };
                    if (!instance)
                        return nullptr;
                    return call_method_named(instance.get(), "encode", { borrowed(aObject) });
                }
                std::string result;
                encoder e{ options };
                if (!e.encode(aObject, result, 0u))
                    return nullptr;
                return make_str(result);
            }

            // --- the decoder

            struct decoder_options
            {
                object* objectHook = nullptr;
                object* objectPairsHook = nullptr;
                object* parseFloat = nullptr;
                object* parseInt = nullptr;
                object* parseConstant = nullptr;
                bool strict = true;
            };

            object* decode_error(std::string const& aMessage, std::u32string const& aDocument, std::size_t aPosition, object* aDocObject)
            {
                std::size_t line = 1u;
                std::size_t lastNewline = std::u32string::npos;
                for (std::size_t i = 0u; i < aPosition && i < aDocument.size(); ++i)
                    if (aDocument[i] == U'\n')
                    {
                        ++line;
                        lastNewline = i;
                    }
                auto const column = lastNewline == std::u32string::npos ? aPosition + 1u : aPosition - lastNewline;
                auto const text = aMessage + ": line " + std::to_string(line) + " column " + std::to_string(column) + " (char " + std::to_string(aPosition) + ")";
                ref const exception{ make_exception("json.decoder.JSONDecodeError", { make_str(text) }) };
                ref const msg{ make_str(aMessage) };
                ref const pos{ make_int(static_cast<long long>(aPosition)) };
                ref const lineno{ make_int(static_cast<long long>(line)) };
                ref const colno{ make_int(static_cast<long long>(column)) };
                setattr(exception.get(), "msg", msg.get());
                setattr(exception.get(), "doc", aDocObject);
                setattr(exception.get(), "pos", pos.get());
                setattr(exception.get(), "lineno", lineno.get());
                setattr(exception.get(), "colno", colno.get());
                fn_raise(exception.get());
                return nullptr;
            }

            class decoder
            {
            public:
                decoder(decoder_options const& aOptions, std::u32string const& aText, object* aDocument) : iOptions{ aOptions }, iText{ aText }, iDocument{ aDocument } {}
            public:
                bool is_whitespace(char32_t c) const { return c == U' ' || c == U'\t' || c == U'\n' || c == U'\r'; }
                std::size_t skip(std::size_t i) const
                {
                    while (i < iText.size() && is_whitespace(iText[i]))
                        ++i;
                    return i;
                }
                object* fail(std::string const& aMessage, std::size_t aPosition) { return decode_error(aMessage, iText, aPosition, iDocument); }
                object* value(std::size_t aIndex, std::size_t& aNext)
                {
                    if (aIndex >= iText.size())
                        return fail("Expecting value", aIndex);
                    auto const c = iText[aIndex];
                    switch (c)
                    {
                    case U'"': return string(aIndex + 1u, aNext);
                    case U'{': return object_value(aIndex + 1u, aNext);
                    case U'[': return array(aIndex + 1u, aNext);
                    default: break;
                    }
                    auto const matches = [&](std::u32string_view aWord) { return std::u32string_view{ iText }.substr(aIndex, aWord.size()) == aWord; };
                    if (c == U'n' && matches(U"null")) { aNext = aIndex + 4u; return borrowed(none()); }
                    if (c == U't' && matches(U"true")) { aNext = aIndex + 4u; return make_bool(true); }
                    if (c == U'f' && matches(U"false")) { aNext = aIndex + 5u; return make_bool(false); }
                    for (auto const& constant : { std::pair{ std::u32string_view{ U"NaN" }, "NaN" }, std::pair{ std::u32string_view{ U"Infinity" }, "Infinity" },
                        std::pair{ std::u32string_view{ U"-Infinity" }, "-Infinity" } })
                        if (matches(constant.first))
                        {
                            aNext = aIndex + constant.first.size();
                            if (iOptions.parseConstant != nullptr && kind_of(iOptions.parseConstant) != NEOS_PY_NONE)
                                return call_with(iOptions.parseConstant, { make_str(constant.second) });
                            return make_float(constant.second[0] == 'N' ? std::nan("") : constant.second[0] == '-' ? -HUGE_VAL : HUGE_VAL);
                        }
                    return number(aIndex, aNext);
                }
                object* number(std::size_t aStart, std::size_t& aNext)
                {
                    auto idx = aStart;
                    auto const n = iText.size();
                    auto const digit = [&](std::size_t i) { return i < n && iText[i] >= U'0' && iText[i] <= U'9'; };
                    if (idx < n && iText[idx] == U'-')
                        ++idx;
                    if (idx < n && iText[idx] >= U'1' && iText[idx] <= U'9')
                    {
                        ++idx;
                        while (digit(idx))
                            ++idx;
                    }
                    else if (idx < n && iText[idx] == U'0')
                        ++idx;
                    else
                        return fail("Expecting value", aStart);
                    bool isFloat = false;
                    if (idx + 1u < n && iText[idx] == U'.' && digit(idx + 1u))
                    {
                        isFloat = true;
                        idx += 2u;
                        while (digit(idx))
                            ++idx;
                    }
                    if (idx + 1u < n && (iText[idx] == U'e' || iText[idx] == U'E'))
                    {
                        auto const eStart = idx;
                        ++idx;
                        if (idx + 1u < n && (iText[idx] == U'-' || iText[idx] == U'+'))
                            ++idx;
                        while (digit(idx))
                            ++idx;
                        if (digit(idx - 1u))
                            isFloat = true;
                        else
                            idx = eStart;
                    }
                    aNext = idx;
                    auto const text = from_code_points(std::u32string_view{ iText }.substr(aStart, idx - aStart));
                    if (isFloat)
                    {
                        if (iOptions.parseFloat != nullptr && kind_of(iOptions.parseFloat) != NEOS_PY_NONE)
                            return call_with(iOptions.parseFloat, { make_str(text) });
                        return make_float(std::strtod(text.c_str(), nullptr));
                    }
                    if (iOptions.parseInt != nullptr && kind_of(iOptions.parseInt) != NEOS_PY_NONE)
                        return call_with(iOptions.parseInt, { make_str(text) });
                    ref const intType{ fn_builtin("int") };
                    return call_with(intType.get(), { make_str(text) });
                }
                object* string(std::size_t aEnd, std::size_t& aNext)
                {
                    auto const begin = aEnd - 1u;
                    std::string result;
                    auto end = aEnd;
                    auto const n = iText.size();
                    for (;;)
                    {
                        auto next = end;
                        char32_t c = 0u;
                        for (; next < n; ++next)
                        {
                            c = iText[next];
                            if (c == U'"' || c == U'\\')
                                break;
                            if (c <= 0x1fu && iOptions.strict)
                                return fail("Invalid control character at", next);
                        }
                        if (next >= n)
                            return fail("Unterminated string starting at", begin);
                        result += from_code_points(std::u32string_view{ iText }.substr(end, next - end));
                        ++next;
                        if (c == U'"')
                        {
                            aNext = next;
                            return make_str(result);
                        }
                        if (next == n)
                            return fail("Unterminated string starting at", begin);
                        c = iText[next];
                        if (c != U'u')
                        {
                            end = next + 1u;
                            switch (c)
                            {
                            case U'"': case U'\\': case U'/': break;
                            case U'b': c = U'\b'; break;
                            case U'f': c = U'\f'; break;
                            case U'n': c = U'\n'; break;
                            case U'r': c = U'\r'; break;
                            case U't': c = U'\t'; break;
                            default: return fail("Invalid \\escape", end - 2u);
                            }
                            append_code_point(result, c);
                            continue;
                        }
                        ++next;
                        end = next + 4u;
                        if (end > n)
                            return fail("Invalid \\uXXXX escape", next - 1u);
                        auto const hex = [&](std::size_t aFrom, char32_t& aValue) -> bool
                        {
                            aValue = 0u;
                            for (auto i = aFrom; i < aFrom + 4u; ++i)
                            {
                                auto const d = iText[i];
                                aValue <<= 4;
                                if (d >= U'0' && d <= U'9') aValue |= d - U'0';
                                else if (d >= U'a' && d <= U'f') aValue |= d - U'a' + 10u;
                                else if (d >= U'A' && d <= U'F') aValue |= d - U'A' + 10u;
                                else return false;
                            }
                            return true;
                        };
                        char32_t value = 0u;
                        if (!hex(next, value))
                            return fail("Invalid \\uXXXX escape", end - 5u);
                        if (value >= 0xD800u && value <= 0xDBFFu && end + 6u < n + 1u && end + 1u < n && iText[end] == U'\\' && iText[end + 1u] == U'u' && end + 6u <= n)
                        {
                            char32_t low = 0u;
                            if (!hex(end + 2u, low))
                                return fail("Invalid \\uXXXX escape", end + 1u);
                            if (low >= 0xDC00u && low <= 0xDFFFu)
                            {
                                value = 0x10000u + ((value - 0xD800u) << 10) + (low - 0xDC00u);
                                end += 6u;
                            }
                        }
                        append_code_point(result, value);
                    }
                }
                object* object_value(std::size_t aIndex, std::size_t& aNext)
                {
                    bool const pairs = iOptions.objectPairsHook != nullptr && kind_of(iOptions.objectPairsHook) != NEOS_PY_NONE;
                    ref result{ pairs ? make_list() : make_dict() };
                    auto idx = skip(aIndex);
                    auto const n = iText.size();
                    if (idx >= n || iText[idx] != U'}')
                    {
                        for (;;)
                        {
                            if (idx >= n || iText[idx] != U'"')
                                return fail("Expecting property name enclosed in double quotes", idx);
                            std::size_t next = 0u;
                            ref const key{ string(idx + 1u, next) };
                            if (!key)
                                return nullptr;
                            idx = skip(next);
                            if (idx >= n || iText[idx] != U':')
                                return fail("Expecting ':' delimiter", idx);
                            idx = skip(idx + 1u);
                            ref const item{ value(idx, next) };
                            if (!item)
                                return nullptr;
                            if (pairs)
                                as<list_object>(result.get())->items.push_back(make_tuple({ borrowed(key.get()), borrowed(item.get()) }));
                            else
                                dict_set(result.get(), key.get(), item.get());
                            idx = skip(next);
                            if (idx < n && iText[idx] == U'}')
                                break;
                            if (idx >= n || iText[idx] != U',')
                                return fail("Expecting ',' delimiter", idx);
                            auto const comma = idx;
                            idx = skip(idx + 1u);
                            if (idx < n && iText[idx] == U'}')
                                return fail("Illegal trailing comma before end of object", comma);
                        }
                    }
                    aNext = idx + 1u;
                    if (pairs)
                        return call_with(iOptions.objectPairsHook, { result.release_ownership() });
                    if (iOptions.objectHook != nullptr && kind_of(iOptions.objectHook) != NEOS_PY_NONE)
                        return call_with(iOptions.objectHook, { result.release_ownership() });
                    return result.release_ownership();
                }
                object* array(std::size_t aIndex, std::size_t& aNext)
                {
                    ref result{ make_list() };
                    auto idx = skip(aIndex);
                    auto const n = iText.size();
                    if (idx >= n || iText[idx] != U']')
                    {
                        for (;;)
                        {
                            std::size_t next = 0u;
                            auto const item = value(idx, next);
                            if (item == nullptr)
                                return nullptr;
                            as<list_object>(result.get())->items.push_back(item);
                            idx = skip(next);
                            if (idx < n && iText[idx] == U']')
                                break;
                            if (idx >= n || iText[idx] != U',')
                                return fail("Expecting ',' delimiter", idx);
                            auto const comma = idx;
                            idx = skip(idx + 1u);
                            if (idx < n && iText[idx] == U']')
                                return fail("Illegal trailing comma before end of array", comma);
                        }
                    }
                    aNext = idx + 1u;
                    return result.release_ownership();
                }
            private:
                decoder_options const& iOptions;
                std::u32string const& iText;
                object* iDocument;
            };

            bool decoder_options_from(object* aKwargs, decoder_options& aOptions, object** aCls, char const* aFunction, bool aAllowCls)
            {
                if (aKwargs == nullptr)
                    return true;
                for (auto const& item : as<dict_object>(aKwargs)->items)
                {
                    auto const& name = as<str_object>(item.first)->value;
                    auto const v = item.second;
                    if (name == "object_hook") aOptions.objectHook = v;
                    else if (name == "object_pairs_hook") aOptions.objectPairsHook = v;
                    else if (name == "parse_float") aOptions.parseFloat = v;
                    else if (name == "parse_int") aOptions.parseInt = v;
                    else if (name == "parse_constant") aOptions.parseConstant = v;
                    else if (name == "strict") aOptions.strict = truth(v) == 1;
                    else if (name == "cls" && aAllowCls) { if (kind_of(v) != NEOS_PY_NONE) *aCls = v; }
                    else
                    {
                        error("TypeError", std::string{ aFunction } + "() got an unexpected keyword argument '" + name + "'");
                        return false;
                    }
                }
                return true;
            }

            // (the document: str, bytes or bytearray)
            bool document_of(object* aDocument, std::u32string& aText)
            {
                if (kind_of(aDocument) == NEOS_PY_STR)
                {
                    aText = to_code_points(as<str_object>(aDocument)->value);
                    return true;
                }
                std::string_view bytes;
                if (bytes_like(aDocument, bytes))
                {
                    std::string decoded;
                    if (!decode(bytes, "utf-8", "surrogatepass", decoded) && !decode(bytes, "utf-8", "strict", decoded))
                        return false;
                    aText = to_code_points(decoded);
                    return true;
                }
                ref const cls{ class_of(aDocument) };
                ref const name{ getattr(cls.get(), "__name__") };
                clear_error();
                error("TypeError", std::string{ "the JSON object must be str, bytes or bytearray, not " } +
                    (name && kind_of(name.get()) == NEOS_PY_STR ? as<str_object>(name.get())->value : std::string{ type_name(aDocument) }));
                return false;
            }

            object* raw_decode(decoder_options const& aOptions, object* aDocument, std::size_t aIndex, std::size_t& aEnd)
            {
                std::u32string text;
                if (!document_of(aDocument, text))
                    return nullptr;
                decoder d{ aOptions, text, aDocument };
                return d.value(aIndex, aEnd);
            }

            object* loads(object* aDocument, object* aKwargs)
            {
                decoder_options options;
                object* cls = nullptr;
                if (!decoder_options_from(aKwargs, options, &cls, "loads", true))
                    return nullptr;
                if (cls != nullptr)
                {
                    ref kwargs{ make_dict() };
                    if (aKwargs != nullptr)
                        for (auto const& item : as<dict_object>(aKwargs)->items)
                            if (as<str_object>(item.first)->value != "cls")
                                dict_set(kwargs.get(), item.first, item.second);
                    ref const empty{ make_tuple() };
                    ref const instance{ call(cls, empty.get(), kwargs.get()) };
                    if (!instance)
                        return nullptr;
                    return call_method_named(instance.get(), "decode", { borrowed(aDocument) });
                }
                std::u32string text;
                if (!document_of(aDocument, text))
                    return nullptr;
                if (!text.empty() && text[0] == 0xFEFFu)
                    return decode_error("Unexpected UTF-8 BOM (decode using utf-8-sig)", text, 0u, aDocument);
                decoder d{ options, text, aDocument };
                std::size_t end = 0u;
                ref result{ d.value(d.skip(0u), end) };
                if (!result)
                    return nullptr;
                end = d.skip(end);
                if (end != text.size())
                    return decode_error("Extra data", text, end, aDocument);
                return result.release_ownership();
            }

            // --- JSONEncoder and JSONDecoder

            char const* const sEncoderOptions[] = { "skipkeys", "ensure_ascii", "check_circular", "allow_nan", "sort_keys", "indent", "separators", "default" };

            bool encoder_of(object* aSelf, encoder_options& aOptions)
            {
                ref kwargs{ make_dict() };
                for (auto name : sEncoderOptions)
                {
                    ref const value{ getattr(aSelf, name) };
                    if (!value)
                    {
                        clear_error();
                        continue;
                    }
                    if (std::strcmp(name, "default") == 0)
                        continue;
                    ref const key{ make_str(name) };
                    dict_set(kwargs.get(), key.get(), value.get());
                }
                object* cls = nullptr;
                return options_from(kwargs.get(), aOptions, &cls, "JSONEncoder", false);
            }

            builtin_entry const sEncoderMethods[] =
            {
                { "__init__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        if (aArgs.size() > 1u)
                            return error("TypeError", "JSONEncoder.__init__() takes 1 positional argument but " + std::to_string(aArgs.size()) + " were given");
                        encoder_options options;
                        object* cls = nullptr;
                        if (!options_from(aKwargs, options, &cls, "__init__", false))
                            return nullptr;
                        auto const self = aArgs.at(0);
                        // (the options as attributes (their defaults if not given))
                        static std::pair<char const*, bool> const sDefaults[] = { { "skipkeys", false }, { "ensure_ascii", true }, { "check_circular", true },
                            { "allow_nan", true }, { "sort_keys", false } };
                        for (auto const& d : sDefaults)
                            setattr(self, d.first, make_bool(d.second));
                        setattr(self, "indent", none());
                        if (aKwargs != nullptr)
                            for (auto const& item : as<dict_object>(aKwargs)->items)
                                if (as<str_object>(item.first)->value != "separators")
                                    setattr(self, as<str_object>(item.first)->value.c_str(), item.second);
                        ref const separators{ make_tuple({ make_str(options.itemSeparator), make_str(options.keySeparator) }) };
                        setattr(self, "item_separator", separators.get() ? as<tuple_object>(separators.get())->items[0] : none());
                        setattr(self, "key_separator", as<tuple_object>(separators.get())->items[1]);
                        setattr(self, "separators", separators.get());
                        return borrowed(none());
                    } },
                { "default", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        if (aArgs.size() != 2u)
                            return error("TypeError", "default() takes 2 positional arguments");
                        // (a default function given to __init__)
                        if (is_instance(aArgs[0]))
                        {
                            ref const dict{ getattr(aArgs[0], "__dict__") };
                            ref const key{ make_str("default") };
                            if (dict && kind_of(dict.get()) == NEOS_PY_DICT)
                            {
                                auto const position = dict_find(dict.get(), key.get());
                                if (position >= 0)
                                    return call_with(as<dict_object>(dict.get())->items[static_cast<std::size_t>(position)].second, { borrowed(aArgs[1]) });
                            }
                            clear_error();
                        }
                        return encoder::not_serializable(aArgs[1]);
                    } },
                { "encode", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        if (aArgs.size() != 2u)
                            return error("TypeError", "encode() takes 2 positional arguments");
                        encoder_options options;
                        if (!encoder_of(aArgs[0], options))
                            return nullptr;
                        std::string result;
                        encoder e{ options, aArgs[0] };
                        if (!e.encode(aArgs[1], result, 0u))
                            return nullptr;
                        return make_str(result);
                    } },
                { "iterencode", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        if (aArgs.size() < 2u)
                            return error("TypeError", "iterencode() missing 1 required positional argument: 'o'");
                        ref const text{ call_method_named(aArgs[0], "encode", { borrowed(aArgs[1]) }) };
                        if (!text)
                            return nullptr;
                        ref const list{ make_list({ borrowed(text.get()) }) };
                        return iter(list.get());
                    } }
            };

            builtin_entry const sDecoderMethods[] =
            {
                { "__init__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        decoder_options options;
                        object* cls = nullptr;
                        if (!decoder_options_from(aKwargs, options, &cls, "__init__", false))
                            return nullptr;
                        auto const self = aArgs.at(0);
                        for (auto name : { "object_hook", "object_pairs_hook", "parse_float", "parse_int", "parse_constant" })
                            setattr(self, name, none());
                        setattr(self, "strict", make_bool(true));
                        if (aKwargs != nullptr)
                            for (auto const& item : as<dict_object>(aKwargs)->items)
                                setattr(self, as<str_object>(item.first)->value.c_str(), item.second);
                        return borrowed(none());
                    } },
                { "decode", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        if (aArgs.size() != 2u)
                            return error("TypeError", "decode() takes 2 positional arguments");
                        ref kwargs{ make_dict() };
                        for (auto name : { "object_hook", "object_pairs_hook", "parse_float", "parse_int", "parse_constant", "strict" })
                        {
                            ref const value{ getattr(aArgs[0], name) };
                            if (!value)
                            {
                                clear_error();
                                continue;
                            }
                            ref const key{ make_str(name) };
                            dict_set(kwargs.get(), key.get(), value.get());
                        }
                        return loads(aArgs[1], kwargs.get());
                    } },
                { "raw_decode", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        if (aArgs.size() < 2u || aArgs.size() > 3u)
                            return error("TypeError", "raw_decode() takes 2 or 3 positional arguments");
                        decoder_options options;
                        long long index = 0;
                        if (aArgs.size() == 3u && !to_index(aArgs[2], index))
                            return nullptr;
                        std::size_t end = 0u;
                        ref const value{ raw_decode(options, aArgs[1], static_cast<std::size_t>(index), end) };
                        if (!value)
                            return nullptr;
                        return make_tuple({ borrowed(value.get()), make_int(static_cast<long long>(end)) });
                    } }
            };

            builtin_entry const sJsonFunctions[] =
            {
                { "dumps", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        if (aArgs.size() != 1u)
                            return error("TypeError", aArgs.empty() ? "dumps() missing 1 required positional argument: 'obj'" :
                                "dumps() takes 1 positional argument but " + std::to_string(aArgs.size()) + " were given");
                        return dumps(aArgs[0], aKwargs);
                    } },
                { "dump", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        if (aArgs.size() != 2u)
                            return error("TypeError", "dump() missing 1 required positional argument: 'fp'");
                        ref const text{ dumps(aArgs[0], aKwargs) };
                        if (!text)
                            return nullptr;
                        ref const r{ call_method_named(aArgs[1], "write", { borrowed(text.get()) }) };
                        return r ? borrowed(none()) : nullptr;
                    } },
                { "loads", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        if (aArgs.size() != 1u)
                            return error("TypeError", aArgs.empty() ? "loads() missing 1 required positional argument: 's'" :
                                "loads() takes 1 positional argument but " + std::to_string(aArgs.size()) + " were given");
                        return loads(aArgs[0], aKwargs);
                    } },
                { "load", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        if (aArgs.size() != 1u)
                            return error("TypeError", "load() missing 1 required positional argument: 'fp'");
                        ref const text{ call_method_named(aArgs[0], "read", {}) };
                        if (!text)
                            return nullptr;
                        return loads(text.get(), aKwargs);
                    } }
            };

            bool json_module(std::vector<std::pair<std::string, object*>>& aAttributes)
            {
                for (auto const& f : sJsonFunctions)
                    aAttributes.emplace_back(f.name, make_builtin(f));
                if (!is_exception_class("json.decoder.JSONDecodeError"))
                    user_exception_bases()["json.decoder.JSONDecodeError"] = { "ValueError" };
                aAttributes.emplace_back("JSONDecodeError", exception_class("json.decoder.JSONDecodeError"));
                for (auto const& c : { std::pair{ "JSONEncoder", true }, std::pair{ "JSONDecoder", false } })
                {
                    std::vector<std::pair<std::string, object*>> ns;
                    if (c.second)
                        for (auto const& m : sEncoderMethods)
                            ns.emplace_back(m.name, make_builtin(m));
                    else
                        for (auto const& m : sDecoderMethods)
                            ns.emplace_back(m.name, make_builtin(m));
                    auto const cls = make_class("json", c.first, {}, ns);
                    for (auto& n : ns)
                        release(n.second);
                    if (cls == nullptr)
                        return false;
                    aAttributes.emplace_back(c.first, cls);
                }
                return true;
            }

            inline bool const sJson = register_module("json", json_module);
        }
    }
}
