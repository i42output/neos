/*
  lib_package.cpp

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

// The package library (require: Lua modules found by package.path; there are no C modules)

#include <cstdlib>
#include <fstream>
#include "lua.hpp"
#include "api.hpp"

namespace neos::lua_runtime
{
    namespace
    {
#if defined(_WIN32)
        constexpr char DirectorySeparator = '\\';
#else
        constexpr char DirectorySeparator = '/';
#endif
        char const* const DefaultPath = "./?.lua;./?/init.lua";

        value package()
        {
            return index(the_state().loaded, intern("package"));
        }

        bool readable(std::string const& aPath)
        {
            std::ifstream file{ aPath };
            return static_cast<bool>(file);
        }

        // (the file that a name is found as by a path's templates; else the files tried)
        std::string search_path(std::string aName, std::string const& aPath, std::string const& aSeparator, std::string const& aReplacement,
            std::string& aTried)
        {
            if (!aSeparator.empty())
                for (auto p = aName.find(aSeparator); p != std::string::npos; p = aName.find(aSeparator, p + aReplacement.size()))
                    aName.replace(p, aSeparator.size(), aReplacement);
            std::size_t begin = 0u;
            while (begin <= aPath.size())
            {
                auto end = aPath.find(';', begin);
                if (end == std::string::npos)
                    end = aPath.size();
                auto candidate = aPath.substr(begin, end - begin);
                begin = end + 1u;
                if (candidate.empty())
                    continue;
                for (auto p = candidate.find('?'); p != std::string::npos; p = candidate.find('?', p + aName.size()))
                    candidate.replace(p, 1u, aName);
                if (readable(candidate))
                    return candidate;
                aTried += "\n\tno file '" + candidate + "'";
            }
            return {};
        }

        void lua_searchpath(native_call& c)
        {
            auto const name = std::string{ check_string(c, 0u) };
            auto const path = std::string{ check_string(c, 1u) };
            auto const separator = std::string{ optional_string(c, 2u, ".") };
            auto const replacement = std::string{ optional_string(c, 3u, std::string_view{ &DirectorySeparator, 1u }) };
            std::string tried;
            auto const found = search_path(name, path, separator, replacement, tried);
            if (!found.empty())
                c.ret(make_string(found));
            else
            {
                c.ret(Nil);
                c.ret(make_string(tried));
            }
        }

        void lua_require(native_call& c)
        {
            auto const nameValue = check_string_value(c, 0u);
            auto const name = str(nameValue);
            auto& s = the_state();
            auto const existing = raw_get(s.loaded, nameValue);
            if (truthy(existing))
            {
                c.ret(existing);
                return;
            }
            auto const pkg = package();
            std::string tried;
            value loader = Nil;
            value extra = Nil;
            // (package.preload, then package.path)
            auto const preload = is_table(pkg) ? index(pkg, intern("preload")) : Nil;
            if (is_table(preload))
                loader = raw_get(preload, nameValue);
            if (!is_nil(loader))
                extra = intern(":preload:");
            else
            {
                tried += "\n\tno field package.preload['" + name + "']";
                auto const path = is_table(pkg) ? index(pkg, intern("path")) : Nil;
                if (!is_string(path))
                    runtime_error("'package.path' must be a string");
                auto const file = search_path(name, str(path), ".", std::string(1u, DirectorySeparator), tried);
                if (!file.empty())
                {
                    bool ok = false;
                    auto const source = read_source_file(file, ok);
                    std::string error;
                    loader = load_chunk(source, chunk_display_name("@" + file), "bt", Nil, error);
                    if (!is_function(loader))
                        runtime_error("error loading module '" + name + "' from file '" + file + "':\n\t" + error);
                    extra = make_string(file);
                }
                else
                    tried += "\n\tno file '" + std::string{ "/usr/local/lib/lua/5.4/" } + name + ".so'";
            }
            if (is_nil(loader))
                runtime_error("module '" + name + "' not found:" + tried);
            value_list keep{ loader, extra };
            value const arguments[] = { nameValue, extra };
            auto const result = call1(loader, arguments);
            keep.push_back(result);
            if (!is_nil(result))
                raw_set(s.loaded, nameValue, result);
            else if (is_nil(raw_get(s.loaded, nameValue)))
                raw_set(s.loaded, nameValue, True);
            c.ret(raw_get(s.loaded, nameValue));
            c.ret(extra);
        }

        std::string environment_path()
        {
            for (auto const* variable : { "LUA_PATH_5_4", "LUA_PATH" })
                if (auto const* text = std::getenv(variable); text != nullptr)
                {
                    std::string path{ text };
                    if (auto const p = path.find(";;"); p != std::string::npos)
                        path.replace(p, 2u, std::string{ ";" } + DefaultPath + ";");
                    return path;
                }
            return DefaultPath;
        }
    }

    void open_package()
    {
        auto& s = the_state();
        auto const t = library_table("package");
        value_list keep{ t };
        raw_set(t, intern("loaded"), s.loaded);
        raw_set(t, intern("preload"), make_table());
        raw_set(t, intern("path"), make_string(environment_path()));
        raw_set(t, intern("cpath"), make_string(""));
        raw_set(t, intern("config"), make_string(std::string(1u, DirectorySeparator) + "\n;\n?\n!\n-\n"));
        register_library(t, "package", { { "searchpath", &lua_searchpath } });
        register_library(s.globals, nullptr, { { "require", &lua_require } });
        auto const searchers = make_table();
        raw_set(t, intern("searchers"), searchers);
    }
}
