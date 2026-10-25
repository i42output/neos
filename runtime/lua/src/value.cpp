/*
  value.cpp

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

// Values: strings (interned if short), numbers and their conversions, the state and errors

#include <algorithm>
#include <cerrno>
#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include "lua.hpp"

namespace neos::lua_runtime
{
    // --- the state

    state& create_state()
    {
        static state* sState = new state{};
        gState = sState;
        return *sState;
    }

    namespace
    {
        root_tracer const sStateRoots{ [](tracer& aTracer)
        {
            auto& s = the_state();
            aTracer.mark(s.globals);
            aTracer.mark(s.registry);
            aTracer.mark(s.loaded);
            aTracer.mark(s.stringMetatable);
            for (auto v : s.typeMetatables)
                aTracer.mark(v);
            aTracer.mark(s.pending);
            for (auto v : s.names)
                aTracer.mark(v);
            aTracer.mark(s.mainThread);
            s.mainStacks.trace(aTracer);
            s.stacks->trace(aTracer);
            if (s.current != nullptr)
                aTracer.mark(s.current);
        } };
    }

    // --- strings

    namespace
    {
        // (open addressing (linear probing) of the strings' values and their texts' hashes; no tombstones: the table is rebuilt (from 
        // the strings that are alive) after each collection; the pinned strings are listed (they are roots))
        struct intern_table
        {
            struct entry
            {
                value v = 0u;           ///< (0: a free slot)
                std::size_t hash = 0u;
            };
            std::vector<entry> slots = std::vector<entry>(1024u);
            std::size_t count = 0u;
            std::vector<value> pinned;

            static std::size_t hash_of(std::string_view aText)
            {
                return std::hash<std::string_view>{}(aText);
            }
            value find(std::string_view aText, std::size_t aHash) const
            {
                auto const mask = slots.size() - 1u;
                for (auto i = aHash & mask;; i = (i + 1u) & mask)
                {
                    auto const& e = slots[i];
                    if (e.v == 0u)
                        return 0u;
                    if (e.hash == aHash && cell_of<string_cell>(e.v).text == aText)
                        return e.v;
                }
            }
            void place(entry const& aEntry)
            {
                auto const mask = slots.size() - 1u;
                auto i = aEntry.hash & mask;
                while (slots[i].v != 0u)
                    i = (i + 1u) & mask;
                slots[i] = aEntry;
            }
            void insert(value aValue, std::size_t aHash)
            {
                if ((count + 1u) * 2u > slots.size())
                    rebuild(slots.size() * 2u, [](value) { return true; });
                place(entry{ aValue, aHash });
                ++count;
            }
            template <typename Predicate>
            void rebuild(std::size_t aSize, Predicate aKeep)
            {
                std::vector<entry> live;
                live.reserve(count);
                for (auto const& e : slots)
                    if (e.v != 0u && aKeep(e.v))
                        live.push_back(e);
                while (aSize > 1024u && live.size() * 8u < aSize)
                    aSize /= 2u;
                slots.assign(aSize, entry{});
                count = live.size();
                for (auto const& e : live)
                    place(e);
            }
        };

        intern_table& interned()
        {
            static intern_table* sTable = new intern_table{};
            return *sTable;
        }

        // (pinned interned strings live as long as the program; the others while they are referred to: those that die are removed)
        root_tracer const sInternedRoots{ [](tracer& aTracer)
        {
            for (auto v : interned().pinned)
                aTracer.mark(v);
        }, [](tracer& aTracer)
        {
            auto& table = interned();
            table.rebuild(table.slots.size(), [&](value v) { return aTracer.alive(v); });
        } };
    }

    value intern(std::string_view aText, bool aPinned)
    {
        auto& table = interned();
        auto const hash = intern_table::hash_of(aText);
        if (auto const existing = table.find(aText, hash); existing != 0u)
        {
            if (aPinned)
            {
                auto& s = cell_of<string_cell>(existing);
                if (!s.pinned)
                {
                    s.pinned = true;
                    table.pinned.push_back(existing);
                }
            }
            return existing;
        }
        the_heap().account(aText.size());
        auto& s = the_heap().make<string_cell>(std::string{ aText });
        s.interned = true;
        s.pinned = aPinned;
        s.hashValue = hash;
        s.hashed = true;
        auto const result = value_of(s);
        if (aPinned)
            table.pinned.push_back(result);
        table.insert(result, hash);
        return result;
    }

    value make_string(std::string_view aText)
    {
        if (aText.size() <= MaxInternedLength)
            return intern(aText);
        the_heap().account(aText.size());
        auto& s = the_heap().make<string_cell>(std::string{ aText });
        return value_of(s);
    }

    value make_string(std::string&& aText)
    {
        if (aText.size() <= MaxInternedLength)
            return intern(aText);
        the_heap().account(aText.size());
        auto& s = the_heap().make<string_cell>(std::move(aText));
        return value_of(s);
    }

    // --- other cells

    value make_box(value aValue)
    {
        return value_of(the_heap().make<box_cell>(aValue));
    }

    value make_native(native_function aFunction, char const* aName, std::initializer_list<value> aUpvalues)
    {
        value_list keep{ aUpvalues };
        auto& f = the_heap().make<function_cell>();
        f.native = true;
        f.function = aFunction;
        f.name = aName;
        f.upvalues.assign(aUpvalues.begin(), aUpvalues.end());
        return value_of(f);
    }

    // --- numbers

    value make_integer(std::int64_t aValue)
    {
        if (fits_inline(aValue))
            return TagInteger | (static_cast<value>(aValue) & PayloadMask);
        return value_of(the_heap().make<integer_cell>(aValue));
    }

    bool float_to_integer(double aValue, std::int64_t& aResult)
    {
        // (exactly: its value is an integer in the range of 64 bit integers)
        if (!(aValue >= -9223372036854775808.0 && aValue < 9223372036854775808.0))
            return false;
        auto const i = static_cast<std::int64_t>(aValue);
        if (static_cast<double>(i) != aValue)
            return false;
        aResult = i;
        return true;
    }

    bool to_integer(value aValue, std::int64_t& aResult, bool aStrings)
    {
        if (is_integer(aValue))
        {
            aResult = integer_of(aValue);
            return true;
        }
        if (is_float(aValue))
            return float_to_integer(float_of(aValue), aResult);
        if (aStrings && is_string(aValue))
        {
            value n;
            if (string_to_number(str_view(aValue), n))
                return to_integer(n, aResult);
        }
        return false;
    }

    bool to_number(value aValue, value& aResult)
    {
        if (is_number(aValue))
        {
            aResult = aValue;
            return true;
        }
        if (is_string(aValue))
            return string_to_number(str_view(aValue), aResult);
        return false;
    }

    namespace
    {
        bool is_space(char c)
        {
            return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
        }

        int hex_digit(char c)
        {
            if (c >= '0' && c <= '9')
                return c - '0';
            if (c >= 'a' && c <= 'f')
                return c - 'a' + 10;
            if (c >= 'A' && c <= 'F')
                return c - 'A' + 10;
            return -1;
        }
    }

    bool string_to_number(std::string_view aText, value& aResult)
    {
        // (as a numeral (with an optional sign), with white space before and after it)
        std::size_t begin = 0u;
        std::size_t end = aText.size();
        while (begin < end && is_space(aText[begin]))
            ++begin;
        while (end > begin && is_space(aText[end - 1u]))
            --end;
        auto text = aText.substr(begin, end - begin);
        if (text.empty())
            return false;
        bool negative = false;
        std::size_t i = 0u;
        if (text[0] == '-' || text[0] == '+')
        {
            negative = text[0] == '-';
            ++i;
        }
        if (i + 1u < text.size() && text[i] == '0' && (text[i + 1u] == 'x' || text[i + 1u] == 'X'))
        {
            // (hexadecimal: an integer (wrapping around) unless it has a fraction or an exponent)
            std::size_t j = i + 2u;
            std::uint64_t integer = 0u;
            double mantissa = 0.0;
            int exponent = 0;
            bool anyDigit = false;
            bool isFloat = false;
            while (j < text.size() && hex_digit(text[j]) >= 0)
            {
                integer = integer * 16u + static_cast<std::uint64_t>(hex_digit(text[j]));
                mantissa = mantissa * 16.0 + hex_digit(text[j]);
                anyDigit = true;
                ++j;
            }
            if (j < text.size() && text[j] == '.')
            {
                isFloat = true;
                ++j;
                while (j < text.size() && hex_digit(text[j]) >= 0)
                {
                    mantissa = mantissa * 16.0 + hex_digit(text[j]);
                    exponent -= 4;
                    anyDigit = true;
                    ++j;
                }
            }
            if (!anyDigit)
                return false;
            if (j < text.size() && (text[j] == 'p' || text[j] == 'P'))
            {
                isFloat = true;
                ++j;
                bool expNegative = false;
                if (j < text.size() && (text[j] == '+' || text[j] == '-'))
                    expNegative = text[j++] == '-';
                if (j >= text.size() || text[j] < '0' || text[j] > '9')
                    return false;
                int e = 0;
                while (j < text.size() && text[j] >= '0' && text[j] <= '9')
                {
                    if (e < 100000)
                        e = e * 10 + (text[j] - '0');
                    ++j;
                }
                exponent += expNegative ? -e : e;
            }
            if (j != text.size())
                return false;
            if (isFloat)
            {
                auto const d = std::ldexp(mantissa, exponent);
                aResult = make_float(negative ? -d : d);
            }
            else
                aResult = make_integer(static_cast<std::int64_t>(negative ? 0u - integer : integer));
            return true;
        }
        // (decimal: digits, an optional fraction and exponent; an integer unless it has a fraction or an exponent or overflows)
        std::size_t j = i;
        bool anyDigit = false;
        bool isFloat = false;
        while (j < text.size() && text[j] >= '0' && text[j] <= '9')
        {
            anyDigit = true;
            ++j;
        }
        if (j < text.size() && text[j] == '.')
        {
            isFloat = true;
            ++j;
            while (j < text.size() && text[j] >= '0' && text[j] <= '9')
            {
                anyDigit = true;
                ++j;
            }
        }
        if (!anyDigit)
            return false;
        if (j < text.size() && (text[j] == 'e' || text[j] == 'E'))
        {
            isFloat = true;
            ++j;
            if (j < text.size() && (text[j] == '+' || text[j] == '-'))
                ++j;
            if (j >= text.size() || text[j] < '0' || text[j] > '9')
                return false;
            while (j < text.size() && text[j] >= '0' && text[j] <= '9')
                ++j;
        }
        if (j != text.size())
            return false;
        if (!isFloat)
        {
            std::uint64_t value = 0u;
            bool overflow = false;
            for (std::size_t k = i; k < text.size(); ++k)
            {
                auto const digit = static_cast<std::uint64_t>(text[k] - '0');
                if (value > (0x7FFFFFFFFFFFFFFFull - digit) / 10u)
                {
                    // (-9223372036854775808 fits)
                    if (negative && k + 1u == text.size() && value == 922337203685477580ull && digit == 8u)
                    {
                        aResult = make_integer(std::numeric_limits<std::int64_t>::min());
                        return true;
                    }
                    overflow = true;
                    break;
                }
                value = value * 10u + digit;
            }
            if (!overflow)
            {
                aResult = make_integer(negative ? -static_cast<std::int64_t>(value) : static_cast<std::int64_t>(value));
                return true;
            }
        }
        std::string const copy{ text };
        aResult = make_float(std::strtod(copy.c_str(), nullptr));
        return true;
    }

    std::string integer_text(std::int64_t aValue)
    {
        char buffer[32];
        auto const result = std::to_chars(buffer, buffer + sizeof(buffer), aValue);
        return std::string{ buffer, result.ptr };
    }

    std::string float_text(double aValue)
    {
        if (std::isnan(aValue))
            return std::signbit(aValue) ? "-nan" : "nan";
        if (std::isinf(aValue))
            return aValue < 0.0 ? "-inf" : "inf";
        char buffer[64];
        std::snprintf(buffer, sizeof(buffer), "%.14g", aValue);
        std::string result{ buffer };
        // (a float that looks like an integer: ".0")
        if (result.find_first_of(".eE") == std::string::npos)
            result += ".0";
        return result;
    }

    std::string number_text(value aNumber)
    {
        return is_float(aNumber) ? float_text(float_of(aNumber)) : integer_text(integer_of(aNumber));
    }

    // --- types

    std::string type_name(value aValue)
    {
        if (is_nil(aValue))
            return "nil";
        if (is_boolean(aValue))
            return "boolean";
        if (is_number(aValue))
            return "number";
        if (is_string(aValue))
            return "string";
        if (is_table(aValue))
            return "table";
        if (is_function(aValue))
            return "function";
        if (auto* c = cell_at(aValue))
        {
            switch (c->type)
            {
            case cell_type::Userdata: return "userdata";
            case cell_type::Thread: return "thread";
            default: break;
            }
        }
        return "no value";
    }

    std::string object_type_name(value aValue)
    {
        auto const mt = metatable_of(aValue);
        if (is_table(mt))
        {
            auto const n = tab(mt).get_string(name(NameName));
            if (is_string(n))
                return str(n);
        }
        return type_name(aValue);
    }

    // --- errors

    std::string where(int aLevel)
    {
        // (the position of the Lua function at the level (1: the function that is running, as the host's frames name it))
        auto const* h = host();
        if (h == nullptr || h->frames == nullptr || aLevel < 1)
            return {};
        neos_rt_frame frames[32];
        auto const count = h->frames(frames, std::min<unsigned>(32u, static_cast<unsigned>(aLevel)));
        if (count < static_cast<unsigned>(aLevel))
            return {};
        auto const& f = frames[aLevel - 1];
        // (the function at the level below it was called by a native function: the level is that native function (it has no position))
        if (aLevel >= 2)
        {
            auto const& boundaries = stacks().boundaries;
            if (std::find(boundaries.begin(), boundaries.end(), f.id) != boundaries.end())
                return {};
        }
        if (f.source == nullptr || f.line == 0u)
            return {};
        std::string source{ f.source };
        auto const slash = source.find_last_of("/\\");
        if (slash != std::string::npos && source.find("[string") != 0u)
            source = source.substr(slash + 1u);
        return source + ":" + std::to_string(f.line) + ": ";
    }

    namespace
    {
        NEOS_LUA_FAST_TLS thread_local bool tIndirect = false;   ///< (a native function that a native function called is running: its errors have no position)
    }

    indirect_scope::indirect_scope(bool aIndirect) : iPrevious{ tIndirect }
    {
        tIndirect = aIndirect;
    }

    indirect_scope::~indirect_scope()
    {
        tIndirect = iPrevious;
    }

    void runtime_error(std::string const& aMessage)
    {
        throw_value(make_string(tIndirect ? aMessage : where(1) + aMessage));
    }

    void argument_error(native_call const& aCall, std::size_t aIndex, std::string const& aMessage)
    {
        // (called by Lua code: with its position and the function's name; else with the function's qualified name)
        auto const name = std::string{ aCall.direct ? aCall.self.name : aCall.self.qualified };
        if (aCall.method && aIndex == 0u)
            runtime_error("calling '" + name + "' on bad self (" + aMessage + ")");
        auto const message = "bad argument #" + std::to_string(aCall.method ? aIndex : aIndex + 1u) + " to '" + name + "' (" + aMessage + ")";
        if (aCall.direct)
            runtime_error(message);
        throw_value(make_string(message));
    }

    void type_error(native_call const& aCall, std::size_t aIndex, char const* aExpected)
    {
        argument_error(aCall, aIndex, std::string{ aExpected } + " expected, got " + (aCall.none(aIndex) ? std::string{ "no value" } : object_type_name(aCall.arg(aIndex))));
    }

    std::int64_t check_integer(native_call const& aCall, std::size_t aIndex)
    {
        auto const v = aCall.arg(aIndex);
        std::int64_t result;
        if (to_integer(v, result, true))
            return result;
        value n;
        if (to_number(v, n))
            argument_error(aCall, aIndex, "number has no integer representation");
        type_error(aCall, aIndex, "number");
    }

    std::int64_t optional_integer(native_call const& aCall, std::size_t aIndex, std::int64_t aDefault)
    {
        if (is_nil(aCall.arg(aIndex)))
            return aDefault;
        return check_integer(aCall, aIndex);
    }

    double check_number(native_call const& aCall, std::size_t aIndex)
    {
        value n;
        if (!to_number(aCall.arg(aIndex), n))
            type_error(aCall, aIndex, "number");
        return number_of(n);
    }

    value check_number_value(native_call const& aCall, std::size_t aIndex)
    {
        value n;
        if (!to_number(aCall.arg(aIndex), n))
            type_error(aCall, aIndex, "number");
        return n;
    }

    value check_string_value(native_call const& aCall, std::size_t aIndex)
    {
        auto const v = aCall.arg(aIndex);
        if (is_string(v))
            return v;
        if (is_number(v))
            return make_string(number_text(v));
        type_error(aCall, aIndex, "string");
    }

    std::string_view check_string(native_call const& aCall, std::size_t aIndex)
    {
        auto const v = aCall.arg(aIndex);
        if (is_string(v))
            return str_view(v);
        if (is_number(v))
        {
            // (the number's string: kept alive (with the others most recently converted) while the native function uses it)
            auto const s = make_string(number_text(v));
            auto const view = str_view(s);
            static static_values tKeep;
            tKeep.push_back(s);
            if (tKeep.size() > 64u)
                tKeep->erase(tKeep->begin(), tKeep->begin() + 32);
            return view;
        }
        type_error(aCall, aIndex, "string");
    }

    std::string_view optional_string(native_call const& aCall, std::size_t aIndex, std::string_view aDefault)
    {
        if (is_nil(aCall.arg(aIndex)))
            return aDefault;
        return check_string(aCall, aIndex);
    }

    table& check_table(native_call const& aCall, std::size_t aIndex)
    {
        if (!is_table(aCall.arg(aIndex)))
            type_error(aCall, aIndex, "table");
        return tab(aCall.arg(aIndex));
    }

    void check_any(native_call const& aCall, std::size_t aIndex)
    {
        if (aCall.none(aIndex))
            argument_error(aCall, aIndex, "value expected");
    }

    // --- standard output

    void write_standard(int aStream, std::string_view aText)
    {
        auto const* h = host();
        if (h != nullptr && h->version >= 9u && h->std_write != nullptr)
            h->std_write(aStream, aText.data(), aText.size());
        else
            std::fwrite(aText.data(), 1u, aText.size(), aStream == 2 ? stderr : stdout);
    }

    void flush_standard(int aStream)
    {
        auto const* h = host();
        if (h != nullptr && h->version >= 9u && h->std_flush != nullptr)
            h->std_flush(aStream);
        else
            std::fflush(aStream == 2 ? stderr : stdout);
    }
}
