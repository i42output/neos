/*
  call.cpp

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

// Calls: of native functions, of compiled functions (through the host), protected calls, coroutines (the host's coroutines: each
// Lua coroutine runs on a native stack of its own), load (the host compiles a chunk while the program runs) and tracebacks

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include "lua.hpp"
#include "api.hpp"

namespace neos::lua_runtime
{
    namespace
    {
        neos_rt_host const* sHost = nullptr;

        // (the programs (the main program and the chunks that load compiled): their dispatchers and their %lua.nret globals)
        std::vector<program_info>& programs()
        {
            static std::vector<program_info> sPrograms;
            return sPrograms;
        }

        NEOS_LUA_FAST_TLS thread_local std::size_t tResultsCount = 0u;

        thread_local std::string tUncaughtTraceback;
        NEOS_LUA_FAST_TLS thread_local function_cell const* tNative = nullptr;   ///< (the native function that is running (null: Lua code))

        // (while it exists: the native function (null: Lua code) is running)
        struct running_native
        {
            explicit running_native(function_cell const* aNative) : previous{ tNative } { tNative = aNative; }
            ~running_native() { tNative = previous; }
            function_cell const* previous;
        };
        thread_local bool tInHandler = false;
    }

    neos_rt_host const* host()
    {
        return sHost;
    }

    void set_host(neos_rt_host const* aHost)
    {
        sHost = aHost;
    }

    void* current_program()
    {
        return sHost != nullptr && sHost->thread_context != nullptr ? sHost->thread_context() : nullptr;
    }

    program_info* find_program(void* aProgram)
    {
        for (auto& p : programs())
            if (p.program == aProgram)
                return &p;
        return nullptr;
    }

    program_info& register_program(std::uint32_t aDispatcher, std::uint32_t aResultsGlobal)
    {
        auto* const program = current_program();
        auto* existing = find_program(program);
        if (existing == nullptr)
        {
            programs().push_back(program_info{});
            existing = &programs().back();
        }
        existing->program = program;
        existing->dispatcher = aDispatcher;
        existing->resultsGlobal = aResultsGlobal;
        existing->results = sHost != nullptr && sHost->version >= 10u && sHost->global != nullptr ? sHost->global(aResultsGlobal) : nullptr;
        if (existing->results == nullptr)
        {
            std::fprintf(stderr, "lua: the host does not provide the program's globals\n");
            std::abort();
        }
        return *existing;
    }

    void forget_program(void* aProgram)
    {
        std::erase_if(programs(), [&](program_info const& p) { return p.program == aProgram; });
    }

    std::size_t results_count()
    {
        return tResultsCount;
    }

    void set_results_count(std::size_t aCount)
    {
        tResultsCount = aCount;
        if (auto* p = find_program(current_program()))
            *p->results = aCount;
    }

    void set_program_results(std::span<value const> aResults)
    {
        // (a call's results as compiled code receives them: its first (returned), the others in the thread's results, their number in
        // the program's %lua.nret)
        auto& r = stacks().results;
        r.clear();
        if (aResults.size() > 1u)
            r.assign(aResults.begin() + 1, aResults.end());
        set_results_count(aResults.size());
    }

    // --- calls

    value call_compiled(function_cell& aFunction, std::span<value const> aArguments)
    {
        // (the arguments after the first four are pushed on the extra arguments stack while it runs)
        auto& extra = stacks().extra;
        auto const base = extra.size();
        for (std::size_t i = 4u; i < aArguments.size(); ++i)
            extra.push_back(aArguments[i]);
        neos_rt_type const types[] = { NEOS_RT_I64, NEOS_RT_I32, NEOS_RT_I64, NEOS_RT_I64, NEOS_RT_I64, NEOS_RT_I64 };
        neos_rt_value values[6];
        values[0].i64 = static_cast<long long>(value_of(aFunction));
        values[1].i32 = static_cast<int>(aArguments.size());
        for (std::size_t i = 0u; i < 4u; ++i)
            values[2u + i].i64 = static_cast<long long>(i < aArguments.size() ? aArguments[i] : Nil);
        neos_rt_value result{};
#if defined(_WIN32)
        // (generated code runs on a fiber of its own: this stack's live part is scanned while it runs)
        NEOS_LUA_SPILL_REGISTERS();
        stack_switch const switching{ stack_pointer() };
#endif
        // (the frame of the Lua function that called the native function that is calling it: for error levels (see where))
        struct boundary
        {
            boundary()
            {
                neos_rt_frame frame{};
                auto const* h = sHost;
                stacks().boundaries.push_back(h != nullptr && h->frames != nullptr && h->frames(&frame, 1u) == 1u ? frame.id : 0u);
            }
            ~boundary()
            {
                stacks().boundaries.pop_back();
            }
        } const callBoundary;
        auto* const program = aFunction.program;
        void* previous = nullptr;
        if (program != nullptr && program != current_program())
        {
            previous = current_program();
            sHost->thread_attach(program);
        }
        auto const trap = sHost->call(aFunction.index, 6u, types, values, NEOS_RT_I64, &result);
        std::size_t count = 1u;
        if (auto* p = find_program(program != nullptr ? program : current_program()))
            count = static_cast<std::size_t>(*p->results);
        if (previous != nullptr)
            sHost->thread_attach(previous);
        extra.resize(std::min(extra.size(), base));
        if (trap != nullptr)
        {
            std::string const message{ trap };
            if (message.find("stack") != std::string::npos)
                runtime_error("stack overflow");
            runtime_error("internal error: " + message);
        }
        auto const r = static_cast<value>(result.i64);
        if (r == Exception)
        {
            auto const thrown = the_state().pending;
            the_state().pending = Nil;
            throw lua_error{ thrown };
        }
        tResultsCount = count;
        return r;
    }

    void call(value aFunction, std::span<value const> aArguments, value_list& aResults, value aDescription)
    {
        call(aFunction, aArguments, aResults, aDescription, false);
    }

    void call(value aFunction, std::span<value const> aArguments, value_list& aResults, value aDescription, bool aDirect)
    {
        if (is_function(aFunction))
        {
            auto& f = fun(aFunction);
            if (f.native)
            {
                native_call c{ aArguments, aResults, f, aDirect };
                c.method = aDirect && is_string(aDescription) && str(aDescription).starts_with("method '");
                indirect_scope const scope{ !aDirect };
                running_native const running{ &f };
                f.function(c);
                return;
            }
            indirect_scope const scope{ false };
            running_native const running{ nullptr };
            auto const first = call_compiled(f, aArguments);
            auto const count = tResultsCount;
            if (count >= 1u)
                aResults.push_back(first);
            auto const& r = stacks().results;
            for (std::size_t i = 1u; i < count; ++i)
                aResults.push_back(i - 1u < r.size() ? r[i - 1u] : Nil);
            return;
        }
        // (a value with a __call metamethod: called with the value as its first argument)
        auto const handler = metamethod(aFunction, NameCall);
        if (is_nil(handler))
        {
            if (is_string(aDescription))
                runtime_error("attempt to call a " + type_name(aFunction) + " value (" + str(aDescription) + ")");
            runtime_error("attempt to call a " + type_name(aFunction) + " value");
        }
        value_list arguments;
        arguments->reserve(aArguments.size() + 1u);
        arguments.push_back(aFunction);
        arguments->insert(arguments->end(), aArguments.begin(), aArguments.end());
        call(handler, arguments.span(), aResults, Nil, aDirect);
    }

    value call1(value aFunction, std::span<value const> aArguments)
    {
        value_list results;
        call(aFunction, aArguments, results);
        return results.empty() ? Nil : results[0];
    }

    value call1(value aFunction, std::initializer_list<value> aArguments)
    {
        return call1(aFunction, std::span<value const>{ aArguments.begin(), aArguments.size() });
    }

    // --- errors

    void throw_value(value aValue)
    {
        // (an error raised: the innermost protected call's handler (xpcall's) is called now (where the error is, its frames intact);
        // an error that nothing will handle: its traceback is kept, for its report)
        auto& protectors = stacks().protectors;
        if (the_state().exitRequested)
            throw lua_error{ aValue };
        if (!protectors.empty())
        {
            auto* p = protectors.back();
            if (!is_nil(p->handler) && !tInHandler)
            {
                tInHandler = true;
                value result = aValue;
                try
                {
                    value const arguments[] = { aValue };
                    result = call1(p->handler, arguments);
                    p->handled = true;
                }
                catch (lua_error const& e)
                {
                    // (an error in the handler)
                    result = make_string("error in error handling");
                    p->handled = true;
                }
                catch (...)
                {
                }
                tInHandler = false;
                throw lua_error{ result };
            }
        }
        else if (the_state().current == nullptr)
        {
            // (not in a coroutine (whose resume handles it))
            value_list keep{ aValue };
            std::string message;
            bool withTraceback = true;
            if (is_string(aValue) || is_number(aValue))
                message = is_string(aValue) ? str(aValue) : number_text(aValue);
            else if (!is_nil(metamethod(aValue, NameToString)))
            {
                // (an error object with __tostring: its string, without a traceback (as Lua's standalone interpreter reports it))
                withTraceback = false;
                try
                {
                    message = tostring_text(aValue);
                }
                catch (...)
                {
                    message = "(error object is a " + type_name(aValue) + " value)";
                }
            }
            else
                message = "(error object is a " + type_name(aValue) + " value)";
            tUncaughtTraceback = withTraceback ? traceback(message, 1, true) : message;
        }
        throw lua_error{ aValue };
    }

    void request_exit(int aStatus)
    {
        auto& s = the_state();
        s.exitRequested = true;
        s.exitCode = aStatus;
        throw lua_error{ Nil };
    }

    std::string const& uncaught_traceback()
    {
        return tUncaughtTraceback;
    }

    protector::protector(value aHandler) : handler{ aHandler }
    {
        auto& s = stacks();
        extra = s.extra.size();
        arguments = s.arguments.size();
        tbc = s.tbc.size();
        keep.push_back(aHandler);
        s.protectors.push_back(this);
    }

    protector::~protector()
    {
        stacks().protectors.pop_back();
    }

    void protector::restore(value aError)
    {
        // (the thread's stacks as they were (the to-be-closed variables that are still open are closed, with the error))
        auto& s = stacks();
        if (s.extra.size() > extra)
            s.extra.resize(extra);
        if (s.arguments.size() > arguments)
            s.arguments.resize(arguments);
        close_tbc(tbc, aError, true);
    }

    std::size_t protection_depth()
    {
        return stacks().protectors.size();
    }

    // --- to-be-closed variables

    void close_tbc(std::size_t aDepth, value aError, bool aErrorClosing)
    {
        auto& s = stacks();
        auto error = aError;
        bool raised = false;
        value_list keep{ aError };
        while (stacks().tbc.size() > aDepth)
        {
            auto const v = stacks().tbc.back();
            stacks().tbc.pop_back();
            if (!truthy(v))
                continue;
            auto const handler = metamethod(v, NameClose);
            if (is_nil(handler))
                continue;
            try
            {
                value const arguments[] = { v, aErrorClosing ? error : Nil };
                value_list results;
                call(handler, arguments, results);
            }
            catch (lua_error const& e)
            {
                if (the_state().exitRequested)
                    throw;
                // (an error in a closing method replaces the error)
                error = e.v;
                keep.push_back(error);
                raised = true;
                aErrorClosing = true;
            }
        }
        (void)s;
        if (raised)
            throw lua_error{ error };
    }

    // --- coroutines

    namespace
    {
        void coroutine_run(thread_cell& co)
        {
            try
            {
                value_list arguments{ std::span<value const>{ co.transfer } };
                value_list results;
                call(co.function, arguments.span(), results);
                co.transfer.assign(results.begin(), results.end());
                co.threw = false;
            }
            catch (lua_error const& e)
            {
                co.transfer.assign(1u, e.v);
                co.threw = true;
            }
            catch (std::bad_alloc const&)
            {
                co.transfer.assign(1u, Nil);
                try
                {
                    co.transfer[0] = make_string("not enough memory");
                }
                catch (...)
                {
                }
                co.threw = true;
            }
            catch (...)
            {
                co.transfer.assign(1u, Nil);
                co.threw = true;
            }
        }

#if defined(_MSC_VER)
        __declspec(noinline)
#else
        [[gnu::noinline]]
#endif
        void coroutine_entry(void* aThread)
        {
            auto& co = *static_cast<thread_cell*>(aThread);
            char volatile base = 0;
            co.stackBase = const_cast<char const*>(&base);
            set_stack_base(co.stackBase);
            coroutine_run(co);
        }
    }

    thread_cell::~thread_cell()
    {
        if (coroutine != nullptr && sHost != nullptr)
            sHost->coroutine_delete(coroutine);
    }

    void thread_cell::trace(tracer& aTracer)
    {
        aTracer.mark(function);
        aTracer.mark(transfer);
        stacks.trace(aTracer);
        if (resumer != nullptr)
            aTracer.mark(resumer);
        if (suspended != nullptr)
            suspended_stack(aTracer, suspended, &link, coroutine);
    }

    value make_thread(value aFunction)
    {
        auto& t = the_heap().make<thread_cell>();
        t.function = aFunction;
        t.program = current_program();
        return TagOther | t.handle;
    }

    bool resume(thread_cell& co, std::span<value const> aArguments, value_list& aResults)
    {
        // (false: an error (its value is the result))
        auto& s = the_state();
        if (co.status == thread_cell::status_t::Dead)
        {
            aResults.push_back(make_string("cannot resume dead coroutine"));
            return false;
        }
        if (co.status != thread_cell::status_t::Suspended || &co == s.current)
        {
            aResults.push_back(make_string("cannot resume non-suspended coroutine"));
            return false;
        }
        auto const& h = *sHost;
        if (co.coroutine == nullptr && !co.started)
        {
            co.coroutine = h.coroutine_new(&coroutine_entry, &co);
            if (co.coroutine == nullptr)
            {
                aResults.push_back(make_string("cannot create a coroutine"));
                return false;
            }
        }
        co.started = true;
        co.transfer.assign(aArguments.begin(), aArguments.end());
        auto* const previous = s.current;
        auto* const previousStacks = s.stacks;
        if (previous != nullptr)
            previous->status = thread_cell::status_t::Normal;
        co.status = thread_cell::status_t::Running;
        co.resumer = previous;
        s.current = &co;
        s.stacks = &co.stacks;
        // (while the coroutine runs, the live part of this stack is a segment; the coroutine's segments follow its own (link))
        NEOS_LUA_SPILL_REGISTERS();
        stack_segment resumer{ stack_pointer(), stack_base(), stack_segments() };
        co.link.previous = &resumer;
        stack_segments() = co.suspended != nullptr ? co.suspended : &co.link;
        if (co.stackBase != nullptr)
            set_stack_base(co.stackBase);
        void* previousProgram = nullptr;
        if (co.program != nullptr && co.program != current_program())
        {
            previousProgram = current_program();
            h.thread_attach(co.program);
        }
        int const finished = h.coroutine_resume(co.coroutine);
        if (previousProgram != nullptr)
            h.thread_attach(previousProgram);
        set_stack_base(resumer.high);
        stack_segments() = resumer.previous;
        co.link.previous = nullptr;
        s.current = previous;
        s.stacks = previousStacks;
        co.resumer = nullptr;
        if (previous != nullptr)
            previous->status = thread_cell::status_t::Running;
        if (finished != 0)
        {
            co.status = thread_cell::status_t::Dead;
            h.coroutine_delete(co.coroutine);
            co.coroutine = nullptr;
            co.stacks = thread_stacks{};
        }
        else
            co.status = thread_cell::status_t::Suspended;
        if (s.exitRequested)
            throw lua_error{ Nil };
        aResults->insert(aResults->end(), co.transfer.begin(), co.transfer.end());
        co.transfer.clear();
        return !(finished != 0 && co.threw);
    }

    bool yieldable()
    {
        return the_state().current != nullptr;
    }

    std::size_t yield_values(std::span<value const> aValues, value_list& aResults)
    {
        auto* co = the_state().current;
        if (co == nullptr)
            runtime_error("attempt to yield from outside a coroutine");
        co->transfer.assign(aValues.begin(), aValues.end());
        // (while it is suspended, the live part of its stack is a segment, which its trace scans)
        NEOS_LUA_SPILL_REGISTERS();
        stack_segment here{ stack_pointer(), stack_base(), stack_segments() };
        co->suspended = &here;
        sHost->coroutine_suspend();
        co->suspended = nullptr;
        stack_segments() = here.previous;
        aResults->insert(aResults->end(), co->transfer.begin(), co->transfer.end());
        auto const count = co->transfer.size();
        co->transfer.clear();
        return count;
    }

    bool close_thread(thread_cell& co, value& aError)
    {
        // (a suspended or dead coroutine: its to-be-closed variables are closed and it is dead; false: an error (in aError))
        if (co.status == thread_cell::status_t::Dead)
        {
            if (co.threw && !co.transfer.empty())
            {
                aError = co.transfer[0];
                return false;
            }
            return true;
        }
        bool ok = true;
        auto* const previousStacks = the_state().stacks;
        the_state().stacks = &co.stacks;
        try
        {
            close_tbc(0u, Nil, false);
        }
        catch (lua_error const& e)
        {
            aError = e.v;
            ok = false;
        }
        the_state().stacks = previousStacks;
        co.status = thread_cell::status_t::Dead;
        if (co.coroutine != nullptr)
        {
            sHost->coroutine_delete(co.coroutine);
            co.coroutine = nullptr;
        }
        co.suspended = nullptr;
        co.stacks = thread_stacks{};
        return ok;
    }

    // --- load

    namespace
    {
        thread_local value tLoaded = Nil;
        root_tracer const sLoadRoots{ [](tracer& aTracer) { aTracer.mark(tLoaded); } };
    }

    void loaded_chunk(value aFunction)
    {
        tLoaded = aFunction;
    }

    value load_chunk(std::string_view aSource, std::string const& aChunkName, std::string const& aMode, value aEnvironment, std::string& aError)
    {
        if (!aSource.empty() && aSource[0] == '\x1B')
        {
            aError = "attempt to load a binary chunk (mode is '" + aMode + "')";
            if (aMode.find('b') != std::string::npos)
                aError = "binary chunks are not supported";
            return Nil;
        }
        if (aMode.find('t') == std::string::npos)
        {
            aError = "attempt to load a text chunk (mode is '" + aMode + "')";
            return Nil;
        }
        auto const& h = *sHost;
        if (h.version < 8u || h.load_n == nullptr)
        {
            aError = "load is not supported by the host";
            return Nil;
        }
        value_list keep{ aEnvironment };
        char const* error = nullptr;
        auto* const context = h.load_n("lua", aSource.data(), aSource.size(), aChunkName.c_str(), "load", &error);
        if (context == nullptr)
        {
            aError = compile_error_message(error != nullptr ? std::string{ error } : std::string{ "syntax error" }, aChunkName, aSource);
            return Nil;
        }
        tLoaded = Nil;
        neos_rt_value result{};
        auto const trap = h.run(context, NEOS_RT_VOID, &result);
        if (trap != nullptr)
        {
            aError = trap;
            return Nil;
        }
        auto const f = tLoaded;
        tLoaded = Nil;
        if (!is_function(f))
        {
            aError = "load: the chunk did not compile";
            return Nil;
        }
        if (!is_nil(aEnvironment))
            fun(f).environment = make_box(aEnvironment);
        return f;
    }

    std::string compile_error_message(std::string const& aError, std::string const& aChunkName, std::string_view aSource)
    {
        // (the compiler's report ("<source>(<line>,<column>...): <type>: <message>"): as Lua reports it ("<chunk>:<line>: <message>"))
        auto text = aError;
        while (!text.empty() && (text.back() == '\n' || text.back() == '\r'))
            text.pop_back();
        // (the report's line that has the position: the last line that has "): ")
        std::string reportLine;
        std::size_t start = 0u;
        while (start <= text.size())
        {
            auto end = text.find('\n', start);
            if (end == std::string::npos)
                end = text.size();
            auto const line = text.substr(start, end - start);
            if (line.find("): ") != std::string::npos)
                reportLine = line;
            start = end + 1u;
        }
        if (reportLine.empty())
            reportLine = text.substr(0u, text.find('\n'));
        auto const close = reportLine.find("): ");
        auto const open = close == std::string::npos ? std::string::npos : reportLine.rfind('(', close);
        if (open == std::string::npos)
            return aChunkName + ": " + reportLine;
        auto const position = reportLine.substr(open + 1u, close - open - 1u);
        auto const comma = position.find(',');
        auto const line = position.substr(0u, comma);
        std::size_t column = 0u;
        if (comma != std::string::npos)
            column = static_cast<std::size_t>(std::strtoul(position.c_str() + comma + 1u, nullptr, 10));
        auto message = reportLine.substr(close + 3u);
        if (message.starts_with("syntax error: "))
            message = message.substr(14u);
        else if (message.starts_with("error: "))
            message = message.substr(7u);
        if (message.ends_with(" was unexpected here."))
        {
            // (the parser's: the token where it failed)
            std::size_t offset = 0u;
            auto lineNumber = static_cast<std::size_t>(std::strtoul(line.c_str(), nullptr, 10));
            for (std::size_t l = 1u; l < lineNumber && offset < aSource.size(); ++offset)
                if (aSource[offset] == '\n')
                    ++l;
            offset += column > 0u ? column - 1u : 0u;
            while (offset < aSource.size() && std::isspace(static_cast<unsigned char>(aSource[offset])))
                ++offset;
            if (offset >= aSource.size())
                message = "unexpected symbol near <eof>";
            else
            {
                auto last = offset;
                if (std::isalnum(static_cast<unsigned char>(aSource[last])) || aSource[last] == '_')
                    while (last < aSource.size() && (std::isalnum(static_cast<unsigned char>(aSource[last])) || aSource[last] == '_'))
                        ++last;
                else
                    ++last;
                message = "unexpected symbol near '" + std::string{ aSource.substr(offset, last - offset) } + "'";
            }
        }
        return aChunkName + ":" + line + ": " + message;
    }

    // --- tracebacks

    std::string traceback(std::string const& aMessage, int aLevel, bool aNative)
    {
        std::string result = aMessage;
        if (!result.empty())
            result += "\n";
        result += "stack traceback:";
        auto const* h = sHost;
        if (h == nullptr || h->frames == nullptr)
            return result;
        neos_rt_frame frames[64];
        auto const count = h->frames(frames, 64u);
        if (aNative && tNative != nullptr && aLevel <= 1)
            result += std::string{ "\n\t[C]: in function '" } + tNative->qualified + "'";
        for (unsigned i = static_cast<unsigned>(std::max(aLevel, 1)) - 1u; i < count; ++i)
        {
            auto const& f = frames[i];
            // (the program's entry point is not a Lua function)
            if (f.function == nullptr || *f.function == '\0' || std::string_view{ f.function }.starts_with("__neos"))
                continue;
            std::string source = f.source != nullptr ? f.source : "?";
            auto const slash = source.find_last_of("/\\");
            if (slash != std::string::npos && source.find("[string") != 0u)
                source = source.substr(slash + 1u);
            std::string name = f.function != nullptr ? f.function : "?";
            result += "\n\t" + source + ":" + std::to_string(f.line) + ": in " + (name.empty() ? std::string{ "?" } : name);
        }
        result += "\n\t[C]: in ?";
        return result;
    }
}
