/*
  neo.cpp

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

/*
  The neoscript runtime plugin (neos.runtime.neo): neoscript's stream functions (its package neo.stream: imports named 
  "neo::f"), through the host's standard streams (see neos_rt_host::std_write), so that a program's are its own (e.g. an 
  IDE's window) and a read of its input is the host's (e.g. one that the debugger can interrupt, as every language's)
*/

#include <neos/runtime/plugin.h>
#include <iterator>
#include <string>

namespace
{
    neos_rt_host const* gHost = nullptr;

    // neo::print(s : in string): s, to the program's output
    void neo_print(char const* aText)
    {
        if (gHost == nullptr || gHost->version < 9u || aText == nullptr)
            return;
        std::string const text{ aText };
        gHost->std_write(1, text.data(), text.size());
        gHost->std_flush(1);
    }

    // neo::input(s : out string): a line of the program's input (without its line ending; empty at the end of input)
    char const* neo_input()
    {
        thread_local std::string tLine;
        tLine.clear();
        if (gHost == nullptr || gHost->version < 9u)
            return tLine.c_str();
        for (int c; (c = gHost->std_getc(0)) != -1 && c != '\n';)
            tLine += static_cast<char>(c);
        if (!tLine.empty() && tLine.back() == '\r')
            tLine.pop_back();
        return tLine.c_str();
    }

    neos_rt_plugin const& plugin()
    {
        static neos_rt_function const sFunctions[] =
        {
            { "print", NEOS_RT_FUNCTION(neo_print), NEOS_RT_VOID, 1u, { NEOS_RT_STRING }, 0u },
            { "input", NEOS_RT_FUNCTION(neo_input), NEOS_RT_STRING, 0u, {}, 0u }
        };
        static neos_rt_plugin const sPlugin =
        {
            NEOS_RUNTIME_PLUGIN_VERSION,
            "neo",
            "neoscript runtime",
            static_cast<unsigned>(std::size(sFunctions)),
            sFunctions
        };
        return sPlugin;
    }
}

NEOS_RUNTIME_PLUGIN_ENTRY
{
    return &plugin();
}

NEOS_RUNTIME_PLUGIN_SET_HOST
{
    gHost = aHost;
}
