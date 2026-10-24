/*
  intl_segmenter.cpp

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

// Intl.Segmenter (ECMA-402 §18) with ICU's break iterators (ubrk): the Segmenter, its Segments objects and their iterators.

#include "intl.hpp"

#if defined(NEOS_JS_ICU)

#include "gc.hpp"
#include "intl_icu.hpp"

namespace neos::javascript_runtime::intl
{
    namespace
    {
        struct segmenter_data : intl_data
        {
            std::string granularity;
            break_iterator_ptr breaker;     ///< (cloned for each string segmented)
        };

        // (a Segments object's and a segment iterator's: the string and a break iterator on it)
        struct segments_data : intl_data
        {
            std::string granularity;
            std::u16string text;            ///< (the break iterator's text: not changed)
            value input = Undefined;        ///< (the string)
            break_iterator_ptr breaker;
            std::int32_t position = 0;      ///< (an iterator's: [[IteratedStringNextSegmentCodeUnitIndex]])
        public:
            void trace(tracer& aTracer) override { aTracer.mark(input); }
        };

        struct no_keys_locale_data : locale_data
        {
            std::vector<std::string> values(std::string const&, std::string_view) const override { return {}; }
        };

        value segmenter_constructor(value, arguments aArguments, value aNewTarget, function_object&)
        {
            if (is_undefined(aNewTarget))
                throw_type_error("Constructor Intl.Segmenter requires 'new'");
            auto const result = make_intl_object(aNewTarget, &realm::intlSegmenterPrototype, object_class::IntlSegmenter);
            auto& s = static_cast<intl_object&>(obj(result));
            auto data = std::make_unique<segmenter_data>();
            auto& d = *data;
            auto const requested = canonicalize_locale_list(argument(aArguments, 0u));
            auto const options = get_options_object(argument(aArguments, 1u));
            auto const matcher = *get_string_option(options, "localeMatcher", { "lookup", "best fit" }, "best fit");
            auto const r = resolve_locale(service::Segmenter, requested, matcher, {}, no_keys_locale_data{});
            s.locale = r.locale;
            d.granularity = to_ascii(*get_string_option(options, "granularity", { "grapheme", "word", "sentence" }, "grapheme"));
            auto const icu = icu_locale(r.dataLocale);
            UErrorCode status = U_ZERO_ERROR;
            d.breaker.reset(ubrk_open(d.granularity == "grapheme" ? UBRK_CHARACTER : d.granularity == "word" ? UBRK_WORD : UBRK_SENTENCE, icu.c_str(),
                nullptr, 0, &status));
            if (U_FAILURE(status))
                throw_range_error("Intl.Segmenter: the break iterator cannot be made");
            s.data = std::move(data);
            return result;
        }

        // (a break iterator on a string (its data's text))
        std::unique_ptr<segments_data> segments_of(std::string const& aGranularity, UBreakIterator const* aBreaker, value aString)
        {
            auto data = std::make_unique<segments_data>();
            data->granularity = aGranularity;
            data->input = aString;
            data->text = str(aString);
            UErrorCode status = U_ZERO_ERROR;
            data->breaker.reset(ubrk_clone(aBreaker, &status));
            if (U_FAILURE(status))
                throw_range_error("Intl.Segmenter: the break iterator cannot be made");
            ubrk_setText(data->breaker.get(), uchars(data->text), ulength(data->text), &status);
            return data;
        }

        value segmenter_segment(value aThis, arguments aArguments, value, function_object&)
        {
            auto& s = this_intl(aThis, object_class::IntlSegmenter, "Intl.Segmenter.prototype.segment");
            auto const string = to_string(argument(aArguments, 0u));
            auto const& d = data_of<segmenter_data>(s);
            auto& segments = the_heap().make<intl_object>(the_realm().intlSegmentsPrototype, object_class::IntlSegments);
            auto const result = value_of(segments);
            segments.locale = s.locale;
            segments.data = segments_of(d.granularity, d.breaker.get(), string);
            return result;
        }

        // (CreateSegmentDataObject: {segment, index, input[, isWordLike]} of [aStart, aEnd) (the break iterator's last boundary aEnd))
        value segment_data_object(segments_data& d, std::int32_t aStart, std::int32_t aEnd)
        {
            auto const o = make_object();
            create_data_property_or_throw(o, ascii("segment"), u16_value(std::u16string_view{ d.text }.substr(static_cast<std::size_t>(aStart),
                static_cast<std::size_t>(aEnd - aStart))));
            create_data_property_or_throw(o, ascii("index"), number(aStart));
            create_data_property_or_throw(o, ascii("input"), d.input);
            if (d.granularity == "word")
            {
                auto const status = ubrk_getRuleStatus(d.breaker.get());
                create_data_property_or_throw(o, ascii("isWordLike"), boolean(!(status >= UBRK_WORD_NONE && status < UBRK_WORD_NONE_LIMIT)));
            }
            return o;
        }

        value segments_containing(value aThis, arguments aArguments, value, function_object&)
        {
            auto& segments = this_intl(aThis, object_class::IntlSegments, "%Segments.prototype%.containing");
            auto& d = data_of<segments_data>(segments);
            auto const n = to_integer_or_infinity(argument(aArguments, 0u));
            if (n < 0 || n >= static_cast<double>(d.text.size()))
                return Undefined;
            auto index = static_cast<std::int32_t>(n);
            // (within a surrogate pair: its code point's start)
            if (index > 0 && (d.text[static_cast<std::size_t>(index)] & 0xFC00u) == 0xDC00u && (d.text[static_cast<std::size_t>(index) - 1u] & 0xFC00u) == 0xD800u)
                --index;
            auto* b = d.breaker.get();
            auto const start = ubrk_isBoundary(b, index) ? index : ubrk_preceding(b, index);
            auto const end = ubrk_following(b, start);
            return segment_data_object(d, start, end);
        }

        value segments_iterator(value aThis, arguments, value, function_object&)
        {
            auto& segments = this_intl(aThis, object_class::IntlSegments, "%Segments.prototype%[@@iterator]");
            auto& d = data_of<segments_data>(segments);
            auto& iterator = the_heap().make<intl_object>(the_realm().intlSegmentIteratorPrototype, object_class::IntlSegmentIterator);
            auto const result = value_of(iterator);
            iterator.locale = segments.locale;
            iterator.data = segments_of(d.granularity, d.breaker.get(), d.input);
            return result;
        }

        value segment_iterator_next(value aThis, arguments, value, function_object&)
        {
            auto& iterator = this_intl(aThis, object_class::IntlSegmentIterator, "%SegmentIterator.prototype%.next");
            auto& d = data_of<segments_data>(iterator);
            if (static_cast<std::size_t>(d.position) >= d.text.size())
                return create_iter_result_object(Undefined, true);
            auto const start = d.position;
            auto const end = ubrk_following(d.breaker.get(), start);
            d.position = end;
            return create_iter_result_object(segment_data_object(d, start, end), false);
        }
    }

    void init_intl_segmenter(realm& aRealm, value)
    {
        make_intl_constructor(aRealm, "Segmenter", 0u, segmenter_constructor, &realm::intlSegmenterPrototype, "Intl.Segmenter");
        auto const p = aRealm.intlSegmenterPrototype;
        method(p, "segment", 1u, segmenter_segment);
        method(p, "resolvedOptions", 0u, [](value aThis, arguments, value, function_object&) -> value
        {
            auto& s = this_intl(aThis, object_class::IntlSegmenter, "Intl.Segmenter.prototype.resolvedOptions");
            auto const o = make_object();
            create_data_property_or_throw(o, ascii("locale"), u16_value(from_ascii(s.locale)));
            create_data_property_or_throw(o, ascii("granularity"), u16_value(from_ascii(data_of<segmenter_data>(s).granularity)));
            return o;
        });
        // (%SegmentsPrototype% and %SegmentIteratorPrototype%)
        aRealm.intlSegmentsPrototype = make_object();
        method(aRealm.intlSegmentsPrototype, "containing", 1u, segments_containing);
        obj(aRealm.intlSegmentsPrototype).put(aRealm.symIterator, make_native(aRealm.symIterator, 0u, segments_iterator));
        aRealm.intlSegmentIteratorPrototype = make_object(aRealm.iteratorPrototype);
        method(aRealm.intlSegmentIteratorPrototype, "next", 0u, segment_iterator_next);
        obj(aRealm.intlSegmentIteratorPrototype).put(aRealm.symToStringTag, ascii("Segmenter String Iterator"), AttributeConfigurable);
    }
}

#endif
