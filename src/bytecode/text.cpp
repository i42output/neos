/*
  text.cpp

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