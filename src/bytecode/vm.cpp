/*
  vm.cpp

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

#include "vm.hpp"

namespace neos
{
    namespace bytecode
    {
        namespace vm
        {
            thread::thread(const text& aText) : 
                iText{ aText },
                iResult{}
            {
                iNativeThread.emplace([this]()
                {
                    iResult = execute();
                });
            }

            bool thread::joinable() const
            {
                return iNativeThread->joinable();
            }

            void thread::join()
            {
                iNativeThread->join();
            }

            void thread::terminate()
            {
                // todo
            }

            i64 thread::result() const
            {
                return iResult;
            }

            i64 thread::execute()
            {
                // todo
                return r<i64, reg::R1>();
            }
        }
   }
}