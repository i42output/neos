/*
  text.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/textwrap.py), Copyright (c) 2001-2024 Python Software
  Foundation; All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from Python to C++ for the neos Python runtime plugin and adapted to
  its object model (see runtime/python/NOTICE).

  Lib/textwrap.py: Copyright (C) 1999-2001 Gregory P. Ward. Copyright (C) 2002, 2003 Python Software Foundation. Written
  by Greg Ward.
*/

// The module textwrap (CPython 3.13's algorithms, behaviour and messages)

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <optional>
#include <set>
#include <thread>
#include <string>
#include <vector>
#include "objects.hpp"

namespace neos::python_runtime
{
    namespace detail
    {
        namespace
        {
            // --- helpers

            object* compiled_regex(char const* aPattern, long long aFlags = 0)
            {
                ref const compile{ module_attribute("re", "compile") };
                return compile ? invoke(compile.get(), { make_str(aPattern), make_int(aFlags) }) : nullptr;
            }

            long long text_length(object* aText)
            {
                if (auto const s = str_of_value(aText))
                    return static_cast<long long>(code_point_count(*s));
                return length(aText);
            }

            // (str.strip() == '')
            bool blank(object* aText)
            {
                auto const s = str_of_value(aText);
                if (s == nullptr)
                    return false;
                for (auto c : to_code_points(*s))
                    if (!char_is_space(c))
                        return false;
                return true;
            }

            std::string string_value(object* aObject)
            {
                std::string text;
                if (!str_of(aObject, text))
                    clear_error();
                return text;
            }

            // --- textwrap

            // (TextWrapper.wordsep_re's pattern (re.VERBOSE): CPython's, of re.escape(_whitespace))
            std::string word_separator_pattern()
            {
                std::string const whitespace = "[\\\t\\\n\\\x0b\\\x0c\\\r\\ ]";
                std::string const nowhitespace = "[^" + whitespace.substr(1u);
                std::string const wordPunct = R"([\w!"\'&.,?])";
                std::string const letter = R"([^\d\W])";
                return "\n        ( # any whitespace\n          " + whitespace + "+\n"
                    "        | # em-dash between words\n          (?<=" + wordPunct + ") -{2,} (?=\\w)\n"
                    "        | # word, possibly hyphenated\n          " + nowhitespace + "+? (?:\n"
                    "            # hyphenated word\n"
                    "              -(?: (?<=" + letter + "{2}-) | (?<=" + letter + "-" + letter + "-))\n"
                    "              (?= " + letter + " -? " + letter + ")\n"
                    "            | # end of word\n              (?=" + whitespace + "|\\Z)\n"
                    "            | # em-dash\n              (?<=" + wordPunct + ") (?=-{2,}\\w)\n"
                    "            )\n        )";
            }

