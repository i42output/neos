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

    // (an image of the program: with the compiler stub of its language (its compiler built in) if the program compiles source while it 
    // runs (e.g. Python's exec))
    std::string write_image(neos::context& aContext, std::string const& aPath)
    {
        auto const& language = aContext.schema().meta().language;
        auto const stub = aContext.compiler().compiles_at_run_time(language) ? neos::image::compiler_stub_path(language) : neos::image::stub_path();
        return neos::image::write(image_path(aContext, aPath), aContext.text(), aContext.restricted(), stub);
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
}

bool process_command(neos::context& aContext, bool& aInteractive, const std::string& aConsoleInput)
{
    auto output_parse_time = [](std::chrono::steady_clock::duration aParseTime)
    {
        std::cout << "Parse time: " << std::chrono::duration_cast<std::chrono::microseconds>(aParseTime).count() / 1000.0 << "ms" << std::endl;
    };
    auto output_compilation_time = [&aContext, &output_parse_time]()
    {
        if (aContext.compiler().preparse())
            std::cout << "Pre-parse time: " << std::chrono::duration_cast<std::chrono::microseconds>(aContext.compiler().preparse_time()).count() / 1000.0 << "ms" << std::endl;
        output_parse_time(aContext.compiler().parse_time());
        std::cout << "Compilation time: " <<
            std::chrono::duration_cast<std::chrono::microseconds>(aContext.compiler().end_time() - aContext.compiler().start_time()).count() / 1000.0 << "ms" << std::endl;
    };
    auto output_schema = [&aContext, &output_parse_time]()
    {
        output_parse_time(aContext.schema().parse_time());
        std::cout << "Language: " << aContext.schema().meta().description + "\nVersion: " + aContext.schema().meta().version << std::endl;
        if (!aContext.schema().meta().copyright.empty())
            std::cout << aContext.schema().meta().copyright << std::endl;
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
            std::cout << "h(elp)\n"
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
            std::cout << "Loading schema '" << (parameters.empty() && aContext.schema_loaded() ? aContext.schema().path() : parameters) << "'..." << std::endl;
            aContext.load_schema(parameters);
            output_schema();
        }
        else if (command == "l" || command == "load")
        {
            bool const schemaWasLoaded = aContext.schema_loaded();
            aContext.load_program(parameters);
            if (!schemaWasLoaded && aContext.schema_loaded()) // (the schema for the program's file type)
            {
                std::cout << "Loading schema '" << aContext.schema().path() << "'..." << std::endl;
                output_schema();
            }
            std::cout << "Program: " << parameters << std::endl;
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
                        std::cout << "File '" << *f.source_file_path() << "':" << std::endl;
                    // (the source's last line is terminated when it is loaded (terminate_last_line): a newline only if it is not)
                    std::cout << f.source();
                    if (f.source().empty() || f.source().back() != '\n')
                        std::cout << '\n';
                    std::cout << std::flush;
                }
        }
        else if (command == "r" || command == "run")
            aContext.run();
        else if (command == "img")
        {
            if (aContext.text().empty())
                throw neos::context::no_text();
            auto const path = write_image(aContext, parameters);
            std::cout << "Image: " << path << std::endl;
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
            auto print_concept_library = [](auto const& self, const neos::language::i_concept_library& aConceptLibrary, uint32_t aDepth = 0) -> void
            {
                std::cout << std::string(aDepth * 2, ' ') << "[" << aConceptLibrary.name() << "]";
                if (aDepth == 0)
                    std::cout << " (" << aConceptLibrary.uri() << ")";
                std::cout << std::endl;
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
            std::cout << "Restricted mode: " << (aContext.restricted() ? "on" : "off") << std::endl;
        }
        else if (command == "parsetimes")
        {
            if (parameters == "on")
                aContext.compiler().set_show_parse_times(true);
            else if (parameters == "off")
                aContext.compiler().set_show_parse_times(false);
            else if (!parameters.empty())
                throw std::runtime_error("parsetimes: expected 'on' or 'off'");
            std::cout << "Parse times: " << (aContext.compiler().show_parse_times() ? "on" : "off") << std::endl;
        }
        else if (command == "trap_on_overflow")
        {
            if (parameters == "on")
                aContext.compiler().set_trap_on_overflow(true);
            else if (parameters == "off")
                aContext.compiler().set_trap_on_overflow(false);
            else if (!parameters.empty())
                throw std::runtime_error("trap_on_overflow: expected 'on' or 'off'");
            std::cout << "Trap on overflow: " << (aContext.compiler().trap_on_overflow() ? "on" : "off") << std::endl;
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
            std::cout << "Bigint promotion: " << (setting ? (*setting ? "on" : "off") : "the schema's") << std::endl;
        }
        else if (command == "preparse")
        {
            if (parameters == "on")
                aContext.compiler().set_preparse(true);
            else if (parameters == "off")
                aContext.compiler().set_preparse(false);
            else if (!parameters.empty())
                throw std::runtime_error("preparse: expected 'on' or 'off'");
            std::cout << "Preparse: " << (aContext.compiler().preparse() ? "on" : "off") << std::endl;
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
            std::cout << aContext.metrics();
        else if (command == "q" || command == "quit")
            return false;
        else
            throw std::runtime_error("unknown command '" + command + "'");
    }
    catch (const neos::context::warning& e)
    {
        output_compilation_time();
        std::cerr << (std::string_view{ e.what() }.find("warning:") == std::string_view::npos ? "Warning: " : "") << e.what() << std::endl;
    }
    catch (const std::exception& e)
    {
        std::cerr << (std::string_view{ e.what() }.find("error:") == std::string_view::npos ? "Error: " : "") << e.what() << std::endl;
    }
    catch (...)
    {
        std::cerr << "Unknown error" << std::endl;
    }
    return true;
}

void main_loop(boost::program_options::variables_map& aOptions)
{
    std::cout << "neos " << neos::NEOS_VERSION << std::endl;
    neos::context context{ std::cout };
    bool interactive = false;
    bool first = true;
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
        std::cout << (context.schema_loaded() ? context.running() || interactive ? context.schema().meta().language + "> " : context.schema().meta().language + "] " : "] ");
        if (!std::getline(std::cin, line)) // (end of input: quit)
        {
            std::cout << std::endl;
            return;
        }
        if (!process_command(context, interactive, line))
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
