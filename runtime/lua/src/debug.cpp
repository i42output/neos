/*
  debug.cpp

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

/*
  The Lua runtime plugin's hooks for the debugger (see neos/runtime/debug.h): the values of Lua's variables as the Lua compiler 
  describes them (see neos::language::lua::compiler's debug variables):
  - "value": a Lua value;
  - "box": a box (a captured variable: an upvalue's cell) that holds its value;
  - "upvalue:<n>": the n-th upvalue of a Lua function (the value is the function: its frame's callee);
  - "env": the environment (_ENV, the globals table) of a Lua function (the value is the function).
*/

#include "lua.hpp"
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>
#include <neos/runtime/debug.h>

namespace neos::lua_runtime
{
    namespace
    {
        bool is_identifier(std::string_view aText)
        {
            static std::string_view const sKeywords[] = { "and", "break", "do", "else", "elseif", "end", "false", "for", "function", "goto", 
                "if", "in", "local", "nil", "not", "or", "repeat", "return", "then", "true", "until", "while" };
            if (aText.empty() || std::isdigit(static_cast<unsigned char>(aText[0])))
                return false;
            for (auto c : aText)
                if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_')
                    return false;
            for (auto k : sKeywords)
                if (aText == k)
                    return false;
            return true;
        }

        std::string quoted(std::string_view aText, std::size_t aLimit)
        {
            std::string result = "\"";
            for (auto c : aText)
            {
                if (result.size() > aLimit)
                {
                    result += "...";
                    break;
                }
                switch (c)
                {
                case '"': result += "\\\""; break;
                case '\\': result += "\\\\"; break;
                case '\n': result += "\\n"; break;
                case '\r': result += "\\r"; break;
                case '\t': result += "\\t"; break;
                default:
                    if (static_cast<unsigned char>(c) < 0x20u || c == 0x7F)
                    {
                        char escape[8];
                        std::snprintf(escape, sizeof(escape), "\\%u", static_cast<unsigned>(static_cast<unsigned char>(c)));
                        result += escape;
                    }
                    else
                        result += c;
                }
            }
            result += "\"";
            return result;
        }

        std::string address_text(char const* aKind, value aValue)
        {
            char buffer[64];
            std::snprintf(buffer, sizeof(buffer), "%s: 0x%08llx", aKind, static_cast<unsigned long long>(handle_of(aValue)));
            return buffer;
        }

        // (the Lua value that a variable of the type holds)
        value resolve(std::string_view aType, value aValue)
        {
            if (aType == "box")
                return is_type(aValue, cell_type::Box) ? cell_of<box_cell>(aValue).v : Nil;
            if (aType.starts_with("upvalue:"))
            {
                std::size_t n = 0u;
                for (auto c : aType.substr(8u))
                    n = n * 10u + static_cast<std::size_t>(c - '0');
                if (!is_function(aValue) || fun(aValue).native || n >= fun(aValue).upvalues.size())
                    return Nil;
                auto const box = fun(aValue).upvalues[n];
                return is_type(box, cell_type::Box) ? cell_of<box_cell>(box).v : box;
            }
            if (aType == "env")
            {
                if (!is_function(aValue))
                    return Nil;
                auto const environment = fun(aValue).environment;
                return is_type(environment, cell_type::Box) ? cell_of<box_cell>(environment).v : environment;
            }
            return aValue;
        }

        std::string describe_value(value aValue, std::size_t aLimit)
        {
            if (is_nil(aValue))
                return "nil";
            if (aValue == True)
                return "true";
            if (aValue == False)
                return "false";
            if (is_number(aValue))
                return number_text(aValue);
            if (is_string(aValue))
                return quoted(str_view(aValue), aLimit);
            if (is_table(aValue))
            {
                auto const& t = tab(aValue);
                std::size_t count = t.array.size();
                for (auto const& n : t.nodes)
                    if (n.key != Empty && !is_nil(n.v))
                        ++count;
                return address_text("table", aValue) + " (" + std::to_string(count) + (count == 1u ? " entry)" : " entries)");
            }
            if (is_function(aValue))
            {
                auto const& f = fun(aValue);
                if (f.native)
                    return std::string{ "builtin: " } + (f.qualified != nullptr ? f.qualified : "?");
                return address_text("function", aValue);
            }
            if (auto* c = cell_at(aValue))
            {
                switch (c->type)
                {
                case cell_type::Userdata: return address_text("userdata", aValue);
                case cell_type::Thread: return address_text("thread", aValue);
                case cell_type::Box: return "box: " + describe_value(cell_of<box_cell>(aValue).v, aLimit);
                case cell_type::Varargs: return "(" + std::to_string(cell_of<varargs_cell>(aValue).values.size()) + " values)";
                default: break;
                }
            }
            return "?";
        }

        std::string key_name(value aKey)
        {
            if (is_string(aKey) && is_identifier(str_view(aKey)))
                return std::string{ str_view(aKey) };
            return "[" + describe_value(aKey, 64u) + "]";
        }

