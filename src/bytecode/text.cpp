/*
  text.cpp

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

#include <neos/neos.hpp>
#include <neolib/core/vecarray.hpp>
#include <neos/bytecode/text.hpp>

namespace neos
{
    namespace bytecode
    {
        text& operator<<(text& aText, opcode aOpcode)
        {
            auto const& encoding = opcode_dictionary().at(aOpcode);
            if (std::holds_alternative<op1>(encoding))
                aText.push_back(std::byte{ std::get<op1>(encoding).b1 });
            else if (std::holds_alternative<op2>(encoding))
            {
                aText.push_back(std::byte{ std::get<op2>(encoding).b1 });
                aText.push_back(std::byte{ std::get<op2>(encoding).b2 });
            }
            return aText;
        }

        text::const_iterator next_opcode(text::const_iterator aText, text::const_iterator aTextEnd, opcode& aOpcode)
        {
            if (aText == aTextEnd)
                throw exceptions::out_of_text();
            std::uint8_t b1 = static_cast<std::uint8_t>(*(aText++));
            auto existing = inverse_opcode_dictionary().find(op1{ b1 });
            if (existing == inverse_opcode_dictionary().end())
            {
                if (aText == aTextEnd)
                    throw exceptions::out_of_text();
                std::uint8_t b2 = static_cast<std::uint8_t>(*(aText++));
                existing = inverse_opcode_dictionary().find(op2{ b1, b2 });
            }
            if (existing != inverse_opcode_dictionary().end())
            {
                aOpcode = existing->second;
                return aText;
            }
            throw exceptions::invalid_instruction();
        }
    }
}