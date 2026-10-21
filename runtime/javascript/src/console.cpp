/*
  console.cpp

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

  Portions of this file (decide_styled) are derived from Node.js (lib/internal/tty.js: getColorDepth and its table of
  terminals), which is used under the MIT licence below; Node.js's table of terminals is in part from GNU dircolors,
  whose notice is also below. Changes: translated from JavaScript to C++ for the neos JavaScript runtime plugin (see
  runtime/javascript/NOTICE).

    MIT License

    Copyright (c) Sindre Sorhus <sindresorhus@gmail.com> (sindresorhus.com)

    Permission is hereby granted, free of charge, to any person obtaining a copy
    of this software and associated documentation files (the "Software"), to deal
    in the Software without restriction, including without limitation the rights
    to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
    copies of the Software, and to permit persons to whom the Software is
    furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included in
    all copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
    OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
    SOFTWARE.

    dircolors: Copyright (C) 1996-2016 Free Software Foundation, Inc. Copying and
    distribution of this file, with or without modification, are permitted
    provided the copyright notice and this notice are preserved.
*/


/*
  The console (console.log, ...) and the formatting of values as Node.js's util.inspect formats them (as console.log 
  shows them).
*/

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif
#include "js.hpp"
#include "api.hpp"
#include "typed_arrays.hpp"

