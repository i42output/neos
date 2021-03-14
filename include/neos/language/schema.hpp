/*
  schema.hpp

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
#include <boost/functional/hash.hpp>
#include <neolib/core/i_map.hpp>
#include <neolib/core/i_string.hpp>
#include <neolib/file/json.hpp>
#include <neos/language/i_concept_library.hpp>
#include <neos/language/schema_node_atom.hpp>
#include <neos/language/schema_terminal_atom.hpp>
#include <neos/language/concept_atom.hpp>

namespace neos
{
    namespace language
    {
        struct meta
        {
            std::string name;
            std::string description;
            std::string copyright;
            std::string version;
            std::vector<std::string> sourcecodeFileExtension;
            std::vector<std::string> sourcecodeModulePackageSpecificationFileExtension;
            std::vector<std::string> sourcecodeModulePackageImplementationFileExtension;
            std::size_t parserRecursionLimit = 256u;
        };

        enum class schema_keyword
        {
            Invalid,
            Meta,
            Libraries,
            Is,
            As,
            Done,
            Drain,
            Next,
            Continue,
            Recurse,
            Ignore,
            None,
            Error,
            Default,
            Expect,
            Tokens
        };

        class schema
        {
        public:
            typedef neolib::i_ref_ptr<i_atom> abstract_atom_ptr;
            typedef neolib::ref_ptr<i_atom> atom_ptr;
            typedef std::pair<neolib::rjson_string, std::string> atom_reference_key_t;
            struct atom_reference
            {
                std::string symbol;
                neolib::rjson_value const* node = nullptr;
                abstract_atom_ptr* atomPtr = nullptr;
            };
            typedef std::vector<atom_reference> unresolved_reference_list;
            typedef std::unordered_map<atom_reference_key_t, unresolved_reference_list, boost::hash<atom_reference_key_t>> atom_references_t;
        public:
            struct unresolved_references : std::runtime_error
            {
                unresolved_reference_list references;
                unresolved_references(unresolved_reference_list&& aReferences) :
                    std::runtime_error("neos::language::schema::unresolved_references"),
                    references{ std::move(aReferences) }
                {}
            };
        private:
            typedef std::function<void(neolib::rjson_value const&, i_schema_node_atom&)> atom_handler_t;
            typedef std::map<schema_keyword, atom_handler_t> atom_handlers_t;
            typedef std::unordered_map<const i_concept*, atom_ptr> concept_atoms_t;
        public:
            static constexpr std::size_t RecursionLimit = 64u;
        public:
            schema(std::string const& aPath, neolib::rjson const& aSource, const concept_libraries_t& aConceptLibraries);
        public:
            std::string const& path() const;
            i_schema_node_atom& root() const;
            const i_atom* eof() const;
            language::meta const& meta() const;
            neolib::ref_ptr<i_concept> find_concept(const std::string& aSymbol) const;
        private:
            static schema_keyword keyword(const neolib::rjson_string& aSymbol);
            void default_atom_handler(neolib::rjson_value const& aChildNode, i_schema_node_atom& aParentAtom);
            atom_handlers_t& atom_handlers();
            void parse(neolib::rjson_value const& aNode, i_schema_node_atom& aAtom);
            void parse_meta(neolib::rjson_value const& aNode);
            void parse_tokens(neolib::rjson_value const& aNode, i_schema_node_atom& aAtom);
            std::string fully_qualified_name(neolib::rjson_value const& aNode) const;
            std::string fully_qualified_name(neolib::rjson_value const& aNode, const neolib::rjson_string& aLeafName) const;
            const atom_references_t& atom_references() const;
            atom_references_t& atom_references();
            void add_lhs_atom_reference(neolib::rjson_value const& aNode, i_schema_node_atom& aParentAtom, abstract_atom_ptr& aAtom);
            void add_rhs_atom_reference(neolib::rjson_value const& aNode, i_schema_node_atom& aParentAtom, abstract_atom_ptr& aAtom);
            void resolve_references();
            void fixup_expected(i_atom& aAtom);
            std::string fully_qualified_name(const i_atom& aAtom) const;
            atom_ptr leaf(const std::string& aStem, const neolib::rjson_string& aLeafName);
            atom_ptr leaf(i_schema_node_atom& aNode, const std::string& aStem, const neolib::rjson_string& aLeafName);
            atom_ptr create_concept_atom(const neolib::i_ref_ptr<i_concept>& aConcept);
            void throw_error(neolib::rjson_value const& aNode, const std::string aErrorText);
        private:
            std::string iPath;
            neolib::rjson const& iSource;
            std::optional<atom_handler_t> iDefaultAtomHandler;
            std::optional<atom_handlers_t> iAtomHandlers;
            language::meta iMeta;
            const concept_libraries_t& iConceptLibraries;
            atom_ptr iRoot;
            atom_references_t iAtomReferences;
            concept_atoms_t iConceptAtoms;
            bool iParsingTokens;
            std::vector<i_schema_node_atom*> iContext;
            atom_ptr iEof;
        };
    }
}