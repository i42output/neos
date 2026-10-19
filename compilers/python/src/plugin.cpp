/*
  plugin.cpp

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

// The Python compiler plugin (python.ncp)

#include <neolib/neolib.hpp>
#include <neolib/app/i_application.hpp>
#include <neolib/plugin/i_plugin.hpp>
#include <neos/language/compiler_plugin.hpp>
#include "compiler.hpp"

// (NEOS_PLUGIN_ENTRY_POINT: the name of the entry point of the plugin linked into an executable (e.g. a compiler stub))
#ifdef NEOS_PLUGIN_ENTRY_POINT
void NEOS_PLUGIN_ENTRY_POINT(neolib::i_application& aApplication, const neolib::i_string& aPluginFolder, neolib::i_ref_ptr<neolib::i_plugin>& aPluginInstance)
#else
API void entry_point(neolib::i_application& aApplication, const neolib::i_string& aPluginFolder, neolib::i_ref_ptr<neolib::i_plugin>& aPluginInstance)
#endif
{
    neolib::ref_ptr<neolib::i_plugin> ref{ new neos::language::compiler_plugin<neos::compilers::python::compiler>{ 
        neolib::make_uuid("8C41F2A6-0D3B-4E79-B5C8-1F6A93E2D407"), "python", "Python code generation" } };
    aPluginInstance.reset(ref.ptr());
}
