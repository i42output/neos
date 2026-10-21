/*
  bytecode.hpp

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

#pragma once

#include <neos/neos.hpp>
#include <cstdint>
#include <string_view>
#include <neolib/core/variant.hpp>
#include <neolib/core/vector.hpp>
#include <neolib/core/optional.hpp>
#include <neonumeric/integer.hpp>
#include <neonumeric/real.hpp>
#include <neos/language/symbol_table.hpp>

namespace neos
{
    namespace bytecode
    {
        // a string in linear memory: its length in bytes (less than 2^31), in UTF-16 code units and in code points (all u32), 
        // its characters (UTF-8) and a NUL (so that runtime plugins can use it in place)
        struct string_header
        {
            std::uint32_t bytes;
            std::uint32_t utf16Units;
            std::uint32_t codePoints;
        };
        inline constexpr std::uint32_t StringHeaderSize = static_cast<std::uint32_t>(sizeof(string_header));

        // a line marker: "i64.const LineMarker | line; drop", before the code of a statement on that line of its source; it has no
        // effect (the VM removes it when it decodes a function, recording the line of the code that follows it)
        inline constexpr std::uint64_t LineMarker = 0x4C494E45ull << 32u; // ("LINE")
        inline constexpr std::uint64_t LineMarkerMask = 0xFFFFFFFFull << 32u;
        inline constexpr bool is_line_marker(std::uint64_t aValue) { return (aValue & LineMarkerMask) == LineMarker; }

        // a position marker: "i64.const PositionMarker | position; drop", immediately before a call: the span of the source of the 
        // expression that the call evaluates (see position); it has no effect (the VM removes it when it decodes a function, recording 
        // the position of the call that follows it)
        inline constexpr std::uint64_t PositionMarker = 0x5053ull << 48u; // ("PS")
        inline constexpr std::uint64_t PositionMarkerMask = 0xFFFFull << 48u;
        inline constexpr bool is_position_marker(std::uint64_t aValue) { return (aValue & PositionMarkerMask) == PositionMarker; }
        // (a position: its line (1-based: 20 bits), the number of lines after it that it ends on (8 bits) and its columns (0-based, 
        // in bytes: 10 bits each; its end column is exclusive); none (0) if it does not fit)
        inline constexpr std::uint64_t position(std::uint32_t aLine, std::uint32_t aEndLine, std::uint32_t aColumn, std::uint32_t aEndColumn)
        {
            if (aLine == 0u || aLine >= (1u << 20u) || aEndLine < aLine || aEndLine - aLine >= (1u << 8u) || aColumn >= (1u << 10u) || aEndColumn >= (1u << 10u))
                return 0u;
            return (static_cast<std::uint64_t>(aLine) << 28u) | (static_cast<std::uint64_t>(aEndLine - aLine) << 20u) | (static_cast<std::uint64_t>(aColumn) << 10u) | aEndColumn;
        }
        inline constexpr std::uint32_t position_line(std::uint64_t aPosition) { return static_cast<std::uint32_t>((aPosition >> 28u) & 0xFFFFFu); }
        inline constexpr std::uint32_t position_end_line(std::uint64_t aPosition) { return position_line(aPosition) + static_cast<std::uint32_t>((aPosition >> 20u) & 0xFFu); }
        inline constexpr std::uint32_t position_column(std::uint64_t aPosition) { return static_cast<std::uint32_t>((aPosition >> 10u) & 0x3FFu); }
        inline constexpr std::uint32_t position_end_column(std::uint64_t aPosition) { return static_cast<std::uint32_t>(aPosition & 0x3FFu); }

        // the custom section that names a module's frames (see neos_rt_host::frames): its sources' names (a count, then each); 
        // then, for each function that the module defines (a count, then each, in order): its name (empty: it has no frame), its 
        // source (an index) and its first line
        inline constexpr char FramesSection[] = "neos.frames";

        inline string_header string_header_of(std::string_view aText)
        {
            // (a four byte sequence is a surrogate pair in UTF-16)
            string_header header{ static_cast<std::uint32_t>(aText.size()), 0u, 0u };
            for (auto c : aText)
            {
                auto const b = static_cast<unsigned char>(c);
                if ((b & 0xC0u) != 0x80u)
                {
                    ++header.codePoints;
                    header.utf16Units += (b >= 0xF0u) ? 2u : 1u;
                }
            }
            return header;
        }
    }
}