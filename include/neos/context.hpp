/*
  context.hpp

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
#include <string>
#include <optional>
#include <vector>
#include <memory>
#include <neolib/json.hpp>
#include <neolib/i_application.hpp>
#include <neos/language/schema.hpp>
#include <neos/language/compiler.hpp>
#include <neos/i_context.hpp>

namespace neos
{
    class context : public i_context
    {
    public:
        typedef neolib::map<neolib::i_string, neolib::i_ref_ptr<language::i_concept_library>, neolib::string, neolib::ref_ptr<language::i_concept_library>> concept_libraries_t;
        typedef language::translation_unit translation_unit_t;
        typedef language::program program_t;
    public:
        context();
        context(neolib::i_application& aApplication);
        ~context();
    public:
        const concept_libraries_t& concept_libraries() const override;
    public:
        bool schema_loaded() const;
        void load_schema(const std::string& aSchemaPath);
        const neolib::rjson& schema_source() const;
        const language::schema& schema() const;
        void load_program(const std::string& aPath);
        void load_program(std::istream& aStream);
        language::compiler& compiler() override;
        void compile_program();
        const program_t& program() const;
        program_t& program();
        const text_t& text() const;
    public:
        bool running() const override;
        void run() override;
        bytecode::reg_64 evaluate(const std::string& aExpression) override;
        const neolib::i_string& metrics() const override;
    private:
        void init();
        translation_unit_t& load_unit(language::source_fragment&& aFragment);
        translation_unit_t& load_unit(language::source_fragment&& aFragment, std::istream& aStream);
        void load_fragment(language::i_source_fragment& aFragment) override;
        void load_fragment(language::i_source_fragment& aFragment, std::istream& aStream);
    private:
        std::unique_ptr<neolib::i_application> iPrivateApplication;
        neolib::i_application& iApplication;
        concept_libraries_t iConceptLibraries;
        std::optional<neolib::rjson> iSchemaSource;
        std::shared_ptr<language::schema> iSchema;
        language::compiler iCompiler;
        program_t iProgram;
        std::vector<std::unique_ptr<bytecode::vm::thread>> iThreads;
    };
}