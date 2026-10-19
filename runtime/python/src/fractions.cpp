/*
  fractions.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/fractions.py), Copyright (c) 2001-2024 Python Software
  Foundation; All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from Python to C++ for the neos Python runtime plugin and adapted to
  its object model (see runtime/python/NOTICE).
*/

// The module fractions (CPython 3.13's Fraction: exact rational arithmetic with Python's ints, its hash, formatting and messages)

#include <cctype>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>
#include "objects.hpp"

namespace neos::python_runtime
{
    namespace detail
    {
        namespace
        {
            object* borrowed(object* aObject) { retain(aObject); return aObject; }

            object* call_with(object* aCallable, std::vector<object*> aArgs, object* aKwargs = nullptr)    // (steals the arguments)
            {
                ref const args{ make_tuple(std::move(aArgs)) };
                return call(aCallable, args.get(), aKwargs);
            }

            // --- Python int arithmetic (the values may be big)

            ref op(int aOperation, object* a, object* b) { return ref{ binary(aOperation, a, b, false) }; }
            ref int_of(long long aValue) { return ref{ make_int(aValue) }; }
            bool is_true(ref const& aValue) { return aValue && truth(aValue.get()) == 1; }
            bool less(object* a, object* b) { ref const r{ compare(NEOS_PY_LT, a, b) }; return is_true(r); }
            bool equals(object* a, object* b) { ref const r{ compare(NEOS_PY_EQ, a, b) }; return is_true(r); }
            bool is_zero(object* a) { ref const zero{ make_int(0) }; return equals(a, zero.get()); }
            bool is_negative(object* a) { ref const zero{ make_int(0) }; return less(a, zero.get()); }
            ref absolute(object* a) { return ref{ unary(NEOS_PY_ABSOLUTE, a) }; }

            ref gcd(object* a, object* b)
            {
                ref x{ absolute(a) };
                ref y{ absolute(b) };
                while (y && !is_zero(y.get()))
                {
                    ref r{ op(NEOS_PY_REMAINDER, x.get(), y.get()) };
                    x = y;
                    y = r;
                }
                return x;
            }

            // --- Fraction objects

            struct fraction_object
            {
                header h;
                object* numerator;
                object* denominator;
                ~fraction_object() { release(numerator); release(denominator); }
            };

            neos_py_type make_fraction_type();
            neos_py_type sFractionType = make_fraction_type();
            fraction_object& as_fraction(object* aObject) { return *reinterpret_cast<fraction_object*>(aObject); }
            bool is_fraction(object* aObject) { return aObject != nullptr && aObject->type == &sFractionType.base; }
            bool is_integer(object* aObject) { return kind_of(builtin_value(aObject)) == NEOS_PY_INT || kind_of(aObject) == NEOS_PY_BOOL; }

            // (a normalized fraction: n/d with gcd 1 and d > 0)
            object* make_fraction(ref aNumerator, ref aDenominator, bool aNormalize = true)
            {
                if (!aNumerator || !aDenominator)
                    return nullptr;
                if (aNormalize)
                {
                    if (is_zero(aDenominator.get()))
                    {
                        std::string text;
                        repr_of(aNumerator.get(), text);
                        return error("ZeroDivisionError", "Fraction(" + text + ", 0)");
                    }
                    ref g{ gcd(aNumerator.get(), aDenominator.get()) };
                    if (is_negative(aDenominator.get()))
                        g = ref{ unary(NEOS_PY_NEGATIVE, g.get()) };
                    aNumerator = op(NEOS_PY_FLOOR_DIVIDE, aNumerator.get(), g.get());
                    aDenominator = op(NEOS_PY_FLOOR_DIVIDE, aDenominator.get(), g.get());
                    if (!aNumerator || !aDenominator)
                        return nullptr;
                }
                // (bool and int subclasses: their ints)
                if (kind_of(aNumerator.get()) != NEOS_PY_INT)
                    aNumerator = ref{ call_with(fn_builtin("int"), { aNumerator.release_ownership() }) };
                if (kind_of(aDenominator.get()) != NEOS_PY_INT)
                    aDenominator = ref{ call_with(fn_builtin("int"), { aDenominator.release_ownership() }) };
                return &(new fraction_object{ { { &sFractionType.base } }, aNumerator.release_ownership(), aDenominator.release_ownership() })->h.base;
            }

