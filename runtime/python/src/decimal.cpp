/*
  decimal.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/_pydecimal.py), Copyright (c) 2001-2024 Python Software
  Foundation; All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from Python to C++ for the neos Python runtime plugin and adapted to
  its object model (see runtime/python/NOTICE).
*/

// The module decimal: the General Decimal Arithmetic Specification as CPython 3.13 implements it (the algorithms of _pydecimal.py,
// with _decimal's messages): Decimal, Context, the signals and the context functions

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <optional>
#include <string>
#include <vector>
#include "objects.hpp"

namespace neos::python_runtime
{
    namespace detail
    {
        namespace
        {
            using B = big_integer;

            object* borrowed(object* aObject) { retain(aObject); return aObject; }

            object* call_with(object* aCallable, std::vector<object*> aArgs, object* aKwargs = nullptr)    // (steals the arguments)
            {
                ref const args{ make_tuple(std::move(aArgs)) };
                return call(aCallable, args.get(), aKwargs);
            }

            // --- big integer helpers

            B big(long long aValue) { return B{ aValue }; }
            B from_digits(std::string const& aDigits) { return B::from_string(aDigits.empty() ? std::string{ "0" } : aDigits); }
            std::string digits_of(B const& aValue) { return big_digits(aValue, 10); }
            std::size_t length_of(B const& aValue) { return digits_of(aValue).size(); }
            B pow10(long long n)
            {
                B result{ 1 };
                B ten{ 10 };
                B base = ten;
                while (n > 0)
                {
                    if ((n & 1) != 0)
                        result = result * base;
                    base = base * base;
                    n >>= 1;
                }
                return result;
            }
            B floordiv(B const& a, B const& b) { B q, r; big_divmod(a, b, q, r); return q; }
            B floormod(B const& a, B const& b) { B q, r; big_divmod(a, b, q, r); return r; }
            bool is_odd(B const& a) { return !big_zero(floormod(a, big(2))); }
            B div_nearest(B const& a, B const& b)
            {
                B q, r;
                big_divmod(a, b, q, r);
                auto const twice = r + r + (is_odd(q) ? big(1) : big(0));
                return q + (b < twice ? big(1) : big(0));
            }
            B rshift_nearest(B const& x, long long shift) { return div_nearest(x, B{ 1 } << static_cast<std::int64_t>(shift)); }
            long long nbits(B const& x) { return static_cast<long long>(big_bit_length(x)); }
            B sqrt_nearest(B const& n, B a)
            {
                B b{ 0 };
                while (!(a == b))
                {
                    b = a;
                    a = floordiv(a - floordiv(-n, a), big(2));
                }
                return a;
            }

            // --- log and exp (_pydecimal's _ilog, _dlog, _dlog10, _iexp, _dexp and _dpower)

            B ilog(B const& x, B const& M, long long L = 8)
            {
                auto y = x - M;
                long long R = 0;
                for (;;)
                {
                    auto const ay = big_abs(y);
                    bool const go = (R <= L && !(((ay << static_cast<std::int64_t>(L - R))) < M)) || (R > L && !((ay >> static_cast<std::int64_t>(R - L)) < M));
                    if (!go)
                        break;
                    y = div_nearest((M * y) << 1, M + sqrt_nearest(M * (M + rshift_nearest(y, R)), M));
                    ++R;
                }
                auto const T = -floordiv(big(-10 * static_cast<long long>(length_of(M))), big(3 * L));
                long long t = 0;
                t = std::stoll(digits_of(T));
                auto const yshift = rshift_nearest(y, R);
                auto w = div_nearest(M, big(t));
                for (long long k = t - 1; k > 0; --k)
                    w = div_nearest(M, big(k)) - div_nearest(yshift * w, M);
                return div_nearest(w * y, M);
            }

            std::string sLog10Digits = "23025850929940456840179914546843642076011014886";

            B log10_digits(long long p)
            {
                if (p >= static_cast<long long>(sLog10Digits.size()))
                {
                    long long extra = 3;
                    std::string digits;
                    for (;;)
                    {
                        auto const M = pow10(p + extra + 2);
                        digits = digits_of(div_nearest(ilog(M * big(10), M), big(100)));
                        if (digits.substr(digits.size() - static_cast<std::size_t>(extra)) != std::string(static_cast<std::size_t>(extra), '0'))
                            break;
                        extra += 3;
                    }
                    auto const end = digits.find_last_not_of('0');
                    sLog10Digits = digits.substr(0u, end + 1u);
                    sLog10Digits.pop_back();
                }
                return from_digits(sLog10Digits.substr(0u, static_cast<std::size_t>(p + 1)));
            }

            B dlog10(B c, long long e, long long p)
            {
                p += 2;
                auto const l = static_cast<long long>(length_of(c));
                auto const f = e + l - (e + l >= 1 ? 1 : 0);
                B logD{ 0 }, logTenpower;
                if (p > 0)
                {
                    auto const M = pow10(p);
                    auto const k = e + p - f;
                    if (k >= 0)
                        c = c * pow10(k);
                    else
                        c = div_nearest(c, pow10(-k));
                    logD = ilog(c, M);
                    auto const log10 = log10_digits(p);
                    logD = div_nearest(logD * M, log10);
                    logTenpower = big(f) * M;
                }
                else
                    logTenpower = div_nearest(big(f), pow10(-p));
                return div_nearest(logTenpower + logD, big(100));
            }

            B dlog(B c, long long e, long long p)
            {
                p += 2;
                auto const l = static_cast<long long>(length_of(c));
                auto const f = e + l - (e + l >= 1 ? 1 : 0);
                B logD{ 0 };
                if (p > 0)
                {
                    auto const k = e + p - f;
                    if (k >= 0)
                        c = c * pow10(k);
                    else
                        c = div_nearest(c, pow10(-k));
                    logD = ilog(c, pow10(p));
                }
                B fLogTen{ 0 };
                if (f != 0)
                {
                    auto const extra = static_cast<long long>(std::to_string(f < 0 ? -f : f).size()) - 1;
                    if (p + extra >= 0)
                        fLogTen = div_nearest(big(f) * log10_digits(p + extra), pow10(extra));
                }
                return div_nearest(fLogTen + logD, big(100));
            }

            B iexp(B const& x, B const& M, long long L = 8)
            {
                auto const R = nbits(floordiv(x << static_cast<std::int64_t>(L), M));
                auto const T = -floordiv(big(-10 * static_cast<long long>(length_of(M))), big(3 * L));
                auto const t = std::stoll(digits_of(T));
                auto y = div_nearest(x, big(t));
                auto Mshift = M << static_cast<std::int64_t>(R);
                for (long long i = t - 1; i > 0; --i)
                    y = div_nearest(x * (Mshift + y), Mshift * big(i));
                for (long long k = R - 1; k >= 0; --k)
                {
                    auto const ms = M << static_cast<std::int64_t>(k + 2);
                    y = div_nearest(y * (y + ms), ms);
                }
                return M + y;
            }

            std::pair<B, long long> dexp(B const& c, long long e, long long p)
            {
                p += 2;
                auto const extra = std::max(0ll, e + static_cast<long long>(length_of(c)) - 1);
                auto const q = p + extra;
                auto const shift = e + q;
                B cshift = shift >= 0 ? c * pow10(shift) : floordiv(c, pow10(-shift));
                B quot, rem;
                big_divmod(cshift, log10_digits(q), quot, rem);
                rem = div_nearest(rem, pow10(extra));
                return { div_nearest(iexp(rem, pow10(p)), big(1000)), std::stoll((big_negative(quot) ? "-" : "") + digits_of(quot)) - p + 3 };
            }

            std::pair<B, long long> dpower(B const& xc, long long xe, B const& yc, long long ye, long long p)
            {
                auto const b = static_cast<long long>(length_of(yc)) + ye;
                auto const lxc = dlog(xc, xe, p + b + 1);
                auto const shift = ye - b;
                B pc = shift >= 0 ? lxc * yc * pow10(shift) : div_nearest(lxc * yc, pow10(-shift));
                if (big_zero(pc))
                {
                    if ((static_cast<long long>(length_of(xc)) + xe >= 1) == !big_negative(yc))
                        return { pow10(p - 1) + big(1), 1 - p };
                    return { pow10(p) - big(1), -p };
                }
                auto [coeff, exp] = dexp(pc, -(p + 1), p + 1);
                return { div_nearest(coeff, big(10)), exp + 1 };
            }

            // --- signals, contexts

            enum signal : unsigned
            {
                InvalidOperation = 1u << 0, FloatOperation = 1u << 1, DivisionByZero = 1u << 2, Overflow = 1u << 3, Underflow = 1u << 4,
                Subnormal = 1u << 5, Inexact = 1u << 6, Rounded = 1u << 7, Clamped = 1u << 8
            };
            char const* const sSignalNames[] = { "InvalidOperation", "FloatOperation", "DivisionByZero", "Overflow", "Underflow", "Subnormal", "Inexact", "Rounded", "Clamped" };

            enum rounding_mode { RoundDown, RoundHalfUp, RoundHalfEven, RoundCeiling, RoundFloor, RoundUp, RoundHalfDown, Round05Up };
            char const* const sRoundingNames[] = { "ROUND_DOWN", "ROUND_HALF_UP", "ROUND_HALF_EVEN", "ROUND_CEILING", "ROUND_FLOOR", "ROUND_UP", "ROUND_HALF_DOWN", "ROUND_05UP" };

            struct context_state
            {
                long long prec = 28;
                int rounding = RoundHalfEven;
                long long Emin = -999999;
                long long Emax = 999999;
                int capitals = 1;
                int clamp = 0;
                unsigned flags = 0u;
                unsigned traps = InvalidOperation | DivisionByZero | Overflow;
                long long Etiny() const { return Emin - prec + 1; }
                long long Etop() const { return Emax - prec + 1; }
            };

            struct trapped {};  // (a signal that is trapped: its exception has been raised)

            struct context_object
            {
                header h;
                context_state state;
            };

            neos_py_type make_context_type();
            neos_py_type sContextType = make_context_type();
            context_object& as_context(object* aObject) { return *reinterpret_cast<context_object*>(aObject); }
            bool is_context(object* aObject) { return aObject != nullptr && aObject->type == &sContextType.base; }

            object* new_context(context_state const& aState) { return &(new context_object{ { { &sContextType.base } }, aState })->h.base; }

            thread_local object* tCurrent = nullptr;

            // (the current context (borrowed))
            object* current_context()
            {
                if (tCurrent == nullptr)
                    tCurrent = new_context(context_state{});
                return tCurrent;
            }
            context_state& current() { return as_context(current_context()).state; }

            std::string exception_name(char const* aSignal) { return std::string{ "decimal." } + aSignal; }

            void register_signals()
            {
                static bool sDone = false;
                if (sDone)
                    return;
                sDone = true;
                auto& bases = user_exception_bases();
                bases["decimal.DecimalException"] = { "ArithmeticError" };
                bases["decimal.Clamped"] = { "decimal.DecimalException" };
                bases["decimal.InvalidOperation"] = { "decimal.DecimalException" };
                bases["decimal.ConversionSyntax"] = { "decimal.InvalidOperation" };
                bases["decimal.DivisionByZero"] = { "decimal.DecimalException", "ZeroDivisionError" };
                bases["decimal.DivisionImpossible"] = { "decimal.InvalidOperation" };
                bases["decimal.DivisionUndefined"] = { "decimal.InvalidOperation", "ZeroDivisionError" };
                bases["decimal.Inexact"] = { "decimal.DecimalException" };
                bases["decimal.InvalidContext"] = { "decimal.InvalidOperation" };
                bases["decimal.Rounded"] = { "decimal.DecimalException" };
                bases["decimal.Subnormal"] = { "decimal.DecimalException" };
                bases["decimal.Overflow"] = { "decimal.Inexact", "decimal.Rounded" };
                bases["decimal.Underflow"] = { "decimal.Inexact", "decimal.Rounded", "decimal.Subnormal" };
                bases["decimal.FloatOperation"] = { "decimal.DecimalException", "TypeError" };
            }

            // --- Decimal values

            struct dec
            {
                int sign = 0;
                std::string digits = "0";   ///< (the coefficient; a NaN's payload (possibly empty))
                long long exp = 0;
                char special = 0;           ///< (0: finite, 'F': infinity, 'n': quiet NaN, 'N': signaling NaN)
                bool is_special() const { return special != 0; }
                bool is_nan() const { return special == 'n' || special == 'N'; }
                int nan_kind() const { return special == 'n' ? 1 : special == 'N' ? 2 : 0; }
                int infinity() const { return special == 'F' ? (sign != 0 ? -1 : 1) : 0; }
                bool is_zero() const { return !is_special() && digits == "0"; }
                bool truth() const { return is_special() || digits != "0"; }
                long long adjusted() const { return is_special() ? 0 : exp + static_cast<long long>(digits.size()) - 1; }
                B coefficient() const { return from_digits(digits); }
            };

            dec triple(int aSign, std::string aDigits, long long aExp)
            {
                auto const first = aDigits.find_first_not_of('0');
                return dec{ aSign, first == std::string::npos ? std::string{ "0" } : aDigits.substr(first), aExp, 0 };
            }
            dec triple(int aSign, B const& aCoefficient, long long aExp) { return triple(aSign, digits_of(aCoefficient), aExp); }
            dec infinity_of(int aSign) { return dec{ aSign, "0", 0, 'F' }; }
            dec nan_value() { return dec{ 0, "", 0, 'n' }; }

            // (a signal: its flag; trapped: its exception (and C++ exception trapped); the condition's (aCondition: its class's name, e.g. DivisionUndefined))
            dec raise_error(context_state& aContext, unsigned aSignal, char const* aCondition, int aSign = 0)
            {
                register_signals();
                aContext.flags |= aSignal;
                if ((aContext.traps & aSignal) != 0u)
                {
                    std::string signalName;
                    for (std::size_t i = 0u; i < std::size(sSignalNames); ++i)
                        if (aSignal == (1u << i))
                            signalName = sSignalNames[i];
                    ref const exception{ make_exception(exception_name(signalName.c_str()), { make_str("[<class 'decimal." + std::string{ aCondition } + "'>]") }) };
                    fn_raise(exception.get());
                    throw trapped{};
                }
                switch (aSignal)
                {
                case InvalidOperation: return nan_value();
                case DivisionByZero: return infinity_of(aSign);
                case Overflow:
                    {
                        auto const r = aContext.rounding;
                        if (r == RoundHalfUp || r == RoundHalfEven || r == RoundHalfDown || r == RoundUp)
                            return infinity_of(aSign);
                        if (aSign == 0)
                        {
                            if (r == RoundCeiling)
                                return infinity_of(aSign);
                            return triple(aSign, std::string(static_cast<std::size_t>(aContext.prec), '9'), aContext.Emax - aContext.prec + 1);
                        }
                        if (r == RoundFloor)
                            return infinity_of(aSign);
                        return triple(aSign, std::string(static_cast<std::size_t>(aContext.prec), '9'), aContext.Emax - aContext.prec + 1);
                    }
                default:
                    return dec{};
                }
            }

