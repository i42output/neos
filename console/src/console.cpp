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
#include <string>
#include <string_view>
#include <vector>
#include <algorithm>
#include <iostream>
#include <filesystem>
#include <boost/program_options.hpp>
#include <neos/context.hpp>
#include <neos/image.hpp>
#include <neos/console.hpp>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

using namespace std::literals::string_literals;

namespace
{
    bool command_arg_to_bool(const std::string& aArg)
    {
        if (aArg == "true" || aArg == "1" || aArg == "on")
            return true;
        else if (aArg == "false" || aArg == "0" || aArg == "off")
            return false;
        else
            throw std::runtime_error("invalid command argument(s)");
    }

    // --img: its path is only ever given as --img=<path> (an argument that follows it is not its path but e.g. the
    // program); --img alone (or with no path) is "img-default" (the default path)
    std::pair<std::string, std::string> img_option(std::string const& aArgument)
    {
        if (aArgument.starts_with("--"))
        {
            auto const equals = aArgument.find('=');
            auto const name = aArgument.substr(2u, equals == std::string::npos ? std::string::npos : equals - 2u);
            if (!name.empty() && std::string_view{ "img" }.starts_with(name)) // (or an abbreviation of it)
            {
                auto const path = equals == std::string::npos ? std::string{} : aArgument.substr(equals + 1u);
                return path.empty() ? std::pair<std::string, std::string>{ "img-default", {} } : std::pair<std::string, std::string>{ "img", path };
            }
        }
        return {};
    }