            // (a rational's parts: a Fraction's or an int's)
            bool parts_of(object* aValue, ref& aNumerator, ref& aDenominator)
            {
                if (is_fraction(aValue))
                {
                    aNumerator = ref::borrowed(as_fraction(aValue).numerator);
                    aDenominator = ref::borrowed(as_fraction(aValue).denominator);
                    return true;
                }
                if (is_integer(aValue))
                {
                    aNumerator = ref::borrowed(builtin_value(aValue));
                    aDenominator = int_of(1);
                    return true;
                }
                return false;
            }

            object* from_float(double aValue)
            {
                ref const f{ make_float(aValue) };
                ref const ratio{ call_method_named(f.get(), "as_integer_ratio", {}) };
                if (!ratio)
                    return nullptr;
                auto const& items = as<tuple_object>(ratio.get())->items;
                return make_fraction(ref::borrowed(items[0]), ref::borrowed(items[1]), false);
            }

            object* from_string(std::string const& aText)
            {
                // ([sign] (digits [/ digits] | [digits] . digits | digits .) [e[sign]digits], surrounded by whitespace; underscores between digits)
                auto const invalid = [&]() { return error("ValueError", "Invalid literal for Fraction: " + [&]() { std::string t; ref const s{ make_str(aText) }; repr_of(s.get(), t); return t; }()); };
                std::size_t i = 0u;
                auto const n = aText.size();
                auto const space = [&](std::size_t p) { return p < n && std::isspace(static_cast<unsigned char>(aText[p])) != 0; };
                auto const digit = [&](std::size_t p) { return p < n && std::isdigit(static_cast<unsigned char>(aText[p])) != 0; };
                while (space(i))
                    ++i;
                bool negative = false;
                if (i < n && (aText[i] == '+' || aText[i] == '-'))
                    negative = aText[i++] == '-';
                // (digits with single underscores between them)
                auto const digits = [&](std::string& aResult) -> bool
                {
                    if (!digit(i))
                        return false;
                    while (digit(i))
                    {
                        aResult += aText[i++];
                        if (i + 1u < n && aText[i] == '_' && digit(i + 1u))
                            ++i;
                    }
                    return true;
                };
                if (!(digit(i) || (i + 1u < n && aText[i] == '.' && digit(i + 1u))))
                    return invalid();
                std::string whole, fractionDigits, denominatorDigits, exponentDigits;
                digits(whole);
                bool hasDenominator = false;
                bool exponentNegative = false;
                auto j = i;
                while (space(j))
                    ++j;
                if (j < n && aText[j] == '/')
                {
                    i = j + 1u;
                    while (space(i))
                        ++i;
                    if (!digits(denominatorDigits))
                        return invalid();
                    hasDenominator = true;
                }
                else
                {
                    if (i < n && aText[i] == '.')
                    {
                        ++i;
                        digits(fractionDigits);
                    }
                    if (i < n && (aText[i] == 'e' || aText[i] == 'E'))
                    {
                        ++i;
                        if (i < n && (aText[i] == '+' || aText[i] == '-'))
                            exponentNegative = aText[i++] == '-';
                        if (!digits(exponentDigits))
                            return invalid();
                    }
                }
                while (space(i))
                    ++i;
                if (i != n)
                    return invalid();
                auto const parse = [](std::string const& aDigits) { ref const text{ make_str(aDigits.empty() ? std::string{ "0" } : aDigits) }; return ref{ call_with(fn_builtin("int"), { (retain(text.get()), text.get()) }) }; };
                ref numerator{ parse(whole + fractionDigits) };
                ref denominator{ hasDenominator ? parse(denominatorDigits) : int_of(1) };
                if (!numerator || !denominator)
                    return nullptr;
                if (!fractionDigits.empty())
                {
                    ref const ten{ int_of(10) };
                    ref const scale{ op(NEOS_PY_POWER, ten.get(), int_of(static_cast<long long>(fractionDigits.size())).get()) };
                    denominator = op(NEOS_PY_MULTIPLY, denominator.get(), scale.get());
                }
                if (!exponentDigits.empty())
                {
                    ref const ten{ int_of(10) };
                    ref const exponent{ parse(exponentDigits) };
                    ref const scale{ op(NEOS_PY_POWER, ten.get(), exponent.get()) };
                    if (exponentNegative)
                        denominator = op(NEOS_PY_MULTIPLY, denominator.get(), scale.get());
                    else
                        numerator = op(NEOS_PY_MULTIPLY, numerator.get(), scale.get());
                }
                if (negative)
                    numerator = ref{ unary(NEOS_PY_NEGATIVE, numerator.get()) };
                return make_fraction(numerator, denominator);
            }

