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
  The JavaScript runtime plugin's hooks for the debugger (see neos/runtime/debug.h): the values of JavaScript's variables as the 
  JavaScript compiler describes them:
  - "value": a JavaScript value;
  - "env:<hops>:<slot>": a binding that is in an environment (a captured one): the slot of the environment <hops> environments 
    out from the one the variable's local holds;
  - "global": the global object (the variable's value is not used): its properties are the script's globals.
  Values are shown as util.inspect shows them (see console.cpp); no user code runs (proxies' traps and accessors' getters are not 
  called: a proxy's parts are its target's, an accessor is shown as such).
*/

#include "js.hpp"
#include "api.hpp"
#include <string>
#include <string_view>
#include <vector>
#include <neos/runtime/debug.h>

namespace neos::javascript_runtime
{
    namespace
    {
        value resolve(std::string_view aType, value aValue)
        {
            if (aType == "global")
                return the_realm().global;
            if (aType.starts_with("env:"))
            {
                std::uint32_t hops = 0u;
                std::uint32_t slot = 0u;
                auto const rest = aType.substr(4u);
                auto const colon = rest.find(':');
                if (colon == std::string_view::npos)
                    return Undefined;
                for (auto c : rest.substr(0u, colon))
                    hops = hops * 10u + static_cast<std::uint32_t>(c - '0');
                for (auto c : rest.substr(colon + 1u))
                    slot = slot * 10u + static_cast<std::uint32_t>(c - '0');
                auto e = aValue;
                for (std::uint32_t h = 0u; h < hops; ++h)
                {
                    if (!is_internal(e))
                        return Undefined;
                    e = cell_of<environment>(e).parent;
                }
                if (!is_internal(e) || slot >= cell_of<environment>(e).slots.size())
                    return Undefined;
                return cell_of<environment>(e).slots[slot];
            }
            return aValue;
        }

        // (a proxy: its target (its traps are not called))
        value unproxied(value aValue)
        {
            for (int i = 0; i < 64 && is_object(aValue) && obj(aValue).cls == object_class::Proxy; ++i)
                aValue = proxy_target(aValue);
            return aValue;
        }

        std::string type_text(value aValue)
        {
            if (aValue == Empty)
                return "uninitialized";
            if (is_number(aValue))
                return "number";
            if (is_string(aValue))
                return "string";
            if (aValue == Undefined)
                return "undefined";
            if (aValue == Null)
                return "null";
            if (is_boolean(aValue))
                return "boolean";
            if (is_symbol(aValue))
                return "symbol";
            if (is_bigint(aValue))
                return "bigint";
            if (is_object(aValue))
                return is_callable(aValue) ? "function" : obj(aValue).cls == object_class::Array ? "Array" : "object";
            return "internal";
        }

        std::string key_name(value aKey)
        {
            if (is_symbol(aKey))
                return "[" + describe(aKey) + "]";
            auto const text = utf8(aKey);
            bool identifier = !text.empty() && !std::isdigit(static_cast<unsigned char>(text[0]));
            for (auto c : text)
                if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '$' && static_cast<unsigned char>(c) < 0x80u)
                    identifier = false;
            return identifier ? text : "[" + inspect(aKey, false, 0) + "]";
        }

        // (a value's parts: an array's elements, a Map's entries ([key]) or a Set's values, then its own properties (data properties' 
        // values; accessors as such), then its prototype)
        template <typename Visitor>
        void visit_parts(value aValue, Visitor&& aVisitor)
        {
            aValue = unproxied(aValue);
            if (!is_object(aValue))
                return;
            auto& o = obj(aValue);
            bool const array = o.cls == object_class::Array || o.cls == object_class::TypedArray;
            if (o.cls == object_class::Map || o.cls == object_class::Set)
            {
                auto const elements = collection_entries(aValue);
                std::size_t const step = o.cls == object_class::Map ? 2u : 1u;
                for (std::size_t i = 0u; i + step - 1u < elements.size(); i += step)
                    if (!aVisitor(step == 2u ? "[" + inspect(elements[i], false, 0) + "]" : "[" + std::to_string(i) + "]", elements[i + step - 1u]))
                        return;
            }
            for (auto const key : o.own_property_keys())
            {
                auto const desc = o.get_own_property(key);
                if (!desc)
                    continue;
                if (o.callable() && (key == the_names().length || key == the_names().name || key == the_names().prototype) && !desc->enumerable.value_or(false))
                    continue;
                auto const name = array && is_string(key) && !utf8(key).empty() && std::isdigit(static_cast<unsigned char>(utf8(key)[0])) ? "[" + utf8(key) + "]" : key_name(key);
                if (desc->is_accessor())
                {
                    if (!aVisitor(name + " (accessor)", Undefined))
                        return;
                    continue;
                }
                if (!aVisitor(name, desc->valueField.value_or(Undefined)))
                    return;
            }
            if (auto const proto = o.proto(); proto != Null)
                aVisitor("[[Prototype]]", proto);
        }

        // (a variable's parts: the global object's (type "global"): first the global lexical declarations (let, const, class), then its properties)
        template <typename Visitor>
        void visit_typed_parts(std::string_view aType, value aValue, Visitor&& aVisitor)
        {
            if (aType == "global")
                for (auto const& [name, binding] : global_lexicals())
                    if (is_string(name) && !aVisitor(key_name(name), binding.get()))
                        return;
            visit_parts(aValue, aVisitor);
        }

        thread_local std::string tText;
        thread_local std::string tTypeName;
        thread_local std::vector<std::string> tPartNames;

        char const* debug_describe(char const* aType, unsigned long long aValue, size_t aLimit, char const** aTypeName)
        {
            auto const v = resolve(aType != nullptr ? aType : "value", aValue);
            try
            {
                tText = v == Empty ? std::string{ "<uninitialized>" } : inspect(unproxied(v), false, 0);
            }
            catch (...)
            {
                tText = "?";
            }
            if (tText.size() > aLimit)
                tText = tText.substr(0u, aLimit) + "...";
            tTypeName = type_text(v);
            if (aTypeName != nullptr)
                *aTypeName = tTypeName.c_str();
            return tText.c_str();
        }

        size_t debug_part_count(char const* aType, unsigned long long aValue)
        {
            auto const v = resolve(aType != nullptr ? aType : "value", aValue);
            std::size_t count = 0u;
            try
            {
                visit_typed_parts(aType != nullptr ? aType : "value", v, [&](std::string const&, value) { ++count; return true; });
            }
            catch (...)
            {
            }
            return count;
        }

        size_t debug_parts(char const* aType, unsigned long long aValue, size_t aStart, size_t aCount, neos_rt_debug_part* aParts)
        {
            auto const v = resolve(aType != nullptr ? aType : "value", aValue);
            tPartNames.clear();
            std::vector<value> values;
            std::size_t index = 0u;
            try
            {
                visit_typed_parts(aType != nullptr ? aType : "value", v, [&](std::string const& aName, value aPart)
                {
                    if (index++ < aStart)
                        return true;
                    if (values.size() == aCount)
                        return false;
                    tPartNames.push_back(aName);
                    values.push_back(aPart);
                    return true;
                });
            }
            catch (...)
            {
            }
            for (std::size_t i = 0u; i < values.size(); ++i)
                aParts[i] = neos_rt_debug_part{ tPartNames[i].c_str(), "value", values[i] };
            return values.size();
        }
    }

    // (the debugger's functions (see neos/runtime/debug.h): an error that the program does not handle stops it, if it is being debugged)
    neos_rt_debugger const* gDebugger = nullptr;
}

