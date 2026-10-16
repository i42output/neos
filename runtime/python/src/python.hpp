/*
  python.hpp

  Copyright (c) 2026 Leigh Johnston.  All Rights Reserved.

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

// (the Python runtime plugin's parts)

#pragma once

#include <string>
#include <vector>
#include <neos/runtime/plugin.h>

namespace neos::python_runtime
{
    // repr() of a float: the shortest round-tripping digits; scientific notation if the exponent is < -4 or >= 16
    std::string python_float(double aValue);
    // raise a Python exception (it stops the script: the plugin's error is "type: message")
    void raise(std::string const& aType, std::string const& aMessage);
    bool failed();
    // (an exception that has been handled)
    void clear_error();
    char const* error_text();   // (the exception raised: "type: message")
    // a string result (valid until the next call on this thread)
    char const* result(std::string aValue);
    // the functions of the object protocol (objects.cpp)
    std::vector<neos_rt_function> const& object_functions();
}
