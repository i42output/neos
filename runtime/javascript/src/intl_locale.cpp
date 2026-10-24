/*
  intl_locale.cpp

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

// Intl: Unicode locale identifiers (UTS 35: their BCP 47 syntax (IsStructurallyValidLanguageTag), canonical syntax and canonical
// form (CLDR's aliases, from ICU's data: the "metadata" bundle's alias tables and ICU's key and type mappings)) and Intl.Locale.

#if defined(NEOS_JS_ICU)

#include <algorithm>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include "intl.hpp"
#include "intl_icu.hpp"

namespace neos::javascript_runtime::intl
{
    namespace
    {
        // --- characters and subtags

        bool alpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
        bool digit(char c) { return c >= '0' && c <= '9'; }
        bool alphanum(char c) { return alpha(c) || digit(c); }
        char lower(char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; }
        char upper(char c) { return c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c; }
        std::string lowercase(std::string_view s) { std::string r{ s }; for (auto& c : r) c = lower(c); return r; }
        std::string uppercase(std::string_view s) { std::string r{ s }; for (auto& c : r) c = upper(c); return r; }
        std::string titlecase(std::string_view s) { auto r = lowercase(s); if (!r.empty()) r[0] = upper(r[0]); return r; }

        bool all_of(std::string_view s, bool (*aClass)(char)) { return std::all_of(s.begin(), s.end(), aClass); }
        bool is_language_subtag(std::string_view s) { return ((s.size() >= 2u && s.size() <= 3u) || (s.size() >= 5u && s.size() <= 8u)) && all_of(s, alpha); }
        bool is_script_subtag(std::string_view s) { return s.size() == 4u && all_of(s, alpha); }
        bool is_region_subtag(std::string_view s) { return (s.size() == 2u && all_of(s, alpha)) || (s.size() == 3u && all_of(s, digit)); }
        bool is_variant_subtag(std::string_view s)
        {
            return ((s.size() >= 5u && s.size() <= 8u) && all_of(s, alphanum)) || (s.size() == 4u && digit(s[0]) && all_of(s, alphanum));
        }
        bool is_type_subtag(std::string_view s) { return s.size() >= 3u && s.size() <= 8u && all_of(s, alphanum); }
        bool is_ukey(std::string_view s) { return s.size() == 2u && alphanum(s[0]) && alpha(s[1]); }
        bool is_tkey(std::string_view s) { return s.size() == 2u && alpha(s[0]) && digit(s[1]); }

        std::vector<std::string_view> split(std::string_view aText, char aSeparator)
        {
            std::vector<std::string_view> result;
            std::size_t start = 0u;
            for (;;)
            {
                auto const end = aText.find(aSeparator, start);
                result.push_back(aText.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start));
                if (end == std::string_view::npos)
                    break;
                start = end + 1u;
            }
            return result;
        }

        std::string join(std::vector<std::string> const& aParts, char aSeparator)
        {
            std::string result;
            for (auto const& p : aParts)
            {
                if (!result.empty())
                    result.push_back(aSeparator);
                result += p;
            }
            return result;
        }

        // (a unicode_language_id's subtags from aSubtags[aIndex] on (not "root", and a language first); none if they are not one)
        bool parse_language_id(std::vector<std::string_view> const& aSubtags, std::size_t& aIndex, language_id& aId, bool aLanguageRequired = true)
        {
            if (aIndex >= aSubtags.size() || !is_language_subtag(aSubtags[aIndex]))
                return !aLanguageRequired;
            aId.language = lowercase(aSubtags[aIndex++]);
            if (aIndex < aSubtags.size() && is_script_subtag(aSubtags[aIndex]))
                aId.script = titlecase(aSubtags[aIndex++]);
            if (aIndex < aSubtags.size() && is_region_subtag(aSubtags[aIndex]))
                aId.region = uppercase(aSubtags[aIndex++]);
            while (aIndex < aSubtags.size() && is_variant_subtag(aSubtags[aIndex]))
            {
                auto v = lowercase(aSubtags[aIndex++]);
                if (std::find(aId.variants.begin(), aId.variants.end(), v) != aId.variants.end())
                    return false;   // (a duplicate variant)
                aId.variants.push_back(std::move(v));
            }
            return true;
        }
    }

    std::string language_id_string(language_id const& aId)
    {
        std::string result = aId.language.empty() ? std::string{ "und" } : aId.language;
        if (!aId.script.empty())
            result += "-" + aId.script;
        if (!aId.region.empty())
            result += "-" + aId.region;
        for (auto const& v : aId.variants)
            result += "-" + v;
        return result;
    }

    std::string locale_id::base_name() const
    {
        return language_id_string(*this);
    }

    std::string locale_id::to_string() const
    {
        // (extensions in their singletons' order (as canonical syntax has them): -t- and -u- among the others, then -x-)
        std::vector<std::pair<char, std::string>> extensions = otherExtensions;
        if (transformedExtension)
        {
            std::string t;
            if (tlang)
            {
                // (a tlang is lowercase)
                t = language_id_string(*tlang);
                for (auto& c : t)
                    c = lower(c);
            }
            for (auto const& [key, val] : tfields)
                t += (t.empty() ? "" : "-") + key + "-" + val;
            extensions.emplace_back('t', t);
        }
        if (unicodeExtension)
        {
            std::string u;
            for (auto const& a : attributes)
                u += (u.empty() ? "" : "-") + a;
            for (auto const& [key, val] : keywords)
            {
                u += (u.empty() ? "" : "-") + key;
                if (!val.empty())
                    u += "-" + val;
            }
            extensions.emplace_back('u', u);
        }
        std::stable_sort(extensions.begin(), extensions.end(), [](auto const& a, auto const& b) { return a.first < b.first; });
        std::string result = base_name();
        for (auto const& [singleton, text] : extensions)
        {
            result += "-";
            result.push_back(singleton);
            if (!text.empty())
                result += "-" + text;
        }
        if (!privateUse.empty())
            result += "-x-" + privateUse;
        return result;
    }

    std::optional<std::string> locale_id::keyword(std::string_view aKey) const
    {
        for (auto const& [key, val] : keywords)
            if (key == aKey)
                return val;
        return {};
    }

    std::optional<locale_id> parse_language_tag(std::string_view aTag)
    {
        // IsStructurallyValidLanguageTag: unicode_locale_id (BCP 47 syntax only (no "root", no script first, '-' separators)), no
        // duplicate variants (also in a -t- extension's tlang), no duplicate singletons
        if (aTag.empty())
            return {};
        for (auto c : aTag)
            if (!alphanum(c) && c != '-')
                return {};
        auto const subtags = split(aTag, '-');
        for (auto const& s : subtags)
            if (s.empty() || s.size() > 8u)
                return {};
        locale_id result;
        std::size_t i = 0u;
        if (!parse_language_id(subtags, i, result))
            return {};
        std::string singletons;
        while (i < subtags.size())
        {
            auto const& s = subtags[i];
            if (s.size() != 1u)
                return {};
            auto const singleton = lower(s[0]);
            if (singletons.find(singleton) != std::string::npos)
                return {};
            singletons.push_back(singleton);
            ++i;
            if (singleton == 'x')
            {
                // (private use: the rest)
                if (i >= subtags.size())
                    return {};
                std::vector<std::string> parts;
                for (; i < subtags.size(); ++i)
                    parts.push_back(lowercase(subtags[i]));
                result.privateUse = join(parts, '-');
                break;
            }
            if (singleton == 'u')
            {
                result.unicodeExtension = true;
                bool any = false;
                while (i < subtags.size() && is_type_subtag(subtags[i]))
                {
                    result.attributes.push_back(lowercase(subtags[i++]));
                    any = true;
                }
                while (i < subtags.size() && is_ukey(subtags[i]))
                {
                    auto key = lowercase(subtags[i++]);
                    std::vector<std::string> types;
                    while (i < subtags.size() && is_type_subtag(subtags[i]))
                        types.push_back(lowercase(subtags[i++]));
                    result.keywords.emplace_back(std::move(key), join(types, '-'));
                    any = true;
                }
                if (!any)
                    return {};
            }
            else if (singleton == 't')
            {
                result.transformedExtension = true;
                bool any = false;
                if (i < subtags.size() && is_language_subtag(subtags[i]))
                {
                    language_id tlang;
                    if (!parse_language_id(subtags, i, tlang))
                        return {};
                    result.tlang = std::move(tlang);
                    any = true;
                }
                while (i < subtags.size() && is_tkey(subtags[i]))
                {
                    auto key = lowercase(subtags[i++]);
                    std::vector<std::string> values;
                    while (i < subtags.size() && is_type_subtag(subtags[i]))
                        values.push_back(lowercase(subtags[i++]));
                    if (values.empty())
                        return {};
                    result.tfields.emplace_back(std::move(key), join(values, '-'));
                    any = true;
                }
                if (!any)
                    return {};
            }
            else
            {
                std::vector<std::string> parts;
                while (i < subtags.size() && subtags[i].size() >= 2u)
                    parts.push_back(lowercase(subtags[i++]));
                if (parts.empty())
                    return {};
                result.otherExtensions.emplace_back(singleton, join(parts, '-'));
            }
        }
        return result;
    }

    // --- CLDR's aliases (ICU's data)

    namespace
    {
        std::string ustring_to_ascii(UChar const* aText, std::int32_t aLength)
        {
            std::string result;
            for (std::int32_t i = 0; i < aLength; ++i)
                result.push_back(aText[i] < 0x80u ? static_cast<char>(aText[i]) : '?');
            return result;
        }

        // (an ICU locale ID's language_id (e.g. "sr_Latn", "und_AX", "aa_saaho"))
        language_id icu_language_id(std::string_view aId)
        {
            language_id result;
            auto const parts = split(aId, '_');
            std::size_t i = 0u;
            if (i < parts.size() && is_language_subtag(parts[i]))
            {
                auto const l = lowercase(parts[i++]);
                if (l != "und")
                    result.language = l;
            }
            if (i < parts.size() && is_script_subtag(parts[i]))
                result.script = titlecase(parts[i++]);
            if (i < parts.size() && is_region_subtag(parts[i]))
                result.region = uppercase(parts[i++]);
            for (; i < parts.size(); ++i)
                if (!parts[i].empty())
                    result.variants.push_back(lowercase(parts[i]));
            std::sort(result.variants.begin(), result.variants.end());
            return result;
        }

        struct language_rule
        {
            language_id source;     ///< (an empty language: any)
            language_id replacement;
        };

        struct alias_data
        {
            std::vector<language_rule> languageRules;   ///< (the more specific first)
            std::unordered_map<std::string, std::string> scripts;
            std::unordered_map<std::string, std::vector<std::string>> territories;
            std::unordered_map<std::string, std::string> variants;
            std::unordered_map<std::string, std::string> subdivisions;
        };

        // (a table of the metadata bundle's alias table: key -> replacement)
        void alias_table(UResourceBundle* aAlias, char const* aName, std::vector<std::pair<std::string, std::string>>& aEntries)
        {
            UErrorCode status = U_ZERO_ERROR;
            resource_ptr table{ ures_getByKey(aAlias, aName, nullptr, &status) };
            if (U_FAILURE(status))
                return;
            ures_resetIterator(table.get());
            while (ures_hasNext(table.get()))
            {
                status = U_ZERO_ERROR;
                resource_ptr entry{ ures_getNextResource(table.get(), nullptr, &status) };
                if (U_FAILURE(status))
                    break;
                std::int32_t length = 0;
                auto const* replacement = ures_getStringByKey(entry.get(), "replacement", &length, &status);
                if (U_FAILURE(status) || ures_getKey(entry.get()) == nullptr)
                    continue;
                aEntries.emplace_back(ures_getKey(entry.get()), ustring_to_ascii(replacement, length));
            }
        }

        alias_data const& aliases()
        {
            static alias_data const sData = []()
            {
                alias_data data;
                UErrorCode status = U_ZERO_ERROR;
                resource_ptr metadata{ ures_openDirect(nullptr, "metadata", &status) };
                resource_ptr alias{ ures_getByKey(metadata.get(), "alias", nullptr, &status) };
                if (U_FAILURE(status))
                    return data;
                std::vector<std::pair<std::string, std::string>> entries;
                alias_table(alias.get(), "language", entries);
                for (auto const& [key, replacement] : entries)
                    data.languageRules.push_back({ icu_language_id(key), icu_language_id(replacement) });
                // (the more specific rules first: more variants, then with a region, then with a script)
                std::stable_sort(data.languageRules.begin(), data.languageRules.end(), [](language_rule const& a, language_rule const& b)
                {
                    auto const rank = [](language_id const& id)
                    {
                        return std::make_tuple(id.variants.size(), id.region.empty() ? 0 : 1, id.script.empty() ? 0 : 1, id.language.empty() ? 0 : 1);
                    };
                    return rank(a.source) > rank(b.source);
                });
                entries.clear();
                alias_table(alias.get(), "script", entries);
                for (auto const& [key, replacement] : entries)
                    data.scripts[key] = replacement;
                entries.clear();
                alias_table(alias.get(), "territory", entries);
                for (auto const& [key, replacement] : entries)
                {
                    std::vector<std::string> regions;
                    for (auto r : split(replacement, ' '))
                        if (!r.empty())
                            regions.push_back(std::string{ r });
                    data.territories[key] = std::move(regions);
                }
                entries.clear();
                alias_table(alias.get(), "variant", entries);
                for (auto const& [key, replacement] : entries)
                    data.variants[lowercase(key)] = lowercase(replacement);
                entries.clear();
                alias_table(alias.get(), "subdivision", entries);
                for (auto const& [key, replacement] : entries)
                {
                    // (a subdivision may be replaced by several (or by a region): the first)
                    auto const first = split(replacement, ' ').front();
                    data.subdivisions[lowercase(key)] = lowercase(first);
                }
                return data;
            }();
            return sData;
        }

        bool includes_variants(std::vector<std::string> const& aVariants, std::vector<std::string> const& aSubset)
        {
            for (auto const& v : aSubset)
                if (std::find(aVariants.begin(), aVariants.end(), v) == aVariants.end())
                    return false;
            return true;
        }

        // (the likely region of a language and script (for a territory alias with several replacements))
        std::string likely_region(language_id const& aId)
        {
            language_id id = aId;
            id.region.clear();
            id.variants.clear();
            auto const icu = icu_locale(language_id_string(id));
            auto const maximized = icu_chars([&](char* aBuffer, std::int32_t aCapacity, UErrorCode* aStatus)
                { return uloc_addLikelySubtags(icu.c_str(), aBuffer, aCapacity, aStatus); });
            return icu_chars([&](char* aBuffer, std::int32_t aCapacity, UErrorCode* aStatus)
                { return uloc_getCountry(maximized.c_str(), aBuffer, aCapacity, aStatus); });
        }

        // (UTS 35's alias replacement of a language_id: until no rule applies)
        void replace_aliases(language_id& aId, bool aLanguageOnly = false)
        {
            auto const& data = aliases();
            std::sort(aId.variants.begin(), aId.variants.end());
            for (int pass = 0; pass < 32; ++pass)
            {
                bool changed = false;
                // (a language rule: the first (most specific) that matches)
                auto const language = aId.language == "und" ? std::string{} : aId.language;
                for (auto const& rule : data.languageRules)
                {
                    if (!rule.source.language.empty() && rule.source.language != language)
                        continue;
                    if (rule.source.language.empty() && rule.source.variants.empty())
                        continue;
                    if (!rule.source.script.empty() && rule.source.script != aId.script)
                        continue;
                    if (!rule.source.region.empty() && rule.source.region != aId.region)
                        continue;
                    if (!includes_variants(aId.variants, rule.source.variants))
                        continue;
                    auto const& r = rule.replacement;
                    if (!rule.source.language.empty())
                        aId.language = r.language.empty() ? std::string{ "und" } : r.language;
                    else if ((aId.language.empty() || aId.language == "und") && !r.language.empty())
                        aId.language = r.language;
                    if (!rule.source.script.empty())
                        aId.script = r.script;
                    else if (aId.script.empty() && !r.script.empty())
                        aId.script = r.script;
                    if (!rule.source.region.empty())
                        aId.region = r.region;
                    else if (aId.region.empty() && !r.region.empty())
                        aId.region = r.region;
                    std::vector<std::string> variants;
                    for (auto const& v : aId.variants)
                        if (std::find(rule.source.variants.begin(), rule.source.variants.end(), v) == rule.source.variants.end())
                            variants.push_back(v);
                    for (auto const& v : r.variants)
                        if (std::find(variants.begin(), variants.end(), v) == variants.end())
                            variants.push_back(v);
                    std::sort(variants.begin(), variants.end());
                    aId.variants = std::move(variants);
                    changed = true;
                    break;
                }
                if (changed)
                    continue;
                if (aLanguageOnly)
                    break;
                if (!aId.script.empty())
                    if (auto const s = data.scripts.find(aId.script); s != data.scripts.end())
                    {
                        aId.script = s->second;
                        continue;
                    }
                if (!aId.region.empty())
                    if (auto const t = data.territories.find(aId.region); t != data.territories.end() && !t->second.empty())
                    {
                        auto replacement = t->second.front();
                        if (t->second.size() > 1u)
                        {
                            auto const likely = likely_region(aId);
                            if (std::find(t->second.begin(), t->second.end(), likely) != t->second.end())
                                replacement = likely;
                        }
                        aId.region = replacement;
                        continue;
                    }
                for (auto& v : aId.variants)
                    if (auto const a = data.variants.find(v); a != data.variants.end())
                    {
                        v = a->second;
                        changed = true;
                    }
                if (changed)
                {
                    std::sort(aId.variants.begin(), aId.variants.end());
                    aId.variants.erase(std::unique(aId.variants.begin(), aId.variants.end()), aId.variants.end());
                    continue;
                }
                break;
            }
        }
    }

    std::string canonicalize_u_value(std::string_view aKey, std::string_view aValue)
    {
        // (CanonicalizeUValue: the canonical form of a -u- keyword's value: CLDR's aliases ("true" is "")
        auto value = lowercase(aValue);
        if (aKey == "rg" || aKey == "sd")
        {
            if (auto const s = aliases().subdivisions.find(value); s != aliases().subdivisions.end())
                value = s->second;
        }
        else if (!value.empty())
        {
            auto const key = std::string{ aKey };
            if (auto const* t = uloc_toUnicodeLocaleType(key.c_str(), value.c_str()); t != nullptr)
                value = lowercase(t);
        }
        if (value == "true")
            value.clear();
        return value;
    }

    void canonicalize(locale_id& aLocale)
    {
        // CanonicalizeUnicodeLocaleId: canonical syntax (case (done when parsed), sorted variants, -u- keywords and -t- fields) and
        // canonical form (aliases)
        replace_aliases(aLocale);
        if (aLocale.unicodeExtension)
        {
            // (UnicodeExtensionComponents: the first of duplicate attributes and keys)
            std::vector<std::string> attributes;
            for (auto const& a : aLocale.attributes)
                if (std::find(attributes.begin(), attributes.end(), a) == attributes.end())
                    attributes.push_back(a);
            std::sort(attributes.begin(), attributes.end());
            aLocale.attributes = std::move(attributes);
            std::vector<std::pair<std::string, std::string>> keywords;
            for (auto const& [key, val] : aLocale.keywords)
                if (std::find_if(keywords.begin(), keywords.end(), [&](auto const& k) { return k.first == key; }) == keywords.end())
                    keywords.emplace_back(key, canonicalize_u_value(key, val));
            std::stable_sort(keywords.begin(), keywords.end(), [](auto const& a, auto const& b) { return a.first < b.first; });
            aLocale.keywords = std::move(keywords);
        }
        if (aLocale.transformedExtension)
        {
            if (aLocale.tlang)
                replace_aliases(*aLocale.tlang, true);
            for (auto& [key, val] : aLocale.tfields)
                if (auto const* t = uloc_toUnicodeLocaleType(key.c_str(), val.c_str()); t != nullptr)
                    val = lowercase(t);
            std::stable_sort(aLocale.tfields.begin(), aLocale.tfields.end(), [](auto const& a, auto const& b) { return a.first < b.first; });
        }
        std::stable_sort(aLocale.otherExtensions.begin(), aLocale.otherExtensions.end(), [](auto const& a, auto const& b) { return a.first < b.first; });
    }

    std::string canonicalize_language_tag(std::string_view aTag)
    {
        auto parsed = parse_language_tag(aTag);
        if (!parsed)
            return std::string{ aTag };
        canonicalize(*parsed);
        return parsed->to_string();
    }

    std::string icu_locale(std::string_view aTag)
    {
        auto const tag = std::string{ aTag };
        std::int32_t parsed = 0;
        auto result = icu_chars([&](char* aBuffer, std::int32_t aCapacity, UErrorCode* aStatus)
            { return uloc_forLanguageTag(tag.c_str(), aBuffer, aCapacity, &parsed, aStatus); });
        return result.empty() ? std::string{ "und" } : result;
    }

    std::string language_tag(char const* aIcuLocale)
    {
        auto const tag = icu_chars([&](char* aBuffer, std::int32_t aCapacity, UErrorCode* aStatus)
            { return uloc_toLanguageTag(aIcuLocale, aBuffer, aCapacity, false, aStatus); });
        return canonicalize_language_tag(tag);
    }

    // --- units and currencies (sanctioned units: ECMA-402 Table 2)

    bool is_sanctioned_single_unit(std::string_view aUnit)
    {
        static constexpr std::string_view sUnits[] =
        {
            "acre", "bit", "byte", "celsius", "centimeter", "day", "degree", "fahrenheit", "fluid-ounce", "foot", "gallon", "gigabit", "gigabyte",
            "gram", "hectare", "hour", "inch", "kilobit", "kilobyte", "kilogram", "kilometer", "liter", "megabit", "megabyte", "meter", "microsecond",
            "mile", "mile-scandinavian", "milliliter", "millimeter", "millisecond", "minute", "month", "nanosecond", "ounce", "percent", "petabyte",
            "pound", "second", "stone", "terabit", "terabyte", "week", "yard", "year"
        };
        return std::find(std::begin(sUnits), std::end(sUnits), aUnit) != std::end(sUnits);
    }

    bool is_well_formed_unit_identifier(std::u16string_view aUnit)
    {
        auto const unit = to_ascii(aUnit);
        if (is_sanctioned_single_unit(unit))
            return true;
        auto const per = unit.find("-per-");
        if (per == std::string::npos)
            return false;
        auto const numerator = unit.substr(0u, per);
        auto const denominator = unit.substr(per + 5u);
        return is_sanctioned_single_unit(numerator) && is_sanctioned_single_unit(denominator);
    }

    bool is_well_formed_currency_code(std::u16string_view aCurrency)
    {
        return aCurrency.size() == 3u && std::all_of(aCurrency.begin(), aCurrency.end(),
            [](char16_t c) { return (c >= u'a' && c <= u'z') || (c >= u'A' && c <= u'Z'); });
    }

    // --- Intl.Locale

    namespace
    {
        struct locale_data_record : intl_data
        {
            locale_id id;       ///< (canonical)
            std::optional<std::string> calendar, caseFirst, collation, hourCycle, numberingSystem, numeric;
        };

        bool is_type_sequence(std::string_view aValue)
        {
            // (a -u- value: (3*8alphanum) *("-" (3*8alphanum)))
            if (aValue.empty())
                return false;
            for (auto const& s : split(aValue, '-'))
                if (!is_type_subtag(s))
                    return false;
            return true;
        }

        std::string tag_argument(value aTag)
        {
            if (!is_string(aTag) && !is_object(aTag))
                throw_type_error("First argument to Intl.Locale constructor can't be empty or missing");
            if (auto* l = as_intl(aTag, object_class::IntlLocale))
                return l->locale;
            return utf8(to_string(aTag));
        }

        // (a string option (GetOption, then ToString))
        std::optional<std::string> string_option(value aOptions, char const* aName)
        {
            auto const v = get(aOptions, ascii(aName));
            if (is_undefined(v))
                return {};
            return utf8(to_string(v));
        }

        value locale_constructor(value, arguments aArguments, value aNewTarget, function_object&)
        {
            if (is_undefined(aNewTarget))
                throw_type_error("Constructor Intl.Locale requires 'new'");
            auto const result = make_intl_object(aNewTarget, &realm::intlLocalePrototype, object_class::IntlLocale);
            auto const tagText = tag_argument(argument(aArguments, 0u));
            auto const options = coerce_options_to_object(argument(aArguments, 1u));
            auto parsed = parse_language_tag(tagText);
            if (!parsed)
                throw_range_error("Incorrect locale information provided");
            canonicalize(*parsed);
            // UpdateLanguageId: language, script, region and variants of the options
            auto id = *parsed;
            if (auto const language = string_option(options, "language"))
            {
                if (!is_language_subtag(*language))
                    throw_range_error("Incorrect locale information provided");
                id.language = lowercase(*language);
            }
            if (auto const script = string_option(options, "script"))
            {
                if (!is_script_subtag(*script))
                    throw_range_error("Incorrect locale information provided");
                id.script = titlecase(*script);
            }
            if (auto const region = string_option(options, "region"))
            {
                if (!is_region_subtag(*region))
                    throw_range_error("Incorrect locale information provided");
                id.region = uppercase(*region);
            }
            if (auto const variants = string_option(options, "variants"))
            {
                std::vector<std::string> list;
                for (auto v : split(*variants, '-'))
                {
                    if (!is_variant_subtag(v))
                        throw_range_error("Incorrect locale information provided");
                    auto lv = lowercase(v);
                    if (std::find(list.begin(), list.end(), lv) != list.end())
                        throw_range_error("Incorrect locale information provided");
                    list.push_back(std::move(lv));
                }
                id.variants = std::move(list);
            }
            // (the keywords of the options)
            std::vector<std::pair<std::string, std::optional<std::string>>> overrides;
            auto const keyword_option = [&](char const* aName, char const* aKey, bool aTypeSequence, std::initializer_list<char const*> aValues = {})
            {
                auto v = string_option(options, aName);
                if (v)
                {
                    if (aTypeSequence && !is_type_sequence(*v))
                        throw_range_error(std::string{ "Incorrect locale information provided: " } + aName);
                    if (aValues.size() != 0u && std::find_if(aValues.begin(), aValues.end(), [&](char const* a) { return *v == a; }) == aValues.end())
                        throw_range_error(std::string{ "Value " } + *v + " out of range for Intl.Locale options property " + aName);
                }
                overrides.emplace_back(aKey, v);
            };
            keyword_option("calendar", "ca", true);
            keyword_option("collation", "co", true);
            keyword_option("hourCycle", "hc", false, { "h11", "h12", "h23", "h24" });
            keyword_option("caseFirst", "kf", false, { "upper", "lower", "false" });
            {
                auto const v = get(options, ascii("numeric"));
                if (is_undefined(v))
                    overrides.emplace_back("kn", std::nullopt);
                else
                    overrides.emplace_back("kn", to_boolean(v) ? std::string{ "true" } : std::string{ "false" });
            }
            keyword_option("numberingSystem", "nu", true);
            // MakeLocaleRecord: the options' keywords override the tag's
            auto data = std::make_unique<locale_data_record>();
            for (auto const& [key, override] : overrides)
            {
                if (!override)
                    continue;
                auto const v = canonicalize_u_value(key, *override);
                auto existing = std::find_if(id.keywords.begin(), id.keywords.end(), [&](auto const& k) { return k.first == key; });
                if (existing != id.keywords.end())
                    existing->second = v;
                else
                    id.keywords.emplace_back(key, v);
                id.unicodeExtension = true;
            }
            canonicalize(id);
            auto const relevant = [&](char const* aKey) -> std::optional<std::string>
            {
                auto const v = id.keyword(aKey);
                if (!v)
                    return {};
                return *v;
            };
            data->calendar = relevant("ca");
            data->collation = relevant("co");
            data->hourCycle = relevant("hc");
            data->caseFirst = relevant("kf");
            data->numeric = relevant("kn");
            data->numberingSystem = relevant("nu");
            auto& l = static_cast<intl_object&>(obj(result));
            l.locale = id.to_string();
            data->id = std::move(id);
            l.data = std::move(data);
            return result;
        }

        locale_data_record& this_locale(value aThis, char const* aMethod)
        {
            return data_of<locale_data_record>(this_intl(aThis, object_class::IntlLocale, aMethod));
        }

        value optional_string(std::optional<std::string> const& aValue)
        {
            return aValue ? u16_value(from_ascii(*aValue)) : Undefined;
        }

        value locale_to_string(value aThis, arguments, value, function_object&)
        {
            return u16_value(from_ascii(this_intl(aThis, object_class::IntlLocale, "Intl.Locale.prototype.toString").locale));
        }

        value locale_base_name(value aThis, arguments, value, function_object&)
        {
            return u16_value(from_ascii(this_locale(aThis, "Intl.Locale.prototype.baseName").id.base_name()));
        }

        value locale_keyword_getter(value aThis, arguments, value, function_object& aCallee)
        {
            auto const& d = this_locale(aThis, "Intl.Locale.prototype getter");
            switch (aCallee.nativeId)
            {
            case 0: return optional_string(d.calendar);
            case 1: return optional_string(d.caseFirst);
            case 2: return optional_string(d.collation);
            case 3: return optional_string(d.hourCycle);
            case 4: return optional_string(d.numberingSystem);
            default: return boolean(d.numeric && (d.numeric->empty() || *d.numeric == "true"));
            }
        }

        value locale_subtag_getter(value aThis, arguments, value, function_object& aCallee)
        {
            auto const& d = this_locale(aThis, "Intl.Locale.prototype getter");
            switch (aCallee.nativeId)
            {
            case 0: return u16_value(from_ascii(d.id.language.empty() ? std::string{ "und" } : d.id.language));
            case 1: return d.id.script.empty() ? Undefined : u16_value(from_ascii(d.id.script));
            case 2: return d.id.region.empty() ? Undefined : u16_value(from_ascii(d.id.region));
            default: return d.id.variants.empty() ? Undefined : u16_value(from_ascii(join(d.id.variants, '-')));
            }
        }

        // (a Locale with the language_id of aLocale replaced by aId)
        value locale_with_language_id(locale_data_record const& aLocale, language_id const& aId)
        {
            auto id = aLocale.id;
            static_cast<language_id&>(id) = aId;
            auto const& r = the_realm();
            value const argument = u16_value(from_ascii(id.to_string()));
            return construct(r.intlLocale, std::span<value const>{ &argument, 1u });
        }

        language_id language_id_of(std::string const& aIcuLocale)
        {
            // (an ICU locale ID's language, script and region (its variants are the tag's own))
            language_id result;
            result.language = lowercase(icu_chars([&](char* b, std::int32_t c, UErrorCode* s) { return uloc_getLanguage(aIcuLocale.c_str(), b, c, s); }));
            result.script = titlecase(icu_chars([&](char* b, std::int32_t c, UErrorCode* s) { return uloc_getScript(aIcuLocale.c_str(), b, c, s); }));
            result.region = uppercase(icu_chars([&](char* b, std::int32_t c, UErrorCode* s) { return uloc_getCountry(aIcuLocale.c_str(), b, c, s); }));
            if (result.language.empty())
                result.language = "und";
            return result;
        }

        value locale_maximize(value aThis, arguments, value, function_object&)
        {
            auto const& d = this_locale(aThis, "Intl.Locale.prototype.maximize");
            language_id base = d.id;
            base.variants.clear();
            auto const icu = icu_locale(language_id_string(base));
            auto const maximized = icu_chars([&](char* b, std::int32_t c, UErrorCode* s) { return uloc_addLikelySubtags(icu.c_str(), b, c, s); });
            auto id = maximized.empty() ? base : language_id_of(maximized);
            id.variants = d.id.variants;
            return locale_with_language_id(d, id);
        }

        value locale_minimize(value aThis, arguments, value, function_object&)
        {
            auto const& d = this_locale(aThis, "Intl.Locale.prototype.minimize");
            language_id base = d.id;
            base.variants.clear();
            auto const icu = icu_locale(language_id_string(base));
            auto const minimized = icu_chars([&](char* b, std::int32_t c, UErrorCode* s) { return uloc_minimizeSubtags(icu.c_str(), b, c, s); });
            auto id = minimized.empty() ? base : language_id_of(minimized);
            id.variants = d.id.variants;
            return locale_with_language_id(d, id);
        }
    }

    void init_intl_locale(realm& aRealm, value aIntl)
    {
        auto const c = make_intl_constructor(aRealm, "Locale", 1u, locale_constructor, &realm::intlLocalePrototype, "Intl.Locale");
        aRealm.intlLocale = c;
        // (Intl.Locale has no supportedLocalesOf)
        obj(c).delete_property(ascii("supportedLocalesOf"));
        auto const p = aRealm.intlLocalePrototype;
        method(p, "toString", 0u, locale_to_string);
        method(p, "maximize", 0u, locale_maximize);
        method(p, "minimize", 0u, locale_minimize);
        getter(p, ascii("baseName"), locale_base_name);
        static char const* const sKeywords[] = { "calendar", "caseFirst", "collation", "hourCycle", "numberingSystem", "numeric" };
        for (std::uint32_t i = 0u; i < 6u; ++i)
            as_function(getter(p, ascii(sKeywords[i]), locale_keyword_getter))->nativeId = i;
        static char const* const sSubtags[] = { "language", "script", "region", "variants" };
        for (std::uint32_t i = 0u; i < 4u; ++i)
            as_function(getter(p, ascii(sSubtags[i]), locale_subtag_getter))->nativeId = i;
        (void)aIntl;
    }
}

#endif
