/*
  intl.hpp

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

// Intl (ECMA-402): the locale machinery (language tags, locale lists and negotiation), the option getters and the Intl objects'
// common representation, shared by the Intl built-ins (intl*.cpp). ICU (its C API: intl_icu.hpp) provides the locale data and the
// formatting; without it (NEOS_JS_ICU undefined) there is no Intl, and the locale-sensitive methods are ECMA-262's fallbacks.

#pragma once

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include "js.hpp"

namespace neos::javascript_runtime::intl
{
    // --- Unicode locale identifiers (UTS 35's BCP 47 syntax): parsed, validated and canonicalized (intl_locale.cpp)

    struct language_id
    {
        std::string language;               ///< (lowercase; "und" if absent)
        std::string script;                 ///< (titlecase)
        std::string region;                 ///< (uppercase, or three digits)
        std::vector<std::string> variants;  ///< (lowercase)
    };

    struct locale_id : language_id
    {
        bool unicodeExtension = false;      ///< (a -u- extension)
        std::vector<std::string> attributes;
        std::vector<std::pair<std::string, std::string>> keywords;  ///< (key, value: "" if none)
        bool transformedExtension = false;  ///< (a -t- extension)
        std::optional<language_id> tlang;
        std::vector<std::pair<std::string, std::string>> tfields;   ///< (key, value (its subtags, joined by '-'))
        std::vector<std::pair<char, std::string>> otherExtensions;  ///< (singleton, its subtags joined by '-')
        std::string privateUse;             ///< (the subtags after x-, joined by '-')
    public:
        std::string to_string() const;
        std::string base_name() const;      ///< (the language, script, region and variants)
        std::optional<std::string> keyword(std::string_view aKey) const;   ///< (a -u- keyword's value ("" if it has none))
    };

    std::string language_id_string(language_id const& aId);
    std::optional<locale_id> parse_language_tag(std::string_view aTag);   ///< (IsStructurallyValidLanguageTag: none if it is not)
    void canonicalize(locale_id& aLocale);                                   ///< (CanonicalizeUnicodeLocaleId)
    std::string canonicalize_language_tag(std::string_view aTag);           ///< (aTag structurally valid)
    std::string canonicalize_u_value(std::string_view aKey, std::string_view aValue); ///< (CanonicalizeUValue: lowercase)
    bool is_well_formed_unit_identifier(std::u16string_view aUnit);
    bool is_well_formed_currency_code(std::u16string_view aCurrency);
    bool is_sanctioned_single_unit(std::string_view aUnit);
    std::string icu_locale(std::string_view aTag);                         ///< (a language tag as ICU's locale ID)
    std::string language_tag(char const* aIcuLocale);                       ///< (an ICU locale ID as a canonical language tag)

    // --- locale lists and negotiation (intl.cpp)

    enum class service : std::uint8_t
    {
        Collator,
        DateTimeFormat,
        DisplayNames,
        DurationFormat,
        ListFormat,
        NumberFormat,
        PluralRules,
        RelativeTimeFormat,
        Segmenter,
        Count
    };

    std::vector<std::string> canonicalize_locale_list(value aLocales);
    std::string const& default_locale();
    bool is_available_locale(service aService, std::string const& aTag);
    std::optional<std::string> lookup_matching_locale(service aService, std::string const& aNoExtensionsLocale);

    struct resolved_locale
    {
        std::string locale;         ///< (with the supported -u- keywords of the request)
        std::string dataLocale;     ///< (the available locale found)
        std::map<std::string, std::string, std::less<>> keywords;      ///< (the relevant extension keys' values ("" none))
    };

    // (the relevant extension keys of a service: aKeyData gives the values a locale supports for a key (its default first; a key
    // whose values are all accepted (e.g. a valid numbering system) checks the value with aKeyValid))
    struct relevant_key
    {
        char const* key;                    ///< (e.g. "co")
        std::optional<std::string> option;  ///< (the corresponding option's value, if given (already canonicalized))
    };
    struct locale_data
    {
        virtual ~locale_data() = default;
        virtual std::vector<std::string> values(std::string const& aDataLocale, std::string_view aKey) const = 0; ///< (the default first)
        virtual bool supports(std::string const& aDataLocale, std::string_view aKey, std::string const& aValue) const;
    };
    resolved_locale resolve_locale(service aService, std::vector<std::string> const& aRequested, std::u16string_view aMatcher,
        std::vector<relevant_key> const& aKeys, locale_data const& aData);
    bool is_type_sequence(std::u16string_view aValue);     ///< (a Unicode locale type: (3*8alphanum) *("-" (3*8alphanum)))
    std::string default_numbering_system(std::string const& aDataLocale);
    bool is_supported_numbering_system(std::string const& aDataLocale, std::string const& aValue);  ///< (the default, or ICU's not algorithmic)
    value supported_locales(service aService, value aLocales, value aOptions);  ///< (FilterLocales: an array)
    std::string insert_unicode_extension(std::string const& aLocale, std::vector<std::pair<std::string, std::string>> const& aKeywords);

    // --- options (intl.cpp)

    value get_options_object(value aOptions);           ///< (GetOptionsObject: undefined -> a null-prototype object; an object; else TypeError)
    value coerce_options_to_object(value aOptions);     ///< (CoerceOptionsToObject: undefined -> a null-prototype object; else ToObject)
    // (GetOption of a string (aValues: the values allowed; empty: any); none if the option is undefined (aDefault, if any))
    std::optional<std::u16string> get_string_option(value aOptions, char const* aProperty, std::initializer_list<char const*> aValues,
        std::optional<char const*> aDefault = {});
    std::optional<bool> get_boolean_option(value aOptions, char const* aProperty, std::optional<bool> aDefault = {});
    std::optional<int> get_number_option(value aOptions, char const* aProperty, int aMinimum, int aMaximum, std::optional<int> aFallback);
    std::optional<int> default_number_option(value aValue, int aMinimum, int aMaximum, std::optional<int> aFallback, char const* aProperty);
    std::string to_ascii(std::u16string_view aText);   ///< (an option's value (ASCII expected: other code units become '?'))
    std::u16string from_ascii(std::string_view aText);

    // --- number formatting's digit options (SetNumberFormatDigitOptions: NumberFormat's and PluralRules')

    struct digit_options
    {
        int minimumIntegerDigits = 1;
        std::optional<int> minimumFractionDigits;
        std::optional<int> maximumFractionDigits;
        std::optional<int> minimumSignificantDigits;
        std::optional<int> maximumSignificantDigits;
        int roundingIncrement = 1;
        std::string roundingMode = "halfExpand";
        std::string roundingType = "fractionDigits";        ///< (fractionDigits, significantDigits, morePrecision or lessPrecision)
        std::string computedRoundingPriority = "auto";
        std::string trailingZeroDisplay = "auto";
    };
    void set_number_format_digit_options(digit_options& aOptions, value aOptionsObject, int aMnfdDefault, int aMxfdDefault,
        std::string_view aNotation);
    std::string digit_options_skeleton(digit_options const& aOptions);     ///< (ICU number skeleton stems: precision, rounding, integer width)
    void digit_options_resolved(digit_options const& aOptions, value aObject, bool aRoundingFields);   ///< (resolvedOptions' digit fields)
    void digit_options_rounding_resolved(digit_options const& aOptions, value aObject);    ///< (roundingIncrement ... trailingZeroDisplay)

    // --- the Intl objects

    // (a service's own data (ICU's handles and the resolved options), by the object that owns it)
    struct intl_data
    {
        virtual ~intl_data() = default;
        virtual void trace(tracer&) {}
    };

    class intl_object : public object
    {
    public:
        intl_object(value aPrototype, object_class aClass) : object{ aPrototype, aClass } {}
    public:
        void trace(tracer& aTracer) override;
    public:
        std::string locale;                     ///< ([[Locale]])
        value bound = Undefined;                ///< (the bound function of its format/compare getter, once made)
        value extra = Undefined;                ///< (a service's other value (e.g. a Segments object's string))
        std::unique_ptr<intl_data> data;
    };

    template <typename Data>
    Data& data_of(intl_object& aObject)
    {
        return static_cast<Data&>(*aObject.data);
    }

    intl_object* as_intl(value aValue, object_class aClass);
    intl_object& this_intl(value aThis, object_class aClass, char const* aMethod);     ///< (TypeError if it is not one)
    value make_intl_object(value aNewTarget, value realm::*aDefaultPrototype, object_class aClass);     ///< (OrdinaryCreateFromConstructor)
    value make_intl_constructor(realm& aRealm, char const* aName, std::uint32_t aLength, native_function aFunction, value realm::*aPrototype,
        char const* aTag);  ///< (Intl[aName] with its prototype (its toStringTag aTag) and supportedLocalesOf)

    // (formatToParts' parts: {type, value[, source/unit]})
    struct part
    {
        std::u16string type;
        std::u16string text;
        std::optional<std::u16string> source;   ///< (formatRangeToParts: startRange, endRange or shared)
        std::optional<std::u16string> unit;     ///< (RelativeTimeFormat, DurationFormat: the unit)
    };
    value parts_array(std::vector<part> const& aParts);

    // (an ICU formatted value's fields, as spans of its text: parts of the text (the innermost field of each code unit: its type by
    // aType (none: a literal); runs of a field are one part (a field that recurs adjacently, e.g. two integer groups, is a part each)))
    struct field_span
    {
        std::int32_t category;
        std::int32_t field;
        std::int32_t begin;
        std::int32_t end;
    };
    using field_type = std::optional<std::u16string> (*)(field_span const& aSpan, std::u16string_view aText, void* aContext);
    std::vector<part> make_parts(std::u16string const& aText, std::vector<field_span> const& aSpans, field_type aType, void* aContext);
    value u16_value(std::u16string_view aText);

    // --- the services (intl_*.cpp)

    void init_intl_locale(realm& aRealm, value aIntl);
    void init_intl_collator(realm& aRealm, value aIntl);
    void init_intl_number_format(realm& aRealm, value aIntl);
    void init_intl_plural_rules(realm& aRealm, value aIntl);
    void init_intl_date_time_format(realm& aRealm, value aIntl);
    void init_intl_relative_time_format(realm& aRealm, value aIntl);
    void init_intl_list_format(realm& aRealm, value aIntl);
    void init_intl_display_names(realm& aRealm, value aIntl);
    void init_intl_segmenter(realm& aRealm, value aIntl);
    void init_intl_duration_format(realm& aRealm, value aIntl);

    // (a NumberFormat's parts of an exact decimal (e.g. "-12.5"; "-0" negative zero) (DurationFormat's))
    std::vector<part> format_decimal_to_parts(value aNumberFormat, std::string const& aDecimal);

    // (a list's parts (ListFormat's: type conjunction, disjunction or unit; style long, short or narrow) (DurationFormat's))
    std::vector<part> format_list_to_parts(std::string const& aLocale, std::string const& aType, std::string const& aStyle,
        std::vector<std::u16string> const& aList);

    // (the locale-sensitive methods of ECMA-262 (ECMA-402's definitions of them))
    value locale_compare(value aThis, value aThat, value aLocales, value aOptions);         ///< (String.prototype.localeCompare)
    value to_locale_case(value aString, value aLocales, bool aUpper);                       ///< (String.prototype.toLocaleUpper/LowerCase)
    value number_to_locale_string(value aNumber, value aLocales, value aOptions);           ///< (Number.prototype.toLocaleString, BigInt's)
    value date_to_locale_string(double aTime, value aLocales, value aOptions, int aRequired, int aDefaults);   ///< (0 any/all, 1 date, 2 time)
    value temporal_to_locale_string(value aTemporal, value aLocales, value aOptions);      ///< (the Temporal objects' toLocaleString)
    value duration_to_locale_string(value aDuration, value aLocales, value aOptions);      ///< (Temporal.Duration.prototype.toLocaleString)
}