            object* construct(object* aNumerator, object* aDenominator)
            {
                if (aDenominator == nullptr || kind_of(aDenominator) == NEOS_PY_NONE)
                {
                    if (aNumerator == nullptr)
                        return make_fraction(int_of(0), int_of(1), false);
                    ref n, d;
                    if (parts_of(aNumerator, n, d))
                        return make_fraction(n, d, false);
                    if (kind_of(aNumerator) == NEOS_PY_FLOAT)
                        return from_float(as<float_object>(aNumerator)->value);
                    if (kind_of(aNumerator) == NEOS_PY_STR)
                        return from_string(as<str_object>(aNumerator)->value);
                    // (a Decimal, or another number with as_integer_ratio)
                    if (ref const ratio{ call_method_named(aNumerator, "as_integer_ratio", {}) })
                    {
                        auto const& items = as<tuple_object>(ratio.get())->items;
                        return make_fraction(ref::borrowed(items[0]), ref::borrowed(items[1]));
                    }
                    clear_error();
                    return error("TypeError", "argument should be a string or a Rational instance");
                }
                ref n1, d1, n2, d2;
                if (!parts_of(aNumerator, n1, d1) || !parts_of(aDenominator, n2, d2))
                    return error("TypeError", "both arguments should be Rational instances");
                // ((n1/d1) / (n2/d2))
                return make_fraction(op(NEOS_PY_MULTIPLY, n1.get(), d2.get()), op(NEOS_PY_MULTIPLY, d1.get(), n2.get()));
            }

            double to_float(fraction_object const& f)
            {
                ref const q{ op(NEOS_PY_TRUE_DIVIDE, f.numerator, f.denominator) };
                double d = 0.0;
                if (!q || !to_double(q.get(), d))
                    return std::nan("");
                return d;
            }

