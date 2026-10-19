/*
  stub.cpp

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

// The runtime stub of an image (see neos/image.hpp): runs the program (text) appended to its own executable file

#include <neos/neos.hpp>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <neolib/file/file.hpp>
#include <neos/image.hpp>
#include <neos/bytecode/vm/vm.hpp>

int main()
{
    try
    {
        auto const path = neolib::program_file();
        auto const image = neos::image::read(path);
        if (!image)
            throw std::runtime_error("no program: '" + path + "' is not a neos image");
        neos::bytecode::vm::thread thread{ image->text, std::cout, std::cin, image->restricted };
        thread.join();
        std::cout << std::flush;
        // (a runtime error has been output by the VM)
        return thread.error() ? EXIT_FAILURE : thread.exit_status().value_or(EXIT_SUCCESS);
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
