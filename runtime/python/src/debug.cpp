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

// The Python runtime plugin's hooks for the debugger (see neos/runtime/debug.h): the values of Python's variables, objects (the VM 
// gives the hooks an object's address: type "pointer"), shown as repr() shows them; their parts: a list's or tuple's items, a dict's 
// entries, an object's attributes (its __dict__). An object's repr is not called while an exception is being raised (it would be 
// cleared): its type is shown instead.

#include <cstdio>
#include <string>
#include <string_view>
#include <vector>
#include "objects.hpp"
#include <neos/runtime/debug.h>

namespace neos::python_runtime
{
    namespace detail
    {
        namespace
        {
            object* object_of(char const* aType, unsigned long long aValue)
            {
                if (aType == nullptr || std::string_view{ aType } != "pointer")
                    return nullptr;
                return reinterpret_cast<object*>(static_cast<std::uintptr_t>(aValue & ~1ull));
            }

            std::string address_text(object* aObject)
            {
                char buffer[96];
                std::snprintf(buffer, sizeof(buffer), "<%s object at %p>", type_name(aObject), static_cast<void*>(aObject));
                return buffer;
            }

            std::string repr_safe(object* aObject, std::size_t aLimit)
            {
                if (aObject == nullptr)
                    return "<no value>";
                std::string text;
                if (!failed())
                    text = repr_text(aObject);
                if (text.empty())
                    text = address_text(aObject);
                if (text.size() > aLimit)
                    text = text.substr(0u, aLimit) + "...";
                return text;
            }

            // (an object's parts (borrowed: an object's __dict__ is kept (in tHeld) until the hooks are next called))
            thread_local std::vector<ref> tHeld;

            template <typename Visitor>
            void visit_parts(object* aObject, Visitor&& aVisitor)
            {
                if (aObject == nullptr)
                    return;
                switch (kind_of(aObject))
                {
                case NEOS_PY_LIST:
                case NEOS_PY_TUPLE:
                    {
                        auto const& items = kind_of(aObject) == NEOS_PY_LIST ? as<list_object>(aObject)->items : as<tuple_object>(aObject)->items;
                        for (std::size_t i = 0u; i < items.size(); ++i)
                            if (!aVisitor("[" + std::to_string(i) + "]", items[i]))
                                return;
                    }
                    return;
                case NEOS_PY_DICT:
                    for (auto const& [key, v] : as<dict_object>(aObject)->items)
                        if (key != nullptr && !aVisitor("[" + repr_safe(key, 64u) + "]", v))
                            return;
                    return;
                case NEOS_PY_STR:
                case NEOS_PY_INT:
                case NEOS_PY_BOOL:
                case NEOS_PY_NONE:
                case NEOS_PY_FLOAT:
                case NEOS_PY_COMPLEX:
                case NEOS_PY_RANGE:
                    return;
                default:
                    break;
                }
                // (an object's attributes)
                if (failed())
                    return;
                ref attributes{ getattr(aObject, "__dict__") };
                if (!attributes)
                {
                    clear_error();
                    return;
                }
                if (kind_of(attributes.get()) != NEOS_PY_DICT)
                    return;
                auto* const d = attributes.get();
                tHeld.push_back(std::move(attributes));
                for (auto const& [key, v] : as<dict_object>(d)->items)
                {
                    if (key == nullptr)
                        continue;
                    std::string name;
                    if (kind_of(key) == NEOS_PY_STR)
                        name = as<str_object>(key)->value;
                    else
                        name = "[" + repr_safe(key, 64u) + "]";
                    if (!aVisitor(name, v))
                        return;
                }
            }

            thread_local std::string tText;
            thread_local std::string tTypeName;
            thread_local std::vector<std::string> tPartNames;

            char const* debug_describe(char const* aType, unsigned long long aValue, size_t aLimit, char const** aTypeName)
            {
                tHeld.clear();
                auto* const o = object_of(aType, aValue);
                tText = repr_safe(o, aLimit);
                tTypeName = o != nullptr ? type_name(o) : "";
                if (aTypeName != nullptr)
                    *aTypeName = tTypeName.c_str();
                return tText.c_str();
            }

            size_t debug_part_count(char const* aType, unsigned long long aValue)
            {
                std::size_t count = 0u;
                visit_parts(object_of(aType, aValue), [&](std::string const&, object*) { ++count; return true; });
                return count;
            }

            size_t debug_parts(char const* aType, unsigned long long aValue, size_t aStart, size_t aCount, neos_rt_debug_part* aParts)
            {
                tHeld.clear();
                tPartNames.clear();
                std::vector<object*> values;
                std::size_t index = 0u;
                visit_parts(object_of(aType, aValue), [&](std::string const& aName, object* aPart)
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
                    aParts[i] = neos_rt_debug_part{ tPartNames[i].c_str(), "pointer", static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(values[i])) };
                return values.size();
            }
        }
    }

    // (the debugger's functions (see neos/runtime/debug.h): an error that the program does not handle stops it, if it is being debugged)
    neos_rt_debugger const* gDebugger = nullptr;
}

// (true if the debugger is ending the program: the error need not be reported)
bool neos_python_debugger_error(std::string const& aMessage)
{
    return neos::python_runtime::gDebugger != nullptr && neos::python_runtime::gDebugger->error != nullptr && neos::python_runtime::gDebugger->error(aMessage.c_str()) != 0;
}

bool neos_python_debugger_catching()
{
    auto const* const debugger = neos::python_runtime::gDebugger;
    return debugger != nullptr && debugger->version >= 2u && debugger->catching != nullptr && debugger->catching() != 0;
}

void neos_python_debugger_thrown(std::string const& aType, std::string const& aMessage, bool aControl)
{
    auto const* const debugger = neos::python_runtime::gDebugger;
    if (debugger != nullptr && debugger->version >= 2u && debugger->thrown != nullptr)
        debugger->thrown(aType.c_str(), aMessage.c_str(), aControl ? 1 : 0);
}

NEOS_RUNTIME_PLUGIN_SET_DEBUGGER
{
    neos::python_runtime::gDebugger = aDebugger;
}

NEOS_RUNTIME_PLUGIN_DEBUG
{
    static neos_rt_debug const sHooks{ NEOS_RUNTIME_DEBUG_VERSION, neos::python_runtime::detail::debug_describe, neos::python_runtime::detail::debug_part_count, 
        neos::python_runtime::detail::debug_parts };
    return &sHooks;
}