            // (arithmetic: aOperation on two Fractions (or ints))
            object* rational_operation(int aOperation, object* aLhs, object* aRhs)
            {
                ref na, da, nb, db;
                parts_of(aLhs, na, da);
                parts_of(aRhs, nb, db);
                switch (aOperation)
                {
                case NEOS_PY_ADD: case NEOS_PY_SUBTRACT:
                    return make_fraction(op(aOperation, op(NEOS_PY_MULTIPLY, na.get(), db.get()).get(), op(NEOS_PY_MULTIPLY, nb.get(), da.get()).get()),
                        op(NEOS_PY_MULTIPLY, da.get(), db.get()));
                case NEOS_PY_MULTIPLY:
                    return make_fraction(op(NEOS_PY_MULTIPLY, na.get(), nb.get()), op(NEOS_PY_MULTIPLY, da.get(), db.get()));
                case NEOS_PY_TRUE_DIVIDE:
                    if (is_zero(nb.get()))
                    {
                        std::string text;
                        repr_of(db.get(), text);
                        return error("ZeroDivisionError", "Fraction(" + text + ", 0)");
                    }
                    return make_fraction(op(NEOS_PY_MULTIPLY, na.get(), db.get()), op(NEOS_PY_MULTIPLY, da.get(), nb.get()));
                case NEOS_PY_FLOOR_DIVIDE:
                    {
                        ref const x{ op(NEOS_PY_MULTIPLY, na.get(), db.get()) };
                        ref const y{ op(NEOS_PY_MULTIPLY, da.get(), nb.get()) };
                        if (is_zero(y.get()))
                            return error("ZeroDivisionError", "division by zero");
                        return binary(NEOS_PY_FLOOR_DIVIDE, x.get(), y.get(), false);
                    }
                case NEOS_PY_REMAINDER:
                    {
                        ref const x{ op(NEOS_PY_MULTIPLY, na.get(), db.get()) };
                        ref const y{ op(NEOS_PY_MULTIPLY, nb.get(), da.get()) };
                        if (is_zero(y.get()))
                            return error("ZeroDivisionError", "division by zero");
                        return make_fraction(op(NEOS_PY_REMAINDER, x.get(), y.get()), op(NEOS_PY_MULTIPLY, da.get(), db.get()));
                    }
                case NEOS_PY_POWER:
                    {
                        if (!equals(db.get(), int_of(1).get()))
                            return nullptr;     // (not rational: float arithmetic)
                        auto const& power = nb;
                        if (!is_negative(power.get()))
                            return make_fraction(op(NEOS_PY_POWER, na.get(), power.get()), op(NEOS_PY_POWER, da.get(), power.get()), false);
                        ref const positive{ unary(NEOS_PY_NEGATIVE, power.get()) };
                        if (is_zero(na.get()))
                        {
                            ref const d{ op(NEOS_PY_POWER, da.get(), positive.get()) };
                            std::string text;
                            repr_of(d.get(), text);
                            return error("ZeroDivisionError", "Fraction(" + text + ", 0)");
                        }
                        ref numerator{ op(NEOS_PY_POWER, da.get(), positive.get()) };
                        ref denominator{ op(NEOS_PY_POWER, na.get(), positive.get()) };
                        if (is_negative(denominator.get()))
                        {
                            numerator = ref{ unary(NEOS_PY_NEGATIVE, numerator.get()) };
                            denominator = ref{ unary(NEOS_PY_NEGATIVE, denominator.get()) };
                        }
                        return make_fraction(numerator, denominator, false);
                    }
                default:
                    return borrowed(not_implemented());
                }
            }

            // (a hash equal to the numbers' hash: CPython's algorithm (modulo 2**61 - 1))
            bool fraction_hash(fraction_object const& f, long long& aResult)
            {
                ref const modulus{ make_int(2305843009213693951ll) };
                ref const exponent{ make_int(2305843009213693949ll) };
                ref const dinv{ call_with(fn_builtin("pow"), { borrowed(f.denominator), borrowed(exponent.get()), borrowed(modulus.get()) }) };
                if (!dinv)
                    return false;
                long long h = 0;
                if (is_zero(dinv.get()))
                    h = 314159;
                else
                {
                    ref const absolute{ unary(NEOS_PY_ABSOLUTE, f.numerator) };
                    long long numeratorHash = 0;
                    if (!hash_of(absolute.get(), numeratorHash))
                        return false;
                    ref const nh{ make_int(numeratorHash) };
                    ref const product{ op(NEOS_PY_MULTIPLY, nh.get(), dinv.get()) };
                    if (!hash_of(product.get(), h))
                        return false;
                }
                auto result = is_negative(f.numerator) ? -h : h;
                aResult = result == -1 ? -2 : result;
                return true;
            }

