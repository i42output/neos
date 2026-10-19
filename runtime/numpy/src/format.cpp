/*
  format.cpp

  Copyright (c) 2026 Leigh Johnston.  All Rights Reserved.

  This program is free software: you can redistribute it and / or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.

  Portions of this file are derived from NumPy 2.5.3 (numpy/_core/arrayprint.py and
  numpy/_core/src/multiarray/scalartypes.c.src), Copyright (c) 2005-2025, NumPy Developers; All rights reserved. They
  are used under NumPy's BSD 3-Clause licence (see runtime/numpy/LICENSE.NumPy). Changes: translated from C and Python
  to C++ for the neos NumPy runtime plugin and adapted to its object model (see runtime/numpy/NOTICE).
*/

// NumPy's printing of arrays and scalars (numpy/_core/arrayprint.py and the Dragon4 formatting of floats)

#include <algorithm>
#include <charconv>
#include <cmath>
#include "numpy.hpp"
#include "cast.hpp"
#include "functions.hpp"

namespace neos::numpy
{
    print_options& options()
    {
        static print_options sOptions;
        return sOptions;
    }

    std::string shape_repr(shape_t const& aShape)
    {
        std::string result = "(";
        for (std::size_t i = 0u; i < aShape.size(); ++i)
            result += (i != 0u ? ", " : "") + std::to_string(aShape[i]);
        result += aShape.size() == 1u ? ",)" : ")";
        return result;
    }

    namespace
    {
        // a decimal: 0.d1d2d3... x 10^exponent (digits has no leading zeros; "0" for zero)
        struct decimal
        {
            std::string digits;
            int exponent = 0;
        };

        // (from to_chars' scientific form "d.ddde+xx")
        decimal parse_scientific(std::string_view aText)
        {
            decimal result;
            auto const e = aText.find('e');
            auto const mantissa = aText.substr(0u, e);
            for (auto c : mantissa)
                if (c >= '0' && c <= '9')
                    result.digits += c;
            int exponent = 0;
            std::from_chars(aText.data() + e + (aText[e + 1u] == '+' ? 2u : 1u), aText.data() + aText.size(), exponent);
            result.exponent = exponent + 1;
            if (result.digits.find_first_not_of('0') == std::string::npos)
                result = decimal{ "0", 0 };
            return result;
        }

        // the shortest digits that identify |aValue| as a value of its type
        decimal shortest(double aValue, dtype aType)
        {
            char buffer[64];
            aValue = std::fabs(aValue);
            if (aValue == 0.0)
                return decimal{ "0", 0 };
            if (aType == dtype::Float32 || aType == dtype::Complex64)
            {
                auto const r = std::to_chars(buffer, buffer + sizeof(buffer), static_cast<float>(aValue), std::chars_format::scientific);
                return parse_scientific({ buffer, static_cast<std::size_t>(r.ptr - buffer) });
            }
            if (aType == dtype::Float16)
            {
                half const h{ aValue };
                for (int p = 0; p < 6; ++p)
                {
                    auto const r = std::to_chars(buffer, buffer + sizeof(buffer), aValue, std::chars_format::scientific, p);
                    double back = 0.0;
                    std::from_chars(buffer, r.ptr, back);
                    if (half{ back }.bits == h.bits)
                        return parse_scientific({ buffer, static_cast<std::size_t>(r.ptr - buffer) });
                }
            }
            auto const r = std::to_chars(buffer, buffer + sizeof(buffer), aValue, std::chars_format::scientific);
            return parse_scientific({ buffer, static_cast<std::size_t>(r.ptr - buffer) });
        }

        // |aValue| (exactly) rounded to aDigits digits after the decimal point
        decimal rounded_fraction(double aValue, int aDigits)
        {
            std::string buffer(400u + static_cast<std::size_t>(aDigits), '\0');
            auto const r = std::to_chars(buffer.data(), buffer.data() + buffer.size(), std::fabs(aValue), std::chars_format::fixed, aDigits);
            std::string_view const text{ buffer.data(), static_cast<std::size_t>(r.ptr - buffer.data()) };
            decimal result;
            auto const point = text.find('.');
            auto const integral = text.substr(0u, point);
            std::string all{ integral };
            if (point != std::string_view::npos)
                all += text.substr(point + 1u);
            int exponent = static_cast<int>(integral.size());
            auto const first = all.find_first_not_of('0');
            if (first == std::string::npos)
                return decimal{ "0", 0 };
            result.digits = all.substr(first);
            result.exponent = exponent - static_cast<int>(first);
            return result;
        }

        // |aValue| (exactly) rounded to aDigits significant digits
        decimal rounded_significant(double aValue, int aDigits)
        {
            char buffer[512];
            if (aValue == 0.0)
                return decimal{ std::string(static_cast<std::size_t>(std::max(aDigits, 1)), '0'), 0 };
            auto const r = std::to_chars(buffer, buffer + sizeof(buffer), std::fabs(aValue), std::chars_format::scientific, std::max(aDigits - 1, 0));
            return parse_scientific({ buffer, static_cast<std::size_t>(r.ptr - buffer) });
        }

