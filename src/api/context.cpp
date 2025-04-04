/*
  context.cpp

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

#include <neolib/neolib.hpp>

#include <iostream>
#include <filesystem>

#include <neolib/core/string_utf.hpp>
#include <neolib/app/application.hpp>
#include <neos/context.hpp>

namespace neos
{
    context::context(std::ostream& aCout) :
        iCout{ aCout },
        iPrivateApplication{ std::make_unique<neolib::application<>>(neolib::application_info{ "neos", "i42 software", {}, "Copyright (c) 2019 Leigh Johnston", {}, {}, {}, ".ncl" }) },
        iApplication{ *iPrivateApplication },
        iCompiler{ *this }
    {
        init();
    }

    context::context(std::ostream& aCout, neolib::i_service_provider& aServiceProvider) :
        iCout{ aCout },
        iPrivateApplication{ std::make_unique<neolib::application<>>(neolib::application_info{ "neos", "i42 software", {}, "Copyright (c) 2019 Leigh Johnston", {}, {}, {}, ".ncl" }, aServiceProvider) },
        iApplication{ *iPrivateApplication },
        iCompiler{ *this }
    {
        init();
    }

    context::context(std::ostream& aCout, neolib::i_application& aApplication) :
        iCout{ aCout },
        iApplication{ aApplication },
        iCompiler{ *this }
    {
        iApplication.plugin_manager().plugin_file_extensions().clear();
        iApplication.plugin_manager().plugin_file_extensions().push_back(neolib::string{ ".ncl" });
        init();
    }

    context::~context()
    {
    }

    std::ostream& context::cout()
    {
        return iCout;
    }

    const context::concept_libraries_t& context::concept_libraries() const
    {
        return iConceptLibraries;
    }

    void context::find_concept(neolib::i_string_view const& aSymbol, neolib::i_ref_ptr<language::i_semantic_concept>& aResult) const
    {
        for (auto const& cl : iConceptLibraries)
        {
            if (cl.second()->find_concept(aSymbol, aResult))
                return;
        }
    }

    bool context::schema_loaded() const
    {
        return iSchema != nullptr;
    }

    void context::load_schema(std::string const& aSchemaPath)
    {
        std::string const schemaPath = (aSchemaPath.empty() && iSchema ? schema().path() : aSchemaPath);
        if (schemaPath.empty())
            throw no_schema_path_specified();
        cout() << "Loading schema '" + schemaPath + "'..." << std::endl;
        iSchema.reset();
        iSchema = std::make_shared<language::schema>(schemaPath, concept_libraries());
        for (auto& unit : program().translationUnits)
            unit.schema = iSchema;
    }

    const language::schema& context::schema() const
    {
        if (!schema_loaded())
            throw no_schema_loaded();
        return *iSchema;
    }

    void context::load_program(std::string const& aPath)
    {
        std::string const path = (aPath.empty() && !iProgram.translationUnits.empty() ? *iProgram.translationUnits.front().fragments.front().source_file_path() : aPath);
        iProgram = decltype(iProgram){};
        auto& unit = load_unit(language::source_fragment{ neolib::string{ path } });
    }

    void context::load_program(std::istream& aStream)
    {
        auto& unit = load_unit(language::source_fragment{}, aStream);
    }

    language::compiler& context::compiler()
    {
        return iCompiler;
    }

    void context::compile_program()
    {
        compiler().compile(program());
    }

    const context::program_t& context::program() const
    {
        return iProgram;
    }

    context::program_t& context::program()
    {
        return iProgram;
    }

    const text_t& context::text() const
    {
        return program().text;
    }

    bool context::running() const
    {
        for (auto const& t : iThreads)
            if (t->joinable())
                return true;
        return false;
    }

    void context::run()
    {
        if (text().empty())
            throw no_text();
        iThreads.push_back(std::make_unique<bytecode::vm::thread>(text()));
    }

    bytecode::data_type context::evaluate(const std::string& aExpression)
    {
        program().translationUnits.clear();
        std::istringstream stream{ aExpression };
        load_program(stream);
        compile_program();
        run();
        iThreads.back()->join();
        auto const result = iThreads.back()->result();
        iThreads.pop_back();
        return result;
    }

    const neolib::i_string& context::metrics() const
    {
        thread_local neolib::string result;
        for (auto const& t : iThreads)
            result += neolib::string{ t->metrics() };
        return result;
    }

    void context::init()
    {
        iApplication.plugin_manager().load_plugins();
        for (neolib::ref_ptr<neolib::i_plugin> plugin : iApplication.plugin_manager().plugins())
        {
            neolib::ref_ptr<language::i_concept_library> library(*plugin);
            iConceptLibraries[library->name()] = library;
            auto add_sublibraries = [this](auto& self, auto& aParentLibrary) -> void
            {
                for (auto& sublibrary : aParentLibrary.sublibraries())
                {
                    iConceptLibraries[sublibrary.first()] = sublibrary.second();
                    self(self, *sublibrary.second());
                }
            };
            add_sublibraries(add_sublibraries, *library);
        }
    }

    context::translation_unit_t& context::load_unit(language::source_fragment&& aFragment)
    {
        if (!schema_loaded())
            throw compiler_error("no schema loaded");

        std::ifstream fragmentStream{ *aFragment.source_file_path(), std::ios::binary };
        if (!fragmentStream)
            throw compiler_error("failed to open source file '" + *aFragment.source_file_path() + "'");

        return load_unit(std::move(aFragment), fragmentStream);
    }

    context::translation_unit_t& context::load_unit(language::source_fragment&& aFragment, std::istream& aStream)
    {
        if (!schema_loaded())
            throw compiler_error("no schema loaded");

        auto& unit = *program().translationUnits.insert(program().translationUnits.end(), 
            translation_unit_t{ iSchema, { std::move(aFragment) }, language::ast{ program().symbolTable } });

        load_fragment(unit.fragments.back(), aStream);

        return unit;
    }

    void context::load_fragment(language::i_source_fragment& aFragment)
    {
        if (aFragment.source_file_path() == std::nullopt)
            throw invalid_fragment();

        std::optional<std::ifstream> fragmentStream;
        fragmentStream.emplace(*aFragment.source_file_path(), std::ios::binary);
        if (!*fragmentStream)
            for (auto const& ext : schema().meta().sourcecodeFileExtension)
            {
                std::string tryPath = "packages/" + schema().meta().language + "/" + *aFragment.source_file_path() + ext;
                fragmentStream.emplace(tryPath, std::ios::binary);
                if (*fragmentStream)
                {
                    *aFragment.source_file_path() = tryPath;
                    break;
                }
            }
        if (!*fragmentStream)
            throw compiler_error("failed to open source file '" + *aFragment.source_file_path() + "'");

        load_fragment(aFragment, *fragmentStream);
    }

    void context::load_fragment(language::i_source_fragment& aFragment, std::istream& aStream)
    {
        if (!aStream)
            throw compiler_error("input stream bad");

        typedef typename std::istream::pos_type pos_type;
        pos_type count = 0;
        aStream.seekg(0, std::ios::end);
        if (aStream)
        {
            count = aStream.tellg();
            if (count == static_cast<pos_type>(-1))
                count = 0;
            aStream.seekg(0, std::ios::beg);
        }
        else
            aStream.clear();

        if (count != std::istream::pos_type(0))
        {
            thread_local neolib::string source;
            source.clear();
            source.reserve(static_cast<language::source_t::size_type>(count) + 1);
            source.resize(static_cast<language::source_t::size_type>(count));
            aStream.read(&source[0], count);
            source.resize(static_cast<language::source_t::size_type>(aStream.gcount()));
            aFragment.set_source(source.to_string_view());
        }
        else
        {
            char buffer[1024];
            thread_local neolib::string source;
            source.clear();
            while (aStream.read(buffer, sizeof(buffer)))
                source.append(buffer, static_cast<language::source_t::size_type>(aStream.gcount()));
            if (aStream.eof())
                source.append(buffer, static_cast<language::source_t::size_type>(aStream.gcount()));
            aFragment.set_source(source.to_string_view());
        }

        if (aFragment.source().empty())
            throw compiler_error("no source code");

        if (!neolib::check_utf8(aFragment.source()))
            throw compiler_error("source file has invalid utf-8");
    }
}