            dec fix_nan(dec const& aValue, context_state const& aContext)
            {
                auto const max = static_cast<std::size_t>(aContext.prec - aContext.clamp);
                if (aValue.digits.size() > max)
                {
                    auto payload = aValue.digits.substr(aValue.digits.size() - max);
                    auto const first = payload.find_first_not_of('0');
                    return dec{ aValue.sign, first == std::string::npos ? std::string{} : payload.substr(first), 0, 'n' };
                }
                return dec{ aValue.sign, aValue.digits, 0, 'n' };
            }

            // (rounding: 1 round up, 0 exact, -1 round down (the digits after aPrecision))
            bool all_zeros(std::string const& s, std::size_t p) { return s.find_first_not_of('0', p) == std::string::npos; }
            bool exact_half(std::string const& s, std::size_t p) { return p < s.size() && s[p] == '5' && all_zeros(s, p + 1u); }
            int round_down(dec const& d, std::size_t p) { return all_zeros(d.digits, p) ? 0 : -1; }
            int round_half_up(dec const& d, std::size_t p) { if (p < d.digits.size() && d.digits[p] >= '5') return 1; return all_zeros(d.digits, p) ? 0 : -1; }
            int rounding_change(dec const& d, std::size_t p, int aRounding)
            {
                switch (aRounding)
                {
                case RoundDown: return round_down(d, p);
                case RoundUp: return -round_down(d, p);
                case RoundHalfUp: return round_half_up(d, p);
                case RoundHalfDown: return exact_half(d.digits, p) ? -1 : round_half_up(d, p);
                case RoundHalfEven: return exact_half(d.digits, p) && (p == 0u || std::strchr("02468", d.digits[p - 1u]) != nullptr) ? -1 : round_half_up(d, p);
                case RoundCeiling: return d.sign != 0 ? round_down(d, p) : -round_down(d, p);
                case RoundFloor: return d.sign == 0 ? round_down(d, p) : -round_down(d, p);
                default: return p != 0u && std::strchr("05", d.digits[p - 1u]) == nullptr ? round_down(d, p) : -round_down(d, p);
                }
            }

            std::string increment(std::string const& aDigits)
            {
                return digits_of(from_digits(aDigits) + B{ 1 });
            }

            dec fix(dec self, context_state& aContext)
            {
                if (self.is_special())
                    return self.is_nan() ? fix_nan(self, aContext) : self;
                auto const Etiny = aContext.Etiny();
                auto const Etop = aContext.Etop();
                if (!self.truth())
                {
                    auto const expMax = aContext.clamp != 0 ? Etop : aContext.Emax;
                    auto const newExp = std::min(std::max(self.exp, Etiny), expMax);
                    if (newExp != self.exp)
                    {
                        raise_error(aContext, Clamped, "Clamped");
                        return triple(self.sign, "0", newExp);
                    }
                    return self;
                }
                auto expMin = static_cast<long long>(self.digits.size()) + self.exp - aContext.prec;
                if (expMin > Etop)
                {
                    auto ans = raise_error(aContext, Overflow, "Overflow", self.sign);
                    raise_error(aContext, Inexact, "Inexact");
                    raise_error(aContext, Rounded, "Rounded");
                    return ans;
                }
                bool const subnormal = expMin < Etiny;
                if (subnormal)
                    expMin = Etiny;
                if (self.exp < expMin)
                {
                    auto digits = static_cast<long long>(self.digits.size()) + self.exp - expMin;
                    if (digits < 0)
                    {
                        self = triple(self.sign, "1", expMin - 1);
                        digits = 0;
                    }
                    auto const changed = rounding_change(self, static_cast<std::size_t>(digits), aContext.rounding);
                    std::string coeff = self.digits.substr(0u, static_cast<std::size_t>(digits));
                    if (coeff.empty())
                        coeff = "0";
                    if (changed > 0)
                    {
                        coeff = increment(coeff);
                        if (static_cast<long long>(coeff.size()) > aContext.prec)
                        {
                            coeff.pop_back();
                            ++expMin;
                        }
                    }
                    dec ans;
                    if (expMin > Etop)
                        ans = raise_error(aContext, Overflow, "Overflow", self.sign);
                    else
                        ans = triple(self.sign, coeff, expMin);
                    if (changed != 0 && subnormal)
                        raise_error(aContext, Underflow, "Underflow");
                    if (subnormal)
                        raise_error(aContext, Subnormal, "Subnormal");
                    if (changed != 0)
                        raise_error(aContext, Inexact, "Inexact");
                    raise_error(aContext, Rounded, "Rounded");
                    if (!ans.truth())
                        raise_error(aContext, Clamped, "Clamped");
                    return ans;
                }
                if (subnormal)
                    raise_error(aContext, Subnormal, "Subnormal");
                if (aContext.clamp == 1 && self.exp > Etop)
                {
                    raise_error(aContext, Clamped, "Clamped");
                    return triple(self.sign, self.digits + std::string(static_cast<std::size_t>(self.exp - Etop), '0'), Etop);
                }
                return self;
            }

            dec rescale(dec self, long long aExp, int aRounding)
            {
                if (self.is_special())
                    return self;
                if (!self.truth())
                    return triple(self.sign, "0", aExp);
                if (self.exp >= aExp)
                    return triple(self.sign, self.digits + std::string(static_cast<std::size_t>(self.exp - aExp), '0'), aExp);
                auto digits = static_cast<long long>(self.digits.size()) + self.exp - aExp;
                if (digits < 0)
                {
                    self = triple(self.sign, "1", aExp - 1);
                    digits = 0;
                }
                auto const changed = rounding_change(self, static_cast<std::size_t>(digits), aRounding);
                std::string coeff = self.digits.substr(0u, static_cast<std::size_t>(digits));
                if (coeff.empty())
                    coeff = "0";
                if (changed == 1)
                    coeff = increment(coeff);
                return triple(self.sign, coeff, aExp);
            }

            dec round_places(dec const& self, long long aPlaces, int aRounding)
            {
                if (self.is_special() || !self.truth())
                    return self;
                auto ans = rescale(self, self.adjusted() + 1 - aPlaces, aRounding);
                if (ans.adjusted() != self.adjusted())
                    ans = rescale(ans, ans.adjusted() + 1 - aPlaces, aRounding);
                return ans;
            }

            // (NaN operands: the result, if any)
            std::optional<dec> check_nans(dec const& self, dec const* aOther, context_state& aContext)
            {
                auto const selfNan = self.nan_kind();
                auto const otherNan = aOther != nullptr ? aOther->nan_kind() : 0;
                if (selfNan == 0 && otherNan == 0)
                    return {};
                if (selfNan == 2)
                {
                    raise_error(aContext, InvalidOperation, "InvalidOperation");
                    return fix_nan(self, aContext);
                }
                if (otherNan == 2)
                {
                    raise_error(aContext, InvalidOperation, "InvalidOperation");
                    return fix_nan(*aOther, aContext);
                }
                return selfNan != 0 ? fix_nan(self, aContext) : fix_nan(*aOther, aContext);
            }

            int cmp(dec const& self, dec const& other)
            {
                if (self.is_special() || other.is_special())
                {
                    auto const a = self.infinity();
                    auto const b = other.infinity();
                    return a == b ? 0 : a < b ? -1 : 1;
                }
                auto const sign = [](int s) { return s != 0 ? -1 : 1; };
                if (!self.truth())
                    return !other.truth() ? 0 : -sign(other.sign);
                if (!other.truth())
                    return sign(self.sign);
                if (other.sign < self.sign)
                    return -1;
                if (self.sign < other.sign)
                    return 1;
                auto const sa = self.adjusted();
                auto const oa = other.adjusted();
                if (sa == oa)
                {
                    auto const sp = self.digits + std::string(static_cast<std::size_t>(std::max(0ll, self.exp - other.exp)), '0');
                    auto const op = other.digits + std::string(static_cast<std::size_t>(std::max(0ll, other.exp - self.exp)), '0');
                    if (sp == op)
                        return 0;
                    return (sp.size() < op.size() || (sp.size() == op.size() && sp < op)) ? -sign(self.sign) : sign(self.sign);
                }
                return sa > oa ? sign(self.sign) : -sign(self.sign);
            }

            dec negate(dec d) { d.sign = d.sign != 0 ? 0 : 1; return d; }
            dec absolute(dec d) { d.sign = 0; return d; }

            dec add(dec self, dec other, context_state& c)
            {
                if (self.is_special() || other.is_special())
                {
                    if (auto const n = check_nans(self, &other, c))
                        return *n;
                    if (self.infinity() != 0)
                    {
                        if (self.sign != other.sign && other.infinity() != 0)
                            return raise_error(c, InvalidOperation, "InvalidOperation");
                        return self;
                    }
                    if (other.infinity() != 0)
                        return other;
                }
                auto exp = std::min(self.exp, other.exp);
                int const negativezero = c.rounding == RoundFloor && self.sign != other.sign ? 1 : 0;
                if (!self.truth() && !other.truth())
                    return fix(triple(negativezero != 0 ? 1 : std::min(self.sign, other.sign), "0", exp), c);
                if (!self.truth())
                {
                    exp = std::max(exp, other.exp - c.prec - 1);
                    return fix(rescale(other, exp, c.rounding), c);
                }
                if (!other.truth())
                {
                    exp = std::max(exp, self.exp - c.prec - 1);
                    return fix(rescale(self, exp, c.rounding), c);
                }
                // (_normalize: the smaller operand reduced if far below the other's precision)
                struct work { int sign; B coefficient; long long exp; };
                work op1{ self.sign, self.coefficient(), self.exp };
                work op2{ other.sign, other.coefficient(), other.exp };
                {
                    auto& tmp = op1.exp < op2.exp ? op2 : op1;
                    auto& lower = op1.exp < op2.exp ? op1 : op2;
                    auto const tmpLength = static_cast<long long>(length_of(tmp.coefficient));
                    auto const otherLength = static_cast<long long>(length_of(lower.coefficient));
                    auto const e = tmp.exp + std::min(-1ll, tmpLength - c.prec - 2);
                    if (otherLength + lower.exp - 1 < e)
                    {
                        lower.coefficient = B{ 1 };
                        lower.exp = e;
                    }
                    tmp.coefficient = tmp.coefficient * pow10(tmp.exp - lower.exp);
                    tmp.exp = lower.exp;
                }
                int resultSign = 0;
                if (op1.sign != op2.sign)
                {
                    if (op1.coefficient == op2.coefficient)
                        return fix(triple(negativezero, "0", exp), c);
                    if (op1.coefficient < op2.coefficient)
                        std::swap(op1, op2);
                    if (op1.sign == 1)
                    {
                        resultSign = 1;
                        std::swap(op1.sign, op2.sign);
                    }
                }
                else if (op1.sign == 1)
                {
                    resultSign = 1;
                    op1.sign = 0;
                    op2.sign = 0;
                }
                auto const coefficient = op2.sign == 0 ? op1.coefficient + op2.coefficient : op1.coefficient - op2.coefficient;
                return fix(triple(resultSign, coefficient, op1.exp), c);
            }

            dec multiply(dec self, dec other, context_state& c)
            {
                auto const sign = self.sign ^ other.sign;
                if (self.is_special() || other.is_special())
                {
                    if (auto const n = check_nans(self, &other, c))
                        return *n;
                    if (self.infinity() != 0)
                        return !other.truth() ? raise_error(c, InvalidOperation, "InvalidOperation") : infinity_of(sign);
                    if (other.infinity() != 0)
                        return !self.truth() ? raise_error(c, InvalidOperation, "InvalidOperation") : infinity_of(sign);
                }
                auto const exp = self.exp + other.exp;
                if (!self.truth() || !other.truth())
                    return fix(triple(sign, "0", exp), c);
                return fix(triple(sign, self.coefficient() * other.coefficient(), exp), c);
            }

            dec divide(dec self, dec other, context_state& c)
            {
                auto const sign = self.sign ^ other.sign;
                if (self.is_special() || other.is_special())
                {
                    if (auto const n = check_nans(self, &other, c))
                        return *n;
                    if (self.infinity() != 0 && other.infinity() != 0)
                        return raise_error(c, InvalidOperation, "InvalidOperation");
                    if (self.infinity() != 0)
                        return infinity_of(sign);
                    if (other.infinity() != 0)
                    {
                        raise_error(c, Clamped, "Clamped");
                        return triple(sign, "0", c.Etiny());
                    }
                }
                if (!other.truth())
                {
                    if (!self.truth())
                        return raise_error(c, InvalidOperation, "DivisionUndefined");
                    return raise_error(c, DivisionByZero, "DivisionByZero", sign);
                }
                long long exp = 0;
                B coeff{ 0 };
                if (!self.truth())
                    exp = self.exp - other.exp;
                else
                {
                    auto const shift = static_cast<long long>(other.digits.size()) - static_cast<long long>(self.digits.size()) + c.prec + 1;
                    exp = self.exp - other.exp - shift;
                    B remainder;
                    if (shift >= 0)
                        big_divmod(self.coefficient() * pow10(shift), other.coefficient(), coeff, remainder);
                    else
                        big_divmod(self.coefficient(), other.coefficient() * pow10(-shift), coeff, remainder);
                    if (!big_zero(remainder))
                    {
                        if (big_zero(floormod(coeff, big(5))))
                            coeff = coeff + B{ 1 };
                    }
                    else
                    {
                        auto const ideal = self.exp - other.exp;
                        while (exp < ideal && big_zero(floormod(coeff, big(10))))
                        {
                            coeff = floordiv(coeff, big(10));
                            ++exp;
                        }
                    }
                }
                return fix(triple(sign, coeff, exp), c);
            }

            // (// and %: the integer quotient and the remainder (unrounded))
            bool divide_integer(dec const& self, dec const& other, context_state& c, dec& aQuotient, dec& aRemainder)
            {
                auto const sign = self.sign ^ other.sign;
                auto const ideal = other.infinity() != 0 ? self.exp : std::min(self.exp, other.exp);
                auto const expdiff = self.adjusted() - other.adjusted();
                if (!self.truth() || other.infinity() != 0 || expdiff <= -2)
                {
                    aQuotient = triple(sign, "0", 0);
                    aRemainder = rescale(self, ideal, c.rounding);
                    return true;
                }
                if (expdiff <= c.prec)
                {
                    auto a = self.coefficient();
                    auto b = other.coefficient();
                    if (self.exp >= other.exp)
                        a = a * pow10(self.exp - other.exp);
                    else
                        b = b * pow10(other.exp - self.exp);
                    B q, r;
                    big_divmod(a, b, q, r);
                    if (q < pow10(c.prec))
                    {
                        aQuotient = triple(sign, q, 0);
                        aRemainder = triple(self.sign, r, ideal);
                        return true;
                    }
                }
                aQuotient = aRemainder = raise_error(c, InvalidOperation, "DivisionImpossible");
                return false;
            }

