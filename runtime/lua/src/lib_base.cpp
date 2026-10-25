/*
  lib_base.cpp

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

// The basic library (_G) and the libraries' registration

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>
#include "lua.hpp"
#include "api.hpp"

namespace neos::lua_runtime
{
    // --- registration

    namespace
    {
        // (the qualified names of library functions ("string.rep"): kept for the program's life)
        char const* qualified_name(char const* aLibrary, char const* aName)
        {
            static std::vector<std::unique_ptr<std::string>> sNames;
            if (aLibrary == nullptr || *aLibrary == '\0')
                return aName;
            sNames.push_back(std::make_unique<std::string>(std::string{ aLibrary } + "." + aName));
            return sNames.back()->c_str();
        }
    }

    void set_function(value aTable, char const* aName, native_function aFunction)
    {
        auto const f = make_native(aFunction, aName);
        value_list keep{ f };
        raw_set(aTable, intern(aName, true), f);
    }

    void register_library(value aTable, char const* aLibrary, std::initializer_list<std::pair<char const*, native_function>> aFunctions)
    {
        value_list keep{ aTable };
        for (auto const& [n, f] : aFunctions)
        {
            auto const v = make_native(f, n);
            fun(v).qualified = qualified_name(aLibrary, n);
            keep.push_back(v);
            raw_set(aTable, intern(n, true), v);
        }
    }

    void register_library(value aTable, std::initializer_list<std::pair<char const*, native_function>> aFunctions)
    {
        register_library(aTable, nullptr, aFunctions);
    }

    value library_table(char const* aName)
    {
        // (a library's table: a global, and in package.loaded)
        auto& s = the_state();
        auto const t = make_table();
        value_list keep{ t };
        raw_set(s.globals, intern(aName, true), t);
        if (is_table(s.loaded))
            raw_set(s.loaded, intern(aName, true), t);
        return t;
    }

    void open_libraries()
    {
        auto& s = the_state();
        s.loaded = make_table();
        open_base();
        open_package();
        open_coroutine();
        open_table();
        open_io();
        open_os();
        open_string();
        open_math();
        open_utf8();
        open_debug();
    }

    // --- chunk names

    std::string chunk_display_name(std::string const& aChunkName)
    {
        // ("=name": name; "@file": file; else the source: [string "its first line..."])
        if (!aChunkName.empty() && aChunkName[0] == '=')
            return aChunkName.substr(1u);
        if (!aChunkName.empty() && aChunkName[0] == '@')
            return aChunkName.substr(1u);
        auto const newline = aChunkName.find_first_of("\r\n");
        std::string first = aChunkName.substr(0u, newline);
        constexpr std::size_t Maximum = 60u - 15u;
        bool truncated = newline != std::string::npos;
        if (first.size() > Maximum)
        {
            first = first.substr(0u, Maximum);
            truncated = true;
        }
        return "[string \"" + first + (truncated ? "...\"]" : "\"]");
    }

    // --- the functions

    namespace
    {
        void lua_print(native_call& c)
        {
            std::string line;
            for (std::size_t i = 0u; i < c.count(); ++i)
            {
                if (i > 0u)
                    line += '\t';
                auto const v = c.arg(i);
                if (is_string(v))
                    line += str(v);
                else
                    line += tostring_text(v);
            }
            line += '\n';
            write_standard(1, line);
        }

        void lua_type(native_call& c)
        {
            check_any(c, 0u);
            c.ret(intern(type_name(c.arg(0u))));
        }

        void lua_tostring(native_call& c)
        {
            check_any(c, 0u);
            c.ret(tostring(c.arg(0u)));
        }

        void lua_tonumber(native_call& c)
        {
            if (c.none(1u) || is_nil(c.arg(1u)))
            {
                check_any(c, 0u);
                auto const v = c.arg(0u);
                value n;
                if (is_number(v))
                    c.ret(v);
                else if (is_string(v) && string_to_number(str_view(v), n))
                    c.ret(n);
                else
                    c.ret(Nil);
                return;
            }
            auto const base = check_integer(c, 1u);
            auto const v = c.arg(0u);
            if (!is_string(v))
                type_error(c, 0u, "string");
            if (base < 2 || base > 36)
                argument_error(c, 1u, "base out of range");
            auto const text = str_view(v);
            std::size_t i = 0u;
            while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i])))
                ++i;
            bool negative = false;
            if (i < text.size() && (text[i] == '-' || text[i] == '+'))
                negative = text[i++] == '-';
            std::uint64_t n = 0u;
            bool any = false;
            for (; i < text.size(); ++i)
            {
                auto const ch = static_cast<unsigned char>(text[i]);
                int digit;
                if (std::isdigit(ch))
                    digit = ch - '0';
                else if (std::isalpha(ch))
                    digit = std::tolower(ch) - 'a' + 10;
                else
                    break;
                if (digit >= base)
                {
                    c.ret(Nil);
                    return;
                }
                n = n * static_cast<std::uint64_t>(base) + static_cast<std::uint64_t>(digit);
                any = true;
            }
            while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i])))
                ++i;
            if (!any || i != text.size())
            {
                c.ret(Nil);
                return;
            }
            c.ret(make_integer(static_cast<std::int64_t>(negative ? 0u - n : n)));
        }

        void lua_ipairs_iterator(native_call& c)
        {
            auto const i = integer_of(c.arg(1u)) + 1;
            auto const t = c.arg(0u);
            value v;
            if (is_table(t) && !is_table(tab(t).metatable))
                v = tab(t).get_integer(i);
            else
                v = index(t, make_integer(i));
            if (is_nil(v))
            {
                c.ret(Nil);
                return;
            }
            c.ret(make_integer(i));
            c.ret(v);
        }

        void lua_ipairs(native_call& c)
        {
            check_any(c, 0u);
            static static_values tIterator;
            if (tIterator.empty())
            {
                tIterator.push_back(make_native(&lua_ipairs_iterator, "ipairs_iterator"));
            }
            c.ret(tIterator[0]);
            c.ret(c.arg(0u));
            c.ret(make_integer(0));
        }

        void lua_next(native_call& c)
        {
            auto& t = check_table(c, 0u);
            value key = c.arg(1u);
            value v = Nil;
            if (t.next(key, v))
            {
                c.ret(key);
                c.ret(v);
            }
            else
                c.ret(Nil);
        }

        void lua_pairs(native_call& c)
        {
            check_any(c, 0u);
            auto const handler = metamethod(c.arg(0u), NamePairs);
            if (!is_nil(handler))
            {
                value_list results;
                value const arguments[] = { c.arg(0u) };
                call(handler, arguments, results);
                for (std::size_t i = 0u; i < 3u; ++i)
                    c.ret(i < results.size() ? results[i] : Nil);
                return;
            }
            if (!is_table(c.arg(0u)))
                type_error(c, 0u, "table");
            c.ret(index(the_state().globals, intern("next")));
            c.ret(c.arg(0u));
            c.ret(Nil);
        }

        void lua_select(native_call& c)
        {
            auto const n = c.arg(0u);
            auto const count = static_cast<std::int64_t>(c.count()) - 1;
            if (is_string(n) && str(n) == "#")
            {
                c.ret(make_integer(count));
                return;
            }
            auto i = check_integer(c, 0u);
            if (i < 0)
            {
                i = count + i;
                if (i < 0)
                    argument_error(c, 0u, "index out of range");
            }
            else if (i == 0)
                argument_error(c, 0u, "index out of range");
            else
                i = i - 1;
            if (i > count)
                i = count;
            for (auto k = static_cast<std::size_t>(i) + 1u; k < c.count(); ++k)
                c.ret(c.args[k]);
        }

        void lua_error_(native_call& c)
        {
            auto const level = optional_integer(c, 1u, 1);
            auto v = c.arg(0u);
            if (is_string(v) && level > 0 && c.direct)
                v = make_string(where(static_cast<int>(level)) + str(v));
            throw_value(v);
        }

        void lua_assert(native_call& c)
        {
            if (truthy(c.arg(0u)))
            {
                for (auto v : c.args)
                    c.ret(v);
                return;
            }
            check_any(c, 0u);
            // (as error(message, 1))
            auto message = c.count() >= 2u ? c.arg(1u) : make_string("assertion failed!");
            if (is_string(message) && c.direct)
                message = make_string(where(1) + str(message));
            throw_value(message);
        }

        void lua_pcall(native_call& c)
        {
            check_any(c, 0u);
            protector p{ Nil };
            auto const results = c.results.size();
            c.ret(True);
            try
            {
                call(c.arg(0u), c.args.subspan(1u), c.results, Nil, false);
            }
            catch (lua_error const& e)
            {
                if (the_state().exitRequested)
                    throw;
                value_list keep{ e.v };
                p.restore(e.v);
                c.results->resize(results);
                c.ret(False);
                c.ret(e.v);
            }
        }

        void lua_xpcall(native_call& c)
        {
            check_any(c, 1u);
            auto const handler = c.arg(1u);
            protector p{ handler };
            auto const results = c.results.size();
            c.ret(True);
            try
            {
                call(c.arg(0u), c.args.subspan(2u), c.results, Nil, false);
            }
            catch (lua_error const& e)
            {
                if (the_state().exitRequested)
                    throw;
                value_list keep{ e.v };
                p.restore(e.v);
                auto error = e.v;
                if (!p.handled)
                {
                    // (an error that the handler was not called for where it was raised)
                    try
                    {
                        value const arguments[] = { e.v };
                        error = call1(handler, arguments);
                    }
                    catch (lua_error const&)
                    {
                        error = make_string("error in error handling");
                    }
                }
                keep.push_back(error);
                c.results->resize(results);
                c.ret(False);
                c.ret(error);
            }
        }

        void lua_getmetatable(native_call& c)
        {
            check_any(c, 0u);
            auto const mt = metatable_of(c.arg(0u));
            if (is_table(mt))
            {
                auto const protectedValue = tab(mt).get_string(name(NameMetatable));
                if (!is_nil(protectedValue))
                {
                    c.ret(protectedValue);
                    return;
                }
            }
            c.ret(mt);
        }

        void lua_setmetatable(native_call& c)
        {
            auto& t = check_table(c, 0u);
            auto const mt = c.arg(1u);
            if (!is_nil(mt) && !is_table(mt))
                type_error(c, 1u, "nil or table");
            if (c.none(1u))
                type_error(c, 1u, "nil or table");
            if (is_table(t.metatable) && !is_nil(tab(t.metatable).get_string(name(NameMetatable))))
                runtime_error("cannot change a protected metatable");
            t.metatable = mt;
            t.weakKeys = 0u;
            t.weakValues = 0u;
            if (is_table(mt))
            {
                auto const mode = tab(mt).get_string(name(NameMode));
                if (is_string(mode))
                {
                    t.weakKeys = str(mode).find('k') != std::string::npos ? 1u : 0u;
                    t.weakValues = str(mode).find('v') != std::string::npos ? 1u : 0u;
                }
            }
            c.ret(c.arg(0u));
        }

        void lua_rawequal(native_call& c)
        {
            check_any(c, 0u);
            check_any(c, 1u);
            c.ret(boolean(raw_equal(c.arg(0u), c.arg(1u))));
        }

        void lua_rawlen(native_call& c)
        {
            auto const v = c.arg(0u);
            if (is_table(v))
                c.ret(make_integer(tab(v).length()));
            else if (is_string(v))
                c.ret(make_integer(static_cast<std::int64_t>(str(v).size())));
            else
                argument_error(c, 0u, "table or string expected");
        }

        void lua_rawget(native_call& c)
        {
            check_table(c, 0u);
            check_any(c, 1u);
            c.ret(raw_get(c.arg(0u), c.arg(1u)));
        }

        void lua_rawset(native_call& c)
        {
            check_table(c, 0u);
            check_any(c, 1u);
            check_any(c, 2u);
            raw_set(c.arg(0u), c.arg(1u), c.arg(2u));
            c.ret(c.arg(0u));
        }

        void lua_collectgarbage(native_call& c)
        {
            auto const option = std::string{ optional_string(c, 0u, "collect") };
            if (option == "collect")
            {
                collect_garbage();
                c.ret(make_integer(0));
            }
            else if (option == "count")
            {
                auto const bytes = static_cast<double>(the_heap().live_bytes());
                c.ret(make_float(bytes / 1024.0));
            }
            else if (option == "step")
            {
                collect_garbage();
                c.ret(True);
            }
            else if (option == "isrunning")
                c.ret(boolean(!gc_stopped()));
            else if (option == "stop")
            {
                gc_stop(true);
                c.ret(make_integer(0));
            }
            else if (option == "restart")
            {
                gc_stop(false);
                c.ret(make_integer(0));
            }
            else if (option == "incremental" || option == "generational")
                c.ret(intern("incremental"));
            else if (option == "setpause" || option == "setstepmul")
                c.ret(make_integer(100));
            else
                argument_error(c, 0u, "invalid option '" + option + "'");
        }

        std::string read_file(std::string const& aPath, bool& aOk)
        {
            std::ifstream file{ aPath, std::ios::binary };
            aOk = static_cast<bool>(file);
            if (!aOk)
                return {};
            std::ostringstream text;
            text << file.rdbuf();
            auto result = text.str();
            // (a first line that begins with '#' (e.g. #!/usr/bin/lua): skipped (its line break kept))
            if (!result.empty() && result[0] == '#')
            {
                auto const newline = result.find('\n');
                result = newline == std::string::npos ? std::string{} : result.substr(newline);
            }
            return result;
        }

        void return_chunk(native_call& c, value aFunction, std::string const& aError)
        {
            if (is_function(aFunction))
                c.ret(aFunction);
            else
            {
                c.ret(Nil);
                c.ret(make_string(aError));
            }
        }

        void lua_load(native_call& c)
        {
            auto const chunk = c.arg(0u);
            std::string source;
            std::string chunkName;
            if (is_string(chunk))
            {
                source = str(chunk);
                chunkName = std::string{ optional_string(c, 1u, source) };
            }
            else if (is_function(chunk))
            {
                // (a function: called until it returns nil or an empty string; the pieces are the source)
                for (;;)
                {
                    auto const piece = call1(chunk, std::span<value const>{});
                    if (is_nil(piece) || (is_string(piece) && str(piece).empty()))
                        break;
                    if (!is_string(piece))
                    {
                        c.ret(Nil);
                        c.ret(make_string("reader function must return a string"));
                        return;
                    }
                    source += str(piece);
                }
                chunkName = std::string{ optional_string(c, 1u, "=(load)") };
            }
            else
                type_error(c, 0u, "string");
            auto const mode = std::string{ optional_string(c, 2u, "bt") };
            std::string error;
            auto const f = load_chunk(source, chunk_display_name(chunkName), mode, c.none(3u) ? Nil : c.arg(3u), error);
            if (!c.none(3u) && is_nil(c.arg(3u)) && is_function(f))
                fun(f).environment = make_box(Nil);
            return_chunk(c, f, error);
        }

        void lua_loadfile(native_call& c)
        {
            auto const path = std::string{ optional_string(c, 0u, "") };
            auto const mode = std::string{ optional_string(c, 1u, "bt") };
            bool ok = false;
            auto const source = read_file(path, ok);
            if (!ok)
            {
                c.ret(Nil);
                c.ret(make_string("cannot open " + path));
                return;
            }
            std::string error;
            auto const f = load_chunk(source, chunk_display_name("@" + path), mode, c.none(2u) ? Nil : c.arg(2u), error);
            return_chunk(c, f, error);
        }

        void lua_dofile(native_call& c)
        {
            auto const path = std::string{ optional_string(c, 0u, "") };
            bool ok = false;
            auto const source = read_file(path, ok);
            if (!ok)
                runtime_error("cannot open " + path);
            std::string error;
            auto const f = load_chunk(source, chunk_display_name("@" + path), "bt", Nil, error);
            if (!is_function(f))
                throw_value(make_string(error));
            value_list keep{ f };
            call(f, {}, c.results);
        }

        void lua_warn(native_call& c)
        {
            auto& s = the_state();
            std::string message;
            for (std::size_t i = 0u; i < c.count(); ++i)
                message += check_string(c, i);
            if (c.count() == 0u)
                check_string(c, 0u);
            if (c.count() == 1u && !message.empty() && message[0] == '@')
            {
                if (message == "@on")
                    s.warnOn = true;
                else if (message == "@off")
                    s.warnOn = false;
                return;
            }
            if (s.warnOn)
                write_standard(2, "Lua warning: " + message + "\n");
        }
    }

    void open_base()
    {
        auto& s = the_state();
        auto const g = s.globals;
        register_library(g, nullptr, {
            { "print", &lua_print },
            { "type", &lua_type },
            { "tostring", &lua_tostring },
            { "tonumber", &lua_tonumber },
            { "ipairs", &lua_ipairs },
            { "pairs", &lua_pairs },
            { "next", &lua_next },
            { "select", &lua_select },
            { "error", &lua_error_ },
            { "assert", &lua_assert },
            { "pcall", &lua_pcall },
            { "xpcall", &lua_xpcall },
            { "getmetatable", &lua_getmetatable },
            { "setmetatable", &lua_setmetatable },
            { "rawequal", &lua_rawequal },
            { "rawlen", &lua_rawlen },
            { "rawget", &lua_rawget },
            { "rawset", &lua_rawset },
            { "collectgarbage", &lua_collectgarbage },
            { "load", &lua_load },
            { "loadfile", &lua_loadfile },
            { "dofile", &lua_dofile },
            { "warn", &lua_warn } });
        raw_set(g, intern("_G", true), g);
        raw_set(g, intern("_VERSION", true), intern("Lua 5.4", true));
        raw_set(s.loaded, intern("_G", true), g);
    }

    std::string read_source_file(std::string const& aPath, bool& aOk)
    {
        return read_file(aPath, aOk);
    }
}
