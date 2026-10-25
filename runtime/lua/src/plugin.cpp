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

/*
  The Lua runtime plugin (neos.runtime.lua): the functions that code compiled from Lua calls (as imports named "lua::f"; see 
  api.cpp) and the Lua standard library. See lua.hpp.
*/

#include "lua.hpp"
#include "api.hpp"

namespace
{
    neos_rt_plugin const& plugin()
    {
        static auto const sFunctions = neos::lua_runtime::api_functions();
        static neos_rt_plugin const sPlugin =
        {
            NEOS_RUNTIME_PLUGIN_VERSION,
            "lua",
            "Lua runtime",
            static_cast<unsigned>(sFunctions.size()),
            sFunctions.data()
        };
        return sPlugin;
    }
}

NEOS_RUNTIME_PLUGIN_ENTRY
{
    return &plugin();
}

NEOS_RUNTIME_PLUGIN_SET_HOST
{
    neos::lua_runtime::set_host(aHost);
}

NEOS_RUNTIME_PLUGIN_END
{
    neos::lua_runtime::program_ended();
}
