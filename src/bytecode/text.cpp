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
            auto const& encoding = opcode_dictionary().at(aOpcode).encoding;
            if (std::holds_alternative<op1>(encoding))
                aText.push_back(std::byte{ std::get<op1>(encoding).b });
            else if (std::holds_alternative<op2>(encoding))
            {
                aText.push_back(std::byte{ std::get<op2>(encoding).p });
                std::visit([&](auto const& e)
                    {
                        for (auto const& b : e)
                            aText.push_back(std::byte{ b });
                    }, std::get<op2>(encoding).e);
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
                leb128_1 rest1 = { static_cast<std::uint8_t>(*(aText++)) };
                existing = inverse_opcode_dictionary().find(op2{ b1, rest1 });
                if (existing == inverse_opcode_dictionary().end())
                {
                    if (aText == aTextEnd)
                        throw exceptions::out_of_text();
                    leb128_2 rest2 = { rest1[0], static_cast<std::uint8_t>(*(aText++)) };
                    existing = inverse_opcode_dictionary().find(op2{ b1, rest2 });
                    if (existing == inverse_opcode_dictionary().end())
                    {
                        if (aText == aTextEnd)
                            throw exceptions::out_of_text();
                        leb128_3 rest3 = { rest2[0], rest2[1], static_cast<std::uint8_t>(*(aText++)) };
                        existing = inverse_opcode_dictionary().find(op2{ b1, rest3 });
                        if (existing == inverse_opcode_dictionary().end())
                        {
                            if (aText == aTextEnd)
                                throw exceptions::out_of_text();
                            leb128_4 rest4 = { rest3[0], rest3[1], rest3[2], static_cast<std::uint8_t>(*(aText++)) };
                            existing = inverse_opcode_dictionary().find(op2{ b1, rest4 });
                            if (existing == inverse_opcode_dictionary().end())
                            {
                                if (aText == aTextEnd)
                                    throw exceptions::out_of_text();
                                leb128_5 rest5 = { rest4[0], rest4[1], rest4[2], rest4[3], static_cast<std::uint8_t>(*(aText++)) };
                                existing = inverse_opcode_dictionary().find(op2{ b1, rest5 });
                            }
                        }
                    }
                }
            }
            if (existing != inverse_opcode_dictionary().end())
            {
                aOpcode = existing->second.opcode;
                return aText;
            }
            throw exceptions::invalid_instruction();
        }
    }
}