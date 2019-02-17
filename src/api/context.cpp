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
#include <neolib/string_utf.hpp>
#include <neos/context.hpp>
#include "../bytecode/vm.hpp"
#include "../bytecode/opcodes.hpp"
#include "../bytecode/text.hpp"

namespace neos
{
	context::context()
	{
	}

    context::~context()
    {
    }

	bool context::schema_loaded() const
	{
		return iSchema != std::nullopt && iLanguage != std::nullopt;
	}

	void context::load_schema(const std::string& aSchemaPath)
	{
		std::cout << "Loading schema '" + aSchemaPath + "'..." << std::endl;
        iSchema.reset();
        iLanguage.reset();
		iSchema.emplace(aSchemaPath);
        iLanguage.emplace(*iSchema);
    }

	const neos::language& context::language() const
	{
        if (!schema_loaded())
            throw compiler_error("no schema loaded");
        return *iLanguage;
	}

    void context::load_program(const std::string& aPath)
    {
        auto& unit = load_unit(aPath);
        compile_program(unit);
    }

    void context::load_program(std::istream& aStream)
    {
        auto& unit = load_unit(aStream);
        compile_program(unit);
    }

    void context::compile_program(const translation_unit_t& aProgram)
    {
        // todo
        auto loop = emit(iText, bytecode::opcode::ADD, bytecode::reg::R1, bytecode::u64{ 10 });
        emit(iText, bytecode::opcode::ADD, bytecode::reg::R1, bytecode::i8{ -1 });
        emit(iText, bytecode::opcode::B, loop);
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
        return iText;
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

        auto& unit = *program().emplace(program().end());

        if (count != std::istream::pos_type(0))
        {
            unit.reserve(static_cast<translation_unit_t::size_type>(count) + 1);
            unit.resize(static_cast<translation_unit_t::size_type>(count));
            aStream.read(&unit[0], count);
            unit.resize(static_cast<translation_unit_t::size_type>(aStream.gcount()));
        }
        else
        {
            char buffer[1024];
            while (aStream.read(buffer, sizeof(buffer)))
                unit.append(buffer, static_cast<translation_unit_t::size_type>(aStream.gcount()));
            if (aStream.eof())
                unit.append(buffer, static_cast<translation_unit_t::size_type>(aStream.gcount()));
        }

        if (unit.empty())
            throw compiler_error("no source code");

        if (!neolib::check_utf8(unit))
            throw compiler_error("source file has invalid utf-8");

        return unit;
    }
}