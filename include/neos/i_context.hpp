/*
  i_context.hpp

  Copyright (c) 2019 Leigh Johnston.  All Rights Reserved.

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
#include <neolib/core/map.hpp>
#include <neolib/core/string.hpp>
#include <neos/language/i_concept_library.hpp>
#include <neos/language/i_compiler.hpp>
#include <neos/bytecode/registers.hpp>
#include <neos/bytecode/vm/vm.hpp>

namespace neos
{
    class i_context
    {
    public:
        typedef neolib::i_map<neolib::i_string, neolib::i_ref_ptr<language::i_concept_library>> concept_libraries_t;
    public:
        struct warning : std::runtime_error { using runtime_error::runtime_error; };
        struct error : std::runtime_error { using runtime_error::runtime_error; };
        struct invalid_fragment : std::logic_error { invalid_fragment() : std::logic_error("neos::i_context::invalid_fragment") {} };
        struct no_schema_source : std::logic_error { no_schema_source() : std::logic_error("neos::i_context::no_schema_source") {} };
        struct no_schema_loaded : error { no_schema_loaded() : error("no schema loaded") {} };
        struct no_text : warning { no_text() : warning("no text") {} };
        struct compiler_error : error { compiler_error(const std::string& aReason) : error(aReason) {} };
    public:
        virtual const concept_libraries_t& concept_libraries() const = 0;
    public:
        virtual language::i_compiler& compiler() = 0;
    public:
        virtual bool running() const = 0;
        virtual void run() = 0;
        virtual bytecode::reg_64 evaluate(const std::string& aExpression) = 0;
        virtual const neolib::i_string& metrics() const = 0;
    public:
        virtual void load_fragment(language::i_source_fragment& aFragment) = 0;
    };
}