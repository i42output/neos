/*
  console.cpp

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

#include <neos/neos.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <chrono>
#include <filesystem>
#include <boost/lexical_cast.hpp>
#include <neolib/core/string_utils.hpp>
#include <neos/image.hpp>
#include <neos/console.hpp>

namespace neos
{
    namespace
    {
        // the path of an image of the program: aPath, else (in the current directory) the program's file name's stem with 
        // ".exe" on Windows and "a.out" elsewhere; it must not be one of the program's source files
        std::string image_path(neos::context const& aContext, std::string const& aPath)
        {
            std::string result = aPath;
            if (result.empty())
            {
#if defined(_WIN32)
                std::string stem = "a";
                if (!aContext.program().translationUnits.empty() && !aContext.program().translationUnits.front().fragments.empty())
                    if (auto const& path = aContext.program().translationUnits.front().fragments.front().source_file_path())
                        stem = std::filesystem::path{ path->to_std_string() }.stem().string();
                result = stem + ".exe";
#else
                result = "a.out";
#endif
            }
            for (auto const& unit : aContext.program().translationUnits)
                for (auto const& fragment : unit.fragments)
                    if (auto const& source = fragment.source_file_path())
                    {
                        std::error_code ec;
                        if (std::filesystem::equivalent(result, source->to_std_string(), ec))
                            throw std::runtime_error("the image would replace the source file '" + source->to_std_string() + "'");
                    }
            return result;
        }

        // a source syntax highlighted (see context::highlight) for a terminal: its runs in colours (ANSI escape sequences: 24-bit) as Visual 
        // Studio Code's dark theme's
        std::string highlighted(neos::context& aContext, std::string_view aSource)
        {
            std::vector<std::optional<neos::context::highlight_category>> categories(aSource.size());
            for (auto const& span : aContext.highlight(aContext.schema().path(), aSource))
                for (auto i = span.first; i < span.last && i < aSource.size(); ++i)
                    categories[i] = span.category;
            static char const* const sColours[] =
            {
                "\x1b[38;2;106;153;85m",   // Comment
                "\x1b[38;2;86;156;214m",   // Keyword
                "\x1b[38;2;206;145;120m",  // String
                "\x1b[38;2;181;206;168m",  // Number
                "\x1b[38;2;86;156;214m",   // Constant
                "\x1b[38;2;78;201;176m",   // Type
                "\x1b[38;2;220;220;170m"   // Function
            };
            std::string result;
            for (std::size_t first = 0u; first < aSource.size();)
            {
                auto last = first + 1u;
                while (last < aSource.size() && categories[last] == categories[first])
                    ++last;
                if (categories[first])
                    result.append(sColours[static_cast<std::size_t>(*categories[first])]).append(aSource.substr(first, last - first)).append("\x1b[0m");
                else
                    result.append(aSource.substr(first, last - first));
                first = last;
            }
            return result;
        }
    }

    // (an image of the program: with the compiler stub of its language (its compiler built in) if the program compiles source while it 
    // runs (e.g. Python's exec))
    std::string write_image(neos::context& aContext, std::string const& aPath)
    {
        auto const& language = aContext.schema().meta().language;
        auto const stub = aContext.compiler().compiles_at_run_time(language) ? neos::image::compiler_stub_path(language) : neos::image::stub_path();
        return neos::image::write(image_path(aContext, aPath), aContext.text(), aContext.restricted(), stub);
    }

    bool process_command(neos::context& aContext, bool& aInteractive, const std::string& aConsoleInput, bool aColour)
    {
        auto output_parse_time = [&aContext](std::chrono::steady_clock::duration aParseTime)
        {
            aContext.cout() << "Parse time: " << std::chrono::duration_cast<std::chrono::microseconds>(aParseTime).count() / 1000.0 << "ms" << std::endl;
        };
        auto output_compilation_time = [&aContext, &output_parse_time]()
        {
            if (aContext.compiler().preparse())
                aContext.cout() << "Pre-parse time: " << std::chrono::duration_cast<std::chrono::microseconds>(aContext.compiler().preparse_time()).count() / 1000.0 << "ms" << std::endl;
            output_parse_time(aContext.compiler().parse_time());
            aContext.cout() << "Compilation time: " <<
                std::chrono::duration_cast<std::chrono::microseconds>(aContext.compiler().end_time() - aContext.compiler().start_time()).count() / 1000.0 << "ms" << std::endl;
        };
        auto output_schema = [&aContext, &output_parse_time]()
        {
            output_parse_time(aContext.schema().parse_time());
            aContext.cout() << "Language: " << aContext.schema().meta().description + "\nVersion: " + aContext.schema().meta().version << std::endl;
            if (!aContext.schema().meta().copyright.empty())
                aContext.cout() << aContext.schema().meta().copyright << std::endl;
        };
        std::vector<std::pair<std::string::const_iterator, std::string::const_iterator>> words;
        const std::string delimeters{ " " };
        neolib::tokens(aConsoleInput.cbegin(), aConsoleInput.cend(), delimeters.cbegin(), delimeters.cend(), words);
        if (words.empty())
            return true;
        const std::string command{ words[0].first, words[0].second };
        if (command.empty())
            return true;
        std::string parameters{ words.size() > 1 ? words[1].first : aConsoleInput.cend(), aConsoleInput.cend() };
        try
        {
            if (aInteractive)
                aContext.evaluate(aConsoleInput);
            else if (command == "?" || command == "h" || command == "help")
            {
                aContext.cout() << "h(elp)\n"
                    << "s(chema) <path to language schema>       Load language schema\n"
                    << "l(oad) <path to program>                 Load program\n"
                    << "list                                     List program\n"
                    << "c(ompile)                                Compile program\n"
                    << "r(un)                                    Run program\n"
                    << "img [<path>]                             Make an image of the (compiled) program: an executable that\n"
                    << "                                         runs it (by default <program>.exe on Windows, else a.out)\n"
                    << "![<expression>]                          Evaluate expression (enter interactive mode if expression omitted)\n"
                    << ":<input>                                 Input (as stdin)\n"
                    << "q(uit)                                   Quit neos\n"
                    << "lc                                       List loaded concept libraries\n"
                    << "t(race) <0|1|2|3|4|5> [<filter>]         Compiler trace\n"
                    << "parsetimes [on|off]                      Output the time of each parse (off: only the total)\n"
                    << "preparse [on|off]                        A language's own pass over a source before it is parsed (on by\n"
                    << "                                         default)\n"
                    << "trap_on_overflow [on|off]                Integer arithmetic whose result overflows its (fixed size) type\n"
                    << "                                         traps (off by default: it wraps)\n"
                    << "bigint_promotion [on|off|schema]         Integers of any size: a result that overflows is promoted to an\n"
                    << "                                         arbitrary precision integer (e.g. Python's int); by default the\n"
                    << "                                         language's schema says (bigint.promotion)\n"
                    << "m(etrics)                                Display metrics for running programs\n"
                    << "restricted [on|off]                      Restricted mode: programs cannot call runtime plugin functions\n"
                    << "                                         that can change the host environment (off by default)\n"
                    << std::flush;
            }
            else if (command == "s" || command == "schema")
            {
                aContext.cout() << "Loading schema '" << (parameters.empty() && aContext.schema_loaded() ? aContext.schema().path() : parameters) << "'..." << std::endl;
                aContext.load_schema(parameters);
                output_schema();
            }
            else if (command == "l" || command == "load")
            {
                bool const schemaWasLoaded = aContext.schema_loaded();
                aContext.load_program(parameters);
                if (!schemaWasLoaded && aContext.schema_loaded()) // (the schema for the program's file type)
                {
                    aContext.cout() << "Loading schema '" << aContext.schema().path() << "'..." << std::endl;
                    output_schema();
                }
                aContext.cout() << "Program: " << parameters << std::endl;
            }
            else if (command == "c" || command == "compile")
            {
                aContext.compile_program();
                output_compilation_time();
            }
            else if (command == "list")
            {
                for (auto const& tu : aContext.program().translationUnits)
                    for (auto const& f : tu.fragments)
                    {
                        if (f.source_file_path() != std::nullopt)
                            aContext.cout() << "File '" << *f.source_file_path() << "':" << std::endl;
                        // (the source's last line is terminated when it is loaded (terminate_last_line): a newline only if it is not)
                        if (aColour)
                            aContext.cout() << highlighted(aContext, f.source().to_std_string_view());
                        else
                            aContext.cout() << f.source();
                        if (f.source().empty() || f.source().back() != '\n')
                            aContext.cout() << '\n';
                        aContext.cout() << std::flush;
                    }
            }
            else if (command == "r" || command == "run")
                aContext.run();
            else if (command == "img")
            {
                if (aContext.text().empty())
                    throw neos::context::no_text();
                auto const path = write_image(aContext, parameters);
                aContext.cout() << "Image: " << path << std::endl;
            }
            else if (command[0] == '!')
            {
                if (!aContext.schema_loaded())
                    throw std::runtime_error("no schema loaded");
                std::string expression(std::next(words[0].first), aConsoleInput.cend());
                if (!expression.empty())
                {
                    aContext.evaluate(expression);
                    output_compilation_time();
                }
                else
                    aInteractive = true;
            }
            else if (command == "lc")
            {
                auto print_concept_library = [&aContext](auto const& self, const neos::language::i_concept_library& aConceptLibrary, uint32_t aDepth = 0) -> void
                {
                    aContext.cout() << std::string(aDepth * 2, ' ') << "[" << aConceptLibrary.name() << "]";
                    if (aDepth == 0)
                        aContext.cout() << " (" << aConceptLibrary.uri() << ")";
                    aContext.cout() << std::endl;
                    for (auto const& conceptLibrary : aConceptLibrary.sublibraries())
                        self(self, *conceptLibrary.second(), aDepth + 1);
                };
                for (auto const& conceptLibrary : aContext.concept_libraries())
                    if (conceptLibrary.second()->depth() == 0)
                        print_concept_library(print_concept_library, *conceptLibrary.second());
            }
            else if (command == "restricted")
            {
                if (parameters == "on")
                    aContext.set_restricted(true);
                else if (parameters == "off")
                    aContext.set_restricted(false);
                else if (!parameters.empty())
                    throw std::runtime_error("restricted: expected 'on' or 'off'");
                aContext.cout() << "Restricted mode: " << (aContext.restricted() ? "on" : "off") << std::endl;
            }
            else if (command == "parsetimes")
            {
                if (parameters == "on")
                    aContext.compiler().set_show_parse_times(true);
                else if (parameters == "off")
                    aContext.compiler().set_show_parse_times(false);
                else if (!parameters.empty())
                    throw std::runtime_error("parsetimes: expected 'on' or 'off'");
                aContext.cout() << "Parse times: " << (aContext.compiler().show_parse_times() ? "on" : "off") << std::endl;
            }
            else if (command == "trap_on_overflow")
            {
                if (parameters == "on")
                    aContext.compiler().set_trap_on_overflow(true);
                else if (parameters == "off")
                    aContext.compiler().set_trap_on_overflow(false);
                else if (!parameters.empty())
                    throw std::runtime_error("trap_on_overflow: expected 'on' or 'off'");
                aContext.cout() << "Trap on overflow: " << (aContext.compiler().trap_on_overflow() ? "on" : "off") << std::endl;
            }
            else if (command == "bigint_promotion")
            {
                if (parameters == "on")
                    aContext.compiler().set_bigint_promotion_override(true);
                else if (parameters == "off")
                    aContext.compiler().set_bigint_promotion_override(false);
                else if (parameters == "schema")
                    aContext.compiler().set_bigint_promotion_override({});
                else if (!parameters.empty())
                    throw std::runtime_error("bigint_promotion: expected 'on', 'off' or 'schema'");
                auto const setting = aContext.compiler().bigint_promotion_override();
                aContext.cout() << "Bigint promotion: " << (setting ? (*setting ? "on" : "off") : "the schema's") << std::endl;
            }
            else if (command == "preparse")
            {
                if (parameters == "on")
                    aContext.compiler().set_preparse(true);
                else if (parameters == "off")
                    aContext.compiler().set_preparse(false);
                else if (!parameters.empty())
                    throw std::runtime_error("preparse: expected 'on' or 'off'");
                aContext.cout() << "Preparse: " << (aContext.compiler().preparse() ? "on" : "off") << std::endl;
            }
            else if (command == "t" || command == "trace")
            {
                if (words.size() >= 2)
                    aContext.compiler().set_trace(
                        boost::lexical_cast<uint32_t>(std::string{ words[1].first, words[1].second }), 
                        words.size() > 2 ? std::string{ words[2].first, words[2].second } : std::optional<std::string>{});
                else
                    throw std::runtime_error("invalid command argument(s)");
            }
            else if (command == "m" || command == "metrics")
                aContext.cout() << aContext.metrics();
            else if (command == "q" || command == "quit")
                return false;
            else
                throw std::runtime_error("unknown command '" + command + "'");
        }
        catch (const neos::context::warning& e)
        {
            output_compilation_time();
            aContext.cerr() << (std::string_view{ e.what() }.find("warning:") == std::string_view::npos ? "Warning: " : "") << e.what() << std::endl;
        }
        catch (const std::exception& e)
        {
            aContext.cerr() << (std::string_view{ e.what() }.find("error:") == std::string_view::npos ? "Error: " : "") << e.what() << std::endl;
        }
        catch (...)
        {
            aContext.cerr() << "Unknown error" << std::endl;
        }
        return true;
    }

    std::string console_prompt(context const& aContext, bool aInteractive)
    {
        return aContext.schema_loaded() ? aContext.running() || aInteractive ? aContext.schema().meta().language + "> " : aContext.schema().meta().language + "] " : "] ";
    }
}
