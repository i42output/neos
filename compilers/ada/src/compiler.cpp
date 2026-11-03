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

// Ada: the language compiler (the syntax tree, the program's source files and the driver); analysis and code generation are
// the program's (see program.hpp)

#include <neolib/neolib.hpp>
#include <neolib/core/pair.hpp>
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <fstream>
#include <regex>
#include <iostream>
#include <sstream>
#include <neolib/core/string_utf.hpp>
#include "compiler.hpp"
#include "program.hpp"

namespace neos::compilers::ada
{
    compiler::compiler() :
        language_compiler{ language::ada::Language }
    {
    }

    compiler::~compiler() = default;

    void compiler::begin_compilation()
    {
        begin_pass();
    }

    void compiler::begin_pass()
    {
        // (each pass compiles the program's files again)
        iFragments.clear();
        iNodes.clear();
        iStack.clear();
        iFiles.clear();
        iCompilations.clear();
        iImported.clear();
        iExpected = 0u;
        iMainSeen = false;
        iProgram.reset();
    }

    void compiler::begin_fragment(i_source_fragment& aFragment)
    {
        // the source's own pass: its identifiers and reserved words folded to lower case; the compiler keeps its own copy (the
        // syntax tree's sources are in it)
        std::string original = aFragment.source().to_std_string();
        std::string folded = original;
        // (a byte order mark: not part of the text (spaces, keeping the positions))
        if (folded.size() >= 3u && folded.compare(0u, 3u, "\xEF\xBB\xBF") == 0)
            folded.replace(0u, 3u, "   ");
        fold_case(folded);
        if (folded != original)
            aFragment.set_source(neolib::string_view{ folded });
        auto& file = iFiles.emplace_back();
        file.path = aFragment.source_file_path().has_value() ? aFragment.source_file_path().value().to_std_string() : std::string{};
        file.text = folded;
        file.original = std::move(original);
        iFragments.push_back(fragment_state{ aFragment.source().to_std_string_view(), &file });
        // (the prelude (package Standard) is in the language's packages folder)
        if (aFragment.imported() && !iPackagesFolder && !file.path.empty())
        {
            std::filesystem::path const path{ file.path };
            auto stem = path.stem().string();
            std::transform(stem.begin(), stem.end(), stem.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (stem == "standard")
                iPackagesFolder = path.parent_path().string();
        }
    }

    void compiler::end_fragment()
    {
        if (!iFragments.empty())
            iFragments.pop_back();
    }

    bool compiler::parsable_source(neolib::i_string_view const& aSource, neolib::i_string& aResult, neolib::i_vector<std::uint32_t>& aPositions) const
    {
        std::string folded{ aSource.to_std_string_view() };
        fold_case(folded);
        aResult = neolib::string{ folded };
        aPositions.clear();
        for (std::uint32_t i = 0u; i < folded.size(); ++i)
            aPositions.push_back(i);
        return true;
    }

    bool compiler::frame_name(std::uint32_t aFunction, neolib::i_string& aName)
    {
        auto const existing = iFrameNames.find(aFunction);
        if (existing == iFrameNames.end())
            return false;
        aName = existing->second;
        return true;
    }

    void compiler::diagnose(i_source_fragment const&, std::uint32_t& aLine, std::uint32_t& aColumn, std::uint32_t&, std::uint32_t&, 
        neolib::i_string& aType, neolib::i_string& aMessage, neolib::i_string& aReport)
    {
        if (!iPendingError)
            return;
        aLine = iPendingError->line;
        aColumn = iPendingError->column;
        aMessage = neolib::string{ iPendingError->message };
        aReport = neolib::string{ iPendingError->file + "(" + std::to_string(iPendingError->line) + "," + std::to_string(iPendingError->column) + "): " +
            aType.to_std_string() + ": " + iPendingError->message };
        iPendingError.reset();
    }

    // --- the syntax tree

    node* compiler::make_node(construct aKind, std::string_view aSource)
    {
        iNodes.push_back(node{ aKind, aSource });
        auto* result = &iNodes.back();
        if (!iFragments.empty())
            result->file = iFragments.back().file;
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
                source = std::string_view{ f.file->text.data() + (source.data() - f.base.data()), source.size() };
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
        // (an empty context clause: no node (an empty node would be within the construct that follows it))
        if (aKind == construct::Context && aSource.empty())
            return;
        std::size_t first = iStack.size();
        while (first > 0u && within(iStack[first - 1u]))
            --first;
        auto* result = make_node(aKind, aSource);
        if (language::ada::info(aKind).postfix && first > 0u)
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
        if (aKind == construct::Compilation)
        {
            iStack.clear();
            compilation_parsed(result);
        }
    }

    void compiler::dump(node* aNode, int aDepth) const
    {
        std::string text{ aNode->source.substr(0u, 40u) };
        for (auto& c : text)
            if (c == '\n' || c == '\r' || c == '\t')
                c = ' ';
        std::cerr << std::string(static_cast<std::size_t>(aDepth) * 2u, ' ') << language::ada::info(aNode->kind).name << " '" << text <<
            (aNode->source.size() > 40u ? "...'" : "'") << std::endl;
        for (auto* child : aNode->children)
            dump(child, aDepth + 1);
    }

    // --- the program's source files

    void compiler::compilation_parsed(node* aCompilation)
    {
        iCompilations.push_back(aCompilation);
        if (std::getenv("NEOS_ADA_AST") != nullptr)
            dump(aCompilation);
        // (the declaration pass: nothing (the program's functions are all defined by the code generation pass))
        if (declaration_pass())
            return;
        auto const& file = *aCompilation->file;
        bool const prelude = iPackagesFolder && !file.path.empty() && std::filesystem::path{ file.path }.parent_path().string() == *iPackagesFolder &&
            iCompilations.size() == 1u;
        if (!iMainSeen && !prelude)
        {
            // the main file: the units it names (and theirs) are compiled after it
            iMainSeen = true;
            find_units(file.path, file.text);
            iExpected = iCompilations.size() + iImported.size();
        }
        if (iMainSeen && iCompilations.size() >= iExpected)
            generate();
    }

    namespace
    {
        // (a source file's text: UTF-8, or else Latin-1 (Ada's default source representation) transcoded to UTF-8)
        std::string source_text(std::string aSource)
        {
            if (neolib::check_utf8(aSource))
                return aSource;
            std::string result;
            result.reserve(aSource.size() * 2u);
            for (char const c : aSource)
            {
                auto const byte = static_cast<unsigned char>(c);
                if (byte < 0x80u)
                    result += c;
                else
                {
                    result += static_cast<char>(0xC0u | (byte >> 6u));
                    result += static_cast<char>(0x80u | (byte & 0x3Fu));
                }
            }
            return result;
        }
    }

    void compiler::find_units(std::string const& aMainFile, std::string const& aMainSource)
    {
        iSearchPath.clear();
        std::error_code ec;
        if (!aMainFile.empty())
            iSearchPath.push_back(std::filesystem::absolute(std::filesystem::path{ aMainFile }, ec).parent_path().string());
        iSearchPath.push_back(std::filesystem::current_path(ec).string());
        for (auto const* variable : { "ADA_INCLUDE_PATH", "NEOS_ADA_PATH" })
            if (auto const list = std::getenv(variable))
            {
#if defined(_WIN32)
                char const separator = ';';
#else
                char const separator = ':';
#endif
                std::string const paths{ list };
                std::size_t start = 0u;
                for (;;)
                {
                    auto const end = paths.find(separator, start);
                    auto const entry = paths.substr(start, end == std::string::npos ? std::string::npos : end - start);
                    if (!entry.empty())
                        iSearchPath.push_back(entry);
                    if (end == std::string::npos)
                        break;
                    start = end + 1u;
                }
            }
        if (iPackagesFolder)
            iSearchPath.push_back(*iPackagesFolder);
        // (the units named, and the units that their files name)
        std::string mainPath = aMainFile.empty() ? std::string{} : std::filesystem::absolute(std::filesystem::path{ aMainFile }, ec).lexically_normal().generic_string();
        std::vector<std::string> work = units_named(aMainSource);
        std::set<std::string> seen;
        // (the subunits of a file's body stubs: the files named for them (GNAT's names: the parent's file's name, a '-' and the
        // stub's name (or nested units' names then the stub's)))
        std::function<void(std::string const&, std::string const&)> load_subunits = [&](std::string const& aPath, std::string const& aFolded)
        {
            auto const stubs = stub_names(aFolded);
            if (std::getenv("NEOS_ADA_DEBUG"))
                std::cerr << "subunits of " << aPath << ": " << stubs.size() << std::endl;
            if (stubs.empty())
                return;
            std::filesystem::path const p{ aPath };
            auto stem = p.stem().string();
            std::transform(stem.begin(), stem.end(), stem.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            std::error_code iec;
            std::vector<std::filesystem::path> found;
            for (auto const& entry : std::filesystem::directory_iterator{ p.parent_path(), iec })
            {
                auto name = entry.path().filename().string();
                std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                if (name.size() <= stem.size() + 5u || name.compare(0u, stem.size(), stem) != 0 || name[stem.size()] != '-' ||
                    name.substr(name.size() - 4u) != ".adb")
                    continue;
                auto const rest = name.substr(stem.size() + 1u, name.size() - stem.size() - 5u);
                auto const last = rest.substr(rest.rfind('-') == std::string::npos ? 0u : rest.rfind('-') + 1u);
                if (std::find(stubs.begin(), stubs.end(), last) != stubs.end())
                    found.push_back(entry.path());
            }
            for (auto const& f : found)
            {
                auto const path = std::filesystem::absolute(f, iec).lexically_normal().generic_string();
                if (path == mainPath || iImported.contains(path))
                    continue;
                std::ifstream stream{ path, std::ios::binary };
                if (!stream)
                    continue;
                std::ostringstream contents;
                contents << stream.rdbuf();
                std::string source = source_text(contents.str());
                if (!source.empty() && source.back() != '\n')
                    source += '\n';
                iImported.insert(path);
                generator().import_fragment(neolib::string{ path }, neolib::string{ source });
                std::string folded = source;
                fold_case(folded);
                for (auto const& named : units_named(folded))
                    work.push_back(named);
                load_subunits(path, folded);
            }
        };
        if (!mainPath.empty())
        {
            std::string folded{ aMainSource };
            fold_case(folded);
            load_subunits(mainPath, folded);
        }
        for (std::size_t i = 0u; i < work.size(); ++i)
        {
            auto const unit = work[i];
            if (!seen.insert(unit).second)
                continue;
            // (a library unit that is an instance has no body: of a declaration that is an instance and a body of its name, the one
            // compiled (written) later replaces the other (10.1.4(5)))
            bool skipSpec = false;
            bool skipBody = false;
            if (auto const specPath = unit_file(unit, false), bodyPath = unit_file(unit, true); specPath && bodyPath)
            {
                std::ifstream stream{ *specPath, std::ios::binary };
                std::ostringstream contents;
                contents << stream.rdbuf();
                std::string folded = source_text(contents.str());
                fold_case(folded);
                static std::regex const sInstance{ R"((^|[^a-z0-9_.])(procedure|function|package)\s+([a-z0-9_.]+)\s+is\s+new\s)" };
                bool instance = false;
                std::smatch m;
                for (auto it = folded.cbegin(); !instance && std::regex_search(it, folded.cend(), m, sInstance); it = m.suffix().first)
                    instance = m[3].str() == unit;
                if (instance)
                {
                    std::error_code tec;
                    auto const specTime = std::filesystem::last_write_time(*specPath, tec);
                    auto const bodyTime = std::filesystem::last_write_time(*bodyPath, tec);
                    if (bodyTime > specTime)
                        skipSpec = true;
                    else
                        skipBody = true;
                }
            }
            for (bool body : { false, true })
            {
                auto const path = unit_file(unit, body);
                if (!path || *path == mainPath || iImported.contains(*path))
                    continue;
                if ((body && skipBody) || (!body && skipSpec))
                    continue;
                std::ifstream stream{ *path, std::ios::binary };
                if (!stream)
                    continue;
                std::ostringstream contents;
                contents << stream.rdbuf();
                std::string source = source_text(contents.str());
                if (!source.empty() && source.back() != '\n')
                    source += '\n';
                iImported.insert(*path);
                generator().import_fragment(neolib::string{ *path }, neolib::string{ source });
                std::string folded = source;
                fold_case(folded);
                for (auto const& named : units_named(folded))
                    work.push_back(named);
                if (body)
                    load_subunits(*path, folded);
            }
        }
    }

    std::optional<std::string> compiler::unit_file(std::string const& aUnit, bool aBody) const
    {
        // (GNAT's file names (e.g. ada-text_io.ads) and the unit's name (e.g. Ada.Text_IO.ads), in any case)
        std::string gnat = aUnit;
        std::replace(gnat.begin(), gnat.end(), '.', '-');
        std::string const extension = aBody ? ".adb" : ".ads";
        std::error_code ec;
        for (auto const& folder : iSearchPath)
        {
            std::filesystem::directory_iterator it{ folder, ec };
            if (ec)
                continue;
            for (auto const& entry : it)
            {
                auto name = entry.path().filename().string();
                std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                if (name == gnat + extension || name == aUnit + extension)
                    return std::filesystem::absolute(entry.path(), ec).lexically_normal().generic_string();
            }
        }
        return {};
    }

    void compiler::generate()
    {
        try
        {
            iProgram = std::make_unique<program>(*this, iCompilations);
            iProgram->generate();
        }
        catch (compile_error const& e)
        {
            // (errors recovered from (NEOS_ADA_ALL_ERRORS): the others, then the first)
            if (iProgram != nullptr)
                for (auto const& d : iProgram->diagnostics())
                    if (d.where != e.where || std::string{ d.what() } != e.what())
                    {
                        std::string location;
                        if (d.where != nullptr && d.where->file != nullptr)
                            location = d.where->file->path + "(" + std::to_string(d.where->file->line_of(d.where->source)) + "," +
                                std::to_string(d.where->file->column_of(d.where->source)) + "): ";
                        std::cerr << location << "error: " << d.what() << std::endl;
                    }
            report(e);
        }
    }

    void compiler::report(compile_error const& aError)
    {
        pending_error error;
        error.message = aError.what();
        if (aError.where != nullptr && aError.where->file != nullptr)
        {
            error.file = aError.where->file->path;
            error.line = aError.where->file->line_of(aError.where->source);
            error.column = aError.where->file->column_of(aError.where->source);
        }
        iPendingError = error;
        auto const& fragment = generator().current_fragment();
        generator().throw_error(fragment.begin(), neolib::string{ error.message }, neolib::string{ "error" });
    }
}
