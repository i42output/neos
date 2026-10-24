/*
  intl_list_format.cpp

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

// Intl.ListFormat (ECMA-402 §13) with ICU's list formatter (ulistfmt).

#include "intl.hpp"

#if defined(NEOS_JS_ICU)

#include "intl_icu.hpp"

namespace neos::javascript_runtime::intl
{
    namespace
    {
        struct list_format_data : intl_data
        {
            std::string type;
            std::string style;
            list_formatter_ptr format;
        };

        struct no_keys_locale_data : locale_data
        {
            std::vector<std::string> values(std::string const&, std::string_view) const override { return {}; }
        };

        list_formatter_ptr open_list_formatter(std::string const& aDataLocale, std::string const& aType, std::string const& aStyle)
        {
            auto const icu = icu_locale(aDataLocale);
            UErrorCode status = U_ZERO_ERROR;
            list_formatter_ptr f{ ulistfmt_openForType(icu.c_str(), aType == "conjunction" ? ULISTFMT_TYPE_AND : aType == "disjunction" ?
                ULISTFMT_TYPE_OR : ULISTFMT_TYPE_UNITS, aStyle == "long" ? ULISTFMT_WIDTH_WIDE : aStyle == "short" ? ULISTFMT_WIDTH_SHORT :
                ULISTFMT_WIDTH_NARROW, &status) };
            if (U_FAILURE(status))
                throw_range_error("Intl.ListFormat: the format cannot be made");
            return f;
        }

        std::optional<std::u16string> list_part_type(field_span const& aSpan, std::u16string_view, void*)
        {
            if (aSpan.category == UFIELD_CATEGORY_LIST && aSpan.field == ULISTFMT_ELEMENT_FIELD)
                return u"element";
            return std::nullopt;
        }

        // (CreatePartsFromList)
        std::vector<part> list_parts(UListFormatter* aFormat, std::vector<std::u16string> const& aList)
        {
            std::vector<UChar const*> strings;
            std::vector<std::int32_t> lengths;
            for (auto const& s : aList)
            {
                strings.push_back(uchars(s));
                lengths.push_back(ulength(s));
            }
            UErrorCode status = U_ZERO_ERROR;
            formatted_list_ptr result{ ulistfmt_openResult(&status) };
            ulistfmt_formatStringsToResult(aFormat, strings.data(), lengths.data(), static_cast<std::int32_t>(aList.size()), result.get(), &status);
            if (U_FAILURE(status))
                throw_range_error("Intl.ListFormat: the list cannot be formatted");
            auto const* v = ulistfmt_resultAsValue(result.get(), &status);
            auto const text = formatted_value_string(v);
            auto spans = formatted_value_spans(v);
            // (an element's own fields are not its parts: only the elements' spans count)
            std::erase_if(spans, [](field_span const& s) { return s.category != UFIELD_CATEGORY_LIST || s.field != ULISTFMT_ELEMENT_FIELD; });
            // (adjacent elements (e.g. an empty separator) are a part each: make_parts merges equal adjacent types, so they are made here)
            std::vector<part> parts;
            std::int32_t offset = 0;
            for (auto const& s : spans)
            {
                if (s.begin > offset)
                    parts.push_back({ u"literal", text.substr(static_cast<std::size_t>(offset), static_cast<std::size_t>(s.begin - offset)) });
                parts.push_back({ u"element", text.substr(static_cast<std::size_t>(s.begin), static_cast<std::size_t>(s.end - s.begin)) });
                offset = s.end;
            }
            if (static_cast<std::size_t>(offset) < text.size())
                parts.push_back({ u"literal", text.substr(static_cast<std::size_t>(offset)) });
            return parts;
        }

        value list_format_constructor(value, arguments aArguments, value aNewTarget, function_object&)
        {
            if (is_undefined(aNewTarget))
                throw_type_error("Constructor Intl.ListFormat requires 'new'");
            auto const result = make_intl_object(aNewTarget, &realm::intlListFormatPrototype, object_class::IntlListFormat);
            auto& lf = static_cast<intl_object&>(obj(result));
            auto data = std::make_unique<list_format_data>();
            auto& d = *data;
            auto const requested = canonicalize_locale_list(argument(aArguments, 0u));
            auto const options = get_options_object(argument(aArguments, 1u));
            auto const matcher = *get_string_option(options, "localeMatcher", { "lookup", "best fit" }, "best fit");
            auto const r = resolve_locale(service::ListFormat, requested, matcher, {}, no_keys_locale_data{});
            lf.locale = r.locale;
            d.type = to_ascii(*get_string_option(options, "type", { "conjunction", "disjunction", "unit" }, "conjunction"));
            d.style = to_ascii(*get_string_option(options, "style", { "long", "short", "narrow" }, "long"));
            d.format = open_list_formatter(r.dataLocale, d.type, d.style);
            lf.data = std::move(data);
            return result;
        }

        // (StringListFromIterable)
        std::vector<std::u16string> string_list_from_iterable(value aIterable)
        {
            std::vector<std::u16string> list;
            if (is_undefined(aIterable))
                return list;
            auto record = get_iterator(aIterable);
            while (auto const next = iterator_step_value(record))
            {
                if (!is_string(*next))
                {
                    iterator_close_on_throw(record);
                    throw_type_error("Iterable yielded a value which is not a string");
                }
                list.push_back(str(*next));
            }
            return list;
        }

        value list_format_format(value aThis, arguments aArguments, value, function_object&)
        {
            auto& lf = this_intl(aThis, object_class::IntlListFormat, "Intl.ListFormat.prototype.format");
            auto const list = string_list_from_iterable(argument(aArguments, 0u));
            std::u16string text;
            for (auto const& p : list_parts(data_of<list_format_data>(lf).format.get(), list))
                text += p.text;
            return u16_value(text);
        }

        value list_format_format_to_parts(value aThis, arguments aArguments, value, function_object&)
        {
            auto& lf = this_intl(aThis, object_class::IntlListFormat, "Intl.ListFormat.prototype.formatToParts");
            auto const list = string_list_from_iterable(argument(aArguments, 0u));
            return parts_array(list_parts(data_of<list_format_data>(lf).format.get(), list));
        }

        value list_format_resolved_options(value aThis, arguments, value, function_object&)
        {
            auto& lf = this_intl(aThis, object_class::IntlListFormat, "Intl.ListFormat.prototype.resolvedOptions");
            auto const& d = data_of<list_format_data>(lf);
            auto const o = make_object();
            create_data_property_or_throw(o, ascii("locale"), u16_value(from_ascii(lf.locale)));
            create_data_property_or_throw(o, ascii("type"), u16_value(from_ascii(d.type)));
            create_data_property_or_throw(o, ascii("style"), u16_value(from_ascii(d.style)));
            return o;
        }
    }

    std::vector<part> format_list_to_parts(std::string const& aLocale, std::string const& aType, std::string const& aStyle,
        std::vector<std::u16string> const& aList)
    {
        auto const f = open_list_formatter(aLocale, aType, aStyle);
        return list_parts(f.get(), aList);
    }

    void init_intl_list_format(realm& aRealm, value)
    {
        make_intl_constructor(aRealm, "ListFormat", 0u, list_format_constructor, &realm::intlListFormatPrototype, "Intl.ListFormat");
        auto const p = aRealm.intlListFormatPrototype;
        method(p, "format", 1u, list_format_format);
        method(p, "formatToParts", 1u, list_format_format_to_parts);
        method(p, "resolvedOptions", 0u, list_format_resolved_options);
    }
}

#endif
