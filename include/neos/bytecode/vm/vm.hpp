/*
  vm.hpp

  Copyright (c) 2025 Leigh Johnston.  All Rights Reserved.

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
#include <array>
#include <vector>
#include <optional>
#include <thread>
#include <string>
#include <iostream>
#include <chrono>
#include <functional>
#include <neos/bytecode/bytecode.hpp>
#include <neos/bytecode/exceptions.hpp>
#include <neos/bytecode/opcodes.hpp>
#include <neos/language/type.hpp>

namespace neos
{
    namespace bytecode
    {
        namespace vm
        {
            // (the compilation of source while a program runs (see neos_rt_host::load): the text of the source aSource (named
            // aSourceName) in the language aLanguage with the language's options aOptions (none: aError, the error))
            using compile_service = std::function<std::optional<text>(std::string const& aLanguage, std::string const& aSource, std::string const& aSourceName,
                std::string const& aOptions, std::string& aError)>;
            void set_compile_service(compile_service aService);
            // (the program's command line arguments (see neos_rt_host::arguments))
            void set_program_arguments(std::vector<std::string> aArguments);

            class thread : public std::thread
            {
            public:
                thread(text const& aText, std::ostream& aOutput, std::istream& aInput, bool aRestricted = false) :
                    iText{ aText },
                    iOutput{ aOutput },
                    iInput{ aInput },
                    iRestricted{ aRestricted }
                {
                    // start executing only once all members have been constructed
                    static_cast<std::thread&>(*this) = std::thread{ [this]() { execute(); } };
                }
            public:
                void execute();
            public:
                language::data_type const& result() const
                {
                    return iResult;
                }
                std::optional<std::string> const& error() const
                {
                    return iError;
                }
                std::optional<int> const& exit_status() const   ///< (the status the program exited with (neos::exit), if it did)
                {
                    return iExitStatus;
                }
            public:
                std::string metrics() const;
            private:
                text const iText;
                std::ostream& iOutput;
                std::istream& iInput;
                bool const iRestricted; ///< restricted mode: runtime plugin functions that can change the host environment cannot be called
                language::data_type iResult;
                std::optional<std::string> iError;
                std::optional<int> iExitStatus;
                std::uint64_t iInstructions = 0u;
                bool iJit = false;
                std::chrono::steady_clock::duration iExecutionTime = {};
            };
        }
    }
}