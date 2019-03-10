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
#include "../bytecode/vm.hpp"

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

    const context::concept_libraries_t& context::concept_libraries() const
    {
        return iConceptLibraries;
    }

    bool context::schema_loaded() const
    {
        return iSchemaSource != std::nullopt && iSchema != std::nullopt;
    }

    void context::load_schema(const std::string& aSchemaPath)
    {
        std::cout << "Loading schema '" + aSchemaPath + "'..." << std::endl;
        iSchemaSource.reset();
        iSchema.reset();
        iSchemaSource.emplace(aSchemaPath);
        iSchema.emplace(*iSchemaSource);
    }

    const language::schema& context::schema() const
    {
        if (!schema_loaded())
            throw compiler_error("no schema loaded");
        return *iSchema;
    }

    void context::load_program(const std::string& aPath)
    {
        auto& unit = load_unit(aPath);
        compile_program();
    }

    void context::load_program(std::istream& aStream)
    {
        auto& unit = load_unit(aStream);
        compile_program();
    }

    void context::compile_program()
    {
        language::compiler{}.compile(program());
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
            throw no_program_loaded();
        iThreads.push_back(std::make_unique<bytecode::vm::thread>(text()));
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
            iConceptLibraries.push_back(neolib::ref_ptr<i_concept_library>(*plugin));
        }
    }

    context::translation_unit_t& context::load_unit(const std::string& aPath)
    {
        if (!schema_loaded())
            throw compiler_error("no schema loaded");

        std::ifstream unit{ aPath, std::ios::binary };
        if (!unit)
            throw compiler_error("failed to open source file '" + aPath + "'");

        return load_unit(unit);
    }

    context::translation_unit_t& context::load_unit(std::istream& aStream)
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

        auto& unit = *program().translationUnits.insert(program().translationUnits.end(), translation_unit_t{ translation_unit_t::source_t{}, language::ast{ program().symbolTable } });

        if (count != std::istream::pos_type(0))
        {
            unit.source.reserve(static_cast<translation_unit_t::source_t::size_type>(count) + 1);
            unit.source.resize(static_cast<translation_unit_t::source_t::size_type>(count));
            aStream.read(&unit.source[0], count);
            unit.source.resize(static_cast<translation_unit_t::source_t::size_type>(aStream.gcount()));
        }
        else
        {
            char buffer[1024];
            while (aStream.read(buffer, sizeof(buffer)))
                unit.source.append(buffer, static_cast<translation_unit_t::source_t::size_type>(aStream.gcount()));
            if (aStream.eof())
                unit.source.append(buffer, static_cast<translation_unit_t::source_t::size_type>(aStream.gcount()));
        }

        if (unit.source.empty())
            throw compiler_error("no source code");

        if (!neolib::check_utf8(unit.source))
            throw compiler_error("source file has invalid utf-8");

        return unit;
    }
}