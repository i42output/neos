/*
  compiler.cpp

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

// C: the language compiler (the preprocessor's pass, the syntax tree and the driver); analysis and code generation are the
// program's (see program.hpp)

#include <neolib/neolib.hpp>
#include <neolib/core/pair.hpp>
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <iostream>
#include "compiler.hpp"
#include "program.hpp"

namespace neos::compilers::c
{
    source_location translation_unit::location_of(std::string_view aSource) const
    {
        if (aSource.data() < text.data() || aSource.data() > text.data() + text.size())
            return {};
        auto const offset = static_cast<std::size_t>(aSource.data() - text.data());
        auto const it = std::upper_bound(output.map.begin(), output.map.end(), offset,
            [](std::size_t aValue, auto const& aEntry) { return aValue < aEntry.first; });
        if (it == output.map.begin())
            return output.map.empty() ? source_location{} : output.map.front().second;
        return std::prev(it)->second;
    }

    compiler::compiler() :
        language_compiler{ language::c::Language }
    {
    }

    compiler::~compiler() = default;

    void compiler::begin_compilation()
    {
        iProgramUnits = 0u;
        iLibraryRead = false;
        begin_pass();
    }

    void compiler::begin_pass()
    {
        // (each pass compiles the program's translation units again)
        iFragments.clear();
        iNodes.clear();
        iStack.clear();
        iUnits.clear();
        iParsed.clear();
        iUnitsParsed = 0u;
        iImportedLibraries.clear();
        iPendingLibraries.clear();
        iGenerated = false;
        iProgram.reset();
    }

    preprocessor::options compiler::preprocessor_options() const
    {
        preprocessor::options options;
#if defined(_WIN32)
        char const separator = ';';
#else
        char const separator = ':';
#endif
        auto const add = [&](char const* aVariable, std::vector<std::string>& aFolders)
        {
            if (auto const list = std::getenv(aVariable))
            {
                std::string const paths{ list };
                std::size_t start = 0u;
                for (;;)
                {
                    auto const end = paths.find(separator, start);
                    auto const entry = paths.substr(start, end == std::string::npos ? std::string::npos : end - start);
                    if (!entry.empty())
                        aFolders.push_back(entry);
                    if (end == std::string::npos)
                        break;
                    start = end + 1u;
                }
            }
        };
        add("NEOS_C_INCLUDE_PATH", options.includes);
        add("C_INCLUDE_PATH", options.includes);
        add("CPATH", options.includes);
        if (iPackagesFolder)
            options.systemIncludes.push_back((std::filesystem::path{ *iPackagesFolder } / "include").generic_string());
        return options;
    }

    void compiler::begin_fragment(i_source_fragment& aFragment)
    {
        auto const path = aFragment.source_file_path().has_value() ? aFragment.source_file_path().value().to_std_string() : std::string{ "<source>" };
        // (the prelude (the C library's own C code) is in the language's packages folder, whose include folder has the standard headers)
        if (aFragment.imported() && !iPackagesFolder && aFragment.source_file_path().has_value())
        {
            std::filesystem::path const p{ path };
            if (p.stem().string() == "libc")
                iPackagesFolder = p.parent_path().string();
        }
        auto& unit = iUnits.emplace_back();
        unit.path = path;
        unit.index = static_cast<std::uint32_t>(iUnits.size() - 1u);
        // (the prelude, and the library's files in C: in the packages folder)
        unit.prelude = aFragment.imported() && iPackagesFolder && (std::filesystem::path{ path }.parent_path().string() == *iPackagesFolder ||
            std::filesystem::path{ path }.parent_path() == std::filesystem::path{ *iPackagesFolder } / "lib");
        iFragments.push_back(fragment_state{ aFragment.source().to_std_string_view(), &unit });
        // (the program's first fragment begins before the prelude is compiled (within it): it is preprocessed when the prelude ends, 
        // before it is parsed)
        if (!aFragment.imported() && !iPackagesFolder)
        {
            iFragments.back().pending = &aFragment;
            return;
        }
        preprocess(aFragment, iFragments.back());
    }

    void compiler::preprocess(i_source_fragment& aFragment, fragment_state& aState)
    {
        auto& unit = *aState.unit;
        auto const& path = unit.path;
        std::string source = aFragment.source().to_std_string();
        if (auto const known = iSources.find(path); known != iSources.end() && known->second.second == source)
            source = known->second.first;
        auto const key = std::make_pair(path, source);
        auto cached = iPreprocessed.find(key);
        if (cached == iPreprocessed.end())
        {
            preprocessor::options options = preprocessor_options();
            preprocessor const* running = nullptr;
            options.warning = [&](source_location const& aWhere, std::string const& aMessage)
            {
                std::cerr << (running != nullptr && aWhere.file < running->files().size() ? running->files()[aWhere.file] : path) << "(" << 
                    aWhere.line << "," << aWhere.column << "): warning: " << aMessage << std::endl;
            };
            preprocessor pp{ options };
            running = &pp;
            try
            {
                auto output = pp.run(path, source);
                cached = iPreprocessed.emplace(key, std::make_pair(std::move(output), pp.files())).first;
            }
            catch (preprocess_error const& e)
            {
                unit.files = pp.files();
                report(unit, e.where, e.what());
                return;
            }
        }
        unit.output = cached->second.first;
        unit.files = cached->second.second;
        unit.text = unit.output.text;
        iSources[path] = std::make_pair(source, unit.text);
        aFragment.set_source(neolib::string_view{ unit.text });
        aState.base = aFragment.source().to_std_string_view();
        aState.pending = nullptr;
        import_libraries(unit.output.identifiers);
    }

    void compiler::import_libraries(std::vector<std::string> const& aIdentifiers)
    {
        // (the library's files that provide the functions that a unit names: imported (each once a pass), parsed after it)
        if (!iPackagesFolder)
            return;
        if (!iLibraryRead)
        {
            iLibraryRead = true;
            iLibraryProvides.clear();
            std::error_code ec;
            for (auto const& entry : std::filesystem::directory_iterator{ std::filesystem::path{ *iPackagesFolder } / "lib", ec })
            {
                if (entry.path().extension() != ".c")
                    continue;
                std::ifstream file{ entry.path(), std::ios::binary };
                std::ostringstream text;
                text << file.rdbuf();
                auto const contents = text.str();
                auto const begin = contents.find("/* provides:");
                if (begin == std::string::npos)
                    continue;
                auto const end = contents.find("*/", begin);
                std::istringstream names{ contents.substr(begin + 12u, end - begin - 12u) };
                for (std::string name; names >> name;)
                    iLibraryProvides[name] = entry.path().generic_string();
            }
        }
        for (auto const& identifier : aIdentifiers)
        {
            auto const provider = iLibraryProvides.find(identifier);
            if (provider == iLibraryProvides.end() || iImportedLibraries.contains(provider->second))
                continue;
            std::ifstream file{ provider->second, std::ios::binary };
            std::ostringstream text;
            text << file.rdbuf();
            iImportedLibraries.insert(provider->second);
            iPendingLibraries.insert(provider->second);
            generator().import_fragment(neolib::string{ provider->second }, neolib::string{ text.str() });
        }
    }

    void compiler::end_fragment()
    {
        if (!iFragments.empty())
            iFragments.pop_back();
        // (the prelude ended: the program's first fragment, which began before it, preprocessed)
        if (!iFragments.empty() && iFragments.back().pending != nullptr && iPackagesFolder)
            preprocess(*iFragments.back().pending, iFragments.back());
    }

    bool compiler::parsable_source(neolib::i_string_view const& aSource, neolib::i_string& aResult, neolib::i_vector<std::uint32_t>& aPositions) const
    {
        // (as written: a syntax highlighter sees the source before preprocessing)
        (void)aSource;
        (void)aResult;
        (void)aPositions;
        return false;
    }

    bool compiler::frame_name(std::uint32_t aFunction, neolib::i_string& aName)
    {
        auto const existing = iFrameNames.find(aFunction);
        if (existing == iFrameNames.end())
            return false;
        aName = existing->second;
        return true;
    }

    std::uint32_t compiler::source_line(i_source_fragment const& aFragment, std::size_t aPosition)
    {
        for (auto& unit : iUnits)
            if (aFragment.source().data() == unit.text.data() || (aFragment.source_file_path().has_value() && aFragment.source_file_path().value().to_std_string() == unit.path))
            {
                if (aPosition >= unit.text.size())
                    return 0u;
                return unit.location_of(std::string_view{ unit.text.data() + aPosition, 0u }).line;
            }
        return 0u;
    }

    void compiler::warning(translation_unit const* aUnit, source_location const& aWhere, std::string const& aMessage)
    {
        std::cerr << (aUnit != nullptr ? aUnit->file_of(aWhere) : std::string{}) << "(" << aWhere.line << "," << aWhere.column << "): warning: " << aMessage << std::endl;
    }

    void compiler::warning(node const* aWhere, std::string const& aMessage)
    {
        if (aWhere == nullptr || aWhere->unit == nullptr)
        {
            std::cerr << "warning: " << aMessage << std::endl;
            return;
        }
        warning(aWhere->unit, aWhere->unit->location_of(aWhere->source), aMessage);
    }

    void compiler::diagnose(i_source_fragment const& aFragment, std::uint32_t& aLine, std::uint32_t& aColumn, std::uint32_t& aEndLine, 
        std::uint32_t& aEndColumn, neolib::i_string& aType, neolib::i_string& aMessage, neolib::i_string& aReport)
    {
        if (iPendingError)
        {
            aLine = iPendingError->line;
            aColumn = iPendingError->column;
            aEndLine = 0u;
            aEndColumn = 0u;
            aMessage = neolib::string{ iPendingError->message };
            aReport = neolib::string{ iPendingError->file + "(" + std::to_string(iPendingError->line) + "," + std::to_string(iPendingError->column) + "): " +
                aType.to_std_string() + ": " + iPendingError->message };
            iPendingError.reset();
            return;
        }
        // (the parser's error: its line and column in the preprocessed text, as the source as written has them)
        for (auto& unit : iUnits)
        {
            // (the fragment's source: a copy of its unit's preprocessed text (see compiler::preprocess))
            auto const source = aFragment.source().to_std_string_view();
            if (source.data() != unit.text.data() && source != std::string_view{ unit.text })
                continue;
            std::size_t offset = 0u;
            for (std::uint32_t line = 1u; line < aLine && offset < unit.text.size(); ++line)
            {
                auto const next = unit.text.find('\n', offset);
                if (next == std::string::npos)
                    break;
                offset = next + 1u;
            }
            offset += aColumn > 0u ? aColumn - 1u : 0u;
            offset = std::min(offset, unit.text.size());
            auto const where = unit.location_of(std::string_view{ unit.text.data() + offset, 0u });
            // (the token there, for the message)
            auto end = offset;
            while (end < unit.text.size() && unit.text[end] != ' ' && unit.text[end] != '\n')
                ++end;
            std::string token = unit.text.substr(offset, end - offset);
            if (!token.empty() && token.front() == '@')
                token.erase(0u, 1u);
            aLine = where.line;
            aColumn = where.column;
            aEndLine = 0u;
            aEndColumn = 0u;
            std::string message = aMessage.to_std_string();
            if (!token.empty() && message.find("syntax error") != std::string::npos)
                message = "syntax error before '" + token + "'";
            aMessage = neolib::string{ message };
            aReport = neolib::string{ unit.file_of(where) + "(" + std::to_string(where.line) + "," + std::to_string(where.column) + "): " +
                aType.to_std_string() + ": " + message };
            return;
        }
    }

    // --- the syntax tree

    node* compiler::make_node(construct aKind, std::string_view aSource)
    {
        iNodes.push_back(node{ aKind, aSource });
        auto* result = &iNodes.back();
        if (!iFragments.empty())
            result->unit = iFragments.back().unit;
        return result;
    }

    void compiler::emit_construct(std::uint32_t aConstruct, neolib::i_string_view const& aSource)
    {
        // (the construct's source in the compiler's copy of the fragment's source)
        auto source = aSource.to_std_string_view();
        if (!iFragments.empty())
        {
            auto const& f = iFragments.back();
            if (source.data() >= f.base.data() && source.data() + source.size() <= f.base.data() + f.base.size())
                source = std::string_view{ f.unit->text.data() + (source.data() - f.base.data()), source.size() };
        }
        build(static_cast<construct>(aConstruct), source);
    }

    void compiler::build(construct aKind, std::string_view aSource)
    {
        // a construct's children are the nodes on the stack whose source is within its source (in order); a postfix construct's
        // first child is the node before them (its operand), and its source begins with its operand's
        auto const within = [&](node const* aNode)
        {
            return aNode->source.data() >= aSource.data() && aNode->source.data() + aNode->source.size() <= aSource.data() + aSource.size();
        };
        std::size_t first = iStack.size();
        while (first > 0u && within(iStack[first - 1u]))
            --first;
        auto* result = make_node(aKind, aSource);
        if (language::c::info(aKind).postfix && first > 0u)
        {
            --first;
            auto* operand = iStack[first];
            result->source = std::string_view{ operand->source.data(), static_cast<std::size_t>(aSource.data() + aSource.size() - operand->source.data()) };
        }
        result->children.assign(iStack.begin() + static_cast<std::ptrdiff_t>(first), iStack.end());
        for (auto* child : result->children)
            child->parent = result;
        iStack.resize(first);
        iStack.push_back(result);
        if (aKind == construct::TranslationUnit)
        {
            iStack.clear();
            unit_parsed(result);
        }
    }

    void compiler::dump(node* aNode, int aDepth) const
    {
        std::string text{ aNode->source.substr(0u, 50u) };
        for (auto& c : text)
            if (c == '\n' || c == '\r' || c == '\t')
                c = ' ';
        std::cerr << std::string(static_cast<std::size_t>(aDepth) * 2u, ' ') << language::c::info(aNode->kind).name << " '" << text <<
            (aNode->source.size() > 50u ? "...'" : "'") << std::endl;
        for (auto* child : aNode->children)
            dump(child, aDepth + 1);
    }

    // --- the program's translation units

    void compiler::unit_parsed(node* aUnit)
    {
        auto* unit = aUnit->unit;
        if (unit == nullptr)
            return;
        unit->root = aUnit;
        iPendingLibraries.erase(unit->path);
        // (the program generated as a whole: when its last translation unit, and the library's files that they use, have been parsed)
        auto const generate_if_complete = [&]()
        {
            if (!declaration_pass() && !iGenerated && iProgramUnits != 0u && iUnitsParsed >= iProgramUnits && iPendingLibraries.empty())
            {
                iGenerated = true;
                generate();
            }
        };
        // (the prelude and the library's files: imported by each of the program's translation units that use them, compiled with the 
        // program once)
        if (unit->prelude && std::any_of(iParsed.begin(), iParsed.end(), [&](translation_unit const* aParsed) { return aParsed->prelude && aParsed->path == unit->path; }))
        {
            generate_if_complete();
            return;
        }
        iParsed.push_back(unit);
        if (std::getenv("NEOS_C_AST") != nullptr && (!unit->prelude || std::getenv("NEOS_C_AST")[0] == '2'))
            dump(aUnit);
        // (the declaration pass: the program's translation units counted (the program's functions are all defined by the code 
        // generation pass, when its last translation unit has been parsed: the program is generated as a whole))
        if (declaration_pass())
        {
            if (!unit->prelude)
                ++iProgramUnits;
            return;
        }
        if (!unit->prelude)
            ++iUnitsParsed;
        generate_if_complete();
    }

    void compiler::generate()
    {
        try
        {
            iProgram = std::make_unique<program>(*this, iParsed);
            iProgram->generate();
        }
        catch (compile_error const& e)
        {
            report(e);
        }
    }

    void compiler::report(compile_error const& aError)
    {
        pending_error error;
        error.message = aError.what();
        if (aError.where != nullptr && aError.where->unit != nullptr)
        {
            auto const where = aError.where->unit->location_of(aError.where->source);
            error.file = aError.where->unit->file_of(where);
            error.line = where.line;
            error.column = where.column;
        }
        iPendingError = error;
        auto const& fragment = generator().current_fragment();
        generator().throw_error(fragment.begin(), neolib::string{ error.message }, neolib::string{ "error" });
    }

    void compiler::report(translation_unit const& aUnit, source_location const& aWhere, std::string const& aMessage)
    {
        pending_error error;
        error.message = aMessage;
        error.file = aUnit.file_of(aWhere);
        error.line = aWhere.line;
        error.column = aWhere.column;
        iPendingError = error;
        auto const& fragment = generator().current_fragment();
        generator().throw_error(fragment.begin(), neolib::string{ error.message }, neolib::string{ "error" });
    }
}
