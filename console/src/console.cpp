#include <neos/neos.hpp>
#include <string>
#include <iostream>
#include <boost/program_options.hpp>
#include <neos/context.hpp>

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
}

bool process_command(neos::context& aContext, bool& aInteractive, const std::string& aConsoleInput)
{
    auto output_compilation_time = [&aContext]()
    {
        std::cout << "Compilation time: " <<
            std::chrono::duration_cast<std::chrono::microseconds>(aContext.compiler().end_time() - aContext.compiler().start_time()).count() / 1000.0 << "ms" << std::endl;
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
                << "![<expression>]                          Evaluate expression (enter interactive mode if expression omitted)\n"
                << ":<input>                                 Input (as stdin)\n"
                << "q(uit)                                   Quit neos\n"
                << "lc                                       List loaded concept libraries\n"
                << "t(race) <0|1|2|3|4|5> [<filter>]         Compiler trace\n"
                << "m(etrics)                                Display metrics for running programs\n"
                << std::flush;
        }
        else if (command == "s" || command == "schema")
        {
            aContext.load_schema(parameters);
            std::cout << "Language: " << aContext.schema().meta().description + "\nVersion: " + aContext.schema().meta().version << std::endl;
            if (!aContext.schema().meta().copyright.empty())
                std::cout << aContext.schema().meta().copyright << std::endl;
        }
        else if (command == "l" || command == "load")
        {
            aContext.load_program(parameters);
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
                    std::cout << f.source() << std::endl;
                }
        }
        else if (command == "r" || command == "run")
            aContext.run();
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
    catch (const neos::language::schema::unresolved_references& e)
    {
        for (auto const& r : e.references)
        {
            std::cerr << "Error: " << aContext.schema_source().to_error_text(*r.node, "unresolved schema reference '" + r.symbol + "'") << std::endl;
        }
        throw;
    }
    catch (const neos::context::warning& e)
    {
        output_compilation_time();
        std::cerr << "Warning: " << e.what() << std::endl;
    }
    catch (const std::exception& e)
    {
        std::cerr << "Error: " << e.what() << std::endl;
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
    neos::context context;
    bool interactive = false;
    bool first = true;
    for (;;)
    {
        // todo: non-interactive mode (stdin goes straight to vm stdin)
        if (first)
        {
            first = false;
            if (aOptions.count("s"))
                process_command(context, interactive, "s " + aOptions["s"].as<std::string>());
            if (aOptions.count("schema"))
                process_command(context, interactive, "s " + aOptions["schema"].as<std::string>());
            if (aOptions.count("trace"))
                process_command(context, interactive, "t " + boost::lexical_cast<std::string>(aOptions["trace"].as<uint32_t>()));
            if (aOptions.count("t"))
                process_command(context, interactive, "t " + boost::lexical_cast<std::string>(aOptions["t"].as<uint32_t>()));
            if (aOptions.count("program"))
                for (auto const& p : aOptions["program"].as<std::vector<std::string>>())
                    process_command(context, interactive, "l " + p);
            if (aOptions.count("c") || aOptions.count("compile"))
                process_command(context, interactive, "c");
        }
        std::string line;
        std::cout << (context.schema_loaded() ? context.running() || interactive ? context.schema().meta().name + "> " : context.schema().meta().name + "] " : "] ");
        std::getline(std::cin, line);
        if (!process_command(context, interactive, line))
            return;
    }
}

int main(int argc, char* argv[])
{
    try
    {
        boost::program_options::options_description optionsDescription{ "Allowed options" };
        optionsDescription.add_options()
            ("schema", boost::program_options::value<std::string>(), "language schema")
            ("s", boost::program_options::value<std::string>(), "language schema")
            ("trace", boost::program_options::value<uint32_t>(), "compiler trace")
            ("t", boost::program_options::value<uint32_t>(), "compiler trace")
            ("compile", "compile program")
            ("c", "compile program")
            ("program", boost::program_options::value<std::vector<std::string>>(), "program(s) to load");
        boost::program_options::positional_options_description positionalOptionsDescription;
        positionalOptionsDescription.add("program", -1);
        boost::program_options::variables_map options;
        boost::program_options::store(boost::program_options::command_line_parser(argc, argv).
            options(optionsDescription).positional(positionalOptionsDescription).run(), options);
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
