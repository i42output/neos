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

#include <neolib/neolib.hpp>
#include <neolib/app/i_application.hpp>
#include <neolib/plugin/i_plugin.hpp>
#include <neos/language/concept_library_plugin.hpp>
#include <neos/context.hpp>
#include "javascript.hpp"

// (NEOS_PLUGIN_ENTRY_POINT: the name of the entry point of the plugin linked into an executable (e.g. a compiler stub))
#ifdef NEOS_PLUGIN_ENTRY_POINT
void NEOS_PLUGIN_ENTRY_POINT(neolib::i_application& aApplication, const neolib::i_string& aPluginFolder, neolib::i_ref_ptr<neolib::i_plugin>& aPluginInstance)
#else
API void entry_point(neolib::i_application& aApplication, const neolib::i_string& aPluginFolder, neolib::i_ref_ptr<neolib::i_plugin>& aPluginInstance)
#endif
{
    neolib::ref_ptr<neolib::i_plugin> ref{ new neos::language::concept_library_plugin<neos::concepts::javascript::javascript>{} };
    aPluginInstance.reset(ref.ptr());
}

