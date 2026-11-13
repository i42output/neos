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
#include <optional>
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

        // the custom section that describes the variables of a module's functions to the debugger (see neos/debug.hpp), in a module 
        // compiled to be debugged (see i_code_generator::debug_info): its entries (a count, then each): a function's index (that of 
        // the module's function index space; NoFunction: the module's globals), then its variables (a count, then each): its name, its 
        // flags (VariableParameter; VariableScope: its parts are names that the function's code can see (e.g. Lua's _ENV, a module's 
        // globals): the debugger looks a name up in them after the function's variables), where it is (VariableLocal: the function's local (its parameters first) of the index that follows; 
        // VariableGlobal: the module's global of the index that follows), the lines of its source where it can be seen (its first and 
        // last: 0 and 0, all of the function's), and its value's language (the namespace of the runtime plugin that describes its 
        // values (see neos/runtime/debug.h); empty: the VM's) and type (as that language describes it). The VM's types: i8, u8, i16, 
        // u16, i32, u32, i64, u64, f32, f64, bool, char (a code point), cchar (a C char: a byte), string (a string in linear memory: 
        // its address (see string_header)); and those of objects in linear memory (e.g. C's), whose values are addresses: 
        // "@<offset>:<type>" (the object of <type> at the value plus <offset>), "*<type>" (a pointer to an object of <type>), 
        // "[<count>*<size>]<type>" (an array of <count> elements of <type>, each <size> bytes, indexed from 0), "[@<dimensions>*<size>]<type>" 
        // (an array with a descriptor: at its address its elements' offset from it, then each dimension's first and last index (i64s); the last
        // dimension varies fastest), "<name>{<member>@<offset>:<type>;...}" (a 
        // structure or union), "%<n>" (the type named "%type<n>" among the module's globals (NoFunction, VariableHidden): e.g. a 
        // structure that a member points to)
        inline constexpr char VariablesSection[] = "neos.variables";
        inline constexpr std::uint32_t NoFunction = 0xFFFFFFFFu;
        inline constexpr std::uint8_t VariableParameter = 0x01u;
        inline constexpr std::uint8_t VariableScope = 0x02u;
        inline constexpr std::uint8_t VariableHidden = 0x04u;   ///< (not shown among the function's variables: e.g. a scope (VariableScope) shown as the globals)
        inline constexpr std::uint8_t VariableModule = 0x08u;   ///< (a variable of the code of a module's (or script's) top level: the module's functions can 
                                                                ///< see it (the debugger looks a name up in the outermost frame of the module's top level, 
                                                                ///< after the frame's own variables and the globals))
        inline constexpr std::uint8_t VariableLocal = 0u;
        inline constexpr std::uint8_t VariableGlobal = 1u;
        inline constexpr std::uint8_t VariableAddress = 2u;     ///< (its value (a word) is the index that follows: e.g. the address of a static object 
                                                                ///< (see the VM's memory types below))

        // linking (a program of more than one translation unit, of any languages: see neos_rt_link_type): the custom section of the modules 
        // of the translation units after the first (each compiled on its own, a program of its own: its top level runs when its turn comes, 
        // when the program calls the VM's function neos::link_run(its index)): a count, then for each its language 
        // and its module; and the custom section of the functions of a module that the code of the program's other translation units calls
        // by their names (e.g. neoscript's, whose language has no runtime plugin): a count, then for each its name, its language, its index and
        // its link signature
        // (a global's flags (the global section's mutability byte): 0x01 mutable, as WebAssembly's; GlobalPerThread: each thread that runs the 
        // program has its own (its value as the global's initializer gives it, when the thread first uses it; the program's own thread's: the
        // module's), e.g. a language's stack pointer)
        inline constexpr std::uint8_t GlobalPerThread = 0x80u;
        inline constexpr char LinkedSection[] = "neos.linked";
        inline constexpr char LinksSection[] = "neos.links";
        // (a module's import of a function that another translation unit's code defines (its global of the name): "link::<name>#<link 
        // signature>")
        inline constexpr char LinkNamespace[] = "link";
        // (a link signature: a code for each parameter, then '>', then the result's code: 'b' bool, 'i' a signed and 'u' an unsigned integer 
        // of at most 32 bits, 'I' and 'U' of 64 bits, 'f' f32, 'F' f64, 's' string, 'v' (a result) none)
        inline constexpr std::optional<char> link_code(language::type aType)
        {
            switch (aType)
            {
            case language::type::Void: return 'v';
            case language::type::Boolean: return 'b';
            case language::type::I8:
            case language::type::I16:
            case language::type::I32: return 'i';
            case language::type::U8:
            case language::type::U16:
            case language::type::U32: return 'u';
            case language::type::I64: return 'I';
            case language::type::U64: return 'U';
            case language::type::F32: return 'f';
            case language::type::F64: return 'F';
            case language::type::String: return 's';
            default: return std::nullopt;
            }
        }

        inline string_header string_header_of(std::string_view aText)
        {
            // (a four byte sequence is a surrogate pair in UTF-16)
            string_header header{ static_cast<std::uint32_t>(aText.size()), 0u, 0u };
            // (without branches, so that it can be vectorized: a byte that is not a continuation byte starts a code point)
            for (auto c : aText)
            {
                auto const b = static_cast<unsigned char>(c);
                std::uint32_t const starts = (b & 0xC0u) != 0x80u ? 1u : 0u;
                header.codePoints += starts;
                header.utf16Units += starts + (b >= 0xF0u ? 1u : 0u);
            }
            return header;
        }
    }
}