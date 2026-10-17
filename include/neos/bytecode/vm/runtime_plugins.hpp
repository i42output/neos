/*
  runtime_plugins.hpp

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
#include <string>
#include <string_view>
#include <vector>
#include <functional>
#include <stdexcept>

namespace neos
{
    namespace bytecode
    {
        namespace vm
        {
            namespace runtime
            {
                // Runtime plugins: native functions called through libffi (see neos/runtime/plugin.h)

                using value = std::uint64_t;

                struct function;

                struct marshaller
                {
                    std::function<std::string(std::uint32_t aAddress)> readString;
                    std::function<char const*(std::uint32_t aAddress)> stringPointer; ///< (the string in place if it is NUL terminated, otherwise null)
                    std::function<std::uint32_t(std::string_view aString)> allocateString;
                    std::function<void*(std::uint32_t aHandle)> objectPointer;  ///< (the object of a handle; null for 0)
                    std::function<std::uint32_t(void* aObject)> objectHandle;   ///< (a handle for a new reference; 0 for null)
                };

                // the error a runtime plugin function reports
                struct plugin_error : std::runtime_error
                {
                    using std::runtime_error::runtime_error;
                };
                // false if neos was built without libffi
                bool available();
                // the host's functions that plugins call (see neos/runtime/plugin.h: neos_rt_host; defined by the VM)
                void const* host();
                // the function an import named "x::f" refers to (the runtime plugin for "x" is loaded on first use);
                // throws std::runtime_error if there is none or if its type differs from the import's (WebAssembly
                // value types)
                function const& resolve(std::string const& aImport, std::vector<std::uint8_t> const& aParameters, std::vector<std::uint8_t> const& aResults);
                // true if the function can change the host environment (it cannot be called in restricted mode)
                bool restricted(function const& aFunction);
                // call it (arguments and results are raw WebAssembly values: the arguments are in aBuffer; its result, if 
                // any, replaces the first)
                void call(function const& aFunction, value* aBuffer, marshaller const& aMarshaller);
            }
        }
    }
}
