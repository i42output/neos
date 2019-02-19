/*
  compiler.cpp

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
#include <neos/language/compiler.hpp>
#include "bytecode/opcodes.hpp"
#include "bytecode/text.hpp"

namespace neos
{
    namespace language
    {
        compiler::compiler()
        {
        }

        void compiler::compile(program& aProgram)
        {
            // todo
            auto loop = emit(aProgram.text, bytecode::opcode::ADD, bytecode::reg::R1, bytecode::u64{ 10 });
            emit(aProgram.text, bytecode::opcode::ADD, bytecode::reg::R1, bytecode::i8{ -1 });
            emit(aProgram.text, bytecode::opcode::B, loop);
        }
    }
}