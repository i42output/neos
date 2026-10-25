/*
  ops.cpp

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

// Operations: metatables and metamethods, indexing, arithmetic, comparisons, concatenation, length and tostring

#include <algorithm>
#include <cstdio>
#include <limits>
#include "lua.hpp"

namespace neos::lua_runtime
{
    // --- metatables

    value metatable_of(value aValue)
    {
        if (is_table(aValue))
            return tab(aValue).metatable;
        if (is_string(aValue))
            return the_state().stringMetatable;
        auto& s = the_state();
        if (is_nil(aValue))
            return s.typeMetatables[0];
        if (is_boolean(aValue))
            return s.typeMetatables[1];
        if (is_float(aValue) || is_inline_integer(aValue))
            return s.typeMetatables[2];
        if (is_function(aValue))
            return s.typeMetatables[3];
        if (auto* c = cell_at(aValue))
        {
            switch (c->type)
            {
            case cell_type::Userdata: return static_cast<userdata_cell*>(c)->metatable;
            case cell_type::Thread: return s.typeMetatables[4];
            case cell_type::Integer: return s.typeMetatables[2];
            default: break;
            }
        }
        return Nil;
    }

    value metamethod(value aValue, name_index aEvent)
    {
        auto const mt = metatable_of(aValue);
        if (!is_table(mt))
            return Nil;
        return tab(mt).get_string(name(aEvent));
    }

    // --- raw access

    value raw_get(value aTable, value aKey)
    {
        return tab(aTable).get(aKey);
    }

    void raw_set(value aTable, value aKey, value aValue)
    {
        if (is_nil(aKey))
            runtime_error("table index is nil");
        if (is_float(aKey) && std::isnan(float_of(aKey)))
            runtime_error("table index is NaN");
        tab(aTable).set(normalize_key(aKey), aValue);
    }

    // --- indexing

    namespace
    {
        constexpr int MaxTagLoop = 2000;

        std::string described(value aDescription)
        {
            return is_string(aDescription) ? " (" + str(aDescription) + ")" : std::string{};
        }
    }

    value index(value aObject, value aKey, value aDescription)
    {
        return index(aObject, aKey, aDescription, false);
    }

    value index(value aObject, value aKey, value aDescription, bool aMissed)
    {
        // (aMissed: the object is a table that does not have the key)
        auto object = aObject;
        for (int loop = 0; loop < MaxTagLoop; ++loop)
        {
            value handler;
            if (is_table(object))
            {
                auto& t = tab(object);
                auto const v = loop == 0 && aMissed ? Nil : t.get(aKey);
                if (v != Nil)
                    return v;
                if (!is_table(t.metatable))
                    return Nil;
                handler = tab(t.metatable).get_string(name(NameIndex));
                if (is_nil(handler))
                    return Nil;
            }
            else
            {
                handler = metamethod(object, NameIndex);
                if (is_nil(handler))
                {
                    if (loop == 0)
                    {
                        // (a field of a value that has none: its key, if it is a string, describes it)
                        if (is_nil(aDescription) && is_string(aKey))
                            runtime_error("attempt to index a " + type_name(object) + " value (field '" + str(aKey) + "')");
                        runtime_error("attempt to index a " + type_name(object) + " value" + described(aDescription));
                    }
                    runtime_error("attempt to index a " + type_name(object) + " value");
                }
            }
            if (is_function(handler))
            {
                value const arguments[] = { object, aKey };
                return call1(handler, arguments);
            }
            object = handler;
        }
        runtime_error("'__index' chain too long; possible loop");
    }

    void set_index(value aObject, value aKey, value aValue, value aDescription)
    {
        auto object = aObject;
        for (int loop = 0; loop < MaxTagLoop; ++loop)
        {
            value handler;
            if (is_table(object))
            {
                auto& t = tab(object);
                if (!is_table(t.metatable) || t.get(aKey) != Nil)
                {
                    raw_set(object, aKey, aValue);
                    return;
                }
                handler = tab(t.metatable).get_string(name(NameNewIndex));
                if (is_nil(handler))
                {
                    raw_set(object, aKey, aValue);
                    return;
                }
            }
            else
            {
                handler = metamethod(object, NameNewIndex);
                if (is_nil(handler))
                {
                    if (loop == 0)
                        runtime_error("attempt to index a " + type_name(object) + " value" + described(aDescription));
                    runtime_error("attempt to index a " + type_name(object) + " value");
                }
            }
            if (is_function(handler))
            {
                value_list results;
                value const arguments[] = { object, aKey, aValue };
                call(handler, arguments, results);
                return;
            }
            object = handler;
        }
        runtime_error("'__newindex' chain too long; possible loop");
    }

    // --- equality and comparisons

    bool raw_equal(value aLeft, value aRight)
    {
        if (aLeft == aRight)
            return !is_float(aLeft) || !std::isnan(float_of(aLeft));
        if (is_float(aLeft) || is_float(aRight))
        {
            if (is_float(aLeft) && is_float(aRight))
                return float_of(aLeft) == float_of(aRight);
            auto const f = is_float(aLeft) ? float_of(aLeft) : float_of(aRight);
            auto const other = is_float(aLeft) ? aRight : aLeft;
            if (!is_integer(other))
                return false;
            std::int64_t i;
            return float_to_integer(f, i) && i == integer_of(other);
        }
        if (is_string(aLeft) && is_string(aRight))
            return key_equal(aLeft, aRight);
        if (is_integer(aLeft) && is_integer(aRight))
            return integer_of(aLeft) == integer_of(aRight);
        return false;
    }

    bool equal(value aLeft, value aRight)
    {
        if (raw_equal(aLeft, aRight))
            return true;
        // (tables or userdata: __eq, of the first operand's metatable, else the second's)
        bool const tables = is_table(aLeft) && is_table(aRight);
        bool const userdata = is_type(aLeft, cell_type::Userdata) && is_type(aRight, cell_type::Userdata);
        if (!tables && !userdata)
            return false;
        auto handler = metamethod(aLeft, NameEq);
        if (is_nil(handler))
            handler = metamethod(aRight, NameEq);
        if (is_nil(handler))
            return false;
        value const arguments[] = { aLeft, aRight };
        return truthy(call1(handler, arguments));
    }

    namespace
    {
        constexpr double TwoTo63 = 9223372036854775808.0;

        bool lt_integer_float(std::int64_t i, double f)
        {
            if (std::isnan(f))
                return false;
            if (f >= TwoTo63)
                return true;
            if (f <= -TwoTo63)
                return false;
            return i < static_cast<std::int64_t>(std::ceil(f));
        }

        bool le_integer_float(std::int64_t i, double f)
        {
            if (std::isnan(f))
                return false;
            if (f >= TwoTo63)
                return true;
            if (f < -TwoTo63)
                return false;
            return i <= static_cast<std::int64_t>(std::floor(f));
        }

        bool lt_float_integer(double f, std::int64_t i)
        {
            if (std::isnan(f))
                return false;
            if (f >= TwoTo63)
                return false;
            if (f < -TwoTo63)
                return true;
            return static_cast<std::int64_t>(std::floor(f)) < i;
        }

        bool le_float_integer(double f, std::int64_t i)
        {
            if (std::isnan(f))
                return false;
            if (f >= TwoTo63)
                return false;
            if (f <= -TwoTo63)
                return true;
            return static_cast<std::int64_t>(std::ceil(f)) <= i;
        }

        bool numeric_less(value a, value b, bool aOrEqual)
        {
            if (is_integer(a) && is_integer(b))
                return aOrEqual ? integer_of(a) <= integer_of(b) : integer_of(a) < integer_of(b);
            if (is_float(a) && is_float(b))
                return aOrEqual ? float_of(a) <= float_of(b) : float_of(a) < float_of(b);
            if (is_integer(a))
                return aOrEqual ? le_integer_float(integer_of(a), float_of(b)) : lt_integer_float(integer_of(a), float_of(b));
            return aOrEqual ? le_float_integer(float_of(a), integer_of(b)) : lt_float_integer(float_of(a), integer_of(b));
        }

        [[noreturn]] void compare_error(value a, value b)
        {
            auto const t1 = object_type_name(a);
            auto const t2 = object_type_name(b);
            if (t1 == t2)
                runtime_error("attempt to compare two " + t1 + " values");
            runtime_error("attempt to compare " + t1 + " with " + t2);
        }

        bool compare(value a, value b, bool aOrEqual)
        {
            if (is_number(a) && is_number(b))
                return numeric_less(a, b, aOrEqual);
            if (is_string(a) && is_string(b))
            {
                auto const& l = str(a);
                auto const& r = str(b);
                auto const c = l.compare(r);
                return aOrEqual ? c <= 0 : c < 0;
            }
            auto const event = aOrEqual ? NameLe : NameLt;
            auto handler = metamethod(a, event);
            if (is_nil(handler))
                handler = metamethod(b, event);
            if (is_nil(handler))
                compare_error(a, b);
            value const arguments[] = { a, b };
            return truthy(call1(handler, arguments));
        }
    }

    bool less_than(value aLeft, value aRight)
    {
        return compare(aLeft, aRight, false);
    }

    bool less_equal(value aLeft, value aRight)
    {
        return compare(aLeft, aRight, true);
    }

    // --- arithmetic

    namespace
    {
        enum : int
        {
            OpAdd, OpSub, OpMul, OpMod, OpPow, OpDiv, OpIDiv, OpBAnd, OpBOr, OpBXor, OpShl, OpShr, OpUnm, OpBNot
        };

        name_index const sEvents[] = { NameAdd, NameSub, NameMul, NameMod, NamePow, NameDiv, NameIDiv, NameBAnd, NameBOr, NameBXor, NameShl,
            NameShr, NameUnm, NameBNot };
        char const* const sOperationNames[] = { "add", "sub", "mul", "mod", "pow", "div", "idiv", "band", "bor", "bxor", "shl", "shr", "unm", "bnot" };

        bool is_bitwise(int aOperation)
        {
            return (aOperation >= OpBAnd && aOperation <= OpShr) || aOperation == OpBNot;
        }

        std::int64_t shift_left(std::int64_t a, std::int64_t n)
        {
            if (n <= -64 || n >= 64)
                return 0;
            if (n >= 0)
                return static_cast<std::int64_t>(static_cast<std::uint64_t>(a) << n);
            return static_cast<std::int64_t>(static_cast<std::uint64_t>(a) >> (-n));
        }

        std::int64_t integer_operation(int aOperation, std::int64_t a, std::int64_t b)
        {
            auto const ua = static_cast<std::uint64_t>(a);
            auto const ub = static_cast<std::uint64_t>(b);
            switch (aOperation)
            {
            case OpAdd: return static_cast<std::int64_t>(ua + ub);
            case OpSub: return static_cast<std::int64_t>(ua - ub);
            case OpMul: return static_cast<std::int64_t>(ua * ub);
            case OpMod:
                {
                    if (b == 0)
                        runtime_error("attempt to perform 'n%0'");
                    if (b == -1)
                        return 0;
                    auto m = a % b;
                    if (m != 0 && (m ^ b) < 0)
                        m += b;
                    return m;
                }
            case OpIDiv:
                {
                    if (b == 0)
                        runtime_error("attempt to divide by zero");
                    if (b == -1)
                        return static_cast<std::int64_t>(0u - ua);
                    auto q = a / b;
                    if ((a % b != 0) && ((a ^ b) < 0))
                        q -= 1;
                    return q;
                }
            case OpBAnd: return a & b;
            case OpBOr: return a | b;
            case OpBXor: return a ^ b;
            case OpShl: return shift_left(a, b);
            case OpShr: return shift_left(a, b == std::numeric_limits<std::int64_t>::min() ? 64 : -b);
            case OpUnm: return static_cast<std::int64_t>(0u - ua);
            case OpBNot: return ~a;
            default: return 0;
            }
        }

        double float_operation(int aOperation, double a, double b)
        {
            switch (aOperation)
            {
            case OpAdd: return a + b;
            case OpSub: return a - b;
            case OpMul: return a * b;
            case OpDiv: return a / b;
            case OpPow: return b == 2.0 ? a * a : std::pow(a, b);
            case OpIDiv: return std::floor(a / b);
            case OpMod:
                {
                    // (floored: a nonzero remainder whose sign is not the divisor's is adjusted)
                    double m = std::fmod(a, b);
                    if ((m > 0.0 && b < 0.0) || (m < 0.0 && b > 0.0))
                        m += b;
                    return m;
                }
            case OpUnm: return -a;
            default: return 0.0;
            }
        }

        // (the operands as numbers: strings are converted (as the string library's metamethods do), except for bitwise operations)
        bool numeric_operands(int aOperation, value& a, value& b)
        {
            if (is_number(a) && is_number(b))
                return true;
            if (is_bitwise(aOperation))
                return false;
            value na, nb;
            if (to_number(a, na) && to_number(b, nb))
            {
                a = na;
                b = nb;
                return true;
            }
            return false;
        }

        [[noreturn]] void arith_error(int aOperation, value a, value b, value aDescriptionA, value aDescriptionB)
        {
            // (the operand that is to blame: the second if the first is a number (or a numeral))
            value n;
            bool const firstOk = is_bitwise(aOperation) ? is_number(a) : to_number(a, n);
            auto const culprit = firstOk ? b : a;
            auto const description = firstOk ? aDescriptionB : aDescriptionA;
            if (is_bitwise(aOperation))
            {
                if (is_number(a) && is_number(b))
                    runtime_error("number has no integer representation");
                runtime_error("attempt to perform bitwise operation on a " + type_name(culprit) + " value" + described(description));
            }
            if (is_string(a) || is_string(b))
            {
                // (as the string library's metamethods report it)
                if ((is_string(a) || is_number(a)) && (is_string(b) || is_number(b)))
                    runtime_error(std::string{ "attempt to " } + sOperationNames[aOperation] + " a '" + type_name(a) + "' with a '" + type_name(b) + "'");
            }
            runtime_error("attempt to perform arithmetic on a " + type_name(culprit) + " value" + described(description));
        }
    }

    value arith(int aOperation, value aLeft, value aRight, value aLeftDescription, value aRightDescription)
    {
        auto a = aLeft;
        auto b = aRight;
        if (numeric_operands(aOperation, a, b))
        {
            if (is_bitwise(aOperation))
            {
                std::int64_t ia, ib;
                if (!to_integer(a, ia) || !to_integer(b, ib))
                    runtime_error("number has no integer representation");
                return make_integer(integer_operation(aOperation, ia, ib));
            }
            if (aOperation != OpDiv && aOperation != OpPow && is_integer(a) && is_integer(b))
                return make_integer(integer_operation(aOperation, integer_of(a), integer_of(b)));
            return make_float(float_operation(aOperation, number_of(a), number_of(b)));
        }
        // (a metamethod: the first operand's, else the second's)
        auto const event = sEvents[aOperation];
        auto handler = metamethod(aLeft, event);
        if (is_nil(handler))
            handler = metamethod(aRight, event);
        if (is_nil(handler))
            arith_error(aOperation, aLeft, aRight, aLeftDescription, aRightDescription);
        value const arguments[] = { aLeft, aRight };
        return call1(handler, arguments);
    }

    value unary_minus(value aValue, value aDescription)
    {
        if (is_integer(aValue))
            return make_integer(integer_operation(OpUnm, integer_of(aValue), 0));
        if (is_float(aValue))
            return make_float(-float_of(aValue));
        value n;
        if (to_number(aValue, n))
            return unary_minus(n, Nil);
        auto const handler = metamethod(aValue, NameUnm);
        if (is_nil(handler))
        {
            if (is_string(aValue))
                runtime_error("attempt to unm a 'string' with a 'string'");
            runtime_error("attempt to perform arithmetic on a " + type_name(aValue) + " value" + described(aDescription));
        }
        value const arguments[] = { aValue, aValue };
        return call1(handler, arguments);
    }

    value bitwise_not(value aValue, value aDescription)
    {
        if (is_number(aValue))
        {
            std::int64_t i;
            if (!to_integer(aValue, i))
                runtime_error("number has no integer representation");
            return make_integer(~i);
        }
        auto const handler = metamethod(aValue, NameBNot);
        if (is_nil(handler))
            runtime_error("attempt to perform bitwise operation on a " + type_name(aValue) + " value" + described(aDescription));
        value const arguments[] = { aValue, aValue };
        return call1(handler, arguments);
    }

    value length(value aValue, value aDescription)
    {
        if (is_string(aValue))
            return make_integer(static_cast<std::int64_t>(str(aValue).size()));
        auto const handler = metamethod(aValue, NameLen);
        if (!is_nil(handler))
        {
            value const arguments[] = { aValue };
            return call1(handler, arguments);
        }
        if (is_table(aValue))
            return make_integer(tab(aValue).length());
        runtime_error("attempt to get length of a " + type_name(aValue) + " value" + described(aDescription));
    }

    // --- concatenation

    namespace
    {
        bool concatenable(value aValue)
        {
            return is_string(aValue) || is_number(aValue);
        }

        void append(std::string& aResult, value aValue)
        {
            if (is_string(aValue))
                aResult += str(aValue);
            else
                aResult += number_text(aValue);
        }

        [[noreturn]] void concat_error(value a, value b)
        {
            auto const culprit = concatenable(a) ? b : a;
            runtime_error("attempt to concatenate a " + type_name(culprit) + " value");
        }
    }

    value concat(value aLeft, value aRight)
    {
        if (concatenable(aLeft) && concatenable(aRight))
        {
            if (is_string(aLeft) && str(aLeft).empty() && is_string(aRight))
                return aRight;
            if (is_string(aRight) && str(aRight).empty() && is_string(aLeft))
                return aLeft;
            std::string result;
            append(result, aLeft);
            append(result, aRight);
            return make_string(std::move(result));
        }
        auto handler = metamethod(aLeft, NameConcat);
        if (is_nil(handler))
            handler = metamethod(aRight, NameConcat);
        if (is_nil(handler))
            concat_error(aLeft, aRight);
        value const arguments[] = { aLeft, aRight };
        return call1(handler, arguments);
    }

    value concat(std::span<value const> aValues)
    {
        // (right to left: the strings and numbers at the end are joined at once; a metamethod joins the value before them with them)
        if (aValues.empty())
            return make_string(std::string_view{});
        value_list keep{ aValues };
        auto result = aValues.back();
        std::size_t i = aValues.size() - 1u;
        while (i > 0u)
        {
            if (concatenable(result) && concatenable(aValues[i - 1u]))
            {
                // (the run of strings and numbers that ends with result)
                std::size_t first = i - 1u;
                while (first > 0u && concatenable(aValues[first - 1u]))
                    --first;
                std::string text;
                for (std::size_t k = first; k < i; ++k)
                    append(text, aValues[k]);
                append(text, result);
                result = make_string(std::move(text));
                keep.push_back(result);
                i = first;
            }
            else
            {
                result = concat(aValues[i - 1u], result);
                keep.push_back(result);
                --i;
            }
        }
        if (!is_string(result) && is_number(result) && aValues.size() == 1u)
            result = make_string(number_text(result));
        return result;
    }

    // --- tostring

    std::string raw_tostring(value aValue)
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
            return str(aValue);
        char buffer[64];
        auto* c = cell_at(aValue);
        std::snprintf(buffer, sizeof(buffer), "%s: %p", type_name(aValue).c_str(), static_cast<void*>(c));
        return buffer;
    }

    value tostring(value aValue)
    {
        auto const handler = metamethod(aValue, NameToString);
        if (!is_nil(handler))
        {
            value const arguments[] = { aValue };
            auto const result = call1(handler, arguments);
            if (!is_string(result))
            {
                if (is_number(result))
                    return make_string(number_text(result));
                runtime_error("'__tostring' must return a string");
            }
            return result;
        }
        if (is_string(aValue))
            return aValue;
        auto const mt = metatable_of(aValue);
        if (is_table(mt))
        {
            auto const n = tab(mt).get_string(name(NameName));
            if (is_string(n))
            {
                char buffer[64];
                std::snprintf(buffer, sizeof(buffer), ": %p", static_cast<void*>(cell_at(aValue)));
                return make_string(str(n) + buffer);
            }
        }
        return make_string(raw_tostring(aValue));
    }

    std::string tostring_text(value aValue)
    {
        auto const s = tostring(aValue);
        return str(s);
    }
}
