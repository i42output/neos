/*
  compiler_stub.hpp

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
