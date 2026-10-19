/*
  compiler_stub.hpp

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

// A compiler stub's built-in compiler (see CMakeLists.txt's add_neos_compiler_stub, which generates their definitions): its language, the 
// entry points of its concept libraries and compiler plugin (linked in), and its resources (its schema, "<language>.neos", and its packages, 
// "packages/<language>/...")

#include <neos/neos.hpp>
#include <cstddef>
#include <neolib/app/i_application.hpp>
#include <neolib/plugin/i_plugin.hpp>

namespace neos::stub
{
    using plugin_entry_point = void (*)(neolib::i_application& aApplication, const neolib::i_string& aPluginFolder, neolib::i_ref_ptr<neolib::i_plugin>& aPluginInstance);

    struct resource
    {
        char const* name;
        unsigned char const* data;
        std::size_t size;
    };

    extern char const* const language;
    extern plugin_entry_point const plugins[];
    extern std::size_t const pluginCount;
    extern resource const resources[];
    extern std::size_t const resourceCount;
}
