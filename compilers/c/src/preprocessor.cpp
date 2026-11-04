/*
  preprocessor.cpp

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

// C: the preprocessor. Macro replacement follows Prosser's algorithm (each token has a hide set: the macros whose replacement it
// came from, which do not replace it again)

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>
#include "preprocessor.hpp"

namespace neos::compilers::c
{
    namespace
    {
        bool identifier_start(unsigned char c)
        {
            return std::isalpha(c) || c == '_' || c == '$' || c >= 0x80u;
        }

        bool identifier_char(unsigned char c)
        {
            return identifier_start(c) || std::isdigit(c);
        }

        // (the punctuators, longest first; a digraph's punctuator follows it)
        struct punctuator_spelling
        {
            char const* spelling;
            char const* punctuator;
        };
        punctuator_spelling const sPunctuators[] =
        {
            { "%:%:", "##" }, { "...", "..." }, { "<<=", "<<=" }, { ">>=", ">>=" },
            { "->", "->" }, { "++", "++" }, { "--", "--" }, { "<<", "<<" }, { ">>", ">>" }, { "<=", "<=" }, { ">=", ">=" }, { "==", "==" },
            { "!=", "!=" }, { "&&", "&&" }, { "||", "||" }, { "*=", "*=" }, { "/=", "/=" }, { "%=", "%=" }, { "+=", "+=" }, { "-=", "-=" },
            { "&=", "&=" }, { "^=", "^=" }, { "|=", "|=" }, { "##", "##" }, { "<:", "[" }, { ":>", "]" }, { "<%", "{" }, { "%>", "}" },
            { "%:", "#" }, { "::", "::" },
            { "[", "[" }, { "]", "]" }, { "(", "(" }, { ")", ")" }, { "{", "{" }, { "}", "}" }, { ".", "." }, { "&", "&" }, { "*", "*" },
            { "+", "+" }, { "-", "-" }, { "~", "~" }, { "!", "!" }, { "/", "/" }, { "%", "%" }, { "<", "<" }, { ">", ">" }, { "^", "^" },
            { "|", "|" }, { "?", "?" }, { ":", ":" }, { ";", ";" }, { "=", "=" }, { ",", "," }, { "#", "#" }
        };

        std::string digraph_spelling(std::string const& aPunctuator)
        {
            for (auto const& p : sPunctuators)
                if (aPunctuator == p.punctuator && std::strcmp(p.spelling, p.punctuator) != 0)
                    return p.spelling;
            return aPunctuator;
        }

        std::string spelling_of(pp_token const& aToken)
        {
            return aToken.digraph ? digraph_spelling(aToken.text) : aToken.text;
        }

        std::string read_file(std::string const& aPath, bool& aOk)
        {
            std::ifstream stream{ aPath, std::ios::binary };
            aOk = static_cast<bool>(stream);
            if (!aOk)
                return {};
            std::ostringstream contents;
            contents << stream.rdbuf();
            return contents.str();
        }

        // (a value of a #if expression: intmax_t or uintmax_t)
        struct pp_value
        {
            std::uint64_t value = 0u;
            bool isUnsigned = false;
            std::int64_t s() const { return static_cast<std::int64_t>(value); }
        };
    }

    // --- readers: where macro replacement takes its tokens from (the files being preprocessed, or a list of tokens)

    class preprocessor::reader
    {
    public:
        virtual ~reader() = default;
        virtual bool next(pp_token& aToken) = 0;
        virtual void push(std::vector<pp_token> const& aTokens) = 0;   ///< (read next, in order)
        virtual void unget(pp_token const& aToken) = 0;
    };

    class preprocessor::list_reader : public reader
    {
    public:
        list_reader(std::vector<pp_token> const& aTokens) : iTokens{ aTokens.rbegin(), aTokens.rend() } {}
        bool next(pp_token& aToken) override
        {
            if (iTokens.empty())
                return false;
            aToken = std::move(iTokens.back());
            iTokens.pop_back();
            return true;
        }
        void push(std::vector<pp_token> const& aTokens) override
        {
            iTokens.insert(iTokens.end(), aTokens.rbegin(), aTokens.rend());
        }
        void unget(pp_token const& aToken) override
        {
            iTokens.push_back(aToken);
        }
    private:
        std::vector<pp_token> iTokens;  ///< (the next on top)
    };

    class preprocessor::file_reader : public reader
    {
    public:
        file_reader(preprocessor& aPreprocessor) : iPreprocessor{ aPreprocessor } {}
        bool next(pp_token& aToken) override
        {
            if (!iPreprocessor.iPending.empty())
            {
                aToken = std::move(iPreprocessor.iPending.back());
                iPreprocessor.iPending.pop_back();
                return true;
            }
            return iPreprocessor.next_from_files(aToken);
        }
        void push(std::vector<pp_token> const& aTokens) override
        {
            iPreprocessor.iPending.insert(iPreprocessor.iPending.end(), aTokens.rbegin(), aTokens.rend());
        }
        void unget(pp_token const& aToken) override
        {
            iPreprocessor.iPending.push_back(aToken);
        }
    private:
        preprocessor& iPreprocessor;
    };

    // --- the preprocessor

    preprocessor::preprocessor(options const& aOptions) :
        iOptions{ aOptions }
    {
        iHideSets.push_back({});
        iHideSetIndex[{}] = 0u;
        auto const now = std::time(nullptr);
        std::tm local{};
#if defined(_WIN32)
        localtime_s(&local, &now);
#else
        localtime_r(&now, &local);
#endif
        char buffer[64];
        std::strftime(buffer, sizeof(buffer), "\"%b %e %Y\"", &local);
        iDate = buffer;
        std::strftime(buffer, sizeof(buffer), "\"%H:%M:%S\"", &local);
        iTime = buffer;
    }

    preprocessor::~preprocessor() = default;

    void preprocessor::warn(source_location const& aWhere, std::string const& aMessage) const
    {
        if (iOptions.warning)
            iOptions.warning(aWhere, aMessage);
    }

    bool preprocessor::is_defined(std::string const& aName) const
    {
        // (a macro's name; and the operators of #if that #ifdef and defined treat as macros (6.10.1))
        return iMacros.contains(aName) || aName == "__has_include" || aName == "__has_embed" || aName == "__has_c_attribute" || 
            aName == "__has_include_next" || aName == "__has_attribute";
    }

    std::uint32_t preprocessor::add_file(std::string const& aPath)
    {
        auto const existing = iFileIndex.find(aPath);
        if (existing != iFileIndex.end())
            return existing->second;
        auto const index = static_cast<std::uint32_t>(iFiles.size());
        iFiles.push_back(aPath);
        iFileIndex[aPath] = index;
        return index;
    }

    source_location preprocessor::location_of(output const& aOutput, std::size_t aOffset) const
    {
        auto const it = std::upper_bound(aOutput.map.begin(), aOutput.map.end(), aOffset,
            [](std::size_t aValue, auto const& aEntry) { return aValue < aEntry.first; });
        if (it == aOutput.map.begin())
            return aOutput.map.empty() ? source_location{} : aOutput.map.front().second;
        return std::prev(it)->second;
    }

    std::uint32_t preprocessor::presumed_line(source_location const& aWhere) const
    {
        for (auto i = iFileStack.rbegin(); i != iFileStack.rend(); ++i)
            if (i->file == aWhere.file)
                return static_cast<std::uint32_t>(static_cast<std::int64_t>(aWhere.line) + i->lineDelta);
        return aWhere.line;
    }

    std::string preprocessor::presumed_name(source_location const& aWhere) const
    {
        for (auto i = iFileStack.rbegin(); i != iFileStack.rend(); ++i)
            if (i->file == aWhere.file && i->presumedName)
                return *i->presumedName;
        return aWhere.file < iFiles.size() ? iFiles[aWhere.file] : std::string{};
    }

    // --- translation phases 1 to 3: lines spliced, preprocessing tokens

    std::vector<pp_token> preprocessor::tokenize(std::string const& aSource, std::uint32_t aFile)
    {
        // (the source's lines spliced: the characters, and each one's line and column)
        std::string text;
        std::vector<std::uint32_t> lines;
        std::vector<std::uint32_t> columns;
        text.reserve(aSource.size() + 1u);
        {
            std::uint32_t line = 1u;
            std::uint32_t column = 1u;
            std::size_t i = 0u;
            // (a UTF-8 byte order mark: not part of the text)
            if (aSource.size() >= 3u && aSource.compare(0u, 3u, "\xEF\xBB\xBF") == 0)
                i = 3u;
            for (; i < aSource.size(); ++i)
            {
                char c = aSource[i];
                if (c == '\\')
                {
                    // (a backslash then a new-line (white space between them is allowed, as GCC does))
                    std::size_t j = i + 1u;
                    while (j < aSource.size() && (aSource[j] == ' ' || aSource[j] == '\t'))
                        ++j;
                    if (j < aSource.size() && (aSource[j] == '\n' || aSource[j] == '\r'))
                    {
                        if (aSource[j] == '\r' && j + 1u < aSource.size() && aSource[j + 1u] == '\n')
                            ++j;
                        i = j;
                        ++line;
                        column = 1u;
                        continue;
                    }
                }
                if (c == '\r')
                {
                    if (i + 1u < aSource.size() && aSource[i + 1u] == '\n')
                        continue;
                    c = '\n';
                }
                text.push_back(c);
                lines.push_back(line);
                columns.push_back(column);
                if (c == '\n')
                {
                    ++line;
                    column = 1u;
                }
                else
                    ++column;
            }
            // (a source file that does not end in a new-line: as if it did)
            if (text.empty() || text.back() != '\n')
            {
                text.push_back('\n');
                lines.push_back(line);
                columns.push_back(column);
            }
        }
        std::vector<pp_token> result;
        bool lineStart = true;
        bool space = false;
        // (a directive's state: header names are tokens after #include, #embed, __has_include and __has_embed)
        int directiveTokens = -1;           ///< (tokens of the current line if it is a directive, else -1)
        bool headerNameNext = false;
        std::size_t const n = text.size();
        std::size_t i = 0u;
        auto const location = [&](std::size_t aAt) { return source_location{ aFile, lines[aAt], columns[aAt] }; };
        while (i < n)
        {
            char const c = text[i];
            if (c == '\n')
            {
                lineStart = true;
                space = false;
                directiveTokens = -1;
                headerNameNext = false;
                ++i;
                continue;
            }
            if (c == ' ' || c == '\t' || c == '\v' || c == '\f')
            {
                space = true;
                ++i;
                continue;
            }
            if (c == '/' && i + 1u < n && text[i + 1u] == '*')
            {
                auto const end = text.find("*/", i + 2u);
                if (end == std::string::npos)
                    throw preprocess_error{ location(i), "unterminated comment" };
                i = end + 2u;
                space = true;
                continue;
            }
            if (c == '/' && i + 1u < n && text[i + 1u] == '/')
            {
                while (i < n && text[i] != '\n')
                    ++i;
                space = true;
                continue;
            }
            pp_token token;
            token.where = location(i);
            token.space = space;
            token.lineStart = lineStart;
            std::size_t const start = i;
            auto const quoted = [&](char aQuote) -> bool
            {
                // (a character constant or string literal from i (its prefix done): false if it is not terminated on its line)
                std::size_t j = i + 1u;
                while (j < n && text[j] != aQuote && text[j] != '\n')
                {
                    if (text[j] == '\\' && j + 1u < n && text[j + 1u] != '\n')
                        ++j;
                    ++j;
                }
                if (j >= n || text[j] != aQuote)
                    return false;
                i = j + 1u;
                return true;
            };
            if (headerNameNext && (c == '<' || c == '"'))
            {
                auto const close = text.find(c == '<' ? '>' : '"', i + 1u);
                auto const eol = text.find('\n', i);
                if (close != std::string::npos && close < eol)
                {
                    token.kind = token_kind::HeaderName;
                    token.text = text.substr(i, close + 1u - i);
                    i = close + 1u;
                    headerNameNext = false;
                    result.push_back(std::move(token));
                    lineStart = false;
                    space = false;
                    if (directiveTokens >= 0)
                        ++directiveTokens;
                    continue;
                }
            }
            headerNameNext = false;
            if (std::isdigit(static_cast<unsigned char>(c)) || (c == '.' && i + 1u < n && std::isdigit(static_cast<unsigned char>(text[i + 1u]))))
            {
                // (a preprocessing number)
                ++i;
                while (i < n)
                {
                    char const d = text[i];
                    if ((d == '+' || d == '-') && (text[i - 1u] == 'e' || text[i - 1u] == 'E' || text[i - 1u] == 'p' || text[i - 1u] == 'P'))
                        ++i;
                    else if (identifier_char(static_cast<unsigned char>(d)) || d == '.')
                        ++i;
                    else if (d == '\'' && i + 1u < n && identifier_char(static_cast<unsigned char>(text[i + 1u])))
                        i += 2u;
                    else
                        break;
                }
                token.kind = token_kind::Number;
            }
            else if (identifier_start(static_cast<unsigned char>(c)))
            {
                while (i < n && identifier_char(static_cast<unsigned char>(text[i])))
                    ++i;
                auto const word = std::string_view{ text }.substr(start, i - start);
                token.kind = token_kind::Identifier;
                if ((word == "u8" || word == "u" || word == "U" || word == "L") && i < n && (text[i] == '\'' || text[i] == '"'))
                {
                    char const quote = text[i];
                    auto const saved = i;
                    if (quoted(quote))
                        token.kind = quote == '"' ? token_kind::String : token_kind::Character;
                    else
                        i = saved;
                }
            }
            else if (c == '\'' || c == '"')
            {
                if (quoted(c))
                    token.kind = c == '"' ? token_kind::String : token_kind::Character;
                else
                {
                    // (an unmatched quote: a token of its own (an error, unless the line is skipped))
                    ++i;
                    token.kind = token_kind::Other;
                }
            }
            else
            {
                token.kind = token_kind::Other;
                for (auto const& p : sPunctuators)
                {
                    auto const length = std::strlen(p.spelling);
                    if (text.compare(i, length, p.spelling) == 0)
                    {
                        // ("<::" is "<" "::" unless followed by ':' or '>' (as in C++): C has no such rule; "<:" is a digraph)
                        token.kind = token_kind::Punctuator;
                        token.digraph = std::strcmp(p.spelling, p.punctuator) != 0;
                        token.text = p.punctuator;
                        i += length;
                        break;
                    }
                }
                if (token.kind == token_kind::Other)
                {
                    // (a character that is no token's: a UTF-8 sequence is one)
                    ++i;
                    while (i < n && (static_cast<unsigned char>(text[i]) & 0xC0u) == 0x80u)
                        ++i;
                }
            }
            if (token.text.empty())
                token.text = text.substr(start, i - start);
            // (a directive: #include <...>; or __has_include (<...>) in a directive)
            if (token.lineStart && token.is("#"))
                directiveTokens = 0;
            else if (directiveTokens >= 0)
            {
                ++directiveTokens;
                if (directiveTokens == 1 && token.kind == token_kind::Identifier && (token.text == "include" || token.text == "embed" || token.text == "include_next"))
                    headerNameNext = true;
                else if (token.is("(") && !result.empty() && result.back().kind == token_kind::Identifier &&
                    (result.back().text == "__has_include" || result.back().text == "__has_embed" || result.back().text == "__has_include_next"))
                    headerNameNext = true;
            }
            result.push_back(std::move(token));
            lineStart = false;
            space = false;
        }
        return result;
    }

    // --- files

    void preprocessor::push_file(std::string const& aPath, std::string const& aSource)
    {
        if (iIncludeDepth > 200u)
            throw preprocess_error{ iFileStack.empty() ? source_location{} : iFileStack.back().tokens.empty() ? source_location{} :
                iFileStack.back().tokens[std::min(iFileStack.back().next, iFileStack.back().tokens.size() - 1u)].where, "#include nested too deeply" };
        file_state state;
        state.file = add_file(aPath);
        state.tokens = tokenize(aSource, state.file);
        std::error_code ec;
        state.folder = std::filesystem::path{ aPath }.parent_path().string();
        iFileStack.push_back(std::move(state));
        ++iIncludeDepth;
    }

    std::vector<pp_token> preprocessor::read_line(file_state& aFile)
    {
        // (the tokens of the rest of the line)
        std::vector<pp_token> line;
        while (aFile.next < aFile.tokens.size() && !aFile.tokens[aFile.next].lineStart)
            line.push_back(aFile.tokens[aFile.next++]);
        return line;
    }

    bool preprocessor::next_from_files(pp_token& aToken)
    {
        for (;;)
        {
            if (iFileStack.empty())
                return false;
            auto& file = iFileStack.back();
            if (file.next >= file.tokens.size())
            {
                if (!file.conditionals.empty())
                    throw preprocess_error{ file.conditionals.back().where, "unterminated conditional directive" };
                iFileStack.pop_back();
                --iIncludeDepth;
                continue;
            }
            bool const active = file.conditionals.empty() || file.conditionals.back().active;
            auto& token = file.tokens[file.next];
            if (token.lineStart && token.is("#"))
            {
                std::vector<pp_token> line;
                line.push_back(file.tokens[file.next++]);
                auto rest = read_line(file);
                line.insert(line.end(), rest.begin(), rest.end());
                if (active)
                    directive(file, line);
                else
                    skip_group_line(file, line);
                // (a directive's tokens (#embed's): before the files')
                if (!iPending.empty())
                {
                    aToken = std::move(iPending.back());
                    iPending.pop_back();
                    return true;
                }
                continue;
            }
            if (!active)
            {
                ++file.next;
                continue;
            }
            aToken = file.tokens[file.next++];
            return true;
        }
    }

    std::optional<std::string> preprocessor::find_include(std::string const& aName, bool aQuoted, std::string const& aFolder, bool aNext) const
    {
        namespace fs = std::filesystem;
        std::error_code ec;
        auto const found = [&](fs::path const& aPath) -> std::optional<std::string>
        {
            if (fs::is_regular_file(aPath, ec))
                return fs::absolute(aPath, ec).lexically_normal().generic_string();
            return {};
        };
        if (fs::path{ aName }.is_absolute())
            return found(aName);
        std::vector<std::string> folders;
        if (aQuoted)
        {
            folders.push_back(aFolder);
            folders.insert(folders.end(), iOptions.quoteIncludes.begin(), iOptions.quoteIncludes.end());
        }
        folders.insert(folders.end(), iOptions.includes.begin(), iOptions.includes.end());
        folders.insert(folders.end(), iOptions.systemIncludes.begin(), iOptions.systemIncludes.end());
        bool skipping = aNext;
        for (auto const& folder : folders)
        {
            if (skipping)
            {
                // (#include_next: the folders after the current file's)
                if (fs::equivalent(fs::path{ folder }, fs::path{ aFolder }, ec))
                    skipping = false;
                continue;
            }
            if (auto const path = found(fs::path{ folder.empty() ? "." : folder } / aName))
                return path;
        }
        if (aNext && skipping)
            return find_include(aName, aQuoted, aFolder, false);
        return {};
    }

    bool preprocessor::header_spelling(std::vector<pp_token> const& aTokens, std::size_t& aIndex, std::string& aName, bool& aQuoted, source_location const& aWhere)
    {
        // (a header name: a header name token, a string literal, or (macro replaced) tokens from '<' to '>')
        if (aIndex >= aTokens.size())
            return false;
        auto const& first = aTokens[aIndex];
        if (first.kind == token_kind::HeaderName || (first.kind == token_kind::String && first.text.front() == '"'))
        {
            aQuoted = first.text.front() == '"';
            aName = first.text.substr(1u, first.text.size() - 2u);
            ++aIndex;
            return true;
        }
        if (first.is("<"))
        {
            std::string name;
            std::size_t i = aIndex + 1u;
            for (; i < aTokens.size() && !aTokens[i].is(">"); ++i)
            {
                if (aTokens[i].space && !name.empty())
                    name += ' ';
                name += spelling_of(aTokens[i]);
            }
            if (i >= aTokens.size())
                throw preprocess_error{ aWhere, "missing '>' in header name" };
            aName = name;
            aQuoted = false;
            aIndex = i + 1u;
            return true;
        }
        return false;
    }

    // --- directives

    void preprocessor::skip_group_line(file_state& aFile, std::vector<pp_token>& aLine)
    {
        // (a directive in a group that is skipped: only conditionals' nesting matters)
        if (aLine.size() < 2u || aLine[1].kind != token_kind::Identifier)
            return;
        auto const& name = aLine[1].text;
        auto& conditionals = aFile.conditionals;
        if (name == "if" || name == "ifdef" || name == "ifndef")
        {
            conditional c;
            c.outerActive = false;
            c.active = false;
            c.taken = true;
            c.where = aLine[0].where;
            conditionals.push_back(c);
        }
        else if (name == "elif" || name == "elifdef" || name == "elifndef" || name == "else" || name == "endif")
            directive(aFile, aLine);
    }

    void preprocessor::directive(file_state& aFile, std::vector<pp_token>& aLine)
    {
        auto const where = aLine[0].where;
        if (aLine.size() == 1u)
            return;     // (the null directive)
        auto& conditionals = aFile.conditionals;
        auto const& nameToken = aLine[1];
        if (nameToken.kind != token_kind::Identifier)
        {
            if (nameToken.kind == token_kind::Number && (conditionals.empty() || conditionals.back().active))
            {
                // (# digit-sequence: as #line (GCC's output))
                line_directive(aFile, aLine, 1u);
                return;
            }
            throw preprocess_error{ nameToken.where, "invalid preprocessing directive" };
        }
        auto const& name = nameToken.text;
        bool const active = conditionals.empty() || conditionals.back().active;
        if (name == "if" || name == "ifdef" || name == "ifndef")
        {
            conditional c;
            c.where = where;
            c.outerActive = active;
            bool value = false;
            if (name == "if")
                value = evaluate_condition(aFile, aLine, 2u);
            else
            {
                if (aLine.size() < 3u || aLine[2].kind != token_kind::Identifier)
                    throw preprocess_error{ where, "#" + name + " without a macro name" };
                value = is_defined(aLine[2].text) == (name == "ifdef");
            }
            c.active = value;
            c.taken = value;
            conditionals.push_back(c);
            return;
        }
        if (name == "elif" || name == "elifdef" || name == "elifndef")
        {
            if (conditionals.empty())
                throw preprocess_error{ where, "#" + name + " without #if" };
            auto& c = conditionals.back();
            if (c.sawElse)
                throw preprocess_error{ where, "#" + name + " after #else" };
            if (c.taken || !c.outerActive)
            {
                c.active = false;
                return;
            }
            bool value = false;
            if (name == "elif")
                value = evaluate_condition(aFile, aLine, 2u);
            else
            {
                if (aLine.size() < 3u || aLine[2].kind != token_kind::Identifier)
                    throw preprocess_error{ where, "#" + name + " without a macro name" };
                value = is_defined(aLine[2].text) == (name == "elifdef");
            }
            c.active = value;
            c.taken = value;
            return;
        }
        if (name == "else")
        {
            if (conditionals.empty())
                throw preprocess_error{ where, "#else without #if" };
            auto& c = conditionals.back();
            if (c.sawElse)
                throw preprocess_error{ where, "#else after #else" };
            c.sawElse = true;
            c.active = c.outerActive && !c.taken;
            c.taken = true;
            return;
        }
        if (name == "endif")
        {
            if (conditionals.empty())
                throw preprocess_error{ where, "#endif without #if" };
            conditionals.pop_back();
            return;
        }
        if (!active)
            return;
        if (name == "define")
            define(aLine, 2u);
        else if (name == "undef")
        {
            if (aLine.size() < 3u || aLine[2].kind != token_kind::Identifier)
                throw preprocess_error{ where, "#undef without a macro name" };
            if (aLine[2].text == "defined")
                throw preprocess_error{ where, "\"defined\" cannot be undefined" };
            iMacros.erase(aLine[2].text);
        }
        else if (name == "include" || name == "include_next")
            include(aFile, aLine, 2u, name == "include_next");
        else if (name == "embed")
        {
            std::vector<pp_token> tokens;
            embed(aFile, aLine, 2u, tokens);
            iPending.insert(iPending.end(), tokens.rbegin(), tokens.rend());
        }
        else if (name == "line")
            line_directive(aFile, aLine, 2u);
        else if (name == "error" || name == "warning")
        {
            std::string message;
            for (std::size_t i = 2u; i < aLine.size(); ++i)
            {
                if (i > 2u && aLine[i].space)
                    message += ' ';
                message += spelling_of(aLine[i]);
            }
            if (name == "error")
                throw preprocess_error{ where, "#error " + message };
            warn(where, "#warning " + message);
        }
        else if (name == "pragma")
            pragma(aLine, 2u, where);
        else if (name == "ident" || name == "sccs" || name == "assert" || name == "unassert")
            ;   // (common extensions: ignored)
        else
            throw preprocess_error{ nameToken.where, "invalid preprocessing directive #" + name };
    }

    void preprocessor::define(std::vector<pp_token> const& aLine, std::size_t aFirst)
    {
        auto const where = aLine[0].where;
        if (aLine.size() <= aFirst || aLine[aFirst].kind != token_kind::Identifier)
            throw preprocess_error{ where, "macro name missing" };
        macro m;
        m.name = aLine[aFirst].text;
        m.where = aLine[aFirst].where;
        if (m.name == "defined" || m.name == "__has_include" || m.name == "__has_embed" || m.name == "__has_c_attribute")
            throw preprocess_error{ where, "\"" + m.name + "\" cannot be used as a macro name" };
        std::size_t i = aFirst + 1u;
        if (i < aLine.size() && aLine[i].is("(") && !aLine[i].space)
        {
            m.function = true;
            ++i;
            bool first = true;
            for (;;)
            {
                if (i >= aLine.size())
                    throw preprocess_error{ where, "missing ')' in macro parameter list" };
                if (aLine[i].is(")") && first)
                {
                    ++i;
                    break;
                }
                if (aLine[i].is("..."))
                {
                    m.variadic = true;
                    m.parameters.push_back("__VA_ARGS__");
                    ++i;
                    if (i >= aLine.size() || !aLine[i].is(")"))
                        throw preprocess_error{ where, "missing ')' after \"...\"" };
                    ++i;
                    break;
                }
                if (aLine[i].kind != token_kind::Identifier)
                    throw preprocess_error{ aLine[i].where, "invalid macro parameter" };
                if (aLine[i].text == "__VA_ARGS__" || aLine[i].text == "__VA_OPT__")
                    throw preprocess_error{ aLine[i].where, aLine[i].text + " cannot be a macro parameter" };
                if (std::find(m.parameters.begin(), m.parameters.end(), aLine[i].text) != m.parameters.end())
                    throw preprocess_error{ aLine[i].where, "duplicate macro parameter \"" + aLine[i].text + "\"" };
                m.parameters.push_back(aLine[i].text);
                ++i;
                first = false;
                if (i < aLine.size() && aLine[i].is("..."))
                {
                    // (a GNU named variadic parameter (args...): as __VA_ARGS__)
                    m.variadic = true;
                    ++i;
                    if (i >= aLine.size() || !aLine[i].is(")"))
                        throw preprocess_error{ where, "missing ')' after \"...\"" };
                    ++i;
                    break;
                }
                if (i < aLine.size() && aLine[i].is(")"))
                {
                    ++i;
                    break;
                }
                if (i >= aLine.size() || !aLine[i].is(","))
                    throw preprocess_error{ where, "expected ',' or ')' in macro parameter list" };
                ++i;
            }
        }
        else if (i < aLine.size() && !aLine[i].space)
            warn(aLine[i].where, "missing white space after the macro name");
        for (; i < aLine.size(); ++i)
        {
            auto t = aLine[i];
            t.lineStart = false;
            if (m.body.empty())
                t.space = false;
            m.body.push_back(t);
        }
        if (!m.body.empty() && (m.body.front().is("##") || m.body.back().is("##")))
            throw preprocess_error{ where, "'##' cannot be at either end of a macro's replacement list" };
        if (m.function)
            for (std::size_t j = 0u; j < m.body.size(); ++j)
                if (m.body[j].is("#") && (j + 1u >= m.body.size() || m.body[j + 1u].kind != token_kind::Identifier ||
                    (std::find(m.parameters.begin(), m.parameters.end(), m.body[j + 1u].text) == m.parameters.end() && m.body[j + 1u].text != "__VA_OPT__")))
                    throw preprocess_error{ m.body[j].where, "'#' is not followed by a macro parameter" };
        auto const existing = iMacros.find(m.name);
        if (existing != iMacros.end())
        {
            auto const& old = existing->second;
            bool same = !old.builtin && old.function == m.function && old.variadic == m.variadic && old.parameters == m.parameters &&
                old.body.size() == m.body.size();
            for (std::size_t j = 0u; same && j < m.body.size(); ++j)
                same = old.body[j].text == m.body[j].text && old.body[j].kind == m.body[j].kind && (j == 0u || old.body[j].space == m.body[j].space);
            if (!same)
                warn(m.where, "\"" + m.name + "\" redefined");
            m.id = old.id;
        }
        else
            m.id = iNextMacro++;
        iMacros[m.name] = std::move(m);
    }

    void preprocessor::include(file_state& aFile, std::vector<pp_token> aLine, std::size_t aFirst, bool aNext)
    {
        auto const where = aLine[0].where;
        std::vector<pp_token> operand{ aLine.begin() + static_cast<std::ptrdiff_t>(aFirst), aLine.end() };
        std::string name;
        bool quoted = false;
        std::size_t index = 0u;
        if (!header_spelling(operand, index, name, quoted, where))
        {
            // (#include pp-tokens: macro replaced, then a header name)
            list_reader r{ operand };
            std::vector<pp_token> expanded;
            expand(r, expanded, true);
            index = 0u;
            if (!header_spelling(expanded, index, name, quoted, where))
                throw preprocess_error{ where, "#include expects \"FILENAME\" or <FILENAME>" };
        }
        auto const path = find_include(name, quoted, aFile.folder, aNext);
        if (!path)
            throw preprocess_error{ where, name + ": no such file" };
        if (iOnce.contains(*path))
            return;
        bool ok = false;
        auto source = read_file(*path, ok);
        if (!ok)
            throw preprocess_error{ where, name + ": cannot be read" };
        push_file(*path, source);
    }

    void preprocessor::embed(file_state& aFile, std::vector<pp_token> aLine, std::size_t aFirst, std::vector<pp_token>& aResult)
    {
        auto const where = aLine[0].where;
        std::vector<pp_token> operand{ aLine.begin() + static_cast<std::ptrdiff_t>(aFirst), aLine.end() };
        std::string name;
        bool quoted = false;
        std::size_t index = 0u;
        if (!header_spelling(operand, index, name, quoted, where))
        {
            list_reader r{ operand };
            std::vector<pp_token> expanded;
            expand(r, expanded, true);
            operand = expanded;
            index = 0u;
            if (!header_spelling(operand, index, name, quoted, where))
                throw preprocess_error{ where, "#embed expects \"FILENAME\" or <FILENAME>" };
        }
        // (its parameters: limit, prefix, suffix, if_empty (and their __x__ spellings))
        std::optional<std::uint64_t> limit;
        std::vector<pp_token> prefix, suffix, ifEmpty;
        while (index < operand.size())
        {
            if (operand[index].kind != token_kind::Identifier)
                throw preprocess_error{ operand[index].where, "invalid #embed parameter" };
            auto parameter = operand[index].text;
            if (parameter.size() > 4u && parameter.starts_with("__") && parameter.ends_with("__"))
                parameter = parameter.substr(2u, parameter.size() - 4u);
            ++index;
            if (index < operand.size() && operand[index].is("::"))
            {
                // (a vendor's parameter: ignored)
                index += 2u;
                parameter = "?";
            }
            std::vector<pp_token> clause;
            if (index < operand.size() && operand[index].is("("))
            {
                int depth = 1;
                for (++index; index < operand.size(); ++index)
                {
                    if (operand[index].is("("))
                        ++depth;
                    else if (operand[index].is(")") && --depth == 0)
                        break;
                    clause.push_back(operand[index]);
                }
                if (index >= operand.size())
                    throw preprocess_error{ where, "missing ')' in #embed parameter" };
                ++index;
            }
            if (parameter == "limit")
            {
                std::vector<pp_token> line;
                line.push_back(aLine[0]);
                line.push_back(aLine[0]);
                line.insert(line.end(), clause.begin(), clause.end());
                limit = evaluate_condition(aFile, line, 2u) ? 1u : 0u;
                // (the limit's value (evaluate_condition gives its truth): evaluated again for its value)
                list_reader r{ clause };
                std::vector<pp_token> expanded;
                expand(r, expanded, true);
                std::uint64_t value = 0u;
                if (expanded.size() == 1u && expanded[0].kind == token_kind::Number)
                    value = std::stoull(expanded[0].text, nullptr, 0);
                else if (*limit != 0u)
                    throw preprocess_error{ where, "#embed limit must be an integer constant" };
                limit = value;
            }
            else if (parameter == "prefix")
                prefix = clause;
            else if (parameter == "suffix")
                suffix = clause;
            else if (parameter == "if_empty")
                ifEmpty = clause;
            else if (parameter != "?")
                throw preprocess_error{ where, "unknown #embed parameter \"" + parameter + "\"" };
        }
        auto const path = find_include(name, quoted, aFile.folder);
        if (!path)
            throw preprocess_error{ where, name + ": no such file" };
        bool ok = false;
        auto const bytes = read_file(*path, ok);
        if (!ok)
            throw preprocess_error{ where, name + ": cannot be read" };
        auto const count = limit ? std::min<std::uint64_t>(*limit, bytes.size()) : bytes.size();
        if (count == 0u)
        {
            aResult = ifEmpty;
            return;
        }
        aResult = prefix;
        for (std::uint64_t i = 0u; i < count; ++i)
        {
            pp_token t;
            t.kind = token_kind::Number;
            t.text = std::to_string(static_cast<unsigned char>(bytes[i]));
            t.where = where;
            t.space = true;
            aResult.push_back(t);
            if (i + 1u < count)
            {
                pp_token comma;
                comma.kind = token_kind::Punctuator;
                comma.text = ",";
                comma.where = where;
                aResult.push_back(comma);
            }
        }
        aResult.insert(aResult.end(), suffix.begin(), suffix.end());
    }

    void preprocessor::line_directive(file_state& aFile, std::vector<pp_token> aLine, std::size_t aFirst)
    {
        auto const where = aLine[0].where;
        std::vector<pp_token> operand{ aLine.begin() + static_cast<std::ptrdiff_t>(aFirst), aLine.end() };
        if (operand.empty() || operand[0].kind != token_kind::Number)
        {
            list_reader r{ operand };
            std::vector<pp_token> expanded;
            expand(r, expanded, true);
            operand = expanded;
        }
        if (operand.empty() || operand[0].kind != token_kind::Number ||
            !std::all_of(operand[0].text.begin(), operand[0].text.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)); }))
            throw preprocess_error{ where, "#line expects a line number" };
        auto const line = std::stoll(operand[0].text);
        // (the next line's presumed line is the number)
        aFile.lineDelta = line - static_cast<std::int64_t>(where.line) - 1;
        if (operand.size() > 1u && operand[1].kind == token_kind::String)
            aFile.presumedName = operand[1].text.substr(1u, operand[1].text.size() - 2u);
    }

    void preprocessor::pragma(std::vector<pp_token> const& aLine, std::size_t aFirst, source_location const& aWhere)
    {
        // (pragmas: once; push_macro, pop_macro and pack (as GCC and MSVC have them); STDC's (FP_CONTRACT, FENV_ACCESS, CX_LIMITED_RANGE, 
        // FENV_ROUND, FENV_DEC_ROUND) and others are ignored)
        if (aLine.size() <= aFirst || aLine[aFirst].kind != token_kind::Identifier)
            return;
        auto const& name = aLine[aFirst].text;
        if (name == "once")
            iOnce.insert(iFiles[aWhere.file]);
        else if (name == "pack")
        {
            // (pack(n), pack(), pack(push[, identifier][, n]), pack(pop[, identifier | n]): the alignment limit of the members of the
            // structures and unions defined after it (0: none))
            std::vector<pp_token> operands;
            for (std::size_t i = aFirst + 1u; i < aLine.size(); ++i)
                if (!aLine[i].is("(") && !aLine[i].is(")") && !aLine[i].is(","))
                    operands.push_back(aLine[i]);
            auto const value = [&](pp_token const& aToken) -> std::optional<std::uint64_t>
            {
                if (aToken.kind != token_kind::Number)
                    return {};
                auto const n = std::strtoull(aToken.text.c_str(), nullptr, 0);
                if (n == 0u || n > 16u || (n & (n - 1u)) != 0u)
                {
                    warn(aToken.where, "alignment must be a small power of two, not " + aToken.text + " (#pragma pack ignored)");
                    return {};
                }
                return n;
            };
            if (operands.empty())
                iPack = 0u;
            else if (operands[0].kind == token_kind::Identifier && operands[0].text == "push")
            {
                iPackStack.push_back(iPack);
                if (operands.size() > 1u)
                    if (auto const n = value(operands.back()))
                        iPack = *n;
            }
            else if (operands[0].kind == token_kind::Identifier && operands[0].text == "pop")
            {
                if (!iPackStack.empty())
                {
                    iPack = iPackStack.back();
                    iPackStack.pop_back();
                }
            }
            else if (auto const n = value(operands[0]))
                iPack = *n;
        }
        else if ((name == "push_macro" || name == "pop_macro") && aLine.size() > aFirst + 3u && aLine[aFirst + 1u].is("(") &&
            aLine[aFirst + 2u].kind == token_kind::String)
        {
            auto const& literal = aLine[aFirst + 2u].text;
            auto const macroName = literal.substr(1u, literal.size() - 2u);
            auto& stack = iPushedMacros[macroName];
            if (name == "push_macro")
            {
                auto const existing = iMacros.find(macroName);
                stack.push_back(existing != iMacros.end() ? std::optional<macro>{ existing->second } : std::nullopt);
            }
            else if (!stack.empty())
            {
                if (stack.back())
                    iMacros[macroName] = *stack.back();
                else
                    iMacros.erase(macroName);
                stack.pop_back();
            }
        }
    }

    // --- #if

    bool preprocessor::evaluate_condition(file_state& aFile, std::vector<pp_token> const& aLine, std::size_t aFirst)
    {
        auto const where = aLine[0].where;
        // (defined, __has_include, __has_embed and __has_c_attribute: replaced before macro replacement)
        std::vector<pp_token> tokens;
        auto const number = [&](std::uint64_t aValue)
        {
            pp_token t;
            t.kind = token_kind::Number;
            t.text = std::to_string(aValue);
            t.where = where;
            t.space = true;
            return t;
        };
        auto const operator_operands = [&](std::vector<pp_token> const& aTokens, std::size_t& i, bool aParenthesized) -> std::vector<pp_token>
        {
            std::vector<pp_token> operand;
            if (aParenthesized)
            {
                if (i >= aTokens.size() || !aTokens[i].is("("))
                    throw preprocess_error{ where, "missing '(' after \"" + aTokens[i - 1u].text + "\"" };
                int depth = 1;
                for (++i; i < aTokens.size(); ++i)
                {
                    if (aTokens[i].is("("))
                        ++depth;
                    else if (aTokens[i].is(")") && --depth == 0)
                        break;
                    operand.push_back(aTokens[i]);
                }
                if (i >= aTokens.size())
                    throw preprocess_error{ where, "missing ')'" };
                ++i;
            }
            return operand;
        };
        std::function<void(std::vector<pp_token> const&, std::vector<pp_token>&)> special = [&](std::vector<pp_token> const& aTokens, std::vector<pp_token>& aOut)
        {
            for (std::size_t i = 0u; i < aTokens.size();)
            {
                auto const& t = aTokens[i];
                if (t.kind == token_kind::Identifier && t.text == "defined")
                {
                    ++i;
                    bool parenthesized = i < aTokens.size() && aTokens[i].is("(");
                    if (parenthesized)
                        ++i;
                    if (i >= aTokens.size() || aTokens[i].kind != token_kind::Identifier)
                        throw preprocess_error{ where, "\"defined\" without a macro name" };
                    aOut.push_back(number(is_defined(aTokens[i].text) ? 1u : 0u));
                    ++i;
                    if (parenthesized)
                    {
                        if (i >= aTokens.size() || !aTokens[i].is(")"))
                            throw preprocess_error{ where, "missing ')' after \"defined\"" };
                        ++i;
                    }
                    continue;
                }
                if (t.kind == token_kind::Identifier && (t.text == "__has_include" || t.text == "__has_include_next" || t.text == "__has_embed"))
                {
                    ++i;
                    auto operand = operator_operands(aTokens, i, true);
                    std::string name;
                    bool quoted = false;
                    std::size_t index = 0u;
                    if (!header_spelling(operand, index, name, quoted, where))
                    {
                        list_reader r{ operand };
                        std::vector<pp_token> expanded;
                        expand(r, expanded, true);
                        operand = expanded;
                        index = 0u;
                        if (!header_spelling(operand, index, name, quoted, where))
                            throw preprocess_error{ where, t.text + " expects \"FILENAME\" or <FILENAME>" };
                    }
                    auto const path = find_include(name, quoted, aFile.folder, t.text == "__has_include_next");
                    if (t.text == "__has_embed")
                    {
                        // (__STDC_EMBED_NOT_FOUND__ 0, __STDC_EMBED_FOUND__ 1, __STDC_EMBED_EMPTY__ 2)
                        std::uint64_t result = 0u;
                        if (path)
                        {
                            std::error_code ec;
                            auto const size = std::filesystem::file_size(*path, ec);
                            bool limitZero = false;
                            for (std::size_t j = index; j + 2u < operand.size(); ++j)
                                if (operand[j].kind == token_kind::Identifier && (operand[j].text == "limit" || operand[j].text == "__limit__") &&
                                    operand[j + 1u].is("(") && operand[j + 2u].text == "0")
                                    limitZero = true;
                            result = (!ec && size == 0u) || limitZero ? 2u : 1u;
                            for (std::size_t j = index; j < operand.size(); ++j)
                                if (operand[j].kind == token_kind::Identifier)
                                {
                                    auto p = operand[j].text;
                                    if (p.size() > 4u && p.starts_with("__") && p.ends_with("__"))
                                        p = p.substr(2u, p.size() - 4u);
                                    if (p != "limit" && p != "prefix" && p != "suffix" && p != "if_empty" && (j == 0u || !operand[j - 1u].is("(")))
                                        result = 0u;
                                    // (skip a parameter's clause)
                                    if (j + 1u < operand.size() && operand[j + 1u].is("("))
                                    {
                                        int depth = 0;
                                        for (++j; j < operand.size(); ++j)
                                        {
                                            if (operand[j].is("("))
                                                ++depth;
                                            else if (operand[j].is(")") && --depth == 0)
                                                break;
                                        }
                                    }
                                }
                        }
                        aOut.push_back(number(result));
                    }
                    else
                        aOut.push_back(number(path ? 1u : 0u));
                    continue;
                }
                if (t.kind == token_kind::Identifier && (t.text == "__has_c_attribute" || t.text == "__has_attribute"))
                {
                    // (GNU's __has_attribute: of its attributes, those that the compiler implements (see gnu_attributes))
                    bool const gnu = t.text == "__has_attribute";
                    ++i;
                    auto const operand = operator_operands(aTokens, i, true);
                    std::string attribute;
                    for (auto const& o : operand)
                        attribute += o.text;
                    auto const unwrap = [](std::string& aName)
                    {
                        if (aName.size() > 4u && aName.starts_with("__") && aName.ends_with("__"))
                            aName = aName.substr(2u, aName.size() - 4u);
                    };
                    if (attribute.starts_with("gnu::") || attribute.starts_with("__gnu__::"))
                    {
                        attribute = attribute.substr(attribute.find("::") + 2u);
                        unwrap(attribute);
                        attribute = "gnu::" + attribute;
                    }
                    else
                    {
                        unwrap(attribute);
                        if (gnu)
                            attribute = "gnu::" + attribute;
                    }
                    static std::map<std::string, std::uint64_t> const sAttributes =
                    {
                        { "deprecated", 201904u }, { "fallthrough", 201904u }, { "maybe_unused", 201904u }, { "nodiscard", 202003u },
                        { "noreturn", 202202u }, { "_Noreturn", 202202u }, { "unsequenced", 202207u }, { "reproducible", 202207u },
                        { "gnu::aligned", 1u }, { "gnu::packed", 1u }, { "gnu::constructor", 1u }, { "gnu::destructor", 1u },
                        { "gnu::cleanup", 1u }, { "gnu::noreturn", 1u }
                    };
                    auto const known = sAttributes.find(attribute);
                    aOut.push_back(number(known != sAttributes.end() ? known->second : 0u));
                    continue;
                }
                aOut.push_back(t);
                ++i;
            }
        };
        std::vector<pp_token> line{ aLine.begin() + static_cast<std::ptrdiff_t>(aFirst), aLine.end() };
        special(line, tokens);
        // (macro replaced (with "defined" that replacement produces evaluated too), then evaluated: identifiers that remain are 0, but 
        // true and false are 1 and 0)
        {
            list_reader r{ tokens };
            std::vector<pp_token> expanded;
            expand(r, expanded, true);
            tokens.clear();
            special(expanded, tokens);
        }
        if (tokens.empty())
            throw preprocess_error{ where, "#if with no expression" };
        std::size_t position = 0u;
        auto const peek = [&]() -> pp_token const* { return position < tokens.size() ? &tokens[position] : nullptr; };
        auto const error = [&](std::string const& aMessage) -> preprocess_error
        {
            return preprocess_error{ position < tokens.size() ? tokens[position].where : where, aMessage };
        };
        std::function<pp_value(bool)> conditional_expression;
        auto const arithmetic = [](pp_value& a, pp_value& b) { bool const u = a.isUnsigned || b.isUnsigned; a.isUnsigned = u; b.isUnsigned = u; };
        // (a value's evaluation is skipped (aEvaluate false) in the operand of && || ?: that is not evaluated: no division by zero error)
        std::function<pp_value(bool)> primary = [&](bool aEvaluate) -> pp_value
        {
            auto const* t = peek();
            if (t == nullptr)
                throw error("missing operand in #if expression");
            if (t->is("("))
            {
                ++position;
                auto const v = conditional_expression(aEvaluate);
                if (peek() == nullptr || !peek()->is(")"))
                    throw error("missing ')' in #if expression");
                ++position;
                return v;
            }
            if (t->is("-") || t->is("+") || t->is("~") || t->is("!"))
            {
                auto const op = t->text;
                ++position;
                auto v = primary(aEvaluate);
                if (op == "-")
                    v.value = static_cast<std::uint64_t>(0u - v.value);
                else if (op == "~")
                    v.value = ~v.value;
                else if (op == "!")
                {
                    v.value = v.value == 0u ? 1u : 0u;
                    v.isUnsigned = false;
                }
                return v;
            }
            ++position;
            if (t->kind == token_kind::Identifier)
            {
                if (t->text == "true")
                    return pp_value{ 1u, false };
                return pp_value{ 0u, false };
            }
            if (t->kind == token_kind::Number)
            {
                std::string text;
                for (char c : t->text)
                    if (c != '\'')
                        text += c;
                std::size_t end = 0u;
                std::uint64_t value = 0u;
                int base = 10;
                std::size_t start = 0u;
                if (text.size() > 2u && text[0] == '0' && (text[1] == 'x' || text[1] == 'X'))
                {
                    base = 16;
                    start = 2u;
                }
                else if (text.size() > 2u && text[0] == '0' && (text[1] == 'b' || text[1] == 'B'))
                {
                    base = 2;
                    start = 2u;
                }
                else if (text.size() > 1u && text[0] == '0')
                    base = 8;
                end = start;
                bool overflow = false;
                while (end < text.size())
                {
                    int digit = -1;
                    char const c = text[end];
                    if (c >= '0' && c <= '9')
                        digit = c - '0';
                    else if (c >= 'a' && c <= 'f')
                        digit = c - 'a' + 10;
                    else if (c >= 'A' && c <= 'F')
                        digit = c - 'A' + 10;
                    if (digit < 0 || digit >= base)
                        break;
                    auto const next = value * static_cast<std::uint64_t>(base) + static_cast<std::uint64_t>(digit);
                    if (next / static_cast<std::uint64_t>(base) != value)
                        overflow = true;
                    value = next;
                    ++end;
                }
                if (end == start && base != 8)
                    throw error("invalid integer constant \"" + t->text + "\" in #if expression");
                auto suffix = text.substr(end);
                for (auto& c : suffix)
                    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                if (suffix.find('.') != std::string::npos || (base != 16 && suffix.find('e') != std::string::npos) || suffix.find('p') != std::string::npos)
                    throw error("floating constant in #if expression");
                bool isUnsigned = suffix.find('u') != std::string::npos;
                std::string rest;
                for (char c : suffix)
                    if (c != 'u')
                        rest += c;
                if (!rest.empty() && rest != "l" && rest != "ll" && rest != "wb" && rest != "z")
                    throw error("invalid suffix \"" + suffix + "\" on integer constant");
                if (overflow)
                    warn(t->where, "integer constant is too large");
                // (a constant too large for intmax_t is uintmax_t (as other implementations do))
                if (!isUnsigned && value > static_cast<std::uint64_t>(INT64_MAX) && base != 10)
                    isUnsigned = true;
                return pp_value{ value, isUnsigned };
            }
            if (t->kind == token_kind::Character)
            {
                // (a character constant: its value as the compiler gives it (an int: a plain char is signed))
                auto text = t->text;
                bool wide = false;
                if (text[0] != '\'')
                {
                    wide = text[0] != 'u' || text.size() < 2u || text[1] != '8';
                    text = text.substr(text.find('\''));
                }
                std::string body = text.substr(1u, text.size() - 2u);
                std::int64_t value = 0;
                std::size_t k = 0u;
                int count = 0;
                while (k < body.size())
                {
                    std::uint32_t c = static_cast<unsigned char>(body[k++]);
                    if (c == '\\' && k < body.size())
                    {
                        char const e = body[k++];
                        switch (e)
                        {
                        case 'n': c = '\n'; break;
                        case 't': c = '\t'; break;
                        case 'r': c = '\r'; break;
                        case 'a': c = '\a'; break;
                        case 'b': c = '\b'; break;
                        case 'f': c = '\f'; break;
                        case 'v': c = '\v'; break;
                        case 'x':
                            c = 0u;
                            while (k < body.size() && std::isxdigit(static_cast<unsigned char>(body[k])))
                                c = c * 16u + static_cast<std::uint32_t>(std::isdigit(static_cast<unsigned char>(body[k])) ? body[k] - '0' : (std::tolower(body[k]) - 'a' + 10)), ++k;
                            break;
                        default:
                            if (e >= '0' && e <= '7')
                            {
                                c = static_cast<std::uint32_t>(e - '0');
                                for (int d = 0; d < 2 && k < body.size() && body[k] >= '0' && body[k] <= '7'; ++d)
                                    c = c * 8u + static_cast<std::uint32_t>(body[k++] - '0');
                            }
                            else
                                c = static_cast<unsigned char>(e);
                            break;
                        }
                    }
                    else if (c >= 0xC0u && wide)
                    {
                        // (a UTF-8 sequence: its code point)
                        int extra = c >= 0xF0u ? 3 : c >= 0xE0u ? 2 : 1;
                        c &= (0x3Fu >> extra);
                        while (extra-- > 0 && k < body.size())
                            c = (c << 6u) | (static_cast<unsigned char>(body[k++]) & 0x3Fu);
                    }
                    value = wide ? static_cast<std::int64_t>(c) : (value << 8) | static_cast<std::int64_t>(c & 0xFFu);
                    ++count;
                }
                if (!wide && count == 1)
                    value = static_cast<std::int8_t>(value);
                return pp_value{ static_cast<std::uint64_t>(value), false };
            }
            throw error("invalid token \"" + t->text + "\" in #if expression");
        };
        struct binary_operator
        {
            char const* text;
            int precedence;
        };
        static binary_operator const sOperators[] =
        {
            { "*", 10 }, { "/", 10 }, { "%", 10 }, { "+", 9 }, { "-", 9 }, { "<<", 8 }, { ">>", 8 }, { "<", 7 }, { ">", 7 }, { "<=", 7 }, { ">=", 7 },
            { "==", 6 }, { "!=", 6 }, { "&", 5 }, { "^", 4 }, { "|", 3 }, { "&&", 2 }, { "||", 1 }
        };
        std::function<pp_value(int, bool)> binary = [&](int aPrecedence, bool aEvaluate) -> pp_value
        {
            auto left = primary(aEvaluate);
            for (;;)
            {
                auto const* t = peek();
                if (t == nullptr || t->kind != token_kind::Punctuator)
                    return left;
                int precedence = 0;
                for (auto const& o : sOperators)
                    if (t->text == o.text)
                        precedence = o.precedence;
                if (precedence == 0 || precedence < aPrecedence)
                    return left;
                auto const op = t->text;
                ++position;
                if (op == "&&" || op == "||")
                {
                    bool const shortCircuit = op == "&&" ? left.value == 0u : left.value != 0u;
                    auto right = binary(precedence + 1, aEvaluate && !shortCircuit);
                    left = pp_value{ op == "&&" ? (left.value != 0u && right.value != 0u ? 1u : 0u) : (left.value != 0u || right.value != 0u ? 1u : 0u), false };
                    continue;
                }
                auto right = binary(precedence + 1, aEvaluate);
                if (op != "<<" && op != ">>")
                    arithmetic(left, right);
                bool const u = left.isUnsigned;
                if (op == "*") left.value = left.value * right.value;
                else if (op == "/" || op == "%")
                {
                    if (right.value == 0u)
                    {
                        if (aEvaluate)
                            throw error("division by zero in #if expression");
                        left.value = 0u;
                    }
                    else if (u)
                        left.value = op == "/" ? left.value / right.value : left.value % right.value;
                    else if (left.s() == INT64_MIN && right.s() == -1)
                        left.value = op == "/" ? left.value : 0u;
                    else
                        left.value = static_cast<std::uint64_t>(op == "/" ? left.s() / right.s() : left.s() % right.s());
                }
                else if (op == "+") left.value = left.value + right.value;
                else if (op == "-") left.value = left.value - right.value;
                else if (op == "<<") left.value = right.value >= 64u ? 0u : left.value << right.value;
                else if (op == ">>") left.value = right.value >= 64u ? (u || left.s() >= 0 ? 0u : ~std::uint64_t{}) : (u ? left.value >> right.value : static_cast<std::uint64_t>(left.s() >> right.value));
                else if (op == "&") left.value &= right.value;
                else if (op == "^") left.value ^= right.value;
                else if (op == "|") left.value |= right.value;
                else
                {
                    bool r = false;
                    if (op == "==") r = left.value == right.value;
                    else if (op == "!=") r = left.value != right.value;
                    else if (u)
                        r = op == "<" ? left.value < right.value : op == ">" ? left.value > right.value : op == "<=" ? left.value <= right.value : left.value >= right.value;
                    else
                        r = op == "<" ? left.s() < right.s() : op == ">" ? left.s() > right.s() : op == "<=" ? left.s() <= right.s() : left.s() >= right.s();
                    left = pp_value{ r ? 1u : 0u, false };
                }
            }
        };
        conditional_expression = [&](bool aEvaluate) -> pp_value
        {
            auto condition = binary(1, aEvaluate);
            if (peek() != nullptr && peek()->is("?"))
            {
                ++position;
                auto a = conditional_expression(aEvaluate && condition.value != 0u);
                if (peek() == nullptr || !peek()->is(":"))
                    throw error("missing ':' in #if expression");
                ++position;
                auto b = conditional_expression(aEvaluate && condition.value == 0u);
                arithmetic(a, b);
                return condition.value != 0u ? a : b;
            }
            return condition;
        };
        auto const result = conditional_expression(true);
        while (peek() != nullptr && peek()->is(","))
        {
            // (a comma expression (not allowed by the standard in a constant expression; accepted, as GCC does))
            ++position;
            return conditional_expression(true).value != 0u;
        }
        if (peek() != nullptr)
            throw error("invalid token \"" + peek()->text + "\" in #if expression");
        return result.value != 0u;
    }

    // --- hide sets

    std::uint32_t preprocessor::intern(std::vector<std::uint32_t> aSet)
    {
        auto const existing = iHideSetIndex.find(aSet);
        if (existing != iHideSetIndex.end())
            return existing->second;
        auto const index = static_cast<std::uint32_t>(iHideSets.size());
        iHideSets.push_back(aSet);
        iHideSetIndex[std::move(aSet)] = index;
        return index;
    }

    bool preprocessor::hidden(std::uint32_t aSet, std::uint32_t aMacro) const
    {
        auto const& set = iHideSets[aSet];
        return std::binary_search(set.begin(), set.end(), aMacro);
    }

    std::uint32_t preprocessor::hide_set_add(std::uint32_t aSet, std::uint32_t aMacro)
    {
        if (hidden(aSet, aMacro))
            return aSet;
        auto set = iHideSets[aSet];
        set.insert(std::lower_bound(set.begin(), set.end(), aMacro), aMacro);
        return intern(std::move(set));
    }

    std::uint32_t preprocessor::hide_set_union(std::uint32_t aA, std::uint32_t aB)
    {
        if (aA == aB || aB == 0u)
            return aA;
        if (aA == 0u)
            return aB;
        auto const key = std::make_pair(std::min(aA, aB), std::max(aA, aB));
        auto const existing = iHideSetUnions.find(key);
        if (existing != iHideSetUnions.end())
            return existing->second;
        std::vector<std::uint32_t> set;
        std::set_union(iHideSets[aA].begin(), iHideSets[aA].end(), iHideSets[aB].begin(), iHideSets[aB].end(), std::back_inserter(set));
        auto const result = intern(std::move(set));
        iHideSetUnions[key] = result;
        return result;
    }

    std::uint32_t preprocessor::hide_set_intersection(std::uint32_t aA, std::uint32_t aB)
    {
        if (aA == aB)
            return aA;
        if (aA == 0u || aB == 0u)
            return 0u;
        std::vector<std::uint32_t> set;
        std::set_intersection(iHideSets[aA].begin(), iHideSets[aA].end(), iHideSets[aB].begin(), iHideSets[aB].end(), std::back_inserter(set));
        return intern(std::move(set));
    }

    // --- macro replacement

    pp_token preprocessor::stringize(std::vector<pp_token> const& aTokens, source_location const& aWhere) const
    {
        std::string text = "\"";
        bool first = true;
        for (auto const& t : aTokens)
        {
            if (t.kind == token_kind::Placemarker)
                continue;
            if (!first && t.space)
                text += ' ';
            first = false;
            auto const spelling = spelling_of(t);
            if (t.kind == token_kind::String || t.kind == token_kind::Character)
            {
                for (char c : spelling)
                {
                    if (c == '"' || c == '\\')
                        text += '\\';
                    text += c;
                }
            }
            else
                text += spelling;
        }
        text += '"';
        pp_token result;
        result.kind = token_kind::String;
        result.text = text;
        result.where = aWhere;
        return result;
    }

    pp_token preprocessor::paste(pp_token const& aLeft, pp_token const& aRight)
    {
        if (aLeft.kind == token_kind::Placemarker)
            return aRight;
        if (aRight.kind == token_kind::Placemarker)
            return aLeft;
        auto const text = spelling_of(aLeft) + spelling_of(aRight);
        auto tokens = tokenize(text, aLeft.where.file);
        if (tokens.size() != 1u)
            throw preprocess_error{ aLeft.where, "pasting \"" + spelling_of(aLeft) + "\" and \"" + spelling_of(aRight) + "\" does not give a valid preprocessing token" };
        auto result = tokens[0];
        result.where = aLeft.where;
        result.space = aLeft.space;
        result.lineStart = false;
        result.hideSet = 0u;
        return result;
    }

    std::vector<pp_token> preprocessor::expanded_argument(std::vector<pp_token> const& aArgument)
    {
        list_reader r{ aArgument };
        std::vector<pp_token> result;
        expand(r, result);
        return result;
    }

    std::vector<pp_token> preprocessor::substitute(macro const& aMacro, std::vector<std::vector<pp_token>> const& aArguments, std::uint32_t aHideSet,
        source_location const& aWhere)
    {
        auto const parameter_index = [&](pp_token const& aToken) -> int
        {
            if (aToken.kind != token_kind::Identifier)
                return -1;
            for (std::size_t i = 0u; i < aMacro.parameters.size(); ++i)
                if (aMacro.parameters[i] == aToken.text)
                    return static_cast<int>(i);
            return -1;
        };
        std::vector<std::vector<pp_token>> expandedArguments(aArguments.size());
        std::vector<bool> expandedDone(aArguments.size(), false);
        auto const expanded = [&](int aIndex) -> std::vector<pp_token> const&
        {
            if (!expandedDone[static_cast<std::size_t>(aIndex)])
            {
                expandedArguments[static_cast<std::size_t>(aIndex)] = expanded_argument(aArguments[static_cast<std::size_t>(aIndex)]);
                expandedDone[static_cast<std::size_t>(aIndex)] = true;
            }
            return expandedArguments[static_cast<std::size_t>(aIndex)];
        };
        // (__VA_OPT__'s content is replaced if the variable arguments, macro replaced, are not empty)
        bool const variadicPresent = aMacro.variadic && !aArguments.empty() && !expanded(static_cast<int>(aArguments.size()) - 1).empty();
        // (the replacement of a list of the body's tokens (the body, or a __VA_OPT__'s content))
        std::function<std::vector<pp_token>(std::vector<pp_token> const&)> replace = [&](std::vector<pp_token> const& aBody) -> std::vector<pp_token>
        {
            std::vector<pp_token> output;
            // (__VA_OPT__ ( content ): its content's tokens (in aBody, from aIndex at the '__VA_OPT__'); aEnd: after its ')')
            auto const va_opt = [&](std::size_t aIndex, std::size_t& aEnd) -> std::vector<pp_token>
            {
                std::size_t i = aIndex + 1u;
                if (i >= aBody.size() || !aBody[i].is("("))
                    throw preprocess_error{ aBody[aIndex].where, "__VA_OPT__ must be followed by '('" };
                int depth = 1;
                std::vector<pp_token> content;
                for (++i; i < aBody.size(); ++i)
                {
                    if (aBody[i].is("("))
                        ++depth;
                    else if (aBody[i].is(")") && --depth == 0)
                        break;
                    content.push_back(aBody[i]);
                }
                if (i >= aBody.size())
                    throw preprocess_error{ aBody[aIndex].where, "unterminated __VA_OPT__" };
                aEnd = i + 1u;
                if (!variadicPresent)
                {
                    pp_token placemarker;
                    placemarker.kind = token_kind::Placemarker;
                    placemarker.where = aBody[aIndex].where;
                    return { placemarker };
                }
                auto result = replace(content);
                if (result.empty())
                {
                    pp_token placemarker;
                    placemarker.kind = token_kind::Placemarker;
                    placemarker.where = aBody[aIndex].where;
                    result.push_back(placemarker);
                }
                else
                    result.front().space = aBody[aIndex].space;
                return result;
            };
            auto const raw = [&](int aIndex, pp_token const& aAt) -> std::vector<pp_token>
            {
                auto result = aArguments[static_cast<std::size_t>(aIndex)];
                if (result.empty())
                {
                    pp_token placemarker;
                    placemarker.kind = token_kind::Placemarker;
                    placemarker.where = aAt.where;
                    result.push_back(placemarker);
                }
                else
                    result.front().space = aAt.space;
                return result;
            };
            for (std::size_t i = 0u; i < aBody.size();)
            {
                auto const& t = aBody[i];
                // # parameter
                if (aMacro.function && t.is("#") && i + 1u < aBody.size())
                {
                    if (aBody[i + 1u].kind == token_kind::Identifier && aBody[i + 1u].text == "__VA_OPT__")
                    {
                        std::size_t end = 0u;
                        auto content = va_opt(i + 1u, end);
                        auto s = stringize(content, t.where);
                        s.space = t.space;
                        output.push_back(s);
                        i = end;
                        continue;
                    }
                    auto const p = parameter_index(aBody[i + 1u]);
                    if (p >= 0)
                    {
                        auto s = stringize(aArguments[static_cast<std::size_t>(p)], t.where);
                        s.space = t.space;
                        output.push_back(s);
                        i += 2u;
                        continue;
                    }
                }
                // x ## y
                if (t.is("##") && i + 1u < aBody.size())
                {
                    std::vector<pp_token> right;
                    std::size_t next = i + 2u;
                    auto const& r = aBody[i + 1u];
                    if (auto const p = parameter_index(r); p >= 0)
                        right = raw(p, r);
                    else if (r.kind == token_kind::Identifier && r.text == "__VA_OPT__")
                        right = va_opt(i + 1u, next);
                    else
                        right = { r };
                    if (output.empty())
                        output.insert(output.end(), right.begin(), right.end());
                    else
                    {
                        auto left = output.back();
                        output.pop_back();
                        auto pasted = paste(left, right.front());
                        output.push_back(pasted);
                        output.insert(output.end(), right.begin() + 1, right.end());
                    }
                    i = next;
                    continue;
                }
                if (t.kind == token_kind::Identifier && t.text == "__VA_OPT__" && aMacro.variadic)
                {
                    std::size_t end = 0u;
                    auto content = va_opt(i, end);
                    output.insert(output.end(), content.begin(), content.end());
                    i = end;
                    continue;
                }
                if (auto const p = parameter_index(t); p >= 0)
                {
                    // (an operand of ##: the argument as it is; else the argument macro replaced)
                    if (i + 1u < aBody.size() && aBody[i + 1u].is("##"))
                    {
                        auto a = raw(p, t);
                        output.insert(output.end(), a.begin(), a.end());
                    }
                    else
                    {
                        auto a = expanded(p);
                        if (!a.empty())
                            a.front().space = t.space;
                        output.insert(output.end(), a.begin(), a.end());
                    }
                    ++i;
                    continue;
                }
                output.push_back(t);
                ++i;
            }
            return output;
        };
        auto result = replace(aMacro.body);
        std::vector<pp_token> final;
        final.reserve(result.size());
        for (auto& t : result)
        {
            if (t.kind == token_kind::Placemarker)
                continue;
            t.hideSet = hide_set_union(t.hideSet, aHideSet);
            t.where = aWhere;
            t.lineStart = false;
            final.push_back(std::move(t));
        }
        return final;
    }

    bool preprocessor::builtin_macro(pp_token const& aToken, reader& aReader, std::vector<pp_token>& aResult)
    {
        auto const& name = aToken.text;
        auto const make = [&](token_kind aKind, std::string const& aText)
        {
            pp_token t;
            t.kind = aKind;
            t.text = aText;
            t.where = aToken.where;
            t.space = aToken.space;
            aResult.push_back(t);
        };
        if (name == "__LINE__")
            make(token_kind::Number, std::to_string(presumed_line(aToken.where)));
        else if (name == "__FILE__")
        {
            std::string text = "\"";
            for (char c : presumed_name(aToken.where))
            {
                if (c == '"' || c == '\\')
                    text += '\\';
                text += c;
            }
            make(token_kind::String, text + "\"");
        }
        else if (name == "__DATE__")
            make(token_kind::String, iDate);
        else if (name == "__TIME__")
            make(token_kind::String, iTime);
        else if (name == "__COUNTER__")
            make(token_kind::Number, std::to_string(iCounter++));
        else if (name == "_Pragma")
        {
            // (the _Pragma operator: _Pragma ( string-literal ), destringized and processed as a #pragma's tokens)
            pp_token open, literal, close;
            if (!aReader.next(open) || !open.is("(") || !aReader.next(literal) || literal.kind != token_kind::String || !aReader.next(close) || !close.is(")"))
                throw preprocess_error{ aToken.where, "_Pragma takes a parenthesized string literal" };
            std::string text = literal.text.substr(literal.text.find('"') + 1u);
            text.pop_back();
            std::string destringized;
            for (std::size_t i = 0u; i < text.size(); ++i)
            {
                if (text[i] == '\\' && i + 1u < text.size() && (text[i + 1u] == '"' || text[i + 1u] == '\\'))
                    ++i;
                destringized += text[i];
            }
            auto tokens = tokenize(destringized, aToken.where.file);
            std::vector<pp_token> line(2u);
            line.insert(line.end(), tokens.begin(), tokens.end());
            pragma(line, 2u, aToken.where);
        }
        else
            return false;
        return true;
    }

    std::vector<pp_token> preprocessor::gnu_attributes(std::vector<pp_token> const& aArgument, pp_token const& aWhere) const
    {
        // (GNU's __attribute__((a, b(...), ...)): the attributes that the compiler implements, as C23's [[gnu::a, gnu::b(...)]] (their
        // arguments replaced when the result is examined again); the others are dropped (they may be where C23 allows no attributes))
        static std::set<std::string> const sImplemented = { "aligned", "packed", "constructor", "destructor", "cleanup", "noreturn" };
        std::vector<pp_token> result;
        if (aArgument.size() < 2u || !aArgument.front().is("(") || !aArgument.back().is(")"))
            return result;
        auto const make = [&](token_kind aKind, std::string const& aText)
        {
            pp_token t;
            t.kind = aKind;
            t.text = aText;
            t.where = aWhere.where;
            t.space = true;
            t.noExpand = aKind == token_kind::Identifier;
            return t;
        };
        std::size_t i = 1u;
        std::size_t const end = aArgument.size() - 1u;
        while (i < end)
        {
            // (one attribute: its name, then its arguments up to the next comma outside parentheses)
            std::size_t next = i;
            int depth = 0;
            for (; next < end; ++next)
            {
                if (aArgument[next].is("("))
                    ++depth;
                else if (aArgument[next].is(")"))
                    --depth;
                else if (aArgument[next].is(",") && depth == 0)
                    break;
            }
            if (aArgument[i].kind == token_kind::Identifier)
            {
                auto name = aArgument[i].text;
                if (name.size() > 4u && name.starts_with("__") && name.ends_with("__"))
                    name = name.substr(2u, name.size() - 4u);
                if (sImplemented.contains(name))
                {
                    result.push_back(make(token_kind::Punctuator, result.empty() ? "[" : ","));
                    if (result.size() == 1u)
                        result.push_back(make(token_kind::Punctuator, "["));
                    result.push_back(make(token_kind::Identifier, "gnu"));
                    result.push_back(make(token_kind::Punctuator, "::"));
                    result.push_back(make(token_kind::Identifier, name));
                    for (std::size_t k = i + 1u; k < next; ++k)
                        result.push_back(aArgument[k]);
                }
            }
            i = next + 1u;
        }
        if (!result.empty())
        {
            result.push_back(make(token_kind::Punctuator, "]"));
            result.push_back(make(token_kind::Punctuator, "]"));
            result.front().space = aWhere.space;
        }
        return result;
    }

    void preprocessor::expand(reader& aReader, std::vector<pp_token>& aOutput, bool aDirective)
    {
        pp_token t;
        while (aReader.next(t))
        {
            if (t.kind != token_kind::Identifier || t.noExpand)
            {
                aOutput.push_back(std::move(t));
                continue;
            }
            // ("defined" in a #if: its operand is not replaced)
            if (aDirective && t.text == "defined")
            {
                aOutput.push_back(t);
                pp_token u;
                if (aReader.next(u))
                {
                    bool const parenthesized = u.is("(");
                    aOutput.push_back(u);
                    if (parenthesized && aReader.next(u))
                        aOutput.push_back(u);
                }
                continue;
            }
            auto const found = iMacros.find(t.text);
            // (__attribute__ is GNU's keyword: #undef does not remove it)
            macro const* const pm = found != iMacros.end() ? &found->second : 
                t.text == "__attribute__" || t.text == "__attribute" ? &iAttributeMacro : nullptr;
            if (pm == nullptr)
            {
                // (#pragma pack: a structure or union specifier's members' alignment limited (by an attribute after the keyword))
                if (iPack != 0u && !aDirective && (t.text == "struct" || t.text == "union"))
                {
                    auto const where = t.where;
                    aOutput.push_back(std::move(t));
                    for (auto const* text : { "[", "[", "neos", "::", "pack", "(", "#", ")", "]", "]" })
                    {
                        pp_token a;
                        a.kind = std::isalpha(static_cast<unsigned char>(text[0])) ? token_kind::Identifier : token_kind::Punctuator;
                        a.text = text;
                        if (a.text == "#")
                        {
                            a.kind = token_kind::Number;
                            a.text = std::to_string(iPack);
                        }
                        a.where = where;
                        a.space = true;
                        a.noExpand = a.kind == token_kind::Identifier;
                        aOutput.push_back(std::move(a));
                    }
                    continue;
                }
                aOutput.push_back(std::move(t));
                continue;
            }
            auto const& m = *pm;
            if (hidden(t.hideSet, m.id))
            {
                t.noExpand = true;
                aOutput.push_back(std::move(t));
                continue;
            }
            if (m.builtin)
            {
                std::vector<pp_token> result;
                if (builtin_macro(t, aReader, result))
                {
                    aOutput.insert(aOutput.end(), result.begin(), result.end());
                    continue;
                }
            }
            if (!m.function)
            {
                auto body = substitute(m, {}, hide_set_add(t.hideSet, m.id), t.where);
                if (!body.empty())
                    body.front().space = t.space;
                aReader.push(body);
                continue;
            }
            // (a function-like macro's name not followed by '(' is not an invocation)
            pp_token open;
            if (!aReader.next(open))
            {
                aOutput.push_back(std::move(t));
                continue;
            }
            if (!open.is("("))
            {
                aReader.unget(open);
                aOutput.push_back(std::move(t));
                continue;
            }
            // (its arguments: separated by commas outside parentheses; a variadic macro's last takes the rest, commas and all)
            std::vector<std::vector<pp_token>> arguments(1u);
            int depth = 0;
            pp_token u;
            bool closed = false;
            auto const named = m.parameters.size();
            while (aReader.next(u))
            {
                if (u.is("("))
                    ++depth;
                else if (u.is(")"))
                {
                    if (depth == 0)
                    {
                        closed = true;
                        break;
                    }
                    --depth;
                }
                else if (u.is(",") && depth == 0 && !(m.variadic && arguments.size() == named))
                {
                    arguments.emplace_back();
                    continue;
                }
                u.lineStart = false;
                if (arguments.back().empty())
                    u.space = false;
                arguments.back().push_back(u);
            }
            if (!closed)
                throw preprocess_error{ t.where, "unterminated argument list invoking macro \"" + m.name + "\"" };
            // (no parameters: one empty argument; a variadic macro's variable arguments may be absent)
            if (named == 0u && arguments.size() == 1u && arguments[0].empty())
                arguments.clear();
            if (m.variadic && arguments.size() + 1u == named)
                arguments.emplace_back();
            if (arguments.size() != named)
                throw preprocess_error{ t.where, "macro \"" + m.name + "\" takes " + std::to_string(m.variadic ? named - 1u : named) +
                    (m.variadic ? " or more" : "") + " arguments, but " + std::to_string(arguments.size()) + " given" };
            if (m.builtin)
            {
                // (__attribute__)
                auto body = gnu_attributes(arguments[0], t);
                aReader.push(body);
                continue;
            }
            auto const hideSet = hide_set_add(hide_set_intersection(t.hideSet, u.hideSet), m.id);
            auto body = substitute(m, arguments, hideSet, t.where);
            if (!body.empty())
                body.front().space = t.space;
            aReader.push(body);
        }
    }

    // --- typedef names: the identifiers that are typedef names where they are used (the scopes that declarations have decide)

    void preprocessor::mark_typedef_names(std::vector<pp_token>& aTokens)
    {
        // (a scope's names: true for a typedef name, false for an ordinary identifier that hides one)
        std::vector<std::unordered_map<std::string, bool>> scopes(1u);
        auto const lookup = [&](std::string const& aName) -> bool
        {
            for (auto i = scopes.rbegin(); i != scopes.rend(); ++i)
            {
                auto const found = i->find(aName);
                if (found != i->end())
                    return found->second;
            }
            return false;
        };
        auto const declare = [&](std::string const& aName, bool aTypedef)
        {
            scopes.back()[aName] = aTypedef;
        };
        std::size_t const n = aTokens.size();
        auto const is = [&](std::size_t i, char const* aText) { return i < n && aTokens[i].is(aText); };
        auto const word = [&](std::size_t i, char const* aText) { return i < n && aTokens[i].kind == token_kind::Identifier && aTokens[i].text == aText; };
        static std::set<std::string> const sTypeKeywords =
        {
            "void", "char", "short", "int", "long", "float", "double", "signed", "unsigned", "bool", "_Bool", "_Complex", "_Imaginary",
            "_Decimal32", "_Decimal64", "_Decimal128", "_BitInt", "struct", "union", "enum", "typeof", "typeof_unqual"
        };
        static std::set<std::string> const sOtherSpecifiers =
        {
            "auto", "constexpr", "extern", "register", "static", "thread_local", "_Thread_local", "typedef", "const", "restrict", "volatile",
            "_Atomic", "inline", "_Noreturn", "alignas", "_Alignas"
        };
        static std::set<std::string> const sKeywords =
        {
            "alignas", "alignof", "auto", "bool", "break", "case", "char", "const", "constexpr", "continue", "default", "do", "double", "else",
            "enum", "extern", "false", "float", "for", "goto", "if", "inline", "int", "long", "nullptr", "register", "restrict", "return", "short",
            "signed", "sizeof", "static", "static_assert", "struct", "switch", "thread_local", "true", "typedef", "typeof", "typeof_unqual",
            "union", "unsigned", "void", "volatile", "while", "_Alignas", "_Alignof", "_Atomic", "_BitInt", "_Bool", "_Complex", "_Decimal128",
            "_Decimal32", "_Decimal64", "_Generic", "_Imaginary", "_Noreturn", "_Static_assert", "_Thread_local"
        };
        auto const plain_identifier = [&](std::size_t i) { return i < n && aTokens[i].kind == token_kind::Identifier && !sKeywords.contains(aTokens[i].text); };
        // (skip a balanced bracket from i (at its opener): the identifiers in it are expressions' (marked if they are typedef names))
        std::function<std::size_t(std::size_t)> skip_balanced;
        std::function<std::size_t(std::size_t, bool, bool)> declaration;
        auto const mark = [&](std::size_t i)
        {
            if (plain_identifier(i) && lookup(aTokens[i].text) && !(i > 0u && (aTokens[i - 1u].is(".") || aTokens[i - 1u].is("->"))))
                aTokens[i].typedefName = true;
        };
        // (an expression (or balanced tokens) from i to a token at depth 0 that ends it: its typedef names marked; compound literals' and
        // casts' type names are within it; a statement expression's or a braced initializer's blocks are not scopes)
        auto const expression = [&](std::size_t i, std::function<bool(std::size_t)> const& aEnd) -> std::size_t
        {
            int depth = 0;
            for (; i < n; ++i)
            {
                if (depth == 0 && aEnd(i))
                    return i;
                auto const& t = aTokens[i];
                if (t.is("(") || t.is("[") || t.is("{"))
                    ++depth;
                else if (t.is(")") || t.is("]") || t.is("}"))
                {
                    if (depth == 0)
                        return i;
                    --depth;
                }
                else if (t.kind == token_kind::Identifier && (t.text == "struct" || t.text == "union" || t.text == "enum") && plain_identifier(i + 1u))
                    ++i;    // (a tag)
                else
                    mark(i);
            }
            return i;
        };
        skip_balanced = [&](std::size_t i) -> std::size_t
        {
            // (from an opener to after its closer)
            return expression(i + 1u, [](std::size_t) { return false; }) + 1u;
        };
        // (a declarator from i: its identifier declared (as a typedef name if aTypedef); parameters' names in a scope of their own
        // (kept in aParameters for a function definition's body); returns after it)
        std::vector<std::unordered_map<std::string, bool>> lastParameters;
        std::function<std::size_t(std::size_t, bool, bool)> declarator = [&](std::size_t i, bool aTypedef, bool aMember) -> std::size_t
        {
            // (pointers, qualifiers and attributes)
            for (;;)
            {
                if (is(i, "*"))
                    ++i;
                else if (i < n && aTokens[i].kind == token_kind::Identifier && (aTokens[i].text == "const" || aTokens[i].text == "volatile" ||
                    aTokens[i].text == "restrict" || aTokens[i].text == "_Atomic"))
                    ++i;
                else if (is(i, "[") && is(i + 1u, "["))
                    i = skip_balanced(i);
                else
                    break;
            }
            bool named = false;
            if (plain_identifier(i))
            {
                if (!aMember)
                    declare(aTokens[i].text, aTypedef);
                named = true;
                ++i;
            }
            else if (is(i, "(") && !(plain_identifier(i + 1u) && lookup(aTokens[i + 1u].text)) && !(i + 1u < n && aTokens[i + 1u].kind == token_kind::Identifier &&
                (sTypeKeywords.contains(aTokens[i + 1u].text) || sOtherSpecifiers.contains(aTokens[i + 1u].text))) && !is(i + 1u, ")") && !is(i + 1u, "..."))
            {
                // (a parenthesized declarator)
                i = declarator(i + 1u, aTypedef, aMember);
                if (is(i, ")"))
                    ++i;
                named = true;
            }
            (void)named;
            // (suffixes)
            for (;;)
            {
                if (is(i, "["))
                {
                    if (is(i + 1u, "["))
                        i = skip_balanced(i);
                    else
                        i = skip_balanced(i);
                }
                else if (is(i, "("))
                {
                    // (a parameter list: a prototype scope)
                    scopes.emplace_back();
                    std::size_t j = i + 1u;
                    while (j < n && !is(j, ")"))
                    {
                        if (is(j, "..."))
                        {
                            ++j;
                            continue;
                        }
                        j = declaration(j, false, true);
                        if (is(j, ","))
                            ++j;
                        else if (!is(j, ")"))
                            j = expression(j, [&](std::size_t k) { return is(k, ",") || is(k, ")"); });
                    }
                    lastParameters.assign(1u, scopes.back());
                    scopes.pop_back();
                    i = j < n ? j + 1u : j;
                }
                else
                    break;
            }
            return i;
        };
        // (a declaration (or a parameter's, aParameter) from i: its specifiers, then its declarators (for a parameter, one); returns
        // where it ends (after its ';', or at a function definition's body); if it is not a declaration, i)
        declaration = [&](std::size_t i, bool aMember, bool aParameter) -> std::size_t
        {
            std::size_t const start = i;
            bool typeSeen = false;
            bool isTypedef = false;
            bool any = false;
            for (;;)
            {
                if (is(i, "[") && is(i + 1u, "["))
                {
                    i = skip_balanced(i);
                    continue;
                }
                if (i >= n || aTokens[i].kind != token_kind::Identifier)
                    break;
                auto const& text = aTokens[i].text;
                if (text == "typedef")
                {
                    isTypedef = true;
                    any = true;
                    ++i;
                }
                else if (text == "struct" || text == "union" || text == "enum")
                {
                    bool const isEnum = text == "enum";
                    typeSeen = true;
                    any = true;
                    ++i;
                    while (is(i, "[") && is(i + 1u, "["))
                        i = skip_balanced(i);
                    if (plain_identifier(i))
                        ++i;    // (the tag)
                    if (isEnum && is(i, ":"))
                    {
                        // (an enumeration's underlying type: a specifier qualifier list)
                        ++i;
                        while (i < n && aTokens[i].kind == token_kind::Identifier && (sTypeKeywords.contains(aTokens[i].text) ||
                            sOtherSpecifiers.contains(aTokens[i].text) || lookup(aTokens[i].text)))
                        {
                            mark(i);
                            ++i;
                        }
                    }
                    if (is(i, "{"))
                    {
                        ++i;
                        if (isEnum)
                        {
                            // (enumerators: ordinary identifiers of the scope)
                            while (i < n && !is(i, "}"))
                            {
                                if (plain_identifier(i))
                                    declare(aTokens[i].text, false);
                                i = expression(i + 1u, [&](std::size_t k) { return is(k, ",") || is(k, "}"); });
                                if (is(i, ","))
                                    ++i;
                            }
                        }
                        else
                        {
                            // (members: names that do not hide typedef names)
                            while (i < n && !is(i, "}"))
                            {
                                auto const end = declaration(i, true, false);
                                if (end == i)
                                    i = expression(i, [&](std::size_t k) { return is(k, ";"); }) + 1u;
                                else
                                    i = end;
                            }
                        }
                        if (is(i, "}"))
                            ++i;
                    }
                }
                else if (text == "_Atomic" && is(i + 1u, "("))
                {
                    typeSeen = true;
                    any = true;
                    i = skip_balanced(i + 1u);
                }
                else if (text == "typeof" || text == "typeof_unqual" || text == "_BitInt" || text == "alignas" || text == "_Alignas")
                {
                    if (text != "alignas" && text != "_Alignas")
                        typeSeen = true;
                    any = true;
                    ++i;
                    if (is(i, "("))
                        i = skip_balanced(i);
                }
                else if (sTypeKeywords.contains(text))
                {
                    typeSeen = true;
                    any = true;
                    ++i;
                }
                else if (sOtherSpecifiers.contains(text))
                {
                    any = true;
                    ++i;
                }
                else if (!typeSeen && !sKeywords.contains(text) && lookup(text))
                {
                    // (a typedef name used as a type specifier)
                    aTokens[i].typedefName = true;
                    typeSeen = true;
                    any = true;
                    ++i;
                }
                else
                    break;
            }
            if (!any)
                return start;
            if (aParameter)
            {
                if (!is(i, ",") && !is(i, ")"))
                    i = declarator(i, false, false);
                return i;
            }
            // (static_assert, as a declaration, is handled by the caller)
            for (;;)
            {
                if (is(i, ";"))
                    return i + 1u;
                if (is(i, ":") && aMember)
                {
                    i = expression(i + 1u, [&](std::size_t k) { return is(k, ",") || is(k, ";"); });
                }
                else
                {
                    lastParameters.clear();
                    i = declarator(i, isTypedef, aMember);
                    while (is(i, "[") && is(i + 1u, "["))
                        i = skip_balanced(i);
                    if (aMember && is(i, ":"))
                        i = expression(i + 1u, [&](std::size_t k) { return is(k, ",") || is(k, ";"); });
                    if (is(i, "{"))
                        return i;   // (a function definition's body)
                    if (is(i, "="))
                        i = expression(i + 1u, [&](std::size_t k) { return is(k, ",") || is(k, ";"); });
                }
                if (is(i, ","))
                {
                    ++i;
                    continue;
                }
                if (is(i, ";"))
                    return i + 1u;
                // (not a declaration after all (an error): the rest as an expression)
                return expression(i, [&](std::size_t k) { return is(k, ";") || is(k, "{") || is(k, "}"); });
            }
        };
        // (statements and declarations of a block (or the file) from i, to its closing brace (at depth 0: the end))
        // (a scope that a statement opens (for, if, switch, while: C23 6.8.5) closes at the end of its statement: these are kept as a stack
        // of the depths of the braces that they are at, with whether a statement without braces ends them)
        struct pending_scope
        {
            int depth;
            bool braced;        ///< (its statement is a compound statement)
            int parentheses;    ///< (the parentheses open when it opened)
        };
        std::vector<pending_scope> statementScopes;
        int braceDepth = 0;
        std::size_t i = 0u;
        bool statementStart = true;
        bool functionBody = false;
        auto const close_statement_scopes = [&]()
        {
            // (a statement ended at the current depth: the scopes of statements without braces that it was the body of)
            while (!statementScopes.empty() && statementScopes.back().depth == braceDepth && !statementScopes.back().braced)
            {
                statementScopes.pop_back();
                scopes.pop_back();
            }
        };
        while (i < n)
        {
            auto const& t = aTokens[i];
            if (t.is("{"))
            {
                ++braceDepth;
                scopes.emplace_back();
                if (functionBody && !lastParameters.empty())
                    scopes.back() = lastParameters.front();
                functionBody = false;
                lastParameters.clear();
                ++i;
                statementStart = true;
                continue;
            }
            if (t.is("}"))
            {
                if (scopes.size() > 1u)
                    scopes.pop_back();
                --braceDepth;
                ++i;
                // (a compound statement that is a for/while/if/switch's body: its scope ends too)
                while (!statementScopes.empty() && statementScopes.back().depth == braceDepth && statementScopes.back().braced)
                {
                    statementScopes.pop_back();
                    scopes.pop_back();
                }
                close_statement_scopes();
                statementStart = true;
                continue;
            }
            if (t.is(";"))
            {
                ++i;
                close_statement_scopes();
                statementStart = true;
                continue;
            }
            if (statementStart)
            {
                statementStart = false;
                // (labels (in the label name space: a typedef name's identifier may be one))
                if (plain_identifier(i) && is(i + 1u, ":"))
                {
                    i += 2u;
                    statementStart = true;
                    continue;
                }
                if (word(i, "goto") && plain_identifier(i + 1u))
                {
                    i += 2u;
                    continue;
                }
                if (word(i, "case") || word(i, "default"))
                {
                    i = expression(i + 1u, [&](std::size_t k) { return is(k, ":"); }) + 1u;
                    statementStart = true;
                    continue;
                }
                if (word(i, "static_assert") || word(i, "_Static_assert"))
                {
                    i = expression(i + 1u, [&](std::size_t k) { return is(k, ";"); });
                    continue;
                }
                if (word(i, "for") || word(i, "if") || word(i, "switch") || word(i, "while"))
                {
                    // (a scope of its own: a for's declaration's, and (C23) the statement's (its body's scope is within it))
                    bool const isFor = word(i, "for");
                    scopes.emplace_back();
                    ++i;
                    if (is(i, "("))
                    {
                        ++i;
                        if (isFor)
                        {
                            auto const end = declaration(i, false, false);
                            if (end != i)
                                i = end;
                            else
                                i = expression(i, [&](std::size_t k) { return is(k, ";"); }) + 1u;
                            i = expression(i, [&](std::size_t k) { return is(k, ";"); }) + 1u;
                        }
                        i = expression(i, [&](std::size_t k) { return false; (void)k; });
                        if (is(i, ")"))
                            ++i;
                    }
                    statementScopes.push_back(pending_scope{ braceDepth, is(i, "{"), 0 });
                    statementStart = true;
                    continue;
                }
                if (word(i, "else") || word(i, "do"))
                {
                    ++i;
                    statementStart = true;
                    continue;
                }
                auto const end = declaration(i, false, false);
                if (end != i)
                {
                    i = end;
                    if (is(i, "{"))
                        functionBody = true;
                    else
                        close_statement_scopes();
                    statementStart = true;
                    continue;
                }
            }
            // (an expression (in a statement))
            if (t.is("(") || t.is("["))
            {
                i = skip_balanced(i);
                continue;
            }
            if (t.kind == token_kind::Identifier && (t.text == "struct" || t.text == "union" || t.text == "enum") && plain_identifier(i + 1u))
            {
                i += 2u;
                continue;
            }
            mark(i);
            ++i;
        }
    }

    // --- the translation unit

    preprocessor::output preprocessor::run(std::string const& aPath, std::string const& aSource)
    {
        // (the predefined macros)
        auto const predefine = [&](std::string const& aName, std::string const& aValue, bool aBuiltin = false)
        {
            macro m;
            m.id = iNextMacro++;
            m.name = aName;
            m.builtin = aBuiltin;
            if (!aValue.empty())
            {
                auto tokens = tokenize(aValue, 0u);
                for (auto& t : tokens)
                    t.lineStart = false;
                if (!tokens.empty())
                    tokens.front().space = false;
                m.body = tokens;
            }
            iMacros[aName] = m;
        };
        iFiles.clear();
        iFileIndex.clear();
        iPack = 0u;
        iPackStack.clear();
        add_file(aPath);
        for (auto const* name : { "__LINE__", "__FILE__", "__DATE__", "__TIME__", "__COUNTER__", "_Pragma" })
            predefine(name, "", true);
        for (auto const& [name, value] : std::initializer_list<std::pair<char const*, char const*>>{
            { "__STDC__", "1" }, { "__STDC_VERSION__", "202311L" }, { "__STDC_HOSTED__", "1" }, { "__STDC_UTF_16__", "1" },
            { "__STDC_UTF_32__", "1" }, { "__STDC_IEC_559__", "1" }, { "__STDC_IEC_60559_BFP__", "202311L" }, { "__STDC_NO_COMPLEX__", "1" },
            { "__STDC_EMBED_NOT_FOUND__", "0" }, { "__STDC_EMBED_FOUND__", "1" }, { "__STDC_EMBED_EMPTY__", "2" },
            { "__neos__", "1" }, { "__LP64__", "1" }, { "_LP64", "1" }, { "__CHAR_BIT__", "8" }, { "__SIZEOF_POINTER__", "8" }, { "__SIZEOF_INT__", "4" },
            { "__SIZEOF_LONG__", "8" }, { "__SIZEOF_LONG_LONG__", "8" }, { "__SIZEOF_SHORT__", "2" }, { "__SIZEOF_FLOAT__", "4" },
            { "__SIZEOF_DOUBLE__", "8" }, { "__SIZEOF_LONG_DOUBLE__", "8" }, { "__SIZEOF_SIZE_T__", "8" }, { "__ORDER_LITTLE_ENDIAN__", "1234" },
            { "__ORDER_BIG_ENDIAN__", "4321" }, { "__BYTE_ORDER__", "1234" }, { "__SIZEOF_WCHAR_T__", "4" }, { "__SIZEOF_WINT_T__", "4" },
            { "__SIZEOF_PTRDIFF_T__", "8" }, { "__SIZE_TYPE__", "unsigned long" }, { "__PTRDIFF_TYPE__", "long" }, { "__WCHAR_TYPE__", "int" },
            { "__WINT_TYPE__", "unsigned int" }, { "__INTMAX_TYPE__", "long" }, { "__UINTMAX_TYPE__", "unsigned long" },
            { "__INTPTR_TYPE__", "long" }, { "__UINTPTR_TYPE__", "unsigned long" }, { "__CHAR16_TYPE__", "unsigned short" },
            { "__CHAR32_TYPE__", "unsigned int" }, { "__SCHAR_MAX__", "0x7f" }, { "__SHRT_MAX__", "0x7fff" }, { "__INT_MAX__", "0x7fffffff" },
            { "__LONG_MAX__", "0x7fffffffffffffffL" }, { "__LONG_LONG_MAX__", "0x7fffffffffffffffLL" }, { "__WCHAR_MAX__", "0x7fffffff" },
            { "__SIZE_MAX__", "0xffffffffffffffffUL" }, { "__FUNCTION__", "__func__" }, { "__thread", "thread_local" }, { "__PRETTY_FUNCTION__", "__func__" },
            // (GNU's spellings of keywords, and its __attribute__ and __extension__ (ignored), that code written for GCC uses)
            { "__inline", "inline" }, { "__inline__", "inline" }, { "__restrict", "restrict" }, { "__restrict__", "restrict" },
            { "__volatile__", "volatile" }, { "__const", "const" }, { "__signed__", "signed" }, { "__typeof__", "typeof" },
            { "__typeof", "typeof" }, { "__alignof__", "alignof" }, { "__alignof", "alignof" }, { "__extension__", "" } })
            predefine(name, value);
        for (auto const* name : { "__attribute__", "__attribute" })
        {
            // (__attribute__((...)): the attributes that the compiler implements as C23 attributes ([[gnu::...]]); others: nothing
            // (see gnu_attributes))
            macro m;
            m.id = iNextMacro++;
            m.name = name;
            m.function = true;
            m.variadic = true;
            m.builtin = true;
            m.parameters.push_back("__VA_ARGS__");
            iMacros[m.name] = m;
            iAttributeMacro = m;
        }
        // (function-like predefined macros: GNU's offsetof and atomic operations)
        auto const predefine_function = [&](std::string const& aName, std::vector<std::string> const& aParameters, std::string const& aBody)
        {
            predefine(aName, aBody);
            auto& m = iMacros[aName];
            m.function = true;
            m.parameters = aParameters;
        };
        predefine_function("__builtin_offsetof", { "t", "m" }, "((unsigned long)&((t*)0)->m)");
        for (auto const& [name, value] : std::initializer_list<std::pair<char const*, char const*>>{
            { "__ATOMIC_RELAXED", "0" }, { "__ATOMIC_CONSUME", "1" }, { "__ATOMIC_ACQUIRE", "2" }, { "__ATOMIC_RELEASE", "3" },
            { "__ATOMIC_ACQ_REL", "4" }, { "__ATOMIC_SEQ_CST", "5" }, { "__GCC_ATOMIC_INT_LOCK_FREE", "2" }, { "__GCC_ATOMIC_POINTER_LOCK_FREE", "2" } })
            predefine(name, value);
        // (the atomic builtins (GCC's): real atomic accesses (see the compiler's builtins __builtin_atomic_*); the memory order (o) is
        // evaluated and ignored (each is sequentially consistent))
        predefine_function("__atomic_load_n", { "p", "o" }, "((void)(o), __builtin_atomic_load((p)))");
        predefine_function("__atomic_store_n", { "p", "v", "o" }, "((void)(o), __builtin_atomic_store((p), (v)))");
        predefine_function("__atomic_exchange_n", { "p", "v", "o" }, "((void)(o), __builtin_atomic_exchange((p), (v)))");
        predefine_function("__atomic_compare_exchange_n", { "p", "e", "d", "w", "s", "f" },
            "((void)(w), (void)(s), (void)(f), __builtin_atomic_compare_exchange((p), (e), (d)))");
        predefine_function("__atomic_load", { "p", "r", "o" }, "((void)(o), (void)(*(r) = __builtin_atomic_load((p))))");
        predefine_function("__atomic_store", { "p", "v", "o" }, "((void)(o), __builtin_atomic_store((p), *(v)))");
        predefine_function("__atomic_exchange", { "p", "v", "r", "o" }, "((void)(o), (void)(*(r) = __builtin_atomic_exchange((p), *(v))))");
        predefine_function("__atomic_compare_exchange", { "p", "e", "d", "w", "s", "f" },
            "((void)(w), (void)(s), (void)(f), __builtin_atomic_compare_exchange((p), (e), *(d)))");
        for (auto const* op : { "add", "sub", "and", "or", "xor" })
        {
            std::string const o = op;
            std::string const operation = o == "add" ? "+" : o == "sub" ? "-" : o == "and" ? "&" : o == "or" ? "|" : "^";
            // (op_fetch: the new value, from the old)
            std::string const newValue = "({ __typeof__(*(p)) __neos_v = (v); (__typeof__(*(p)))(__builtin_atomic_fetch_" + o + "((p), __neos_v) " +
                operation + " __neos_v); })";
            predefine_function("__atomic_fetch_" + o, { "p", "v", "o" }, "((void)(o), __builtin_atomic_fetch_" + o + "((p), (v)))");
            predefine_function("__atomic_" + o + "_fetch", { "p", "v", "o" }, "((void)(o), " + newValue + ")");
            predefine_function("__sync_fetch_and_" + o, { "p", "v" }, "__builtin_atomic_fetch_" + o + "((p), (v))");
            predefine_function("__sync_" + o + "_and_fetch", { "p", "v" }, newValue);
        }
        predefine_function("__atomic_test_and_set", { "p", "o" }, "((void)(o), __builtin_atomic_exchange((unsigned char*)(p), 1) != 0)");
        predefine_function("__atomic_clear", { "p", "o" }, "((void)(o), __builtin_atomic_store((unsigned char*)(p), 0))");
        predefine_function("__atomic_thread_fence", { "o" }, "((void)(o), __builtin_atomic_fence())");
        predefine_function("__atomic_signal_fence", { "o" }, "((void)(o))");
        predefine_function("__atomic_always_lock_free", { "s", "p" }, "((void)(p), (s) <= 8)");
        predefine_function("__atomic_is_lock_free", { "s", "p" }, "((void)(p), (s) <= 8)");
        predefine_function("__sync_bool_compare_and_swap", { "p", "o", "n" },
            "({ __typeof__(*(p)) __neos_old = (o); __builtin_atomic_compare_exchange((p), &__neos_old, (n)); })");
        predefine_function("__sync_val_compare_and_swap", { "p", "o", "n" },
            "({ __typeof__(*(p)) __neos_old = (o); __builtin_atomic_compare_exchange((p), &__neos_old, (n)); __neos_old; })");
        predefine_function("__sync_lock_test_and_set", { "p", "v" }, "__builtin_atomic_exchange((p), (v))");
        predefine_function("__sync_lock_release", { "p" }, "__builtin_atomic_store((p), 0)");
        predefine_function("__sync_synchronize", {}, "__builtin_atomic_fence()");
        output result;
        push_file(aPath, aSource);
        // (the tokens of the translation unit, macros replaced)
        std::vector<pp_token> tokens;
        file_reader r{ *this };
        expand(r, tokens);
        mark_typedef_names(tokens);
        // (the text: tokens separated by spaces (or new-lines, where a token's line in its file is not the previous token's))
        std::set<std::string> identifiers;
        result.text.reserve(tokens.size() * 6u);
        source_location previous{};
        bool first = true;
        for (auto const& t : tokens)
        {
            if (!first)
                result.text += (t.where.file == previous.file && t.where.line == previous.line) ? ' ' : '\n';
            first = false;
            previous = t.where;
            if (t.kind == token_kind::Other)
                throw preprocess_error{ t.where, "stray \"" + t.text + "\" in program" };
            if (t.kind == token_kind::HeaderName)
                throw preprocess_error{ t.where, "header name outside a directive" };
            result.map.emplace_back(static_cast<std::uint32_t>(result.text.size()), t.where);
            if (t.typedefName)
                result.text += '@';
            result.text += t.text;
            if (t.kind == token_kind::Identifier)
                identifiers.insert(t.text);
        }
        result.text += '\n';
        result.identifiers.assign(identifiers.begin(), identifiers.end());
        return result;
    }
}