        std::size_t fraction_digits(decimal const& aDecimal)
        {
            auto const n = static_cast<int>(aDecimal.digits.size()) - aDecimal.exponent;
            return aDecimal.digits == "0" ? 0u : static_cast<std::size_t>(std::max(n, 0));
        }

        struct float_options
        {
            int precision = -1;     // (-1: none)
            int minDigits = -1;
            bool unique = true;
            char trim = 'k';
            bool sign = false;
            int padLeft = -1;
            int padRight = -1;
            int expDigits = -1;
        };

        std::string apply_trim(std::string aFraction, float_options const& aOptions, bool& aDropPoint)
        {
            aDropPoint = false;
            if (aOptions.trim == 'k' && !aOptions.unique && aOptions.precision >= 0 && static_cast<int>(aFraction.size()) < aOptions.precision)
                aFraction.append(static_cast<std::size_t>(aOptions.precision) - aFraction.size(), '0');
            if (aOptions.minDigits > 0 && static_cast<int>(aFraction.size()) < aOptions.minDigits)
                aFraction.append(static_cast<std::size_t>(aOptions.minDigits) - aFraction.size(), '0');
            if (aOptions.trim == '.' || aOptions.trim == '-' || aOptions.trim == '0')
            {
                auto const keep = static_cast<std::size_t>(std::max(aOptions.minDigits, 0));
                while (aFraction.size() > keep && aFraction.back() == '0')
                    aFraction.pop_back();
            }
            if (aOptions.trim == '0' && aFraction.empty())
                aFraction = "0";
            if (aOptions.trim == '-' && aFraction.empty())
                aDropPoint = true;
            return aFraction;
        }

        std::string positional(double aValue, dtype aType, float_options const& aOptions)
        {
            decimal d;
            if (aOptions.unique)
            {
                d = shortest(aValue, aType);
                auto const n = static_cast<int>(fraction_digits(d));
                if (aOptions.precision >= 0 && n > aOptions.precision)
                    d = rounded_fraction(aValue, aOptions.precision);
                else if (aOptions.minDigits > 0 && n < aOptions.minDigits)
                    d = rounded_fraction(aValue, aOptions.minDigits);
            }
            else
                d = rounded_fraction(aValue, std::max(aOptions.precision, 0));
            std::string integral, fraction;
            if (d.digits == "0")
                integral = "0";
            else if (d.exponent <= 0)
            {
                integral = "0";
                fraction = std::string(static_cast<std::size_t>(-d.exponent), '0') + d.digits;
            }
            else if (d.exponent >= static_cast<int>(d.digits.size()))
                integral = d.digits + std::string(static_cast<std::size_t>(d.exponent) - d.digits.size(), '0');
            else
            {
                integral = d.digits.substr(0u, static_cast<std::size_t>(d.exponent));
                fraction = d.digits.substr(static_cast<std::size_t>(d.exponent));
            }
            bool dropPoint = false;
            fraction = apply_trim(fraction, aOptions, dropPoint);
            std::string sign = std::signbit(aValue) ? "-" : aOptions.sign ? "+" : "";
            std::string left = sign + integral;
            if (aOptions.padLeft > static_cast<int>(left.size()))
                left.insert(0u, static_cast<std::size_t>(aOptions.padLeft) - left.size(), ' ');
            std::string result = left + (dropPoint ? "" : ".") + fraction;
            if (aOptions.padRight > static_cast<int>(fraction.size()))
                result.append(static_cast<std::size_t>(aOptions.padRight) - fraction.size(), ' ');
            return result;
        }

        std::string scientific(double aValue, dtype aType, float_options const& aOptions)
        {
            decimal d;
            if (aOptions.unique)
            {
                d = shortest(aValue, aType);
                auto const n = static_cast<int>(d.digits.size()) - 1;
                if (aOptions.precision >= 0 && n > aOptions.precision)
                    d = rounded_significant(aValue, aOptions.precision + 1);
                else if (aOptions.minDigits > 0 && n < aOptions.minDigits)
                    d = rounded_significant(aValue, aOptions.minDigits + 1);
            }
            else
                d = rounded_significant(aValue, std::max(aOptions.precision, 0) + 1);
            int exponent = d.digits.find_first_not_of('0') == std::string::npos ? 0 : d.exponent - 1;
            std::string fraction = d.digits.substr(1u);
            bool dropPoint = false;
            fraction = apply_trim(fraction, aOptions, dropPoint);
            std::string sign = std::signbit(aValue) ? "-" : aOptions.sign ? "+" : "";
            std::string left = sign + d.digits.substr(0u, 1u);
            if (aOptions.padLeft > static_cast<int>(left.size()))
                left.insert(0u, static_cast<std::size_t>(aOptions.padLeft) - left.size(), ' ');
            std::string exponentDigits = std::to_string(std::abs(exponent));
            auto const minimum = static_cast<std::size_t>(aOptions.expDigits >= 0 ? aOptions.expDigits : 2);
            if (exponentDigits.size() < minimum)
                exponentDigits.insert(0u, minimum - exponentDigits.size(), '0');
            return left + (dropPoint ? "" : ".") + fraction + "e" + (exponent < 0 ? "-" : "+") + exponentDigits;
        }

