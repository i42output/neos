/*
  vm.hpp

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
#include <array>
#include <vector>
#include <optional>
#include <thread>
#include <string>
#include <neos/bytecode/bytecode.hpp>
#include <neos/bytecode/exceptions.hpp>
#include <neos/bytecode/opcodes.hpp>

namespace neos
{
    namespace bytecode
    {
        namespace vm
        {
            class thread : public std::thread
            {
            public:
                thread(text const& aText) :
                    std::thread{ [&](){ execute(aText); } }
                {
                }
            public:
                void execute(text const& aText)
                {
                    /// todo
                }
            public:
                data_type const& result() const
                {
                    return iResult;
                }
            public:
                std::string metrics() const
                {
                    /// todo
                    return {}; 
                }
            private:
                data_type iResult;
            };
        }
    }
}