            builtin_entry const sTextWrapperMethods[] =
            {
                { "__init__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!self_arguments("TextWrapper", aArgs, aKwargs, { "width", "initial_indent", "subsequent_indent", "expand_tabs", "replace_whitespace",
                            "fix_sentence_endings", "break_long_words", "drop_whitespace", "break_on_hyphens", "tabsize", "max_lines", "placeholder" }, 0u, 0u, a))
                            return nullptr;
                        if (aArgs.size() > 11u)
                            return error("TypeError", "TextWrapper.__init__() takes from 1 to 11 positional arguments but " + std::to_string(aArgs.size()) + " were given");
                        auto const self = aArgs[0];
                        std::pair<char const*, object*> const defaults[] = { { "width", nullptr }, { "initial_indent", nullptr }, { "subsequent_indent", nullptr },
                            { "expand_tabs", nullptr }, { "replace_whitespace", nullptr }, { "fix_sentence_endings", nullptr }, { "break_long_words", nullptr },
                            { "drop_whitespace", nullptr }, { "break_on_hyphens", nullptr }, { "tabsize", nullptr }, { "max_lines", nullptr }, { "placeholder", nullptr } };
                        for (std::size_t i = 0u; i < a.size(); ++i)
                        {
                            ref value;
                            if (a[i] != nullptr)
                                value = ref{ new_reference(a[i]) };
                            else
                                switch (i)
                                {
                                case 0: value = ref{ make_int(70) }; break;
                                case 1: case 2: value = ref{ make_str("") }; break;
                                case 3: case 4: case 6: case 7: case 8: value = ref{ make_bool(true) }; break;
                                case 5: value = ref{ make_bool(false) }; break;
                                case 9: value = ref{ make_int(8) }; break;
                                case 10: value = ref{ new_reference(none()) }; break;
                                default: value = ref{ make_str(" [...]") }; break;
                                }
                            if (setattr(self, defaults[i].first, value.get()) < 0)
                                return nullptr;
                        }
                        return new_reference(none());
                    } },
                { "_munge_whitespace", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!self_arguments("_munge_whitespace", aArgs, aKwargs, { "text" }, 1u, 0u, a))
                            return nullptr;
                        auto const self = aArgs[0];
                        ref text{ new_reference(a[0]) };
                        auto const expand = attribute_truth(self, "expand_tabs");
                        if (expand < 0)
                            return nullptr;
                        if (expand == 1)
                        {
                            ref const tabsize{ getattr(self, "tabsize") };
                            text = tabsize ? ref{ call_method_named(text.get(), "expandtabs", { new_reference(tabsize.get()) }) } : ref{};
                            if (!text)
                                return nullptr;
                        }
                        auto const replace = attribute_truth(self, "replace_whitespace");
                        if (replace < 0)
                            return nullptr;
                        if (replace == 1)
                        {
                            ref const table{ getattr(self, "unicode_whitespace_trans") };
                            text = table ? ref{ call_method_named(text.get(), "translate", { new_reference(table.get()) }) } : ref{};
                            if (!text)
                                return nullptr;
                        }
                        return text.release_ownership();
                    } },
                { "_split", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!self_arguments("_split", aArgs, aKwargs, { "text" }, 1u, 0u, a))
                            return nullptr;
                        auto const self = aArgs[0];
                        ref const hyphens{ getattr(self, "break_on_hyphens") };
                        if (!hyphens)
                            return nullptr;
                        ref const regex{ getattr(self, hyphens.get() == make_bool(true) ? "wordsep_re" : "wordsep_simple_re") };
                        ref const chunks{ regex ? call_method_named(regex.get(), "split", { new_reference(a[0]) }) : nullptr };
                        if (!chunks)
                            return nullptr;
                        std::vector<object*> result;
                        for (auto c : as<list_object>(chunks.get())->items)
                        {
                            auto const t = truth(c);
                            if (t < 0)
                            {
                                for (auto r : result)
                                    release(r);
                                return nullptr;
                            }
                            if (t == 1)
                                result.push_back(new_reference(c));
                        }
                        return make_list(std::move(result));
                    } },
                { "_fix_sentence_endings", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!self_arguments("_fix_sentence_endings", aArgs, aKwargs, { "chunks" }, 1u, 0u, a))
                            return nullptr;
                        ref const regex{ getattr(aArgs[0], "sentence_end_re") };
                        ref const search{ regex ? getattr(regex.get(), "search") : nullptr };
                        if (!search)
                            return nullptr;
                        auto const chunks = a[0];
                        long long i = 0;
                        for (;;)
                        {
                            auto const n = length(chunks);
                            if (n < 0)
                                return nullptr;
                            if (i >= n - 1)
                                break;
                            ref const index{ make_int(i + 1) };
                            ref const nextChunk{ getitem(chunks, index.get()) };
                            if (!nextChunk)
                                return nullptr;
                            bool advanceTwo = false;
                            if (auto const s = str_of_value(nextChunk.get()); s != nullptr && *s == " ")
                            {
                                ref const current{ getitem(chunks, ref{ make_int(i) }.get()) };
                                ref const m{ current ? invoke(search.get(), { new_reference(current.get()) }) : nullptr };
                                if (!m)
                                    return nullptr;
                                if (truth(m.get()) == 1)
                                {
                                    ref const two{ make_str("  ") };
                                    if (setitem(chunks, index.get(), two.get()) < 0)
                                        return nullptr;
                                    advanceTwo = true;
                                }
                            }
                            i += advanceTwo ? 2 : 1;
                        }
                        return new_reference(none());
                    } },
                { "_handle_long_word", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!self_arguments("_handle_long_word", aArgs, aKwargs, { "reversed_chunks", "cur_line", "cur_len", "width" }, 4u, 0u, a))
                            return nullptr;
                        auto const self = aArgs[0];
                        long long curLen = 0, width = 0;
                        if (!to_index(a[2], curLen) || !to_index(a[3], width))
                            return nullptr;
                        long long const spaceLeft = width < 1 ? 1 : width - curLen;
                        auto const breakLong = attribute_truth(self, "break_long_words");
                        if (breakLong < 0)
                            return nullptr;
                        if (breakLong == 1 && spaceLeft > 0)
                        {
                            auto end = spaceLeft;
                            ref const lastIndex{ make_int(-1) };
                            ref const chunk{ getitem(a[0], lastIndex.get()) };
                            if (!chunk)
                                return nullptr;
                            auto const hyphens = attribute_truth(self, "break_on_hyphens");
                            if (hyphens < 0)
                                return nullptr;
                            auto const points = to_code_points(string_value(chunk.get()));
                            if (hyphens == 1 && static_cast<long long>(points.size()) > spaceLeft)
                            {
                                // (a hyphen within the space left, after a character that is not a hyphen)
                                auto const hyphen = std::u32string_view{ points }.substr(0u, static_cast<std::size_t>(spaceLeft)).rfind(U'-');
                                if (hyphen != std::u32string_view::npos && hyphen > 0u &&
                                    std::any_of(points.begin(), points.begin() + static_cast<std::ptrdiff_t>(hyphen), [](char32_t c) { return c != U'-'; }))
                                    end = static_cast<long long>(hyphen) + 1;
                            }
                            auto const split = static_cast<std::size_t>(std::min<long long>(end, static_cast<long long>(points.size())));
                            ref const head{ make_str(from_code_points(points.substr(0u, split))) };
                            ref const tail{ make_str(from_code_points(points.substr(split))) };
                            ref const appended{ call_method_named(a[1], "append", { new_reference(head.get()) }) };
                            if (!appended || setitem(a[0], lastIndex.get(), tail.get()) < 0)
                                return nullptr;
                        }
                        else if (length(a[1]) == 0)
                        {
                            ref const popped{ call_method_named(a[0], "pop", {}) };
                            ref const appended{ popped ? call_method_named(a[1], "append", { new_reference(popped.get()) }) : nullptr };
                            if (!appended)
                                return nullptr;
                        }
                        return new_reference(none());
                    } },
                { "_wrap_chunks", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!self_arguments("_wrap_chunks", aArgs, aKwargs, { "chunks" }, 1u, 0u, a))
                            return nullptr;
                        auto const self = aArgs[0];
                        auto const chunks = a[0];
                        long long selfWidth = 0;
                        ref const widthValue{ getattr(self, "width") };
                        if (!widthValue)
                            return nullptr;
                        {
                            ref const zero{ make_int(0) };
                            ref const nonPositive{ compare(NEOS_PY_LE, widthValue.get(), zero.get()) };
                            if (!nonPositive)
                                return nullptr;
                            if (truth(nonPositive.get()) == 1)
                            {
                                std::string r;
                                repr_of(widthValue.get(), r);
                                return error("ValueError", "invalid width " + r + " (must be > 0)");
                            }
                        }
                        if (!to_index(widthValue.get(), selfWidth))
                            return nullptr;
                        ref const maxLines{ getattr(self, "max_lines") };
                        ref const placeholder{ getattr(self, "placeholder") };
                        ref const initialIndent{ getattr(self, "initial_indent") };
                        ref const subsequentIndent{ getattr(self, "subsequent_indent") };
                        if (!maxLines || !placeholder || !initialIndent || !subsequentIndent)
                            return nullptr;
                        bool const limited = kind_of(maxLines.get()) != NEOS_PY_NONE;
                        long long maxLineCount = 0;
                        if (limited)
                        {
                            if (!to_index(maxLines.get(), maxLineCount))
                                return nullptr;
                            auto const indent = maxLineCount > 1 ? subsequentIndent.get() : initialIndent.get();
                            ref const stripped{ call_method_named(placeholder.get(), "lstrip", {}) };
                            if (!stripped)
                                return nullptr;
                            if (text_length(indent) + text_length(stripped.get()) > selfWidth)
                                return error("ValueError", "placeholder too large for max width");
                        }
                        auto const dropWhitespace = attribute_truth(self, "drop_whitespace");
                        if (dropWhitespace < 0)
                            return nullptr;
                        ref const reversed{ call_method_named(chunks, "reverse", {}) };
                        if (!reversed || kind_of(chunks) != NEOS_PY_LIST)
                            return reversed ? error("TypeError", "chunks must be a list") : nullptr;
                        auto& items = as<list_object>(chunks)->items;
                        std::vector<object*> lines;
                        auto const finish = [&](object* aResult) -> object*
                        {
                            if (aResult == nullptr)
                                for (auto l : lines)
                                    release(l);
                            return aResult;
                        };
                        auto const join = [](object* aIndent, std::vector<object*> const& aLine, object* aExtra = nullptr) -> object*
                        {
                            std::string text = string_value(aIndent);
                            for (auto c : aLine)
                                text += string_value(c);
                            if (aExtra != nullptr)
                                text += string_value(aExtra);
                            return make_str(std::move(text));
                        };
                        while (!items.empty())
                        {
                            // (the chunks of the current line (owned by the list curLine))
                            ref const curLineList{ make_list() };
                            auto& curLine = as<list_object>(curLineList.get())->items;
                            long long curLen = 0;
                            auto const indent = lines.empty() ? initialIndent.get() : subsequentIndent.get();
                            long long const width = selfWidth - text_length(indent);
                            // (the whitespace at the start of a line (but not of the first line): dropped)
                            if (dropWhitespace == 1 && blank(items.back()) && !lines.empty())
                            {
                                release(items.back());
                                items.pop_back();
                            }
                            while (!items.empty())
                            {
                                auto const l = text_length(items.back());
                                if (curLen + l <= width)
                                {
                                    curLine.push_back(items.back());
                                    items.pop_back();
                                    curLen += l;
                                }
                                else
                                    break;
                            }
                            // (a chunk that is too long for any line)
                            if (!items.empty() && text_length(items.back()) > width)
                            {
                                ref const r{ call_method_named(self, "_handle_long_word", { new_reference(chunks), new_reference(curLineList.get()), make_int(curLen), make_int(width) }) };
                                if (!r)
                                    return finish(nullptr);
                                curLen = 0;
                                for (auto c : curLine)
                                    curLen += text_length(c);
                            }
                            // (the whitespace at the end of a line: dropped)
                            if (dropWhitespace == 1 && !curLine.empty() && blank(curLine.back()))
                            {
                                curLen -= text_length(curLine.back());
                                release(curLine.back());
                                curLine.pop_back();
                            }
                            if (!curLine.empty())
                            {
                                if (!limited || static_cast<long long>(lines.size()) + 1 < maxLineCount ||
                                    ((items.empty() || (dropWhitespace == 1 && items.size() == 1u && blank(items[0]))) && curLen <= width))
                                    lines.push_back(join(indent, curLine));
                                else
                                {
                                    bool placed = false;
                                    while (!curLine.empty())
                                    {
                                        if (!blank(curLine.back()) && curLen + text_length(placeholder.get()) <= width)
                                        {
                                            lines.push_back(join(indent, curLine, placeholder.get()));
                                            placed = true;
                                            break;
                                        }
                                        curLen -= text_length(curLine.back());
                                        release(curLine.back());
                                        curLine.pop_back();
                                    }
                                    if (!placed)
                                    {
                                        bool appended = false;
                                        if (!lines.empty())
                                        {
                                            ref const previous{ call_method_named(lines.back(), "rstrip", {}) };
                                            if (!previous)
                                                return finish(nullptr);
                                            if (text_length(previous.get()) + text_length(placeholder.get()) <= selfWidth)
                                            {
                                                release(lines.back());
                                                lines.back() = make_str(string_value(previous.get()) + string_value(placeholder.get()));
                                                appended = true;
                                            }
                                        }
                                        if (!appended)
                                        {
                                            ref const stripped{ call_method_named(placeholder.get(), "lstrip", {}) };
                                            if (!stripped)
                                                return finish(nullptr);
                                            lines.push_back(make_str(string_value(indent) + string_value(stripped.get())));
                                        }
                                    }
                                    break;
                                }
                            }
                        }
                        return make_list(std::move(lines));
                    } },
                { "_split_chunks", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!self_arguments("_split_chunks", aArgs, aKwargs, { "text" }, 1u, 0u, a))
                            return nullptr;
                        ref const text{ call_method_named(aArgs[0], "_munge_whitespace", { new_reference(a[0]) }) };
                        return text ? call_method_named(aArgs[0], "_split", { new_reference(text.get()) }) : nullptr;
                    } },
                { "wrap", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!self_arguments("wrap", aArgs, aKwargs, { "text" }, 1u, 0u, a))
                            return nullptr;
                        auto const self = aArgs[0];
                        ref const chunks{ call_method_named(self, "_split_chunks", { new_reference(a[0]) }) };
                        if (!chunks)
                            return nullptr;
                        auto const fix = attribute_truth(self, "fix_sentence_endings");
                        if (fix < 0)
                            return nullptr;
                        if (fix == 1)
                        {
                            ref const r{ call_method_named(self, "_fix_sentence_endings", { new_reference(chunks.get()) }) };
                            if (!r)
                                return nullptr;
                        }
                        return call_method_named(self, "_wrap_chunks", { new_reference(chunks.get()) });
                    } },
                { "fill", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!self_arguments("fill", aArgs, aKwargs, { "text" }, 1u, 0u, a))
                            return nullptr;
                        ref const lines{ call_method_named(aArgs[0], "wrap", { new_reference(a[0]) }) };
                        ref const separator{ make_str("\n") };
                        return lines ? call_method_named(separator.get(), "join", { new_reference(lines.get()) }) : nullptr;
                    } }
            };

            // (TextWrapper(width=width, **kwargs) (and max_lines=1 for shorten))
            object* text_wrapper(object* aWidth, object* aKwargs, bool aShorten)
            {
                ref const cls{ module_attribute("textwrap", "TextWrapper") };
                if (!cls)
                    return nullptr;
                ref const kwargs{ make_dict() };
                if (aKwargs != nullptr && !dict_update(kwargs.get(), aKwargs))
                    return nullptr;
                ref const widthKey{ make_str("width") };
                if (dict_find(kwargs.get(), widthKey.get()) >= 0)
                    return error("TypeError", "textwrap.TextWrapper() got multiple values for keyword argument 'width'");
                dict_set(kwargs.get(), widthKey.get(), aWidth);
                if (aShorten)
                {
                    ref const key{ make_str("max_lines") };
                    if (dict_find(kwargs.get(), key.get()) >= 0)
                        return error("TypeError", "textwrap.TextWrapper() got multiple values for keyword argument 'max_lines'");
                    ref const one{ make_int(1) };
                    dict_set(kwargs.get(), key.get(), one.get());
                }
                return call(cls.get(), nullptr, kwargs.get());
            }

            // (wrap(text, width=70, **kwargs), fill and shorten: the text, the width and the other keyword arguments)
            bool wrap_arguments(char const* aFunction, std::vector<object*> const& aArgs, object* aKwargs, bool aWidthRequired, ref& aText, ref& aWidth, ref& aRest)
            {
                if (aArgs.size() > 2u)
                    return (error("TypeError", std::string{ aFunction } + "() takes from 1 to 2 positional arguments but " + std::to_string(aArgs.size()) + " were given"), false);
                aRest = ref{ make_dict() };
                if (aKwargs != nullptr && !dict_update(aRest.get(), aKwargs))
                    return false;
                auto const take = [&](char const* aName) -> ref
                {
                    ref const key{ make_str(aName) };
                    auto const at = dict_find(aRest.get(), key.get());
                    if (at < 0)
                        return {};
                    ref value{ new_reference(as<dict_object>(aRest.get())->items[static_cast<std::size_t>(at)].second) };
                    dict_erase(*as<dict_object>(aRest.get()), static_cast<std::size_t>(at));
                    return value;
                };
                ref textKeyword{ take("text") };
                ref widthKeyword{ take("width") };
                if (!aArgs.empty() && textKeyword)
                    return (error("TypeError", std::string{ aFunction } + "() got multiple values for argument 'text'"), false);
                if (aArgs.size() > 1u && widthKeyword)
                    return (error("TypeError", std::string{ aFunction } + "() got multiple values for argument 'width'"), false);
                aText = !aArgs.empty() ? ref{ new_reference(aArgs[0]) } : textKeyword;
                aWidth = aArgs.size() > 1u ? ref{ new_reference(aArgs[1]) } : widthKeyword;
                if (!aText)
                    return (error("TypeError", std::string{ aFunction } + "() missing " + (aWidthRequired && !aWidth ? "2 required positional arguments: 'text' and 'width'" :
                        "1 required positional argument: 'text'")), false);
                if (!aWidth)
                {
                    if (aWidthRequired)
                        return (error("TypeError", std::string{ aFunction } + "() missing 1 required positional argument: 'width'"), false);
                    aWidth = ref{ make_int(70) };
                }
                return true;
            }

            builtin_entry const sTextwrapFunctions[] =
            {
                { "wrap", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        ref text, width, rest;
                        if (!wrap_arguments("wrap", aArgs, aKwargs, false, text, width, rest))
                            return nullptr;
                        ref const wrapper{ text_wrapper(width.get(), rest.get(), false) };
                        return wrapper ? call_method_named(wrapper.get(), "wrap", { new_reference(text.get()) }) : nullptr;
                    } },
                { "fill", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        ref text, width, rest;
                        if (!wrap_arguments("fill", aArgs, aKwargs, false, text, width, rest))
                            return nullptr;
                        ref const wrapper{ text_wrapper(width.get(), rest.get(), false) };
                        return wrapper ? call_method_named(wrapper.get(), "fill", { new_reference(text.get()) }) : nullptr;
                    } },
                { "shorten", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        ref text, width, rest;
                        if (!wrap_arguments("shorten", aArgs, aKwargs, true, text, width, rest))
                            return nullptr;
                        ref const wrapper{ text_wrapper(width.get(), rest.get(), true) };
                        if (!wrapper)
                            return nullptr;
                        // (' '.join(text.strip().split()))
                        ref const stripped{ call_method_named(text.get(), "strip", {}) };
                        ref const words{ stripped ? call_method_named(stripped.get(), "split", {}) : nullptr };
                        ref const space{ make_str(" ") };
                        ref const joined{ words ? call_method_named(space.get(), "join", { new_reference(words.get()) }) : nullptr };
                        return joined ? call_method_named(wrapper.get(), "fill", { new_reference(joined.get()) }) : nullptr;
                    } },
                { "dedent", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("dedent", aArgs, aKwargs, { "text" }, 1u, 0u, a))
                            return nullptr;
                        auto const source = str_of_value(a[0]);
                        if (source == nullptr)
                        {
                            ref const r{ call_method_named(a[0], "splitlines", {}) };
                            return r ? error("TypeError", "expected str") : nullptr;
                        }
                        // (lines that are only spaces and tabs: emptied; the common margin of the other lines' leading spaces and tabs)
                        std::vector<std::string> lines;
                        std::size_t start = 0u;
                        for (;;)
                        {
                            auto const end = source->find('\n', start);
                            lines.push_back(source->substr(start, end == std::string::npos ? std::string::npos : end - start));
                            if (end == std::string::npos)
                                break;
                            start = end + 1u;
                        }
                        std::optional<std::string> margin;
                        for (auto& line : lines)
                        {
                            auto const content = line.find_first_not_of(" \t");
                            if (content == std::string::npos)
                            {
                                line.clear();
                                continue;
                            }
                            auto const indent = line.substr(0u, content);
                            if (!margin)
                                margin = indent;
                            else if (indent.starts_with(*margin))
                                ;
                            else if (margin->starts_with(indent))
                                margin = indent;
                            else
                                for (std::size_t i = 0u; i < margin->size() && i < indent.size(); ++i)
                                    if ((*margin)[i] != indent[i])
                                    {
                                        margin = margin->substr(0u, i);
                                        break;
                                    }
                        }
                        std::string result;
                        for (std::size_t i = 0u; i < lines.size(); ++i)
                        {
                            if (i > 0u)
                                result += '\n';
                            auto const& line = lines[i];
                            result += margin && !margin->empty() && line.starts_with(*margin) ? line.substr(margin->size()) : line;
                        }
                        return make_str(std::move(result));
                    } },
                { "indent", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("indent", aArgs, aKwargs, { "text", "prefix", "predicate" }, 2u, 0u, a))
                            return nullptr;
                        ref const lines{ call_method_named(a[0], "splitlines", { make_bool(true) }) };
                        if (!lines)
                            return nullptr;
                        auto const prefix = str_of_value(a[1]);
                        if (prefix == nullptr)
                            return error("TypeError", std::string{ "can only concatenate str (not \"" } + type_name(a[1]) + "\") to str");
                        std::string result;
                        for (auto line : as<list_object>(lines.get())->items)
                        {
                            bool prefixed = false;
                            if (a[2] == nullptr || kind_of(a[2]) == NEOS_PY_NONE)
                                prefixed = string_value(line).empty() || !blank(line);   // (not line.isspace())
                            else
                            {
                                ref const r{ invoke(a[2], { new_reference(line) }) };
                                if (!r)
                                    return nullptr;
                                auto const t = truth(r.get());
                                if (t < 0)
                                    return nullptr;
                                prefixed = t == 1;
                            }
                            if (prefixed)
                                result += *prefix;
                            result += string_value(line);
                        }
                        return make_str(std::move(result));
                    } }
            };

            bool textwrap_module(std::vector<std::pair<std::string, object*>>& aAttributes)
            {
                // (TextWrapper's class attributes: its regular expressions and the translation of whitespace)
                ref const wordSeparator{ compiled_regex(word_separator_pattern().c_str(), 64) };     // (re.VERBOSE)
                ref const simple{ compiled_regex("([\\\t\\\n\\\x0b\\\x0c\\\r\\ ]+)") };
                ref const sentenceEnd{ compiled_regex("[a-z][\\.\\!\\?][\\\"\\']?\\Z") };
                if (!wordSeparator || !simple || !sentenceEnd)
                    return false;
                ref const translation{ make_dict() };
                for (char c : std::string{ "\t\n\x0b\x0c\r " })
                {
                    ref const key{ make_int(static_cast<unsigned char>(c)) };
                    ref const space{ make_int(' ') };
                    dict_set(translation.get(), key.get(), space.get());
                }
                ref const cls{ make_class_of("textwrap", "TextWrapper", {}, sTextWrapperMethods, { { "wordsep_re", new_reference(wordSeparator.get()) },
                    { "wordsep_simple_re", new_reference(simple.get()) }, { "sentence_end_re", new_reference(sentenceEnd.get()) },
                    { "unicode_whitespace_trans", new_reference(translation.get()) } }) };
                if (!cls)
                    return false;
                aAttributes.emplace_back("TextWrapper", new_reference(cls.get()));
                for (auto const& f : sTextwrapFunctions)
                    aAttributes.emplace_back(f.name, make_builtin(f));
                aAttributes.emplace_back("__all__", make_list({ make_str("TextWrapper"), make_str("wrap"), make_str("fill"), make_str("dedent"), make_str("indent"),
                    make_str("shorten") }));
                return true;
            }

            // --- reprlib

            std::pair<char const*, long long> const sReprLimits[] = { { "maxlevel", 6 }, { "maxtuple", 6 }, { "maxlist", 6 }, { "maxarray", 5 }, { "maxdict", 4 },
                { "maxset", 6 }, { "maxfrozenset", 6 }, { "maxdeque", 6 }, { "maxstring", 30 }, { "maxlong", 40 }, { "maxother", 30 } };

            object* repr1(object* aSelf, object* aValue, long long aLevel)
            {
                return call_method_named(aSelf, "repr1", { new_reference(aValue), make_int(aLevel) });
            }

            // (s[:i] + fillvalue + s[len(s)-j:] if s is longer than the limit)
            object* abbreviated(object* aSelf, std::string const& aText, long long aLimit)
            {
                auto const points = to_code_points(aText);
                if (static_cast<long long>(points.size()) <= aLimit)
                    return make_str(aText);
                auto const i = std::max<long long>(0, (aLimit - 3) / 2);
                auto const j = std::max<long long>(0, aLimit - 3 - i);
                ref const fill{ getattr(aSelf, "fillvalue") };
                if (!fill)
                    return nullptr;
                auto const n = static_cast<long long>(points.size());
                return make_str(from_code_points(points.substr(0u, static_cast<std::size_t>(std::min(i, n)))) + text_of(fill.get()) +
                    from_code_points(points.substr(static_cast<std::size_t>(std::max<long long>(0, n - j)))));
            }

            // (Repr._join)
            object* repr_join(object* aSelf, std::vector<std::string> const& aPieces, long long aLevel)
            {
                ref const indent{ getattr(aSelf, "indent") };
                if (!indent)
                    return nullptr;
                if (kind_of(indent.get()) == NEOS_PY_NONE)
                {
                    std::string result;
                    for (std::size_t i = 0u; i < aPieces.size(); ++i)
                        result += (i == 0u ? "" : ", ") + aPieces[i];
                    return make_str(std::move(result));
                }
                if (aPieces.empty())
                    return make_str("");
                std::string unit;
                if (is_int_like(indent.get()))
                {
                    long long n = 0;
                    if (!to_index(indent.get(), n))
                        return nullptr;
                    if (n < 0)
                        return error("ValueError", "Repr.indent cannot be negative int (was " + std::to_string(n) + ")");
                    unit = std::string(static_cast<std::size_t>(n), ' ');
                }
                else if (auto const t = str_of_value(indent.get()))
                    unit = *t;
                else
                {
                    ref const cls{ class_of(indent.get()) };
                    return error("TypeError", "Repr.indent must be a str, int or None, not " + repr_text(cls.get()));
                }
                long long maxLevel = 0;
                if (!attribute_int(aSelf, "maxlevel", maxLevel))
                    return nullptr;
                std::string separator = ",\n";
                for (long long i = 0; i < maxLevel - aLevel + 1; ++i)
                    separator += unit;
                // (sep.join(('', *pieces, ''))[1:-len(indent) or None])
                std::string joined;
                for (std::size_t i = 0u; i < aPieces.size(); ++i)
                    joined += separator + aPieces[i];
                joined += separator;
                auto points = to_code_points(joined);
                points.erase(0u, 1u);
                auto const indentLength = to_code_points(unit).size();
                if (indentLength > 0u)
                    points.erase(points.size() - std::min(indentLength, points.size()));
                return make_str(from_code_points(points));
            }

            // (Repr._repr_iterable)
            object* repr_iterable(object* aSelf, object* aValue, long long aLevel, std::string const& aLeft, std::string aRight, char const* aLimit,
                std::string const& aTrail = {}, bool aSorted = false)
            {
                auto const n = length(aValue);
                if (n < 0)
                    return nullptr;
                ref const fill{ getattr(aSelf, "fillvalue") };
                if (!fill)
                    return nullptr;
                std::string body;
                if (aLevel <= 0 && n > 0)
                    body = text_of(fill.get());
                else
                {
                    long long maxIter = 0;
                    if (!attribute_int(aSelf, aLimit, maxIter))
                        return nullptr;
                    std::vector<object*> items;
                    if (aSorted)
                    {
                        // (sorted if the items can be sorted)
                        ref sorted{ b_sorted({ aValue }, nullptr) };
                        if (!sorted)
                        {
                            clear_error();
                            sorted = ref{ b_list({ aValue }, nullptr) };
                            if (!sorted)
                                return nullptr;
                        }
                        for (auto i : as<list_object>(sorted.get())->items)
                            items.push_back(new_reference(i));
                    }
                    else if (!collect(aValue, items))
                        return nullptr;
                    std::vector<std::string> pieces;
                    for (std::size_t i = 0u; i < items.size(); ++i)
                    {
                        if (static_cast<long long>(i) < maxIter)
                        {
                            ref const r{ repr1(aSelf, items[i], aLevel - 1) };
                            if (!r)
                            {
                                for (auto x : items)
                                    release(x);
                                return nullptr;
                            }
                            pieces.push_back(text_of(r.get()));
                        }
                    }
                    for (auto x : items)
                        release(x);
                    if (n > maxIter)
                        pieces.push_back(text_of(fill.get()));
                    ref const joined{ repr_join(aSelf, pieces, aLevel) };
                    if (!joined)
                        return nullptr;
                    body = text_of(joined.get());
                    ref const indent{ getattr(aSelf, "indent") };
                    if (n == 1 && !aTrail.empty() && indent && kind_of(indent.get()) == NEOS_PY_NONE)
                        aRight = aTrail + aRight;
                }
                return make_str(aLeft + body + aRight);
            }

            bool repr_level(char const* aFunction, std::vector<object*> const& aArgs, object* aKwargs, object*& aValue, long long& aLevel)
            {
                std::vector<object*> a;
                if (!self_arguments(aFunction, aArgs, aKwargs, { "x", "level" }, 2u, 0u, a) || !to_index(a[1], aLevel))
                    return false;
                aValue = a[0];
                return true;
            }

            builtin_entry const sReprMethods[] =
            {
                { "__init__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        if (aArgs.size() != 1u)
                            return error("TypeError", "Repr.__init__() takes 1 positional argument but " + std::to_string(aArgs.size()) + " were given");
                        std::vector<char const*> names;
                        for (auto const& l : sReprLimits)
                            names.push_back(l.first);
                        names.push_back("fillvalue");
                        names.push_back("indent");
                        std::vector<object*> a;
                        if (!parse_arguments("Repr", {}, aKwargs, names, 0u, 0u, a))
                            return nullptr;
                        for (std::size_t i = 0u; i < names.size(); ++i)
                        {
                            ref const value{ a[i] != nullptr ? new_reference(a[i]) : i < std::size(sReprLimits) ? make_int(sReprLimits[i].second) :
                                i == std::size(sReprLimits) ? make_str("...") : new_reference(none()) };
                            if (setattr(aArgs[0], names[i], value.get()) < 0)
                                return nullptr;
                        }
                        return new_reference(none());
                    } },
                { "repr", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        long long level = 0;
                        if (!self_arguments("repr", aArgs, aKwargs, { "x" }, 1u, 0u, a) || !attribute_int(aArgs[0], "maxlevel", level))
                            return nullptr;
                        return repr1(aArgs[0], a[0], level);
                    } },
                { "repr1", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        object* x = nullptr;
                        long long level = 0;
                        if (!repr_level("repr1", aArgs, aKwargs, x, level))
                            return nullptr;
                        auto const self = aArgs[0];
                        ref const cls{ class_of(x) };
                        ref const nameValue{ getattr(cls.get(), "__name__") };
                        if (!nameValue)
                            return nullptr;
                        std::string typeName = text_of(nameValue.get());
                        if (typeName.find(' ') != std::string::npos)
                        {
                            std::string joined;
                            std::size_t start = 0u;
                            for (;;)
                            {
                                auto const p = typeName.find_first_not_of(" \t\n", start);
                                if (p == std::string::npos)
                                    break;
                                auto const e = typeName.find_first_of(" \t\n", p);
                                joined += (joined.empty() ? "" : "_") + typeName.substr(p, e == std::string::npos ? std::string::npos : e - p);
                                if (e == std::string::npos)
                                    break;
                                start = e;
                            }
                            typeName = joined;
                        }
                        ref method{ getattr(self, ("repr_" + typeName).c_str()) };
                        if (!method)
                            clear_error();
                        if (method && truth(method.get()) == 1)
                        {
                            static std::map<std::string, std::string, std::less<>> const sLookup = { { "tuple", "builtins" }, { "list", "builtins" },
                                { "array", "array" }, { "set", "builtins" }, { "frozenset", "builtins" }, { "deque", "collections" }, { "dict", "builtins" },
                                { "str", "builtins" }, { "int", "builtins" } };
                            auto const lookup = sLookup.find(typeName);
                            if (lookup == sLookup.end())
                                return invoke(method.get(), { new_reference(x), make_int(level) });
                            ref const module{ getattr(cls.get(), "__module__") };
                            if (!module)
                                clear_error();
                            if (module && text_of(module.get()) == lookup->second)
                                return invoke(method.get(), { new_reference(x), make_int(level) });
                        }
                        return call_method_named(self, "repr_instance", { new_reference(x), make_int(level) });
                    } },
                { "_join", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        long long level = 0;
                        if (!self_arguments("_join", aArgs, aKwargs, { "pieces", "level" }, 2u, 0u, a) || !to_index(a[1], level))
                            return nullptr;
                        std::vector<object*> items;
                        if (!collect(a[0], items))
                            return nullptr;
                        std::vector<std::string> pieces;
                        for (auto i : items)
                        {
                            pieces.push_back(text_of(i));
                            release(i);
                        }
                        return repr_join(aArgs[0], pieces, level);
                    } },
                { "repr_tuple", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        object* x = nullptr;
                        long long level = 0;
                        return repr_level("repr_tuple", aArgs, aKwargs, x, level) ? repr_iterable(aArgs[0], x, level, "(", ")", "maxtuple", ",") : nullptr;
                    } },
                { "repr_list", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        object* x = nullptr;
                        long long level = 0;
                        return repr_level("repr_list", aArgs, aKwargs, x, level) ? repr_iterable(aArgs[0], x, level, "[", "]", "maxlist") : nullptr;
                    } },
                { "repr_array", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        object* x = nullptr;
                        long long level = 0;
                        if (!repr_level("repr_array", aArgs, aKwargs, x, level))
                            return nullptr;
                        ref const typecode{ getattr(x, "typecode") };
                        if (!typecode)
                            return nullptr;
                        if (length(x) == 0)
                            return make_str("array('" + text_of(typecode.get()) + "')");
                        return repr_iterable(aArgs[0], x, level, "array('" + text_of(typecode.get()) + "', [", "])", "maxarray");
                    } },
                { "repr_set", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        object* x = nullptr;
                        long long level = 0;
                        if (!repr_level("repr_set", aArgs, aKwargs, x, level))
                            return nullptr;
                        if (length(x) == 0)
                            return make_str("set()");
                        return repr_iterable(aArgs[0], x, level, "{", "}", "maxset", {}, true);
                    } },
                { "repr_frozenset", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        object* x = nullptr;
                        long long level = 0;
                        if (!repr_level("repr_frozenset", aArgs, aKwargs, x, level))
                            return nullptr;
                        if (length(x) == 0)
                            return make_str("frozenset()");
                        return repr_iterable(aArgs[0], x, level, "frozenset({", "})", "maxfrozenset", {}, true);
                    } },
                { "repr_deque", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        object* x = nullptr;
                        long long level = 0;
                        return repr_level("repr_deque", aArgs, aKwargs, x, level) ? repr_iterable(aArgs[0], x, level, "deque([", "])", "maxdeque") : nullptr;
                    } },
                { "repr_dict", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        object* x = nullptr;
                        long long level = 0;
                        if (!repr_level("repr_dict", aArgs, aKwargs, x, level))
                            return nullptr;
                        auto const self = aArgs[0];
                        auto const n = length(x);
                        if (n < 0)
                            return nullptr;
                        if (n == 0)
                            return make_str("{}");
                        ref const fill{ getattr(self, "fillvalue") };
                        long long maxDict = 0;
                        if (!fill || !attribute_int(self, "maxdict", maxDict))
                            return nullptr;
                        if (level <= 0)
                            return make_str("{" + text_of(fill.get()) + "}");
                        ref keys{ b_sorted({ x }, nullptr) };
                        if (!keys)
                        {
                            clear_error();
                            keys = ref{ b_list({ x }, nullptr) };
                            if (!keys)
                                return nullptr;
                        }
                        std::vector<std::string> pieces;
                        for (auto const key : as<list_object>(keys.get())->items)
                        {
                            if (static_cast<long long>(pieces.size()) >= maxDict)
                                break;
                            ref const keyRepr{ repr1(self, key, level - 1) };
                            ref const value{ keyRepr ? getitem(x, key) : nullptr };
                            ref const valueRepr{ value ? repr1(self, value.get(), level - 1) : nullptr };
                            if (!valueRepr)
                                return nullptr;
                            pieces.push_back(text_of(keyRepr.get()) + ": " + text_of(valueRepr.get()));
                        }
                        if (n > maxDict)
                            pieces.push_back(text_of(fill.get()));
                        ref const joined{ repr_join(self, pieces, level) };
                        return joined ? make_str("{" + text_of(joined.get()) + "}") : nullptr;
                    } },
                { "repr_str", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        object* x = nullptr;
                        long long level = 0;
                        if (!repr_level("repr_str", aArgs, aKwargs, x, level))
                            return nullptr;
                        auto const self = aArgs[0];
                        long long maxString = 0;
                        ref const fill{ getattr(self, "fillvalue") };
                        if (!fill || !attribute_int(self, "maxstring", maxString))
                            return nullptr;
                        auto const points = to_code_points(text_of(x));
                        auto const n = static_cast<long long>(points.size());
                        auto const slice = [&](long long aStart, long long aEnd) { return from_code_points(points.substr(static_cast<std::size_t>(std::clamp<long long>(aStart, 0, n)),
                            static_cast<std::size_t>(std::max<long long>(0, std::clamp<long long>(aEnd, 0, n) - std::clamp<long long>(aStart, 0, n))))); };
                        std::string s = repr_text(ref{ make_str(slice(0, maxString)) }.get());
                        auto const sp = to_code_points(s);
                        if (static_cast<long long>(sp.size()) > maxString)
                        {
                            auto const i = std::max<long long>(0, (maxString - 3) / 2);
                            auto const j = std::max<long long>(0, maxString - 3 - i);
                            auto const r = to_code_points(repr_text(ref{ make_str(slice(0, i) + slice(n - j, n)) }.get()));
                            auto const rn = static_cast<long long>(r.size());
                            s = from_code_points(r.substr(0u, static_cast<std::size_t>(std::min(i, rn)))) + text_of(fill.get()) +
                                from_code_points(r.substr(static_cast<std::size_t>(std::max<long long>(0, rn - j))));
                        }
                        return make_str(s);
                    } },
                { "repr_int", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        object* x = nullptr;
                        long long level = 0;
                        if (!repr_level("repr_int", aArgs, aKwargs, x, level))
                            return nullptr;
                        long long maxLong = 0;
                        if (!attribute_int(aArgs[0], "maxlong", maxLong))
                            return nullptr;
                        std::string text;
                        if (!repr_of(x, text))
                            return nullptr;
                        return abbreviated(aArgs[0], text, maxLong);
                    } },
                { "repr_instance", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        object* x = nullptr;
                        long long level = 0;
                        if (!repr_level("repr_instance", aArgs, aKwargs, x, level))
                            return nullptr;
                        long long maxOther = 0;
                        if (!attribute_int(aArgs[0], "maxother", maxOther))
                            return nullptr;
                        std::string text;
                        if (!repr_of(x, text))
                        {
                            ref const e{ take_exception() };
                            if (e && !exception_is(e.get(), "Exception"))
                            {
                                raise_exception(e.get());
                                return nullptr;
                            }
                            ref const cls{ class_of(x) };
                            ref const name{ getattr(cls.get(), "__name__") };
                            char address[32];
                            std::snprintf(address, sizeof(address), "%#llx", static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(x)));
                            return make_str("<" + (name ? text_of(name.get()) : std::string{}) + " instance at " + address + ">");
                        }
                        return abbreviated(aArgs[0], text, maxOther);
                    } }
            };

            // (recursive_repr(fillvalue='...'): a decorator; its wrapper returns fillvalue for a recursive call of a repr function (the wrapper
            // binds as a method: its state is its function's data)
            struct recursive_repr_state
            {
                object* function;
                object* fillvalue;
                std::set<std::pair<object*, std::thread::id>> running;
            };

            object* recursive_repr_wrapper(neos_py_context*, void* aData, object* aArgs, object*)
            {
                auto& state = *static_cast<recursive_repr_state*>(aData);
                auto const& args = as<tuple_object>(aArgs)->items;
                if (args.size() != 1u)
                    return error("TypeError", "wrapper() takes 1 positional argument but " + std::to_string(args.size()) + " were given");
                std::pair<object*, std::thread::id> const key{ args[0], std::this_thread::get_id() };
                if (state.running.contains(key))
                    return new_reference(state.fillvalue);
                state.running.insert(key);
                ref const result{ invoke(state.function, { new_reference(args[0]) }) };
                state.running.erase(key);
                return result ? new_reference(result.get()) : nullptr;
            }

            builtin_entry const sRecursiveReprDecorator{ "decorating_function", [](std::vector<object*> const& aArgs, object*) -> object*
                {
                    // (self (the fillvalue), the user's function)
                    if (aArgs.size() != 2u)
                        return error("TypeError", "decorating_function() takes 1 positional argument");
                    // (the state lives as long as the program)
                    auto* const state = new recursive_repr_state{ new_reference(aArgs[1]), new_reference(aArgs[0]) };
                    ref const name{ getattr(aArgs[1], "__name__") };
                    clear_error();
                    return make_function(name ? text_of(name.get()) : std::string{ "wrapper" }, recursive_repr_wrapper, state);
                } };

            builtin_entry const sReprlibFunctions[] =
            {
                { "recursive_repr", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("recursive_repr", aArgs, aKwargs, { "fillvalue" }, 0u, 0u, a))
                            return nullptr;
                        ref const fill{ a[0] != nullptr ? new_reference(a[0]) : make_str("...") };
                        return make_function("decorating_function", call_builtin, const_cast<builtin_entry*>(&sRecursiveReprDecorator), fill.get());
                    } }
            };

            bool reprlib_module(std::vector<std::pair<std::string, object*>>& aAttributes)
            {
                ref const lookup{ make_dict() };
                for (auto const& [k, v] : { std::pair{ "tuple", "builtins" }, std::pair{ "list", "builtins" }, std::pair{ "array", "array" },
                    std::pair{ "set", "builtins" }, std::pair{ "frozenset", "builtins" }, std::pair{ "deque", "collections" }, std::pair{ "dict", "builtins" },
                    std::pair{ "str", "builtins" }, std::pair{ "int", "builtins" } })
                {
                    ref const key{ make_str(k) };
                    ref const value{ make_str(v) };
                    dict_set(lookup.get(), key.get(), value.get());
                }
                ref const cls{ make_class_of("reprlib", "Repr", {}, sReprMethods, { { "_lookup", new_reference(lookup.get()) } }) };
                if (!cls)
                    return false;
                ref const instance{ call(cls.get(), nullptr, nullptr) };
                if (!instance)
                    return false;
                aAttributes.emplace_back("Repr", new_reference(cls.get()));
                aAttributes.emplace_back("aRepr", new_reference(instance.get()));
                aAttributes.emplace_back("repr", getattr(instance.get(), "repr"));
                for (auto const& f : sReprlibFunctions)
                    aAttributes.emplace_back(f.name, make_builtin(f));
                aAttributes.emplace_back("__all__", make_list({ make_str("Repr"), make_str("repr"), make_str("recursive_repr") }));
                return true;
            }

            inline bool const sReprlib = register_module("reprlib", reprlib_module);
            inline bool const sTextwrap = register_module("textwrap", textwrap_module);
        }
    }
}