        int type_precision(dtype aType)
        {
            switch (aType)
            {
            case dtype::Float16: return 3;
            case dtype::Float32: case dtype::Complex64: return 6;
            default: return 15;
            }
        }

        // (FloatingFormat)
        class floating_format
        {
        public:
            floating_format(std::vector<double> const& aData, dtype aType, char aSign, bool aZeroDimensional) :
                iType{ aType }, iSign{ aSign }
            {
                auto const& o = options();
                iFloatmode = o.floatmode;
                iPrecision = iFloatmode == "unique" ? -1 : o.precision;
                std::vector<double> finite;
                bool anyNonFinite = false;
                bool negativeInfinity = false;
                for (auto v : aData)
                    if (std::isfinite(v))
                        finite.push_back(v);
                    else
                    {
                        anyNonFinite = true;
                        if (std::isinf(v) && v < 0.0)
                            negativeInfinity = true;
                    }
                std::vector<double> absNonZero;
                for (auto v : finite)
                    if (v != 0.0)
                        absNonZero.push_back(std::fabs(v));
                if (!absNonZero.empty())
                {
                    auto const maxValue = *std::max_element(absNonZero.begin(), absNonZero.end());
                    auto const minValue = *std::min_element(absNonZero.begin(), absNonZero.end());
                    double const cutoff = std::pow(10.0, std::min(8, type_precision(aType)));
                    if (maxValue >= cutoff || (!o.suppress && (minValue < 0.0001 || maxValue / minValue > 1000.0)))
                        iExpFormat = true;
                }
                if (finite.empty())
                {
                    iPadLeft = 0;
                    iPadRight = 0;
                    iTrim = '.';
                    iExpSize = -1;
                    iUnique = true;
                    iMinDigits = -1;
                }
                else if (iExpFormat)
                {
                    char trim = '.';
                    bool unique = true;
                    if (iFloatmode == "fixed")
                        trim = 'k', unique = false;
                    std::size_t expSize = 0u, precision = 0u, padLeft = 0u;
                    for (auto v : finite)
                    {
                        float_options f;
                        f.precision = iPrecision;
                        f.unique = unique;
                        f.trim = trim;
                        f.sign = iSign == '+';
                        auto const s = scientific(v, aType, f);
                        auto const e = s.find('e');
                        auto const point = s.find('.');
                        expSize = std::max(expSize, s.size() - e - 2u);
                        precision = std::max(precision, e - point - 1u);
                        padLeft = std::max(padLeft, point);
                    }
                    iExpSize = static_cast<int>(expSize);
                    iTrim = 'k';
                    iPrecision = static_cast<int>(precision);
                    iMinDigits = iPrecision;
                    iUnique = unique;
                    iPadLeft = static_cast<int>(padLeft);
                    iPadRight = iExpSize + 2 + iPrecision;
                }
                else
                {
                    char trim = '.';
                    bool unique = true;
                    if (iFloatmode == "fixed")
                        trim = 'k', unique = false;
                    std::size_t padLeft = 0u, padRight = 0u;
                    for (auto v : finite)
                    {
                        float_options f;
                        f.precision = iPrecision;
                        f.unique = unique;
                        f.trim = trim;
                        f.sign = iSign == '+';
                        auto const s = positional(v, aType, f);
                        auto const point = s.find('.');
                        padLeft = std::max(padLeft, point);
                        padRight = std::max(padRight, s.size() - point - 1u);
                    }
                    iPadLeft = static_cast<int>(padLeft);
                    iPadRight = static_cast<int>(padRight);
                    iExpSize = -1;
                    iUnique = unique;
                    if (iFloatmode == "fixed" || iFloatmode == "maxprec_equal")
                    {
                        iPrecision = iMinDigits = iPadRight;
                        iTrim = 'k';
                    }
                    else
                    {
                        iTrim = '.';
                        iMinDigits = 0;
                    }
                }
                if (iSign == ' ')
                {
                    bool anyNegative = false;
                    for (auto v : finite)
                        if (std::signbit(v))
                            anyNegative = true;
                    if (!anyNegative)
                        ++iPadLeft;
                }
                if (anyNonFinite)
                {
                    int const neginf = (iSign != '-' || negativeInfinity) ? 1 : 0;
                    int const offset = iPadRight + 1;
                    iPadLeft = std::max({ iPadLeft, static_cast<int>(o.nanstr.size()) - offset, static_cast<int>(o.infstr.size()) + neginf - offset });
                }
                (void)aZeroDimensional;
            }
        public:
            std::string operator()(double aValue) const
            {
                auto const& o = options();
                if (!std::isfinite(aValue))
                {
                    std::string text;
                    if (std::isnan(aValue))
                        text = (iSign == '+' ? "+" : "") + o.nanstr;
                    else
                        text = (aValue < 0.0 ? "-" : iSign == '+' ? "+" : "") + o.infstr;
                    auto const width = iPadLeft + iPadRight + 1;
                    if (static_cast<int>(text.size()) < width)
                        text.insert(0u, static_cast<std::size_t>(width) - text.size(), ' ');
                    return text;
                }
                float_options f;
                f.precision = iPrecision;
                f.minDigits = iMinDigits;
                f.unique = iUnique;
                f.trim = iTrim;
                f.sign = iSign == '+';
                f.padLeft = iPadLeft;
                if (iExpFormat)
                {
                    f.expDigits = iExpSize;
                    return scientific(aValue, iType, f);
                }
                f.padRight = iPadRight;
                return positional(aValue, iType, f);
            }
        private:
            dtype iType;
            char iSign;
            std::string iFloatmode;
            int iPrecision = -1;
            int iMinDigits = -1;
            bool iUnique = true;
            char iTrim = '.';
            bool iExpFormat = false;
            int iExpSize = -1;
            int iPadLeft = 0;
            int iPadRight = 0;
        };

