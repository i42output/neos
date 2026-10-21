/*
  operations.cpp

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

// ECMAScript's abstract operations: conversions, comparisons, object operations and iterators

#include <algorithm>
#include <charconv>
#include <neos/language/ecmascript.hpp>
#include "js.hpp"
#include "api.hpp"

namespace neos::javascript_runtime
{
    namespace es = neos::ecmascript;

    // --- type conversion

    bool to_boolean(value aValue)
    {
        if (is_number(aValue))
        {
            auto const n = as_number(aValue);
            return !(n == 0.0 || std::isnan(n));
        }
        switch (tag_of(aValue))
        {
        case TagSpecial: return aValue == True;
        case TagString: return string_length(aValue) != 0u;
        case TagBigInt: return !bigint_is_zero(aValue);
        case TagObject: return obj(aValue).cls != object_class::HTMLDDA;
        default: return true;
        }
    }

    value to_primitive(value aValue, hint aHint)
    {
        if (!is_object(aValue))
            return aValue;
        auto const& r = the_realm();
        auto const exotic = get_method(aValue, r.symToPrimitive);
        if (exotic != Undefined)
        {
            value const hintString[] = { ascii(aHint == hint::Default ? "default" : aHint == hint::Number ? "number" : "string") };
            auto const result = call(exotic, aValue, hintString);
            if (is_object(result))
                throw_type_error("Cannot convert object to primitive value");
            return result;
        }
        // (OrdinaryToPrimitive)
        value const first = aHint == hint::String ? the_names().toString : the_names().valueOf;
        value const second = aHint == hint::String ? the_names().valueOf : the_names().toString;
        for (auto const name : { first, second })
        {
            auto const method = get(aValue, name);
            if (is_callable(method))
            {
                auto const result = call(method, aValue);
                if (!is_object(result))
                    return result;
            }
        }
        throw_type_error("Cannot convert object to primitive value");
    }

    double string_to_number(std::u16string_view aText)
    {
        // (StringToNumber: StrWhiteSpace is trimmed; no numeric separators; hexadecimal, octal and binary integers are unsigned)
        auto const text = es::trim(aText);
        if (text.empty())
            return 0.0;
        for (auto c : text)
            if (c >= 0x80u)
                return std::numeric_limits<double>::quiet_NaN();
        std::string ascii{ text.begin(), text.end() };
        if (ascii.size() > 2u && ascii[0] == '0')
        {
            int radix = 0;
            switch (ascii[1])
            {
            case 'x': case 'X': radix = 16; break;
            case 'o': case 'O': radix = 8; break;
            case 'b': case 'B': radix = 2; break;
            default: break;
            }
            if (radix != 0)
            {
                double result = 0.0;
                for (std::size_t i = 2u; i < ascii.size(); ++i)
                {
                    int const digit = es::detail::digit_value(static_cast<char16_t>(ascii[i]));
                    if (digit >= radix)
                        return std::numeric_limits<double>::quiet_NaN();
                    result = result * radix + digit;
                }
                if (radix == 16 && result >= 9007199254740992.0)
                    return std::strtod(("0x" + ascii.substr(2u)).c_str(), nullptr);  // (correctly rounded)
                return result;
            }
        }
        auto const length = es::detail::decimal_literal(ascii);
        if (length != ascii.size())
            return std::numeric_limits<double>::quiet_NaN();
        return es::detail::decimal_value(ascii);
    }

    double to_number(value aValue)
    {
        if (is_number(aValue))
            return as_number(aValue);
        switch (tag_of(aValue))
        {
        case TagSpecial:
            if (aValue == Undefined)
                return std::numeric_limits<double>::quiet_NaN();
            return aValue == True ? 1.0 : 0.0;
        case TagString:
            return string_to_number(str(aValue));
        case TagSymbol:
            throw_type_error("Cannot convert a Symbol value to a number");
        case TagBigInt:
            throw_type_error("Cannot convert a BigInt value to a number");
        default:
            return to_number(to_primitive(aValue, hint::Number));
        }
    }

    value to_numeric(value aValue)
    {
        if (is_number(aValue))
            return aValue;
        auto const primitive = to_primitive(aValue, hint::Number);
        if (is_bigint(primitive))
            return primitive;
        return number(to_number(primitive));
    }

    std::u16string number_to_u16string(double aValue, int aRadix)
    {
        return utf16_ascii(es::number_to_string(aValue, aRadix));
    }

    std::size_t number_characters(double aValue, char16_t (&aBuffer)[32])
    {
        // (a safe integer's digits directly (the common case); else Number::toString's)
        if (aValue > -9007199254740992.0 && aValue < 9007199254740992.0)
        {
            auto const integer = static_cast<std::int64_t>(aValue);
            if (static_cast<double>(integer) == aValue)
            {
                // (-0 is "0"; two digits at a time)
                static constexpr char sPairs[] =
                    "0001020304050607080910111213141516171819202122232425262728293031323334353637383940414243444546474849"
                    "5051525354555657585960616263646566676869707172737475767778798081828384858687888990919293949596979899";
                std::uint64_t magnitude = integer < 0 ? static_cast<std::uint64_t>(-integer) : static_cast<std::uint64_t>(integer);
                char16_t reversed[20];
                std::size_t count = 0u;
                while (magnitude >= 100u)
                {
                    auto const pair = static_cast<std::size_t>(magnitude % 100u) * 2u;
                    magnitude /= 100u;
                    reversed[count++] = static_cast<char16_t>(sPairs[pair + 1u]);
                    reversed[count++] = static_cast<char16_t>(sPairs[pair]);
                }
                if (magnitude >= 10u)
                {
                    auto const pair = static_cast<std::size_t>(magnitude) * 2u;
                    reversed[count++] = static_cast<char16_t>(sPairs[pair + 1u]);
                    reversed[count++] = static_cast<char16_t>(sPairs[pair]);
                }
                else
                    reversed[count++] = static_cast<char16_t>(u'0' + magnitude);
                std::size_t length = 0u;
                if (integer < 0)
                    aBuffer[length++] = u'-';
                while (count > 0u)
                    aBuffer[length++] = reversed[--count];
                return length;
            }
        }
        auto const text = es::number_to_string(aValue);
        std::copy(text.begin(), text.end(), aBuffer);
        return text.size();
    }

    value number_to_string(double aValue)
    {
        // (small integers are common: their strings are interned)
        if (aValue >= 0.0 && aValue < 1024.0 && std::trunc(aValue) == aValue && !(aValue == 0.0 && std::signbit(aValue)))
            return index_key(static_cast<std::uint64_t>(aValue));
        char16_t buffer[32];
        auto const length = number_characters(aValue, buffer);
        return make_string(std::u16string{ buffer, length });
    }

    value to_string(value aValue)
    {
        if (is_number(aValue))
            return number_to_string(as_number(aValue));
        auto const& n = the_names();
        switch (tag_of(aValue))
        {
        case TagString:
            return aValue;
        case TagSpecial:
            if (aValue == Undefined)
                return n.undefined;
            if (aValue == Null)
                return n.null;
            return aValue == True ? n.true_ : n.false_;
        case TagSymbol:
            throw_type_error("Cannot convert a Symbol value to a string");
        case TagBigInt:
            return make_string(bigint_to_string(aValue));
        default:
            return to_string(to_primitive(aValue, hint::String));
        }
    }

    std::u16string to_u16string(value aValue)
    {
        return str(to_string(aValue));
    }

    value to_object(value aValue)
    {
        auto const& r = the_realm();
        if (is_object(aValue))
            return aValue;
        if (is_nullish(aValue))
            throw_type_error(std::string{ "Cannot convert " } + (aValue == Null ? "null" : "undefined") + " to object");
        object* result = nullptr;
        if (is_number(aValue))
            result = &the_heap().make<object>(r.numberPrototype, object_class::Number);
        else if (is_boolean(aValue))
            result = &the_heap().make<object>(r.booleanPrototype, object_class::Boolean);
        else if (is_symbol(aValue))
            result = &the_heap().make<object>(r.symbolPrototype, object_class::Symbol);
        else if (is_bigint(aValue))
            result = &the_heap().make<object>(r.bigintPrototype, object_class::BigInt);
        else
            return make_string_object(aValue, r.stringPrototype);
        result->internal = aValue;
        return value_of(*result);
    }

    value to_property_key(value aValue)
    {
        if (is_string(aValue))
            return intern(aValue);
        if (is_symbol(aValue))
            return aValue;
        if (is_number(aValue))
        {
            auto const n = as_number(aValue);
            if (n >= 0.0 && n < 4294967295.0 && std::trunc(n) == n && !(n == 0.0 && std::signbit(n)))
                return index_key(static_cast<std::uint64_t>(n));
        }
        auto const key = to_primitive(aValue, hint::String);
        if (is_symbol(key))
            return key;
        return intern(to_string(key));
    }

    double to_integer_or_infinity(value aValue)
    {
        auto const n = to_number(aValue);
        if (std::isnan(n) || n == 0.0)
            return 0.0;
        if (std::isinf(n))
            return n;
        return std::trunc(n) + 0.0;     // (-0.5: +0)
    }

    double to_length(value aValue)
    {
        auto const length = to_integer_or_infinity(aValue);
        if (length <= 0.0)
            return 0.0;
        return std::min(length, 9007199254740991.0);
    }

    double to_index(value aValue)
    {
        if (aValue == Undefined)
            return 0.0;
        auto const integer = to_integer_or_infinity(aValue);
        if (integer < 0.0 || integer > 9007199254740991.0)
            throw_range_error("invalid index");
        return integer;
    }

    std::int32_t to_int32(double aValue)
    {
        return es::to_int32(aValue);
    }

    std::uint32_t to_uint32(double aValue)
    {
        return static_cast<std::uint32_t>(es::to_int32(aValue));
    }

    std::int32_t to_int32(value aValue)
    {
        return to_int32(to_number(aValue));
    }

    std::uint32_t to_uint32(value aValue)
    {
        return to_uint32(to_number(aValue));
    }

    value canonical_numeric_index_string(value aKey)
    {
        if (!is_string(aKey))
            return Undefined;
        auto const& text = str(aKey);
        if (text == u"-0")
            return number(-0.0);
        auto const n = string_to_number(text);
        // (ToString(n) must be the key)
        if (text.empty() || (std::isnan(n) && text != u"NaN"))
            return Undefined;
        if (number_to_u16string(n) != text)
            return Undefined;
        return number(n);
    }

    std::string type_name(value aValue)
    {
        return utf8(type_of(aValue));
    }

    std::string describe(value aValue)
    {
        if (is_string(aValue))
        {
            auto text = utf8(aValue);
            if (text.size() > 40u)
                text = text.substr(0u, 40u) + "...";
            return "\"" + text + "\"";
        }
        if (is_symbol(aValue))
        {
            auto const description = cell_of<symbol>(aValue).description;
            return "Symbol(" + (description == Undefined ? std::string{} : utf8(description)) + ")";
        }
        if (is_object(aValue))
        {
            if (auto const* f = as_function(aValue))
            {
                auto const* name = const_cast<function_object*>(f)->own(the_names().name);
                if (name != nullptr && !name->accessor() && is_string(name->data) && string_length(name->data) != 0u)
                    return "function " + utf8(name->data);
                return "function";
            }
            if (is_array(aValue))
                return "[object Array]";
            return "[object Object]";
        }
        if (is_bigint(aValue))
            return utf8(make_string(bigint_to_string(aValue))) + "n";
        return utf8(to_string(aValue));
    }

    // --- comparison

    bool same_value(value aLhs, value aRhs)
    {
        if (is_number(aLhs) && is_number(aRhs))
        {
            auto const l = as_number(aLhs);
            auto const r = as_number(aRhs);
            if (std::isnan(l) && std::isnan(r))
                return true;
            return aLhs == aRhs;
        }
        if (is_string(aLhs) && is_string(aRhs))
            return string_equal(aLhs, aRhs);
        if (is_bigint(aLhs) && is_bigint(aRhs))
            return bigint_equal(aLhs, aRhs);
        return aLhs == aRhs;
    }

    bool same_value_zero(value aLhs, value aRhs)
    {
        if (is_number(aLhs) && is_number(aRhs))
        {
            auto const l = as_number(aLhs);
            auto const r = as_number(aRhs);
            if (std::isnan(l) && std::isnan(r))
                return true;
            return l == r;
        }
        return same_value(aLhs, aRhs);
    }

    bool strictly_equal(value aLhs, value aRhs)
    {
        if (is_number(aLhs) && is_number(aRhs))
            return as_number(aLhs) == as_number(aRhs);
        if (is_string(aLhs) && is_string(aRhs))
            return string_equal(aLhs, aRhs);
        if (is_bigint(aLhs) && is_bigint(aRhs))
            return bigint_equal(aLhs, aRhs);
        return aLhs == aRhs;
    }

    bool loosely_equal(value aLhs, value aRhs)
    {
        for (;;)
        {
            bool const ln = is_number(aLhs);
            bool const rn = is_number(aRhs);
            if (ln && rn)
                return as_number(aLhs) == as_number(aRhs);
            if ((!ln && !rn && tag_of(aLhs) == tag_of(aRhs) && tag_of(aLhs) != TagSpecial) ||
                (is_boolean(aLhs) && is_boolean(aRhs)) || (is_nullish(aLhs) && aLhs == aRhs))
                return strictly_equal(aLhs, aRhs);
            if (is_nullish(aLhs) && is_nullish(aRhs))
                return true;
            // (Annex B.3.6.2: an object with an [[IsHTMLDDA]] slot is loosely equal to null and undefined)
            if ((is_nullish(aLhs) && is_object(aRhs) && obj(aRhs).cls == object_class::HTMLDDA) ||
                (is_nullish(aRhs) && is_object(aLhs) && obj(aLhs).cls == object_class::HTMLDDA))
                return true;
            if (is_nullish(aLhs) || is_nullish(aRhs) || aLhs == Empty || aRhs == Empty)
                return false;
            if (ln && is_string(aRhs))
                return as_number(aLhs) == to_number(aRhs);
            if (is_string(aLhs) && rn)
                return to_number(aLhs) == as_number(aRhs);
            if (is_bigint(aLhs) && is_string(aRhs))
            {
                auto const n = string_to_bigint(str(aRhs));
                if (!n)
                    return false;
                aRhs = *n;
                continue;
            }
            if (is_string(aLhs) && is_bigint(aRhs))
            {
                std::swap(aLhs, aRhs);
                continue;
            }
            if (is_boolean(aLhs))
            {
                aLhs = number(aLhs == True ? 1.0 : 0.0);
                continue;
            }
            if (is_boolean(aRhs))
            {
                aRhs = number(aRhs == True ? 1.0 : 0.0);
                continue;
            }
            if (!is_object(aLhs) && is_object(aRhs))
            {
                aRhs = to_primitive(aRhs);
                continue;
            }
            if (is_object(aLhs) && !is_object(aRhs))
            {
                aLhs = to_primitive(aLhs);
                continue;
            }
            if (is_bigint(aLhs) && rn)
                return bigint_compare_number(aLhs, as_number(aRhs)) == 0;
            if (ln && is_bigint(aRhs))
                return bigint_compare_number(aRhs, as_number(aLhs)) == 0;
            return false;
        }
    }

    value type_of(value aValue)
    {
        auto const& n = the_names();
        if (is_number(aValue))
            return n.number;
        switch (tag_of(aValue))
        {
        case TagSpecial:
            if (aValue == Undefined || aValue == Empty)
                return n.undefined;
            if (aValue == Null)
                return n.object;
            return n.boolean;
        case TagString: return n.string;
        case TagSymbol: return n.symbol;
        case TagBigInt: return n.bigint;
        case TagObject: return obj(aValue).cls == object_class::HTMLDDA ? n.undefined : obj(aValue).callable() ? n.function : n.object;
        default: return n.undefined;
        }
    }

    // --- testing and comparison

    bool is_callable(value aValue)
    {
        return is_object(aValue) && obj(aValue).callable();
    }

    bool is_constructor(value aValue)
    {
        return is_object(aValue) && obj(aValue).constructor();
    }

    bool is_array(value aValue)
    {
        if (!is_object(aValue))
            return false;
        auto& o = obj(aValue);
        if (o.cls == object_class::Array)
            return true;
        if (o.cls == object_class::Proxy)
            return proxy_is_array(aValue);
        return false;
    }

    bool is_regexp(value aValue)
    {
        if (!is_object(aValue))
            return false;
        auto const matcher = get(aValue, the_realm().symMatch);
        if (matcher != Undefined)
            return to_boolean(matcher);
        return obj(aValue).cls == object_class::RegExp;
    }

    bool is_integral_number(value aValue)
    {
        if (!is_number(aValue))
            return false;
        auto const n = as_number(aValue);
        return std::isfinite(n) && std::trunc(n) == n;
    }

    // --- operations on objects

    value get(value aObject, value aKey)
    {
        return obj(aObject).get(aKey, aObject);
    }

    value get_v(value aValue, value aKey)
    {
        if (is_object(aValue))
            return obj(aValue).get(aKey, aValue);
        auto const& r = the_realm();
        value prototype = Undefined;
        if (is_string(aValue))
        {
            if (aKey == the_names().length)
                return number(string_length(aValue));
            if (auto const index = array_index(aKey))
            {
                if (*index < string_length(aValue))
                    return make_string(str(aValue)[*index]);
            }
            prototype = r.stringPrototype;
        }
        else if (is_number(aValue))
            prototype = r.numberPrototype;
        else if (is_boolean(aValue))
            prototype = r.booleanPrototype;
        else if (is_symbol(aValue))
            prototype = r.symbolPrototype;
        else if (is_bigint(aValue))
            prototype = r.bigintPrototype;
        else
            to_object(aValue);  // (throws)
        return obj(prototype).get(aKey, aValue);
    }

    value get_method(value aValue, value aKey)
    {
        auto const f = get_v(aValue, aKey);
        if (is_nullish(f))
            return Undefined;
        if (!is_callable(f))
            throw_type_error(describe(f) + " is not a function");
        return f;
    }

    void set(value aObject, value aKey, value aValue, bool aThrow)
    {
        if (!obj(aObject).set(aKey, aValue, aObject) && aThrow)
            throw_type_error("Cannot assign to read only property '" + describe(aKey) + "' of object");
    }

    bool create_data_property(value aObject, value aKey, value aValue)
    {
        return obj(aObject).define_own_property(aKey, property_descriptor::data(aValue, AttributesDefault));
    }

    void create_data_property_or_throw(value aObject, value aKey, value aValue)
    {
        if (!create_data_property(aObject, aKey, aValue))
            throw_type_error("Cannot define property " + describe(aKey));
    }

    void define_property_or_throw(value aObject, value aKey, property_descriptor const& aDescriptor)
    {
        if (!obj(aObject).define_own_property(aKey, aDescriptor))
            throw_type_error("Cannot redefine property: " + describe(aKey));
    }

    void delete_property_or_throw(value aObject, value aKey)
    {
        if (!obj(aObject).delete_property(aKey))
            throw_type_error("Cannot delete property " + describe(aKey));
    }

    bool has_property(value aObject, value aKey)
    {
        return obj(aObject).has_property(aKey);
    }

    bool has_own_property(value aObject, value aKey)
    {
        return obj(aObject).get_own_property(aKey).has_value();
    }

    value call(value aFunction, value aThis, arguments aArguments)
    {
        if (!is_callable(aFunction))
            throw_type_error(describe(aFunction) + " is not a function");
        return obj(aFunction).call(aThis, aArguments);
    }

    value construct(value aConstructor, arguments aArguments, std::optional<value> aNewTarget)
    {
        if (!is_constructor(aConstructor))
            throw_type_error(describe(aConstructor) + " is not a constructor");
        return obj(aConstructor).construct(aArguments, aNewTarget.value_or(aConstructor));
    }

    value invoke(value aValue, value aKey, arguments aArguments)
    {
        auto const f = get_v(aValue, aKey);
        return call(f, aValue, aArguments);
    }

    bool set_integrity_level(value aObject, bool aFrozen)
    {
        auto& o = obj(aObject);
        if (!o.prevent_extensions())
            return false;
        auto const keys = o.own_property_keys();
        for (auto const key : keys)
        {
            if (!aFrozen)
            {
                property_descriptor d;
                d.configurable = false;
                define_property_or_throw(aObject, key, d);
            }
            else if (auto const current = o.get_own_property(key))
            {
                property_descriptor d;
                d.configurable = false;
                if (!current->is_accessor())
                    d.writable = false;
                define_property_or_throw(aObject, key, d);
            }
        }
        return true;
    }

    bool test_integrity_level(value aObject, bool aFrozen)
    {
        auto& o = obj(aObject);
        if (o.is_extensible())
            return false;
        for (auto const key : o.own_property_keys())
            if (auto const current = o.get_own_property(key))
            {
                if (current->configurable.value_or(false))
                    return false;
                if (aFrozen && current->is_data() && current->writable.value_or(false))
                    return false;
            }
        return true;
    }

    value create_array_from_list(std::span<value const> aElements)
    {
        return make_array(value_list{ aElements.begin(), aElements.end() });
    }

    double length_of_array_like(value aObject)
    {
        if (auto const* a = as_array(aObject))
            return a->length;
        return to_length(get(aObject, the_names().length));
    }

    value_list create_list_from_array_like(value aObject, bool aKeysOnly)
    {
        if (!is_object(aObject))
            throw_type_error("CreateListFromArrayLike called on non-object");
        auto const length = length_of_array_like(aObject);
        value_list result;
        for (double i = 0.0; i < length; ++i)
        {
            auto const next = get(aObject, index_key(static_cast<std::uint64_t>(i)));
            if (aKeysOnly && !is_property_key(next))
                throw_type_error("not a property key");
            result.push_back(next);
        }
        return result;
    }

    bool ordinary_has_instance(value aConstructor, value aObject)
    {
        if (!is_callable(aConstructor))
            return false;
        if (auto const* f = as_function(aConstructor); f != nullptr && f->kind == function_kind::Bound)
            return instance_of(aObject, f->boundTarget);
        if (!is_object(aObject))
            return false;
        auto const prototype = get(aConstructor, the_names().prototype);
        if (!is_object(prototype))
            throw_type_error("Function has non-object prototype in instanceof check");
        for (auto o = aObject;;)
        {
            o = obj(o).get_prototype_of();
            if (o == Null)
                return false;
            if (o == prototype)
                return true;
        }
    }

    bool instance_of(value aValue, value aTarget)
    {
        if (!is_object(aTarget))
            throw_type_error("Right-hand side of 'instanceof' is not an object");
        auto const handler = get_method(aTarget, the_realm().symHasInstance);
        if (handler != Undefined)
        {
            value const argument[] = { aValue };
            return to_boolean(call(handler, aTarget, argument));
        }
        if (!is_callable(aTarget))
            throw_type_error("Right-hand side of 'instanceof' is not callable");
        return ordinary_has_instance(aTarget, aValue);
    }

    value species_constructor(value aObject, value aDefault)
    {
        auto const c = get(aObject, the_names().constructor);
        if (c == Undefined)
            return aDefault;
        if (!is_object(c))
            throw_type_error("object.constructor is not an object");
        auto const s = get(c, the_realm().symSpecies);
        if (is_nullish(s))
            return aDefault;
        if (is_constructor(s))
            return s;
        throw_type_error("object.constructor[Symbol.species] is not a constructor");
    }

    value get_prototype_from_constructor(value aConstructor, value realm::*aDefault)
    {
        auto const prototype = get(aConstructor, the_names().prototype);
        if (!is_object(prototype))
            return function_realm(aConstructor).*aDefault;     // (GetFunctionRealm: a revoked proxy throws)
        return prototype;
    }

    value ordinary_create_from_constructor(value aConstructor, value realm::*aDefault, object_class aClass)
    {
        auto const prototype = get_prototype_from_constructor(aConstructor, aDefault);
        return value_of(the_heap().make<object>(prototype, aClass));
    }

    value_list enumerable_own_properties(value aObject, int aKind)
    {
        auto& o = obj(aObject);
        value_list result;
        for (auto const key : o.own_property_keys())
        {
            if (!is_string(key))
                continue;
            auto const desc = o.get_own_property(key);
            if (!desc || !desc->enumerable.value_or(false))
                continue;
            if (aKind == 0)
                result.push_back(key);
            else
            {
                auto const v = get(aObject, key);
                if (aKind == 1)
                    result.push_back(v);
                else
                {
                    value const entry[] = { key, v };
                    result.push_back(create_array_from_list(entry));
                }
            }
        }
        return result;
    }

    void copy_data_properties(value aTarget, value aSource, std::span<value const> aExcluded)
    {
        if (is_nullish(aSource))
            return;
        auto const from = to_object(aSource);
        auto& f = obj(from);
        for (auto const key : f.own_property_keys())
        {
            bool excluded = false;
            for (auto const e : aExcluded)
                if (same_value(e, key))
                {
                    excluded = true;
                    break;
                }
            if (excluded)
                continue;
            auto const desc = f.get_own_property(key);
            if (desc && desc->enumerable.value_or(false))
                create_data_property_or_throw(aTarget, key, get(from, key));
        }
    }

    property_descriptor to_property_descriptor(value aObject)
    {
        if (!is_object(aObject))
            throw_type_error("Property description must be an object");
        auto const& n = the_names();
        property_descriptor d;
        if (has_property(aObject, n.enumerable))
            d.enumerable = to_boolean(get(aObject, n.enumerable));
        if (has_property(aObject, n.configurable))
            d.configurable = to_boolean(get(aObject, n.configurable));
        if (has_property(aObject, n.value_))
            d.valueField = get(aObject, n.value_);
        if (has_property(aObject, n.writable))
            d.writable = to_boolean(get(aObject, n.writable));
        if (has_property(aObject, n.get))
        {
            auto const g = get(aObject, n.get);
            if (!is_callable(g) && g != Undefined)
                throw_type_error("Getter must be a function: " + describe(g));
            d.getter = g;
        }
        if (has_property(aObject, n.set))
        {
            auto const s = get(aObject, n.set);
            if (!is_callable(s) && s != Undefined)
                throw_type_error("Setter must be a function: " + describe(s));
            d.setter = s;
        }
        if ((d.getter || d.setter) && (d.valueField || d.writable))
            throw_type_error("Invalid property descriptor. Cannot both specify accessors and a value or writable attribute");
        return d;
    }

    value from_property_descriptor(std::optional<property_descriptor> const& aDescriptor)
    {
        if (!aDescriptor)
            return Undefined;
        auto const& n = the_names();
        auto const result = make_object();
        auto& o = obj(result);
        if (aDescriptor->valueField)
            o.put(n.value_, *aDescriptor->valueField, AttributesDefault);
        if (aDescriptor->writable)
            o.put(n.writable, boolean(*aDescriptor->writable), AttributesDefault);
        if (aDescriptor->getter)
            o.put(n.get, *aDescriptor->getter, AttributesDefault);
        if (aDescriptor->setter)
            o.put(n.set, *aDescriptor->setter, AttributesDefault);
        if (aDescriptor->enumerable)
            o.put(n.enumerable, boolean(*aDescriptor->enumerable), AttributesDefault);
        if (aDescriptor->configurable)
            o.put(n.configurable, boolean(*aDescriptor->configurable), AttributesDefault);
        return result;
    }

    void complete_property_descriptor(property_descriptor& aDescriptor)
    {
        if (aDescriptor.is_generic() || aDescriptor.is_data())
        {
            if (!aDescriptor.valueField)
                aDescriptor.valueField = Undefined;
            if (!aDescriptor.writable)
                aDescriptor.writable = false;
        }
        else
        {
            if (!aDescriptor.getter)
                aDescriptor.getter = Undefined;
            if (!aDescriptor.setter)
                aDescriptor.setter = Undefined;
        }
        if (!aDescriptor.enumerable)
            aDescriptor.enumerable = false;
        if (!aDescriptor.configurable)
            aDescriptor.configurable = false;
    }

    // --- iterators

    iterator_record get_iterator_from_method(value aValue, value aMethod)
    {
        auto const iterator = call(aMethod, aValue);
        if (!is_object(iterator))
            throw_type_error("Result of the Symbol.iterator method is not an object");
        return iterator_record{ iterator, get(iterator, the_names().next), false };
    }

    iterator_record get_iterator(value aValue, bool aAsync)
    {
        auto const& r = the_realm();
        if (aAsync)
        {
            auto const method = get_method(aValue, r.symAsyncIterator);
            if (method == Undefined)
            {
                auto const syncMethod = get_method(aValue, r.symIterator);
                if (syncMethod == Undefined)
                    throw_type_error(describe(aValue) + " is not async iterable");
                auto const syncRecord = get_iterator_from_method(aValue, syncMethod);
                auto record = create_async_from_sync_iterator(syncRecord);
                record.async = true;
                return record;
            }
            auto record = get_iterator_from_method(aValue, method);
            record.async = true;
            return record;
        }
        auto const method = get_method(aValue, r.symIterator);
        if (method == Undefined)
            throw_type_error(describe(aValue) + " is not iterable");
        return get_iterator_from_method(aValue, method);
    }

    iterator_record get_iterator_direct(value aObject)
    {
        return iterator_record{ aObject, get(aObject, the_names().next), false };
    }

    value iterator_next(iterator_record& aRecord, std::optional<value> aValue)
    {
        value result;
        if (aValue)
        {
            value const argument[] = { *aValue };
            result = call(aRecord.nextMethod, aRecord.iterator, argument);
        }
        else
            result = call(aRecord.nextMethod, aRecord.iterator);
        if (aRecord.async)
            result = await_value(result);
        if (!is_object(result))
            throw_type_error("Iterator result " + describe(result) + " is not an object");
        return result;
    }

    bool iterator_complete(value aResult)
    {
        return to_boolean(get(aResult, the_names().done));
    }

    value iterator_value(value aResult)
    {
        return get(aResult, the_names().value_);
    }

    std::optional<value> iterator_step_value(iterator_record& aRecord)
    {
        value result;
        try
        {
            result = iterator_next(aRecord);
        }
        catch (...)
        {
            aRecord.done = true;
            throw;
        }
        bool done;
        try
        {
            done = iterator_complete(result);
        }
        catch (...)
        {
            aRecord.done = true;
            throw;
        }
        if (done)
        {
            aRecord.done = true;
            return {};
        }
        try
        {
            return iterator_value(result);
        }
        catch (...)
        {
            aRecord.done = true;
            throw;
        }
    }

    void iterator_close(iterator_record const& aRecord)
    {
        auto const returnMethod = get_method(aRecord.iterator, the_names().return_);
        if (returnMethod == Undefined)
            return;
        auto result = call(returnMethod, aRecord.iterator);
        if (aRecord.async)
            result = await_value(result);
        if (!is_object(result))
            throw_type_error("iterator.return() did not return an object");
    }

    void iterator_close_on_throw(iterator_record const& aRecord)
    {
        try
        {
            auto const returnMethod = get_method(aRecord.iterator, the_names().return_);
            if (returnMethod != Undefined)
            {
                auto const result = call(returnMethod, aRecord.iterator);
                if (aRecord.async)
                    await_value(result);
            }
        }
        catch (js_exception const&)
        {
        }
    }

    value create_iter_result_object(value aValue, bool aDone)
    {
        auto const result = make_object();
        auto& o = obj(result);
        o.properties.add(the_names().value_, aValue, Undefined, AttributesDefault);
        o.properties.add(the_names().done, boolean(aDone), Undefined, AttributesDefault);
        return result;
    }

    value_list iterate_to_list(value aIterable)
    {
        auto record = get_iterator(aIterable);
        value_list result;
        while (auto const next = iterator_step_value(record))
            result.push_back(*next);
        return result;
    }
}