            object* limit_denominator(fraction_object const& f, object* aMax)
            {
                // (the closest fraction with a denominator at most aMax: CPython's algorithm)
                if (less(aMax, int_of(1).get()))
                    return error("ValueError", "max_denominator should be at least 1");
                if (!less(aMax, f.denominator))
                    return make_fraction(ref::borrowed(f.numerator), ref::borrowed(f.denominator), false);
                ref p0{ int_of(0) }, q0{ int_of(1) }, p1{ int_of(1) }, q1{ int_of(0) };
                ref n{ borrowed(f.numerator) }, d{ borrowed(f.denominator) };
                for (;;)
                {
                    ref const a{ op(NEOS_PY_FLOOR_DIVIDE, n.get(), d.get()) };
                    ref const q2{ op(NEOS_PY_ADD, q0.get(), op(NEOS_PY_MULTIPLY, a.get(), q1.get()).get()) };
                    if (less(aMax, q2.get()))
                        break;
                    ref const newP1{ op(NEOS_PY_ADD, p0.get(), op(NEOS_PY_MULTIPLY, a.get(), p1.get()).get()) };
                    p0 = p1;
                    q0 = q1;
                    p1 = newP1;
                    q1 = q2;
                    ref const newD{ op(NEOS_PY_SUBTRACT, n.get(), op(NEOS_PY_MULTIPLY, a.get(), d.get()).get()) };
                    n = d;
                    d = newD;
                }
                ref const k{ op(NEOS_PY_FLOOR_DIVIDE, op(NEOS_PY_SUBTRACT, aMax, q0.get()).get(), q1.get()) };
                // (the bounds: bound1 = (p0 + k*p1)/(q0 + k*q1), bound2 = p1/q1)
                ref const bound1{ make_fraction(op(NEOS_PY_ADD, p0.get(), op(NEOS_PY_MULTIPLY, k.get(), p1.get()).get()),
                    op(NEOS_PY_ADD, q0.get(), op(NEOS_PY_MULTIPLY, k.get(), q1.get()).get())) };
                ref const bound2{ make_fraction(p1, q1) };
                ref const self{ make_fraction(ref::borrowed(f.numerator), ref::borrowed(f.denominator), false) };
                if (!bound1 || !bound2 || !self)
                    return nullptr;
                ref const d2{ binary(NEOS_PY_SUBTRACT, bound2.get(), self.get(), false) };
                ref const d1{ binary(NEOS_PY_SUBTRACT, bound1.get(), self.get(), false) };
                ref const a2{ unary(NEOS_PY_ABSOLUTE, d2.get()) };
                ref const a1{ unary(NEOS_PY_ABSOLUTE, d1.get()) };
                ref const le{ compare(NEOS_PY_LE, a2.get(), a1.get()) };
                return borrowed(is_true(le) ? bound2.get() : bound1.get());
            }

            object* fraction_round(fraction_object const& f, object* aDigits)
            {
                if (aDigits == nullptr || kind_of(aDigits) == NEOS_PY_NONE)
                {
                    // (round half to even)
                    ref const floor{ op(NEOS_PY_FLOOR_DIVIDE, f.numerator, f.denominator) };
                    ref const remainder{ op(NEOS_PY_REMAINDER, f.numerator, f.denominator) };
                    ref const twice{ op(NEOS_PY_MULTIPLY, remainder.get(), int_of(2).get()) };
                    if (less(twice.get(), f.denominator))
                        return borrowed(floor.get());
                    if (less(f.denominator, twice.get()))
                        return binary(NEOS_PY_ADD, floor.get(), int_of(1).get(), false);
                    ref const odd{ op(NEOS_PY_REMAINDER, floor.get(), int_of(2).get()) };
                    return is_zero(odd.get()) ? borrowed(floor.get()) : binary(NEOS_PY_ADD, floor.get(), int_of(1).get(), false);
                }
                long long n = 0;
                if (!to_index(aDigits, n))
                    return nullptr;
                ref const ten{ int_of(10) };
                ref const shift{ op(NEOS_PY_POWER, ten.get(), int_of(n < 0 ? -n : n).get()) };
                ref const self{ make_fraction(ref::borrowed(f.numerator), ref::borrowed(f.denominator), false) };
                ref const scaled{ n > 0 ? binary(NEOS_PY_MULTIPLY, self.get(), shift.get(), false) : binary(NEOS_PY_TRUE_DIVIDE, self.get(), shift.get(), false) };
                if (!scaled)
                    return nullptr;
                ref const rounded{ fraction_round(as_fraction(scaled.get()), nullptr) };
                if (!rounded)
                    return nullptr;
                if (n >= 0)
                    return make_fraction(rounded, shift);
                return make_fraction(op(NEOS_PY_MULTIPLY, rounded.get(), shift.get()), int_of(1), false);
            }