        std::string integer_text(scalar_value const& aValue)
        {
            if (is_unsigned(aValue.type))
                return std::to_string(aValue.u);
            return std::to_string(aValue.i);
        }

        // the format of an array's elements
        struct element_format
        {
            dtype type;
            std::optional<floating_format> real;
            std::optional<floating_format> imag;
            std::size_t width = 0u;     // (integers)
            bool zeroDimensional = false;

            std::string operator()(unsigned char const* aElement) const
            {
                auto const v = load(aElement, type);
                if (is_bool(type))
                    return v.b ? (zeroDimensional ? "True" : " True") : "False";
                if (is_integer(type))
                {
                    auto text = integer_text(v);
                    if (text.size() < width)
                        text.insert(0u, width - text.size(), ' ');
                    return text;
                }
                if (is_float(type))
                    return (*real)(v.f);
                auto const r = (*real)(v.c.real());
                auto i = (*imag)(v.c.imag());
                auto const end = i.find_last_not_of(' ') + 1u;
                i.insert(end, "j");
                return r + i;
            }
        };

        // the elements that are printed (all of them, or the leading and trailing edges of each dimension)
        void collect(ndarray const& aArray, std::size_t aAxis, unsigned char const* aPointer, bool aSummarize, std::vector<unsigned char const*>& aResult)
        {
            if (aAxis == aArray.ndim())
            {
                aResult.push_back(aPointer);
                return;
            }
            auto const n = aArray.shape[aAxis];
            auto const edge = options().edgeitems;
            if (aSummarize && n > 2 * edge)
            {
                for (std::int64_t i = 0; i < edge; ++i)
                    collect(aArray, aAxis + 1u, aPointer + i * aArray.strides[aAxis], aSummarize, aResult);
                for (std::int64_t i = n - edge; i < n; ++i)
                    collect(aArray, aAxis + 1u, aPointer + i * aArray.strides[aAxis], aSummarize, aResult);
            }
            else
                for (std::int64_t i = 0; i < n; ++i)
                    collect(aArray, aAxis + 1u, aPointer + i * aArray.strides[aAxis], aSummarize, aResult);
        }

        element_format make_format(ndarray const& aArray, bool aSummarize)
        {
            element_format result;
            result.type = aArray.type;
            result.zeroDimensional = aArray.ndim() == 0;
            std::vector<unsigned char const*> elements;
            collect(aArray, 0u, aArray.pointer(), aSummarize, elements);
            auto const sign = options().sign.empty() ? '-' : options().sign[0];
            if (is_integer(aArray.type))
            {
                if (!elements.empty())
                {
                    std::string maxText, minText;
                    bool first = true;
                    scalar_value maxValue, minValue;
                    for (auto e : elements)
                    {
                        auto const v = load(e, aArray.type);
                        bool const greater = is_unsigned(v.type) ? v.u > maxValue.u : v.i > maxValue.i;
                        bool const less = is_unsigned(v.type) ? v.u < minValue.u : v.i < minValue.i;
                        if (first || greater)
                            maxValue = v;
                        if (first || less)
                            minValue = v;
                        first = false;
                    }
                    auto maxLength = integer_text(maxValue).size();
                    auto s = sign;
                    bool const minNegative = !is_unsigned(minValue.type) && minValue.i < 0;
                    bool const maxNonNegative = is_unsigned(maxValue.type) || maxValue.i >= 0;
                    if (s == ' ' && minNegative)
                        s = '-';
                    if (maxNonNegative && (s == '+' || s == ' '))
                        ++maxLength;
                    result.width = std::max(maxLength, integer_text(minValue).size());
                }
            }
            else if (is_float(aArray.type))
            {
                std::vector<double> data;
                for (auto e : elements)
                    data.push_back(read_as<double>(e, aArray.type));
                result.real.emplace(data, aArray.type, sign, result.zeroDimensional);
            }
            else if (is_complex(aArray.type))
            {
                std::vector<double> re, im;
                for (auto e : elements)
                {
                    auto const v = read_as<std::complex<double>>(e, aArray.type);
                    re.push_back(v.real());
                    im.push_back(v.imag());
                }
                result.real.emplace(re, aArray.type, sign, result.zeroDimensional);
                result.imag.emplace(im, aArray.type, '+', result.zeroDimensional);
            }
            return result;
        }

