/*
  plugin.cpp

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

// The C runtime plugin (neos.runtime.c): the C library for neoscript (c.c), and the C language's runtime (the C memory, "c::rt.*",
// and the native parts of its standard library, "c::libc.*")

#include <vector>
#include "core.hpp"

extern "C"
{
    void neos_c_set_host(neos_rt_host const* aHost);
    neos_rt_function const* neos_c_functions(unsigned* aCount);
}

namespace neos::c_runtime
{
    void set_host(neos_rt_host const* aHost);
}

NEOS_RUNTIME_PLUGIN_SET_HOST
{
    neos_c_set_host(aHost);
    neos::c_runtime::set_host(aHost);
}

NEOS_RUNTIME_PLUGIN_ENTRY
{
    static std::vector<neos_rt_function> const sFunctions = []()
    {
        std::vector<neos_rt_function> result;
        unsigned count = 0u;
        auto const* neoscript = neos_c_functions(&count);
        result.insert(result.end(), neoscript, neoscript + count);
        auto const& language = neos::c_runtime::functions();
        result.insert(result.end(), language.begin(), language.end());
        return result;
    }();
    static neos_rt_plugin const sPlugin =
    {
        NEOS_RUNTIME_PLUGIN_VERSION,
        "c",
        "The C runtime",
        static_cast<unsigned>(sFunctions.size()),
        sFunctions.data()
    };
    return &sPlugin;
}