            dec floor_divide(dec self, dec other, context_state& c)
            {
                if (auto const n = check_nans(self, &other, c))
                    return *n;
                if (self.infinity() != 0)
                {
                    if (other.infinity() != 0)
                        return raise_error(c, InvalidOperation, "InvalidOperation");
                    return infinity_of(self.sign ^ other.sign);
                }
                if (!other.truth())
                {
                    if (self.truth())
                        return raise_error(c, DivisionByZero, "DivisionByZero", self.sign ^ other.sign);
                    return raise_error(c, InvalidOperation, "DivisionUndefined");
                }
                dec q, r;
                divide_integer(self, other, c, q, r);
                return q;
            }

            dec modulo(dec self, dec other, context_state& c)
            {
                if (auto const n = check_nans(self, &other, c))
                    return *n;
                if (self.infinity() != 0)
                    return raise_error(c, InvalidOperation, "InvalidOperation");
                if (!other.truth())
                {
                    if (self.truth())
                        return raise_error(c, InvalidOperation, "InvalidOperation");
                    return raise_error(c, InvalidOperation, "DivisionUndefined");
                }
                dec q, r;
                if (!divide_integer(self, other, c, q, r))
                    return r;
                return fix(r, c);
            }

            bool is_integer_value(dec const& d)
            {
                if (d.is_special())
                    return false;
                if (d.exp >= 0)
                    return true;
                auto const n = static_cast<std::size_t>(std::min<long long>(-d.exp, static_cast<long long>(d.digits.size())));
                return all_zeros(d.digits, d.digits.size() - n);
            }

            bool is_even(dec const& d)
            {
                if (!d.truth() || d.exp > 0)
                    return true;
                auto const index = static_cast<long long>(d.digits.size()) - 1 + d.exp;
                if (index < 0)
                    return true;
                return std::strchr("02468", d.digits[static_cast<std::size_t>(index)]) != nullptr;
            }

            long long log10_exp_bound(dec const& d)
            {
                auto const adj = d.exp + static_cast<long long>(d.digits.size()) - 1;
                if (adj >= 1)
                    return static_cast<long long>(std::to_string(adj).size()) - 1;
                if (adj <= -2)
                    return static_cast<long long>(std::to_string(-1 - adj).size()) - 1;
                auto const c = d.coefficient();
                auto const e = d.exp;
                if (adj == 0)
                {
                    auto const num = digits_of(c - pow10(-e));
                    auto const den = digits_of(c * big(231));
                    auto const numNeg = big_negative(c - pow10(-e));
                    std::string const numText = (numNeg ? "-" : "") + num;
                    return static_cast<long long>(numText.size()) - static_cast<long long>(den.size()) - (numText < den ? 1 : 0) + 2;
                }
                auto const num = digits_of(pow10(-e) - c);
                return static_cast<long long>(num.size()) + e - (num < std::string{ "231" } ? 1 : 0) - 1;
            }

            long long ln_exp_bound(dec const& d)
            {
                auto const adj = d.exp + static_cast<long long>(d.digits.size()) - 1;
                if (adj >= 1)
                    return static_cast<long long>(std::to_string(adj * 23 / 10).size()) - 1;
                if (adj <= -2)
                    return static_cast<long long>(std::to_string((-1 - adj) * 23 / 10).size()) - 1;
                auto const c = d.coefficient();
                auto const e = d.exp;
                if (adj == 0)
                {
                    auto const numValue = c - pow10(-e);
                    std::string const num = (big_negative(numValue) ? "-" : "") + digits_of(numValue);
                    auto const den = digits_of(c);
                    return static_cast<long long>(num.size()) - static_cast<long long>(den.size()) - (num < den ? 1 : 0);
                }
                auto const num = digits_of(pow10(-e) - c);
                return static_cast<long long>(num.size()) + e - (num < std::string{ "1" } ? 1 : 0) - 1;
            }

            // (ln, log10 and exp: correctly rounded (round half even))
            dec finish_transcendental(dec ans, context_state& c)
            {
                auto const rounding = c.rounding;
                c.rounding = RoundHalfEven;
                try
                {
                    ans = fix(ans, c);
                }
                catch (...)
                {
                    c.rounding = rounding;
                    throw;
                }
                c.rounding = rounding;
                return ans;
            }

            bool needs_more(B const& aCoefficient, long long p)
            {
                auto const n = static_cast<long long>(length_of(aCoefficient));
                if (n - p - 1 < 0)
                    return true;
                return big_zero(floormod(big_abs(aCoefficient), big(5) * pow10(n - p - 1)));
            }

            dec ln(dec self, context_state& c)
            {
                if (auto const n = check_nans(self, nullptr, c))
                    return *n;
                if (!self.truth())
                    return infinity_of(1);
                if (self.infinity() == 1)
                    return self;
                if (self.sign == 0 && self.digits == "1" && self.exp == 0)
                    return dec{};
                if (cmp(self, triple(0, "1", 0)) == 0)
                    return dec{};
                if (self.sign == 1)
                    return raise_error(c, InvalidOperation, "InvalidOperation");
                auto const p = c.prec;
                auto places = p - ln_exp_bound(self) + 2;
                B coeff;
                for (;;)
                {
                    coeff = dlog(self.coefficient(), self.exp, places);
                    if (!needs_more(coeff, p))
                        break;
                    places += 3;
                }
                return finish_transcendental(triple(big_negative(coeff) ? 1 : 0, big_abs(coeff), -places), c);
            }

            dec log10(dec self, context_state& c)
            {
                if (auto const n = check_nans(self, nullptr, c))
                    return *n;
                if (!self.truth())
                    return infinity_of(1);
                if (self.infinity() == 1)
                    return self;
                if (self.sign == 1)
                    return raise_error(c, InvalidOperation, "InvalidOperation");
                dec ans;
                if (self.digits[0] == '1' && all_zeros(self.digits, 1u))
                {
                    auto const v = self.exp + static_cast<long long>(self.digits.size()) - 1;
                    ans = triple(v < 0 ? 1 : 0, std::to_string(v < 0 ? -v : v), 0);
                }
                else
                {
                    auto const p = c.prec;
                    auto places = p - log10_exp_bound(self) + 2;
                    B coeff;
                    for (;;)
                    {
                        coeff = dlog10(self.coefficient(), self.exp, places);
                        if (!needs_more(coeff, p))
                            break;
                        places += 3;
                    }
                    ans = triple(big_negative(coeff) ? 1 : 0, big_abs(coeff), -places);
                }
                return finish_transcendental(ans, c);
            }

            dec exp_of(dec self, context_state& c)
            {
                if (auto const n = check_nans(self, nullptr, c))
                    return *n;
                if (self.infinity() == -1)
                    return dec{};
                if (!self.truth())
                    return triple(0, "1", 0);
                if (self.infinity() == 1)
                    return self;
                auto const p = c.prec;
                auto const adj = self.adjusted();
                dec ans;
                if (self.sign == 0 && adj > static_cast<long long>(std::to_string((c.Emax + 1) * 3).size()))
                    ans = triple(0, "1", c.Emax + 1);
                else if (self.sign == 1 && adj > static_cast<long long>(std::to_string((-c.Etiny() + 1) * 3).size()))
                    ans = triple(0, "1", c.Etiny() - 1);
                else if (self.sign == 0 && adj < -p)
                    ans = triple(0, "1" + std::string(static_cast<std::size_t>(p - 1), '0') + "1", -p);
                else if (self.sign == 1 && adj < -p - 1)
                    ans = triple(0, std::string(static_cast<std::size_t>(p + 1), '9'), -p - 1);
                else
                {
                    auto cc = self.coefficient();
                    if (self.sign == 1)
                        cc = -cc;
                    long long extra = 3;
                    for (;;)
                    {
                        auto [coeff, e] = dexp(cc, self.exp, p + extra);
                        if (!needs_more(coeff, p))
                        {
                            ans = triple(0, coeff, e);
                            break;
                        }
                        extra += 3;
                    }
                }
                return finish_transcendental(ans, c);
            }

            dec sqrt(dec self, context_state& c)
            {
                if (self.is_special())
                {
                    if (auto const n = check_nans(self, nullptr, c))
                        return *n;
                    if (self.infinity() == 1)
                        return self;
                }
                if (!self.truth())
                    return fix(triple(self.sign, "0", self.exp >= 0 ? self.exp / 2 : -((-self.exp + 1) / 2)), c);
                if (self.sign == 1)
                    return raise_error(c, InvalidOperation, "InvalidOperation");
                auto const prec = c.prec + 1;
                // (e = exp >> 1 (floor))
                auto e = self.exp >= 0 ? self.exp / 2 : -((-self.exp + 1) / 2);
                B cc;
                long long l = 0;
                if ((self.exp & 1) != 0)
                {
                    cc = self.coefficient() * big(10);
                    l = (static_cast<long long>(self.digits.size()) >> 1) + 1;
                }
                else
                {
                    cc = self.coefficient();
                    l = (static_cast<long long>(self.digits.size()) + 1) >> 1;
                }
                auto const shift = prec - l;
                bool exact = true;
                if (shift >= 0)
                    cc = cc * pow10(2 * shift);
                else
                {
                    B remainder;
                    big_divmod(cc, pow10(-2 * shift), cc, remainder);
                    exact = big_zero(remainder);
                }
                e -= shift;
                auto n = pow10(prec);
                for (;;)
                {
                    auto const q = floordiv(cc, n);
                    if (!(q < n))
                        break;
                    n = floordiv(n + q, big(2));
                }
                exact = exact && n * n == cc;
                if (exact)
                {
                    if (shift >= 0)
                        n = floordiv(n, pow10(shift));
                    else
                        n = n * pow10(-shift);
                    e += shift;
                }
                else if (big_zero(floormod(n, big(5))))
                    n = n + big(1);
                return finish_transcendental(triple(0, n, e), c);
            }

            // (next_minus and next_plus: the largest representable number smaller than it, or the smallest larger)
            dec next_toward_infinity(dec self, context_state& c, int aDirection)
            {
                if (auto const n = check_nans(self, nullptr, c))
                    return *n;
                if (self.infinity() == aDirection)
                    return self;
                if (self.infinity() == -aDirection)
                    return triple(aDirection > 0 ? 1 : 0, std::string(static_cast<std::size_t>(c.prec), '9'), c.Etop());
                auto copy = c;
                copy.rounding = aDirection > 0 ? RoundCeiling : RoundFloor;
                copy.traps = 0u;
                auto const fixed = fix(self, copy);
                if (cmp(fixed, self) != 0)
                    return fixed;
                auto const step = triple(0, "1", copy.Etiny() - 1);
                return add(self, aDirection > 0 ? step : negate(step), copy);
            }

            dec next_toward(dec self, dec other, context_state& c)
            {
                if (auto const n = check_nans(self, &other, c))
                    return *n;
                auto const comparison = cmp(self, other);
                if (comparison == 0)
                {
                    self.sign = other.sign;
                    return self;
                }
                auto const ans = next_toward_infinity(self, c, comparison < 0 ? 1 : -1);
                if (ans.infinity() != 0)
                {
                    raise_error(c, Overflow, "Overflow", ans.sign);
                    raise_error(c, Inexact, "Inexact");
                    raise_error(c, Rounded, "Rounded");
                }
                else if (ans.adjusted() < c.Emin)
                {
                    raise_error(c, Underflow, "Underflow");
                    raise_error(c, Subnormal, "Subnormal");
                    raise_error(c, Inexact, "Inexact");
                    raise_error(c, Rounded, "Rounded");
                    if (!ans.truth())
                        raise_error(c, Clamped, "Clamped");
                }
                return ans;
            }

            dec power(dec self, dec other, context_state& c)
            {
                if (auto const n = check_nans(self, &other, c))
                    return *n;
                if (!other.truth())
                {
                    if (!self.truth())
                        return raise_error(c, InvalidOperation, "InvalidOperation");
                    return triple(0, "1", 0);
                }
                int resultSign = 0;
                if (self.sign == 1)
                {
                    if (is_integer_value(other))
                    {
                        if (!is_even(other))
                            resultSign = 1;
                    }
                    else if (self.truth())
                        return raise_error(c, InvalidOperation, "InvalidOperation");
                    self = negate(self);
                }
                if (!self.truth())
                    return other.sign == 0 ? triple(resultSign, "0", 0) : infinity_of(resultSign);
                if (self.infinity() != 0)
                    return other.sign == 0 ? infinity_of(resultSign) : triple(resultSign, "0", 0);
                if (cmp(self, triple(0, "1", 0)) == 0)
                {
                    long long exp = 0;
                    if (is_integer_value(other))
                    {
                        long long multiplier = 0;
                        if (other.sign == 1)
                            multiplier = 0;
                        else if (cmp(other, triple(0, std::to_string(c.prec), 0)) > 0)
                            multiplier = c.prec;
                        else
                            multiplier = std::stoll(digits_of(floordiv(other.coefficient() * pow10(std::max(0ll, other.exp)), pow10(std::max(0ll, -other.exp)))));
                        exp = self.exp * multiplier;
                        if (exp < 1 - c.prec)
                        {
                            exp = 1 - c.prec;
                            raise_error(c, Rounded, "Rounded");
                        }
                    }
                    else
                    {
                        raise_error(c, Inexact, "Inexact");
                        raise_error(c, Rounded, "Rounded");
                        exp = 1 - c.prec;
                    }
                    return triple(resultSign, "1" + std::string(static_cast<std::size_t>(-exp), '0'), exp);
                }
                auto const selfAdj = self.adjusted();
                if (other.infinity() != 0)
                {
                    if ((other.sign == 0) == (selfAdj < 0))
                        return triple(resultSign, "0", 0);
                    return infinity_of(resultSign);
                }
                // (an integer exponent: exact (then rounded); else exp(y ln x))
                if (is_integer_value(other) && other.adjusted() < 9)
                {
                    auto const y = floordiv(other.coefficient() * pow10(std::max(0ll, other.exp)), pow10(std::max(0ll, -other.exp)));
                    auto const yn = std::stoll(digits_of(y));
                    // (an overflowing or underflowing result: its bound first)
                    auto const bound = static_cast<long double>(selfAdj + 1) * static_cast<long double>(yn);
                    if (bound > static_cast<long double>(c.Emax) * 2 + 10 || (bound < static_cast<long double>(c.Etiny()) * 2 - 10 && other.sign == 0))
                    {
                        auto const ans = other.sign == 0 && selfAdj >= 0 ? triple(resultSign, "1", c.Emax + 1) : triple(resultSign, "1", c.Etiny() - 1);
                        return fix(ans, c);
                    }
                    B coefficient{ 1 };
                    B base = self.coefficient();
                    auto n = yn;
                    while (n > 0)
                    {
                        if ((n & 1) != 0)
                            coefficient = coefficient * base;
                        base = base * base;
                        n >>= 1;
                    }
                    dec exact = triple(resultSign, coefficient, self.exp * yn);
                    if (other.sign == 0)
                        return fix(exact, c);
                    // (a negative exponent: 1 / x**n, correctly rounded)
                    return divide(triple(0, "1", 0), exact, c);
                }
                auto const p = c.prec;
                auto yc = other.coefficient();
                if (other.sign == 1)
                    yc = -yc;
                long long extra = 3;
                dec ans;
                for (;;)
                {
                    auto [coeff, exp] = dpower(self.coefficient(), self.exp, yc, other.exp, p + extra);
                    if (!needs_more(coeff, p))
                    {
                        ans = triple(resultSign, coeff, exp);
                        break;
                    }
                    extra += 3;
                }
                return fix(ans, c);
            }