        void extend_line(std::string& s, std::string& line, std::string const& word, std::size_t width, std::string const& nextLinePrefix)
        {
            bool needsWrap = line.size() + word.size() > width;
            if (line.size() <= nextLinePrefix.size())
                needsWrap = false;
            if (needsWrap)
            {
                auto end = line.find_last_not_of(' ');
                s += line.substr(0u, end == std::string::npos ? 0u : end + 1u) + "\n";
                line = nextLinePrefix;
            }
            line += word;
        }

        // (_formatArray)
        std::string format_array(ndarray const& aArray, element_format const& aFormat, std::size_t aLineWidth, std::string const& aNextLinePrefix,
            std::string const& aSeparator, bool aSummarize)
        {
            auto const edge = static_cast<std::int64_t>(options().edgeitems);
            std::string const separatorStripped = aSeparator.substr(0u, aSeparator.find_last_not_of(' ') + 1u);
            std::function<std::string(std::size_t, unsigned char const*, std::string const&, std::size_t)> recurse =
                [&](std::size_t aAxis, unsigned char const* aPointer, std::string const& aHangingIndent, std::size_t aWidth) -> std::string
            {
                auto const axesLeft = aArray.ndim() - aAxis;
                if (axesLeft == 0u)
                    return aFormat(aPointer);
                auto const nextHangingIndent = aHangingIndent + " ";
                auto const nextWidth = aWidth - 1u;
                auto const n = aArray.shape[aAxis];
                bool const showSummary = aSummarize && 2 * edge < n;
                std::int64_t leading = showSummary ? edge : 0;
                std::int64_t trailing = showSummary ? edge : n;
                auto const element = [&](std::int64_t i) { return aPointer + (i < 0 ? n + i : i) * aArray.strides[aAxis]; };
                std::string s;
                if (axesLeft == 1u)
                {
                    auto const elementWidth = aWidth - std::max<std::size_t>(separatorStripped.size(), 1u);
                    std::string line = aHangingIndent;
                    for (std::int64_t i = 0; i < leading; ++i)
                    {
                        auto const word = recurse(aAxis + 1u, element(i), nextHangingIndent, nextWidth);
                        extend_line(s, line, word, elementWidth, aHangingIndent);
                        line += aSeparator;
                    }
                    if (showSummary)
                    {
                        extend_line(s, line, "...", elementWidth, aHangingIndent);
                        line += aSeparator;
                    }
                    for (std::int64_t i = trailing; i > 1; --i)
                    {
                        auto const word = recurse(aAxis + 1u, element(-i), nextHangingIndent, nextWidth);
                        extend_line(s, line, word, elementWidth, aHangingIndent);
                        line += aSeparator;
                    }
                    auto const word = recurse(aAxis + 1u, element(-1), nextHangingIndent, nextWidth);
                    extend_line(s, line, word, elementWidth, aHangingIndent);
                    s += line;
                }
                else
                {
                    std::string const lineSeparator = separatorStripped + std::string(axesLeft - 1u, '\n');
                    for (std::int64_t i = 0; i < leading; ++i)
                        s += aHangingIndent + recurse(aAxis + 1u, element(i), nextHangingIndent, nextWidth) + lineSeparator;
                    if (showSummary)
                        s += aHangingIndent + "..." + lineSeparator;
                    for (std::int64_t i = trailing; i > 1; --i)
                        s += aHangingIndent + recurse(aAxis + 1u, element(-i), nextHangingIndent, nextWidth) + lineSeparator;
                    s += aHangingIndent + recurse(aAxis + 1u, element(-1), nextHangingIndent, nextWidth);
                }
                return "[" + s.substr(aHangingIndent.size()) + "]";
            };
            return recurse(0u, aArray.pointer(), aNextLinePrefix, aLineWidth);
        }

        std::string array2string(ndarray const& aArray, std::string const& aSeparator, std::string const& aPrefix, std::string const& aSuffix)
        {
            auto const lineWidth = static_cast<std::size_t>(options().linewidth) - aSuffix.size();
            if (aArray.size() == 0)
                return "[]";
            bool const summarize = aArray.size() > options().threshold;
            auto const format = make_format(aArray, summarize);
            std::string const nextLinePrefix = " " + std::string(aPrefix.size(), ' ');
            return format_array(aArray, format, lineWidth, nextLinePrefix, aSeparator, summarize);
        }

        bool dtype_is_implied(dtype aType)
        {
            return aType == dtype::Int64 || aType == dtype::Float64 || aType == dtype::Complex128 || aType == dtype::Bool;
        }
    }

