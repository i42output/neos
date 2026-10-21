/*
  regexp_engine.hpp

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

// The RegExp engine: the pattern parser (ECMA-262 22.2.1 and Annex B.1.2, with its early errors) and a backtracking matcher
// (22.2.2) that compiles a pattern to a program of instructions and runs it with an explicit backtrack stack (it does not recurse
// on the C++ stack as it matches)

#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace neos::javascript_runtime::regexp
{
    enum : std::uint32_t
    {
        FlagHasIndices = 0x01u,     ///< d
        FlagGlobal = 0x02u,         ///< g
        FlagIgnoreCase = 0x04u,     ///< i
        FlagMultiline = 0x08u,      ///< m
        FlagDotAll = 0x10u,         ///< s
        FlagUnicode = 0x20u,        ///< u
        FlagUnicodeSets = 0x40u,    ///< v
        FlagSticky = 0x80u          ///< y
    };

    std::optional<std::uint32_t> parse_flags(std::u16string_view aFlags);   ///< (none: a flag that is not one, or one that is repeated)

    struct syntax_error
    {
        std::string message;
    };

    struct range
    {
        char32_t first;
        char32_t last;              ///< (inclusive)
    };

    enum class opcode : std::uint8_t
    {
        Match,
        Char,                       ///< a: the character
        CharI,                      ///< a: the character (canonicalized)
        Class,                      ///< a: the class
        Dot,                        ///< (any character but a line terminator)
        Any,
        LineStart,                  ///< ^ (flags: multiline)
        LineEnd,                    ///< $ (flags: multiline)
        WordBoundary,               ///< \b (flags: extra word characters (u or v and i))
        NotWordBoundary,            ///< \B
        Fork,                       ///< (continue; on backtracking at a)
        Jump,                       ///< a: the target
        Save,                       ///< a: the capture slot
        ClearCaptures,              ///< a: the first slot; b: the number of slots
        Backreference,              ///< a: the backreference's groups (an index of backreferences)
        LookStart,                  ///< (flags: negative, behind) a: the instruction after its LookEnd
        LookEnd,
        RepeatInit,                 ///< a: the register (a pair: the count and the position at the start of the iteration)
        RepeatLoop,                 ///< a: the register; b: min; c: max; d: the exit
        RepeatStart,                ///< a: the register
        RepeatEnd,                  ///< a: the register; b: min; c: the loop (flags: empty check)
        Star                        ///< a: min; b: max (a loop of the single character instruction that follows it)
    };

    enum : std::uint8_t
    {
        InstructionBackward = 0x01u,
        InstructionGreedy = 0x02u,
        InstructionIgnoreCase = 0x04u,
        InstructionMultiline = 0x08u,
        InstructionExtraWord = 0x10u,
        InstructionNegative = 0x20u,
        InstructionEmptyCheck = 0x40u
    };

    inline constexpr std::uint32_t Infinite = 0xFFFFFFFFu;

    struct instruction
    {
        opcode op;
        std::uint8_t flags = 0u;
        std::uint32_t a = 0u;
        std::uint32_t b = 0u;
        std::uint32_t c = 0u;
        std::uint32_t d = 0u;
    };

    struct character_class
    {
        std::vector<range> ranges;          ///< (canonicalized if it ignores case)
        std::uint64_t ascii[2] = {};
        bool invert = false;
        bool ignoreCase = false;
    };

    // (a compiled pattern)
    struct program
    {
        std::uint32_t flags = 0u;
        std::uint32_t captureCount = 0u;                        ///< (the capturing groups (not the whole match))
        std::vector<std::u16string> groupNames;                 ///< (by group number (1...); empty if unnamed)
        bool hasNamedGroups = false;
        std::vector<instruction> code;
        std::vector<character_class> classes;
        std::vector<std::vector<std::uint32_t>> backreferences; ///< (the groups that each backreference refers to)
        std::uint32_t registers = 0u;
        // (searching)
        bool anchored = false;                                  ///< (it can only match at the start of the input)
        bool hasFirstChar = false;
        char16_t firstChar = 0u;                                ///< (the only code unit that a match can start with)
        bool hasFirstSet = false;
        character_class firstSet;                               ///< (the code units that a match can start with)
        std::u16string prefix;                                  ///< (the literal text that a match starts with (if two or more code units))
        std::array<std::uint16_t, 256> prefixSkip = {};         ///< (its Horspool skips, by the low byte of a code unit)
    };

    // (a SyntaxError (as syntax_error) if the pattern is not one)
    std::shared_ptr<program const> compile(std::u16string_view aPattern, std::uint32_t aFlags);

    enum class match_result
    {
        Match,
        NoMatch,
        Overflow        ///< (the backtrack stack is too big)
    };

    // (finds a match starting at aStart (only at aStart if aSticky) or after it; aCaptures: the capture slots (start and end of each
    // group; the whole match first) (-1: undefined); a match's start (slot 0) is the index it was attempted at)
    match_result exec(program const& aProgram, std::u16string_view aInput, std::size_t aStart, bool aSticky, std::vector<std::int32_t>& aCaptures);
}