            dec quantize(dec self, dec e, int aRounding, context_state& c)
            {
                if (self.is_special() || e.is_special())
                {
                    if (auto const n = check_nans(self, &e, c))
                        return *n;
                    if (e.infinity() != 0 || self.infinity() != 0)
                    {
                        if (e.infinity() != 0 && self.infinity() != 0)
                            return self;
                        return raise_error(c, InvalidOperation, "InvalidOperation");
                    }
                }
                if (!(c.Etiny() <= e.exp && e.exp <= c.Emax))
                    return raise_error(c, InvalidOperation, "InvalidOperation");
                if (!self.truth())
                    return fix(triple(self.sign, "0", e.exp), c);
                auto const selfAdjusted = self.adjusted();
                if (selfAdjusted > c.Emax)
                    return raise_error(c, InvalidOperation, "InvalidOperation");
                if (selfAdjusted - e.exp + 1 > c.prec)
                    return raise_error(c, InvalidOperation, "InvalidOperation");
                auto ans = rescale(self, e.exp, aRounding);
                if (ans.adjusted() > c.Emax)
                    return raise_error(c, InvalidOperation, "InvalidOperation");
                if (static_cast<long long>(ans.digits.size()) > c.prec)
                    return raise_error(c, InvalidOperation, "InvalidOperation");
                if (ans.truth() && ans.adjusted() < c.Emin)
                    raise_error(c, Subnormal, "Subnormal");
                if (ans.exp > self.exp)
                {
                    if (cmp(ans, self) != 0)
                        raise_error(c, Inexact, "Inexact");
                    raise_error(c, Rounded, "Rounded");
                }
                return fix(ans, c);
            }

            dec normalize(dec self, context_state& c)
            {
                if (self.is_special())
                    if (auto const n = check_nans(self, nullptr, c))
                        return *n;
                auto dup = fix(self, c);
                if (dup.infinity() != 0)
                    return dup;
                if (!dup.truth())
                    return triple(dup.sign, "0", 0);
                auto const expMax = c.clamp != 0 ? c.Etop() : c.Emax;
                auto end = dup.digits.size();
                auto exp = dup.exp;
                while (dup.digits[end - 1u] == '0' && exp < expMax)
                {
                    ++exp;
                    --end;
                }
                return triple(dup.sign, dup.digits.substr(0u, end), exp);
            }

            std::string to_string(dec const& d, bool aEngineering, context_state const& c)
            {
                std::string const sign = d.sign != 0 ? "-" : "";
                if (d.is_special())
                {
                    if (d.special == 'F')
                        return sign + "Infinity";
                    return sign + (d.special == 'n' ? "NaN" : "sNaN") + d.digits;
                }
                auto const leftdigits = d.exp + static_cast<long long>(d.digits.size());
                long long dotplace = 0;
                if (d.exp <= 0 && leftdigits > -6)
                    dotplace = leftdigits;
                else if (!aEngineering)
                    dotplace = 1;
                else if (d.digits == "0")
                    dotplace = ((leftdigits + 1) % 3 + 3) % 3 - 1;
                else
                    dotplace = ((leftdigits - 1) % 3 + 3) % 3 + 1;
                std::string intpart, fracpart;
                auto const n = static_cast<long long>(d.digits.size());
                if (dotplace <= 0)
                {
                    intpart = "0";
                    fracpart = "." + std::string(static_cast<std::size_t>(-dotplace), '0') + d.digits;
                }
                else if (dotplace >= n)
                    intpart = d.digits + std::string(static_cast<std::size_t>(dotplace - n), '0');
                else
                {
                    intpart = d.digits.substr(0u, static_cast<std::size_t>(dotplace));
                    fracpart = "." + d.digits.substr(static_cast<std::size_t>(dotplace));
                }
                std::string exp;
                if (leftdigits != dotplace)
                {
                    auto const e = leftdigits - dotplace;
                    exp = std::string{ c.capitals != 0 ? "E" : "e" } + (e >= 0 ? "+" : "-") + std::to_string(e >= 0 ? e : -e);
                }
                return sign + intpart + fracpart + exp;
            }

            // --- Decimal objects

            struct decimal_object
            {
                header h;
                dec value;
            };

            neos_py_type make_decimal_type();
            neos_py_type sDecimalType = make_decimal_type();
            bool is_decimal(object* aObject) { return aObject != nullptr && aObject->type == &sDecimalType.base; }
            dec const& value_of(object* aObject) { return reinterpret_cast<decimal_object*>(aObject)->value; }
            object* make_decimal(dec aValue) { return &(new decimal_object{ { { &sDecimalType.base } }, std::move(aValue) })->h.base; }