    // (the console's output is a terminal that colours text (ANSI escape sequences: on Windows, enabled))
    bool colour_output()
    {
#if defined(_WIN32)
        auto const output = ::GetStdHandle(STD_OUTPUT_HANDLE);
        DWORD mode = 0;
        return output != INVALID_HANDLE_VALUE && ::GetConsoleMode(output, &mode) && ::SetConsoleMode(output, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
#else
        return ::isatty(STDOUT_FILENO) != 0;
#endif
    }
}

void main_loop(boost::program_options::variables_map& aOptions)
{
    std::cout << "neos " << neos::NEOS_VERSION << std::endl;
    neos::context context{ std::cout };
    bool interactive = false;
    bool first = true;
    bool const colour = colour_output();
    for (;;)
    {
        // todo: non-interactive mode (stdin goes straight to vm stdin)
        if (first)
        {
            first = false;
            if (aOptions.count("parse-times"))
                process_command(context, interactive, "parsetimes on");
            if (aOptions.count("no-preparse"))
                process_command(context, interactive, "preparse off");
            if (aOptions.count("trap-on-overflow"))
                process_command(context, interactive, "trap_on_overflow on");
            if (aOptions.count("bigint-promotion"))
                process_command(context, interactive, "bigint_promotion " + aOptions["bigint-promotion"].as<std::string>());
            if (aOptions.count("s"))
                process_command(context, interactive, "s " + aOptions["s"].as<std::string>());
            if (aOptions.count("schema"))
                process_command(context, interactive, "s " + aOptions["schema"].as<std::string>());
            if (aOptions.count("trace"))
                process_command(context, interactive, "t " + boost::lexical_cast<std::string>(aOptions["trace"].as<uint32_t>()));
            if (aOptions.count("t"))
                process_command(context, interactive, "t " + boost::lexical_cast<std::string>(aOptions["t"].as<uint32_t>()));
            if (aOptions.count("load"))
                for (auto const& p : aOptions["load"].as<std::vector<std::string>>())
                    process_command(context, interactive, "l " + p);
            if (aOptions.count("restricted"))
                process_command(context, interactive, "restricted on");
            if (aOptions.count("c") || aOptions.count("compile"))
                process_command(context, interactive, "c");
            if (aOptions.count("img"))
            {
                if (context.text().empty())
                    process_command(context, interactive, "c");
                process_command(context, interactive, "img " + aOptions["img"].as<std::string>());
            }
        }
        std::string line;
        std::cout << neos::console_prompt(context, interactive);
        if (!std::getline(std::cin, line)) // (end of input: quit)
        {
            std::cout << std::endl;
            return;
        }
        if (!process_command(context, interactive, line, colour))
            return;
    }
}

int run_program(boost::program_options::variables_map& aOptions, std::vector<std::string> const& aArguments)
{
    // (a program and no console: the program is compiled and run; the only output is the program's (and any errors, to stderr))
    try
    {
        neos::context context{ std::cout };
        if (aOptions.count("no-preparse"))
            context.compiler().set_preparse(false);
        if (aOptions.count("trap-on-overflow"))
            context.compiler().set_trap_on_overflow(true);
        if (aOptions.count("bigint-promotion"))
        {
            auto const& value = aOptions["bigint-promotion"].as<std::string>();
            if (value == "on" || value == "off")
                context.compiler().set_bigint_promotion_override(value == "on");
            else if (value != "schema")
                throw std::runtime_error("bigint-promotion: expected 'on', 'off' or 'schema'");
        }
        if (aOptions.count("restricted"))
            context.set_restricted(true);
        context.set_arguments(aArguments);
        if (aOptions.count("trace"))
            context.compiler().set_trace(aOptions["trace"].as<uint32_t>());
        if (aOptions.count("schema"))
            context.load_schema(aOptions["schema"].as<std::string>());
        for (auto const& p : aOptions["load"].as<std::vector<std::string>>())
            context.load_program(p);
        try
        {
            context.compile_program();
        }
        catch (const neos::context::warning& e)
        {
            std::cerr << (std::string_view{ e.what() }.find("warning:") == std::string_view::npos ? "Warning: " : "") << e.what() << std::endl;
        }
        if (aOptions.count("img"))
            write_image(context, aOptions["img"].as<std::string>());
        else
        {
            context.run();
            std::cout << std::flush;
            // (a runtime error has been output by the VM)
            return context.exit_status();
        }
    }
    catch (const neos::language::compiler_error& e)
    {
        // (a compile error of a source: its report)
        if (!e.diagnostic.empty())
            std::cerr << e.what() << (std::string_view{ e.what() }.ends_with('\n') ? "" : "\n") << std::flush;
        else
            std::cerr << (std::string_view{ e.what() }.find("error:") == std::string_view::npos ? "Error: " : "") << e.what() << std::endl;
        return EXIT_FAILURE;
    }
    catch (const std::exception& e)
    {
        std::cerr << (std::string_view{ e.what() }.find("error:") == std::string_view::npos ? "Error: " : "") << e.what() << std::endl;
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

namespace
{
    // (the index in argv of the program to run: its first argument that is not an option (or an option's value); the arguments
    // that follow it are the program's own (argc: none))
    int program_index(int argc, char* argv[])
    {
        for (int i = 1; i < argc; ++i)
        {
            std::string_view const argument{ argv[i] };
            if (argument == "--")
                return i + 1 < argc ? i + 1 : argc;
            if (!argument.starts_with("-") || argument == "-")
                return i;
            // (an option whose value is the next argument: -s, -t and -l, and --schema, --trace, --bigint-promotion and --load (or
            // an abbreviation of one of them) without "=")
            bool value = false;
            if (argument.size() == 2u)
                value = argument == "-s" || argument == "-t" || argument == "-l";
            else if (argument.starts_with("--") && argument.find('=') == std::string_view::npos)
            {
                auto const name = argument.substr(2u);
                for (std::string_view option : { "schema", "trace", "bigint-promotion", "load" })
                    if (!name.empty() && option.starts_with(name))
                        value = true;
            }
            if (value)
                ++i;
        }
        return argc;
    }
}

int main(int argc, char* argv[])
{
    try
    {
        // (the program's arguments: not the console's options)
        int const program = program_index(argc, argv);
        std::vector<std::string> programArguments;
        for (int i = program; i < argc; ++i)
            programArguments.emplace_back(argv[i]);
        // (a program after "--": it is the console's positional argument)
        std::vector<char*> consoleArguments{ argv, argv + std::min(argc, program + 1) };
        if (program < argc && std::string_view{ argv[program - 1] } == "--")
            consoleArguments.erase(consoleArguments.begin() + (program - 1));
        argc = static_cast<int>(consoleArguments.size());
        argv = consoleArguments.data();
        boost::program_options::options_description optionsDescription{ "Allowed options" };
        optionsDescription.add_options()
            ("schema,s", boost::program_options::value<std::string>(), "language schema")
            ("trace,t", boost::program_options::value<uint32_t>(), "compiler trace")
            ("console,c", "console (else a program specified is run, with no output but the program's)")
            ("compile", "compile program")
            ("parse-times", "output the time of each parse (else only the total)")
            ("no-preparse", "no language's own pass over a source before it is parsed")
            ("trap-on-overflow", "integer arithmetic whose result overflows its (fixed size) type traps (else it wraps)")
            ("bigint-promotion", boost::program_options::value<std::string>(), "on|off: integers of any size (a result that overflows is promoted to an arbitrary precision integer), overriding the language's schema (bigint.promotion)")
            ("restricted", "restricted mode: programs cannot call runtime plugin functions that can change the host environment")
            ("img", boost::program_options::value<std::string>(), "make an image of the program (an executable that runs it) rather than run it: --img=<path> (by default <program>.exe on Windows, else a.out)")
            ("img-default", "(--img with no path)")
            ("load,l", boost::program_options::value<std::vector<std::string>>(), "load program(s)");
        boost::program_options::positional_options_description positionalOptionsDescription;
        positionalOptionsDescription.add("load", -1);
        boost::program_options::variables_map options;
        boost::program_options::store(boost::program_options::command_line_parser(argc, argv).
            options(optionsDescription).positional(positionalOptionsDescription).extra_parser(img_option).run(), options);
        if (options.count("img-default") && !options.count("img"))
            options.emplace("img", boost::program_options::variable_value{ std::string{}, false });
        if (options.count("load") && !options.count("console"))
            return run_program(options, programArguments);
        main_loop(options);
    }
    catch (std::logic_error&)
    {
        throw;
    }
    catch (...)
    {
        return EXIT_FAILURE;
    }
}
