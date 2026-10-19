/*
  arguments.cpp

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

// The arguments of native functions and methods, checked as CPython's argument clinic checks them (its messages)

#include <cstring>
#include <string>
#include "objects.hpp"
#include "python.hpp"

namespace neos::python_runtime
{
    namespace detail
    {
        bool host_unrestricted(char const* aFunction)
        {
            auto const* h = host();
            if (h != nullptr && h->version >= 4u && h->restricted != nullptr && h->restricted() != 0)
            {
                // (a trap, as the VM's for a restricted function; the exception unwinds the runtime's own callers)
                std::string const message = "'" + std::string{ aFunction } + "' is restricted: it can access the host environment and the program is running in restricted mode";
                trap(message);
                error("RuntimeError", message);
                return false;
            }
            return true;
        }

        bool parse_arguments(char const* aFunction, std::vector<object*> const& aArgs, object* aKwargs, std::vector<char const*> const& aNames,
            std::size_t aRequired, std::size_t aPositionalOnly, std::vector<object*>& aResult)
        {
            // (aFunction: the qualified name (e.g. "bytes.strip"); some messages have the bare name)
            auto const plural = [](std::size_t n) { return n == 1u ? " argument" : " arguments"; };
            std::string const qualified{ aFunction };
            auto const dot = qualified.rfind('.');
            std::string const bare = dot == std::string::npos ? qualified : qualified.substr(dot + 1u);
            aResult.assign(aNames.size(), nullptr);
            bool const keywords = aKwargs != nullptr && !as<dict_object>(aKwargs)->items.empty();
            if (aPositionalOnly >= aNames.size())
            {
                // (only positional parameters)
                if (keywords)
                {
                    error("TypeError", std::string{ aFunction } + "() takes no keyword arguments");
                    return false;
                }
                if (aNames.size() == 1u && aRequired == 1u && aArgs.size() != 1u)
                {
                    error("TypeError", std::string{ aFunction } + "() takes exactly one argument (" + std::to_string(aArgs.size()) + " given)");
                    return false;
                }
                if (aNames.empty() && !aArgs.empty())
                {
                    error("TypeError", std::string{ aFunction } + "() takes no arguments (" + std::to_string(aArgs.size()) + " given)");
                    return false;
                }
                if (aArgs.size() < aRequired)
                {
                    error("TypeError", bare + (aRequired == aNames.size() ? " expected " : " expected at least ") + std::to_string(aRequired) +
                        plural(aRequired) + ", got " + std::to_string(aArgs.size()));
                    return false;
                }
                if (aArgs.size() > aNames.size())
                {
                    error("TypeError", bare + (aRequired == aNames.size() ? " expected " : " expected at most ") + std::to_string(aNames.size()) +
                        plural(aNames.size()) + ", got " + std::to_string(aArgs.size()));
                    return false;
                }
                for (std::size_t i = 0u; i < aArgs.size(); ++i)
                    aResult[i] = aArgs[i];
                return true;
            }
            if (aArgs.size() > aNames.size())
            {
                error("TypeError", bare + "() takes at most " + std::to_string(aNames.size()) + plural(aNames.size()) + " (" +
                    std::to_string(aArgs.size() + (keywords ? as<dict_object>(aKwargs)->items.size() : 0u)) + " given)");
                return false;
            }
            for (std::size_t i = 0u; i < aArgs.size(); ++i)
                aResult[i] = aArgs[i];
            if (keywords)
                for (auto const& item : as<dict_object>(aKwargs)->items)
                {
                    auto const& name = as<str_object>(item.first)->value;
                    std::size_t i = aPositionalOnly;
                    while (i < aNames.size() && name != aNames[i])
                        ++i;
                    if (i == aNames.size())
                    {
                        error("TypeError", bare + "() got an unexpected keyword argument '" + name + "'");
                        return false;
                    }
                    if (aResult[i] != nullptr)
                    {
                        error("TypeError", "argument for " + bare + "() given by name ('" + name + "') and position (" + std::to_string(i + 1u) + ")");
                        return false;
                    }
                    aResult[i] = item.second;
                }
            for (std::size_t i = 0u; i < aRequired; ++i)
                if (aResult[i] == nullptr)
                {
                    error("TypeError", bare + "() missing required argument '" + aNames[i] + "' (pos " + std::to_string(i + 1u) + ")");
                    return false;
                }
            return true;
        }
    }
}