        // (a value's parts: a table's entries (its array part, then the others), a function's upvalues, a userdata's user value, 
        // a varargs' values)
        template <typename Visitor>
        void visit_parts(value aValue, Visitor&& aVisitor)
        {
            if (is_table(aValue))
            {
                auto const& t = tab(aValue);
                for (std::size_t i = 0u; i < t.array.size(); ++i)
                    if (!aVisitor("[" + std::to_string(i + 1u) + "]", t.array[i]))
                        return;
                for (auto const& n : t.nodes)
                    if (n.key != Empty && !is_nil(n.v))
                        if (!aVisitor(key_name(n.key), n.v))
                            return;
                if (!is_nil(t.metatable))
                    aVisitor("(metatable)", t.metatable);
                return;
            }
            if (is_function(aValue))
            {
                auto const& f = fun(aValue);
                for (std::size_t i = 0u; i < f.upvalues.size(); ++i)
                {
                    auto const box = f.upvalues[i];
                    if (!aVisitor("(upvalue " + std::to_string(i + 1u) + ")", is_type(box, cell_type::Box) ? cell_of<box_cell>(box).v : box))
                        return;
                }
                return;
            }
            if (auto* c = cell_at(aValue))
            {
                if (c->type == cell_type::Varargs)
                {
                    auto const& values = cell_of<varargs_cell>(aValue).values;
                    for (std::size_t i = 0u; i < values.size(); ++i)
                        if (!aVisitor("[" + std::to_string(i + 1u) + "]", values[i]))
                            return;
                }
                else if (c->type == cell_type::Userdata)
                {
                    auto const& u = cell_of<userdata_cell>(aValue);
                    if (!is_nil(u.metatable) && !aVisitor("(metatable)", u.metatable))
                        return;
                    if (!is_nil(u.user))
                        aVisitor("(user value)", u.user);
                }
                else if (c->type == cell_type::Box)
                    aVisitor("(value)", cell_of<box_cell>(aValue).v);
            }
        }

        thread_local std::string tText;
        thread_local std::string tTypeName;
        thread_local std::vector<std::string> tPartNames;

        char const* debug_describe(char const* aType, unsigned long long aValue, size_t aLimit, char const** aTypeName)
        {
            auto const v = resolve(aType != nullptr ? aType : "value", aValue);
            tText = describe_value(v, aLimit);
            tTypeName = type_name(v);
            if (aTypeName != nullptr)
                *aTypeName = tTypeName.c_str();
            return tText.c_str();
        }

        size_t debug_part_count(char const* aType, unsigned long long aValue)
        {
            auto const v = resolve(aType != nullptr ? aType : "value", aValue);
            std::size_t count = 0u;
            visit_parts(v, [&](std::string const&, value) { ++count; return true; });
            return count;
        }

        size_t debug_parts(char const* aType, unsigned long long aValue, size_t aStart, size_t aCount, neos_rt_debug_part* aParts)
        {
            auto const v = resolve(aType != nullptr ? aType : "value", aValue);
            tPartNames.clear();
            std::vector<value> values;
            std::size_t index = 0u;
            visit_parts(v, [&](std::string const& aName, value aPart)
            {
                if (index++ < aStart)
                    return true;
                if (values.size() == aCount)
                    return false;
                tPartNames.push_back(aName);
                values.push_back(aPart);
                return true;
            });
            for (std::size_t i = 0u; i < values.size(); ++i)
                aParts[i] = neos_rt_debug_part{ tPartNames[i].c_str(), "value", values[i] };
            return values.size();
        }
    }

    // (the debugger's functions (see neos/runtime/debug.h): an error that the program does not handle stops it, if it is being debugged)
    neos_rt_debugger const* gDebugger = nullptr;
}

// (true if the debugger is ending the program: the error need not be reported)
bool neos_lua_debugger_error(std::string const& aMessage)
{
    return neos::lua_runtime::gDebugger != nullptr && neos::lua_runtime::gDebugger->error != nullptr && neos::lua_runtime::gDebugger->error(aMessage.c_str()) != 0;
}

bool neos_lua_debugger_catching()
{
    auto const* const debugger = neos::lua_runtime::gDebugger;
    return debugger != nullptr && debugger->version >= 2u && debugger->catching != nullptr && debugger->catching() != 0;
}

void neos_lua_debugger_thrown(std::string const& aType, std::string const& aMessage, bool aControl)
{
    auto const* const debugger = neos::lua_runtime::gDebugger;
    if (debugger != nullptr && debugger->version >= 2u && debugger->thrown != nullptr)
        debugger->thrown(aType.c_str(), aMessage.c_str(), aControl ? 1 : 0);
}

NEOS_RUNTIME_PLUGIN_SET_DEBUGGER
{
    neos::lua_runtime::gDebugger = aDebugger;
}

NEOS_RUNTIME_PLUGIN_DEBUG
{
    static neos_rt_debug const sHooks{ NEOS_RUNTIME_DEBUG_VERSION, neos::lua_runtime::debug_describe, neos::lua_runtime::debug_part_count, neos::lua_runtime::debug_parts };
    return &sHooks;
}
