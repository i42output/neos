/*
  debug.cpp

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

#include <neos/neos.hpp>
#include <algorithm>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <chrono>
#include <cstdio>
#include <thread>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <csignal>
#include <pthread.h>
#endif
#include <regex>
#include <neos/debug.hpp>

namespace neos
{
    namespace debug
    {
        // --- blocking reads (see blocking_read): a slot each, lock free (interrupt_reads is called from a signal handler)

        namespace
        {
            // (a slot's wake: Open (reading, not interrupted), Sent (being interrupted), Handled (interrupted), Closed (not reading))
            enum : int { WakeOpen, WakeSent, WakeHandled, WakeClosed };
            struct read_slot
            {
                std::atomic<bool> used = false;
#if defined(_WIN32)
                std::atomic<HANDLE> thread = nullptr;
#else
                std::atomic<pthread_t> thread{};
#endif
                std::atomic<int> wake = WakeClosed;
            };
            constexpr std::size_t MaxReads = 64u;
            read_slot sReads[MaxReads];

#if defined(_WIN32)
            // (a read is interrupted by cancelling its I/O (CancelSynchronousIo), which needs a real handle of its thread)
            HANDLE current_thread()
            {
                struct handle
                {
                    HANDLE h = nullptr;
                    ~handle() { if (h != nullptr) ::CloseHandle(h); }
                };
                thread_local handle tHandle;
                if (tHandle.h == nullptr)
                    ::DuplicateHandle(::GetCurrentProcess(), ::GetCurrentThread(), ::GetCurrentProcess(), &tHandle.h, 0, FALSE, DUPLICATE_SAME_ACCESS);
                return tHandle.h;
            }
#else
            // (a read is interrupted by a signal whose handler does nothing but note it: installed without SA_RESTART, so the
            // read fails with EINTR; SIGURG, whose default action is to ignore it)
            extern "C" void read_interrupted(int)
            {
                auto const self = ::pthread_self();
                for (auto& slot : sReads)
                    if (slot.used.load(std::memory_order_acquire) && ::pthread_equal(slot.thread.load(std::memory_order_acquire), self))
                    {
                        int expected = WakeSent;
                        slot.wake.compare_exchange_strong(expected, WakeHandled, std::memory_order_acq_rel);
                    }
            }

            void install_read_interrupt()
            {
                static std::once_flag sOnce;
                std::call_once(sOnce, []()
                {
                    struct sigaction action = {};
                    action.sa_handler = read_interrupted;
                    sigemptyset(&action.sa_mask);
                    action.sa_flags = 0;
                    ::sigaction(SIGURG, &action, nullptr);
                });
            }
#endif
        }

        blocking_read::blocking_read(bool aActive) :
            iSlot{ MaxReads }
        {
            if (!aActive)
                return;
#if !defined(_WIN32)
            install_read_interrupt();
            auto const self = ::pthread_self();
#else
            auto const self = current_thread();
#endif
            for (std::size_t i = 0u; i < MaxReads; ++i)
            {
                bool expected = false;
                if (sReads[i].used.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
                {
                    sReads[i].thread.store(self, std::memory_order_release);
                    sReads[i].wake.store(WakeOpen, std::memory_order_release);
                    iSlot = i;
                    break;
                }
            }
        }

        blocking_read::~blocking_read()
        {
            finish();
        }

        bool blocking_read::finish()
        {
            if (iSlot == MaxReads)
                return false;
            auto& slot = sReads[iSlot];
            iSlot = MaxReads;
            // (an interruption being sent is waited for, so that it interrupts the read, not a later system call of this thread)
            for (int spin = 0; slot.wake.load(std::memory_order_acquire) == WakeSent && spin < 1000000; ++spin)
                std::this_thread::yield();
            auto const wake = slot.wake.exchange(WakeClosed, std::memory_order_acq_rel);
            slot.used.store(false, std::memory_order_release);
            return wake == WakeSent || wake == WakeHandled;
        }

        std::size_t interrupt_reads()
        {
            std::size_t blocked = 0u;
            for (auto& slot : sReads)
            {
                if (!slot.used.load(std::memory_order_acquire))
                    continue;
                int expected = WakeOpen;
#if !defined(_WIN32)
                // (handled, yet still reading: the signal came before the thread's read began; again)
                if (!slot.wake.compare_exchange_strong(expected, WakeSent, std::memory_order_acq_rel))
                {
                    if (expected != WakeHandled || !slot.wake.compare_exchange_strong(expected, WakeSent, std::memory_order_acq_rel))
                    {
                        if (expected == WakeSent)
                            ++blocked;
                        continue;
                    }
                }
                ::pthread_kill(slot.thread.load(std::memory_order_acquire), SIGURG);
                ++blocked;
#else
                if (!slot.wake.compare_exchange_strong(expected, WakeSent, std::memory_order_acq_rel))
                    continue;
                // (no I/O to cancel: the read has not begun; open again, to be interrupted when it has)
                bool const cancelled = ::CancelSynchronousIo(slot.thread.load(std::memory_order_acquire)) != FALSE;
                slot.wake.store(cancelled ? WakeHandled : WakeOpen, std::memory_order_release);
                if (!cancelled)
                    ++blocked;
#endif
            }
            return blocked;
        }

        // --- session

        session::session(i_frontend& aFrontend) :
            iFrontend{ aFrontend }
        {
        }

        session::~session()
        {
        }

        i_frontend& session::frontend() const
        {
            return iFrontend;
        }

        void session::changed()
        {
            iVersion.fetch_add(1u, std::memory_order_acq_rel);
            iCatching.store(std::any_of(iBreakpoints.begin(), iBreakpoints.end(), [](breakpoint const& b) { return b.enabled && b.exception; }), 
                std::memory_order_release);
        }

        std::uint32_t session::add_breakpoint(location const& aLocation, std::string const& aCondition, bool aTemporary)
        {
            std::lock_guard lock{ iMutex };
            breakpoint b;
            b.id = iNextId++;
            b.requested = aLocation;
            b.condition = aCondition;
            b.temporary = aTemporary;
            iBreakpoints.push_back(b);
            changed();
            return b.id;
        }

        std::uint32_t session::add_catchpoint(std::string const& aException, std::string const& aCondition, bool aTemporary)
        {
            std::lock_guard lock{ iMutex };
            breakpoint b;
            b.id = iNextId++;
            b.exception = aException;
            b.condition = aCondition;
            b.temporary = aTemporary;
            iBreakpoints.push_back(b);
            changed();
            return b.id;
        }

        std::vector<std::uint32_t> session::catchpoints_for(std::string const& aType, bool aControl) const
        {
            std::vector<std::uint32_t> result;
            std::lock_guard lock{ iMutex };
            for (auto const& b : iBreakpoints)
            {
                if (!b.enabled || !b.exception)
                    continue;
                if (b.exception->empty())
                {
                    if (!aControl)
                        result.push_back(b.id);
                    continue;
                }
                try
                {
                    if (std::regex_search(aType, std::regex{ *b.exception, std::regex::ECMAScript | std::regex::icase }))
                        result.push_back(b.id);
                }
                catch (std::regex_error const&)
                {
                }
            }
            return result;
        }

        bool session::remove_breakpoint(std::uint32_t aId)
        {
            std::lock_guard lock{ iMutex };
            auto const b = std::find_if(iBreakpoints.begin(), iBreakpoints.end(), [&](breakpoint const& e) { return e.id == aId; });
            if (b == iBreakpoints.end())
                return false;
            iBreakpoints.erase(b);
            changed();
            return true;
        }

        void session::clear_breakpoints()
        {
            std::lock_guard lock{ iMutex };
            iBreakpoints.clear();
            changed();
        }

        bool session::enable_breakpoint(std::uint32_t aId, bool aEnable)
        {
            std::lock_guard lock{ iMutex };
            for (auto& b : iBreakpoints)
                if (b.id == aId)
                {
                    b.enabled = aEnable;
                    changed();
                    return true;
                }
            return false;
        }

        bool session::set_condition(std::uint32_t aId, std::string const& aCondition)
        {
            std::lock_guard lock{ iMutex };
            for (auto& b : iBreakpoints)
                if (b.id == aId)
                {
                    b.condition = aCondition;
                    return true;
                }
            return false;
        }

        bool session::set_ignore_count(std::uint32_t aId, std::uint32_t aCount)
        {
            std::lock_guard lock{ iMutex };
            for (auto& b : iBreakpoints)
                if (b.id == aId)
                {
                    b.ignoreCount = aCount;
                    return true;
                }
            return false;
        }

        std::vector<breakpoint> session::breakpoints() const
        {
            std::lock_guard lock{ iMutex };
            return iBreakpoints;
        }

        std::optional<breakpoint> session::find_breakpoint(std::uint32_t aId) const
        {
            std::lock_guard lock{ iMutex };
            for (auto const& b : iBreakpoints)
                if (b.id == aId)
                    return b;
            return {};
        }

        bool session::stop_on_entry() const
        {
            std::lock_guard lock{ iMutex };
            return iStopOnEntry;
        }

        void session::set_stop_on_entry(bool aStop)
        {
            std::lock_guard lock{ iMutex };
            iStopOnEntry = aStop;
        }

        bool session::break_on_error() const
        {
            std::lock_guard lock{ iMutex };
            return iBreakOnError;
        }

        void session::set_break_on_error(bool aBreak)
        {
            std::lock_guard lock{ iMutex };
            iBreakOnError = aBreak;
        }

        void session::pause()
        {
            iPause.store(true, std::memory_order_relaxed);
        }

        void session::interrupt()
        {
            iInterrupts.fetch_add(1u, std::memory_order_acq_rel);
            if (!any_stopped())
                pause();
            interrupt_reads();
        }

        void session::terminate()
        {
            iTerminate.store(true, std::memory_order_relaxed);
        }

        void session::begin_run()
        {
            std::lock_guard lock{ iMutex };
            iTerminate.store(false, std::memory_order_relaxed);
            iPause.store(false, std::memory_order_relaxed);
            iStopped.store(false, std::memory_order_release);
            iThreadIds.clear();
            for (auto& b : iBreakpoints)
                b.hits = 0u;
            changed();
        }

        std::uint32_t session::thread_id()
        {
            std::lock_guard lock{ iMutex };
            auto const id = std::this_thread::get_id();
            auto existing = iThreadIds.find(id);
            if (existing == iThreadIds.end())
                existing = iThreadIds.emplace(id, static_cast<std::uint32_t>(iThreadIds.size() + 1u)).first;
            return existing->second;
        }

        bool session::hit(std::uint32_t aId, std::function<bool(std::string const& aCondition)> const& aCondition)
        {
            std::string condition;
            {
                std::lock_guard lock{ iMutex };
                auto b = std::find_if(iBreakpoints.begin(), iBreakpoints.end(), [&](breakpoint const& e) { return e.id == aId; });
                if (b == iBreakpoints.end() || !b->enabled)
                    return false;
                condition = b->condition;
            }
            // (the condition is evaluated without the lock: evaluating it reads the program's values, through its runtime plugin)
            if (!condition.empty() && !aCondition(condition))
                return false;
            std::lock_guard lock{ iMutex };
            auto b = std::find_if(iBreakpoints.begin(), iBreakpoints.end(), [&](breakpoint const& e) { return e.id == aId; });
            if (b == iBreakpoints.end())
                return false;
            ++b->hits;
            if (b->ignoreCount > 0u)
            {
                --b->ignoreCount;
                return false;
            }
            if (b->temporary)
            {
                iBreakpoints.erase(b);
                changed();
            }
            return true;
        }

        void session::bound(std::uint32_t aId, std::uint32_t aLine)
        {
            std::optional<breakpoint> changedBreakpoint;
            {
                std::lock_guard lock{ iMutex };
                for (auto& b : iBreakpoints)
                    if (b.id == aId && b.line != aLine)
                    {
                        b.line = aLine;
                        changedBreakpoint = b;
                    }
            }
            if (changedBreakpoint)
                iFrontend.breakpoint_changed(*changedBreakpoint);
        }

        action session::stop(i_stop& aStop)
        {
            {
                std::unique_lock lock{ iMutex };
                if (iStopped.load(std::memory_order_acquire))
                {
                    lock.unlock();
                    park();
                    return action::Continue;
                }
                iStopped.store(true, std::memory_order_release);
                iPause.store(false, std::memory_order_relaxed);
                ++iStopSerial;
            }
            // (all of the program stops: a thread blocked reading its input parks (see blocking_read), so that it does not read 
            // the debugger's commands; a read that began just as it was interrupted is interrupted again)
            for (int attempt = 0; attempt < 20 && interrupt_reads() != 0u; ++attempt)
                std::this_thread::sleep_for(std::chrono::milliseconds{ 5 });
            action result = action::Continue;
            try
            {
                result = iFrontend.stopped(aStop);
            }
            catch (...)
            {
                result = action::Terminate;
            }
            {
                std::lock_guard lock{ iMutex };
                iStopped.store(false, std::memory_order_release);
                if (result == action::Terminate)
                    iTerminate.store(true, std::memory_order_relaxed);
            }
            iResumed.notify_all();
            return result;
        }

        void session::park()
        {
            auto const id = thread_id();
            std::unique_lock lock{ iMutex };
            auto& self = iParked[id];
            while (iStopped.load(std::memory_order_acquire))
            {
                if (!self.requests.empty())
                {
                    auto request = std::move(self.requests.front());
                    self.requests.pop_front();
                    lock.unlock();
                    try
                    {
                        request();
                    }
                    catch (...)
                    {
                    }
                    lock.lock();
                    ++self.done;
                    iResumed.notify_all();
                    continue;
                }
                iResumed.wait(lock);
            }
            iParked.erase(id);
        }

        bool session::run_on(std::uint32_t aThread, std::function<void()> const& aRequest)
        {
            std::unique_lock lock{ iMutex };
            auto p = iParked.find(aThread);
            if (p == iParked.end())
                return false;
            auto const ticket = ++p->second.posted;
            p->second.requests.push_back(aRequest);
            iResumed.notify_all();
            iResumed.wait(lock, [&]() { auto q = iParked.find(aThread); return q == iParked.end() || q->second.done >= ticket; });
            return true;
        }

        std::vector<std::uint32_t> session::parked_threads() const
        {
            std::lock_guard lock{ iMutex };
            std::vector<std::uint32_t> result;
            for (auto const& p : iParked)
                result.push_back(p.first);
            return result;
        }

        namespace
        {
            thread_local std::shared_ptr<session> tSession;
        }

        void set_session(std::shared_ptr<session> aSession)
        {
            tSession = std::move(aSession);
        }

        std::shared_ptr<session> current_session()
        {
            return tSession;
        }

        bool same_source(std::string_view aRequested, std::string_view aCompiled)
        {
            if (aRequested.empty() || aCompiled.empty())
                return false;
            if (aRequested == aCompiled)
                return true;
            std::filesystem::path const requested{ std::string{ aRequested } };
            std::filesystem::path const compiled{ std::string{ aCompiled } };
            std::error_code ec1, ec2;
            auto const r = std::filesystem::weakly_canonical(std::filesystem::absolute(requested, ec1), ec1);
            auto const c = std::filesystem::weakly_canonical(std::filesystem::absolute(compiled, ec2), ec2);
            if (!ec1 && !ec2 && r == c)
                return true;
            // (a file that exists (e.g. relative to the current folder): that file only)
            if (std::error_code ec; !ec1 && std::filesystem::is_regular_file(r, ec))
                return false;
            // (a name without a folder: a source of that name, wherever it is)
            if (!requested.has_parent_path() || requested.parent_path().empty())
                return requested.filename() == compiled.filename();
            // (a relative path: a source whose path ends with it)
            if (requested.is_relative())
            {
                auto const rs = requested.lexically_normal().generic_string();
                auto const cs = compiled.lexically_normal().generic_string();
                return cs.size() > rs.size() && cs.ends_with(rs) && cs[cs.size() - rs.size() - 1u] == '/';
            }
            return false;
        }

        // --- remote_frontend

        remote_frontend::remote_frontend(stopped_callback aStopped, breakpoint_callback aBreakpointChanged) :
            iStopped{ std::move(aStopped) }, iBreakpointChanged{ std::move(aBreakpointChanged) }
        {
        }

        action remote_frontend::stopped(i_stop& aStop)
        {
            {
                std::lock_guard lock{ iMutex };
                iIsStopped = true;
                iResume.reset();
                iRequests.clear();
            }
            if (iStopped)
                iStopped(aStop.reason(), aStop.thread(), aStop.breakpoint(), aStop.message());
            std::unique_lock lock{ iMutex };
            for (;;)
            {
                iChanged.wait(lock, [&]() { return !iRequests.empty() || iResume.has_value(); });
                if (!iRequests.empty())
                {
                    auto request = std::move(iRequests.front());
                    iRequests.pop_front();
                    lock.unlock();
                    try
                    {
                        request(aStop);
                    }
                    catch (...)
                    {
                    }
                    lock.lock();
                    iChanged.notify_all();
                    continue;
                }
                auto const result = *iResume;
                iIsStopped = false;
                iResume.reset();
                iChanged.notify_all();
                return result;
            }
        }

        void remote_frontend::breakpoint_changed(breakpoint const& aBreakpoint)
        {
            if (iBreakpointChanged)
                iBreakpointChanged(aBreakpoint);
        }

        bool remote_frontend::is_stopped() const
        {
            std::lock_guard lock{ iMutex };
            return iIsStopped;
        }

        bool remote_frontend::post(std::function<void(i_stop& aStop)> aRequest, bool aWait)
        {
            std::unique_lock lock{ iMutex };
            if (!iIsStopped || iResume.has_value())
                return false;
            if (!aWait)
            {
                iRequests.push_back(std::move(aRequest));
                iChanged.notify_all();
                return true;
            }
            bool done = false;
            iRequests.push_back([&, request = std::move(aRequest)](i_stop& aStop)
            {
                request(aStop);
                std::lock_guard doneLock{ iMutex };
                done = true;
            });
            iChanged.notify_all();
            iChanged.wait(lock, [&]() { return done || !iIsStopped; });
            return done;
        }

        void remote_frontend::resume(action aAction)
        {
            std::lock_guard lock{ iMutex };
            if (!iIsStopped)
                return;
            iResume = aAction;
            iChanged.notify_all();
        }

        // --- console_frontend

        namespace
        {
            std::string trim(std::string_view aText)
            {
                auto const first = aText.find_first_not_of(" \t\r\n");
                if (first == std::string_view::npos)
                    return {};
                auto const last = aText.find_last_not_of(" \t\r\n");
                return std::string{ aText.substr(first, last - first + 1u) };
            }

            std::pair<std::string, std::string> split_verb(std::string const& aCommand)
            {
                auto const command = trim(aCommand);
                auto const space = command.find_first_of(" \t");
                if (space == std::string::npos)
                    return { command, {} };
                return { command.substr(0u, space), trim(command.substr(space)) };
            }

            std::optional<std::uint32_t> to_number(std::string_view aText)
            {
                std::uint32_t result = 0u;
                auto const text = trim(aText);
                if (text.empty())
                    return {};
                auto const [p, ec] = std::from_chars(text.data(), text.data() + text.size(), result);
                if (ec != std::errc{} || p != text.data() + text.size())
                    return {};
                return result;
            }

            char const* reason_text(stop_reason aReason)
            {
                switch (aReason)
                {
                case stop_reason::Entry: return "Program stopped at entry";
                case stop_reason::Breakpoint: return "Breakpoint";
                case stop_reason::Step: return "Step";
                case stop_reason::Pause: return "Program paused";
                case stop_reason::Error: return "Program stopped by an error";
                default: return "Stopped";
                }
            }

            struct match_command
            {
                std::string const& verb;
                bool operator()(std::initializer_list<char const*> aNames) const
                {
                    for (auto const* n : aNames)
                        if (verb == n)
                            return true;
                    return false;
                }
            };

            // (a command's help page: "help <command>"; a command's other names give its page too)
            std::optional<std::string> help_page(std::string const& aTopic)
            {
                struct page
                {
                    std::initializer_list<char const*> names;
                    char const* text;
                };
                static page const sPages[] =
                {
                    { { "run", "r" },
                        "run, r\n"
                        "Run the program from its start. It runs until it comes to a breakpoint, a runtime error stops it (see\n"
                        "\"help set\"), Ctrl+C pauses it, or it ends. When it has ended, \"run\" runs it again: breakpoints and\n"
                        "displays are kept. The program's arguments and input are those given to neos.\n" },
                    { { "start" },
                        "start\n"
                        "Run the program from its start (as \"run\"), stopping at its first statement.\n" },
                    { { "break", "b", "br" },
                        "break, b [<source>:]<line> [if <condition>]\n"
                        "break, b\n"
                        "Set a breakpoint at a line. With no <source>: the source last shown (by a stop, \"frame\" or \"list\"),\n"
                        "else the program's first source file. With no argument: the current line of the selected frame.\n"
                        "<source> is a path or a file name: a path that names a file is that file, a file name is any source\n"
                        "with that name, another relative path any source whose path ends with it.\n"
                        "If <line> has no code the breakpoint is at the next line that has. Until the program's code for the\n"
                        "source is loaded the breakpoint is pending (see \"info breakpoints\").\n"
                        "The program stops at a breakpoint when it comes to the line from another line, or in another call\n"
                        "(e.g. a recursive one), not again at each statement of the same line.\n"
                        "if <condition>: stop only when <condition> is true (an expression: see \"help print\"); a condition that\n"
                        "cannot be evaluated stops. A breakpoint's number is used by delete, disable, enable, condition and ignore.\n"
                        "Examples:\n"
                        "  break 12\n"
                        "  break lib/util.lua:40\n"
                        "  break 30 if n > 100 and name == \"x\"\n" },
                    { { "tbreak" },
                        "tbreak [<source>:]<line> [if <condition>]\n"
                        "Set a temporary breakpoint: as \"break\", but it is deleted when the program first stops at it.\n" },
                    { { "catch" },
                        "catch throw [<exception>] [if <condition>]\n"
                        "Set a catchpoint: the program stops where its code throws (raises) an exception, whether or not it is caught, with its\n"
                        "frames as they are there (so the variables of the code that threw it can be seen). <exception>: a regular expression\n"
                        "that the exception's type's name must match, ignoring case (as its language names it: e.g. ValueError, TypeError,\n"
                        "Constraint_Error; a Lua error's is its value's type (or its metatable's __name), e.g. string or table); with none, any\n"
                        "exception. An exception that a language uses to end an iteration (Python's StopIteration, StopAsyncIteration and\n"
                        "GeneratorExit) stops only a catchpoint that names it. An exception raised again as it is (Python's bare raise, Ada's\n"
                        "raise;) is not thrown again. A catchpoint is numbered as a breakpoint: delete, disable, enable, condition and\n"
                        "ignore apply to it, and \"info breakpoints\" lists it. When the program stops at one, \"bt\" shows where it was thrown;\n"
                        "\"continue\" lets it propagate. C and neoscript have no exceptions (a runtime error stops the program: see \"help set\").\n"
                        "Examples:\n"
                        "  catch throw\n"
                        "  catch throw ValueError|KeyError\n"
                        "  catch throw constraint_error if N > 3\n" },
                    { { "tcatch" },
                        "tcatch throw [<exception>] [if <condition>]\n"
                        "Set a temporary catchpoint: as \"catch throw\", but it is deleted when the program first stops at it.\n" },
                    { { "delete", "d" },
                        "delete, d [<n>...]\n"
                        "Delete the breakpoints (and catchpoints) numbered <n>; with no argument delete all of them.\n" },
                    { { "disable" },
                        "disable [<n>...]\n"
                        "Disable the breakpoints numbered <n> (all with no argument): the program does not stop at them until they\n"
                        "are enabled again.\n" },
                    { { "enable" },
                        "enable [<n>...]\n"
                        "Enable the breakpoints numbered <n> (all with no argument).\n" },
                    { { "condition" },
                        "condition <n> [<condition>]\n"
                        "Stop at breakpoint <n> only when <condition> is true (an expression: see \"help print\"); with no\n"
                        "<condition> the breakpoint is unconditional again.\n" },
                    { { "ignore" },
                        "ignore <n> <count>\n"
                        "Pass breakpoint <n> the next <count> times the program comes to it (when its condition, if any, is true)\n"
                        "before stopping at it. \"info breakpoints\" shows the count left.\n" },
                    { { "info", "i" },
                        "info, i <what>\n"
                        "  info breakpoints, info b       The breakpoints and catchpoints: number, enabled (y or n), where (pending: no\n"
                        "                                 code yet) or which exceptions,\n"
                        "                                 temporary, condition, times hit and the ignore count left\n"
                        "  info locals                    The local variables of the selected frame\n"
                        "  info args                      The arguments of the selected frame\n"
                        "  info globals                   The global variables visible from the selected frame (its module's top\n"
                        "                                 level variables, the program's globals and the language's global scope)\n"
                        "  info frame                     The selected frame and its line\n"
                        "  info threads                   The program's threads (* marks the selected one): number, state and where\n"
                        "  info display                   The displays (see \"help display\")\n"
                        "Locals, args, globals, frame and threads need the program to be stopped.\n" },
                    { { "continue", "c" },
                        "continue, c\n"
                        "Continue running the program until it next stops. An empty line repeats it.\n" },
                    { { "step", "s" },
                        "step, s\n"
                        "Run to the next statement, into a call if the statement makes one (stopping at the called function's first\n"
                        "statement), or back in the caller if the function returns. An empty line repeats it.\n" },
                    { { "next", "n" },
                        "next, n\n"
                        "Run to the next statement of this function, over any calls it makes (a breakpoint in a call still stops\n"
                        "the program), or back in the caller if the function returns. An empty line repeats it.\n" },
                    { { "finish", "fin" },
                        "finish, fin\n"
                        "Run until the selected function returns, stopping at the next statement of its caller. An empty line\n"
                        "repeats it.\n" },
                    { { "backtrace", "bt", "where" },
                        "backtrace, bt, where\n"
                        "The stack of frames of the selected thread, innermost (#0) first: each frame's function, its arguments and\n"
                        "its source and line. * marks the selected frame.\n" },
                    { { "frame", "f" },
                        "frame, f [<n>]\n"
                        "Select frame <n> (see \"backtrace\") and show it; with no argument show the selected frame. print,\n"
                        "display, info locals, info args and break with no argument use the selected frame. A stop selects #0.\n" },
                    { { "up" },
                        "up [<n>]\n"
                        "Select the frame <n> (by default 1) levels out (towards the frame that called this one) and show it.\n" },
                    { { "down" },
                        "down [<n>]\n"
                        "Select the frame <n> (by default 1) levels in (towards the innermost frame, #0) and show it.\n" },
                    { { "thread" },
                        "thread [<n>]\n"
                        "Select thread <n> (see \"info threads\") and show its innermost frame; with no argument show the selected\n"
                        "thread. When the program stops all of its threads stop; the thread that stopped it is selected (thread 1 is\n"
                        "the program's own). backtrace, frame, print and the info commands use the selected thread.\n" },
                    { { "print", "p", "inspect" },
                        "print, p <expression>\n"
                        "Print the value of an expression, as $<n> = <value> (<type>), then the value's parts (an object's members,\n"
                        "an array's elements: one level, up to 64).\n"
                        "<expression> is a variable, a part of one or an expression of them:\n"
                        "  a variable                     looked up in the selected frame, then the globals (see \"info globals\")\n"
                        "  a part of one                  .<name>, [<index>] or [\"<key>\"], e.g. point.x, list[2], map[\"key\"]\n"
                        "  literals                       numbers, strings (\"...\" or '...'), true, false, nil (None, null, undefined)\n"
                        "  operators (by precedence)      unary - and ! (not); * / %; + - (+ of strings concatenates);\n"
                        "                                 < <= > >=; == != (= /= === !== ~=); && (and); || (or); parentheses\n"
                        "The program's code does not run: there are no calls or assignments. Numbers are integers or floating\n"
                        "point as they show; other values compare by how they show.\n"
                        "Examples:\n"
                        "  print count\n"
                        "  print items[0].name\n"
                        "  print (total + 1) * 2\n"
                        "  print name == \"x\" or n >= 10\n" },
                    { { "display" },
                        "display [<expression>]\n"
                        "Display an expression (a watch): its value is shown each time the program stops, as <n>: <expression> =\n"
                        "<value>. With no argument show the displays now. See \"help print\" for expressions, \"undisplay\" to\n"
                        "remove one and \"info display\" for the list.\n" },
                    { { "undisplay" },
                        "undisplay [<n>...]\n"
                        "Remove the displays numbered <n>; with no argument remove all displays.\n" },
                    { { "list", "l" },
                        "list, l [[<source>:]<line>]\n"
                        "List ten source lines around <line> (of <source>, as for \"break\"). With no argument list the ten lines\n"
                        "after those last listed (after a stop: around the line it stopped at).\n" },
                    { { "set" },
                        "set break-on-error on|off\n"
                        "on (the default): stop when a runtime error occurs that the program does not handle (e.g. an uncaught\n"
                        "exception or a trap), showing the error and where it occurred. off: such an error ends the program.\n" },
                    { { "kill", "k" },
                        "kill, k\n"
                        "End the program. The debugger stays: \"run\" runs the program again.\n" },
                    { { "quit", "q" },
                        "quit, q\n"
                        "End the program, if it is running, and leave the debugger.\n" },
                    { { "help", "h" },
                        "help, h [<command>]\n"
                        "With no argument list the commands; with a command (or one of its other names) show its page.\n" }
                };
                auto const topic = aTopic.substr(0u, aTopic.find_first_of(" \t"));
                for (auto const& p : sPages)
                    for (auto const* n : p.names)
                        if (topic == n)
                            return p.text;
                return {};
            }
        }

        console_frontend::console_frontend(std::istream& aInput, std::ostream& aOutput) :
            iInput{ aInput }, iOutput{ aOutput }
        {
        }

        void console_frontend::set_session(session& aSession)
        {
            iSession = &aSession;
        }

        void console_frontend::set_default_source(std::string const& aSource)
        {
            iDefaultSource = aSource;
        }

        bool console_frontend::read_command(std::string& aLine)
        {
            for (;;)
            {
                iOutput << prompt() << std::flush;
                if (std::getline(iInput, aLine))
                {
                    if (!aLine.empty() && aLine.back() == '\r')
                        aLine.pop_back();
                    return true;
                }
                if (iSession == nullptr)
                    return false;
                // (on Windows Ctrl+C aborts a console read in progress (the debugger's or the program's), which the stream
                // takes as the end of input: an interrupt not yet handled means read again; the signal handler can run just
                // after the read ends, so wait for it a little)
                for (int wait = 0; wait < 25 && iSession->interrupts() == iHandledInterrupts; ++wait)
                    std::this_thread::sleep_for(std::chrono::milliseconds{ 10 });
                if (iSession->interrupts() == iHandledInterrupts)
                    return false;
                iHandledInterrupts = iSession->interrupts();
                // (the C library's end of file (and error) indicator too: on Windows a stream that reached it reads nothing more)
                iInput.clear();
                if (&iInput == &std::cin)
                    std::clearerr(stdin);
                iOutput << std::endl;
            }
        }

        bool console_frontend::quit_requested() const
        {
            return iQuit;
        }

        std::string const& console_frontend::prompt()
        {
            static std::string const sPrompt = "(ndb) ";
            return sPrompt;
        }

        std::string const& console_frontend::help()
        {
            static std::string const sHelp =
                "Debugger commands (gdb's; an empty line repeats the last step, next or continue):\n"
                "  run, r                         Run the program (it stops at its breakpoints)\n"
                "  start                          Run the program, stopping at its first statement\n"
                "  break, b [<source>:]<line> [if <condition>]\n"
                "                                 Set a breakpoint (the source of the current frame or the program's if none is given)\n"
                "  tbreak [<source>:]<line>       Set a temporary breakpoint (removed when the program first stops at it)\n"
                "  catch throw [<exception>] [if <condition>]\n"
                "                                 Set a catchpoint: stop where an exception is thrown (of a type whose name matches)\n"
                "  tcatch throw [<exception>]     Set a temporary catchpoint\n"
                "  delete, d [<n>...]             Delete breakpoints and catchpoints (all if none is given)\n"
                "  disable [<n>...], enable [<n>...]\n"
                "                                 Disable or enable breakpoints (all if none is given)\n"
                "  condition <n> [<condition>]    Set (or remove) a breakpoint's condition\n"
                "  ignore <n> <count>             Pass a breakpoint <count> times before stopping at it\n"
                "  info breakpoints|locals|args|globals|frame|threads|display\n"
                "  continue, c                    Continue running\n"
                "  step, s                        Step to the next statement (into calls)\n"
                "  next, n                        Step to the next statement of this function (over calls)\n"
                "  finish, fin                    Run until this function returns (to the next statement of its caller)\n"
                "  backtrace, bt, where           The stack of frames\n"
                "  frame, f [<n>]                 Select (or show) a frame; up [<n>], down [<n>]: move\n"
                "  thread [<n>]                   Select (or show) a thread\n"
                "  print, p <expression>          The value of a variable, of a part of one (e.g. a.b[2]) or of an expression\n"
                "  display <expression>           Print an expression's value at each stop (a watch); undisplay [<n>...]: remove\n"
                "  list, l [[<source>:]<line>]    List source lines\n"
                "  set break-on-error on|off      Stop when a runtime error occurs (on by default)\n"
                "  kill, k                        End the program\n"
                "  quit, q                        End the program and leave the debugger\n"
                "  help, h [<command>]            This help, or a command's page (e.g. \"help break\")\n"
                "Ctrl+C pauses the running program.\n";
            return sHelp;
        }

        bool console_frontend::breakpoint_command(std::string const& aVerb, std::string const& aArguments)
        {
            match_command const is{ aVerb };
            if (iSession == nullptr)
                return false;
            auto numbers = [&](std::string const& aText) -> std::vector<std::uint32_t>
            {
                std::vector<std::uint32_t> result;
                std::istringstream iss{ aText };
                std::string word;
                while (iss >> word)
                    if (auto const n = to_number(word))
                        result.push_back(*n);
                    else
                        iOutput << "Not a breakpoint number: " << word << std::endl;
                return result;
            };
            if (is({ "break", "b", "br", "tbreak" }))
            {
                auto spec = aArguments;
                std::string condition;
                if (auto const ifAt = spec.find(" if "); ifAt != std::string::npos)
                {
                    condition = trim(spec.substr(ifAt + 4u));
                    spec = trim(spec.substr(0u, ifAt));
                }
                std::string source = iListSource.empty() ? iDefaultSource : iListSource;
                std::optional<std::uint32_t> line;
                if (spec.empty())
                    line = iCurrentLine;
                else if (auto const colon = spec.rfind(':'); colon != std::string::npos && colon > 0u && !(colon == 1u && spec.size() > 2u && (spec[2] == '\\' || spec[2] == '/')))
                {
                    source = spec.substr(0u, colon);
                    line = to_number(spec.substr(colon + 1u));
                }
                else
                    line = to_number(spec);
                if (!line || *line == 0u)
                {
                    iOutput << "Expected [<source>:]<line>" << std::endl;
                    return true;
                }
                if (source.empty())
                {
                    iOutput << "No source: give the breakpoint's source (<source>:<line>)" << std::endl;
                    return true;
                }
                auto const id = iSession->add_breakpoint(location{ source, *line }, condition, aVerb == "tbreak");
                iOutput << (aVerb == "tbreak" ? "Temporary breakpoint " : "Breakpoint ") << id << " at " << source << ":" << *line <<
                    (condition.empty() ? "" : " if " + condition) << std::endl;
                return true;
            }
            if (is({ "catch", "tcatch" }))
            {
                auto const [event, rest] = split_verb(aArguments);
                if (event != "throw")
                {
                    iOutput << "Expected " << aVerb << " throw [<exception>] [if <condition>]" << std::endl;
                    return true;
                }
                auto exception = rest;
                std::string condition;
                if (exception == "if" || exception.starts_with("if "))
                {
                    condition = trim(exception.substr(2u));
                    exception.clear();
                }
                else if (auto const ifAt = exception.find(" if "); ifAt != std::string::npos)
                {
                    condition = trim(exception.substr(ifAt + 4u));
                    exception = trim(exception.substr(0u, ifAt));
                }
                try
                {
                    std::regex const check{ exception, std::regex::ECMAScript | std::regex::icase };
                }
                catch (std::regex_error const&)
                {
                    iOutput << "Invalid regular expression: " << exception << std::endl;
                    return true;
                }
                auto const id = iSession->add_catchpoint(exception, condition, aVerb == "tcatch");
                iOutput << (aVerb == "tcatch" ? "Temporary catchpoint " : "Catchpoint ") << id << " (throw" << (exception.empty() ? "" : " " + exception) << ")" <<
                    (condition.empty() ? "" : " if " + condition) << std::endl;
                return true;
            }
            if (is({ "delete", "d" }))
            {
                if (aArguments.empty())
                    iSession->clear_breakpoints();
                else
                    for (auto const n : numbers(aArguments))
                        if (!iSession->remove_breakpoint(n))
                            iOutput << "No breakpoint number " << n << "." << std::endl;
                return true;
            }
            if (is({ "disable", "enable" }))
            {
                auto const enable = aVerb == "enable";
                if (aArguments.empty())
                {
                    for (auto const& b : iSession->breakpoints())
                        iSession->enable_breakpoint(b.id, enable);
                }
                else
                    for (auto const n : numbers(aArguments))
                        if (!iSession->enable_breakpoint(n, enable))
                            iOutput << "No breakpoint number " << n << "." << std::endl;
                return true;
            }
            if (is({ "condition" }))
            {
                auto const [n, condition] = split_verb(aArguments);
                auto const id = to_number(n);
                if (!id || !iSession->set_condition(*id, condition))
                    iOutput << "No breakpoint number " << n << "." << std::endl;
                else if (condition.empty())
                    iOutput << "Breakpoint " << *id << " now unconditional." << std::endl;
                return true;
            }
            if (is({ "ignore" }))
            {
                auto const [n, count] = split_verb(aArguments);
                auto const id = to_number(n);
                auto const c = to_number(count);
                if (!id || !c)
                    iOutput << "Expected ignore <n> <count>" << std::endl;
                else if (!iSession->set_ignore_count(*id, *c))
                    iOutput << "No breakpoint number " << n << "." << std::endl;
                else
                    iOutput << "Will ignore next " << *c << " crossings of breakpoint " << *id << "." << std::endl;
                return true;
            }
            if (is({ "set" }))
            {
                auto const [what, value] = split_verb(aArguments);
                if (what == "break-on-error" && (value == "on" || value == "off"))
                    iSession->set_break_on_error(value == "on");
                else
                    iOutput << "Expected set break-on-error on|off" << std::endl;
                return true;
            }
            if (is({ "info", "i" }) && (aArguments == "breakpoints" || aArguments == "b" || aArguments == "break"))
            {
                auto const breakpoints = iSession->breakpoints();
                if (breakpoints.empty())
                {
                    iOutput << "No breakpoints." << std::endl;
                    return true;
                }
                iOutput << "Num  Enb  Where" << std::endl;
                for (auto const& b : breakpoints)
                {
                    std::ostringstream num;
                    num << b.id;
                    iOutput << num.str() << std::string(num.str().size() < 5u ? 5u - num.str().size() : 1u, ' ') << (b.enabled ? "y    " : "n    ");
                    if (b.exception)
                        iOutput << "catch throw" << (b.exception->empty() ? "" : " " + *b.exception);
                    else
                        iOutput << b.requested.source << ":" << b.line.value_or(b.requested.line) << (b.line ? "" : " (pending)");
                    iOutput << (b.temporary ? " (temporary)" : "") << std::endl;
                    if (!b.condition.empty())
                        iOutput << "          stop only if " << b.condition << std::endl;
                    if (b.hits > 0u)
                        iOutput << "          " << (b.exception ? "catchpoint" : "breakpoint") << " already hit " << b.hits << (b.hits == 1u ? " time" : " times") << std::endl;
                    if (b.ignoreCount > 0u)
                        iOutput << "          will ignore next " << b.ignoreCount << " hits" << std::endl;
                }
                return true;
            }
            return false;
        }

        bool console_frontend::command(std::string const& aCommand, bool& aQuit)
        {
            aQuit = false;
            auto const [verb, arguments] = split_verb(aCommand);
            match_command const is{ verb };
            if (verb.empty())
                return true;
            if (is({ "run", "r" }))
            {
                if (iSession != nullptr)
                    iSession->set_stop_on_entry(false);
                return false;
            }
            if (is({ "start" }))
            {
                if (iSession != nullptr)
                    iSession->set_stop_on_entry(true);
                return false;
            }
            if (is({ "quit", "q" }))
            {
                aQuit = true;
                return false;
            }
            if (is({ "help", "h" }))
            {
                if (arguments.empty())
                    iOutput << help();
                else if (auto const page = help_page(arguments))
                    iOutput << *page;
                else
                    iOutput << "Undefined command: \"" << arguments << "\".  Try \"help\"." << std::endl;
                return true;
            }
            if (breakpoint_command(verb, arguments))
                return true;
            if (is({ "list", "l" }))
            {
                auto spec = arguments;
                std::string source = iListSource.empty() ? iDefaultSource : iListSource;
                std::uint32_t line = iListLine == 0u ? 1u : iListLine;
                if (auto const colon = spec.rfind(':'); colon != std::string::npos && colon > 0u)
                {
                    source = spec.substr(0u, colon);
                    line = to_number(spec.substr(colon + 1u)).value_or(1u);
                    line = line > 5u ? line - 5u : 1u;
                }
                else if (auto const n = to_number(spec))
                    line = *n > 5u ? *n - 5u : 1u;
                list(source, line, line + 9u);
                return true;
            }
            if (is({ "display" }))
            {
                if (!arguments.empty())
                {
                    iDisplays.emplace_back(iNextDisplay++, arguments);
                    iOutput << "Display " << iDisplays.back().first << ": " << arguments << " (shown when the program stops)" << std::endl;
                }
                return true;
            }
            if (is({ "undisplay" }))
            {
                if (arguments.empty())
                    iDisplays.clear();
                else
                {
                    std::istringstream iss{ arguments };
                    std::string word;
                    while (iss >> word)
                        if (auto const n = to_number(word))
                            std::erase_if(iDisplays, [&](auto const& d) { return d.first == *n; });
                }
                return true;
            }
            if (is({ "info", "i" }) && arguments == "display")
            {
                if (iDisplays.empty())
                    iOutput << "There are no auto-display expressions now." << std::endl;
                for (auto const& d : iDisplays)
                    iOutput << d.first << ": " << d.second << std::endl;
                return true;
            }
            if (is({ "continue", "c", "step", "s", "next", "n", "finish", "fin", "backtrace", "bt", "where", "frame", "f", "up", "down", "print", "p",
                "kill", "k", "thread" }) || is({ "info", "i" }))
            {
                iOutput << "The program is not being run." << std::endl;
                return true;
            }
            iOutput << "Undefined command: \"" << verb << "\".  Try \"help\"." << std::endl;
            return true;
        }

        void console_frontend::list(std::string const& aSource, std::uint32_t aFirst, std::uint32_t aLast)
        {
            auto existing = iSources.find(aSource);
            if (existing == iSources.end())
            {
                std::vector<std::string> lines;
                std::ifstream file{ std::filesystem::path{ aSource } };
                for (std::string line; std::getline(file, line);)
                {
                    if (!line.empty() && line.back() == '\r')
                        line.pop_back();
                    lines.push_back(std::move(line));
                }
                existing = iSources.emplace(aSource, std::move(lines)).first;
            }
            auto const& lines = existing->second;
            if (lines.empty())
            {
                iOutput << "(the source " << aSource << " cannot be read)" << std::endl;
                return;
            }
            for (auto l = std::max(aFirst, 1u); l <= aLast && l <= lines.size(); ++l)
                iOutput << l << "\t" << lines[l - 1u] << "\n";
            iOutput << std::flush;
            iListSource = aSource;
            iListLine = aLast + 1u;
        }

        void console_frontend::show_location(i_stop& aStop, bool aFull)
        {
            auto const frames = aStop.frames(iThread);
            if (iFrame >= frames.size())
            {
                iOutput << "No frame." << std::endl;
                return;
            }
            auto const& f = frames[iFrame];
            if (aFull)
            {
                iOutput << "#" << iFrame << "  " << (f.function.empty() ? "??" : f.function) << " (";
                bool first = true;
                for (auto const& v : aStop.locals(iFrame, iThread))
                    if (v.argument)
                    {
                        iOutput << (first ? "" : ", ") << v.name << "=" << v.value;
                        first = false;
                    }
                iOutput << ") at " << f.source << ":" << f.line << std::endl;
            }
            if (f.line != 0u)
            {
                list(f.source, f.line, f.line);
                iListSource = f.source;
                iListLine = f.line > 5u ? f.line - 5u : 1u;
                iCurrentLine = f.line;
            }
        }

        void console_frontend::print_variable(variable const& aVariable, i_stop& aStop, std::size_t aDepth, std::string const& aIndent)
        {
            iOutput << aIndent << aVariable.name << " = " << aVariable.value;
            if (!aVariable.type.empty() && aDepth == 0u)
                iOutput << "  (" << aVariable.type << ")";
            iOutput << "\n";
            if (aVariable.reference != 0u && aDepth < 1u)
            {
                std::size_t const limit = 64u;
                auto const parts = aStop.parts(aVariable.reference, 0u, limit + 1u);
                for (std::size_t i = 0u; i < parts.size() && i < limit; ++i)
                    print_variable(parts[i], aStop, aDepth + 1u, aIndent + "  ");
                if (parts.size() > limit)
                    iOutput << aIndent << "  ..." << "\n";
            }
        }

        void console_frontend::show_displays(i_stop& aStop)
        {
            for (auto const& d : iDisplays)
            {
                std::string error;
                if (auto const v = aStop.evaluate(iFrame, d.second, error, iThread))
                    iOutput << d.first << ": " << d.second << " = " << v->value << std::endl;
                else
                    iOutput << d.first << ": " << d.second << " = <" << error << ">" << std::endl;
            }
        }

        action console_frontend::stopped(i_stop& aStop)
        {
            iFrame = 0u;
            iThread.reset();
            auto const frames = aStop.frames();
            iOutput << std::flush;
            iOutput << "\n";
            switch (aStop.reason())
            {
            case stop_reason::Breakpoint:
                iOutput << "Breakpoint " << aStop.breakpoint().value_or(0u) << ", ";
                break;
            case stop_reason::Error:
                iOutput << "Program stopped by an error: " << aStop.message() << "\n";
                break;
            case stop_reason::Pause:
                iOutput << "Program paused.\n";
                break;
            case stop_reason::Throw:
                iOutput << "Catchpoint " << aStop.breakpoint().value_or(0u) << " (exception thrown), " << aStop.message() << "\n";
                break;
            default:
                break;
            }
            if (aStop.thread() != 1u)
                iOutput << "[Thread " << aStop.thread() << "] ";
            if (frames.empty())
                iOutput << "(no frame)" << std::endl;
            else
            {
                // (a step shows only the line, as gdb's, unless it entered or left a function)
                bool const sameFunction = aStop.reason() == stop_reason::Step && frames[0].function == iLastFunction && frames.size() == iLastDepth;
                show_location(aStop, !sameFunction);
            }
            if (!frames.empty())
            {
                iLastFunction = frames[0].function;
                iLastDepth = frames.size();
            }
            show_displays(aStop);
            for (;;)
            {
                std::string line;
                if (!read_command(line))
                    return action::Terminate;
                if (trim(line).empty())
                    line = iLastCommand;
                bool resume = false;
                auto const result = execute(aStop, line, resume);
                if (resume)
                    return result;
            }
        }

        action console_frontend::execute(i_stop& aStop, std::string const& aCommand, bool& aResume)
        {
            aResume = false;
            auto const [verb, arguments] = split_verb(aCommand);
            match_command const is{ verb };
            if (verb.empty())
                return action::Continue;
            auto resume = [&](action aAction)
            {
                iLastCommand = aCommand;
                aResume = true;
                return aAction;
            };
            if (is({ "continue", "c" }))
                return resume(action::Continue);
            if (is({ "step", "s" }))
                return resume(action::StepInto);
            if (is({ "next", "n" }))
                return resume(action::StepOver);
            if (is({ "finish", "fin" }))
                return resume(action::StepOut);
            if (is({ "kill", "k" }))
                return resume(action::Terminate);
            if (is({ "quit", "q" }))
            {
                iQuit = true;
                return resume(action::Terminate);
            }
            if (is({ "run", "r", "start" }))
            {
                iOutput << "The program is already running (kill it first)." << std::endl;
                return action::Continue;
            }
            if (is({ "help", "h" }))
            {
                if (arguments.empty())
                    iOutput << help();
                else if (auto const page = help_page(arguments))
                    iOutput << *page;
                else
                    iOutput << "Undefined command: \"" << arguments << "\".  Try \"help\"." << std::endl;
                return action::Continue;
            }
            if (breakpoint_command(verb, arguments))
                return action::Continue;
            if (is({ "backtrace", "bt", "where" }))
            {
                auto const frames = aStop.frames(iThread);
                for (std::size_t i = 0u; i < frames.size(); ++i)
                {
                    auto const& f = frames[i];
                    iOutput << (i == iFrame ? "* " : "  ") << "#" << i << "  " << (f.function.empty() ? "??" : f.function) << " (";
                    bool first = true;
                    for (auto const& v : aStop.locals(i, iThread))
                        if (v.argument)
                        {
                            iOutput << (first ? "" : ", ") << v.name << "=" << v.value;
                            first = false;
                        }
                    iOutput << ") at " << f.source << ":" << f.line << std::endl;
                }
                return action::Continue;
            }
            if (is({ "frame", "f", "up", "down" }))
            {
                auto const frames = aStop.frames(iThread);
                if (frames.empty())
                {
                    iOutput << "No stack." << std::endl;
                    return action::Continue;
                }
                auto const n = to_number(arguments);
                if (verb == "up")
                    iFrame = std::min(frames.size() - 1u, iFrame + n.value_or(1u));
                else if (verb == "down")
                {
                    auto const by = n.value_or(1u);
                    if (by > iFrame)
                        iOutput << "Bottom (innermost) frame selected; you cannot go down." << std::endl;
                    iFrame = by > iFrame ? 0u : iFrame - by;
                }
                else if (n)
                {
                    if (*n >= frames.size())
                    {
                        iOutput << "No frame at level " << *n << "." << std::endl;
                        return action::Continue;
                    }
                    iFrame = *n;
                }
                show_location(aStop, true);
                return action::Continue;
            }
            if (is({ "thread" }))
            {
                if (arguments.empty())
                {
                    iOutput << "[Current thread is " << iThread.value_or(aStop.thread()) << "]" << std::endl;
                    return action::Continue;
                }
                auto const n = to_number(arguments);
                bool found = false;
                for (auto const& t : aStop.threads())
                    if (n && t.id == *n)
                        found = true;
                if (!found)
                {
                    iOutput << "Unknown thread " << arguments << "." << std::endl;
                    return action::Continue;
                }
                iThread = *n == aStop.thread() ? std::optional<std::uint32_t>{} : n;
                iFrame = 0u;
                iOutput << "[Switching to thread " << *n << "]" << std::endl;
                show_location(aStop, true);
                return action::Continue;
            }
            if (is({ "info", "i" }))
            {
                if (arguments == "locals" || arguments == "args")
                {
                    bool any = false;
                    for (auto const& v : aStop.locals(iFrame, iThread))
                        if (v.argument == (arguments == "args"))
                        {
                            iOutput << v.name << " = " << v.value << std::endl;
                            any = true;
                        }
                    if (!any)
                        iOutput << (arguments == "args" ? "No arguments." : "No locals.") << std::endl;
                    return action::Continue;
                }
                if (arguments == "globals")
                {
                    auto const globals = aStop.globals(iFrame, iThread);
                    if (globals.empty())
                        iOutput << "No globals." << std::endl;
                    for (auto const& v : globals)
                        iOutput << v.name << " = " << v.value << std::endl;
                    return action::Continue;
                }
                if (arguments == "frame")
                {
                    show_location(aStop, true);
                    return action::Continue;
                }
                if (arguments == "threads")
                {
                    for (auto const& t : aStop.threads())
                    {
                        iOutput << (t.id == iThread.value_or(aStop.thread()) ? "* " : "  ") << t.id << "  " << t.state;
                        if (t.top)
                            iOutput << "  " << t.top->function << " at " << t.top->source << ":" << t.top->line;
                        iOutput << std::endl;
                    }
                    return action::Continue;
                }
                if (arguments == "display")
                {
                    if (iDisplays.empty())
                        iOutput << "There are no auto-display expressions now." << std::endl;
                    for (auto const& d : iDisplays)
                        iOutput << d.first << ": " << d.second << std::endl;
                    return action::Continue;
                }
                iOutput << "Expected info breakpoints|locals|args|globals|frame|threads|display" << std::endl;
                return action::Continue;
            }
            if (is({ "print", "p", "inspect" }))
            {
                if (arguments.empty())
                {
                    iOutput << "Expected an expression" << std::endl;
                    return action::Continue;
                }
                std::string error;
                if (auto const v = aStop.evaluate(iFrame, arguments, error, iThread))
                {
                    auto named = *v;
                    named.name = "$" + std::to_string(iNextValue++);
                    print_variable(named, aStop, 0u, {});
                    iOutput << std::flush;
                }
                else
                    iOutput << error << std::endl;
                return action::Continue;
            }
            if (is({ "display" }))
            {
                if (arguments.empty())
                    show_displays(aStop);
                else
                {
                    iDisplays.emplace_back(iNextDisplay++, arguments);
                    std::string error;
                    if (auto const v = aStop.evaluate(iFrame, arguments, error, iThread))
                        iOutput << iDisplays.back().first << ": " << arguments << " = " << v->value << std::endl;
                    else
                        iOutput << iDisplays.back().first << ": " << arguments << " = <" << error << ">" << std::endl;
                }
                return action::Continue;
            }
            bool quit = false;
            command(aCommand, quit);
            return action::Continue;
        }

        void console_frontend::breakpoint_changed(breakpoint const& aBreakpoint)
        {
            (void)aBreakpoint;
        }
    }
}
