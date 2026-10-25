/*
  api.hpp

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

// The runtime's internal interface: the programs that call it, calls (by native functions and by compiled code), protected calls,
// to-be-closed variables, coroutines and load

#pragma once

#include "lua.hpp"

namespace neos::lua_runtime
{
    // (a program (the main program, or a chunk that load compiled): its dispatcher and its global %lua.nret (the number of the results of
    // its calls))
    struct program_info
    {
        void* program = nullptr;
        std::uint32_t dispatcher = 0u;
        std::uint32_t resultsGlobal = 0u;
        unsigned long long* results = nullptr;
    };

    void set_host(neos_rt_host const* aHost);
    program_info* find_program(void* aProgram);
    program_info& register_program(std::uint32_t aDispatcher, std::uint32_t aResultsGlobal);
    void forget_program(void* aProgram);
    void set_program_results(std::span<value const> aResults);  ///< (a call's results as compiled code receives them)

    void call(value aFunction, std::span<value const> aArguments, value_list& aResults, value aDescription, bool aDirect);

    // (a protected call (pcall, xpcall): while it exists, an error raised is its (xpcall's handler is called where the error is raised))
    struct protector
    {
        explicit protector(value aHandler);
        ~protector();
        protector(protector const&) = delete;
        protector& operator=(protector const&) = delete;
        void restore(value aError);     ///< (the thread's stacks as they were when it began; its to-be-closed variables closed)
        value handler;
        bool handled = false;           ///< (the handler was called (the error's value is its result))
        std::size_t extra = 0u;
        std::size_t arguments = 0u;
        std::size_t tbc = 0u;
        value_list keep;
    };
    std::size_t protection_depth();
    std::string const& uncaught_traceback();

    // (to-be-closed variables: those above the depth are closed (in reverse order), with the error if aErrorClosing)
    void close_tbc(std::size_t aDepth, value aError, bool aErrorClosing);

    // (coroutines)
    value make_thread(value aFunction);
    bool resume(thread_cell& aThread, std::span<value const> aArguments, value_list& aResults);
    bool close_thread(thread_cell& aThread, value& aError);

    // (os.exit: the program ends with the status (as an error that nothing can handle))
    [[noreturn]] void request_exit(int aStatus);

    // (load)
    void loaded_chunk(value aFunction);
    std::string compile_error_message(std::string const& aError, std::string const& aChunkName, std::string_view aSource);

    // (the garbage collector)
    void collect_garbage();
    bool gc_stopped();
    void gc_stop(bool aStop);

    std::span<neos_rt_function const> api_functions();
    void program_ended();
}
