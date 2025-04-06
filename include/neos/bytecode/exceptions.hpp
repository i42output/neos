/*
  exceptions.hpp

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

#pragma once

#include <neos/neos.hpp>

namespace neos
{
    namespace bytecode
    {
        namespace exceptions
        {
            struct no_text : std::runtime_error { no_text() : std::runtime_error("neos::bytecode: no text") {} };
            struct out_of_text : std::runtime_error { out_of_text() : std::runtime_error("neos::bytecode: out of text") {} };
            struct invalid_instruction : std::runtime_error { invalid_instruction() : std::runtime_error("neos::bytecode: invalid instruction") {} };
            struct logic_error : std::logic_error 
            { 
                logic_error() : std::logic_error("neos::bytecode: logic error") {} 
                logic_error(std::string const& aReason) : std::logic_error("neos::bytecode: logic error (" + aReason + ")") {}
            };
        }
    }
}