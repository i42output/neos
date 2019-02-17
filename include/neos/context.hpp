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

namespace neos
{
    namespace bytecode
    {
        namespace vm
        {
            class thread;
        }
    }

	class context
	{
    public:
        typedef std::string translation_unit_t;
        typedef std::vector<translation_unit_t> program_t;
    public:
        struct no_program_loaded : std::runtime_error { no_program_loaded() : std::runtime_error("no program loaded") {} };
        struct compiler_error : std::runtime_error { compiler_error(const std::string& aReason) : std::runtime_error(aReason) {} };
	public:
		context();
        ~context();
	public:
		bool schema_loaded() const;
		void load_schema(const std::string& aSchemaPath);
		const std::string& language() const;
        void load_program(const std::string& aPath);
        void load_program(std::istream& aStream);
        void compile_program(const translation_unit_t& aProgram);
        const program_t& program() const;
        program_t& program();
        const text_t& text() const;
	public:
		bool running() const;
		void run();
        std::string metrics() const;
    private:
        translation_unit_t& load_unit(const std::string& aPath);
        translation_unit_t& load_unit(std::istream& aStream);
    private:
		std::optional<neolib::rjson> iSchema;
        std::string iLanguage;
        program_t iProgram;
        text_t iText;
        std::vector<std::unique_ptr<bytecode::vm::thread>> iThreads;
	};
}