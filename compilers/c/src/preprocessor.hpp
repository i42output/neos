/*
  preprocessor.hpp

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

// C: the preprocessor (translation phases 1 to 4: line splicing, preprocessing tokens, directives and macro replacement), whose
// output the schema parses: the preprocessing tokens of a translation unit separated by white space, the identifiers that are 
// typedef names where they are used marked by a '@' before them (see mark_typedef_names), with a map from each token's place in
// the output to its place in its source file

#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace neos::compilers::c
{
    // a place in a source file (its index in the preprocessor's files; lines and columns 1-based)
    struct source_location
    {
        std::uint32_t file = 0u;
        std::uint32_t line = 0u;
        std::uint32_t column = 0u;
    };

    struct preprocess_error : std::runtime_error
    {
        preprocess_error(source_location const& aWhere, std::string const& aMessage) : std::runtime_error{ aMessage }, where{ aWhere } {}
        source_location where;
    };

    enum class token_kind : std::uint8_t
    {
        Identifier,
        Number,
        Character,
        String,
        Punctuator,
        HeaderName,
        Other,
        Placemarker,
        End
    };

    struct pp_token
    {
        token_kind kind = token_kind::End;
        std::string text;               ///< (its spelling; a digraph's: the punctuator it is)
        source_location where;
        bool space = false;             ///< (white space before it)
        bool lineStart = false;         ///< (the first token of a line)
        bool digraph = false;
        bool noExpand = false;          ///< (an identifier that is not replaced, even when examined again ("painted blue"))
        bool typedefName = false;       ///< (an identifier that is a typedef name, where it is used: marked in the output)
        std::uint32_t hideSet = 0u;     ///< (the macros that do not replace it: an index of the preprocessor's hide sets (0: none))
        bool is(char const* aPunctuator) const { return kind == token_kind::Punctuator && text == aPunctuator; }
    };

    class preprocessor
    {
    public:
        struct options
        {
            std::vector<std::string> quoteIncludes;     ///< (#include "...": searched after the including file's folder)
            std::vector<std::string> includes;          ///< (#include <...> (and "..." after quoteIncludes))
            std::vector<std::string> systemIncludes;    ///< (the implementation's headers (searched last))
            std::function<void(source_location const&, std::string const&)> warning;
        };
        struct output
        {
            std::string text;
            std::vector<std::pair<std::uint32_t, source_location>> map;    ///< (each token's offset in the text, and its place: by offset)
            std::vector<std::string> identifiers;                          ///< (the identifiers in the output, once each)
        };
    public:
        preprocessor(options const& aOptions);
        ~preprocessor();
    public:
        output run(std::string const& aPath, std::string const& aSource);
        std::vector<std::string> const& files() const { return iFiles; }
        source_location location_of(output const& aOutput, std::size_t aOffset) const;
    private:
        struct macro
        {
            std::uint32_t id = 0u;
            std::string name;
            bool function = false;
            bool variadic = false;
            std::vector<std::string> parameters;     ///< (a variadic macro's last is "__VA_ARGS__")
            std::vector<pp_token> body;
            source_location where;
            bool builtin = false;                    ///< (__FILE__, __LINE__, ...)
        };
        struct conditional
        {
            bool active = false;       ///< (its group is being processed)
            bool taken = false;        ///< (a group of it has been processed)
            bool outerActive = false;  ///< (the groups it is within are being processed)
            bool sawElse = false;
            source_location where;
        };
        struct file_state
        {
            std::uint32_t file = 0u;
            std::vector<pp_token> tokens;
            std::size_t next = 0u;
            std::vector<conditional> conditionals;
            std::int64_t lineDelta = 0;               ///< (#line: the presumed line minus the physical line)
            std::optional<std::string> presumedName;  ///< (#line: the presumed source file name)
            std::string folder;
        };
        class reader;
        class file_reader;
        class list_reader;
        friend class file_reader;
    private:
        bool is_defined(std::string const& aName) const;
        std::uint32_t add_file(std::string const& aPath);
        std::vector<pp_token> tokenize(std::string const& aSource, std::uint32_t aFile);
        void push_file(std::string const& aPath, std::string const& aSource);
        bool next_from_files(pp_token& aToken);
        void directive(file_state& aFile, std::vector<pp_token>& aLine);
        void skip_group_line(file_state& aFile, std::vector<pp_token>& aLine);
        std::vector<pp_token> read_line(file_state& aFile);
        void define(std::vector<pp_token> const& aLine, std::size_t aFirst);
        void include(file_state& aFile, std::vector<pp_token> aLine, std::size_t aFirst, bool aNext);
        void embed(file_state& aFile, std::vector<pp_token> aLine, std::size_t aFirst, std::vector<pp_token>& aResult);
        void line_directive(file_state& aFile, std::vector<pp_token> aLine, std::size_t aFirst);
        void pragma(std::vector<pp_token> const& aLine, std::size_t aFirst, source_location const& aWhere);
        bool evaluate_condition(file_state& aFile, std::vector<pp_token> const& aLine, std::size_t aFirst);
        std::optional<std::string> find_include(std::string const& aName, bool aQuoted, std::string const& aFolder, bool aNext = false) const;
        bool header_spelling(std::vector<pp_token> const& aTokens, std::size_t& aIndex, std::string& aName, bool& aQuoted, source_location const& aWhere);
        // macro replacement
        void expand(reader& aReader, std::vector<pp_token>& aOutput, bool aDirective = false);
        std::vector<pp_token> substitute(macro const& aMacro, std::vector<std::vector<pp_token>> const& aArguments, std::uint32_t aHideSet,
            source_location const& aWhere);
        std::vector<pp_token> expanded_argument(std::vector<pp_token> const& aArgument);
        pp_token stringize(std::vector<pp_token> const& aTokens, source_location const& aWhere) const;
        pp_token paste(pp_token const& aLeft, pp_token const& aRight);
        bool builtin_macro(pp_token const& aToken, reader& aReader, std::vector<pp_token>& aResult);
        // hide sets
        std::uint32_t hide_set_add(std::uint32_t aSet, std::uint32_t aMacro);
        std::uint32_t hide_set_union(std::uint32_t aA, std::uint32_t aB);
        std::uint32_t hide_set_intersection(std::uint32_t aA, std::uint32_t aB);
        bool hidden(std::uint32_t aSet, std::uint32_t aMacro) const;
        std::uint32_t intern(std::vector<std::uint32_t> aSet);
        // the output
        void mark_typedef_names(std::vector<pp_token>& aTokens);
        void warn(source_location const& aWhere, std::string const& aMessage) const;
        std::uint32_t presumed_line(source_location const& aWhere) const;
        std::string presumed_name(source_location const& aWhere) const;
    private:
        options iOptions;
        std::vector<std::string> iFiles;
        std::map<std::string, std::uint32_t> iFileIndex;
        std::vector<file_state> iFileStack;
        std::vector<pp_token> iPending;        ///< (tokens to read before the files' (a stack: the next on top))
        std::unordered_map<std::string, macro> iMacros;
        std::uint32_t iNextMacro = 1u;
        std::vector<std::vector<std::uint32_t>> iHideSets;
        std::map<std::vector<std::uint32_t>, std::uint32_t> iHideSetIndex;
        std::map<std::pair<std::uint32_t, std::uint32_t>, std::uint32_t> iHideSetUnions;
        std::set<std::string> iOnce;           ///< (#pragma once: the files' paths)
        std::map<std::string, std::vector<std::optional<macro>>> iPushedMacros;   ///< (#pragma push_macro: the macros' definitions (none: undefined))
        std::uint32_t iCounter = 0u;           ///< (__COUNTER__)
        std::string iDate;
        std::string iTime;
        std::size_t iIncludeDepth = 0u;
    };
}