            neos_py_type make_fraction_type()
            {
                auto type = make_type("fractions.Fraction", "Fraction", retain_object, release_object<fraction_object>);
                type.repr = [](neos_py_context*, object* aSelf) -> object*
                {
                    auto const& f = as_fraction(aSelf);
                    std::string n, d;
                    if (!repr_of(f.numerator, n) || !repr_of(f.denominator, d))
                        return nullptr;
                    return make_str("Fraction(" + n + ", " + d + ")");
                };
                type.str = [](neos_py_context*, object* aSelf) -> object*
                {
                    auto const& f = as_fraction(aSelf);
                    std::string n, d;
                    if (!repr_of(f.numerator, n) || !repr_of(f.denominator, d))
                        return nullptr;
                    return make_str(d == "1" ? n : n + "/" + d);
                };
                type.binary = [](neos_py_context*, int aOperation, object* aLhs, object* aRhs, int) -> object*
                {
                    ref na, da, nb, db;
                    bool const lhsRational = parts_of(aLhs, na, da);
                    bool const rhsRational = parts_of(aRhs, nb, db);
                    if (lhsRational && rhsRational)
                    {
                        if (aOperation == NEOS_PY_POWER)
                        {
                            if (auto const r = rational_operation(aOperation, aLhs, aRhs))
                                return r;
                            if (failed())
                                return nullptr;
                            // (a fractional power: float arithmetic)
                            ref const x{ make_float(is_fraction(aLhs) ? to_float(as_fraction(aLhs)) : 0.0) };
                            ref const lhsFloat{ is_fraction(aLhs) ? borrowed(x.get()) : borrowed(aLhs) };
                            ref const y{ make_float(is_fraction(aRhs) ? to_float(as_fraction(aRhs)) : 0.0) };
                            return binary(NEOS_PY_POWER, lhsFloat.get(), is_fraction(aRhs) ? y.get() : aRhs, false);
                        }
                        if (aOperation > NEOS_PY_POWER && aOperation != NEOS_PY_MATMUL)
                            return borrowed(not_implemented());
                        return rational_operation(aOperation, aLhs, aRhs);
                    }
                    // (with a float or complex: float arithmetic)
                    auto const other = lhsRational ? aRhs : aLhs;
                    if (kind_of(other) == NEOS_PY_FLOAT || kind_of(other) == NEOS_PY_COMPLEX)
                    {
                        auto const& f = as_fraction(lhsRational ? aLhs : aRhs);
                        ref const value{ make_float(to_float(f)) };
                        return lhsRational ? binary(aOperation, value.get(), aRhs, false) : binary(aOperation, aLhs, value.get(), false);
                    }
                    return borrowed(not_implemented());
                };
                type.unary = [](neos_py_context*, int aOperation, object* aSelf) -> object*
                {
                    auto const& f = as_fraction(aSelf);
                    switch (aOperation)
                    {
                    case NEOS_PY_NEGATIVE: return make_fraction(ref{ unary(NEOS_PY_NEGATIVE, f.numerator) }, ref::borrowed(f.denominator), false);
                    case NEOS_PY_POSITIVE: return make_fraction(ref::borrowed(f.numerator), ref::borrowed(f.denominator), false);
                    case NEOS_PY_ABSOLUTE: return make_fraction(ref{ unary(NEOS_PY_ABSOLUTE, f.numerator) }, ref::borrowed(f.denominator), false);
                    default: return error("TypeError", "bad operand type for unary ~: 'Fraction'");
                    }
                };
                type.compare = [](neos_py_context*, int aOperation, object* aLhs, object* aRhs) -> object*
                {
                    ref na, da, nb, db;
                    bool const lhsRational = parts_of(aLhs, na, da);
                    bool const rhsRational = parts_of(aRhs, nb, db);
                    if (!lhsRational || !rhsRational)
                    {
                        auto const other = lhsRational ? aRhs : aLhs;
                        if (kind_of(other) != NEOS_PY_FLOAT)
                            return borrowed(not_implemented());
                        auto const value = as<float_object>(other)->value;
                        if (!std::isfinite(value))
                        {
                            // (infinities and NaN: compared with 0.0)
                            ref const zero{ make_float(0.0) };
                            return lhsRational ? compare(aOperation, zero.get(), other) : compare(aOperation, other, zero.get());
                        }
                        ref const exact{ from_float(value) };
                        return lhsRational ? compare(aOperation, aLhs, exact.get()) : compare(aOperation, exact.get(), aRhs);
                    }
                    ref const x{ op(NEOS_PY_MULTIPLY, na.get(), db.get()) };
                    ref const y{ op(NEOS_PY_MULTIPLY, nb.get(), da.get()) };
                    return compare(aOperation, x.get(), y.get());
                };
                type.hash = [](neos_py_context*, object* aSelf, long long* aResult) -> int { return fraction_hash(as_fraction(aSelf), *aResult) ? 0 : -1; };
                type.truth = [](neos_py_context*, object* aSelf) -> int { return is_zero(as_fraction(aSelf).numerator) ? 0 : 1; };
                type.to_float = [](neos_py_context*, object* aSelf, double* aResult) -> int { *aResult = to_float(as_fraction(aSelf)); return failed() ? -1 : 0; };
                type.to_int = [](neos_py_context*, object* aSelf, long long* aResult) -> int
                {
                    auto const& f = as_fraction(aSelf);
                    ref const t{ is_negative(f.numerator) ? unary(NEOS_PY_NEGATIVE, op(NEOS_PY_FLOOR_DIVIDE, unary(NEOS_PY_NEGATIVE, f.numerator), f.denominator).get()) :
                        binary(NEOS_PY_FLOOR_DIVIDE, f.numerator, f.denominator, false) };
                    return t && to_index(t.get(), *aResult) ? 0 : -1;
                };
                type.getattr = [](neos_py_context*, object* aSelf, char const* aName) -> object*
                {
                    auto const& f = as_fraction(aSelf);
                    std::string_view const name{ aName };
                    if (name == "numerator")
                        return borrowed(f.numerator);
                    if (name == "denominator")
                        return borrowed(f.denominator);
                    static char const* const sMethods[] = { "limit_denominator", "as_integer_ratio", "is_integer", "__floor__", "__ceil__", "__trunc__", "__round__",
                        "__int__", "__float__", "__copy__", "__deepcopy__", "__format__" };
                    for (std::size_t i = 0u; i < std::size(sMethods); ++i)
                        if (name == sMethods[i])
                            return make_function(sMethods[i], [](neos_py_context*, void* aData, object* aArgs, object* aKwargs) -> object*
                            {
                                auto const& args = as<tuple_object>(aArgs)->items;
                                auto const& f = as_fraction(args[0]);
                                std::vector<object*> const rest{ args.begin() + 1, args.end() };
                                switch (reinterpret_cast<std::uintptr_t>(aData))
                                {
                                case 0:
                                    {
                                        std::vector<object*> a;
                                        if (!parse_arguments("limit_denominator", rest, aKwargs, { "max_denominator" }, 0u, 0u, a))
                                            return nullptr;
                                        ref const max{ a[0] != nullptr ? borrowed(a[0]) : make_int(1000000) };
                                        return limit_denominator(f, max.get());
                                    }
                                case 1: return make_tuple({ borrowed(f.numerator), borrowed(f.denominator) });
                                case 2: return make_bool(equals(f.denominator, int_of(1).get()));
                                case 3: return binary(NEOS_PY_FLOOR_DIVIDE, f.numerator, f.denominator, false);
                                case 4:
                                    {
                                        ref const negated{ unary(NEOS_PY_NEGATIVE, f.numerator) };
                                        ref const q{ op(NEOS_PY_FLOOR_DIVIDE, negated.get(), f.denominator) };
                                        return unary(NEOS_PY_NEGATIVE, q.get());
                                    }
                                case 5: case 7:
                                    {
                                        if (is_negative(f.numerator))
                                        {
                                            ref const negated{ unary(NEOS_PY_NEGATIVE, f.numerator) };
                                            ref const q{ op(NEOS_PY_FLOOR_DIVIDE, negated.get(), f.denominator) };
                                            return unary(NEOS_PY_NEGATIVE, q.get());
                                        }
                                        return binary(NEOS_PY_FLOOR_DIVIDE, f.numerator, f.denominator, false);
                                    }
                                case 6: return fraction_round(f, rest.empty() ? nullptr : rest[0]);
                                case 8: return make_float(to_float(f));
                                case 9: case 10: return borrowed(args[0]);
                                default:
                                    {
                                        // (format(): a float's presentation types; empty: str())
                                        if (rest.empty() || kind_of(rest[0]) != NEOS_PY_STR)
                                            return error("TypeError", "__format__() argument must be str");
                                        if (as<str_object>(rest[0])->value.empty())
                                        {
                                            std::string text;
                                            ref const self{ borrowed(args[0]) };
                                            return str_of(self.get(), text) ? make_str(text) : nullptr;
                                        }
                                        ref const value{ make_float(to_float(f)) };
                                        return call_with(fn_builtin("format"), { borrowed(value.get()), borrowed(rest[0]) });
                                    }
                                }
                            }, reinterpret_cast<void*>(i), aSelf);
                    return error("AttributeError", std::string{ "'Fraction' object has no attribute '" } + aName + "'");
                };
                return type;
            }