            std::optional<dec> parse(std::string aText)
            {
                // ([sign] (digits[.digits] | .digits) [e[sign]digits] | Inf[inity] | [s]NaN[digits]; case insensitive, underscores between digits, whitespace)
                auto const first = aText.find_first_not_of(" \t\n\r\f\v");
                if (first == std::string::npos)
                    return {};
                aText = aText.substr(first, aText.find_last_not_of(" \t\n\r\f\v") - first + 1u);
                std::string text;
                for (std::size_t i = 0u; i < aText.size(); ++i)
                {
                    if (aText[i] == '_')
                    {
                        if (i == 0u || i + 1u >= aText.size() || !std::isdigit(static_cast<unsigned char>(aText[i - 1u])) || !std::isdigit(static_cast<unsigned char>(aText[i + 1u])))
                            return {};
                        continue;
                    }
                    text += aText[i];
                }
                std::size_t i = 0u;
                int sign = 0;
                if (i < text.size() && (text[i] == '+' || text[i] == '-'))
                    sign = text[i++] == '-' ? 1 : 0;
                std::string lower;
                for (auto c : text.substr(i))
                    lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                if (lower == "inf" || lower == "infinity")
                    return infinity_of(sign);
                if (lower.starts_with("nan") || lower.starts_with("snan"))
                {
                    auto const payload = lower.substr(lower[0] == 's' ? 4u : 3u);
                    if (!std::all_of(payload.begin(), payload.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; }))
                        return {};
                    auto const p = payload.find_first_not_of('0');
                    return dec{ sign, p == std::string::npos ? std::string{} : payload.substr(p), 0, lower[0] == 's' ? 'N' : 'n' };
                }
                std::string intpart, fracpart;
                while (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i])))
                    intpart += text[i++];
                if (i < text.size() && text[i] == '.')
                {
                    ++i;
                    while (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i])))
                        fracpart += text[i++];
                }
                if (intpart.empty() && fracpart.empty())
                    return {};
                long long exp = 0;
                if (i < text.size() && (text[i] == 'e' || text[i] == 'E'))
                {
                    ++i;
                    bool negative = false;
                    if (i < text.size() && (text[i] == '+' || text[i] == '-'))
                        negative = text[i++] == '-';
                    std::string digits;
                    while (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i])))
                        digits += text[i++];
                    if (digits.empty() || digits.size() > 17u)
                        return {};
                    exp = std::stoll(digits);
                    if (negative)
                        exp = -exp;
                }
                if (i != text.size())
                    return {};
                return triple(sign, intpart + fracpart, exp - static_cast<long long>(fracpart.size()));
            }

            dec from_double(double aValue)
            {
                if (std::isnan(aValue))
                    return dec{ std::signbit(aValue) ? 1 : 0, "", 0, 'n' };
                if (std::isinf(aValue))
                    return infinity_of(aValue < 0 ? 1 : 0);
                int const sign = std::signbit(aValue) ? 1 : 0;
                ref const f{ make_float(std::fabs(aValue)) };
                ref const ratio{ call_method_named(f.get(), "as_integer_ratio", {}) };
                auto const& items = as<tuple_object>(ratio.get())->items;
                auto const n = big_value(items[0]);
                auto const d = big_value(items[1]);
                auto const k = static_cast<long long>(big_bit_length(d)) - 1;
                B five{ 5 };
                B power{ 1 };
                for (long long i = 0; i < k; ++i)
                    power = power * five;
                return triple(sign, n * power, -k);
            }

            // (an operand: a Decimal or an int (null: neither))
            std::optional<dec> operand(object* aValue)
            {
                if (is_decimal(aValue))
                    return value_of(aValue);
                auto const v = builtin_value(aValue);
                if (kind_of(v) == NEOS_PY_INT || kind_of(v) == NEOS_PY_BOOL)
                {
                    auto const b = big_value(v);
                    return triple(big_negative(b) ? 1 : 0, big_abs(b), 0);
                }
                return {};
            }

            // (a Decimal from a constructor's argument)
            bool convert(object* aValue, context_state& c, dec& aResult)
            {
                if (auto const d = operand(aValue))
                {
                    aResult = *d;
                    return true;
                }
                if (kind_of(aValue) == NEOS_PY_STR)
                {
                    auto const parsed = parse(as<str_object>(aValue)->value);
                    if (!parsed)
                    {
                        aResult = raise_error(c, InvalidOperation, "ConversionSyntax");
                        return true;
                    }
                    aResult = *parsed;
                    return true;
                }
                if (kind_of(aValue) == NEOS_PY_FLOAT)
                {
                    raise_error(c, FloatOperation, "FloatOperation");
                    aResult = from_double(as<float_object>(aValue)->value);
                    return true;
                }
                if (kind_of(aValue) == NEOS_PY_TUPLE || kind_of(aValue) == NEOS_PY_LIST)
                {
                    std::vector<object*> const items = kind_of(aValue) == NEOS_PY_TUPLE ? as<tuple_object>(aValue)->items : as<list_object>(aValue)->items;
                    if (items.size() != 3u)
                    {
                        error("ValueError", "argument must be a sequence of length 3");
                        return false;
                    }
                    long long sign = 0;
                    if (!is_int_like(items[0]) || !to_index(items[0], sign) || (sign != 0 && sign != 1))
                    {
                        clear_error();
                        error("ValueError", "sign must be an integer with the value 0 or 1");
                        return false;
                    }
                    std::string digits;
                    std::vector<object*> digitItems;
                    if (!collect(items[1], digitItems))
                        return false;
                    for (auto d : digitItems)
                    {
                        long long v = -1;
                        if (!is_int_like(d) || !to_index(d, v) || v < 0 || v > 9)
                        {
                            for (auto x : digitItems)
                                release(x);
                            clear_error();
                            error("ValueError", "coefficient must be a tuple of digits");
                            return false;
                        }
                        digits += static_cast<char>('0' + v);
                    }
                    for (auto x : digitItems)
                        release(x);
                    if (kind_of(items[2]) == NEOS_PY_STR)
                    {
                        auto const& e = as<str_object>(items[2])->value;
                        if (e == "F")
                            aResult = infinity_of(static_cast<int>(sign));
                        else if (e == "n" || e == "N")
                        {
                            auto const p = digits.find_first_not_of('0');
                            aResult = dec{ static_cast<int>(sign), p == std::string::npos ? std::string{} : digits.substr(p), 0, e[0] };
                        }
                        else
                        {
                            error("ValueError", "string argument in the third position must be 'F', 'n' or 'N'");
                            return false;
                        }
                        return true;
                    }
                    long long exp = 0;
                    if (!is_int_like(items[2]) || !to_index(items[2], exp))
                    {
                        clear_error();
                        error("ValueError", "exponent must be an integer");
                        return false;
                    }
                    aResult = triple(static_cast<int>(sign), digits.empty() ? std::string{ "0" } : digits, exp);
                    return true;
                }
                error("TypeError", std::string{ "conversion from " } + type_name(aValue) + " to Decimal is not supported");
                return false;
            }

            // (a call of a Decimal operation: trapped signals' exceptions)
            template <typename F>
            object* guarded(F&& aFunction)
            {
                try
                {
                    return aFunction();
                }
                catch (trapped const&)
                {
                    return nullptr;
                }
            }

            object* hash_value(dec const& d, long long& aResult)
            {
                if (d.is_special())
                {
                    if (d.special == 'N')
                        return error("TypeError", "Cannot hash a signaling NaN value.");
                    aResult = d.special == 'n' ? 0 : (d.sign != 0 ? -314159 : 314159);
                    return none();
                }
                static B const modulus{ 2305843009213693951ll };
                // (10 ** exp mod modulus; for a negative exponent, the inverse of 10 (10**(modulus-2)))
                auto const modpow = [](B base, long long e)
                {
                    B result{ 1 };
                    base = floormod(base, modulus);
                    while (e > 0)
                    {
                        if ((e & 1) != 0)
                            result = floormod(result * base, modulus);
                        base = floormod(base * base, modulus);
                        e >>= 1;
                    }
                    return result;
                };
                static B const inverse10 = [&]()
                {
                    B result{ 1 };
                    B base{ 10 };
                    auto e = 2305843009213693951ll - 2;
                    while (e > 0)
                    {
                        if ((e & 1) != 0)
                            result = floormod(result * base, modulus);
                        base = floormod(base * base, modulus);
                        e >>= 1;
                    }
                    return result;
                }();
                auto const expHash = d.exp >= 0 ? modpow(B{ 10 }, d.exp) : modpow(inverse10, -d.exp);
                auto const h = floormod(d.coefficient() * expHash, modulus);
                auto value = std::stoll(digits_of(h));
                if (d.sign != 0 && d.truth())
                    value = -value;
                aResult = value == -1 ? -2 : value;
                return none();
            }

            std::string format_decimal(dec self, std::string const& aSpec, context_state& c);

            object* to_int(dec const& d)
            {
                if (d.is_special())
                {
                    if (d.is_nan())
                        return error("ValueError", "cannot convert NaN to integer");
                    return error("OverflowError", "cannot convert Infinity to integer");
                }
                B value;
                if (d.exp >= 0)
                    value = d.coefficient() * pow10(d.exp);
                else
                {
                    auto const keep = static_cast<long long>(d.digits.size()) + d.exp;
                    value = keep <= 0 ? B{ 0 } : from_digits(d.digits.substr(0u, static_cast<std::size_t>(keep)));
                }
                if (d.sign != 0)
                    value = -value;
                return make_int(value);
            }

            double to_double(dec const& d)
            {
                if (d.is_nan())
                    return d.sign != 0 ? -std::nan("") : std::nan("");
                context_state c;
                auto const text = to_string(d, false, c);
                return std::strtod(text.c_str(), nullptr);
            }

            bool rounding_of(object* aValue, int& aResult)
            {
                if (aValue == nullptr || kind_of(aValue) == NEOS_PY_NONE)
                {
                    aResult = current().rounding;
                    return true;
                }
                if (kind_of(aValue) == NEOS_PY_STR)
                    for (int i = 0; i < 8; ++i)
                        if (as<str_object>(aValue)->value == sRoundingNames[i])
                        {
                            aResult = i;
                            return true;
                        }
                error("TypeError", "valid values for rounding are:\n  [ROUND_CEILING, ROUND_FLOOR, ROUND_UP, ROUND_DOWN,\n   ROUND_HALF_UP, ROUND_HALF_DOWN, ROUND_HALF_EVEN,\n   ROUND_05UP]");
                return false;
            }

            context_state& context_of(object* aContext)
            {
                return aContext != nullptr && is_context(aContext) ? as_context(aContext).state : current();
            }

            // (DecimalTuple)
            object* decimal_tuple(dec const& d)
            {
                static object* sClass = nullptr;
                if (sClass == nullptr)
                {
                    ref const collections{ fn_import("collections") };
                    ref const namedtuple{ getattr(collections.get(), "namedtuple") };
                    ref const kwargs{ make_dict() };
                    ref const moduleKey{ make_str("module") };
                    ref const moduleName{ make_str("decimal") };
                    dict_set(kwargs.get(), moduleKey.get(), moduleName.get());
                    sClass = call_with(namedtuple.get(), { make_str("DecimalTuple"), make_str("sign digits exponent") }, kwargs.get());
                    if (sClass == nullptr)
                        return nullptr;
                }
                std::vector<object*> digits;
                for (char ch : d.digits)
                    digits.push_back(make_int(ch - '0'));
                object* exponent = d.special == 'F' ? make_str("F") : d.special == 'n' ? make_str("n") : d.special == 'N' ? make_str("N") : make_int(d.exp);
                if (d.special == 'F')
                {
                    for (auto x : digits)
                        release(x);
                    digits = { make_int(0) };
                }
                return call_with(sClass, { make_int(d.sign), make_tuple(std::move(digits)), exponent });
            }

            object* as_integer_ratio(dec const& d)
            {
                if (d.is_special())
                {
                    if (d.is_nan())
                        return error("ValueError", "cannot convert NaN to integer ratio");
                    return error("OverflowError", "cannot convert Infinity to integer ratio");
                }
                if (!d.truth())
                    return make_tuple({ make_int(0), make_int(1) });
                auto n = d.coefficient();
                B den{ 1 };
                if (d.exp >= 0)
                    n = n * pow10(d.exp);
                else
                {
                    den = pow10(-d.exp);
                    // (reduced by the gcd)
                    auto a = n, b = den;
                    while (!big_zero(b))
                    {
                        auto const r = floormod(a, b);
                        a = b;
                        b = r;
                    }
                    n = floordiv(n, a);
                    den = floordiv(den, a);
                }
                if (d.sign != 0)
                    n = -n;
                return make_tuple({ make_int(n), make_int(den) });
            }

            using unary_method = dec (*)(dec, context_state&);

            struct method_entry { char const* name; int kind; };

            neos_py_type make_decimal_type()
            {
                auto type = make_type("decimal.Decimal", "Decimal", retain_object, release_object<decimal_object>);
                type.repr = [](neos_py_context*, object* aSelf) -> object* { return make_str("Decimal('" + to_string(value_of(aSelf), false, current()) + "')"); };
                type.str = [](neos_py_context*, object* aSelf) -> object* { return make_str(to_string(value_of(aSelf), false, current())); };
                type.binary = [](neos_py_context*, int aOperation, object* aLhs, object* aRhs, int aInplace) -> object*
                {
                    auto const a = operand(aLhs);
                    auto const b = operand(aRhs);
                    if (!a || !b)
                    {
                        // (a builtin operand: CPython's message (the type's qualified name))
                        auto const other = !a ? aLhs : aRhs;
                        auto const k = kind_of(other);
                        if (k == NEOS_PY_FLOAT || k == NEOS_PY_STR || k == NEOS_PY_NONE || k == NEOS_PY_LIST || k == NEOS_PY_TUPLE || k == NEOS_PY_DICT)
                        {
                            static char const* const sSymbols[] = { "+", "-", "*", "/", "//", "%", "**" };
                            int const operations[] = { NEOS_PY_ADD, NEOS_PY_SUBTRACT, NEOS_PY_MULTIPLY, NEOS_PY_TRUE_DIVIDE, NEOS_PY_FLOOR_DIVIDE, NEOS_PY_REMAINDER, NEOS_PY_POWER };
                            for (std::size_t i = 0u; i < std::size(operations); ++i)
                                if (operations[i] == aOperation)
                                    return error("TypeError", std::string{ "unsupported operand type(s) for " } + sSymbols[i] + (aInplace != 0 ? "=" : "") + ": '" + 
                                        (!a ? type_name(aLhs) : "decimal.Decimal") + "' and '" + (!b ? type_name(aRhs) : "decimal.Decimal") + "'");
                        }
                        return borrowed(not_implemented());
                    }
                    return guarded([&]() -> object*
                    {
                        auto& c = current();
                        switch (aOperation)
                        {
                        case NEOS_PY_ADD: return make_decimal(add(*a, *b, c));
                        case NEOS_PY_SUBTRACT:
                            {
                                if (a->is_special() || b->is_special())
                                    if (auto const n = check_nans(*a, &*b, c))
                                        return make_decimal(*n);
                                return make_decimal(add(*a, negate(*b), c));
                            }
                        case NEOS_PY_MULTIPLY: return make_decimal(multiply(*a, *b, c));
                        case NEOS_PY_TRUE_DIVIDE: return make_decimal(divide(*a, *b, c));
                        case NEOS_PY_FLOOR_DIVIDE: return make_decimal(floor_divide(*a, *b, c));
                        case NEOS_PY_REMAINDER: return make_decimal(modulo(*a, *b, c));
                        case NEOS_PY_POWER: return make_decimal(power(*a, *b, c));
                        default: return borrowed(not_implemented());
                        }
                    });
                };
                type.unary = [](neos_py_context*, int aOperation, object* aSelf) -> object*
                {
                    auto const& d = value_of(aSelf);
                    return guarded([&]() -> object*
                    {
                        auto& c = current();
                        if (d.is_special())
                            if (auto const n = check_nans(d, nullptr, c))
                                return make_decimal(*n);
                        switch (aOperation)
                        {
                        case NEOS_PY_NEGATIVE: return make_decimal(fix(!d.truth() && c.rounding != RoundFloor ? absolute(d) : negate(d), c));
                        case NEOS_PY_POSITIVE: return make_decimal(fix(!d.truth() && c.rounding != RoundFloor ? absolute(d) : d, c));
                        case NEOS_PY_ABSOLUTE:
                            if (d.sign != 0)
                                return make_decimal(fix(!d.truth() && c.rounding != RoundFloor ? absolute(d) : negate(d), c));
                            return make_decimal(fix(!d.truth() && c.rounding != RoundFloor ? absolute(d) : d, c));
                        default: return error("TypeError", "bad operand type for unary ~: 'decimal.Decimal'");
                        }
                    });
                };
                type.compare = [](neos_py_context*, int aOperation, object* aLhs, object* aRhs) -> object*
                {
                    // (with a float: its exact value (FloatOperation))
                    std::optional<dec> a = operand(aLhs);
                    std::optional<dec> b = operand(aRhs);
                    return guarded([&]() -> object*
                    {
                        auto& c = current();
                        if (!a && kind_of(aLhs) == NEOS_PY_FLOAT)
                        {
                            if (aOperation != NEOS_PY_EQ && aOperation != NEOS_PY_NE)
                                raise_error(c, FloatOperation, "FloatOperation");
                            else
                                c.flags |= FloatOperation;
                            a = from_double(as<float_object>(aLhs)->value);
                        }
                        if (!b && kind_of(aRhs) == NEOS_PY_FLOAT)
                        {
                            if (aOperation != NEOS_PY_EQ && aOperation != NEOS_PY_NE)
                                raise_error(c, FloatOperation, "FloatOperation");
                            else
                                c.flags |= FloatOperation;
                            b = from_double(as<float_object>(aRhs)->value);
                        }
                        if (!a || !b)
                        {
                            // (a Fraction: compared by its value)
                            auto const other = !a ? aLhs : aRhs;
                            if (std::string_view{ other->type->name } == "fractions.Fraction")
                            {
                                ref const ratio{ call_method_named(is_decimal(aLhs) ? aLhs : aRhs, "as_integer_ratio", {}) };
                                if (!ratio)
                                {
                                    clear_error();
                                    return borrowed(not_implemented());
                                }
                                ref const fractionType{ getattr(ref{ fn_import("fractions") }.get(), "Fraction") };
                                ref const exact{ call(fractionType.get(), ratio.get(), nullptr) };
                                return is_decimal(aLhs) ? compare(aOperation, exact.get(), aRhs) : compare(aOperation, aLhs, exact.get());
                            }
                            return borrowed(not_implemented());
                        }
                        if (a->is_nan() || b->is_nan())
                        {
                            if (aOperation == NEOS_PY_EQ || aOperation == NEOS_PY_NE)
                            {
                                if (a->special == 'N' || b->special == 'N')
                                    raise_error(c, InvalidOperation, "InvalidOperation");
                                return make_bool(aOperation == NEOS_PY_NE);
                            }
                            raise_error(c, InvalidOperation, "InvalidOperation");
                            return make_bool(false);
                        }
                        auto const r = cmp(*a, *b);
                        switch (aOperation)
                        {
                        case NEOS_PY_LT: return make_bool(r < 0);
                        case NEOS_PY_LE: return make_bool(r <= 0);
                        case NEOS_PY_EQ: return make_bool(r == 0);
                        case NEOS_PY_NE: return make_bool(r != 0);
                        case NEOS_PY_GT: return make_bool(r > 0);
                        default: return make_bool(r >= 0);
                        }
                    });
                };
                type.hash = [](neos_py_context*, object* aSelf, long long* aResult) -> int
                {
                    if (value_of(aSelf).special == 'n')
                    {
                        *aResult = static_cast<long long>(reinterpret_cast<std::uintptr_t>(aSelf) >> 4);
                        return 0;
                    }
                    return hash_value(value_of(aSelf), *aResult) != nullptr ? 0 : -1;
                };
                type.truth = [](neos_py_context*, object* aSelf) -> int { return value_of(aSelf).truth() ? 1 : 0; };
                type.to_float = [](neos_py_context*, object* aSelf, double* aResult) -> int
                {
                    if (value_of(aSelf).special == 'N')
                    {
                        error("ValueError", "cannot convert signaling NaN to float");
                        return -1;
                    }
                    *aResult = to_double(value_of(aSelf));
                    return 0;
                };
                type.to_int = [](neos_py_context*, object* aSelf, long long* aResult) -> int
                {
                    ref const value{ to_int(value_of(aSelf)) };
                    return value && to_index(value.get(), *aResult) ? 0 : -1;
                };
                type.getattr = [](neos_py_context*, object* aSelf, char const* aName) -> object*
                {
                    auto const& d = value_of(aSelf);
                    std::string_view const name{ aName };
                    if (name == "real")
                        return borrowed(aSelf);
                    if (name == "imag")
                        return make_decimal(dec{});
                    static char const* const sMethods[] = { "quantize", "sqrt", "exp", "ln", "log10", "normalize", "to_integral_value", "to_integral_exact",
                        "to_integral", "adjusted", "as_tuple", "as_integer_ratio", "copy_abs", "copy_negate", "copy_sign", "is_finite", "is_infinite", "is_nan",
                        "is_qnan", "is_snan", "is_signed", "is_zero", "is_normal", "is_subnormal", "compare", "max", "min", "same_quantum", "to_eng_string",
                        "__round__", "__floor__", "__ceil__", "__trunc__", "__format__", "__int__", "__float__", "fma", "remainder_near", "number_class",
                        "conjugate", "is_canonical", "canonical", "radix", "scaleb", "__copy__", "__deepcopy__", "max_mag", "min_mag", "compare_signal",
                        "next_plus", "next_minus", "next_toward" };
                    for (std::size_t i = 0u; i < std::size(sMethods); ++i)
                        if (name == sMethods[i])
                            return make_function(sMethods[i], [](neos_py_context*, void* aData, object* aArgs, object* aKwargs) -> object*
                            {
                                auto const& args = as<tuple_object>(aArgs)->items;
                                auto const self = value_of(args[0]);
                                std::vector<object*> const rest{ args.begin() + 1, args.end() };
                                auto const which = reinterpret_cast<std::uintptr_t>(aData);
                                static char const* const sNames[] = { "quantize", "sqrt", "exp", "ln", "log10", "normalize", "to_integral_value", "to_integral_exact",
                                    "to_integral", "adjusted", "as_tuple", "as_integer_ratio", "copy_abs", "copy_negate", "copy_sign", "is_finite", "is_infinite", "is_nan",
                                    "is_qnan", "is_snan", "is_signed", "is_zero", "is_normal", "is_subnormal", "compare", "max", "min", "same_quantum", "to_eng_string",
                                    "__round__", "__floor__", "__ceil__", "__trunc__", "__format__", "__int__", "__float__", "fma", "remainder_near", "number_class",
                                    "conjugate", "is_canonical", "canonical", "radix", "scaleb", "__copy__", "__deepcopy__", "max_mag", "min_mag", "compare_signal",
                        "next_plus", "next_minus", "next_toward" };
                                std::string const methodName = sNames[which];
                                // (an operand argument: a Decimal or an int)
                                auto const other_of = [&](object* aValue, dec& aResult) -> bool
                                {
                                    if (auto const o = operand(aValue))
                                    {
                                        aResult = *o;
                                        return true;
                                    }
                                    std::string text;
                                    repr_of(aValue, text);
                                    error("TypeError", "conversion from " + std::string{ type_name(aValue) } + " to Decimal is not supported");
                                    return false;
                                };
                                return guarded([&]() -> object*
                                {
                                    if (methodName == "quantize")
                                    {
                                        std::vector<object*> a;
                                        if (!parse_arguments("quantize", rest, aKwargs, { "exp", "rounding", "context" }, 1u, 0u, a))
                                            return nullptr;
                                        dec e;
                                        int rounding = 0;
                                        if (!other_of(a[0], e))
                                            return nullptr;
                                        auto& c = context_of(a[2]);
                                        if (a[1] == nullptr || kind_of(a[1]) == NEOS_PY_NONE)
                                            rounding = c.rounding;
                                        else if (!rounding_of(a[1], rounding))
                                            return nullptr;
                                        return make_decimal(quantize(self, e, rounding, c));
                                    }
                                    if (methodName == "sqrt" || methodName == "exp" || methodName == "ln" || methodName == "log10" || methodName == "normalize")
                                    {
                                        std::vector<object*> a;
                                        if (!parse_arguments(sNames[which], rest, aKwargs, { "context" }, 0u, 0u, a))
                                            return nullptr;
                                        auto& c = context_of(a[0]);
                                        if (methodName == "sqrt") return make_decimal(sqrt(self, c));
                                        if (methodName == "exp") return make_decimal(exp_of(self, c));
                                        if (methodName == "ln") return make_decimal(ln(self, c));
                                        if (methodName == "log10") return make_decimal(log10(self, c));
                                        return make_decimal(normalize(self, c));
                                    }
                                    if (methodName == "next_plus" || methodName == "next_minus")
                                    {
                                        std::vector<object*> a;
                                        if (!parse_arguments(sNames[which], rest, aKwargs, { "context" }, 0u, 0u, a))
                                            return nullptr;
                                        return make_decimal(next_toward_infinity(self, context_of(a[0]), methodName == "next_plus" ? 1 : -1));
                                    }
                                    if (methodName == "next_toward")
                                    {
                                        std::vector<object*> a;
                                        if (!parse_arguments("next_toward", rest, aKwargs, { "other", "context" }, 1u, 0u, a))
                                            return nullptr;
                                        dec o;
                                        if (!other_of(a[0], o))
                                            return nullptr;
                                        return make_decimal(next_toward(self, o, context_of(a[1])));
                                    }
                                    if (methodName.starts_with("to_integral"))
                                    {
                                        std::vector<object*> a;
                                        if (!parse_arguments(sNames[which], rest, aKwargs, { "rounding", "context" }, 0u, 0u, a))
                                            return nullptr;
                                        auto& c = context_of(a[1]);
                                        int rounding = c.rounding;
                                        if (a[0] != nullptr && kind_of(a[0]) != NEOS_PY_NONE && !rounding_of(a[0], rounding))
                                            return nullptr;
                                        if (self.is_special())
                                        {
                                            if (auto const n = check_nans(self, nullptr, c))
                                                return make_decimal(*n);
                                            return make_decimal(self);
                                        }
                                        if (self.exp >= 0)
                                            return make_decimal(self);
                                        if (methodName == "to_integral_exact")
                                        {
                                            if (!self.truth())
                                                return make_decimal(triple(self.sign, "0", 0));
                                            auto const ans = rescale(self, 0, rounding);
                                            if (cmp(ans, self) != 0)
                                                raise_error(c, Inexact, "Inexact");
                                            raise_error(c, Rounded, "Rounded");
                                            return make_decimal(ans);
                                        }
                                        return make_decimal(rescale(self, 0, rounding));
                                    }
                                    if (methodName == "adjusted") return make_int(self.adjusted());
                                    if (methodName == "as_tuple") return decimal_tuple(self);
                                    if (methodName == "as_integer_ratio") return as_integer_ratio(self);
                                    if (methodName == "copy_abs") return make_decimal(absolute(self));
                                    if (methodName == "copy_negate") return make_decimal(negate(self));
                                    if (methodName == "copy_sign")
                                    {
                                        dec o;
                                        if (rest.empty() || !other_of(rest[0], o))
                                            return failed() ? nullptr : error("TypeError", "copy_sign() missing required argument 'other' (pos 1)");
                                        auto r = self;
                                        r.sign = o.sign;
                                        return make_decimal(r);
                                    }
                                    if (methodName == "is_finite") return make_bool(!self.is_special());
                                    if (methodName == "is_infinite") return make_bool(self.special == 'F');
                                    if (methodName == "is_nan") return make_bool(self.is_nan());
                                    if (methodName == "is_qnan") return make_bool(self.special == 'n');
                                    if (methodName == "is_snan") return make_bool(self.special == 'N');
                                    if (methodName == "is_signed") return make_bool(self.sign != 0);
                                    if (methodName == "is_zero") return make_bool(self.is_zero());
                                    if (methodName == "is_canonical") return make_bool(true);
                                    if (methodName == "canonical" || methodName == "__copy__" || methodName == "__deepcopy__" || methodName == "conjugate")
                                        return borrowed(args[0]);
                                    if (methodName == "radix") return make_decimal(triple(0, "10", 0));
                                    if (methodName == "is_normal") return make_bool(!self.is_special() && self.truth() && current().Emin <= self.adjusted());
                                    if (methodName == "is_subnormal") return make_bool(!self.is_special() && self.truth() && self.adjusted() < current().Emin);
                                    if (methodName == "number_class")
                                    {
                                        auto const& c = current();
                                        if (self.special == 'N') return make_str("sNaN");
                                        if (self.special == 'n') return make_str("NaN");
                                        auto const sign = std::string{ self.sign != 0 ? "-" : "+" };
                                        if (self.special == 'F') return make_str(sign + "Infinity");
                                        if (!self.truth()) return make_str(sign + "Zero");
                                        if (self.adjusted() < c.Emin) return make_str(sign + "Subnormal");
                                        return make_str(sign + "Normal");
                                    }
                                    if (methodName == "compare" || methodName == "max" || methodName == "min" || methodName == "same_quantum" || methodName == "remainder_near" ||
                                        methodName == "max_mag" || methodName == "min_mag" || methodName == "compare_signal")
                                    {
                                        std::vector<object*> a;
                                        if (!parse_arguments(sNames[which], rest, aKwargs, { "other", "context" }, 1u, 0u, a))
                                            return nullptr;
                                        dec o;
                                        if (!other_of(a[0], o))
                                            return nullptr;
                                        auto& c = context_of(a[1]);
                                        if (methodName == "same_quantum")
                                        {
                                            if (self.is_special() || o.is_special())
                                                return make_bool((self.is_nan() && o.is_nan()) || (self.special == 'F' && o.special == 'F'));
                                            return make_bool(self.exp == o.exp);
                                        }
                                        if (methodName == "compare" || methodName == "compare_signal")
                                        {
                                            if (self.is_special() || o.is_special())
                                            {
                                                if (methodName == "compare_signal" && (self.is_nan() || o.is_nan()))
                                                    return make_decimal(raise_error(c, InvalidOperation, "InvalidOperation"));
                                                if (auto const n = check_nans(self, &o, c))
                                                    return make_decimal(*n);
                                            }
                                            auto const r = cmp(self, o);
                                            return make_decimal(triple(r < 0 ? 1 : 0, r == 0 ? "0" : "1", 0));
                                        }
                                        if (methodName == "remainder_near")
                                        {
                                            if (auto const n = check_nans(self, &o, c))
                                                return make_decimal(*n);
                                            if (self.infinity() != 0)
                                                return make_decimal(raise_error(c, InvalidOperation, "InvalidOperation"));
                                            if (!o.truth())
                                                return make_decimal(raise_error(c, InvalidOperation, self.truth() ? "InvalidOperation" : "DivisionUndefined"));
                                            if (o.infinity() != 0)
                                                return make_decimal(fix(self, c));
                                            auto const ideal = std::min(self.exp, o.exp);
                                            if (!self.truth())
                                                return make_decimal(fix(triple(self.sign, "0", ideal), c));
                                            auto const expdiff = self.adjusted() - o.adjusted();
                                            if (expdiff >= c.prec + 1)
                                                return make_decimal(raise_error(c, InvalidOperation, "DivisionImpossible"));
                                            if (expdiff <= -2)
                                                return make_decimal(fix(rescale(self, ideal, c.rounding), c));
                                            auto a1 = self.coefficient(), a2 = o.coefficient();
                                            if (self.exp >= o.exp)
                                                a1 = a1 * pow10(self.exp - o.exp);
                                            else
                                                a2 = a2 * pow10(o.exp - self.exp);
                                            B q, r;
                                            big_divmod(a1, a2, q, r);
                                            if (a2 < r + r + (is_odd(q) ? B{ 1 } : B{ 0 }))
                                            {
                                                r = r - a2;
                                                q = q + B{ 1 };
                                            }
                                            if (!(q < pow10(c.prec)))
                                                return make_decimal(raise_error(c, InvalidOperation, "DivisionImpossible"));
                                            auto sign = self.sign;
                                            if (big_negative(r))
                                            {
                                                sign = 1 - sign;
                                                r = -r;
                                            }
                                            return make_decimal(fix(triple(sign, r, ideal), c));
                                        }
                                        // (max and min (and by magnitude))
                                        bool const magnitude = methodName.ends_with("_mag");
                                        bool const isMax = methodName.starts_with("max");
                                        if (self.is_special() || o.is_special())
                                        {
                                            auto const sn = self.nan_kind();
                                            auto const on = o.nan_kind();
                                            if (sn != 0 || on != 0)
                                            {
                                                if (on == 1 && sn == 0)
                                                    return make_decimal(fix(self, c));
                                                if (sn == 1 && on == 0)
                                                    return make_decimal(fix(o, c));
                                                return make_decimal(*check_nans(self, &o, c));
                                            }
                                        }
                                        auto r = magnitude ? cmp(absolute(self), absolute(o)) : cmp(self, o);
                                        if (r == 0)
                                        {
                                            // (compare_total: the sign, then the exponent)
                                            if (self.sign != o.sign)
                                                r = self.sign != 0 ? -1 : 1;
                                            else if (self.exp != o.exp)
                                                r = (self.exp < o.exp) != (self.sign != 0) ? -1 : 1;
                                        }
                                        auto const& chosen = isMax ? (r == -1 ? o : self) : (r == -1 ? self : o);
                                        return make_decimal(fix(chosen, c));
                                    }
                                    if (methodName == "to_eng_string") return make_str(to_string(self, true, current()));
                                    if (methodName == "__round__")
                                    {
                                        if (!rest.empty() && kind_of(rest[0]) != NEOS_PY_NONE)
                                        {
                                            if (!is_int_like(rest[0]))
                                                return error("TypeError", "optional arg must be an integer");
                                            long long n = 0;
                                            to_index(rest[0], n);
                                            return make_decimal(quantize(self, triple(0, "1", -n), current().rounding, current()));
                                        }
                                        if (self.is_special())
                                            return self.is_nan() ? error("ValueError", "cannot round a NaN") : error("OverflowError", "cannot round an infinity");
                                        return to_int(rescale(self, 0, RoundHalfEven));
                                    }
                                    if (methodName == "__floor__" || methodName == "__ceil__")
                                    {
                                        if (self.is_special())
                                            return self.is_nan() ? error("ValueError", "cannot round a NaN") : error("OverflowError", "cannot round an infinity");
                                        return to_int(rescale(self, 0, methodName == "__floor__" ? RoundFloor : RoundCeiling));
                                    }
                                    if (methodName == "__trunc__" || methodName == "__int__") return to_int(self);
                                    if (methodName == "__float__")
                                    {
                                        if (self.special == 'N')
                                            return error("ValueError", "cannot convert signaling NaN to float");
                                        return make_float(to_double(self));
                                    }
                                    if (methodName == "__format__")
                                    {
                                        if (rest.empty() || kind_of(rest[0]) != NEOS_PY_STR)
                                            return error("TypeError", "format spec must be a string");
                                        auto const text = format_decimal(self, as<str_object>(rest[0])->value, current());
                                        return failed() ? nullptr : make_str(text);
                                    }
                                    if (methodName == "fma")
                                    {
                                        if (rest.size() < 2u)
                                            return error("TypeError", "fma() missing required argument");
                                        dec o, t;
                                        if (!other_of(rest[0], o) || !other_of(rest[1], t))
                                            return nullptr;
                                        auto& c = current();
                                        dec product;
                                        if (self.is_special() || o.is_special())
                                        {
                                            if (self.special == 'N' || o.special == 'N')
                                                return make_decimal(raise_error(c, InvalidOperation, "InvalidOperation"));
                                            if (self.special == 'n')
                                                product = self;
                                            else if (o.special == 'n')
                                                product = o;
                                            else if (self.special == 'F')
                                            {
                                                if (!o.truth())
                                                    return make_decimal(raise_error(c, InvalidOperation, "InvalidOperation"));
                                                product = infinity_of(self.sign ^ o.sign);
                                            }
                                            else
                                            {
                                                if (!self.truth())
                                                    return make_decimal(raise_error(c, InvalidOperation, "InvalidOperation"));
                                                product = infinity_of(self.sign ^ o.sign);
                                            }
                                        }
                                        else
                                            product = triple(self.sign ^ o.sign, self.coefficient() * o.coefficient(), self.exp + o.exp);
                                        return make_decimal(add(product, t, c));
                                    }
                                    if (methodName == "scaleb")
                                    {
                                        dec o;
                                        if (rest.empty() || !other_of(rest[0], o))
                                            return failed() ? nullptr : error("TypeError", "scaleb() missing required argument");
                                        auto& c = current();
                                        if (auto const n = check_nans(self, &o, c))
                                            return make_decimal(*n);
                                        if (o.exp != 0 || !is_integer_value(o))
                                            return make_decimal(raise_error(c, InvalidOperation, "InvalidOperation"));
                                        if (self.infinity() != 0)
                                            return make_decimal(self);
                                        auto d = self;
                                        auto const shift = std::stoll(digits_of(o.coefficient()));
                                        d.exp += o.sign != 0 ? -shift : shift;
                                        return make_decimal(fix(d, c));
                                    }
                                    return error("AttributeError", methodName);
                                });
                            }, reinterpret_cast<void*>(i), aSelf);
                    return error("AttributeError", std::string{ "'decimal.Decimal' object has no attribute '" } + aName + "'");
                };
                return type;
            }

            // (format(): _pydecimal's __format__)
            std::string format_decimal(dec self, std::string const& aSpec, context_state& c)
            {
                // ([[fill]align][sign][z][#][0][width][grouping][.precision][type])
                std::size_t i = 0u;
                std::string fill = " ";
                char align = 0;
                auto const isAlign = [](char ch) { return ch == '<' || ch == '>' || ch == '=' || ch == '^'; };
                auto const points = to_code_points(aSpec);
                std::string spec = aSpec;
                if (points.size() >= 2u && isAlign(static_cast<char>(points[1] < 0x80u ? points[1] : 0)))
                {
                    fill.clear();
                    append_code_point(fill, points[0]);
                    align = static_cast<char>(points[1]);
                    std::string firstTwo;
                    append_code_point(firstTwo, points[0]);
                    firstTwo += align;
                    spec = aSpec.substr(firstTwo.size());
                }
                else if (!spec.empty() && isAlign(spec[0]))
                {
                    align = spec[0];
                    spec = spec.substr(1u);
                }
                char sign = '-';
                if (i < spec.size() && (spec[i] == '+' || spec[i] == '-' || spec[i] == ' '))
                    sign = spec[i++];
                bool noNegZero = false;
                if (i < spec.size() && spec[i] == 'z')
                {
                    noNegZero = true;
                    ++i;
                }
                bool alt = false;
                if (i < spec.size() && spec[i] == '#')
                {
                    alt = true;
                    ++i;
                }
                bool zeropad = false;
                if (i < spec.size() && spec[i] == '0')
                {
                    zeropad = true;
                    ++i;
                }
                long long width = 0;
                while (i < spec.size() && std::isdigit(static_cast<unsigned char>(spec[i])))
                    width = width * 10 + (spec[i++] - '0');
                std::string thousands;
                if (i < spec.size() && (spec[i] == ',' || spec[i] == '_'))
                    thousands = std::string(1u, spec[i++]);
                std::optional<long long> precision;
                if (i < spec.size() && spec[i] == '.')
                {
                    ++i;
                    long long p = 0;
                    bool any = false;
                    while (i < spec.size() && std::isdigit(static_cast<unsigned char>(spec[i])))
                    {
                        p = p * 10 + (spec[i++] - '0');
                        any = true;
                    }
                    if (!any)
                    {
                        error("ValueError", "Invalid format specifier '" + aSpec + "' for object of type 'decimal.Decimal'");
                        return {};
                    }
                    precision = p;
                }
                char type = 0;
                if (i < spec.size())
                    type = spec[i++];
                if (i != spec.size() || (type != 0 && std::strchr("eEfFgGn%", type) == nullptr))
                {
                    error("ValueError", "Invalid format specifier '" + aSpec + "' for object of type 'decimal.Decimal'");
                    return {};
                }
                if (zeropad && align == 0)
                {
                    fill = "0";
                    align = '=';
                }
                if (align == 0)
                    align = '>';
                if (precision && *precision == 0 && (type == 0 || type == 'g' || type == 'G' || type == 'n'))
                    precision = 1;
                if (type == 'n')
                    type = 'g';
                auto const signText = [&](int aNegative) { return aNegative != 0 ? std::string{ "-" } : sign == '+' || sign == ' ' ? std::string(1u, sign) : std::string{}; };
                auto const align_text = [&](std::string const& aSign, std::string const& aBody)
                {
                    auto const length = static_cast<long long>(to_code_points(aSign + aBody).size());
                    std::string padding;
                    for (long long k = length; k < width; ++k)
                        padding += fill;
                    switch (align)
                    {
                    case '<': return aSign + aBody + padding;
                    case '=': return aSign + padding + aBody;
                    case '^':
                        {
                            auto const half = (width - length) / 2;
                            std::string left;
                            for (long long k = 0; k < half; ++k)
                                left += fill;
                            std::string right;
                            for (long long k = half; k < width - length; ++k)
                                right += fill;
                            return left + aSign + aBody + right;
                        }
                    default: return padding + aSign + aBody;
                    }
                };
                if (self.is_special())
                {
                    auto body = to_string(absolute(self), false, c);
                    if (type == '%')
                        body += "%";
                    return align_text(signText(self.sign), body);
                }
                if (type == 0)
                    type = c.capitals != 0 ? 'G' : 'g';
                if (type == '%')
                    self = triple(self.sign, self.digits, self.exp + 2);
                auto const rounding = c.rounding;
                if (precision)
                {
                    if (type == 'e' || type == 'E')
                        self = round_places(self, *precision + 1, rounding);
                    else if (type == 'f' || type == 'F' || type == '%')
                        self = rescale(self, -*precision, rounding);
                    else if ((type == 'g' || type == 'G') && static_cast<long long>(self.digits.size()) > *precision)
                        self = round_places(self, *precision, rounding);
                }
                if (!self.truth() && self.exp > 0 && (type == 'f' || type == 'F' || type == '%'))
                    self = rescale(self, 0, rounding);
                int adjustedSign = !self.truth() && noNegZero && self.sign != 0 ? 0 : self.sign;
                auto const leftdigits = self.exp + static_cast<long long>(self.digits.size());
                long long dotplace = 0;
                if (type == 'e' || type == 'E')
                    dotplace = !self.truth() && precision ? 1 - *precision : 1;
                else if (type == 'f' || type == 'F' || type == '%')
                    dotplace = leftdigits;
                else
                    dotplace = self.exp <= 0 && leftdigits > -6 ? leftdigits : 1;
                std::string intpart, fracpart;
                auto const n = static_cast<long long>(self.digits.size());
                if (dotplace < 0)
                {
                    intpart = "0";
                    fracpart = std::string(static_cast<std::size_t>(-dotplace), '0') + self.digits;
                }
                else if (dotplace > n)
                    intpart = self.digits + std::string(static_cast<std::size_t>(dotplace - n), '0');
                else
                {
                    intpart = self.digits.substr(0u, static_cast<std::size_t>(dotplace));
                    if (intpart.empty())
                        intpart = "0";
                    fracpart = self.digits.substr(static_cast<std::size_t>(dotplace));
                }
                auto const exp = leftdigits - dotplace;
                auto const signString = signText(adjustedSign);
                if (!fracpart.empty() || alt)
                    fracpart = "." + fracpart;
                if (exp != 0 || type == 'e' || type == 'E')
                {
                    char const echar = type == 'E' || type == 'G' ? 'E' : 'e';
                    fracpart += std::string(1u, echar) + (exp >= 0 ? "+" : "-") + std::to_string(exp >= 0 ? exp : -exp);
                }
                if (type == '%')
                    fracpart += "%";
                // (thousands separators (and zero padding))
                long long minWidth = zeropad ? width - static_cast<long long>(fracpart.size()) - static_cast<long long>(signString.size()) : 0;
                std::vector<std::string> groups;
                auto digits = intpart;
                if (thousands.empty())
                {
                    auto const l = std::max<long long>({ static_cast<long long>(digits.size()), minWidth, 1 });
                    groups.push_back(std::string(static_cast<std::size_t>(l - static_cast<long long>(digits.size())), '0') + digits);
                }
                else
                    for (;;)
                    {
                        auto const l = std::min<long long>(std::max<long long>({ static_cast<long long>(digits.size()), minWidth, 1 }), 3);
                        auto const take = std::min<long long>(l, static_cast<long long>(digits.size()));
                        groups.push_back(std::string(static_cast<std::size_t>(l - take), '0') + digits.substr(digits.size() - static_cast<std::size_t>(take)));
                        digits = digits.substr(0u, digits.size() - static_cast<std::size_t>(take));
                        minWidth -= l;
                        if (digits.empty() && minWidth <= 0)
                            break;
                        minWidth -= static_cast<long long>(thousands.size());
                    }
                std::string grouped;
                for (auto g = groups.rbegin(); g != groups.rend(); ++g)
                    grouped += (grouped.empty() ? "" : thousands) + *g;
                return align_text(signString, grouped + fracpart);
            }

            object* new_decimal(neos_py_context*, void*, object* aArgs, object* aKwargs)
            {
                std::vector<object*> a;
                if (!parse_arguments("Decimal", as<tuple_object>(aArgs)->items, aKwargs, { "value", "context" }, 0u, 0u, a))
                    return nullptr;
                if (a[0] == nullptr)
                    return make_decimal(dec{});
                return guarded([&]() -> object*
                {
                    auto& c = context_of(a[1]);
                    dec value;
                    if (!convert(a[0], c, value))
                        return nullptr;
                    return make_decimal(value);
                });
            }

            int decimal_instancecheck(neos_py_context*, void*, object* aObject) { return is_decimal(aObject) ? 1 : 0; }

            // --- Context objects

            std::string signals_text(unsigned aSignals)
            {
                std::string text = "[";
                bool first = true;
                for (std::size_t i = 0u; i < std::size(sSignalNames); ++i)
                    if ((aSignals & (1u << i)) != 0u)
                    {
                        text += (first ? "" : ", ") + std::string{ sSignalNames[i] };
                        first = false;
                    }
                return text + "]";
            }

            // (a context's repr: libmpdec's order)
            std::string context_signals_text(unsigned aSignals)
            {
                std::string text = "[";
                bool first = true;
                for (auto const s : { InvalidOperation, Clamped, DivisionByZero, Inexact, FloatOperation, Overflow, Rounded, Subnormal, Underflow })
                    if ((aSignals & s) != 0u)
                    {
                        std::size_t i = 0u;
                        while ((1u << i) != s)
                            ++i;
                        text += (first ? "" : ", ") + std::string{ sSignalNames[i] };
                        first = false;
                    }
                return text + "]";
            }

            // (a signal (class) as a bit)
            unsigned signal_of(object* aClass)
            {
                if (!is_class(aClass) && !is_type_object(aClass))
                    return 0u;
                ref const name{ getattr(aClass, "__name__") };
                if (!name || kind_of(name.get()) != NEOS_PY_STR)
                {
                    clear_error();
                    return 0u;
                }
                auto const& n = as<str_object>(name.get())->value;
                for (std::size_t i = 0u; i < std::size(sSignalNames); ++i)
                    if (n == sSignalNames[i])
                        return 1u << i;
                if (n == "DivisionImpossible" || n == "DivisionUndefined" || n == "ConversionSyntax" || n == "InvalidContext")
                    return InvalidOperation;
                return 0u;
            }

            // (a context's flags or traps: a dict of the signals)
            struct signal_dict_object
            {
                header h;
                object* context;
                bool traps;
                ~signal_dict_object() { release(context); }
            };

            neos_py_type make_signal_dict_type();
            neos_py_type sSignalDictType = make_signal_dict_type();

            object* signal_class(std::size_t aIndex)
            {
                register_signals();
                return exception_class(exception_name(sSignalNames[aIndex]));
            }

            neos_py_type make_signal_dict_type()
            {
                auto type = make_type("decimal.SignalDict", "SignalDictMixin", retain_object, release_object<signal_dict_object>);
                auto const bits = [](object* aSelf) -> unsigned&
                {
                    auto& d = *reinterpret_cast<signal_dict_object*>(aSelf);
                    return d.traps ? as_context(d.context).state.traps : as_context(d.context).state.flags;
                };
                static unsigned& (*sBits)(object*) = +bits;
                type.repr = [](neos_py_context*, object* aSelf) -> object*
                {
                    std::string text = "{";
                    for (std::size_t i = 0u; i < std::size(sSignalNames); ++i)
                        text += (i != 0u ? ", " : "") + std::string{ "<class 'decimal." } + sSignalNames[i] + "'>:" + ((sBits(aSelf) & (1u << i)) != 0u ? "True" : "False");
                    return make_str(text + "}");
                };
                type.getitem = [](neos_py_context*, object* aSelf, object* aKey) -> object*
                {
                    auto const s = signal_of(aKey);
                    if (s == 0u)
                    {
                        raise_key_error(aKey);
                        return nullptr;
                    }
                    return make_bool((sBits(aSelf) & s) != 0u);
                };
                type.setitem = [](neos_py_context*, object* aSelf, object* aKey, object* aValue) -> int
                {
                    auto const s = signal_of(aKey);
                    if (s == 0u)
                    {
                        raise_key_error(aKey);
                        return -1;
                    }
                    if (aValue == nullptr || truth(aValue) != 1)
                        sBits(aSelf) &= ~s;
                    else
                        sBits(aSelf) |= s;
                    return 0;
                };
                type.length = [](neos_py_context*, object*) -> long long { return static_cast<long long>(std::size(sSignalNames)); };
                type.iter = [](neos_py_context*, object*) -> object*
                {
                    std::vector<object*> keys;
                    for (std::size_t i = 0u; i < std::size(sSignalNames); ++i)
                        keys.push_back(signal_class(i));
                    ref const list{ make_list(std::move(keys)) };
                    return iter(list.get());
                };
                type.getattr = [](neos_py_context*, object* aSelf, char const* aName) -> object*
                {
                    if (std::strcmp(aName, "keys") == 0 || std::strcmp(aName, "values") == 0 || std::strcmp(aName, "items") == 0 || std::strcmp(aName, "copy") == 0)
                    {
                        ref result{ make_dict() };
                        for (std::size_t i = 0u; i < std::size(sSignalNames); ++i)
                        {
                            ref const key{ signal_class(i) };
                            dict_set(result.get(), key.get(), make_bool((sBits(aSelf) & (1u << i)) != 0u));
                        }
                        if (std::strcmp(aName, "copy") == 0)
                            return make_function("copy", [](neos_py_context*, void* aDict, object*, object*) -> object* { return borrowed(static_cast<object*>(aDict)); },
                                result.get(), result.get());
                        ref const method{ getattr(result.get(), aName) };
                        return method ? borrowed(method.get()) : nullptr;
                    }
                    return error("AttributeError", std::string{ "'SignalDictMixin' object has no attribute '" } + aName + "'");
                };
                return type;
            }

            object* signal_dict(object* aContext, bool aTraps)
            {
                return &(new signal_dict_object{ { { &sSignalDictType.base } }, borrowed(aContext), aTraps })->h.base;
            }

            // (a context's operation (ctx.add(a, b) etc.): the operation with the context)
            object* context_operation(object* aContext, std::string const& aName, std::vector<object*> const& aArgs, object* aKwargs)
            {
                auto& c = as_context(aContext).state;
                std::vector<dec> operands;
                for (auto a : aArgs)
                {
                    if (auto const d = operand(a))
                        operands.push_back(*d);
                    else
                        return error("TypeError", std::string{ "conversion from " } + type_name(a) + " to Decimal is not supported");
                }
                (void)aKwargs;
                return guarded([&]() -> object*
                {
                    auto const need = [&](std::size_t n) { if (operands.size() != n) { error("TypeError", aName + "() takes exactly " + std::to_string(n) + " argument" + (n == 1u ? "" : "s")); return false; } return true; };
                    if (aName == "add") return need(2u) ? make_decimal(add(operands[0], operands[1], c)) : nullptr;
                    if (aName == "subtract") return need(2u) ? make_decimal(add(operands[0], negate(operands[1]), c)) : nullptr;
                    if (aName == "multiply") return need(2u) ? make_decimal(multiply(operands[0], operands[1], c)) : nullptr;
                    if (aName == "divide") return need(2u) ? make_decimal(divide(operands[0], operands[1], c)) : nullptr;
                    if (aName == "divide_int") return need(2u) ? make_decimal(floor_divide(operands[0], operands[1], c)) : nullptr;
                    if (aName == "remainder") return need(2u) ? make_decimal(modulo(operands[0], operands[1], c)) : nullptr;
                    if (aName == "power") return need(2u) ? make_decimal(power(operands[0], operands[1], c)) : nullptr;
                    if (aName == "quantize") return need(2u) ? make_decimal(quantize(operands[0], operands[1], c.rounding, c)) : nullptr;
                    if (aName == "sqrt") return need(1u) ? make_decimal(sqrt(operands[0], c)) : nullptr;
                    if (aName == "exp") return need(1u) ? make_decimal(exp_of(operands[0], c)) : nullptr;
                    if (aName == "ln") return need(1u) ? make_decimal(ln(operands[0], c)) : nullptr;
                    if (aName == "log10") return need(1u) ? make_decimal(log10(operands[0], c)) : nullptr;
                    if (aName == "normalize") return need(1u) ? make_decimal(normalize(operands[0], c)) : nullptr;
                    if (aName == "plus") return need(1u) ? make_decimal(fix(operands[0], c)) : nullptr;
                    if (aName == "minus") return need(1u) ? make_decimal(fix(negate(operands[0]), c)) : nullptr;
                    if (aName == "abs") return need(1u) ? make_decimal(fix(absolute(operands[0]), c)) : nullptr;
                    if (aName == "compare") return need(2u) ? make_decimal(triple(cmp(operands[0], operands[1]) < 0 ? 1 : 0, cmp(operands[0], operands[1]) == 0 ? "0" : "1", 0)) : nullptr;
                    if (aName == "to_sci_string") return need(1u) ? make_str(to_string(operands[0], false, c)) : nullptr;
                    if (aName == "to_eng_string") return need(1u) ? make_str(to_string(operands[0], true, c)) : nullptr;
                    return error("AttributeError", aName);
                });
            }

            neos_py_type make_context_type()
            {
                auto type = make_type("decimal.Context", "Context", retain_object, release_object<context_object>);
                type.repr = [](neos_py_context*, object* aSelf) -> object*
                {
                    auto const& c = as_context(aSelf).state;
                    return make_str("Context(prec=" + std::to_string(c.prec) + ", rounding=" + sRoundingNames[c.rounding] + ", Emin=" + std::to_string(c.Emin) +
                        ", Emax=" + std::to_string(c.Emax) + ", capitals=" + std::to_string(c.capitals) + ", clamp=" + std::to_string(c.clamp) + ", flags=" +
                        context_signals_text(c.flags) + ", traps=" + context_signals_text(c.traps) + ")");
                };
                type.getattr = [](neos_py_context*, object* aSelf, char const* aName) -> object*
                {
                    auto& c = as_context(aSelf).state;
                    std::string_view const name{ aName };
                    if (name == "prec") return make_int(c.prec);
                    if (name == "rounding") return make_str(sRoundingNames[c.rounding]);
                    if (name == "Emin") return make_int(c.Emin);
                    if (name == "Emax") return make_int(c.Emax);
                    if (name == "capitals") return make_int(c.capitals);
                    if (name == "clamp") return make_int(c.clamp);
                    if (name == "flags") return signal_dict(aSelf, false);
                    if (name == "traps") return signal_dict(aSelf, true);
                    static char const* const sMethods[] = { "copy", "clear_flags", "clear_traps", "Etiny", "Etop", "create_decimal", "create_decimal_from_float",
                        "add", "subtract", "multiply", "divide", "divide_int", "remainder", "power", "quantize", "sqrt", "exp", "ln", "log10", "normalize", "plus",
                        "minus", "abs", "compare", "to_sci_string", "to_eng_string", "__copy__" };
                    for (std::size_t i = 0u; i < std::size(sMethods); ++i)
                        if (name == sMethods[i])
                            return make_function(sMethods[i], [](neos_py_context*, void* aData, object* aArgs, object* aKwargs) -> object*
                            {
                                auto const& args = as<tuple_object>(aArgs)->items;
                                auto const self = args[0];
                                auto& c = as_context(self).state;
                                std::vector<object*> const rest{ args.begin() + 1, args.end() };
                                std::string const n = static_cast<char const*>(aData);
                                if (n == "copy" || n == "__copy__") return new_context(c);
                                if (n == "clear_flags") { c.flags = 0u; return borrowed(none()); }
                                if (n == "clear_traps") { c.traps = 0u; return borrowed(none()); }
                                if (n == "Etiny") return make_int(c.Etiny());
                                if (n == "Etop") return make_int(c.Etop());
                                if (n == "create_decimal" || n == "create_decimal_from_float")
                                {
                                    return guarded([&]() -> object*
                                    {
                                        dec value;
                                        if (!rest.empty() && !convert(rest[0], c, value))
                                            return nullptr;
                                        return make_decimal(fix(value, c));
                                    });
                                }
                                return context_operation(self, n, rest, aKwargs);
                            }, const_cast<char*>(sMethods[i]), aSelf);
                    return error("AttributeError", std::string{ "'decimal.Context' object has no attribute '" } + aName + "'");
                };
                type.setattr = [](neos_py_context*, object* aSelf, char const* aName, object* aValue) -> int
                {
                    auto& c = as_context(aSelf).state;
                    std::string_view const name{ aName };
                    if (aValue == nullptr)
                    {
                        error("AttributeError", "context attributes cannot be deleted");
                        return -1;
                    }
                    long long v = 0;
                    if (name == "rounding")
                    {
                        int r = 0;
                        if (!rounding_of(aValue, r))
                            return -1;
                        c.rounding = r;
                        return 0;
                    }
                    if (name == "flags" || name == "traps")
                    {
                        unsigned bits = 0u;
                        std::vector<object*> items;
                        if (kind_of(aValue) == NEOS_PY_DICT)
                        {
                            for (auto const& item : as<dict_object>(aValue)->items)
                                if (truth(item.second) == 1)
                                    bits |= signal_of(item.first);
                        }
                        else if (collect(aValue, items))
                        {
                            for (auto i : items)
                            {
                                bits |= signal_of(i);
                                release(i);
                            }
                        }
                        else
                            return -1;
                        (name == "flags" ? c.flags : c.traps) = bits;
                        return 0;
                    }
                    if (!to_index(aValue, v))
                        return -1;
                    if (name == "prec")
                    {
                        if (v < 1 || v > 999999999999999999ll)
                        {
                            error("ValueError", "valid range for prec is [1, MAX_PREC]");
                            return -1;
                        }
                        c.prec = v;
                    }
                    else if (name == "Emin")
                    {
                        if (v > 0)
                        {
                            error("ValueError", "valid range for Emin is [MIN_EMIN, 0]");
                            return -1;
                        }
                        c.Emin = v;
                    }
                    else if (name == "Emax")
                    {
                        if (v < 0)
                        {
                            error("ValueError", "valid range for Emax is [0, MAX_EMAX]");
                            return -1;
                        }
                        c.Emax = v;
                    }
                    else if (name == "capitals")
                    {
                        if (v != 0 && v != 1)
                        {
                            error("ValueError", "valid values for capitals are 0 or 1");
                            return -1;
                        }
                        c.capitals = static_cast<int>(v);
                    }
                    else if (name == "clamp")
                    {
                        if (v != 0 && v != 1)
                        {
                            error("ValueError", "valid values for clamp are 0 or 1");
                            return -1;
                        }
                        c.clamp = static_cast<int>(v);
                    }
                    else
                    {
                        error("AttributeError", std::string{ "'decimal.Context' object has no attribute '" } + aName + "'");
                        return -1;
                    }
                    return 0;
                };
                return type;
            }

            bool configure(object* aContext, object* aKwargs, std::vector<object*> const& aArgs)
            {
                static char const* const sNames[] = { "prec", "rounding", "Emin", "Emax", "capitals", "clamp", "flags", "traps" };
                for (std::size_t i = 0u; i < aArgs.size() && i < std::size(sNames); ++i)
                    if (kind_of(aArgs[i]) != NEOS_PY_NONE && setattr(aContext, sNames[i], aArgs[i]) < 0)
                        return false;
                if (aKwargs != nullptr)
                    for (auto const& item : as<dict_object>(aKwargs)->items)
                    {
                        auto const& name = as<str_object>(item.first)->value;
                        if (std::find_if(std::begin(sNames), std::end(sNames), [&](char const* n) { return name == n; }) == std::end(sNames))
                        {
                            error("TypeError", "'" + name + "' is an invalid keyword argument for this function");
                            return false;
                        }
                        if (kind_of(item.second) != NEOS_PY_NONE && setattr(aContext, name.c_str(), item.second) < 0)
                            return false;
                    }
                return true;
            }

            object* construct_context(neos_py_context*, void*, object* aArgs, object* aKwargs)
            {
                ref result{ new_context(context_state{}) };
                as_context(result.get()).state.flags = 0u;
                if (!configure(result.get(), aKwargs, as<tuple_object>(aArgs)->items))
                    return nullptr;
                return result.release_ownership();
            }

            int context_instancecheck(neos_py_context*, void*, object* aObject) { return is_context(aObject) ? 1 : 0; }

            // (localcontext's manager)
            struct manager_object
            {
                header h;
                object* context;
                object* saved = nullptr;
                ~manager_object() { release(context); release(saved); }
            };

            neos_py_type make_manager_type();
            neos_py_type sManagerType = make_manager_type();

            neos_py_type make_manager_type()
            {
                auto type = make_type("decimal.ContextManager", "ContextManager", retain_object, release_object<manager_object>);
                type.getattr = [](neos_py_context*, object* aSelf, char const* aName) -> object*
                {
                    if (std::strcmp(aName, "__enter__") == 0)
                        return make_function(aName, [](neos_py_context*, void*, object* aArgs, object*) -> object*
                        {
                            auto& m = *reinterpret_cast<manager_object*>(as<tuple_object>(aArgs)->items[0]);
                            release(m.saved);
                            m.saved = borrowed(current_context());
                            tCurrent = borrowed(m.context);
                            return borrowed(m.context);
                        }, nullptr, aSelf);
                    if (std::strcmp(aName, "__exit__") == 0)
                        return make_function(aName, [](neos_py_context*, void*, object* aArgs, object*) -> object*
                        {
                            auto& m = *reinterpret_cast<manager_object*>(as<tuple_object>(aArgs)->items[0]);
                            release(tCurrent);
                            tCurrent = m.saved;
                            m.saved = nullptr;
                            return borrowed(none());
                        }, nullptr, aSelf);
                    return error("AttributeError", aName);
                };
                return type;
            }

            builtin_entry const sDecimalFunctions[] =
            {
                { "getcontext", [](std::vector<object*> const& aArgs, object*) -> object*
                    { return aArgs.empty() ? borrowed(current_context()) : error("TypeError", "getcontext() takes no arguments"); } },
                { "setcontext", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        if (aArgs.size() != 1u || !is_context(aArgs[0]))
                            return error("TypeError", "argument must be a context");
                        // (DefaultContext etc.: a copy is set)
                        auto const old = tCurrent;
                        tCurrent = new_context(as_context(aArgs[0]).state);
                        release(old);
                        return borrowed(none());
                    } },
                { "localcontext", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        if (aArgs.size() > 1u || (!aArgs.empty() && kind_of(aArgs[0]) != NEOS_PY_NONE && !is_context(aArgs[0])))
                            return error("TypeError", "optional argument must be a context");
                        auto const base = !aArgs.empty() && is_context(aArgs[0]) ? aArgs[0] : current_context();
                        ref context{ new_context(as_context(base).state) };
                        if (!configure(context.get(), aKwargs, {}))
                            return nullptr;
                        return &(new manager_object{ { { &sManagerType.base } }, context.release_ownership() })->h.base;
                    } }
            };

            bool decimal_module(std::vector<std::pair<std::string, object*>>& aAttributes)
            {
                register_signals();
                for (auto const& f : sDecimalFunctions)
                    aAttributes.emplace_back(f.name, make_builtin(f));
                aAttributes.emplace_back("Decimal", context()->type_new(context(), "Decimal", "decimal.Decimal", new_decimal, nullptr, decimal_instancecheck));
                aAttributes.emplace_back("Context", context()->type_new(context(), "Context", "decimal.Context", construct_context, nullptr, context_instancecheck));
                for (std::size_t i = 0u; i < 8u; ++i)
                    aAttributes.emplace_back(sRoundingNames[i], make_str(sRoundingNames[i]));
                for (auto const name : { "DecimalException", "Clamped", "InvalidOperation", "ConversionSyntax", "DivisionByZero", "DivisionImpossible", "DivisionUndefined",
                    "Inexact", "InvalidContext", "Rounded", "Subnormal", "Overflow", "Underflow", "FloatOperation" })
                    aAttributes.emplace_back(name, exception_class(exception_name(name)));
                context_state basic;
                basic.prec = 9;
                basic.rounding = RoundHalfUp;
                basic.traps = InvalidOperation | DivisionByZero | Overflow | Underflow | Clamped;
                context_state extended;
                extended.prec = 9;
                extended.traps = 0u;
                aAttributes.emplace_back("DefaultContext", new_context(context_state{}));
                aAttributes.emplace_back("BasicContext", new_context(basic));
                aAttributes.emplace_back("ExtendedContext", new_context(extended));
                aAttributes.emplace_back("MAX_PREC", make_int(999999999999999999ll));
                aAttributes.emplace_back("MAX_EMAX", make_int(999999999999999999ll));
                aAttributes.emplace_back("MIN_EMIN", make_int(-999999999999999999ll));
                aAttributes.emplace_back("MIN_ETINY", make_int(-1999999999999999997ll));
                aAttributes.emplace_back("HAVE_THREADS", make_bool(true));
                aAttributes.emplace_back("HAVE_CONTEXTVAR", make_bool(true));
                aAttributes.emplace_back("__version__", make_str("1.70"));
                aAttributes.emplace_back("__libmpdec_version__", make_str("2.5.1"));
                return true;
            }

            inline bool const sDecimal = register_module("decimal", decimal_module);
        }

        object* decimal_class_function(char const* aName)
        {
            if (std::strcmp(aName, "from_float") == 0 || std::strcmp(aName, "from_number") == 0)
                return make_function(aName, [](neos_py_context*, void*, object* aArgs, object*) -> object*
                {
                    auto const& args = as<tuple_object>(aArgs)->items;
                    if (args.size() != 1u)
                        return error("TypeError", "from_float() takes exactly one argument");
                    if (kind_of(args[0]) == NEOS_PY_FLOAT)
                        return make_decimal(from_double(as<float_object>(args[0])->value));
                    if (auto const d = operand(args[0]))
                        return make_decimal(*d);
                    return error("TypeError", std::string{ "argument must be int or float" });
                }, nullptr);
            return nullptr;
        }
    }
}
