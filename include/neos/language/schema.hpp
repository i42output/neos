/*
  schema.hpp

  Copyright (c) 2024 Leigh Johnston.  All Rights Reserved.

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
#include <chrono>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <boost/functional/hash.hpp>
#include <neolib/core/i_map.hpp>
#include <neolib/core/i_string.hpp>
#include <neolib/file/json.hpp>
#include <neolib/file/parser.hpp>
#include <neos/language/i_concept_library.hpp>

namespace neos::language
{
    struct meta
    {
        std::string language;
        std::string description;
        std::string copyright;
        std::string version;
        std::vector<std::string> sourcecodeFileExtension;
        std::vector<std::string> sourcecodeModulePackageSpecificationFileExtension;
        std::vector<std::string> sourcecodeModulePackageImplementationFileExtension;
        std::size_t parserRecursionLimit = 256u;
        std::vector<std::string> prelude; ///< packages implicitly imported by every (non-imported) source fragment
        bool bigintPromotion = false;   ///< (meta: bigint.promotion) the language's integers are of any size: a result that overflows 
                                        ///< is promoted to an arbitrary precision integer (e.g. Python's int); the compiler can override it
    };

    namespace code_parser
    {
        enum class symbol : std::uint32_t {};
    }
}

declare_symbols(neos::language::code_parser::symbol)
end_declare_symbols(neos::language::code_parser::symbol);

namespace neos::language
{
    namespace code_parser
    {
        enable_neolib_parser(symbol)
    }

    using parser = neolib::parser<code_parser::symbol>;

    struct schema_stage
    {
        std::string name;
        std::string_view grammar;
        std::optional<std::string> root;
        std::shared_ptr<std::unordered_set<std::string>> infix;
        std::shared_ptr<std::unordered_set<std::string>> discard;
        std::shared_ptr<std::unordered_map<std::string_view, code_parser::symbol>> symbolMap = {};
        std::shared_ptr<parser> parser = {};
    };

    using pipeline = std::vector<std::unique_ptr<schema_stage>>;

    class schema
    {
    public:
        static constexpr std::size_t RecursionLimit = 64u;
    public:
        schema(std::string const& aPath, const concept_libraries_t& aConceptLibraries, bool aShowParseTimes = false, 
            std::optional<std::string> const& aSource = {});   ///< (aSource: the schema's source (it is not read from aPath), e.g. one built in)
    public:
        static std::optional<std::string> find(std::string const& aSourceFileExtension); ///< (the path of the installed schema whose meta source.file.extension matches)
    public:
        std::string const& path() const;
        std::chrono::steady_clock::duration parse_time() const; ///< (the time parsing the schema's grammars took)
        language::meta const& meta() const;
        language::pipeline const& pipeline() const;
    private:
        void parse_meta(neolib::rjson_value const& aNode);
        void throw_error(neolib::rjson_value const& aNode, const std::string aErrorText) const;
    private:
        std::string iPath;
        std::string iSource;
        neolib::rjson iMetaSource;
        language::meta iMeta;
        language::pipeline iPipeline;
        const concept_libraries_t& iConceptLibraries;
        std::chrono::steady_clock::duration iParseTime = {};
    };
}