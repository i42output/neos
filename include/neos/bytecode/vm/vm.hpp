/*
  vm.hpp

  Copyright (c) 2025 Leigh Johnston.  All Rights Reserved.

  This program is free software: you can redistribute it and / or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
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