// (true if the debugger is ending the program: the error need not be reported)
bool neos_javascript_debugger_error(std::string const& aMessage)
{
    return neos::javascript_runtime::gDebugger != nullptr && neos::javascript_runtime::gDebugger->error != nullptr && neos::javascript_runtime::gDebugger->error(aMessage.c_str()) != 0;
}

bool neos_javascript_debugger_catching()
{
    auto const* const debugger = neos::javascript_runtime::gDebugger;
    return debugger != nullptr && debugger->version >= 2u && debugger->catching != nullptr && debugger->catching() != 0;
}

void neos_javascript_debugger_thrown(std::string const& aType, std::string const& aMessage, bool aControl)
{
    auto const* const debugger = neos::javascript_runtime::gDebugger;
    if (debugger != nullptr && debugger->version >= 2u && debugger->thrown != nullptr)
        debugger->thrown(aType.c_str(), aMessage.c_str(), aControl ? 1 : 0);
}

// (a value that the program's code throws, where it throws it: its type (an error's name, else its typeof) and message (an error's message, else
// the value as console.log shows it), for a catchpoint (see neos/runtime/debug.h))
void neos_javascript_debugger_thrown_value(neos::javascript_runtime::value aValue)
{
    using namespace neos::javascript_runtime;
    thread_local bool tReporting = false;
    if (tReporting || !neos_javascript_debugger_catching())
        return;
    tReporting = true;
    struct reported { ~reported() { tReporting = false; } } const done;
    js_exception const keep{ aValue };  // (its value is a root while its text is found)
    std::string type;
    std::string message;
    try
    {
        if (is_object(aValue) && obj(aValue).cls == object_class::Error)
        {
            type = utf8(to_string(get(aValue, ascii("name"))));
            message = utf8(to_string(get(aValue, ascii("message"))));
        }
        else
        {
            type = utf8(type_of(aValue));
            message = inspect(aValue, false, 0);
        }
    }
    catch (...)
    {
    }
    neos_javascript_debugger_thrown(type, message, false);
}

NEOS_RUNTIME_PLUGIN_SET_DEBUGGER
{
    neos::javascript_runtime::gDebugger = aDebugger;
}

NEOS_RUNTIME_PLUGIN_DEBUG
{
    static neos_rt_debug const sHooks{ NEOS_RUNTIME_DEBUG_VERSION, neos::javascript_runtime::debug_describe, neos::javascript_runtime::debug_part_count, 
        neos::javascript_runtime::debug_parts };
    return &sHooks;
}
