/*
  image.hpp

  Copyright (c) 2026 Leigh Johnston.  All Rights Reserved.

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
