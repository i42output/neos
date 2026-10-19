/*
  i_context.hpp

  Copyright (c) 2025 Leigh Johnston.  All Rights Reserved.

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
#include <neolib/core/map.hpp>
#include <neolib/core/string_view.hpp>
#include <neolib/core/reference_counted.hpp>
#include <neos/language/i_concept_library.hpp>
#include <neos/language/i_semantic_concept.hpp>
#include <neos/language/i_compiler.hpp>
#include <neos/language/type.hpp>

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
        struct no_schema_path_specified : warning { no_schema_path_specified() : warning("neos::i_context::no_schema_path_specified") {} };
        struct no_schema_loaded : error { no_schema_loaded() : error("no schema loaded") {} };
        struct no_text : warning { no_text() : warning("no text") {} };
        struct compiler_error : error { compiler_error(std::string const& aReason) : error(aReason) {} };
    public:
        virtual std::ostream& cout() = 0;
    public:
        virtual const concept_libraries_t& concept_libraries() const = 0;
        virtual void find_concept(neolib::i_string_view const& aSymbol, neolib::i_ref_ptr<language::i_semantic_concept>& aResult) const = 0;
    public:
        virtual language::i_compiler& compiler() = 0;
    public:
        virtual bool running() const = 0;
        virtual void run() = 0;
        virtual int exit_status() const = 0;    ///< (of the program last run: failure if it stopped with an error, else the status it exited with (0 if none))
        virtual language::data_type evaluate(std::string const& aExpression) = 0;
        virtual const neolib::i_string& metrics() const = 0;
    public:
        virtual void load_fragment(language::i_source_fragment& aFragment) = 0;

        // helpers
    public:
        neolib::ref_ptr<language::i_semantic_concept> find_concept(std::string_view const& aSymbol) const
        {
            neolib::ref_ptr<language::i_semantic_concept> result;
            find_concept(neolib::string_view{ aSymbol }, result);
            return result;
        }
    };
}