            object* new_fraction(neos_py_context*, void*, object* aArgs, object* aKwargs)
            {
                std::vector<object*> a;
                if (!parse_arguments("Fraction", as<tuple_object>(aArgs)->items, aKwargs, { "numerator", "denominator" }, 0u, 0u, a))
                    return nullptr;
                return construct(a[0], a[1]);
            }

            int fraction_instancecheck(neos_py_context*, void*, object* aObject) { return is_fraction(aObject) ? 1 : 0; }

            bool fractions_module(std::vector<std::pair<std::string, object*>>& aAttributes)
            {
                aAttributes.emplace_back("Fraction", context()->type_new(context(), "Fraction", "fractions.Fraction", new_fraction, nullptr, fraction_instancecheck));
                return true;
            }

            inline bool const sFractions = register_module("fractions", fractions_module);
        }

        // (Fraction.from_float, from_decimal: the type object's class functions)
        object* fraction_class_function(char const* aName)
        {
            if (std::strcmp(aName, "from_float") == 0 || std::strcmp(aName, "from_decimal") == 0 || std::strcmp(aName, "from_number") == 0)
                return make_function(aName, [](neos_py_context*, void*, object* aArgs, object*) -> object*
                {
                    auto const& args = as<tuple_object>(aArgs)->items;
                    if (args.size() != 1u)
                        return error("TypeError", "expected 1 argument");
                    if (kind_of(args[0]) == NEOS_PY_FLOAT)
                        return from_float(as<float_object>(args[0])->value);
                    return construct(args[0], nullptr);
                }, nullptr);
            return nullptr;
        }
    }
}
