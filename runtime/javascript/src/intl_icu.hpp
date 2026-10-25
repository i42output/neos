/*
  intl_icu.hpp

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

// ICU for Intl (ECMA-402): its C API only (Windows provides ICU as a system DLL with the C API alone (the Windows SDK's icu.h);
// elsewhere the system's libicu), and the handles it returns as unique_ptrs that close them.

#pragma once

#if defined(NEOS_JS_ICU)

#if defined(NEOS_JS_WINDOWS_ICU)
#include <icu.h>
#else
#include <unicode/utypes.h>
#include <unicode/uloc.h>
#include <unicode/ures.h>
#include <unicode/ustring.h>
#include <unicode/ucol.h>
#include <unicode/ucal.h>
#include <unicode/udat.h>
#include <unicode/udatpg.h>
#include <unicode/udateintervalformat.h>
#include <unicode/unum.h>
#include <unicode/unumberformatter.h>
#if U_ICU_VERSION_MAJOR_NUM >= 69
#include <unicode/unumberrangeformatter.h>
#endif
#include <unicode/unumsys.h>
#include <unicode/upluralrules.h>
#include <unicode/ureldatefmt.h>
#include <unicode/ulistformatter.h>
#include <unicode/uldnames.h>
#include <unicode/ucurr.h>
#include <unicode/ubrk.h>
#include <unicode/uformattedvalue.h>
#include <unicode/ufieldpositer.h>
#include <unicode/uenum.h>
#include <unicode/uchar.h>
#endif

// (ICU 67 is the oldest ICU supported: the number range formatter (unumrf) and its range spans are ICU 69's)
#if defined(NEOS_JS_WINDOWS_ICU) || U_ICU_VERSION_MAJOR_NUM >= 69
#define NEOS_JS_ICU_NUMBER_RANGES
#endif

#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include "intl.hpp"

namespace neos::javascript_runtime::intl
{
    template <typename T, void (*Close)(T*)>
    struct icu_closer
    {
        void operator()(T* aHandle) const { if (aHandle != nullptr) Close(aHandle); }
    };
    template <typename T, void (*Close)(T*)>
    using icu_ptr = std::unique_ptr<T, icu_closer<T, Close>>;

    using collator_ptr = icu_ptr<UCollator, ucol_close>;
    using number_formatter_ptr = icu_ptr<UNumberFormatter, unumf_close>;
    using formatted_number_ptr = icu_ptr<UFormattedNumber, unumf_closeResult>;
#if defined(NEOS_JS_ICU_NUMBER_RANGES)
    using number_range_formatter_ptr = icu_ptr<UNumberRangeFormatter, unumrf_close>;
    using formatted_number_range_ptr = icu_ptr<UFormattedNumberRange, unumrf_closeResult>;
#endif
    using plural_rules_ptr = icu_ptr<UPluralRules, uplrules_close>;
    using date_format_ptr = icu_ptr<UDateFormat, udat_close>;
    using date_pattern_generator_ptr = icu_ptr<UDateTimePatternGenerator, udatpg_close>;
    using date_interval_format_ptr = icu_ptr<UDateIntervalFormat, udtitvfmt_close>;
    using formatted_date_interval_ptr = icu_ptr<UFormattedDateInterval, udtitvfmt_closeResult>;
    using calendar_ptr = icu_ptr<UCalendar, ucal_close>;
    using number_format_ptr = icu_ptr<UNumberFormat, unum_close>;
    using relative_date_format_ptr = icu_ptr<URelativeDateTimeFormatter, ureldatefmt_close>;
    using formatted_relative_date_ptr = icu_ptr<UFormattedRelativeDateTime, ureldatefmt_closeResult>;
    using list_formatter_ptr = icu_ptr<UListFormatter, ulistfmt_close>;
    using formatted_list_ptr = icu_ptr<UFormattedList, ulistfmt_closeResult>;
    using display_names_ptr = icu_ptr<ULocaleDisplayNames, uldn_close>;
    using break_iterator_ptr = icu_ptr<UBreakIterator, ubrk_close>;
    using enumeration_ptr = icu_ptr<UEnumeration, uenum_close>;
    using resource_ptr = icu_ptr<UResourceBundle, ures_close>;
    using constrained_position_ptr = icu_ptr<UConstrainedFieldPosition, ucfpos_close>;
    using field_position_iterator_ptr = icu_ptr<UFieldPositionIterator, ufieldpositer_close>;
    using numbering_system_ptr = icu_ptr<UNumberingSystem, unumsys_close>;

    // (ICU's UTF-16 is char16_t: a std::u16string's data is a UChar*)
    static_assert(sizeof(UChar) == sizeof(char16_t));
    inline UChar const* uchars(std::u16string_view aText) { return reinterpret_cast<UChar const*>(aText.data()); }
    inline std::int32_t ulength(std::u16string_view aText) { return static_cast<std::int32_t>(aText.size()); }

    // (a function that writes UTF-16 into a buffer (aBuffer, aCapacity, &status) and returns its length: its result (preflighted))
    template <typename F>
    std::u16string icu_string(F&& aWrite)
    {
        std::u16string result(64u, u'\0');
        UErrorCode status = U_ZERO_ERROR;
        auto length = aWrite(reinterpret_cast<UChar*>(result.data()), static_cast<std::int32_t>(result.size()), &status);
        if (status == U_BUFFER_OVERFLOW_ERROR)
        {
            result.assign(static_cast<std::size_t>(length), u'\0');
            status = U_ZERO_ERROR;
            length = aWrite(reinterpret_cast<UChar*>(result.data()), static_cast<std::int32_t>(result.size()), &status);
        }
        if (U_FAILURE(status) && status != U_STRING_NOT_TERMINATED_WARNING)
            return {};
        result.resize(static_cast<std::size_t>(length));
        return result;
    }

    // (an ICU formatted value's fields (all categories), as spans)
    std::vector<field_span> formatted_value_spans(UFormattedValue const* aValue);
    std::u16string formatted_value_string(UFormattedValue const* aValue);

    // (likewise for chars, e.g. a locale ID)
    template <typename F>
    std::string icu_chars(F&& aWrite)
    {
        std::string result(64u, '\0');
        UErrorCode status = U_ZERO_ERROR;
        auto length = aWrite(result.data(), static_cast<std::int32_t>(result.size()), &status);
        if (status == U_BUFFER_OVERFLOW_ERROR)
        {
            result.assign(static_cast<std::size_t>(length), '\0');
            status = U_ZERO_ERROR;
            length = aWrite(result.data(), static_cast<std::int32_t>(result.size()), &status);
        }
        if (U_FAILURE(status) && status != U_STRING_NOT_TERMINATED_WARNING)
            return {};
        result.resize(static_cast<std::size_t>(length));
        return result;
    }
}

#endif
