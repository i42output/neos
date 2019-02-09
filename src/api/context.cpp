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
		return iSchema != std::nullopt;
	}

	void context::load_schema(const std::string& aSchemaPath)
	{
		std::cout << "Loading schema '" + aSchemaPath + "'..." << std::endl;
		iSchema.emplace(aSchemaPath);
		iLanguage = iSchema->root().as<neolib::rjson_object>().at("meta").as<neolib::rjson_object>().at("language").as<neolib::rjson_string>();
        // todo
        auto loop = emit(iText, bytecode::opcode::ADD, bytecode::reg::R1, 1);
        emit(iText, bytecode::opcode::B, loop);
    }

	const std::string& context::language() const
	{
		return iLanguage;
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
        iThreads.push_back(std::make_unique<bytecode::vm::thread>(text()));
	}

    std::string context::metrics() const
    {
        std::string result;
        for (auto const& t : iThreads)
            result += t->metrics();
        return result;
    }
}