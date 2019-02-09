#include <neos/neos.hpp>
#include <neos/context.hpp>
#include <string>
#include <iostream>

using namespace std::literals::string_literals;

int main()
{
	std::cout << "neos " << neos::NEOS_VERSION << std::endl;
	neos::context context;
	for (;;)
	{
        // todo: non-interactive mode (stdin goes straight to vm stdin)
		std::cout << (context.schema_loaded() ? context.running() ? context.language() + ">" : context.language() + "]" : "]");
		std::string line;
		std::getline(std::cin, line);
		std::vector<std::pair<std::string::const_iterator, std::string::const_iterator>> words;
		const std::string delimeters{ " " };
		neolib::tokens(line.cbegin(), line.cend(), delimeters.cbegin(), delimeters.cend(), words);
		if (words.empty())
			continue;
		const std::string command{ words[0].first, words[0].second };
		if (command.empty())
			continue;
		const std::string parameters{ words.size() > 1 ? words[1].first : line.cend(), line.cend() };
		try
		{
            if (command == "?" || command == "h" || command == "help")
            {
                std::cout << "h(elp)\n"
                    << "s(chema) <path to language schema>\n"
                    << "l(oad) <path to script>\n"
                    << "r(un)\n"
                    << "m(etrics)\n"
                    << ":<input>\n"
                    << std::endl;
            }
            else if (command == "s" || command == "schema")
                context.load_schema(parameters);
            else if (command == "r" || command == "run")
                context.run();
            else if (command == "m" || command == "metrics")
                std::cout << context.metrics();
        }
		catch (const std::exception& e)
		{
			std::cerr << "Error: " << e.what() << std::endl;
		}
		catch (...)
		{
			std::cerr << "Unknown error" << std::endl;
		}
	}
}
