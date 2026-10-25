/*
  lib_coroutine.cpp

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

// The coroutine library

#include "lua.hpp"
#include "api.hpp"

namespace neos::lua_runtime
{
    namespace
    {
        thread_cell& check_thread(native_call& c, std::size_t aIndex)
        {
            auto const v = c.arg(aIndex);
            if (!is_type(v, cell_type::Thread))
                type_error(c, aIndex, "coroutine");
            return cell_of<thread_cell>(v);
        }

        value running_thread()
        {
            auto& s = the_state();
            return s.current != nullptr ? value_of(*s.current) : s.mainThread;
        }

        void coroutine_create(native_call& c)
        {
            if (!is_function(c.arg(0u)))
                type_error(c, 0u, "function");
            c.ret(make_thread(c.arg(0u)));
        }

        void coroutine_resume(native_call& c)
        {
            auto& co = check_thread(c, 0u);
            value_list keep{ c.arg(0u) };
            auto const base = c.results.size();
            c.ret(True);
            if (!resume(co, c.args.subspan(1u), c.results))
                c.results[base] = False;
        }

        void coroutine_yield(native_call& c)
        {
            yield_values(c.args, c.results);
        }

        void coroutine_status(native_call& c)
        {
            auto& co = check_thread(c, 0u);
            auto& s = the_state();
            char const* status = "suspended";
            if (&co == s.current || (s.current == nullptr && c.arg(0u) == s.mainThread))
                status = "running";
            else if (c.arg(0u) == s.mainThread)
                status = "normal";
            else
            {
                switch (co.status)
                {
                case thread_cell::status_t::Suspended: status = "suspended"; break;
                case thread_cell::status_t::Running: status = "running"; break;
                case thread_cell::status_t::Normal: status = "normal"; break;
                case thread_cell::status_t::Dead: status = "dead"; break;
                }
            }
            c.ret(intern(status));
        }

        void coroutine_wrapped(native_call& c)
        {
            auto const thread = c.self.upvalues[0];
            value_list results;
            if (!resume(cell_of<thread_cell>(thread), c.args, results))
            {
                auto const error = results.empty() ? Nil : results[0];
                throw_value(error);
            }
            for (auto v : results)
                c.ret(v);
        }

        void coroutine_wrap(native_call& c)
        {
            if (!is_function(c.arg(0u)))
                type_error(c, 0u, "function");
            auto const thread = make_thread(c.arg(0u));
            value_list keep{ thread };
            auto const f = make_native(&coroutine_wrapped, "wrap", { thread });
            c.ret(f);
        }

        void coroutine_isyieldable(native_call& c)
        {
            if (!c.none(0u))
            {
                auto& co = check_thread(c, 0u);
                c.ret(boolean(c.arg(0u) != the_state().mainThread && co.status == thread_cell::status_t::Running));
                return;
            }
            c.ret(boolean(yieldable()));
        }

        void coroutine_running(native_call& c)
        {
            c.ret(running_thread());
            c.ret(boolean(the_state().current == nullptr));
        }

        void coroutine_close(native_call& c)
        {
            auto& co = check_thread(c, 0u);
            auto& s = the_state();
            if (&co == s.current || c.arg(0u) == s.mainThread)
                runtime_error("cannot close a running coroutine");
            if (co.status == thread_cell::status_t::Normal || co.status == thread_cell::status_t::Running)
                runtime_error("cannot close a normal coroutine");
            value error = Nil;
            if (close_thread(co, error))
                c.ret(True);
            else
            {
                c.ret(False);
                c.ret(error);
            }
        }
    }

    void open_coroutine()
    {
        auto const t = library_table("coroutine");
        register_library(t, "coroutine", {
            { "create", &coroutine_create },
            { "resume", &coroutine_resume },
            { "yield", &coroutine_yield },
            { "status", &coroutine_status },
            { "wrap", &coroutine_wrap },
            { "isyieldable", &coroutine_isyieldable },
            { "running", &coroutine_running },
            { "close", &coroutine_close } });
    }
}
