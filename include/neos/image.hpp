/*
  image.hpp

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

#pragma once

#include <neos/neos.hpp>
#include <string>
#include <optional>

namespace neos
{
    namespace image
    {
        // An image is an executable: the runtime stub (neos.stub, which runs the VM) followed by a program's text (its
        // WebAssembly module) and a trailer that locates it. The stub runs the text appended to its own file.

        struct contents
        {
            neos::text text;
            bool restricted = false; ///< restricted mode (see bytecode::vm::thread)
        };

        // the stub of the running executable's installation (the folder of the running executable)
        std::string stub_path();
        // the compiler stub of a language (neos.<language>.compiler.stub, of the running executable's installation): the stub of an image 
        // of a program that compiles source while it runs (e.g. Python's exec); it has the language's compiler (its schema, packages, 
        // concept libraries and compiler plugin) built in
        std::string compiler_stub_path(std::string const& aLanguage);
        // writes an image of aText; returns its path
        std::string write(std::string const& aImagePath, text const& aText, bool aRestricted, std::string const& aStubPath = stub_path());
        // the contents of an image (none if the file is not an image, e.g. the stub itself)
        std::optional<contents> read(std::string const& aPath);
    }
}
