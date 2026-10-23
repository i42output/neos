/*
  console.hpp

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
#include <neos/context.hpp>

namespace neos
{
    // the neos console's commands (the neos program's, and those of a session that a host (e.g. an IDE) runs): the console
    // input aConsoleInput (a command (see "help"), or in interactive mode an expression to evaluate) on the context aContext,
    // whose standard streams (see i_context::cin) its output and errors go to; returns false if the command is quit; aColour: the output 
    // is to a terminal that colours text (ANSI escape sequences): e.g. "list" syntax highlights the program
    bool process_command(context& aContext, bool& aInteractive, std::string const& aConsoleInput, bool aColour = false);
    // the console's prompt (as the context is: the language of the schema loaded, and whether a program is running or the
    // console is in interactive mode)
    std::string console_prompt(context const& aContext, bool aInteractive);
    // an image of the (compiled) program (see the "img" command): its path (aPath: none, the default (<program>.exe on
    // Windows, else a.out))
    std::string write_image(context& aContext, std::string const& aPath);
}