    std::string array_repr(ndarray const& aArray)
    {
        std::string const prefix = "array(";
        auto const text = array2string(aArray, ", ", prefix, ")");
        std::vector<std::string> extras;
        if ((aArray.size() == 0 && !(aArray.ndim() == 1 && aArray.shape[0] == 0)) || aArray.size() > options().threshold)
            extras.push_back("shape=" + shape_repr(aArray.shape));
        if (!dtype_is_implied(aArray.type) || aArray.size() == 0)
            extras.push_back(std::string{ "dtype=" } + info(aArray.type).name);
        if (extras.empty())
            return prefix + text + ")";
        std::string const arrayText = prefix + text + ",";
        std::string extraText;
        for (std::size_t i = 0u; i < extras.size(); ++i)
            extraText += (i != 0u ? ", " : "") + extras[i];
        extraText += ")";
        auto const lastLine = arrayText.size() - (arrayText.rfind('\n') == std::string::npos ? 0u : arrayText.rfind('\n') + 1u);
        std::string spacer = " ";
        if (lastLine + extraText.size() + 1u > static_cast<std::size_t>(options().linewidth))
            spacer = "\n" + std::string(prefix.size(), ' ');
        return arrayText + spacer + extraText;
    }

    std::string array_str(ndarray const& aArray)
    {
        if (aArray.ndim() == 0)
            return scalar_str(aArray);
        return array2string(aArray, " ", "", "");
    }

    namespace
    {
        // a float scalar: positional (the shortest digits) if 1e-4 <= |x| < 10**precision, else scientific
        std::string float_scalar(double aValue, dtype aType, bool aRepr)
        {
            if (std::isnan(aValue))
                return "nan";
            if (std::isinf(aValue))
                return aValue < 0.0 ? "-inf" : "inf";
            double const cutoff = aType == dtype::Float16 ? 1e3 : (aType == dtype::Float32 || aType == dtype::Complex64) ? 1e6 : 1e16;
            auto const absolute = std::fabs(aValue);
            float_options f;
            f.unique = true;
            (void)aRepr;
            if (absolute == 0.0 || (absolute < cutoff && absolute >= 1e-4))
            {
                f.trim = '0';
                return positional(aValue, aType, f);
            }
            f.trim = '-';
            return scientific(aValue, aType, f);
        }

        // (the parts of a complex scalar as Python shows them: "1+2j", "(1.5-0j)", ...)
        std::string complex_part(double aValue, dtype aType)
        {
            if (std::isnan(aValue))
                return "nan";
            if (std::isinf(aValue))
                return aValue < 0.0 ? "-inf" : "inf";
            auto const absolute = std::fabs(aValue);
            double const cutoff = aType == dtype::Complex64 ? 1e6 : 1e16;
            float_options f;
            f.unique = true;
            if (absolute == 0.0 || (absolute < cutoff && absolute >= 1e-4))
            {
                f.trim = '-';
                return positional(aValue, aType, f);
            }
            f.trim = '-';
            return scientific(aValue, aType, f);
        }

        std::string complex_scalar(std::complex<double> aValue, dtype aType)
        {
            auto const re = aValue.real();
            auto const im = aValue.imag();
            std::string imag = complex_part(im, aType);
            if (re == 0.0 && !std::signbit(re))
                return imag + "j";
            std::string const real = complex_part(re, aType);
            bool const negative = std::signbit(im) || (std::isnan(im) && std::signbit(im));
            return "(" + real + (negative ? "" : "+") + imag + "j)";
        }
    }

    std::string format_double_repr(double aValue)
    {
        return float_scalar(aValue, dtype::Float64, true);
    }

    std::string scalar_str(ndarray const& aScalar)
    {
        auto const v = load(aScalar.pointer(), aScalar.type);
        if (is_bool(aScalar.type))
            return v.b ? "True" : "False";
        if (is_integer(aScalar.type))
            return integer_text(v);
        if (is_float(aScalar.type))
            return float_scalar(v.f, aScalar.type, false);
        return complex_scalar(v.c, aScalar.type);
    }

    std::string scalar_repr(ndarray const& aScalar)
    {
        if (is_bool(aScalar.type))
            return load(aScalar.pointer(), aScalar.type).b ? "np.True_" : "np.False_";
        return std::string{ "np." } + info(aScalar.type).scalarName + "(" + scalar_str(aScalar) + ")";
    }

    // --- the printing functions of the module

