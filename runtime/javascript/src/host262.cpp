/*
  host262.cpp

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

// The test262 host: print and $262 (defined when the environment variable NEOS_TEST262 is set: for running ECMAScript's
// conformance tests)

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <memory>
#include <thread>
#include "js.hpp"
#include "gc.hpp"
#include "api.hpp"

namespace neos::javascript_runtime
{
    namespace
    {
        value print(value, arguments aArguments, value, function_object&)
        {
            std::string line;
            for (std::size_t i = 0u; i < aArguments.size(); ++i)
            {
                if (i != 0u)
                    line += " ";
                line += is_string(aArguments[i]) ? utf8(aArguments[i]) : inspect(aArguments[i], false);
            }
            line += "\n";
            write_standard(1, line);
            flush_standard(1);
            return Undefined;
        }

        value gc(value, arguments, value, function_object&)
        {
            collect_garbage();
            return Undefined;
        }

        value host_detach_array_buffer(value, arguments aArguments, value, function_object&)
        {
            detach_array_buffer(argument(aArguments, 0u), argument(aArguments, 1u));
            return Null;
        }

        value eval_script(value, arguments aArguments, value, function_object&)
        {
            return evaluate_script(to_string(argument(aArguments, 0u)));
        }

        value is_htmldda(value, arguments aArguments, value, function_object&)
        {
            // ([[Call]]: null if called with no arguments or the empty string)
            if (aArguments.empty() || (is_string(aArguments[0]) && string_length(aArguments[0]) == 0u))
                return Null;
            return Undefined;
        }

        value create_realm(value, arguments, value, function_object&)
        {
            auto* r = javascript_runtime::create_realm();
            return get(r->global, ascii("$262"));
        }

        // --- agents ($262.agent): each runs (in a realm of its own) on a coroutine; an agent runs until it blocks (Atomics.wait,
        // $262.agent.sleep) or has nothing to do (no broadcast to receive); the main program runs them when it broadcasts, blocks or
        // asks for a report

        struct agent
        {
            realm* agentRealm = nullptr;    ///< (its realm, while it is suspended)
            realm* agentCallerRealm = nullptr;  ///< (likewise its caller realm)
            neos_rt_coroutine* coroutine = nullptr;
            value source = Undefined;
            value callback = Undefined;     ///< (receiveBroadcast's)
            std::optional<std::pair<value, value>> message;
            bool started = false;
            bool finished = false;
            bool waiting = false;
            std::function<bool()> done;
            std::optional<double> deadline;
            std::vector<value> savedArguments;
            void const* generator = nullptr;    ///< (the generator running when it was resumed: a different one runs within it)
            std::deque<std::function<void()>> jobs;     ///< (its job queue)
        };

        std::vector<std::unique_ptr<agent>>& agents()
        {
            static std::vector<std::unique_ptr<agent>> sAgents;
            return sAgents;
        }

        std::deque<value>& reports()
        {
            static std::deque<value> sReports;
            return sReports;
        }

        agent* tAgent = nullptr;

        void agent_entry(void* aData)
        {
            auto& a = *static_cast<agent*>(aData);
            try
            {
                evaluate_script(a.source);
            }
            catch (js_exception const& e)
            {
                std::string const line = "Uncaught (agent) " + inspect(e.thrown, false) + "\n";
                write_standard(1, line);
            }
            for (;;)
            {
                if (!a.jobs.empty())
                {
                    auto job = std::move(a.jobs.front());
                    a.jobs.pop_front();
                    try
                    {
                        job();
                    }
                    catch (js_exception const& e)
                    {
                        std::string const line = "Uncaught (agent) " + inspect(e.thrown, false) + "\n";
                        write_standard(1, line);
                    }
                }
                else if (a.message && a.callback != Undefined)
                {
                    auto const m = *a.message;
                    a.message.reset();
                    try
                    {
                        value const args[] = { m.first, m.second };
                        call(a.callback, Undefined, args);
                    }
                    catch (js_exception const& e)
                    {
                        std::string const line = "Uncaught (agent) " + inspect(e.thrown, false) + "\n";
                        write_standard(1, line);
                    }
                }
                else
                    host()->coroutine_suspend();
            }
        }

        bool runnable(agent const& a)
        {
            if (a.finished)
                return false;
            if (!a.started)
                return true;
            if (a.waiting)
                return a.done() || (a.deadline && monotonic_ms() >= *a.deadline);
            return !a.jobs.empty() || (a.message && a.callback != Undefined);
        }

        void resume(agent& a)
        {
            a.started = true;
            auto& extra = extra_arguments();
            auto const base = extra.size();
            extra.insert(extra.end(), a.savedArguments.begin(), a.savedArguments.end());
            a.savedArguments.clear();
            auto* const previousRealm = exchange_realm(a.agentRealm);
            auto* const previousCallerRealm = exchange_caller_realm(a.agentCallerRealm);
            auto* const previousAgent = std::exchange(tAgent, &a);
            a.generator = running_generator();
            int const finished = host()->coroutine_resume(a.coroutine);
            tAgent = previousAgent;
            a.agentRealm = exchange_realm(previousRealm);
            a.agentCallerRealm = exchange_caller_realm(previousCallerRealm);
            if (finished != 0)
                a.finished = true;
            else
                a.savedArguments.assign(extra.begin() + static_cast<std::ptrdiff_t>(base), extra.end());
            extra.resize(base);
        }

        bool run_agents()
        {
            // (each agent that can run: true if one did)
            bool progress = false;
            auto& list = agents();
            for (std::size_t i = 0u; i < list.size(); ++i)
                if (list[i].get() != tAgent && runnable(*list[i]))
                {
                    resume(*list[i]);
                    progress = true;
                }
            return progress;
        }

        value agent_start(value, arguments aArguments, value, function_object&)
        {
            // (agents run on coroutines whose stacks the collector does not scan: while there are agents, no garbage is collected)
            static std::optional<gc_pause> sNoCollection;
            if (!sNoCollection)
                sNoCollection.emplace();
            auto a = std::make_unique<agent>();
            a->source = to_string(argument(aArguments, 0u));
            {
                auto* r = javascript_runtime::create_realm();
                a->agentRealm = r;
            }
            a->coroutine = host()->coroutine_new(&agent_entry, a.get());
            if (a->coroutine == nullptr)
                throw_range_error("cannot create a coroutine for an agent");
            auto& added = *a;
            agents().push_back(std::move(a));
            resume(added);
            return Undefined;
        }

        value agent_broadcast(value, arguments aArguments, value, function_object&)
        {
            // (each agent receives the buffer (the same object: the agents share the heap) and the number)
            for (auto& a : agents())
                if (!a->finished)
                    a->message = std::make_pair(argument(aArguments, 0u), argument(aArguments, 1u));
            run_agents();
            return Undefined;
        }

        value agent_receive_broadcast(value, arguments aArguments, value, function_object&)
        {
            if (tAgent != nullptr)
                tAgent->callback = argument(aArguments, 0u);
            return Undefined;
        }

        value agent_report(value, arguments aArguments, value, function_object&)
        {
            reports().push_back(to_string(argument(aArguments, 0u)));
            return Undefined;
        }

        value agent_get_report(value, arguments, value, function_object&)
        {
            if (reports().empty() && tAgent == nullptr)
                run_agents();
            if (reports().empty())
                return Null;
            auto const r = reports().front();
            reports().pop_front();
            return r;
        }

        value agent_sleep(value, arguments aArguments, value, function_object&)
        {
            auto const ms = to_number(argument(aArguments, 0u));
            agent_block([]() { return false; }, monotonic_ms() + (std::isnan(ms) ? 0.0 : std::max(ms, 0.0)));
            return Undefined;
        }

        value agent_monotonic_now(value, arguments, value, function_object&)
        {
            return number(std::floor(monotonic_ms()));
        }

        value agent_leaving(value, arguments, value, function_object&)
        {
            return Undefined;
        }
    }

    double monotonic_ms()
    {
        static auto const sStart = std::chrono::steady_clock::now();
        return std::chrono::duration<double, std::milli>{ std::chrono::steady_clock::now() - sStart }.count();
    }

    bool in_agent()
    {
        return tAgent != nullptr;
    }

    bool agents_running()
    {
        for (auto const& a : agents())
            if (!a->finished && a.get() != tAgent)
                return true;
        return tAgent != nullptr;
    }

    bool agent_enqueue_job(std::function<void()>& aJob)
    {
        if (tAgent == nullptr)
            return false;
        tAgent->jobs.push_back(std::move(aJob));
        return true;
    }

    void agent_yield()
    {
        // (an agent (or the main program) that may be spinning on a shared location: the other agents run)
        if (tAgent != nullptr)
            agent_block([]() { return true; }, {});
        else if (!agents().empty())
            run_agents();
    }

    void agent_block(std::function<bool()> const& aDone, std::optional<double> aDeadline)
    {
        if (tAgent != nullptr && running_generator() != tAgent->generator)
        {
            // (an agent within a generator or async function (whose coroutine is not the agent's): it cannot be suspended; it waits)
            while (!aDone() && aDeadline && monotonic_ms() < *aDeadline)
                std::this_thread::sleep_for(std::chrono::milliseconds{ 1 });
            return;
        }
        if (tAgent != nullptr)
        {
            // (an agent: it is suspended until it is done or its deadline passes)
            auto& a = *tAgent;
            a.waiting = true;
            a.done = aDone;
            a.deadline = aDeadline;
            host()->coroutine_suspend();
            a.waiting = false;
            a.done = {};
            a.deadline.reset();
            return;
        }
        // (the main program: the agents run meanwhile)
        for (;;)
        {
            if (aDone())
                return;
            auto const now = monotonic_ms();
            if (aDeadline && now >= *aDeadline)
                return;
            if (!run_agents())
            {
                double wait = 1.0;
                if (aDeadline)
                    wait = std::min(wait, *aDeadline - now);
                std::this_thread::sleep_for(std::chrono::duration<double, std::milli>{ std::max(wait, 0.0) });
            }
        }
    }

    void init_host262(realm& aRealm)
    {
        if (std::getenv("NEOS_TEST262") == nullptr)
            return;
        auto const g = aRealm.global;
        method(g, "print", 1u, print);
        auto const host = make_object();
        obj(host).put(ascii("global"), g);
        method(host, "gc", 0u, gc);
        method(host, "detachArrayBuffer", 1u, host_detach_array_buffer);
        method(host, "evalScript", 1u, eval_script);
        method(host, "createRealm", 0u, create_realm);
        {
            auto const agent = make_object();
            method(agent, "start", 1u, agent_start);
            method(agent, "broadcast", 2u, agent_broadcast);
            method(agent, "receiveBroadcast", 1u, agent_receive_broadcast);
            method(agent, "report", 1u, agent_report);
            method(agent, "getReport", 0u, agent_get_report);
            method(agent, "sleep", 1u, agent_sleep);
            method(agent, "monotonicNow", 0u, agent_monotonic_now);
            method(agent, "leaving", 0u, agent_leaving);
            obj(host).put(ascii("agent"), agent);
        }
        auto const htmldda = make_native("IsHTMLDDA", 0u, is_htmldda);
        obj(htmldda).cls = object_class::HTMLDDA;
        obj(host).put(ascii("IsHTMLDDA"), htmldda);
        obj(g).put(ascii("$262"), host);
    }
}
