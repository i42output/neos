/*
  operators.cpp

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

// Operators (of any values), PutValue, and the iterators that compiled code uses (iterator records and for-in)

#include <set>
#include <neos/language/ecmascript.hpp>
#include "js.hpp"
#include "api.hpp"
#include "gc.hpp"

namespace neos::javascript_runtime
{
    namespace es = neos::ecmascript;

    namespace
    {
        [[noreturn]] void mixed_bigint()
        {
            throw_type_error("Cannot mix BigInt and other types, use explicit conversions");
        }

        // IsLessThan: true, false or undefined (NaN)
        value is_less_than(value aX, value aY, bool aLeftFirst)
        {
            value px, py;
            if (aLeftFirst)
            {
                px = to_primitive(aX, hint::Number);
                py = to_primitive(aY, hint::Number);
            }
            else
            {
                py = to_primitive(aY, hint::Number);
                px = to_primitive(aX, hint::Number);
            }
            if (is_string(px) && is_string(py))
                return boolean(str(px) < str(py));
            if (is_bigint(px) && is_string(py))
            {
                auto const ny = string_to_bigint(str(py));
                if (!ny)
                    return Undefined;
                return boolean(bigint_compare(px, *ny) < 0);
            }
            if (is_string(px) && is_bigint(py))
            {
                auto const nx = string_to_bigint(str(px));
                if (!nx)
                    return Undefined;
                return boolean(bigint_compare(*nx, py) < 0);
            }
            auto const nx = to_numeric(px);
            auto const ny = to_numeric(py);
            if (is_number(nx) && is_number(ny))
            {
                auto const a = as_number(nx);
                auto const b = as_number(ny);
                if (std::isnan(a) || std::isnan(b))
                    return Undefined;
                return boolean(a < b);
            }
            if (is_bigint(nx) && is_bigint(ny))
                return boolean(bigint_compare(nx, ny) < 0);
            if (is_bigint(nx))
            {
                auto const c = bigint_compare_number(nx, as_number(ny));
                return c == 2 ? Undefined : boolean(c < 0);
            }
            auto const c = bigint_compare_number(ny, as_number(nx));
            return c == 2 ? Undefined : boolean(c > 0);
        }

        value number_operation(int aOperation, double a, double b)
        {
            switch (aOperation)
            {
            case 0: return number(a + b);
            case 1: return number(a - b);
            case 2: return number(a * b);
            case 3: return number(a / b);
            case 4: return number(std::fmod(a, b));
            case 5: return number(es::pow(a, b));
            case 6: return number(static_cast<double>(static_cast<std::int32_t>(to_uint32(a) << (to_uint32(b) & 31u))));
            case 7: return number(static_cast<double>(to_int32(a) >> (to_uint32(b) & 31u)));
            case 8: return number(static_cast<double>(to_uint32(a) >> (to_uint32(b) & 31u)));
            case 9: return number(static_cast<double>(to_int32(a) & to_int32(b)));
            case 10: return number(static_cast<double>(to_int32(a) | to_int32(b)));
            case 11: return number(static_cast<double>(to_int32(a) ^ to_int32(b)));
            default: return Undefined;
            }
        }

        value bigint_operation(int aOperation, value a, value b)
        {
            switch (aOperation)
            {
            case 0: return bigint_add(a, b);
            case 1: return bigint_subtract(a, b);
            case 2: return bigint_multiply(a, b);
            case 3: return bigint_divide(a, b);
            case 4: return bigint_remainder(a, b);
            case 5: return bigint_power(a, b);
            case 6: return bigint_shift_left(a, b);
            case 7: return bigint_shift_left(a, bigint_negate(b));
            case 8: throw_type_error("BigInts have no unsigned right shift, use >> instead");
            case 9: return bigint_bitwise(0, a, b);
            case 10: return bigint_bitwise(1, a, b);
            case 11: return bigint_bitwise(2, a, b);
            default: return Undefined;
            }
        }
    }

    value binary_operation(int aOperation, value aLeft, value aRight)
    {
        switch (aOperation)
        {
        case 0:
            {
                if (is_number(aLeft) && is_number(aRight))
                    return number(as_number(aLeft) + as_number(aRight));
                if (is_string(aLeft) && is_string(aRight))
                    return concat(aLeft, aRight);
                if (is_string(aLeft) && is_number(aRight))
                    return concat_number(aLeft, as_number(aRight), false);
                if (is_number(aLeft) && is_string(aRight))
                    return concat_number(aRight, as_number(aLeft), true);
                auto const l = to_primitive(aLeft);
                auto const r = to_primitive(aRight);
                if (is_string(l) || is_string(r))
                    return concat(to_string(l), to_string(r));
                auto const ln = to_numeric(l);
                auto const rn = to_numeric(r);
                if (is_number(ln) && is_number(rn))
                    return number(as_number(ln) + as_number(rn));
                if (is_bigint(ln) && is_bigint(rn))
                    return bigint_add(ln, rn);
                mixed_bigint();
            }
        case 1: case 2: case 3: case 4: case 5: case 6: case 7: case 8: case 9: case 10: case 11:
            {
                auto const ln = to_numeric(aLeft);
                auto const rn = to_numeric(aRight);
                if (is_number(ln) && is_number(rn))
                    return number_operation(aOperation, as_number(ln), as_number(rn));
                if (is_bigint(ln) && is_bigint(rn))
                    return bigint_operation(aOperation, ln, rn);
                mixed_bigint();
            }
        case 12:
            {
                auto const r = is_less_than(aLeft, aRight, true);
                return r == True ? True : False;
            }
        case 13:
            {
                auto const r = is_less_than(aRight, aLeft, false);
                return r == True ? True : False;
            }
        case 14:
            {
                auto const r = is_less_than(aRight, aLeft, false);
                return (r == True || r == Undefined) ? False : True;
            }
        case 15:
            {
                auto const r = is_less_than(aLeft, aRight, true);
                return (r == True || r == Undefined) ? False : True;
            }
        case 16: return boolean(loosely_equal(aLeft, aRight));
        case 17: return boolean(!loosely_equal(aLeft, aRight));
        case 18: return boolean(strictly_equal(aLeft, aRight));
        case 19: return boolean(!strictly_equal(aLeft, aRight));
        case 20: return boolean(instance_of(aLeft, aRight));
        case 21:
            if (!is_object(aRight))
                throw_type_error("Cannot use 'in' operator to search for '" + (is_symbol(aLeft) ? describe(aLeft) : utf8(to_string(aLeft))) + "' in " + describe(aRight));
            return boolean(has_property(aRight, to_property_key(aLeft)));
        default:
            return Undefined;
        }
    }

    value unary_operation(int aOperation, value aValue)
    {
        switch (aOperation)
        {
        case 0:
            {
                auto const n = to_numeric(aValue);
                if (is_bigint(n))
                    return bigint_negate(n);
                return number(-as_number(n));
            }
        case 1:
            {
                auto const n = to_numeric(aValue);
                if (is_bigint(n))
                    return bigint_not(n);
                return number(static_cast<double>(~to_int32(as_number(n))));
            }
        case 2:
            if (is_bigint(aValue))
                return bigint_add(aValue, make_bigint(1));
            return number(as_number(aValue) + 1.0);
        case 3:
            if (is_bigint(aValue))
                return bigint_subtract(aValue, make_bigint(1));
            return number(as_number(aValue) - 1.0);
        default:
            return Undefined;
        }
    }

    void put_value(value aBase, value aKey, value aValue, bool aStrict)
    {
        bool ok;
        if (is_object(aBase))
            ok = obj(aBase).set(aKey, aValue, aBase);
        else
        {
            // (a primitive: its object's [[Set]] with the primitive as the receiver (without making the object))
            auto const& r = the_realm();
            value prototype = Undefined;
            if (is_string(aBase))
            {
                if (aKey == the_names().length)
                    ok = false;
                else if (auto const index = array_index(aKey); index && *index < string_length(aBase))
                    ok = false;
                else
                    ok = obj(r.stringPrototype).set(aKey, aValue, aBase);
                if (!ok && aStrict)
                    throw_type_error("Cannot assign to read only property '" + describe_key(aKey) + "' of string '" + utf8(aBase) + "'");
                return;
            }
            if (is_number(aBase))
                prototype = r.numberPrototype;
            else if (is_boolean(aBase))
                prototype = r.booleanPrototype;
            else if (is_symbol(aBase))
                prototype = r.symbolPrototype;
            else
                prototype = r.bigintPrototype;
            ok = obj(prototype).set(aKey, aValue, aBase);
        }
        if (!ok && aStrict)
            throw_type_error("Cannot assign to read only property '" + describe_key(aKey) + "' of " + (is_object(aBase) ? std::string{ "object" } : describe(aBase)));
    }

    std::string describe_key(value aKey)
    {
        if (is_symbol(aKey))
            return describe(aKey);
        if (is_string(aKey))
            return utf8(aKey);
        try
        {
            return utf8(to_string(aKey));
        }
        catch (...)
        {
            return "?";
        }
    }

    // --- iterator records (as values)

    namespace
    {
        class iterator_record_cell : public cell
        {
        public:
            explicit iterator_record_cell(iterator_record const& aRecord) : cell{ cell_type::Internal }, record{ aRecord } {}
        public:
            void trace(tracer& aTracer) override
            {
                aTracer.mark(record);
            }
        public:
            iterator_record record;
        };

        class for_in_state : public cell
        {
        public:
            for_in_state() : cell{ cell_type::Internal } {}
        public:
            void trace(tracer& aTracer) override
            {
                aTracer.mark(current);
                aTracer.mark(keys);
                for (auto const v : visited)
                    aTracer.mark(v);
            }
        public:
            value current = Null;
            std::vector<value> keys;
            std::size_t next = 0u;
            std::set<value> visited;
        };
    }

    value make_iterator_record(iterator_record const& aRecord)
    {
        return TagInternal | the_heap().make<iterator_record_cell>(aRecord).handle;
    }

    iterator_record& iterator_record_of(value aRecord)
    {
        return cell_of<iterator_record_cell>(aRecord).record;
    }

    value make_for_in_iterator(value aObject)
    {
        auto& state = the_heap().make<for_in_state>();
        if (!is_nullish(aObject))
        {
            state.current = to_object(aObject);
            auto const keys = obj(state.current).own_property_keys();
            state.keys.assign(keys.begin(), keys.end());
        }
        return TagInternal | state.handle;
    }

    value for_in_step(value aIterator)
    {
        // (EnumerateObjectProperties: own enumerable string keys, then the prototype's that are not shadowed; a key deleted before
        // it is visited is not visited)
        auto& state = cell_of<for_in_state>(aIterator);
        while (state.current != Null)
        {
            while (state.next < state.keys.size())
            {
                auto const key = state.keys[state.next++];
                if (!is_string(key))
                    continue;
                if (!state.visited.insert(key).second)
                    continue;
                auto const desc = obj(state.current).get_own_property(key);
                if (!desc || !desc->enumerable.value_or(false))
                    continue;
                return key;
            }
            state.current = obj(state.current).get_prototype_of();
            state.keys.clear();
            state.next = 0u;
            if (state.current != Null)
            {
                auto const keys = obj(state.current).own_property_keys();
                state.keys.assign(keys.begin(), keys.end());
            }
        }
        return Empty;
    }
}
