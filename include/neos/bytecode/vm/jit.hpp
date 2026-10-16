/*
  jit.hpp

  Copyright (c) 2026 Leigh Johnston.  All Rights Reserved.

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
#include <cstdint>
#include <vector>
#include <memory>
#include <functional>

namespace neos
{
    namespace bytecode
    {
        namespace vm
        {
            namespace jit
            {
                // Compiles the (pre-decoded) WebAssembly functions of a module to native code.
                // Supported targets: x86-64 (Windows x64 and System V ABIs); elsewhere available() is false and the
                // interpreter is used.

                using value = std::uint64_t;

                struct function_description
                {
                    std::vector<std::uint8_t> const* parameters = nullptr; ///< value types
                    std::vector<std::uint8_t> const* results = nullptr;    ///< value types
                    std::vector<std::uint8_t> const* locals = nullptr;     ///< value types of locals declared by the body
                    bool imported = false;
                    std::size_t codeBegin = 0u; ///< token index
                    std::size_t codeEnd = 0u;   ///< token index
                };

                // linear memory (updated by the VM when it moves or grows)
                struct memory
                {
                    std::uint8_t* base = nullptr;
                    std::uint64_t size = 0u;
                };

                struct module_description
                {
                    std::vector<function_description> functions;
                    std::vector<std::uint64_t> const* tokens = nullptr; ///< decoded code: an opcode token followed by its immediates
                    std::vector<value>* globals = nullptr;
                    std::vector<std::uint8_t> const* globalTypes = nullptr;
                    memory const* linearMemory = nullptr;
                };

                // (an imported function: aBuffer holds its arguments and receives its results; its size is the larger of 
                // their counts)
                using host_function = std::function<void(std::uint32_t aFunction, value* aBuffer)>;

                class compiled_module;
                struct compiled_module_deleter { void operator()(compiled_module* aModule) const; };
                using compiled_module_ptr = std::unique_ptr<compiled_module, compiled_module_deleter>;

                bool available();
                // returns null if the module uses an instruction the JIT does not support (the interpreter is then used)
                compiled_module_ptr compile(module_description const& aModule, host_function aHost);
                // traps are thrown as std::runtime_error (exceptions thrown by host functions propagate unchanged)
                std::vector<value> invoke(compiled_module& aModule, std::uint32_t aFunction, std::vector<value> const& aArguments);
            }
        }
    }
}
