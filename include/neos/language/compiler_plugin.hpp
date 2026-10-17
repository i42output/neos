/*
  compiler_plugin.hpp

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

// A compiler plugin (".ncp"): a plugin whose object is a language compiler (i_language_compiler)

#include <neos/neos.hpp>
#include <boost/dll.hpp>
#include <neolib/core/pair.hpp>
#include <neolib/core/string.hpp>
#include <neolib/core/uuid.hpp>
#include <neolib/app/version.hpp>
#include <neolib/plugin/plugin.hpp>
#include <neos/language/i_language_compiler.hpp>

namespace neos::language
{
    template <typename LanguageCompiler>
    class compiler_plugin : public neolib::plugin<>
    {
    public:
        compiler_plugin(neolib::uuid const& aId, std::string const& aName, std::string const& aDescription = {}, 
            neolib::version const& aVersion = {}, std::string const& aCopyright = {}) :
            iId{ aId }, iName{ aName }, iDescription{ aDescription }, iVersion{ aVersion }, iCopyright{ aCopyright }
        {
        }
    public:
        bool discover(neolib::uuid const& aId, void*& aObject) override
        {
            if (aId == i_language_compiler::iid())
            {
                aObject = static_cast<i_language_compiler*>(new LanguageCompiler{});
                return true;
            }
            return false;
        }
    public:
        neolib::uuid const& id() const override { return iId; }
        neolib::i_string const& name() const override { return iName; }
        neolib::i_string const& description() const override { return iDescription; }
        neolib::i_version const& version() const override { return iVersion; }
        neolib::i_string const& copyright() const override { return iCopyright; }
    private:
        neolib::uuid iId;
        neolib::string iName;
        neolib::string iDescription;
        neolib::version iVersion;
        neolib::string iCopyright;
    };
}