    namespace
    {
        // (the print options given as arguments, from aFirst: precision, threshold, edgeitems, linewidth, suppress, nanstr, infstr, formatter, sign, floatmode)
        bool apply_options(arguments const& aArgs, std::size_t aPrecision, std::size_t aThreshold, std::size_t aEdgeitems, std::size_t aLinewidth,
            std::size_t aSuppress, std::size_t aNanstr, std::size_t aInfstr, std::size_t aSign, std::size_t aFloatmode, print_options& aOptions)
        {
            std::int64_t value = 0;
            auto const integer = [&](std::size_t aIndex, auto& aResult) -> bool
            {
                if (aIndex == ~std::size_t{} || !aArgs.has(aIndex))
                    return true;
                if (!to_int(aArgs[aIndex], value))
                    return false;
                aResult = static_cast<std::remove_reference_t<decltype(aResult)>>(value);
                return true;
            };
            auto const text = [&](std::size_t aIndex, std::string& aResult) -> bool
            {
                if (aIndex == ~std::size_t{} || !aArgs.has(aIndex))
                    return true;
                auto const s = as_str(aArgs[aIndex]);
                if (!s)
                {
                    error("TypeError", "expected a string");
                    return false;
                }
                aResult = *s;
                return true;
            };
            if (!integer(aPrecision, aOptions.precision) || !integer(aThreshold, aOptions.threshold) || !integer(aEdgeitems, aOptions.edgeitems) ||
                !integer(aLinewidth, aOptions.linewidth))
                return false;
            if (aSuppress != ~std::size_t{} && aArgs.has(aSuppress) && !to_bool(aArgs[aSuppress], aOptions.suppress))
                return false;
            if (!text(aNanstr, aOptions.nanstr) || !text(aInfstr, aOptions.infstr) || !text(aSign, aOptions.sign) || !text(aFloatmode, aOptions.floatmode))
                return false;
            if (aOptions.sign != "-" && aOptions.sign != "+" && aOptions.sign != " ")
            {
                error("ValueError", "sign option must be one of ' ', '+', or '-'");
                return false;
            }
            if (aOptions.floatmode != "fixed" && aOptions.floatmode != "unique" && aOptions.floatmode != "maxprec" && aOptions.floatmode != "maxprec_equal")
            {
                error("ValueError", "floatmode option must be one of 'fixed', 'unique', 'maxprec', 'maxprec_equal'");
                return false;
            }
            return true;
        }

        constexpr std::size_t None = ~std::size_t{};

        object* f_set_printoptions(object* aArgs, object* aKwargs)
        {
            arguments args{ "set_printoptions", aArgs, aKwargs, { "precision", "threshold", "edgeitems", "linewidth", "suppress", "nanstr", "infstr", "formatter",
                "sign", "floatmode", "*legacy", "*override_repr" } };
            if (!args.ok())
                return nullptr;
            auto o = options();
            if (!apply_options(args, 0, 1, 2, 3, 4, 5, 6, 8, 9, o))
                return nullptr;
            options() = o;
            return new_none();
        }

        object* f_get_printoptions(object* aArgs, object* aKwargs)
        {
            arguments args{ "get_printoptions", aArgs, aKwargs, {} };
            if (!args.ok())
                return nullptr;
            auto const& o = options();
            auto const d = ctx->dict_new(ctx);
            auto const set = [&](char const* aKey, object* aValue)
            {
                ref const key{ make_str(aKey) };
                ref const value{ aValue };
                ctx->dict_set(ctx, d, key.get(), value.get());
            };
            set("edgeitems", make_int(o.edgeitems));
            set("threshold", make_int(o.threshold));
            set("floatmode", make_str(o.floatmode));
            set("precision", make_int(o.precision));
            set("suppress", make_bool(o.suppress));
            set("linewidth", make_int(o.linewidth));
            set("nanstr", make_str(o.nanstr));
            set("infstr", make_str(o.infstr));
            set("sign", make_str(o.sign));
            set("formatter", new_none());
            set("legacy", make_bool(false));
            set("override_repr", new_none());
            return d;
        }

        object* f_array2string(object* aArgs, object* aKwargs)
        {
            arguments args{ "array2string", aArgs, aKwargs, { "a", "max_line_width", "precision", "suppress_small", "separator", "prefix", "*formatter",
                "*threshold", "*edgeitems", "*sign", "*floatmode", "*suffix", "*legacy" } };
            if (!args.ok())
                return nullptr;
            auto const v = array_ref(args[0], {});
            if (!v)
                return nullptr;
            auto o = options();
            if (!apply_options(args, 2, 7, 8, 1, 3, None, None, 9, 10, o))
                return nullptr;
            auto const separator = args.has(4) ? as_str(args[4]).value_or(" ") : std::string{ " " };
            auto const prefix = args.has(5) ? as_str(args[5]).value_or("") : std::string{};
            auto const suffix = args.has(11) ? as_str(args[11]).value_or("") : std::string{};
            auto const saved = options();
            options() = o;
            auto const text = array2string(*array_of(v), separator, prefix, suffix);
            options() = saved;
            return make_str(text);
        }

        object* array_text(char const* aName, object* aArgs, object* aKwargs, bool aRepr)
        {
            arguments args{ aName, aArgs, aKwargs, { "a", "max_line_width", "precision", "suppress_small" } };
            if (!args.ok())
                return nullptr;
            auto const v = array_ref(args[0], {});
            if (!v)
                return nullptr;
            auto o = options();
            if (!apply_options(args, 2, None, None, 1, 3, None, None, None, None, o))
                return nullptr;
            auto const saved = options();
            options() = o;
            auto const* a = array_of(v);
            auto const text = aRepr ? (a->scalar ? scalar_repr(*a) : array_repr(*a)) : (a->scalar ? scalar_str(*a) : array_str(*a));
            options() = saved;
            return make_str(text);
        }

