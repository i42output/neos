/*
  format.hpp

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

// The C language's runtime: the printf and scanf families' formatting and scanning (lib_format.cpp)

#pragma once

#include <functional>
#include <string>
#include "core.hpp"

namespace neos::c_runtime
{
    // a function's variable arguments (an area of 8-byte slots: see the C compiler's calling convention) or a va_list (its address)
    struct arguments
    {
        address next = 0u;
        std::int64_t integer() { auto const v = *at<std::int64_t>(next); next += 8u; return v; }
        double floating() { double v; std::memcpy(&v, at<std::uint8_t>(next), 8u); next += 8u; return v; }
        address pointer() { return static_cast<address>(integer()); }
    };

    // printf's formatting: the output (wide: the format's and %ls's characters are wchar_t (UTF-8 output))
    std::string format(char const* aFormat, arguments& aArguments);

    // scanf's scanning: the source's characters (get: EOF at its end; unget: one back); the number of items assigned (EOF: input
    // failure before the first conversion)
    struct character_source
    {
        std::function<int()> get;
        std::function<void(int)> unget;
        std::uint64_t count = 0u;
    };
    int scan(character_source& aSource, char const* aFormat, arguments& aArguments);

    // UTF-8
    void append_utf8(std::string& aOut, std::uint32_t aCode);
    int decode_utf8(char const* aText, std::size_t aLength, std::uint32_t& aCode);    ///< (bytes taken; 0: none; -1: invalid, -2: incomplete)
}
