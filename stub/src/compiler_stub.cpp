/*
  compiler_stub.cpp

  Copyright (c) 2026 Leigh Johnston.  All Rights Reserved.

  This program is free software: you can redistribute it and / or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/


// The compiler stub of a language (neos.<language>.compiler.stub; see neos/image.hpp): runs the program (text) appended to its own 
// executable file, with the language's compiler built in (its concept libraries and compiler plugin, its schema and its packages: see 
// compiler_stub.hpp) for the source that the program compiles while it runs (e.g. Python's exec)

#include <neos/neos.hpp>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <neolib/core/string.hpp>
#include <neolib/app/application.hpp>
#include <neolib/file/file.hpp>
#include <neos/image.hpp>
#include <neos/context.hpp>
#include "compiler_stub.hpp"

int main()
{
    try
    {
        auto const path = neolib::program_file();
        auto const image = neos::image::read(path);
        if (!image)
            throw std::runtime_error("no program: '" + path + "' is not a neos image");
        // (the built-in plugins: none are loaded from files)
        auto const folder = neolib::program_directory();
        neolib::application<> application{ neolib::application_info{ "neos", "i42 software", {}, "Copyright (c) 2025 Leigh Johnston", folder, {}, {}, ".ncl" } };
        application.plugin_manager().plugin_folders().clear();
        neos::context context{ std::cout, application };
        for (std::size_t i = 0u; i < neos::stub::pluginCount; ++i)
        {
            neolib::ref_ptr<neolib::i_plugin> plugin;
            neos::stub::plugins[i](application, neolib::string{ folder }, plugin);
            if (!plugin)
                throw std::runtime_error("built-in plugin " + std::to_string(i) + " failed");
            context.add_plugin(*plugin);
        }
        // (the built-in schema and packages (as though they were in the executable's folder))
        std::string const schemaName = std::string{ neos::stub::language } + ".neos";
        std::string schemaSource;
        for (std::size_t i = 0u; i < neos::stub::resourceCount; ++i)
        {
            auto const& r = neos::stub::resources[i];
            std::string source{ reinterpret_cast<char const*>(r.data), r.size };
            if (schemaName == r.name)
                schemaSource = std::move(source);
            else
                context.add_package((std::filesystem::path{ folder } / r.name).string(), source);
        }
        context.load_schema((std::filesystem::path{ folder } / schemaName).string(), schemaSource);
        context.program().text = image->text;
        context.set_restricted(image->restricted);
        context.run();
        std::cout << std::flush;
        // (a runtime error has been output by the VM)
        return context.exit_status();
    }
    catch (std::exception const& e)
    {
        std::cerr << "Error: " << e.what() << std::endl;
    }
    catch (...)
    {
        std::cerr << "Unknown error" << std::endl;
    }
    return EXIT_FAILURE;
}
