/*
  context.hpp

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
#include <map>
#include <string>
#include <iostream>
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
        context(std::ostream& aCout) : 
            context{ std::cin, aCout, std::cerr } {}
        context(std::ostream& aCout, neolib::i_service_provider& aServiceProvider) : 
            context{ std::cin, aCout, std::cerr, aServiceProvider } {}
        context(std::ostream& aCout, neolib::i_application& aApplication) : 
            context{ std::cin, aCout, std::cerr, aApplication } {}
        // (the standard streams: aCin, aCout and aCerr (see i_context::cin))
        context(std::istream& aCin, std::ostream& aCout, std::ostream& aCerr);
        context(std::istream& aCin, std::ostream& aCout, std::ostream& aCerr, neolib::i_service_provider& aServiceProvider);
        context(std::istream& aCin, std::ostream& aCout, std::ostream& aCerr, neolib::i_application& aApplication);
        ~context();
    public:
        std::istream& cin() final;
        std::ostream& cout() final;
        std::ostream& cerr() final;
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
        std::istream& iCin;
        std::ostream& iCout;
        std::ostream& iCerr;
        std::unique_ptr<neolib::i_application> iPrivateApplication;
        neolib::i_application& iApplication;
        concept_libraries_t iConceptLibraries;
        std::shared_ptr<language::schema> iSchema;
        language::compiler iCompiler;
        program_t iProgram;
        std::vector<std::unique_ptr<bytecode::vm::thread>> iThreads;
        std::vector<neolib::ref_ptr<neolib::i_plugin>> iAddedPlugins;
        std::map<std::string, std::string> iPackages;
        std::vector<std::string> iArguments;
        bool iRestricted = false;
    };
}