/*
  python.hpp

  Copyright (c) 2026 Leigh Johnston.  All Rights Reserved.

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

// (the Python runtime plugin's parts)

#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <neos/runtime/plugin.h>

namespace neos::python_runtime
{
    // repr() of a float: the shortest round-tripping digits; scientific notation if the exponent is < -4 or >= 16
    std::string python_float(double aValue);
    // raise a Python exception (it stops the script: the plugin's error is "type: message")
    void raise(std::string const& aType, std::string const& aMessage);
    bool failed();
    // (a trap: the program stops with the runtime error aMessage when the plugin function the calling thread is in returns (e.g. a function
    // that can change the host environment, called in restricted mode))
    void trap(std::string const& aMessage);
    // (an exception that has been handled)
    void clear_error();
    char const* error_text();   // (the exception raised: "type: message")
    // a string result (valid until the next call on this thread)
    char const* result(std::string aValue);
    // (as result(): a copy of aValue (into the result's storage: no allocation if it fits))
    char const* result_view(std::string_view aValue);
    // the host (for calls of the program's functions)
    neos_rt_host const*& host();
    // the functions of the object protocol (objects.cpp)
    std::vector<neos_rt_function> const& object_functions();
}
