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
#include <neolib/application.hpp>
#include <neolib/string_utf.hpp>
#include <neos/context.hpp>

namespace neos
{
    context::context() : 
        iPrivateApplication{ std::make_unique<neolib::application>(neolib::application_info{ {}, "neos", "i42 software", {}, "Copyright (c) 2019 Leigh Johnston", {}, {}, {}, ".ncl" }) },
        iApplication{ *iPrivateApplication }
    {
        init();
    }

    context::context(neolib::i_application& aApplication) :
        iApplication{ aApplication }
    {
        iApplication.plugin_manager().plugin_file_extensions().clear();
        iApplication.plugin_manager().plugin_file_extensions().push_back(neolib::string{ ".ncl" });
        init();
    }

    context::~context()
    {
    }

    const language::concept_libraries_t& context::concept_libraries() const
    {
        return iConceptLibraries;
    }

    bool context::schema_loaded() const
    {
        return iSchemaSource != std::nullopt && iSchema != nullptr;
    }

    void context::load_schema(const std::string& aSchemaPath)
    {
        std::cout << "Loading schema '" + aSchemaPath + "'..." << std::endl;
        iSchemaSource.reset();
        iSchema.reset();
        if (boost::filesystem::exists(aSchemaPath))
            iSchemaSource.emplace(aSchemaPath);
        else if (boost::filesystem::exists(aSchemaPath + ".neos"))
            iSchemaSource.emplace(aSchemaPath + ".neos");
        iSchema = std::make_shared<language::schema>(*iSchemaSource, concept_libraries());
    }

    const neolib::rjson& context::schema_source() const
    {
        if (iSchemaSource == std::nullopt)
            throw no_schema_source();
        return *iSchemaSource;
    }

    const language::schema& context::schema() const
    {
        if (!schema_loaded())
            throw no_schema_loaded();
        return *iSchema;
    }

    void context::load_program(const std::string& aPath)
    {
        iProgram = decltype(iProgram){};
        auto& unit = load_unit(language::source_fragment{ aPath });
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

    bytecode::reg_64 context::evaluate(const std::string& aExpression)
    {
        program().translationUnits.clear();
        std::istringstream stream{ aExpression };
        load_program(stream);
        run();
        iThreads.back()->join();
        auto result = iThreads.back()->result();
        iThreads.pop_back();
        return result;
    }

    std::string context::metrics() const
    {
        std::string result;
        for (auto const& t : iThreads)
            result += t->metrics();
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

        if (aFragment.filePath == std::nullopt)
            throw invalid_fragment();

        std::ifstream fragmentStream{ *aFragment.filePath, std::ios::binary };
        if (!fragmentStream)
            throw compiler_error("failed to open source file '" + *aFragment.filePath + "'");

        return load_unit(std::move(aFragment), fragmentStream);
    }

    context::translation_unit_t& context::load_unit(language::source_fragment&& aFragment, std::istream& aStream)
    {
        if (!schema_loaded())
            throw compiler_error("no schema loaded");

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

        auto& unit = *program().translationUnits.insert(program().translationUnits.end(), 
            translation_unit_t{ iSchema, { std::move(aFragment) }, language::ast{ program().symbolTable } });

        auto& fragment = unit.fragments.back();

        if (count != std::istream::pos_type(0))
        {
            fragment.source.reserve(static_cast<language::source_t::size_type>(count) + 1);
            fragment.source.resize(static_cast<language::source_t::size_type>(count));
            aStream.read(&fragment.source[0], count);
            fragment.source.resize(static_cast<language::source_t::size_type>(aStream.gcount()));
        }
        else
        {
            char buffer[1024];
            while (aStream.read(buffer, sizeof(buffer)))
                fragment.source.append(buffer, static_cast<language::source_t::size_type>(aStream.gcount()));
            if (aStream.eof())
                fragment.source.append(buffer, static_cast<language::source_t::size_type>(aStream.gcount()));
        }

        if (fragment.source.empty())
            throw compiler_error("no source code");

        if (!neolib::check_utf8(fragment.source))
            throw compiler_error("source file has invalid utf-8");

        return unit;
    }
}