namespace neos::javascript_runtime
{
    namespace
    {
        // whether console output to a stream (1: the standard output, 2: the standard error stream) is styled (coloured), as
        // node decides it: if FORCE_COLOR is set, as it says (0 or false: not styled); else only if the stream is a terminal
        // (on Windows, a console that processes ANSI escape sequences) whose colours are not disabled (NO_COLOR,
        // NODE_DISABLE_COLORS, TERM=dumb) and that has them (Windows, tmux, COLORTERM or a TERM that node knows has them)
        bool decide_styled(int aStream)
        {
            auto const env = [](char const* aName) -> std::optional<std::string_view>
            {
                if (auto const value = std::getenv(aName))
                    return std::string_view{ value };
                return {};
            };
#if defined(_WIN32)
            HANDLE const handle = ::GetStdHandle(aStream == 2 ? STD_ERROR_HANDLE : STD_OUTPUT_HANDLE);
            DWORD mode = 0;
            bool const terminal = handle != nullptr && handle != INVALID_HANDLE_VALUE && ::GetConsoleMode(handle, &mode) != 0;
            auto const ansi = [&]()
            {
                return !terminal || (mode & ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0 ||
                    ::SetConsoleMode(handle, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0;
            };
#else
            bool const terminal = ::isatty(aStream) != 0;
            auto const ansi = []() { return true; };
#endif
            if (auto const force = env("FORCE_COLOR"))
                return (force->empty() || *force == "1" || *force == "2" || *force == "3" || *force == "true") && ansi();
            if (!terminal || env("NODE_DISABLE_COLORS") || env("NO_COLOR") || env("TERM") == "dumb")
                return false;
#if defined(_WIN32)
            return ansi();
#else
            if (env("TMUX") || !env("COLORTERM").value_or("").empty())
                return true;
            auto const term = env("TERM").value_or("");
            static std::string_view const sNames[] = { "eterm", "cons25", "console", "cygwin", "dtterm", "gnome", "hurd", "jfbterm",
                "konsole", "kterm", "mlterm", "mosh", "putty", "st", "rxvt-unicode-24bit", "terminator", "xterm-kitty" };
            static std::string_view const sContaining[] = { "ansi", "color", "linux", "direct" };
            static std::string_view const sPrefixes[] = { "rxvt", "screen", "xterm", "vt100", "vt220" };
            std::string lower{ term };
            for (auto& c : lower)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            for (auto const name : sNames)
                if (lower == name)
                    return true;
            for (auto const part : sContaining)
                if (lower.find(part) != std::string::npos)
                    return true;
            for (auto const prefix : sPrefixes)
                if (lower.starts_with(prefix))
                    return true;
            // (con<digits>x<digit>)
            if (lower.starts_with("con"))
            {
                auto p = lower.find_first_not_of("0123456789", 3u);
                if (p != std::string::npos && lower[p] == 'x' && p + 1u < lower.size() && std::isdigit(static_cast<unsigned char>(lower[p + 1u])))
                    return true;
            }
            return false;
#endif
        }


        bool styled(int aStream)
        {
            static bool const sStyled[] = { decide_styled(1), decide_styled(2) };
            return sStyled[aStream == 2 ? 1 : 0];
        }

        std::string style(std::string const& aText, char const* aStart, char const* aEnd, bool aStyled)
        {
            if (!aStyled)
                return aText;
            return std::string{ "\x1b[" } + aStart + "m" + aText + "\x1b[" + aEnd + "m";
        }

        std::string quote(std::u16string const& aText)
        {
            // (as util.inspect quotes a string: single quotes unless it has them (then double quotes, then backticks))
            char q = '\'';
            if (aText.find(u'\'') != std::u16string::npos)
            {
                if (aText.find(u'"') == std::u16string::npos)
                    q = '"';
                else if (aText.find(u'`') == std::u16string::npos && aText.find(u"${") == std::u16string::npos)
                    q = '`';
            }
            std::u16string escaped;
            for (auto c : aText)
            {
                if (c == static_cast<char16_t>(q) || c == u'\\')
                {
                    escaped.push_back(u'\\');
                    escaped.push_back(c);
                }
                else if (c == u'\n')
                    escaped += u"\\n";
                else if (c == u'\t')
                    escaped += u"\\t";
                else if (c == u'\r')
                    escaped += u"\\r";
                else if (c == u'\b')
                    escaped += u"\\b";
                else if (c == u'\f')
                    escaped += u"\\f";
                else if (c == u'\v')
                    escaped += u"\\v";
                else if (c < 0x20u || c == 0x7Fu)
                {
                    char buffer[8];
                    std::snprintf(buffer, sizeof(buffer), "\\x%02X", static_cast<unsigned>(c));
                    escaped += utf16_ascii(buffer);
                }
                else
                    escaped.push_back(c);
            }
            return std::string(1u, q) + wtf8(escaped) + std::string(1u, q);
        }

        bool identifier(std::u16string const& aText)
        {
            if (aText.empty())
                return false;
            for (std::size_t i = 0u; i < aText.size(); ++i)
            {
                auto const c = aText[i];
                bool const letter = (c >= u'a' && c <= u'z') || (c >= u'A' && c <= u'Z') || c == u'_' || c == u'$';
                if (!letter && !(i > 0u && c >= u'0' && c <= u'9'))
                    return false;
            }
            return true;
        }

        struct inspector
        {
            bool styled;
            std::vector<value> seen;
            std::set<value> circular;

            std::string number_text(double aValue)
            {
                if (aValue == 0.0 && std::signbit(aValue))
                    return "-0";
                return utf8(number_to_string(aValue));
            }

            std::string function_name(value aFunction)
            {
                auto* f = as_function(aFunction);
                value name = Undefined;
                if (f != nullptr)
                    if (auto const* p = f->own(the_names().name); p != nullptr && !p->accessor())
                        name = p->data;
                if (!is_string(name) || string_length(name) == 0u)
                    return {};
                return utf8(name);
            }

            std::string constructor_name(value aObject)
            {
                // (the name of the constructor of the object's prototype chain's first object that has one)
                for (auto o = aObject; o != Null;)
                {
                    auto& object = obj(o);
                    if (auto const* p = object.own(the_names().constructor); p != nullptr && !p->accessor() && is_callable(p->data))
                    {
                        auto const name = function_name(p->data);
                        if (!name.empty())
                            return name;
                    }
                    o = object.proto;
                    if (o == Null)
                        break;
                }
                return {};
            }

            std::string key_text(value aKey)
            {
                if (is_symbol(aKey))
                    return "[" + style(describe(aKey), "32", "39", styled) + "]";
                auto const& text = str(aKey);
                if (identifier(text))
                    return wtf8(text);
                return style(quote(text), "32", "39", styled);
            }

            std::string entries(std::vector<std::string> const& aEntries, std::string const& aOpen, std::string const& aClose, int aIndent)
            {
                if (aEntries.empty())
                    return aOpen + aClose;
                std::size_t total = aOpen.size() + aClose.size();
                bool multiline = false;
                for (auto const& e : aEntries)
                {
                    total += e.size() + 2u;
                    if (e.find('\n') != std::string::npos)
                        multiline = true;
                }
                if (!multiline && total + static_cast<std::size_t>(aIndent) <= 72u + 4u)
                {
                    std::string result = aOpen + " ";
                    for (std::size_t i = 0u; i < aEntries.size(); ++i)
                        result += (i != 0u ? ", " : "") + aEntries[i];
                    return result + " " + aClose;
                }
                std::string const pad(static_cast<std::size_t>(aIndent) + 2u, ' ');
                std::string result = aOpen + "\n";
                for (std::size_t i = 0u; i < aEntries.size(); ++i)
                {
                    std::string entry = aEntries[i];
                    for (std::size_t p = entry.find('\n'); p != std::string::npos; p = entry.find('\n', p + 1u))
                        entry.insert(p + 1u, pad);
                    result += pad + entry + (i + 1u < aEntries.size() ? ",\n" : "\n");
                }
                return result + std::string(static_cast<std::size_t>(aIndent), ' ') + aClose;
            }

            std::string format(value aValue, int aDepth, int aIndent, bool aTop)
            {
                if (is_number(aValue))
                    return style(number_text(as_number(aValue)), "33", "39", styled);
                if (is_string(aValue))
                    return aTop ? utf8(aValue) : style(quote(str(aValue)), "32", "39", styled);
                if (aValue == Undefined)
                    return style("undefined", "90", "39", styled);
                if (aValue == Null)
                    return style("null", "1", "22", styled);
                if (is_boolean(aValue))
                    return style(aValue == True ? "true" : "false", "33", "39", styled);
                if (is_symbol(aValue))
                    return style(describe(aValue), "32", "39", styled);
                if (is_bigint(aValue))
                    return style(utf8(make_string(bigint_to_string(aValue))) + "n", "33", "39", styled);
                if (!is_object(aValue))
                    return "?";
                // (a proxy: its target, as node shows it)
                if (obj(aValue).cls == object_class::Proxy)
                {
                    auto const target = proxy_target(aValue);
                    return target == Null ? style("<Revoked Proxy>", "36", "39", styled) : format(target, aDepth, aIndent, aTop);
                }
                if (std::find(seen.begin(), seen.end(), aValue) != seen.end())
                {
                    circular.insert(aValue);
                    return style("[Circular *1]", "36", "39", styled);
                }
                auto& o = obj(aValue);
                // (errors: their stacks (their names and messages))
                if (o.cls == object_class::Error)
                {
                    auto const stack = o.own(the_names().stack);
                    if (stack != nullptr && !stack->accessor() && is_string(stack->data))
                        return utf8(stack->data);
                    try
                    {
                        return utf8(to_string(aValue));
                    }
                    catch (...)
                    {
                        return "Error";
                    }
                }
                std::string prefix;
                std::vector<std::string> items;
                bool const array = o.cls == object_class::Array || o.cls == object_class::TypedArray;
                if (o.callable())
                {
                    auto* f = as_function(aValue);
                    auto const name = function_name(aValue);
                    if (f != nullptr && f->flavour == function_flavour::ClassConstructor)
                        prefix = "[class " + (name.empty() ? std::string{ "(anonymous)" } : name) + "]";
                    else
                    {
                        std::string kind = "Function";
                        if (f != nullptr && f->flavour == function_flavour::Generator)
                            kind = "GeneratorFunction";
                        else if (f != nullptr && (f->flavour == function_flavour::Async || f->flavour == function_flavour::AsyncArrow))
                            kind = "AsyncFunction";
                        else if (f != nullptr && f->flavour == function_flavour::AsyncGenerator)
                            kind = "AsyncGeneratorFunction";
                        prefix = "[" + kind + (name.empty() ? std::string{ " (anonymous)" } : ": " + name) + "]";
                    }
                    prefix = style(prefix, "36", "39", styled);
                }
                if (aDepth < 0 && !o.callable())
                    return style(o.cls == object_class::Array ? "[Array]" : "[" + (constructor_name(aValue).empty() ? std::string{ "Object" } : constructor_name(aValue)) + "]", "36", "39", styled);
                seen.push_back(aValue);
                // (wrapped primitives, dates and regular expressions)
                switch (o.cls)
                {
                case object_class::Number:
                    prefix = "[Number: " + style(number_text(as_number(o.internal)), "33", "39", styled) + "]";
                    break;
                case object_class::String:
                    prefix = "[String: " + style(quote(str(o.internal)), "32", "39", styled) + "]";
                    break;
                case object_class::Boolean:
                    prefix = "[Boolean: " + style(o.internal == True ? "true" : "false", "33", "39", styled) + "]";
                    break;
                case object_class::Symbol:
                    prefix = "[Symbol: " + style(describe(o.internal), "32", "39", styled) + "]";
                    break;
                case object_class::BigInt:
                    prefix = "[BigInt: " + style(describe(o.internal), "33", "39", styled) + "]";
                    break;
                case object_class::Date:
                case object_class::RegExp:
                    try
                    {
                        auto const text = o.cls == object_class::Date ? invoke(aValue, ascii("toISOString")) : to_string(aValue);
                        prefix = style(utf8(text), o.cls == object_class::Date ? "35" : "31", "39", styled);
                    }
                    catch (...)
                    {
                        prefix = o.cls == object_class::Date ? "Invalid Date" : "/(?:)/";
                    }
                    break;
                case object_class::TemporalDuration:
                case object_class::TemporalInstant:
                case object_class::TemporalPlainDate:
                case object_class::TemporalPlainTime:
                case object_class::TemporalPlainDateTime:
                case object_class::TemporalPlainYearMonth:
                case object_class::TemporalPlainMonthDay:
                case object_class::TemporalZonedDateTime:
                    // (as Node: Temporal.PlainDate <2026-10-20>)
                    try
                    {
                        auto const tag = get(aValue, the_realm().symToStringTag);
                        prefix = (is_string(tag) ? utf8(tag) : std::string{ "Temporal" }) + " <" +
                            style(utf8(to_string(invoke(aValue, the_names().toString))), "35", "39", styled) + ">";
                    }
                    catch (...)
                    {
                    }
                    break;
                case object_class::ArrayBuffer:
                case object_class::SharedArrayBuffer:
                    {
                        // (its contents (the first 50 bytes) and byte length)
                        auto& b = static_cast<array_buffer_object&>(o);
                        std::string contents;
                        if (b.detached)
                            contents = "(detached)";
                        else
                        {
                            static char const sDigits[] = "0123456789abcdef";
                            contents = style("[Uint8Contents]", "36", "39", styled) + ": <";
                            for (std::size_t i = 0u; i < b.byte_length() && i < 50u; ++i)
                            {
                                if (i != 0u)
                                    contents += ' ';
                                contents += sDigits[b.data[i] >> 4];
                                contents += sDigits[b.data[i] & 15u];
                            }
                            if (b.byte_length() > 50u)
                                contents += " ... " + std::to_string(b.byte_length() - 50u) + " more byte" + (b.byte_length() - 50u > 1u ? "s" : "");
                            contents += ">";
                        }
                        items.push_back(contents);
                        items.push_back("byteLength: " + style(std::to_string(b.byte_length()), "33", "39", styled));
                    }
                    break;
                case object_class::DataView:
                    {
                        auto& v = static_cast<data_view_object&>(o);
                        auto const& b = v.buffer_object();
                        bool const outOfBounds = b.detached || v.byteOffset > b.byte_length() ||
                            (!v.lengthTracking && v.byteOffset + v.byteLength > b.byte_length());
                        auto const byteLength = outOfBounds ? std::size_t{ 0u } : v.lengthTracking ? b.byte_length() - v.byteOffset : v.byteLength;
                        items.push_back("byteLength: " + style(std::to_string(byteLength), "33", "39", styled));
                        items.push_back("byteOffset: " + style(std::to_string(outOfBounds ? std::size_t{ 0u } : v.byteOffset), "33", "39", styled));
                        items.push_back("buffer: " + format(v.buffer, aDepth - 1, aIndent + 2, false));
                    }
                    break;
                default:
                    break;
                }
                // (an array's elements (holes counted), then the object's other enumerable own properties)
                std::uint32_t length = 0u;
                if (array)
                {
                    length = static_cast<std::uint32_t>(length_of_array_like(aValue));
                    std::uint32_t holes = 0u;
                    std::size_t shown = 0u;
                    for (std::uint32_t i = 0u; i < length && shown < 100u; ++i)
                    {
                        auto const desc = o.get_own_property(index_key(i));
                        if (!desc)
                        {
                            ++holes;
                            continue;
                        }
                        if (holes != 0u)
                        {
                            items.push_back(style("<" + std::to_string(holes) + " empty item" + (holes > 1u ? "s" : "") + ">", "90", "39", styled));
                            ++shown;
                            holes = 0u;
                        }
                        items.push_back(desc->is_accessor() ? style("[Getter/Setter]", "36", "39", styled) : format(desc->valueField.value_or(Undefined), aDepth - 1, aIndent + 2, false));
                        ++shown;
                    }
                    if (holes != 0u)
                        items.push_back(style("<" + std::to_string(holes) + " empty item" + (holes > 1u ? "s" : "") + ">", "90", "39", styled));
                    if (length > 100u)
                        items.push_back("... " + std::to_string(length - 100u) + " more item" + (length - 100u > 1u ? "s" : ""));
                }
                // (a Map's entries (key => value) or a Set's values)
                bool const collection = o.cls == object_class::Map || o.cls == object_class::Set;
                bool const weakCollection = o.cls == object_class::WeakMap || o.cls == object_class::WeakSet;
                std::size_t collectionSize = 0u;
                if (collection)
                {
                    auto const elements = collection_entries(aValue);
                    std::size_t const step = o.cls == object_class::Map ? 2u : 1u;
                    collectionSize = elements.size() / step;
                    for (std::size_t i = 0u; i < elements.size(); i += step)
                        items.push_back(step == 2u ?
                            format(elements[i], aDepth - 1, aIndent + 2, false) + " => " + format(elements[i + 1u], aDepth - 1, aIndent + 2, false) :
                            format(elements[i], aDepth - 1, aIndent + 2, false));
                }
                else if (weakCollection)
                    items.push_back(style("<items unknown>", "36", "39", styled));
                for (auto const key : o.own_property_keys())
                {
                    if (array && (array_index(key) || key == the_names().length))
                        continue;
                    if (o.callable() && (key == the_names().length || key == the_names().name || key == the_names().prototype))
                        continue;
                    if (o.cls == object_class::String && array_index(key))
                        continue;
                    auto const desc = o.get_own_property(key);
                    if (!desc || !desc->enumerable.value_or(false))
                        continue;
                    std::string text;
                    if (desc->is_accessor())
                        text = style(desc->getter.value_or(Undefined) != Undefined ? (desc->setter.value_or(Undefined) != Undefined ? "[Getter/Setter]" : "[Getter]") : "[Setter]", "36", "39", styled);
                    else
                        text = format(desc->valueField.value_or(Undefined), aDepth - 1, aIndent + 2, false);
                    items.push_back(key_text(key) + ": " + text);
                }
                seen.pop_back();
                if (array)
                {
                    auto const name = constructor_name(aValue);
                    std::string const head = (name == "Array" || name.empty()) ? std::string{} : name + "(" + std::to_string(length) + ") ";
                    return head + entries(items, "[", "]", aIndent);
                }
                if (collection || weakCollection)
                {
                    std::string const kind = o.cls == object_class::Map ? "Map" : o.cls == object_class::Set ? "Set" :
                        o.cls == object_class::WeakMap ? "WeakMap" : "WeakSet";
                    auto const name = constructor_name(aValue);
                    auto const size = collection ? "(" + std::to_string(collectionSize) + ")" : std::string{};
                    std::string head = (name.empty() ? kind : name) + size;
                    if (!name.empty() && name != kind)
                        head += " [" + kind + "]";
                    return head + " " + entries(items, "{", "}", aIndent);
                }
                if (!prefix.empty())
                {
                    if (items.empty())
                        return prefix;
                    return entries(items, prefix + " {", "}", aIndent);
                }
                std::string name = constructor_name(aValue);
                if (o.proto == Null)
                    name = "[Object: null prototype]";
                else if (name == "Object")
                    name.clear();
                auto const tag = o.properties.find(the_realm().symToStringTag) == nullptr ? get(aValue, the_realm().symToStringTag) : Undefined;
                if (is_string(tag) && !name.empty() && utf8(tag) != name)
                    name += " [" + utf8(tag) + "]";
                else if (is_string(tag) && name.empty())
                    name = "Object [" + utf8(tag) + "]";
                return (name.empty() ? std::string{} : name + " ") + entries(items, "{", "}", aIndent);
            }
        };

        void write(int aStream, std::string const& aText)
        {
            auto* const stream = aStream == 2 ? stderr : stdout;
            std::fwrite(aText.data(), 1u, aText.size(), stream);
            std::fputc('\n', stream);
            if (aStream == 2)
                std::fflush(stream);
        }

        std::string format_arguments(arguments aArguments, int aStream)
        {
            bool const isStyled = styled(aStream);
            std::string result;
            std::size_t next = 0u;
            // (a format string: %s %d %i %f %j %o %O %c %%)
            if (!aArguments.empty() && is_string(aArguments[0]) && aArguments.size() > 1u)
            {
                auto const text = utf8(aArguments[0]);
                next = 1u;
                for (std::size_t i = 0u; i < text.size(); ++i)
                {
                    if (text[i] != '%' || i + 1u == text.size())
                    {
                        result.push_back(text[i]);
                        continue;
                    }
                    char const c = text[i + 1u];
                    if (c == '%')
                    {
                        result.push_back('%');
                        ++i;
                        continue;
                    }
                    if (std::string_view{ "sdifjoOc" }.find(c) == std::string_view::npos || next >= aArguments.size())
                    {
                        result.push_back(text[i]);
                        continue;
                    }
                    auto const v = aArguments[next++];
                    ++i;
                    switch (c)
                    {
                    case 's':
                        result += is_string(v) ? utf8(v) : inspector{ false }.format(v, 1, 0, false);
                        break;
                    case 'd':
                    case 'i':
                        {
                            if (is_object(v))
                            {
                                result += "NaN";
                                break;
                            }
                            auto const n = to_number(v);
                            result += inspector{ isStyled }.format(number(c == 'i' ? std::trunc(n) : n), 0, 0, false);
                        }
                        break;
                    case 'f':
                        result += inspector{ isStyled }.format(number(to_number(v)), 0, 0, false);
                        break;
                    case 'c':
                        break;
                    default:
                        result += inspector{ isStyled }.format(v, 2, 0, false);
                        break;
                    }
                }
            }
            for (std::size_t i = next; i < aArguments.size(); ++i)
            {
                if (!result.empty() || i != 0u)
                    result += " ";
                result += inspector{ isStyled }.format(aArguments[i], 2, 0, true);
            }
            return result;
        }

        value console_log(value, arguments aArguments, value, function_object& aCallee)
        {
            int const stream = aCallee.nativeId == 2u ? 2 : 1;
            write(stream, format_arguments(aArguments, stream));
            return Undefined;
        }
    }

    std::string inspect(value aValue, bool aStyled, int aDepth)
    {
        return inspector{ aStyled }.format(aValue, aDepth, 0, false);
    }

    void report_uncaught(value aException, bool aInPromise)
    {
        std::fflush(stdout);
        std::string text;
        try
        {
            if (is_object(aException) && obj(aException).cls == object_class::Error)
            {
                auto const s = to_string(aException);
                text = utf8(s);
            }
            else
                text = inspector{ false }.format(aException, 2, 0, false);
        }
        catch (...)
        {
            text = "exception";
        }
        write(2, (aInPromise ? "Uncaught (in promise) " : "Uncaught ") + text);
    }

    namespace
    {
        // (performance.now(): milliseconds since the program started (a host API, as browsers' and Node's))
        std::chrono::steady_clock::time_point const sTimeOrigin = std::chrono::steady_clock::now();

        value performance_now(value, arguments, value, function_object&)
        {
            return number(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - sTimeOrigin).count());
        }
    }

    void init_console(realm& aRealm)
    {
        auto const console = make_object();
        for (auto const& [name, stream] : { std::pair{ "log", 1u }, std::pair{ "info", 1u }, std::pair{ "debug", 1u }, std::pair{ "error", 2u },
            std::pair{ "warn", 2u }, std::pair{ "trace", 2u } })
        {
            auto const f = method(console, name, 0u, console_log);
            static_cast<function_object&>(obj(f)).nativeId = stream;
        }
        obj(aRealm.global).put(ascii("console"), console);
        auto const performance = make_object();
        method(performance, "now", 0u, performance_now);
        obj(aRealm.global).put(ascii("performance"), performance);
    }
}
