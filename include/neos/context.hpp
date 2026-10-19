/*
  context.hpp

  Copyright (c) 2025 Leigh Johnston.  All Rights Reserved.

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
#include <map>
#include <string>
#include <optional>
#include <vector>
#include <memory>
#include <neolib/file/json.hpp>
#include <neolib/app/i_application.hpp>
#include <neolib/plugin/i_plugin.hpp>
#include <neos/language/compiler.hpp>
#include <neos/bytecode/vm/vm.hpp>
#include <neos/i_context.hpp>

namespace neos
{
    class context : public i_context
    {
    public:
        typedef neolib::map<neolib::string, neolib::ref_ptr<language::i_concept_library>> concept_libraries_t;
        typedef language::translation_unit translation_unit_t;
        typedef language::program program_t;
    public:
        context(std::ostream& aCout);
        context(std::ostream& aCout, neolib::i_service_provider& aServiceProvider);
        context(std::ostream& aCout, neolib::i_application& aApplication);
        ~context();
    public:
        std::ostream& cout() final;
    public:
        const concept_libraries_t& concept_libraries() const final;
        void find_concept(neolib::i_string_view const& aSymbol, neolib::i_ref_ptr<language::i_semantic_concept>& aResult) const final;
    public:
        bool schema_loaded() const;
        void load_schema(const std::string& aSchemaPath);
        void load_schema(const std::string& aSchemaPath, const std::string& aSchemaSource);    ///< (a schema that is not a file (e.g. one built in))
        void add_plugin(neolib::i_plugin& aPlugin);     ///< (a concept library or compiler plugin that is not a file (e.g. one linked in))
        void add_package(const std::string& aPath, const std::string& aSource); ///< (a package's source that is not a file (aPath: where the 
                                                        ///< schema's packages would be, e.g. "<schema folder>/packages/python/builtins.py"))
        const language::schema& schema() const;
        void load_program(std::string const& aPath);
        void load_program(std::istream& aStream);
        language::compiler& compiler() final;
        void compile_program();
        const program_t& program() const;
        program_t& program();
        const text& text() const;
    public:
        // restricted mode (off by default): programs cannot call runtime plugin functions that can change the host 
        // environment (e.g. file I/O)
        bool restricted() const;
        void set_restricted(bool aRestricted);
        // the program's command line arguments (the program (its path, as it was given) first, then the arguments that follow it)
        void set_arguments(std::vector<std::string> const& aArguments);
    public:
        bool running() const final;
        void run() final;
        int exit_status() const final;
        language::data_type evaluate(std::string const& aExpression) final;
        const neolib::i_string& metrics() const final;
    private:
        void init();
        translation_unit_t& load_unit(language::source_fragment&& aFragment);
        translation_unit_t& load_unit(language::source_fragment&& aFragment, std::istream& aStream);
        void load_fragment(language::i_source_fragment& aFragment) final;
        void load_fragment(language::i_source_fragment& aFragment, std::istream& aStream);
    private:
        std::ostream& iCout;
        std::unique_ptr<neolib::i_application> iPrivateApplication;
        neolib::i_application& iApplication;
        concept_libraries_t iConceptLibraries;
        std::shared_ptr<language::schema> iSchema;
        language::compiler iCompiler;
        program_t iProgram;
        std::vector<std::unique_ptr<bytecode::vm::thread>> iThreads;
        std::vector<neolib::ref_ptr<neolib::i_plugin>> iAddedPlugins;
        std::map<std::string, std::string> iPackages;
        bool iRestricted = false;
    };
}