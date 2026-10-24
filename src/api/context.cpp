/*
  context.cpp

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

#include <cstdlib>
#include <neolib/neolib.hpp>

#include <algorithm>
#include <iostream>
#include <filesystem>
#include <fstream>
#include <functional>
#include <cctype>
#include <sstream>

#include <neolib/core/string_utf.hpp>
#include <neolib/app/application.hpp>
#include <neolib/file/file.hpp>
#include <neos/context.hpp>

namespace neos
{
    context::context(std::istream& aCin, std::ostream& aCout, std::ostream& aCerr) :
        iCin{ aCin },
        iCout{ aCout },
        iCerr{ aCerr },
        iPrivateApplication{ std::make_unique<neolib::application<>>(neolib::application_info{ "neos", "i42 software", {}, "Copyright (c) 2025 Leigh Johnston", neos::home(), {}, {}, ".ncl" }) },
        iApplication{ *iPrivateApplication },
        iCompiler{ *this }
    {
        init();
    }

    context::context(std::istream& aCin, std::ostream& aCout, std::ostream& aCerr, neolib::i_service_provider& aServiceProvider) :
        iCin{ aCin },
        iCout{ aCout },
        iCerr{ aCerr },
        iPrivateApplication{ std::make_unique<neolib::application<>>(neolib::application_info{ "neos", "i42 software", {}, "Copyright (c) 2025 Leigh Johnston", neos::home(), {}, {}, ".ncl" }, aServiceProvider) },
        iApplication{ *iPrivateApplication },
        iCompiler{ *this }
    {
        init();
    }

    context::context(std::istream& aCin, std::ostream& aCout, std::ostream& aCerr, neolib::i_application& aApplication) :
        iCin{ aCin },
        iCout{ aCout },
        iCerr{ aCerr },
        iApplication{ aApplication },
        iCompiler{ *this }
    {
        iApplication.plugin_manager().plugin_file_extensions().clear();
        iApplication.plugin_manager().plugin_file_extensions().push_back(neolib::string{ ".ncl" });
        init();
    }

    context::~context()
    {
        for (auto& t : iThreads)
            if (t->joinable())
                t->join();
    }

    std::istream& context::cin()
    {
        return iCin;
    }

    std::ostream& context::cout()
    {
        return iCout;
    }

    std::ostream& context::cerr()
    {
        return iCerr;
    }

    const context::concept_libraries_t& context::concept_libraries() const
    {
        return iConceptLibraries;
    }

    void context::find_concept(neolib::i_string_view const& aSymbol, neolib::i_ref_ptr<language::i_semantic_concept>& aResult) const
    {
        std::lock_guard<std::mutex> lock{ iConceptCacheMutex };
        auto const symbol = aSymbol.to_std_string_view();
        if (auto const existing = iConceptCache.find(symbol); existing != iConceptCache.end())
        {
            aResult = existing->second;
            return;
        }
        for (auto const& cl : iConceptLibraries)
        {
            if (cl.second()->find_concept(aSymbol, aResult))
            {
                iConceptCache.emplace(std::string{ symbol }, neolib::ref_ptr<language::i_semantic_concept>{ aResult });
                return;
            }
        }
    }

    bool context::schema_loaded() const
    {
        return iSchema != nullptr;
    }

    void context::load_schema(std::string const& aSchemaPath)
    {
        std::string const schemaPath = (aSchemaPath.empty() && iSchema ? schema().path() : aSchemaPath);
        if (schemaPath.empty())
            throw no_schema_path_specified();
        iSchema.reset();
        iSchema = std::make_shared<language::schema>(schemaPath, concept_libraries(), compiler().show_parse_times());
        for (auto& unit : program().translationUnits)
            unit.schema = iSchema;
    }

    void context::load_schema(std::string const& aSchemaPath, std::string const& aSchemaSource)
    {
        iSchema.reset();
        iSchema = std::make_shared<language::schema>(aSchemaPath, concept_libraries(), compiler().show_parse_times(), aSchemaSource);
        for (auto& unit : program().translationUnits)
            unit.schema = iSchema;
    }

    void context::add_plugin(neolib::i_plugin& aPlugin)
    {
        iAddedPlugins.emplace_back(aPlugin);
        // a compiler plugin: the code generation of its language
        neolib::ref_ptr<language::i_language_compiler> languageCompiler;
        if (aPlugin.discover(languageCompiler))
        {
            iCompiler.add_language_compiler(*languageCompiler);
            return;
        }
        neolib::ref_ptr<language::i_concept_library> library(aPlugin);
        {
            std::lock_guard<std::mutex> lock{ iConceptCacheMutex };
            iConceptCache.clear();
        }
        iConceptLibraries[library->name()] = library;
        auto add_sublibraries = [this](auto& self, auto& aParentLibrary) -> void
        {
            for (auto& sublibrary : aParentLibrary.sublibraries())
            {
                iConceptLibraries[sublibrary.first()] = sublibrary.second();
                self(self, *sublibrary.second());
            }
        };
        add_sublibraries(add_sublibraries, *library);
    }

    void context::add_package(std::string const& aPath, std::string const& aSource)
    {
        iPackages[std::filesystem::path{ aPath }.generic_string()] = aSource;
    }

    const language::schema& context::schema() const
    {
        if (!schema_loaded())
            throw no_schema_loaded();
        return *iSchema;
    }

    void context::load_program(std::string const& aPath)
    {
        std::string const path = (aPath.empty() && !iProgram.translationUnits.empty() ? iProgram.translationUnits.front().fragments.front().source_file_path()->to_std_string() : aPath);
        if (!schema_loaded() && !path.empty())
        {
            // (no schema specified: the installed schema for the program's file type)
            auto const extension = std::filesystem::path{ path }.extension().string();
            auto const schemaPath = language::schema::find(extension);
            if (!schemaPath)
                throw compiler_error("no schema loaded and no schema found for file type '" + extension + "'");
            load_schema(*schemaPath);
        }
        iProgram = decltype(iProgram){};
        auto& unit = load_unit(language::source_fragment{ neolib::string{ path } });
    }

    void context::load_program(std::istream& aStream)
    {
        auto& unit = load_unit(language::source_fragment{}, aStream);
    }

    language::compiler& context::compiler()
    {
        return iCompiler;
    }

    void context::compile_program()
    {
        compiler().compile(program());
    }

    const context::program_t& context::program() const
    {
        return iProgram;
    }

    context::program_t& context::program()
    {
        return iProgram;
    }

    const text& context::text() const
    {
        return program().text;
    }

    bool context::running() const
    {
        for (auto const& t : iThreads)
            if (t->joinable())
                return true;
        return false;
    }

    bool context::restricted() const
    {
        return iRestricted;
    }

    void context::set_restricted(bool aRestricted)
    {
        iRestricted = aRestricted;
    }

    void context::set_arguments(std::vector<std::string> const& aArguments)
    {
        iArguments = aArguments;
    }

    void context::run()
    {
        if (text().empty())
            throw no_text();
        for (auto t = iThreads.begin(); t != iThreads.end();)
        {
            if ((**t).joinable())
                (**t).join();
            t = iThreads.erase(t);
        }
        // (source that the program compiles while it runs (e.g. Python's exec): its program (a translation unit of the 
        // context's schema), compiled by the context's compiler)
        bytecode::vm::set_compile_service([this](std::string const& aLanguage, std::string const& aSource, std::string const& aSourceName,
            std::string const& aOptions, std::string& aError) -> std::optional<neos::text>
            {
                if (!schema_loaded() || schema().meta().language != aLanguage)
                {
                    aError = "no schema for language '" + aLanguage + "'";
                    return {};
                }
                program_t compiled;
                neolib::string source{ aSource };
                if (!source.empty() && source.to_std_string_view().back() != '\n')
                    source.append("\n", 1u);
                compiled.translationUnits.push_back(translation_unit_t{ iSchema, { language::source_fragment{ neolib::string{ aSourceName }, source } },
                    language::ast{ compiled.symbolTable } });
                compiler().set_language_options(aLanguage, aOptions);
                compiler().set_quiet(true);
                struct reset { context& c; std::string const& language; ~reset() { c.compiler().set_language_options(language, {}); c.compiler().set_quiet(false); } } const resetOptions{ *this, aLanguage };
                try
                {
                    if (!compiler().compile(compiled))
                    {
                        aError = "compilation failed";
                        return {};
                    }
                }
                catch (language::compiler_error const& e)
                {
                    // (a compile error of the source: its location and message)
                    aError = e.diagnostic.empty() ? std::string{ e.what() } : e.diagnostic;
                    return {};
                }
                catch (std::exception const& e)
                {
                    aError = e.what();
                    return {};
                }
                return compiled.text;
            });
        // (the program's arguments and the compile service above: the VM thread takes them from this thread (so that contexts that 
        // run programs at the same time each have their own))
        bytecode::vm::set_program_arguments(iArguments);
        iThreads.push_back(std::make_unique<bytecode::vm::thread>(text(), cout(), cin(), cerr(), iRestricted));
        iThreads.back()->join(); // run to completion: the program reads cin(), so the console must not read it concurrently
    }

    int context::exit_status() const
    {
        if (iThreads.empty())
            return EXIT_SUCCESS;
        if (iThreads.back()->error())
            return EXIT_FAILURE;
        return iThreads.back()->exit_status().value_or(EXIT_SUCCESS);
    }

    language::data_type context::evaluate(const std::string& aExpression)
    {
        program().translationUnits.clear();
        std::istringstream stream{ aExpression };
        load_program(stream);
        compile_program();
        run();
        if (iThreads.back()->error())
            throw error(*iThreads.back()->error());
        auto const result = iThreads.back()->result();
        iThreads.pop_back();
        return result;
    }

    const neolib::i_string& context::metrics() const
    {
        thread_local neolib::string result;
        for (auto const& t : iThreads)
            result += neolib::string{ t->metrics() };
        return result;
    }

    void context::init()
    {
        // (concept libraries (".ncl") and compiler plugins (".ncp"))
        auto& extensions = iApplication.plugin_manager().plugin_file_extensions();
        if (std::find(extensions.begin(), extensions.end(), neolib::string{ ".ncp" }) == extensions.end())
            extensions.push_back(neolib::string{ ".ncp" });
        // (the context's own application: neos's home's (not also the folder of the running program, if that is not neos (e.g. an IDE)))
        if (iPrivateApplication)
        {
            auto& folders = iApplication.plugin_manager().plugin_folders();
            folders.clear();
            folders.push_back(neolib::string{ neos::home() });
        }
        iApplication.plugin_manager().load_plugins();
        for (neolib::ref_ptr<neolib::i_plugin> plugin : iApplication.plugin_manager().plugins())
            add_plugin(*plugin);
    }

    context::translation_unit_t& context::load_unit(language::source_fragment&& aFragment)
    {
        if (!schema_loaded())
            throw compiler_error("no schema loaded");

        std::ifstream fragmentStream{ aFragment.source_file_path()->to_std_string(), std::ios::binary};
        if (!fragmentStream)
            throw compiler_error("failed to open source file '" + aFragment.source_file_path()->to_std_string() + "'");

        return load_unit(std::move(aFragment), fragmentStream);
    }

    context::translation_unit_t& context::load_unit(language::source_fragment&& aFragment, std::istream& aStream)
    {
        if (!schema_loaded())
            throw compiler_error("no schema loaded");

        auto& unit = *program().translationUnits.insert(program().translationUnits.end(), 
            translation_unit_t{ iSchema, { std::move(aFragment) }, language::ast{ program().symbolTable } });

        load_fragment(unit.fragments.back(), aStream);

        return unit;
    }

    void context::load_fragment(language::i_source_fragment& aFragment)
    {
        if (aFragment.source_file_path() == std::nullopt)
            throw invalid_fragment();

        auto const sourceFilePath = aFragment.source_file_path().value().to_std_string();
        // (a package's source that is not a file (see add_package))
        if (!iPackages.empty() && schema_loaded())
            for (auto const& ext : schema().meta().sourcecodeFileExtension)
            {
                auto const tryPath = (std::filesystem::path{ schema().path() }.parent_path() / "packages" / schema().meta().language / (sourceFilePath + ext)).generic_string();
                if (auto const package = iPackages.find(tryPath); package != iPackages.end())
                {
                    aFragment.source_file_path().value() = tryPath;
                    std::istringstream packageStream{ package->second };
                    load_fragment(aFragment, packageStream);
                    return;
                }
            }
        std::optional<std::ifstream> fragmentStream;
        fragmentStream.emplace(sourceFilePath, std::ios::binary);
        if (!*fragmentStream)
            for (auto const& ext : schema().meta().sourcecodeFileExtension)
            {
                // (packages live alongside the schema, not relative to the current working directory)
                std::string tryPath = (std::filesystem::path{ schema().path() }.parent_path() / "packages" / schema().meta().language / (sourceFilePath + ext)).generic_string();
                fragmentStream.emplace(tryPath, std::ios::binary);
                if (*fragmentStream)
                {
                    aFragment.source_file_path().value() = tryPath;
                    break;
                }
                tryPath = compiler().current_fragment().source_directory_path().value() + "/" + sourceFilePath + ext;
                fragmentStream.emplace(tryPath, std::ios::binary);
                if (*fragmentStream)
                {
                    aFragment.source_file_path().value() = tryPath;
                    break;
                }
            }
        if (!*fragmentStream)
            throw compiler_error("failed to open source file '" + sourceFilePath + "'");

        load_fragment(aFragment, *fragmentStream);
    }

    namespace
    {
        void terminate_last_line(neolib::string& aSource)
        {
            // the parser does not recognize a token at the very end of the source so ensure the last line is terminated
            if (!aSource.empty() && aSource.to_std_string_view().back() != '\n')
                aSource.append("\n", 1u);
        }
    }

    void context::load_fragment(language::i_source_fragment& aFragment, std::istream& aStream)
    {
        if (!aStream)
            throw compiler_error("input stream bad");

        typedef typename std::istream::pos_type pos_type;
        pos_type count = 0;
        aStream.seekg(0, std::ios::end);
        if (aStream)
        {
            count = aStream.tellg();
            if (count == static_cast<pos_type>(-1))
                count = 0;
            aStream.seekg(0, std::ios::beg);
        }
        else
            aStream.clear();

        if (count != std::istream::pos_type(0))
        {
            thread_local neolib::string source;
            source.clear();
            source.reserve(static_cast<language::source_t::size_type>(count) + 1);
            source.resize(static_cast<language::source_t::size_type>(count));
            aStream.read(&source[0], count);
            source.resize(static_cast<language::source_t::size_type>(aStream.gcount()));
            terminate_last_line(source);
            aFragment.set_source(source.to_string_view());
        }
        else
        {
            char buffer[1024];
            thread_local neolib::string source;
            source.clear();
            while (aStream.read(buffer, sizeof(buffer)))
                source.append(buffer, static_cast<language::source_t::size_type>(aStream.gcount()));
            if (aStream.eof())
                source.append(buffer, static_cast<language::source_t::size_type>(aStream.gcount()));
            terminate_last_line(source);
            aFragment.set_source(source.to_string_view());
        }

        if (aFragment.source().empty())
            throw compiler_error("no source code");

        if (!neolib::check_utf8(aFragment.source()))
            throw compiler_error("source file has invalid utf-8");
    }

    namespace
    {
        // (the highlighting category of a semantic concept, by its name, if it is one that is highlighted)
        std::optional<context::highlight_category> highlight_category_of(std::string_view aConcept)
        {
            auto const is = [&](std::string_view aName) { return aConcept == aName || (aConcept.starts_with(aName) && aConcept.size() > aName.size() && aConcept[aName.size()] == '.'); };
            if (is("language.comment"))
                return context::highlight_category::Comment;
            if (is("language.keyword"))
                return context::highlight_category::Keyword;
            if (is("language.type")) // (before strings: e.g. language.type.string)
                return context::highlight_category::Type;
            if (is("string") || aConcept.ends_with(".string") || aConcept.find(".template.") != std::string_view::npos)
                return context::highlight_category::String;
            if (is("math.universal") || aConcept.ends_with(".number"))
                return context::highlight_category::Number;
            if (is("language.object.boolean") || aConcept.ends_with(".null") || aConcept.ends_with(".undefined"))
                return context::highlight_category::Constant;
            if (is("language.function.name"))
                return context::highlight_category::Function;
            return {};
        }

        bool highlight_parse(language::schema_stage const& aStage, std::string_view aSource)
        {
            static std::ostream sQuiet{ nullptr };
            aStage.parser->set_debug_output(sQuiet, false, false);
            if (aStage.root)
                return aStage.parser->parse(aStage.symbolMap->at(aStage.root.value()), aSource);
            return aStage.parser->parse(aSource);
        }
    }

    std::vector<context::highlight_span> context::highlight(std::string const& aSchemaPath, std::string_view aSource)
    {
        std::vector<highlight_span> result;
        try
        {
            auto existing = iHighlightSchemas.find(aSchemaPath);
            if (existing == iHighlightSchemas.end())
            {
                // (a schema that cannot be loaded is not tried again)
                existing = iHighlightSchemas.emplace(aSchemaPath, std::pair<std::shared_ptr<language::schema>, std::shared_ptr<language::schema>>{}).first;
                auto complete = std::make_shared<language::schema>(aSchemaPath, concept_libraries());
                for (auto const& stage : complete->pipeline())
                    for (auto const& discard : *stage->discard)
                        stage->parser->ignore(stage->symbolMap->at(discard));
                auto whole = std::make_shared<language::schema>(aSchemaPath, concept_libraries());
                // (a source is highlighted again as it is edited: its parsers parse incrementally (each parse reuses those of the previous 
                // source that its edit does not affect); the trees that they parse are of their own copies of it (see neolib::parser::text))
                for (auto const& highlightSchema : { complete, whole })
                    for (auto const& stage : highlightSchema->pipeline())
                        stage->parser->set_incremental(true);
                existing->second = { complete, whole };
            }
            auto const& [complete, whole] = existing->second;
            if (!complete || !whole)
                return result;
            // (the parser does not recognize a token at the very end of a source so its last line is terminated (see terminate_last_line))
            std::string script{ aSource };
            if (!script.empty() && script.back() != '\n')
                script += '\n';
            // (the source of a part (aValue) of a text parsed: the script, or the script as the language rewrites it (the position in the 
            // source of each byte of the text: none, the same)): none if it is not the source's)
            auto source_of = [&](std::string_view aValue, std::string_view aText, std::vector<std::uint32_t> const* aPositions) -> std::optional<std::string_view>
            {
                if (aValue.empty() || aValue.data() < aText.data() || aValue.data() + aValue.size() > aText.data() + aText.size())
                    return {};
                std::size_t first = static_cast<std::size_t>(aValue.data() - aText.data());
                std::string_view value = aValue;
                if (aPositions)
                {
                    // (a part of the rewrite's is not the source's, nor are the bytes that it added at a part's ends (e.g. a prefix of a name))
                    auto const NoPosition = language::i_language_compiler::NoPosition;
                    auto begin = first;
                    auto end = first + value.size();
                    while (begin < end && (*aPositions)[begin] == NoPosition)
                        ++begin;
                    while (end > begin && (*aPositions)[end - 1u] == NoPosition)
                        --end;
                    if (begin == end)
                        return {};
                    value = aText.substr(begin, end - begin);
                    first = (*aPositions)[begin];
                    auto const last = (*aPositions)[end - 1u];
                    if (last == NoPosition || last < first || last - first != value.size() - 1u)
                        return {};
                }
                // (nor is one of text that the rewrite changed)
                if (first + value.size() > aSource.size() || aSource.substr(first, value.size()) != value)
                    return {};
                return aSource.substr(first, value.size());
            };
            auto push = [&](std::string_view aValue, highlight_category aCategory, std::string_view aText, std::vector<std::uint32_t> const* aPositions)
            {
                if (auto const source = source_of(aValue, aText, aPositions))
                {
                    auto const first = static_cast<std::size_t>(source->data() - aSource.data());
                    result.push_back(highlight_span{ first, first + source->size(), aCategory });
                }
            };
            // (whether a node's concept is a (generic) concept: it, or one that its concept derives from (see i_semantic_concept::is), e.g. a 
            // language's own concept for its parameters can be language.function.parameters)
            auto const is_a = [&](language::parser::cst_node const& aNode, std::string_view aConcept)
            {
                if (!aNode.c)
                    return false;
                std::string_view const name{ *aNode.c };
                if (name == aConcept)
                    return true;
                neolib::ref_ptr<language::i_semantic_concept> nodeConcept;
                find_concept(neolib::string_view{ name }, nodeConcept);
                return nodeConcept && nodeConcept->is(neolib::string_view{ aConcept });
            };
            // (the spans of the nodes of a tree of a text parsed (see push))
            auto add = [&](language::parser::cst_node const& aRoot, std::string_view aText, std::vector<std::uint32_t> const* aPositions)
            {
                auto const within = [&](std::string_view aValue) { return aValue.data() >= aText.data() && aValue.data() + aValue.size() <= aText.data() + aText.size(); };
                // (a function's parameters (their names (language.identifier): not within an expression (e.g. a default value), and of an 
                // initialization only what it initializes; as parsed and in the source) and the text that they are its in: from its parameters to 
                // the end of its body (the node that its parameters are in, and the lines after its parameters' that are indented more than it); 
                // a parameter's name, and a name in its body that is it, are its parameter's)
                struct function_scope
                {
                    std::size_t first;
                    std::size_t last;
                    std::vector<std::string_view> parameters;
                    std::vector<std::string_view> sources;
                };
                std::vector<function_scope> functions;
                auto const function_of = [&](language::parser::cst_node const& aParameters, language::parser::cst_node const* aFunction)
                {
                    function_scope result{ static_cast<std::size_t>(aParameters.value.data() - aText.data()), aText.size() };
                    std::vector<language::parser::cst_node const*> parameterNodes{ &aParameters };
                    while (!parameterNodes.empty())
                    {
                        auto const& node = *parameterNodes.back();
                        parameterNodes.pop_back();
                        if (is_a(node, "language.identifier"))
                        {
                            if (within(node.value))
                            {
                                result.parameters.push_back(node.value);
                                if (auto const source = source_of(node.value, aText, aPositions))
                                    result.sources.push_back(*source);
                            }
                            continue;
                        }
                        if (is_a(node, "language.expression"))
                            continue;
                        if (is_a(node, "language.initialize"))
                        {
                            if (!node.children.empty())
                                parameterNodes.push_back(node.children.front().get());
                            continue;
                        }
                        for (auto const& child : node.children)
                            parameterNodes.push_back(child.get());
                    }
                    auto const lineStart = aText.rfind('\n', result.first) == std::string_view::npos ? 0u : aText.rfind('\n', result.first) + 1u;
                    auto const indentation = std::min(aText.find_first_not_of(" \t", lineStart), result.first) - lineStart;
                    for (auto lineEnd = aText.find('\n', result.first); lineEnd != std::string_view::npos;)
                    {
                        auto const next = lineEnd + 1u;
                        auto const nextEnd = std::min(aText.find('\n', next), aText.size());
                        auto const content = aText.find_first_not_of(" \t\r", next);
                        if (content != std::string_view::npos && content < nextEnd && content - next <= indentation)
                        {
                            result.last = next;
                            break;
                        }
                        lineEnd = nextEnd < aText.size() ? nextEnd : std::string_view::npos;
                    }
                    if (aFunction != nullptr && within(aFunction->value))
                        result.last = std::max(result.last, static_cast<std::size_t>(aFunction->value.data() + aFunction->value.size() - aText.data()));
                    return result;
                };
                std::vector<std::string_view> identifiers;
                // (the nodes, and their nearest ancestors that have concepts)
                std::vector<std::pair<language::parser::cst_node const*, language::parser::cst_node const*>> nodes{ { &aRoot, nullptr } };
                while (!nodes.empty())
                {
                    auto const [nodePtr, parent] = nodes.back();
                    auto const& node = *nodePtr;
                    nodes.pop_back();
                    if (node.c)
                    {
                        if (auto const category = highlight_category_of(std::string_view{ *node.c }))
                        {
                            push(node.value, *category, aText, aPositions);
                            continue;
                        }
                        if (is_a(node, "language.function.parameters") && within(node.value))
                            functions.push_back(function_of(node, parent));
                        else if (is_a(node, "language.identifier") && within(node.value))
                            identifiers.push_back(node.value);
                    }
                    for (auto child = node.children.rbegin(); child != node.children.rend(); ++child)
                        nodes.emplace_back(child->get(), node.c ? &node : parent);
                }
                // (a name in a function's body that is one of its parameters' (as parsed, or in the source (e.g. a rewrite's name for it)) is its 
                // parameter)
                for (auto const& identifier : identifiers)
                {
                    auto const at = static_cast<std::size_t>(identifier.data() - aText.data());
                    auto const source = source_of(identifier, aText, aPositions);
                    for (auto const& function : functions)
                        if (at >= function.first && at < function.last && 
                            (std::find(function.parameters.begin(), function.parameters.end(), identifier) != function.parameters.end() ||
                                (source && std::find(function.sources.begin(), function.sources.end(), *source) != function.sources.end())))
                        {
                            push(identifier, highlight_category::Parameter, aText, aPositions);
                            break;
                        }
                }
            };
            // (the first stage (e.g. a tokenizer): its tokens and comments (even if the source does not parse as a whole))
            auto const& first = *whole->pipeline().front();
            bool const single = whole->pipeline().size() == 1u;
            if (highlight_parse(first, script))
            {
                if (single)
                {
                    first.parser->create_ast();
                    add(first.parser->ast(), first.parser->text(), nullptr);
                }
                else
                    add(first.parser->cst(), first.parser->text(), nullptr);
            }
            // (the AST of the last stage (as the compiler parses the source, as the language's own pass over it rewrites it): spans of it 
            // replace those of the first stage)
            if (!single)
            {
                std::string rewritten;
                std::vector<std::uint32_t> positions;
                bool const isRewritten = compiler().parsable_source(complete->meta().language, script, rewritten, positions);
                std::string_view const text = isRewritten ? std::string_view{ rewritten } : std::string_view{ script };
                bool parsed = true;
                for (auto const& stage : complete->pipeline())
                    if (!(parsed = highlight_parse(*stage, text)))
                        break;
                if (parsed)
                {
                    auto const& last = *complete->pipeline().back();
                    last.parser->create_ast();
                    add(last.parser->ast(), last.parser->text(), isRewritten ? &positions : nullptr);
                }
            }
        }
        catch (...)
        {
            result.clear();
        }
        return result;
    }

    namespace
    {
        // (whether a concept is one of a name: the name, or a concept whose name ends with ".<name>" (e.g. python.for, javascript.for))
        bool named(std::string_view aConcept, std::string_view aName)
        {
            return aConcept == aName || 
                (aConcept.size() > aName.size() && aConcept.ends_with(aName) && aConcept[aConcept.size() - aName.size() - 1u] == '.');
        }
    }

    std::vector<context::outline_node> context::outline(std::string const& aSchemaPath, std::string_view aSource)
    {
        std::vector<outline_node> result;
        try
        {
            auto existing = iOutlineSchemas.find(aSchemaPath);
            if (existing == iOutlineSchemas.end())
            {
                // (a schema that cannot be loaded is not tried again)
                existing = iOutlineSchemas.emplace(aSchemaPath, nullptr).first;
                auto schema = std::make_shared<language::schema>(aSchemaPath, concept_libraries());
                for (auto const& stage : schema->pipeline())
                {
                    for (auto const& discard : *stage->discard)
                        stage->parser->ignore(stage->symbolMap->at(discard));
                    stage->parser->set_incremental(true);
                }
                existing->second = schema;
            }
            auto const& schema = existing->second;
            if (!schema)
                return result;
            std::string script{ aSource };
            if (!script.empty() && script.back() != '\n')
                script += '\n';
            for (auto const& stage : schema->pipeline())
                if (!highlight_parse(*stage, script))
                    return result;
            auto const& last = *schema->pipeline().back();
            last.parser->create_ast();
            std::string_view const text = last.parser->text();

            using node_t = language::parser::cst_node;
            auto const name_of = [](node_t const& aNode) { return aNode.c ? std::string_view{ *aNode.c } : std::string_view{}; };
            // (a node's text in the source, without the whitespace at its ends)
            auto const first_of = [&](node_t const& aNode) -> std::size_t
            {
                auto first = std::min<std::size_t>(std::max<std::ptrdiff_t>(aNode.value.data() - text.data(), 0), aSource.size());
                auto const end = std::min(first + aNode.value.size(), aSource.size());
                while (first < end && std::isspace(static_cast<unsigned char>(aSource[first])))
                    ++first;
                return first;
            };
            auto const last_of = [&](node_t const& aNode) -> std::size_t
            {
                auto const first = first_of(aNode);
                auto end = std::min<std::size_t>(std::max<std::ptrdiff_t>(aNode.value.data() - text.data(), 0) + aNode.value.size(), aSource.size());
                while (end > first && std::isspace(static_cast<unsigned char>(aSource[end - 1u])))
                    --end;
                return end;
            };
            // (the statements, and the parts of them, that the outline has: by (generic) childConcept)
            auto const ignored = [&](std::string_view aConcept)
            {
                return aConcept.empty() || named(aConcept, "keyword") || named(aConcept, "scope.open") || named(aConcept, "scope.close") ||
                    named(aConcept, "indentation") || named(aConcept, "comment") || aConcept.starts_with("language.comment");
            };
            auto const is_block = [&](std::string_view aConcept)
            {
                return named(aConcept, "scope") || named(aConcept, "body") || named(aConcept, "block");
            };
            auto const kind_of = [&](std::string_view aConcept) -> std::optional<outline_kind>
            {
                auto const any = [&](std::initializer_list<std::string_view> aNames) 
                { 
                    return std::any_of(aNames.begin(), aNames.end(), [&](std::string_view aName) { return named(aConcept, aName); }); 
                };
                if (any({ "function", "method" }))
                    return outline_kind::Function;
                if (any({ "class", "struct", "interface" }))
                    return outline_kind::Class;
                if (any({ "statement.loop", "loop", "for", "for.in", "for.of", "while", "do" }))
                    return outline_kind::Loop;
                if (any({ "if", "elif", "else", "switch", "case", "default", "try", "except", "catch", "finally", "with", "match" }))
                    return outline_kind::Branch;
                if (any({ "return", "function.return", "break", "continue", "throw", "raise" }))
                    return outline_kind::Return;
                return {};
            };
            auto const is_alternative = [&](std::string_view aConcept)
            {
                return named(aConcept, "elif") || named(aConcept, "else") || named(aConcept, "except") || named(aConcept, "catch") || named(aConcept, "finally");
            };
            auto const statement_like = [&](std::string_view aConcept)
            {
                return kind_of(aConcept).has_value() || named(aConcept, "statement") || named(aConcept, "let") || named(aConcept, "const") || 
                    named(aConcept, "var") || named(aConcept, "declaration") ||
                    (named(aConcept, "expression") && !aConcept.starts_with("language.") && !aConcept.starts_with("math.") && !aConcept.starts_with("boolean."));
            };
            // (the children of a node that have concepts (those of its children that have none are looked through))
            std::function<void(node_t const&, std::vector<node_t const*>&)> concept_children = [&](node_t const& aNode, std::vector<node_t const*>& aChildren)
            {
                for (auto const& child : aNode.children)
                    if (child->c)
                        aChildren.push_back(child.get());
                    else
                        concept_children(*child, aChildren);
            };
            // (the statements of a body: its children (those in blocks within it too), except the parts of them that are not statements)
            std::function<void(node_t const&, std::vector<node_t const*>&)> statements_of = [&](node_t const& aNode, std::vector<node_t const*>& aStatements)
            {
                std::vector<node_t const*> children;
                concept_children(aNode, children);
                for (auto const child : children)
                {
                    auto const childConcept = name_of(*child);
                    if (ignored(childConcept))
                        continue;
                    if (is_block(childConcept))
                        statements_of(*child, aStatements);
                    else
                        aStatements.push_back(child);
                }
            };
            // (a statement that is only a statement of another kind (e.g. a loop or a return): it)
            auto const inner = [&](node_t const& aNode) -> node_t const&
            {
                node_t const* node = &aNode;
                while (!kind_of(name_of(*node)) && named(name_of(*node), "statement"))
                {
                    std::vector<node_t const*> children;
                    concept_children(*node, children);
                    std::erase_if(children, [&](node_t const* aChild) { return ignored(name_of(*aChild)); });
                    if (children.size() != 1u || !kind_of(name_of(*children[0])))
                        break;
                    node = children[0];
                }
                return *node;
            };
            auto const indentation = [&](std::size_t aPosition)
            {
                auto const lineStart = aSource.rfind('\n', aPosition == 0u ? 0u : aPosition - 1u);
                return aPosition - (lineStart == std::string_view::npos || lineStart >= aPosition ? 0u : lineStart + 1u);
            };
            auto const has_body = [&](std::uint32_t aIndex)
            {
                for (auto i = aIndex + 1u; i < result.size(); ++i)
                    if (result[i].parent == aIndex && result[i].kind != outline_kind::Parameter)
                        return true;
                return false;
            };
            auto const compound = [](outline_kind aKind)
            {
                return aKind == outline_kind::Function || aKind == outline_kind::Class || aKind == outline_kind::Loop || aKind == outline_kind::Branch;
            };

            std::function<void(std::vector<node_t const*> const&, std::uint32_t)> add_statements;
            auto const add_statement = [&](node_t const& aStatement, std::uint32_t aParent) -> std::uint32_t
            {
                auto const& node = inner(aStatement);
                auto const kind = kind_of(name_of(node)).value_or(outline_kind::Statement);
                auto const index = static_cast<std::uint32_t>(result.size());
                result.push_back(outline_node{ aParent, kind, first_of(aStatement), last_of(aStatement), last_of(aStatement) });
                if (!compound(kind))
                    return index;
                std::vector<node_t const*> children;
                concept_children(node, children);
                // (a function's parameters: those of its parameter list (all of the list's parts that are not types if it has none))
                if (kind == outline_kind::Function)
                {
                    std::function<node_t const*(node_t const&)> parameters_of = [&](node_t const& aNode) -> node_t const*
                    {
                        std::vector<node_t const*> nodeChildren;
                        concept_children(aNode, nodeChildren);
                        for (auto const child : nodeChildren)
                        {
                            if (named(name_of(*child), "parameters"))
                                return child;
                            if (is_block(name_of(*child)))
                                continue;
                            if (auto const found = parameters_of(*child))
                                return found;
                        }
                        return nullptr;
                    };
                    if (auto const parameters = parameters_of(node))
                    {
                        std::vector<node_t const*> parameterNodes;
                        concept_children(*parameters, parameterNodes);
                        bool const named_parameters = std::any_of(parameterNodes.begin(), parameterNodes.end(), 
                            [&](node_t const* aParameter) { return named(name_of(*aParameter), "parameter"); });
                        for (auto const parameter : parameterNodes)
                            if (named_parameters ? named(name_of(*parameter), "parameter") : 
                                !ignored(name_of(*parameter)) && !name_of(*parameter).starts_with("language.type"))
                                result.push_back(outline_node{ index, outline_kind::Parameter, first_of(*parameter), last_of(*parameter), last_of(*parameter) });
                    }
                }
                // (its body: its blocks, else a statement that ends it (or that is before an else); what is after an else is a branch of its own)
                bool const hasBlock = std::any_of(children.begin(), children.end(), [&](node_t const* aChild) { return is_block(name_of(*aChild)); });
                auto const is_else = [&](node_t const* aChild) { return named(name_of(*aChild), "keyword") && aChild->value == "else"; };
                // (without blocks: the statements that end it (e.g. a for's, or a switch's cases))
                std::size_t trailingStatements = children.size();
                for (auto i = children.size(); i > 0u; --i)
                {
                    auto const childConcept = name_of(*children[i - 1u]);
                    if (ignored(childConcept))
                        continue;
                    if (!statement_like(childConcept))
                        break;
                    trailingStatements = i - 1u;
                }
                std::optional<std::size_t> bodyStart;
                std::optional<std::size_t> elseStart;
                for (std::size_t i = 0u; i < children.size(); ++i)
                {
                    auto const& child = *children[i];
                    auto const childConcept = name_of(child);
                    if (is_else(&child))
                    {
                        elseStart = first_of(child);
                        continue;
                    }
                    if (elseStart)
                    {
                        if (!bodyStart)
                            bodyStart = *elseStart;
                        auto const elseIndex = static_cast<std::uint32_t>(result.size());
                        result.push_back(outline_node{ index, outline_kind::Branch, *elseStart, last_of(child), first_of(child) });
                        std::vector<node_t const*> statements;
                        if (is_block(childConcept))
                            statements_of(child, statements);
                        else
                            statements.push_back(&child);
                        add_statements(statements, elseIndex);
                        elseStart = std::nullopt;
                        continue;
                    }
                    if (ignored(childConcept))
                        continue;
                    bool const beforeElse = i + 1u < children.size() && is_else(children[i + 1u]);
                    if (is_block(childConcept) || (statement_like(childConcept) && (beforeElse || (!hasBlock && i >= trailingStatements))))
                    {
                        if (!bodyStart)
                            bodyStart = first_of(child);
                        std::vector<node_t const*> statements;
                        if (is_block(childConcept))
                            statements_of(child, statements);
                        else
                            statements.push_back(&child);
                        add_statements(statements, index);
                    }
                }
                if (bodyStart)
                    result[index].headerLast = *bodyStart;
                return index;
            };
            add_statements = [&](std::vector<node_t const*> const& aStatements, std::uint32_t aParent)
            {
                std::optional<std::uint32_t> previous;
                for (std::size_t i = 0u; i < aStatements.size();)
                {
                    auto const& statement = *aStatements[i++];
                    auto const index = add_statement(statement, aParent);
                    // (an alternative (else, elif, except, finally) of a branch that is a statement of its own (e.g. Python's): in the branch)
                    std::optional<std::uint32_t> alternativeOf;
                    if (result[index].kind == outline_kind::Branch && previous && result[*previous].kind == outline_kind::Branch && 
                        is_alternative(name_of(inner(statement))))
                    {
                        alternativeOf = *previous;
                        result[index].parent = *previous;
                    }
                    else
                        previous = index;
                    // (a compound statement whose body is not in it (e.g. Python's): the statements after it that are indented more than it)
                    if (compound(result[index].kind) && !has_body(index) && 
                        result[index].last > result[index].first && aSource[result[index].last - 1u] == ':')
                    {
                        auto const indent = indentation(result[index].first);
                        auto end = i;
                        while (end < aStatements.size() && indentation(first_of(*aStatements[end])) > indent)
                            ++end;
                        if (end > i)
                        {
                            std::vector<node_t const*> const body(aStatements.begin() + i, aStatements.begin() + end);
                            add_statements(body, index);
                            result[index].last = std::max(result[index].last, last_of(*aStatements[end - 1u]));
                            i = end;
                        }
                    }
                    if (alternativeOf)
                        result[*alternativeOf].last = std::max(result[*alternativeOf].last, result[index].last);
                }
            };
            result.push_back(outline_node{ outline_node::NoParent, outline_kind::Module, 0u, aSource.size(), 0u });
            std::vector<node_t const*> statements;
            statements_of(last.parser->ast(), statements);
            add_statements(statements, 0u);
        }
        catch (...)
        {
            result.clear();
        }
        return result;
    }

    std::vector<std::string> context::highlight_colours(std::string const& aSchemaPath, std::string const& aTheme)
    {
        // (highlighting.json: an object of themes, each an object of the categories' colours: "comment", "keyword", "string", "number", 
        // "constant", "type", "function" and "parameter"; the installed configuration's (beside the schema), then the user's, which override 
        // them: ~/.neos/highlighting.json (as vim's ~/.vim), or on Windows %LOCALAPPDATA%\neos\highlighting.json)
        static char const* const sCategories[] = { "comment", "keyword", "string", "number", "constant", "type", "function", "parameter" };
        static_assert(std::size(sCategories) == static_cast<std::size_t>(highlight_category::Parameter) + 1u);
        std::vector<std::string> result(std::size(sCategories));
        auto const apply = [&](std::filesystem::path const& aPath)
        {
            try
            {
                std::ifstream file{ aPath };
                if (!file)
                    return;
                neolib::rjson configuration{ file };
                auto const& themes = configuration.root().as<neolib::rjson_object>();
                if (!themes.has(aTheme))
                    return;
                auto const& theme = themes.at(aTheme).as<neolib::rjson_object>();
                for (std::size_t c = 0u; c < std::size(sCategories); ++c)
                    if (theme.has(sCategories[c]))
                        result[c] = std::string{ theme.at(sCategories[c]).as<neolib::rjson_string>() };
            }
            catch (...)
            {
                // (a configuration that cannot be read is not applied)
            }
        };
        apply(std::filesystem::path{ aSchemaPath }.parent_path() / "highlighting.json");
#if defined(_WIN32)
        if (auto const localAppData = std::getenv("LOCALAPPDATA"); localAppData != nullptr && *localAppData != '\0')
            apply(std::filesystem::path{ localAppData } / "neos" / "highlighting.json");
#else
        if (auto const home = std::getenv("HOME"); home != nullptr && *home != '\0')
            apply(std::filesystem::path{ home } / ".neos" / "highlighting.json");
#endif
        if (std::all_of(result.begin(), result.end(), [](std::string const& aColour) { return aColour.empty(); }))
            result.clear();
        return result;
    }
}