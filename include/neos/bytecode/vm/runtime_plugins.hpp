/*
  runtime_plugins.hpp

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
                // true if there is a runtime plugin for the namespace aNamespace (it is not loaded)
                bool plugin_available(std::string const& aNamespace);
                // the host's functions that plugins call (see neos/runtime/plugin.h: neos_rt_host; defined by the VM)
                void const* host();
                // (a program's entry point has returned: the plugins that it used finish their work)
                void program_end();
                // the function an import named "x::f" refers to (the runtime plugin for "x" is loaded on first use);
                // throws std::runtime_error if there is none or if its type differs from the import's (WebAssembly
                // value types)
                function const& resolve(std::string const& aImport, std::vector<std::uint8_t> const& aParameters, std::vector<std::uint8_t> const& aResults);
                // true if the function can change the host environment (it cannot be called in restricted mode)
                bool restricted(function const& aFunction);
                // (for calling it directly) its C function, its C signature (neos_rt_type values: aParameters and aResult) and its
                // plugin's error function (null if it has none)
                void const* c_function(function const& aFunction, std::vector<std::uint8_t>& aParameters, std::uint8_t& aResult, char const* (*&aError)());
                // (and) its plugin's error pending flag: nonzero if its error function may return an error (null if it has none: its 
                // error function is called after every call)
                long const* error_pending(function const& aFunction);
                // call it (arguments and results are raw WebAssembly values: the arguments are in aBuffer; its result, if 
                // any, replaces the first)
                void call(function const& aFunction, value* aBuffer, marshaller const& aMarshaller);
            }
        }
    }
}