        object* format_float(char const* aName, object* aArgs, object* aKwargs, bool aPositional)
        {
            arguments args = aPositional ?
                arguments{ aName, aArgs, aKwargs, { "x", "precision", "unique", "fractional", "trim", "sign", "pad_left", "pad_right", "min_digits" } } :
                arguments{ aName, aArgs, aKwargs, { "x", "precision", "unique", "trim", "sign", "pad_left", "exp_digits", "min_digits" } };
            if (!args.ok())
                return nullptr;
            dtype type = dtype::Float64;
            double x = 0.0;
            if (is_array(args[0]) && as_array(args[0])->ndim() == 0 && is_float(as_array(args[0])->type))
            {
                type = as_array(args[0])->type;
                x = read_as<double>(as_array(args[0])->pointer(), type);
            }
            else if (kind(args[0]) == NEOS_PY_FLOAT)
                to_double(args[0], x);
            else
                return error("TypeError", "not a float");
            float_options f;
            std::int64_t v = 0;
            auto const integer = [&](std::size_t aIndex, int& aResult) -> bool
            {
                if (!args.has(aIndex))
                    return true;
                if (!to_int(args[aIndex], v))
                    return false;
                aResult = static_cast<int>(v);
                return true;
            };
            if (!integer(1, f.precision))
                return nullptr;
            if (args.has(2) && !to_bool(args[2], f.unique))
                return nullptr;
            std::size_t const trimIndex = aPositional ? 4u : 3u;
            if (args.has(trimIndex))
            {
                auto const t = as_str(args[trimIndex]).value_or("k");
                if (t != "k" && t != "." && t != "0" && t != "-")
                    return error("ValueError", "Trim mode must be one of 'k', '.', '0', or '-'");
                f.trim = t[0];
            }
            if (args.has(trimIndex + 1u) && !to_bool(args[trimIndex + 1u], f.sign))
                return nullptr;
            if (!integer(trimIndex + 2u, f.padLeft))
                return nullptr;
            if (aPositional ? !integer(7, f.padRight) : !integer(6, f.expDigits))
                return nullptr;
            if (!integer(aPositional ? 8u : 7u, f.minDigits))
                return nullptr;
            if (!f.unique && f.precision < 0)
                return error("ValueError", "precision must be provided when unique is False");
            if (f.minDigits > 0 && f.precision >= 0 && f.minDigits > f.precision)
                return error("ValueError", "min_digits must be less than or equal to precision");
            if (std::isnan(x))
                return make_str(std::string{ f.sign ? "+" : "" } + "nan");
            if (std::isinf(x))
                return make_str(std::string{ x < 0 ? "-" : (f.sign ? "+" : "") } + "inf");
            if (aPositional)
            {
                bool fractional = true;
                if (args.has(3) && !to_bool(args[3], fractional))
                    return nullptr;
                if (!fractional && f.precision == 0)
                    return error("ValueError", "precision must be greater than 0 if fractional=False");
                if (!fractional)
                {
                    // (precision: significant digits)
                    auto d = f.unique ? shortest(x, type) : rounded_significant(x, f.precision);
                    if (f.unique && f.precision >= 0 && static_cast<int>(d.digits.size()) > f.precision)
                        d = rounded_significant(x, f.precision);
                    // (positional from those digits)
                    std::string integral, fraction;
                    if (d.digits == "0")
                        integral = "0";
                    else if (d.exponent <= 0)
                    {
                        integral = "0";
                        fraction = std::string(static_cast<std::size_t>(-d.exponent), '0') + d.digits;
                    }
                    else if (d.exponent >= static_cast<int>(d.digits.size()))
                        integral = d.digits + std::string(static_cast<std::size_t>(d.exponent) - d.digits.size(), '0');
                    else
                    {
                        integral = d.digits.substr(0u, static_cast<std::size_t>(d.exponent));
                        fraction = d.digits.substr(static_cast<std::size_t>(d.exponent));
                    }
                    bool dropPoint = false;
                    fraction = apply_trim(fraction, f, dropPoint);
                    std::string left = (std::signbit(x) ? "-" : f.sign ? "+" : "") + integral;
                    if (f.padLeft > static_cast<int>(left.size()))
                        left.insert(0u, static_cast<std::size_t>(f.padLeft) - left.size(), ' ');
                    std::string result = left + (dropPoint ? "" : ".") + fraction;
                    if (f.padRight > static_cast<int>(fraction.size()))
                        result.append(static_cast<std::size_t>(f.padRight) - fraction.size(), ' ');
                    return make_str(result);
                }
                return make_str(positional(x, type, f));
            }
            return make_str(scientific(x, type, f));
        }

        object* f_format_float_positional(object* aArgs, object* aKwargs) { return format_float("format_float_positional", aArgs, aKwargs, true); }
        object* f_format_float_scientific(object* aArgs, object* aKwargs) { return format_float("format_float_scientific", aArgs, aKwargs, false); }

        object* f_array_str(object* aArgs, object* aKwargs) { return array_text("array_str", aArgs, aKwargs, false); }
        object* f_array_repr(object* aArgs, object* aKwargs) { return array_text("array_repr", aArgs, aKwargs, true); }
    }

    void register_format()
    {
        register_functions({ { "set_printoptions", f_set_printoptions }, { "get_printoptions", f_get_printoptions }, { "array2string", f_array2string },
            { "array_str", f_array_str }, { "array_repr", f_array_repr }, { "format_float_positional", f_format_float_positional },
            { "format_float_scientific", f_format_float_scientific } });
    }
}
