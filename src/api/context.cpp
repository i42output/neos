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
                existing->second = { complete, std::make_shared<language::schema>(aSchemaPath, concept_libraries()) };
            }
            auto const& [complete, whole] = existing->second;
            if (!complete || !whole)
                return result;
            // (the parser does not recognize a token at the very end of a source so its last line is terminated (see terminate_last_line))
            std::string script{ aSource };
            if (!script.empty() && script.back() != '\n')
                script += '\n';
            // (the spans of the nodes of a tree of a text parsed: the script, or the script as the language rewrites it (the position in 
            // the source of each byte of the text: none, the same))
            auto add = [&](language::parser::cst_node const& aRoot, std::string_view aText, std::vector<std::uint32_t> const* aPositions)
            {
                std::vector<language::parser::cst_node const*> nodes{ &aRoot };
                while (!nodes.empty())
                {
                    auto const& node = *nodes.back();
                    nodes.pop_back();
                    if (node.c)
                        if (auto const category = highlight_category_of(std::string_view{ *node.c }))
                        {
                            if (!node.value.empty() && node.value.data() >= aText.data() && node.value.data() + node.value.size() <= aText.data() + aText.size())
                            {
                                std::size_t first = static_cast<std::size_t>(node.value.data() - aText.data());
                                std::string_view value = node.value;
                                if (aPositions)
                                {
                                    // (a span of the rewrite's is not the source's, nor are the bytes that it added at a span's ends (e.g. a 
                                    // prefix of a name))
                                    auto const NoPosition = language::i_language_compiler::NoPosition;
                                    auto begin = first;
                                    auto end = first + value.size();
                                    while (begin < end && (*aPositions)[begin] == NoPosition)
                                        ++begin;
                                    while (end > begin && (*aPositions)[end - 1u] == NoPosition)
                                        --end;
                                    if (begin == end)
                                        continue;
                                    value = aText.substr(begin, end - begin);
                                    first = (*aPositions)[begin];
                                    auto const last = (*aPositions)[end - 1u];
                                    if (last == NoPosition || last < first || last - first != value.size() - 1u)
                                        continue;
                                }
                                // (nor is one of text that the rewrite changed)
                                if (first + value.size() <= aSource.size() && aSource.substr(first, value.size()) == value)
                                    result.push_back(highlight_span{ first, first + value.size(), *category });
                            }
                            continue;
                        }
                    for (auto child = node.children.rbegin(); child != node.children.rend(); ++child)
                        nodes.push_back(child->get());
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
                    add(first.parser->ast(), script, nullptr);
                }
                else
                    add(first.parser->cst(), script, nullptr);
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
                    add(last.parser->ast(), text, isRewritten ? &positions : nullptr);
                }
            }
        }
        catch (...)
        {
            result.clear();
        }
        return result;
    }
}