/*
  compiler_plugin.hpp

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
