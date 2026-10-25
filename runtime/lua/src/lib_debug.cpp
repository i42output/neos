/*
  lib_debug.cpp

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

// The debug library (what the host's frames can tell: tracebacks and functions' and frames' sources and lines; hooks and locals
// are not supported), and the script's arguments (the arg table)

#include <algorithm>
#include "lua.hpp"
#include "api.hpp"

namespace neos::lua_runtime
{
    namespace
    {
        std::string short_source(char const* aSource)
        {
            std::string source = aSource != nullptr ? aSource : "?";
            auto const slash = source.find_last_of("/\\");
            if (slash != std::string::npos && source.find("[string") != 0u)
                source = source.substr(slash + 1u);
            return source;
        }

        void debug_traceback(native_call& c)
        {
            std::size_t first = 0u;
            if (is_type(c.arg(0u), cell_type::Thread))
                first = 1u;
            auto const message = c.arg(first);
            if (!is_nil(message) && !is_string(message) && !is_number(message))
            {
                c.ret(message);
                return;
            }
            auto const level = optional_integer(c, first + 1u, 1);
            auto const text = is_nil(message) ? std::string{} : (is_string(message) ? str(message) : number_text(message));
            c.ret(make_string(traceback(text, static_cast<int>(std::max<std::int64_t>(level, 1)))));
        }

        void debug_getinfo(native_call& c)
        {
            std::size_t first = 0u;
            if (is_type(c.arg(0u), cell_type::Thread))
                first = 1u;
            auto const target = c.arg(first);
            auto const what = std::string{ optional_string(c, first + 1u, "flnSrtu") };
            auto const t = make_table();
            value_list keep{ t };
            auto& info = tab(t);
            if (is_function(target))
            {
                auto& f = fun(target);
                if (f.native)
                {
                    info.set(intern("what"), intern("C"));
                    info.set(intern("source"), intern("=[C]"));
                    info.set(intern("short_src"), intern("[C]"));
                    info.set(intern("currentline"), make_integer(-1));
                    info.set(intern("linedefined"), make_integer(-1));
                    info.set(intern("lastlinedefined"), make_integer(-1));
                }
                else
                {
                    neos_rt_frame frame{};
                    auto const* h = host();
                    if (h != nullptr && h->version >= 9u && h->function_frame != nullptr &&
                        h->function_frame(f.program, f.index, &frame) != 0u)
                    {
                        info.set(intern("source"), make_string("@" + std::string{ frame.source != nullptr ? frame.source : "?" }));
                        info.set(intern("short_src"), make_string(short_source(frame.source)));
                        info.set(intern("linedefined"), make_integer(frame.firstLine));
                        info.set(intern("what"), intern(frame.firstLine <= 1u && std::string{ frame.function != nullptr ? frame.function : "" } == "main chunk" ? "main" : "Lua"));
                    }
                    else
                        info.set(intern("what"), intern("Lua"));
                    info.set(intern("currentline"), make_integer(-1));
                }
                info.set(intern("nups"), make_integer(static_cast<std::int64_t>(f.upvalues.size())));
                info.set(intern("func"), target);
                c.ret(t);
                return;
            }
            auto const level = check_integer(c, first);
            if (level < 0)
                argument_error(c, first, "level out of range");
            if (level == 0)
            {
                // (getinfo itself)
                info.set(intern("what"), intern("C"));
                info.set(intern("source"), intern("=[C]"));
                info.set(intern("short_src"), intern("[C]"));
                info.set(intern("currentline"), make_integer(-1));
                info.set(intern("name"), intern("getinfo"));
                c.ret(t);
                return;
            }
            auto const* h = host();
            neos_rt_frame frames[256];
            auto const count = h != nullptr && h->frames != nullptr ? h->frames(frames, 256u) : 0u;
            if (static_cast<std::uint64_t>(level) > count)
            {
                c.ret(Nil);
                return;
            }
            auto const& frame = frames[level - 1];
            std::string const function = frame.function != nullptr ? frame.function : "";
            info.set(intern("source"), make_string("@" + std::string{ frame.source != nullptr ? frame.source : "?" }));
            info.set(intern("short_src"), make_string(short_source(frame.source)));
            info.set(intern("currentline"), make_integer(frame.line));
            info.set(intern("linedefined"), make_integer(frame.firstLine));
            info.set(intern("what"), intern(function == "main chunk" ? "main" : "Lua"));
            // (its name: as its frame's name ("local 'f'": f))
            auto const quote = function.find('\'');
            if (quote != std::string::npos && function.find('\'', quote + 1u) != std::string::npos)
            {
                info.set(intern("name"), make_string(function.substr(quote + 1u, function.find('\'', quote + 1u) - quote - 1u)));
                info.set(intern("namewhat"), make_string(function.substr(0u, quote > 0u ? quote - 1u : 0u)));
            }
            else
                info.set(intern("namewhat"), intern(""));
            (void)what;
            c.ret(t);
        }

        void debug_getmetatable(native_call& c)
        {
            check_any(c, 0u);
            c.ret(metatable_of(c.arg(0u)));
        }

        void debug_setmetatable(native_call& c)
        {
            auto const v = c.arg(0u);
            auto const mt = c.arg(1u);
            if (!is_nil(mt) && !is_table(mt))
                type_error(c, 1u, "nil or table");
            auto& s = the_state();
            if (is_table(v))
                tab(v).metatable = mt;
            else if (is_type(v, cell_type::Userdata))
                cell_of<userdata_cell>(v).metatable = mt;
            else if (is_string(v))
                s.stringMetatable = mt;
            else if (is_nil(v))
                s.typeMetatables[0] = mt;
            else if (is_boolean(v))
                s.typeMetatables[1] = mt;
            else if (is_number(v))
                s.typeMetatables[2] = mt;
            else if (is_function(v))
                s.typeMetatables[3] = mt;
            else if (is_type(v, cell_type::Thread))
                s.typeMetatables[4] = mt;
            c.ret(v);
        }

        void debug_getregistry(native_call& c)
        {
            c.ret(the_state().registry);
        }

        void debug_nothing(native_call&)
        {
        }

        void debug_getlocal(native_call& c)
        {
            c.ret(Nil);
        }

        void debug_getupvalue(native_call& c)
        {
            // (Lua functions' upvalues are not named: none)
            c.ret(Nil);
        }

        void debug_upvalueid(native_call& c)
        {
            auto const f = c.arg(0u);
            auto const n = check_integer(c, 1u);
            if (!is_function(f) || n < 1 || static_cast<std::size_t>(n) > fun(f).upvalues.size())
            {
                c.ret(Nil);
                return;
            }
            c.ret(make_integer(static_cast<std::int64_t>(handle_of(fun(f).upvalues[static_cast<std::size_t>(n - 1)]))));
        }

        void debug_getuservalue(native_call& c)
        {
            auto const v = c.arg(0u);
            if (is_type(v, cell_type::Userdata))
            {
                c.ret(cell_of<userdata_cell>(v).user);
                c.ret(True);
            }
            else
            {
                c.ret(Nil);
                c.ret(False);
            }
        }
    }

    void open_debug()
    {
        auto const t = library_table("debug");
        register_library(t, "debug", {
            { "traceback", &debug_traceback },
            { "getinfo", &debug_getinfo },
            { "getmetatable", &debug_getmetatable },
            { "setmetatable", &debug_setmetatable },
            { "getregistry", &debug_getregistry },
            { "sethook", &debug_nothing },
            { "gethook", &debug_nothing },
            { "getlocal", &debug_getlocal },
            { "setlocal", &debug_getlocal },
            { "getupvalue", &debug_getupvalue },
            { "setupvalue", &debug_getupvalue },
            { "upvalueid", &debug_upvalueid },
            { "getuservalue", &debug_getuservalue },
            { "debug", &debug_nothing } });
    }

    void set_script_arguments()
    {
        // (the arg table: the script (arg[0]) and its arguments (arg[1] ...))
        auto& s = the_state();
        auto const* h = host();
        auto const t = make_table();
        value_list keep{ t };
        if (h != nullptr && h->version >= 7u && h->arguments != nullptr)
        {
            auto const count = h->arguments(nullptr, 0u);
            std::vector<char const*> arguments(count);
            h->arguments(arguments.data(), count);
            for (unsigned i = 0u; i < count; ++i)
                tab(t).set_integer(static_cast<std::int64_t>(i), make_string(arguments[i] != nullptr ? arguments[i] : ""));
        }
        raw_set(s.globals, intern("arg"), t);
    }
}
