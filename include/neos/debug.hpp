/*
  debug.hpp

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

#pragma once

#include <neos/neos.hpp>
#include <cstdint>
#include <cstddef>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <map>
#include <deque>
#include <vector>

namespace neos
{
    namespace debug
    {
        // The debugger: a program that runs under a session (see set_session) stops at its breakpoints (and as its session's
        // front end (e.g. a terminal's, an IDE's) asks: stepping, pausing, on an error) and while it is stopped its front end
        // examines it (its threads, their frames, the variables they can see) on the thread that stopped, then resumes it. It
        // is language neutral: the programs' languages' compilers describe their functions' variables (see
        // bytecode::VariablesSection) and their runtime plugins describe their values (see NEOS_RUNTIME_PLUGIN_DEBUG). A
        // program that runs under a session is interpreted (its functions' frames are the interpreter's).

        // a place in a program's source: the source's name (as the compiler named it, e.g. its path), a line (1-based) and a column
        struct location
        {
            std::string source;
            std::uint32_t line = 0u;
            std::uint32_t column = 0u;          ///< (1-based, in bytes: the statement that begins there (e.g. an AST node's); 0: the line's statements)
        };

        struct breakpoint
        {
            std::uint32_t id = 0u;
            location requested;
            std::string condition;              ///< (an expression of the language of the code it is in: the program stops only when it is true; empty: always)
            std::uint32_t ignoreCount = 0u;     ///< (the number of hits to pass before stopping)
            bool enabled = true;
            bool temporary = false;             ///< (removed when the program first stops at it)
            std::optional<std::uint32_t> line;  ///< (the line of the statement it is on: the first at or after the line requested that has code; none: not found in
                                                ///< the code loaded so far)
            std::uint32_t hits = 0u;
            std::optional<std::string> exception;   ///< (a catchpoint (gdb's "catch throw"): the program stops when its code throws an exception 
                                                    ///< whose type's name this regular expression matches (empty: any but its language's control
                                                    ///< flow, e.g. Python's StopIteration); requested and line are unused)
        };

        enum class stop_reason
        {
            Entry,          ///< (the program's first statement: see session::set_stop_on_entry)
            Breakpoint,
            Step,
            Pause,          ///< (see session::pause)
            Throw,          ///< (an exception thrown, where it is thrown: a catchpoint (see session::add_catchpoint); message: its type and message)
            Error           ///< (a runtime error, before it stops the program: see session::set_break_on_error)
        };

        // how a stopped program resumes
        enum class action
        {
            Continue,
            StepInto,       ///< (to the next statement)
            StepOver,       ///< (to the next statement of the frame, or of a frame that called it)
            StepOut,        ///< (to the next statement of a frame that called it)
            Terminate       ///< (the program ends)
        };

        struct frame
        {
            std::string function;
            std::string source;
            std::uint32_t line = 0u;            ///< (0: unknown)
            std::uint32_t column = 0u;          ///< (1-based, in bytes; 0: unknown)
            std::uint32_t endLine = 0u;         ///< (the last line of the expression it is evaluating; 0: unknown)
            std::uint32_t endColumn = 0u;       ///< (1-based, in bytes: the column after the expression's last; 0: unknown)
            std::string language;               ///< (of the code it is running (e.g. "python"); empty: not known)
        };

        // a variable (or a part of one, e.g. an element of an array): its value as text; its parts, if it has any, by its reference
        struct variable
        {
            std::string name;
            std::string type;
            std::string value;
            std::uint64_t reference = 0u;       ///< (its parts (see i_stop::parts): 0, none; valid while the program is stopped)
            std::size_t partCount = 0u;         ///< (the number of its parts, if known (0 with a reference: not known))
            bool argument = false;              ///< (a parameter of its function)
        };

        struct thread_info
        {
            std::uint32_t id = 0u;              ///< (1: the program's own; others in the order they first ran the program's code)
            std::string state;                  ///< (e.g. "stopped")
            std::optional<frame> top;           ///< (its innermost frame)
        };

        // a stopped program (valid during i_frontend::stopped, which is called on the thread that stopped): what its threads
        // are doing; frames are indexed innermost first (0: the one running); the thread is the one that stopped unless one
        // is given
        class i_stop
        {
        public:
            virtual ~i_stop() = default;
        public:
            virtual stop_reason reason() const = 0;
            virtual std::string const& message() const = 0;                     ///< (Error: the error)
            virtual std::optional<std::uint32_t> breakpoint() const = 0;        ///< (Breakpoint: which)
            virtual std::uint32_t thread() const = 0;
            virtual std::vector<thread_info> threads() = 0;
            virtual std::vector<frame> frames(std::optional<std::uint32_t> aThread = {}) = 0;
            virtual std::vector<variable> locals(std::size_t aFrame, std::optional<std::uint32_t> aThread = {}) = 0;   ///< (parameters first)
            virtual std::vector<variable> globals(std::size_t aFrame, std::optional<std::uint32_t> aThread = {}) = 0;
            virtual std::vector<variable> parts(std::uint64_t aReference, std::size_t aStart = 0u, std::size_t aCount = ~std::size_t{ 0u }) = 0;
            // the value of an expression as code of the frame would evaluate it (a variable's name, a part of one (e.g. "a.b[2]"),
            // or (if the frame's language can compile source while its program runs) an expression of the frame's language);
            // none: aError, why not
            virtual std::optional<variable> evaluate(std::size_t aFrame, std::string const& aExpression, std::string& aError,
                std::optional<std::uint32_t> aThread = {}) = 0;
        };

        // a session's front end: it decides how the program resumes, on the thread that stopped (the program's other threads are
        // stopped too, until it returns)
        class i_frontend
        {
        public:
            virtual ~i_frontend() = default;
        public:
            virtual action stopped(i_stop& aStop) = 0;
            virtual void breakpoint_changed(breakpoint const& aBreakpoint) { (void)aBreakpoint; } ///< (e.g. found the line it is on; called on the program's thread)
        };

        // a debugging session: its breakpoints and settings, for the programs that run under it (see set_session); its
        // breakpoints and settings can be changed (and the program paused) from any thread, while the programs run
        class session
        {
        public:
            explicit session(i_frontend& aFrontend);
            ~session();
        public:
            i_frontend& frontend() const;
            // breakpoints
            std::uint32_t add_breakpoint(location const& aLocation, std::string const& aCondition = {}, bool aTemporary = false);
            // a catchpoint: the program stops where its code throws an exception whose type's name aException (a regular expression: see
            // breakpoint::exception) matches; numbered, enabled, conditional and ignored as a breakpoint is
            std::uint32_t add_catchpoint(std::string const& aException = {}, std::string const& aCondition = {}, bool aTemporary = false);
            bool remove_breakpoint(std::uint32_t aId);
            void clear_breakpoints();
            bool enable_breakpoint(std::uint32_t aId, bool aEnable);
            bool set_condition(std::uint32_t aId, std::string const& aCondition);
            bool set_ignore_count(std::uint32_t aId, std::uint32_t aCount);
            std::vector<breakpoint> breakpoints() const;
            std::optional<breakpoint> find_breakpoint(std::uint32_t aId) const;
            // settings
            bool stop_on_entry() const;
            void set_stop_on_entry(bool aStop);
            bool break_on_error() const;
            void set_break_on_error(bool aBreak);
            // the program stops at its next statement (callable from any thread, e.g. a signal handler: it only sets a flag)
            void pause();
            // Ctrl+C (callable from a signal handler): pauses the program if it is running (not when it is already stopped);
            // counted, as on Windows it also aborts a console read in progress (see console_frontend::read_command)
            void interrupt();
            std::uint32_t interrupts() const { return iInterrupts.load(std::memory_order_acquire); }
            // the program ends at its next statement
            void terminate();
            // (the program is run (again): no pause or termination is pending)
            void begin_run();
        public:
            // (the VM's)
            std::uint64_t version() const { return iVersion.load(std::memory_order_acquire); }  ///< (changes when the breakpoints change)
            bool pause_requested() const { return iPause.load(std::memory_order_relaxed); }
            bool terminate_requested() const { return iTerminate.load(std::memory_order_relaxed); }
            bool any_stopped() const { return iStopped.load(std::memory_order_acquire); }
            bool catching() const { return iCatching.load(std::memory_order_acquire); }    ///< (an enabled catchpoint: cheap)
            std::vector<std::uint32_t> catchpoints_for(std::string const& aType, bool aControl) const;  ///< (the enabled catchpoints that an exception thrown matches)
            std::uint32_t thread_id();                                          ///< (the calling thread's (see thread_info::id))
            // a breakpoint was hit: whether the program stops (its ignore count, and its condition (aCondition: evaluates it))
            bool hit(std::uint32_t aId, std::function<bool(std::string const& aCondition)> const& aCondition);
            void bound(std::uint32_t aId, std::uint32_t aLine);                 ///< (a breakpoint was found on a line)
            // a thread stops: the front end is called (the other threads stop at their next statements: see park) unless another
            // thread has stopped, in which case this one waits (as park) and resumes (Continue); returns how this thread resumes
            action stop(i_stop& aStop);
            // the calling thread waits while another thread is stopped, running the requests that its front end makes of this
            // thread (see run_on: e.g. for this thread's frames)
            void park();
            // (the stopped thread's front end) run aRequest on the thread aThread (which is waiting: see park); false if it is not
            bool run_on(std::uint32_t aThread, std::function<void()> const& aRequest);
            std::vector<std::uint32_t> parked_threads() const;
        private:
            void changed();
        private:
            i_frontend& iFrontend;
            mutable std::mutex iMutex;
            std::condition_variable iResumed;
            std::vector<breakpoint> iBreakpoints;
            std::uint32_t iNextId = 1u;
            std::atomic<std::uint64_t> iVersion = 1u;
            std::atomic<bool> iCatching = false;
            std::atomic<bool> iPause = false;
            std::atomic<std::uint32_t> iInterrupts = 0u;
            std::atomic<bool> iTerminate = false;
            std::atomic<bool> iStopped = false;
            bool iStopOnEntry = false;
            bool iBreakOnError = true;
            std::map<std::thread::id, std::uint32_t> iThreadIds;
            std::uint64_t iStopSerial = 0u;
            struct parked
            {
                std::deque<std::function<void()>> requests;
                std::size_t done = 0u;
                std::size_t posted = 0u;
            };
            std::map<std::uint32_t, parked> iParked;
        };

        // the session that the programs which the calling thread runs (see bytecode::vm::thread) run under (none: not debugged)
        void set_session(std::shared_ptr<session> aSession);
        std::shared_ptr<session> current_session();

        // A read of the program's input that blocks (the VM's host_std_read and host_std_getc, while debugging): Ctrl+C (see 
        // session::interrupt) and a stop of the program (see session::stop) interrupt it (the read fails, on every platform), so 
        // that the thread can stop, or park, where it is and then read again; finish() is true if it was interrupted
        class blocking_read
        {
        public:
            explicit blocking_read(bool aActive = true);
            ~blocking_read();
        public:
            bool finish();
        private:
            std::size_t iSlot;
        };
        // (interrupt the blocking reads (callable from a signal handler); the number of them that may still be blocked)
        std::size_t interrupt_reads();

        // a front end for an application whose user interface runs on a thread of its own (e.g. an IDE's): stopped() tells the
        // application (aStopped, on the program's thread: e.g. to post a message to its UI thread) and then waits, running the
        // requests that the application posts (on the program's thread, where the program can be examined) until the
        // application resumes the program
        class remote_frontend : public i_frontend
        {
        public:
            using stopped_callback = std::function<void(stop_reason aReason, std::uint32_t aThread, std::optional<std::uint32_t> aBreakpoint, std::string const& aMessage)>;
            using breakpoint_callback = std::function<void(breakpoint const& aBreakpoint)>;
        public:
            remote_frontend(stopped_callback aStopped, breakpoint_callback aBreakpointChanged = {});
        public:
            action stopped(i_stop& aStop) override;
            void breakpoint_changed(breakpoint const& aBreakpoint) override;
        public:
            // (any thread) whether the program is stopped
            bool is_stopped() const;
            // (any thread) run aRequest with the stopped program (on its thread): false if it is not stopped (the request is not run);
            // waits for it to run if aWait (else aRequest runs later: e.g. it posts its results back to the UI thread)
            bool post(std::function<void(i_stop& aStop)> aRequest, bool aWait = true);
            // (any thread) the program resumes
            void resume(action aAction);
        private:
            stopped_callback iStopped;
            breakpoint_callback iBreakpointChanged;
            mutable std::mutex iMutex;
            std::condition_variable iChanged;
            bool iIsStopped = false;
            std::deque<std::function<void(i_stop&)>> iRequests;
            std::optional<action> iResume;
        };

        // the console's debugger (neos -g, and the console's "debug" command): a front end that reads commands (gdb's) from
        // aInput and writes to aOutput
        class console_frontend : public i_frontend
        {
        public:
            console_frontend(std::istream& aInput, std::ostream& aOutput);
        public:
            action stopped(i_stop& aStop) override;
            void breakpoint_changed(breakpoint const& aBreakpoint) override;
        public:
            void set_session(session& aSession);
            void set_default_source(std::string const& aSource);   ///< (the source of a breakpoint whose source is not given, before the program stops)
            // a command before (or after) the program runs: false if it is "run" (or "start": with stop on entry) and the program
            // should run; aQuit is set if it is "quit"
            bool command(std::string const& aCommand, bool& aQuit);
            // prompt for and read a command: false at the end of input (a read that Ctrl+C aborted is read again)
            bool read_command(std::string& aLine);
            bool quit_requested() const;    ///< ("quit" while the program was stopped: the debugger ends when the program has)
            static std::string const& prompt();
            static std::string const& help();
        private:
            action execute(i_stop& aStop, std::string const& aCommand, bool& aResume);
            bool breakpoint_command(std::string const& aVerb, std::string const& aArguments);
            void show_location(i_stop& aStop, bool aFull);
            void show_displays(i_stop& aStop);
            void list(std::string const& aSource, std::uint32_t aFirst, std::uint32_t aLast);
            void print_variable(variable const& aVariable, i_stop& aStop, std::size_t aDepth, std::string const& aIndent);
        private:
            std::istream& iInput;
            std::ostream& iOutput;
            session* iSession = nullptr;
            std::size_t iFrame = 0u;
            std::optional<std::uint32_t> iThread;
            std::string iDefaultSource;
            std::string iLastCommand;
            std::string iListSource;
            std::uint32_t iListLine = 0u;
            std::uint32_t iCurrentLine = 0u;
            std::vector<std::pair<std::uint32_t, std::string>> iDisplays;
            std::uint32_t iNextDisplay = 1u;
            std::uint32_t iNextValue = 1u;
            std::string iLastFunction;
            std::size_t iLastDepth = 0u;
            std::map<std::string, std::vector<std::string>> iSources;
            bool iQuit = false;
            std::uint32_t iHandledInterrupts = 0u;
        };

        // (a source's name as a breakpoint gives it and as a compiler named it: the same source)
        bool same_source(std::string_view aRequested, std::string_view aCompiled);
    }
}
