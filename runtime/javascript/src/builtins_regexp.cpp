/*
  builtins_regexp.cpp

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

// RegExp: the RegExp constructor, RegExp.prototype, the RegExp String Iterator and RegExpCreate (make_regexp)

#include <algorithm>
#include <cstring>
#include <unordered_map>
#include "js.hpp"
#include "api.hpp"
#include "regexp_engine.hpp"
#include "unicode.hpp"

namespace neos::javascript_runtime
{
    namespace
    {
        // (a RegExp instance: [[OriginalSource]], [[OriginalFlags]] and [[RegExpMatcher]])
        class regexp_object : public object
        {
        public:
            explicit regexp_object(value aPrototype) : object{ aPrototype, object_class::RegExp } {}
        public:
            void trace(tracer& aTracer) override
            {
                object::trace(aTracer);
                aTracer.mark(source);
                aTracer.mark(flags);
            }
        public:
            value source = Undefined;
            value flags = Undefined;
            std::uint32_t flagBits = 0u;
            std::shared_ptr<regexp::program const> matcher;
            bool legacyFeaturesEnabled = false;     ///< ([[LegacyFeaturesEnabled]]: created by %RegExp% itself (not a subclass))
        };

        regexp_object* as_regexp(value aValue)
        {
            if (!is_object(aValue))
                return nullptr;
            auto& o = obj(aValue);
            return o.cls == object_class::RegExp ? static_cast<regexp_object*>(&o) : nullptr;
        }

        class regexp_string_iterator : public object
        {
        public:
            explicit regexp_string_iterator(value aPrototype) : object{ aPrototype, object_class::RegExpStringIterator } {}
        public:
            void trace(tracer& aTracer) override
            {
                object::trace(aTracer);
                aTracer.mark(iteratingRegExp);
                aTracer.mark(iteratedString);
            }
        public:
            value iteratingRegExp = Undefined;
            value iteratedString = Undefined;
            bool global = false;
            bool fullUnicode = false;
            bool done = false;
        };

        struct keys
        {
            value source, flags, exec, global, ignoreCase, multiline, dotAll, unicode, unicodeSets, sticky, hasIndices;
            value execFunction;     ///< (%RegExp.prototype.exec%)
        };

        // (the legacy static properties of %RegExp% (RegExp.$1, lastMatch, ...): the last match of a RegExp with
        // [[LegacyFeaturesEnabled]] (its substrings are made when they are read))
        struct legacy_statics
        {
            bool valid = true;              ///< (false: empty (a RegExp without [[LegacyFeaturesEnabled]] matched))
            value input = Undefined;        ///< ([[RegExpInput]])
            value subject = Undefined;      ///< (the string that was matched)
            std::int32_t captures[20] = {}; ///< (the match and $1-$9: start and end (-1: undefined))
            std::uint32_t count = 10u;      ///< (the captures (the match and $1-$9) that captures has: the others are undefined)
            std::int32_t lastParen[2] = { -1, -1 };
        };

        // (a regexp's lastIndex: its own data property's (as a regexp has it: at a position known for its shape), else as Get has it)
        struct last_index_position
        {
            shape const* layout = nullptr;
            std::uint32_t position = 0u;
            bool writable = false;
        };

        // (the isolate's: one isolate_local (each one's access costs))
        struct regexp_state
        {
            keys k;
            legacy_statics legacy;
            // (see has_builtin_exec: a regexp of this own shape (known not to have its own exec), whose prototype has this shape, has 
            // the built-in at this position of its prototype's (shapes do not change: their keys' positions and attributes are known))
            shape const* execOwn = nullptr;
            shape const* execPrototype = nullptr;
            std::uint32_t execPosition = 0u;
            last_index_position lastIndex;      ///< (see known_last_index)
        };
        isolate_local<regexp_state> sRegExpState;
        regexp_state& the_regexp_state() { return *sRegExpState; }
        keys& the_keys() { return sRegExpState->k; }
        legacy_statics& the_legacy() { return sRegExpState->legacy; }

        root_tracer const sRegExpRoots{ [](tracer& aTracer)
        {
            aTracer.conservative(&the_keys(), &the_keys() + 1);
            aTracer.mark(the_legacy().input);
            aTracer.mark(the_legacy().subject);
        } };

        void update_legacy_statics(legacy_statics& legacy, value aSubject, std::vector<std::int32_t> const& aCaptures, std::int32_t aStart, std::uint32_t aCount)
        {
            legacy.valid = true;
            legacy.input = aSubject;
            legacy.subject = aSubject;
            legacy.captures[0] = aStart;
            legacy.captures[1] = aCaptures[1];
            legacy.count = 10u;
            for (std::uint32_t i = 1u; i <= 9u; ++i)
            {
                legacy.captures[i * 2u] = i <= aCount ? aCaptures[i * 2u] : -1;
                legacy.captures[i * 2u + 1u] = i <= aCount ? aCaptures[i * 2u + 1u] : -1;
            }
            legacy.lastParen[0] = aCount > 0u ? aCaptures[aCount * 2u] : -1;
            legacy.lastParen[1] = aCount > 0u ? aCaptures[aCount * 2u + 1u] : -1;
        }
        void update_legacy_statics(value aSubject, std::vector<std::int32_t> const& aCaptures, std::int32_t aStart, std::uint32_t aCount)
        {
            update_legacy_statics(the_legacy(), aSubject, aCaptures, aStart, aCount);
        }

        last_index_position const* known_last_index(regexp_object& aR, regexp_state& aState)
        {
            auto& k = aState.lastIndex;
            auto const* layout = aR.properties.layout();
            if (layout == nullptr)
                return nullptr;
            if (layout != k.layout)
            {
                auto const p = aR.own(the_names().lastIndex);
                if (!p || p->accessor())
                    return nullptr;
                k = last_index_position{ layout, p.position(), p->writable() };
            }
            return &k;
        }
        last_index_position const* known_last_index(regexp_object& aR)
        {
            return known_last_index(aR, the_regexp_state());
        }
        value last_index(regexp_object& aR, value aRValue, regexp_state& aState)
        {
            if (auto const* k = known_last_index(aR, aState))
                return aR.properties.value_at(k->position);
            return get(aRValue, the_names().lastIndex);
        }
        value last_index(regexp_object& aR, value aRValue)
        {
            return last_index(aR, aRValue, the_regexp_state());
        }
        void set_last_index(regexp_object& aR, value aRValue, value aIndex, regexp_state& aState)
        {
            if (auto const* k = known_last_index(aR, aState); k != nullptr && k->writable)
            {
                aR.properties.set_value_at(k->position, aIndex);
                return;
            }
            set(aRValue, the_names().lastIndex, aIndex, true);
        }
        void set_last_index(regexp_object& aR, value aRValue, value aIndex)
        {
            set_last_index(aR, aRValue, aIndex, the_regexp_state());
        }

        // (compiled patterns, by flags and pattern)
        std::unordered_map<std::u16string, std::shared_ptr<regexp::program const>>& program_cache()
        {
            static isolate_local<std::unordered_map<std::u16string, std::shared_ptr<regexp::program const>>> sCache;
            return *sCache;
        }

        std::shared_ptr<regexp::program const> compile_pattern(value aPattern, value aFlags, std::uint32_t aFlagBits)
        {
            auto const& pattern = str(aPattern);
            std::u16string key;
            key.reserve(pattern.size() + 1u);
            key.push_back(static_cast<char16_t>(aFlagBits));
            key += pattern;
            auto& cache = program_cache();
            if (auto const existing = cache.find(key); existing != cache.end())
                return existing->second;
            std::shared_ptr<regexp::program const> result;
            try
            {
                result = regexp::compile(pattern, aFlagBits);
            }
            catch (regexp::syntax_error const& e)
            {
                throw_syntax_error("Invalid regular expression: /" + utf8(aPattern) + "/" + utf8(aFlags) + ": " + e.message);
            }
            if (cache.size() >= 1024u)
                cache.clear();
            cache.emplace(std::move(key), result);
            return result;
        }

        // (RegExpAlloc)
        value regexp_alloc(value aNewTarget)
        {
            auto const prototype = get_prototype_from_constructor(aNewTarget, &realm::regexpPrototype);
            auto& r = the_heap().make<regexp_object>(prototype);
            r.legacyFeaturesEnabled = aNewTarget == the_realm().regexp;
            r.properties.add(the_names().lastIndex, Undefined, Undefined, AttributeWritable);
            return value_of(r);
        }

        // (RegExpInitialize)
        value regexp_initialize(value aObject, value aPattern, value aFlags)
        {
            auto const p = aPattern == Undefined ? the_names().empty : to_string(aPattern);
            auto const f = aFlags == Undefined ? the_names().empty : to_string(aFlags);
            auto const bits = regexp::parse_flags(str(f));
            if (!bits)
                throw_syntax_error("Invalid regular expression flags '" + utf8(f) + "'");
            auto program = compile_pattern(p, f, *bits);
            auto& r = *as_regexp(aObject);
            r.source = p;
            r.flags = f;
            r.flagBits = *bits;
            r.matcher = std::move(program);
            set(aObject, the_names().lastIndex, number(0.0), true);
            return aObject;
        }

        regexp_object& this_regexp(value aThis, char const* aMethod)
        {
            auto* const r = as_regexp(aThis);
            if (r == nullptr || !r->matcher)
                throw_type_error(std::string{ "RegExp.prototype." } + aMethod + " requires that 'this' be a RegExp object");
            return *r;
        }

        void require_object(value aThis, char const* aMethod)
        {
            if (!is_object(aThis))
                throw_type_error(std::string{ "RegExp.prototype." } + aMethod + " called on a non-object");
        }

        inline bool is_lead(char16_t aChar) { return aChar >= 0xD800u && aChar <= 0xDBFFu; }
        inline bool is_trail(char16_t aChar) { return aChar >= 0xDC00u && aChar <= 0xDFFFu; }

        // (AdvanceStringIndex)
        double advance_string_index(std::u16string_view aText, double aIndex, bool aUnicode)
        {
            if (!aUnicode || aIndex + 1.0 >= static_cast<double>(aText.size()))
                return aIndex + 1.0;
            auto const i = static_cast<std::size_t>(aIndex);
            return is_lead(aText[i]) && is_trail(aText[i + 1u]) ? aIndex + 2.0 : aIndex + 1.0;
        }

        value substring(std::u16string_view aText, std::int32_t aStart, std::int32_t aEnd)
        {
            if (aEnd <= aStart)
                return the_names().empty;
            if (aStart == 0 && static_cast<std::size_t>(aEnd) == aText.size())
                return make_string(std::u16string_view{ aText });
            return make_string(std::u16string_view{ aText }.substr(static_cast<std::size_t>(aStart), static_cast<std::size_t>(aEnd - aStart)));
        }

        enum class exec_kind
        {
            Result,     ///< (the match's array)
            Test,       ///< (true or null)
        };

        // (RegExpBuiltinExec's matching (and its updates of lastIndex): aCaptures: the match's capture slots (slot 0: its index))
        bool builtin_match(regexp_object& aR, value aRValue, value aS, std::vector<std::int32_t>& aCaptures, regexp_state& aState)
        {
            auto const& text = str(aS);
            auto const length = static_cast<double>(text.size());
            auto const bits = aR.flagBits;
            bool const global = (bits & regexp::FlagGlobal) != 0u;
            bool const sticky = (bits & regexp::FlagSticky) != 0u;
            bool const fullUnicode = (bits & (regexp::FlagUnicode | regexp::FlagUnicodeSets)) != 0u;
            // (ToLength(lastIndex): unless it is a number (ToLength has no effect) that is not used (neither global nor sticky))
            auto const lastIndexValue = last_index(aR, aRValue, aState);
            double lastIndex = 0.0;
            if (global || sticky || !is_number(lastIndexValue))
                lastIndex = to_length(lastIndexValue);
            if (!global && !sticky)
                lastIndex = 0.0;
            auto const& program = *aR.matcher;     // (a reference: not a copy of the shared pointer (its count's atomic changes))
            if (lastIndex > length)
            {
                if (global || sticky)
                    set_last_index(aR, aRValue, number(0.0), aState);
                return false;
            }
            auto const start = static_cast<std::size_t>(lastIndex);
            // (with u or v, a lastIndex inside a surrogate pair: the character is the pair)
            auto attempt = start;
            if (fullUnicode && start > 0u && start < text.size() && is_trail(text[start]) && is_lead(text[start - 1u]))
                attempt = start - 1u;
            auto& captures = aCaptures;
            auto const result = regexp::exec(program, text, attempt, sticky, captures);
            if (result == regexp::match_result::Overflow)
                throw_range_error("Maximum regular expression backtracking stack size exceeded");
            if (result == regexp::match_result::NoMatch)
            {
                if (global || sticky)
                    set_last_index(aR, aRValue, number(0.0), aState);
                return false;
            }
            auto index = captures[0];
            if (static_cast<std::size_t>(index) == attempt && attempt != start)
                index = static_cast<std::int32_t>(start);
            auto const e = captures[1];
            if (global || sticky)
                set_last_index(aR, aRValue, number(e), aState);
            if (aR.legacyFeaturesEnabled)
                update_legacy_statics(aState.legacy, aS, captures, index, program.captureCount);
            else
                aState.legacy.valid = false;
            captures[0] = index;
            return true;
        }
        bool builtin_match(regexp_object& aR, value aRValue, value aS, std::vector<std::int32_t>& aCaptures)
        {
            return builtin_match(aR, aRValue, aS, aCaptures, the_regexp_state());
        }

        // (the groups object of a match (aGroupNames: by group (1...), the key of the name whose value it gives, or undefined))
        value make_groups(regexp::program const& aProgram, std::span<value const> aElements, value_list* aGroupNames = nullptr)
        {
            if (!aProgram.hasNamedGroups)
                return Undefined;
            auto const n = aProgram.captureCount;
            auto const groups = make_object(Null);
            thread_local std::vector<std::u16string const*> tMatchedGroupNames;
            tMatchedGroupNames.clear();
            if (aGroupNames != nullptr)
                aGroupNames->assign(n, Undefined);
            for (std::uint32_t i = 1u; i <= n; ++i)
            {
                auto const& name = aProgram.groupNames[i];
                if (name.empty())
                    continue;
                if (std::find_if(tMatchedGroupNames.begin(), tMatchedGroupNames.end(), [&](auto const* m) { return *m == name; }) != tMatchedGroupNames.end())
                    continue;
                if (aElements[i] != Undefined)
                    tMatchedGroupNames.push_back(&name);
                auto const key = intern(std::u16string_view{ name });
                create_data_property(groups, key, aElements[i]);
                if (aGroupNames != nullptr)
                    (*aGroupNames)[i - 1u] = key;
            }
            return groups;
        }

        // (RegExpBuiltinExec)
        value builtin_exec(regexp_object& aR, value aRValue, value aS, exec_kind aKind = exec_kind::Result)
        {
            auto& captures = the_heap().regexpCaptures;
            if (!builtin_match(aR, aRValue, aS, captures))
                return Null;
            if (aKind == exec_kind::Test)
                return True;
            auto const& text = str(aS);
            auto const& program = *aR.matcher;
            bool const hasIndices = (aR.flagBits & regexp::FlagHasIndices) != 0u;
            auto const index = captures[0];
            auto const e = captures[1];
            auto const n = program.captureCount;
            value_list elements(n + 1u, Undefined);
            elements[0] = substring(text, index, e);
            for (std::uint32_t i = 1u; i <= n; ++i)
                if (captures[i * 2u] != -1)
                    elements[i] = substring(text, captures[i * 2u], captures[i * 2u + 1u]);
            auto const a = make_array(elements);
            auto& ao = obj(a);
            ao.put(the_names().index, number(index), AttributesDefault);
            ao.put(the_names().input, aS, AttributesDefault);
            value_list groupNames;
            auto const groups = make_groups(program, elements, &groupNames);
            ao.put(the_names().groups, groups, AttributesDefault);
            if (hasIndices)
            {
                // (MakeMatchIndicesIndexPairArray)
                value_list pairs(n + 1u, Undefined);
                auto const pair = [&](std::int32_t aStart, std::int32_t aEnd)
                {
                    return make_array(value_list{ number(aStart), number(aEnd) });
                };
                pairs[0] = pair(index, e);
                for (std::uint32_t i = 1u; i <= n; ++i)
                    if (captures[i * 2u] != -1)
                        pairs[i] = pair(captures[i * 2u], captures[i * 2u + 1u]);
                auto const indices = make_array(pairs);
                value indexGroups = Undefined;
                if (program.hasNamedGroups)
                {
                    indexGroups = make_object(Null);
                    for (std::uint32_t i = 1u; i <= n; ++i)
                        if (groupNames[i - 1u] != Undefined)
                            create_data_property(indexGroups, groupNames[i - 1u], pairs[i]);
                }
                obj(indices).put(the_names().groups, indexGroups, AttributesDefault);
                ao.put(the_names().indices, indices, AttributesDefault);
            }
            return a;
        }

        // (whether the object's exec is %RegExp.prototype.exec% (a data property that we can use without calling it))
        bool find_builtin_exec(regexp_object& r, regexp_state& aState);
        inline bool has_builtin_exec(regexp_object& r, regexp_state& aState)
        {
            if (!r.matcher || r.proto != the_realm().regexpPrototype)
                return false;
            // (a regexp of a shape known not to have its own exec, whose prototype (of the shape it had then) has the built-in at the 
            // position it had then)
            auto& p = obj(r.proto);
            auto const* ownShape = r.properties.layout();
            auto const* prototypeShape = p.properties.layout();
            if (ownShape != nullptr && ownShape == aState.execOwn && prototypeShape != nullptr && prototypeShape == aState.execPrototype)
                return p.properties.value_at(aState.execPosition) == aState.k.execFunction;
            return find_builtin_exec(r, aState);
        }
        bool find_builtin_exec(regexp_object& r, regexp_state& aState)
        {
            // (as has_builtin_exec, without the shapes it knows: found, and known)
            auto const& keys = aState.k;
            auto& p = obj(r.proto);
            auto const* ownShape = r.properties.layout();
            auto const* prototypeShape = p.properties.layout();
            if (r.own(keys.exec) != nullptr)
                return false;
            auto const exec = p.own(keys.exec);
            if (exec == nullptr || exec->accessor() || exec->data() != keys.execFunction)
                return false;
            aState.execOwn = ownShape;
            aState.execPrototype = prototypeShape;
            aState.execPosition = exec.position();
            return true;
        }
        bool has_builtin_exec(value aR)
        {
            auto* const r = as_regexp(aR);
            return r != nullptr && has_builtin_exec(*r, the_regexp_state());
        }

        // (RegExpExec)
        value regexp_exec(value aR, value aS, exec_kind aKind = exec_kind::Result)
        {
            if (has_builtin_exec(aR))
                return builtin_exec(*as_regexp(aR), aR, aS, aKind);
            auto const exec = get(aR, the_keys().exec);
            if (is_callable(exec))
            {
                value const args[] = { aS };
                auto const result = call(exec, aR, args);
                if (!is_object(result) && result != Null)
                    throw_type_error("The result of a RegExp exec method must be an object or null");
                return result;
            }
            auto* const r = as_regexp(aR);
            if (r == nullptr || !r->matcher)
                throw_type_error("RegExp exec method called on an incompatible receiver");
            return builtin_exec(*r, aR, aS, aKind);
        }

        // --- the constructor

        value regexp_constructor(value, arguments aArguments, value aNewTarget, function_object& aCallee)
        {
            auto const pattern = argument(aArguments, 0u);
            auto const flags = argument(aArguments, 1u);
            bool const patternIsRegExp = is_regexp(pattern);
            auto newTarget = aNewTarget;
            if (newTarget == Undefined)
            {
                newTarget = value_of(aCallee);
                if (patternIsRegExp && flags == Undefined)
                {
                    auto const patternConstructor = get(pattern, the_names().constructor);
                    if (same_value(newTarget, patternConstructor))
                        return pattern;
                }
            }
            value p;
            value f;
            if (auto* const r = as_regexp(pattern); r != nullptr && r->matcher)
            {
                p = r->source;
                f = flags == Undefined ? r->flags : flags;
            }
            else if (patternIsRegExp)
            {
                p = get(pattern, the_keys().source);
                f = flags == Undefined ? get(pattern, the_keys().flags) : flags;
            }
            else
            {
                p = pattern;
                f = flags;
            }
            auto const o = regexp_alloc(newTarget);
            return regexp_initialize(o, p, f);
        }

        value regexp_species(value aThis, arguments, value, function_object&)
        {
            return aThis;
        }

        // (RegExp.escape)
        value regexp_escape(value, arguments aArguments, value, function_object&)
        {
            auto const s = argument(aArguments, 0u);
            if (!is_string(s))
                throw_type_error("RegExp.escape requires a string");
            auto const& text = str(s);
            std::u16string escaped;
            auto const hex = [](unsigned aValue, int aDigits)
            {
                std::u16string result;
                for (int i = aDigits - 1; i >= 0; --i)
                    result.push_back(u"0123456789abcdef"[(aValue >> (i * 4)) & 0xFu]);
                return result;
            };
            for (std::size_t i = 0u; i < text.size(); ++i)
            {
                char32_t cp = text[i];
                std::size_t units = 1u;
                if (is_lead(text[i]) && i + 1u < text.size() && is_trail(text[i + 1u]))
                {
                    cp = 0x10000u + ((cp - 0xD800u) << 10u) + (text[i + 1u] - 0xDC00u);
                    units = 2u;
                }
                if (escaped.empty() && ((cp >= u'0' && cp <= u'9') || (cp >= u'a' && cp <= u'z') || (cp >= u'A' && cp <= u'Z')))
                {
                    escaped += u"\\x";
                    escaped += hex(cp, 2);
                    continue;
                }
                // (EncodeForRegExpEscape)
                switch (cp)
                {
                case u'^': case u'$': case u'\\': case u'.': case u'*': case u'+': case u'?': case u'(': case u')': case u'[': case u']':
                case u'{': case u'}': case u'|': case u'/':
                    escaped.push_back(u'\\');
                    escaped.push_back(static_cast<char16_t>(cp));
                    continue;
                case 0x09u: escaped += u"\\t"; continue;
                case 0x0Au: escaped += u"\\n"; continue;
                case 0x0Bu: escaped += u"\\v"; continue;
                case 0x0Cu: escaped += u"\\f"; continue;
                case 0x0Du: escaped += u"\\r"; continue;
                default:
                    break;
                }
                static constexpr std::u16string_view otherPunctuators = u",-=<>#&!%:;@~'`\"";
                bool const whiteSpaceOrLineTerminator = cp == 0x0Au || cp == 0x0Du || cp == 0x2028u || cp == 0x2029u || unicode::white_space(cp);
                if ((cp < 0x10000u && otherPunctuators.find(static_cast<char16_t>(cp)) != std::u16string_view::npos) || whiteSpaceOrLineTerminator ||
                    (cp >= 0xD800u && cp <= 0xDFFFu))
                {
                    if (cp <= 0xFFu)
                    {
                        escaped += u"\\x";
                        escaped += hex(cp, 2);
                    }
                    else
                        for (std::size_t u = 0u; u < units; ++u)
                        {
                            escaped += u"\\u";
                            escaped += hex(text[i + u], 4);
                        }
                    i += units - 1u;
                    continue;
                }
                escaped.append(text, i, units);
                i += units - 1u;
            }
            return make_string(std::move(escaped));
        }

        // --- RegExp.prototype

        value regexp_prototype_exec(value aThis, arguments aArguments, value, function_object&)
        {
            auto& r = this_regexp(aThis, "exec");
            auto const s = to_string(argument(aArguments, 0u));
            return builtin_exec(r, aThis, s);
        }

        value regexp_prototype_test(value aThis, arguments aArguments, value, function_object&)
        {
            // (a RegExp whose exec is the built-in, and a string: RegExpBuiltinExec's match (as below, without the steps that change 
            // nothing))
            if (auto* const r = as_regexp(aThis); r != nullptr)
                if (auto const s = argument(aArguments, 0u); is_string(s))
                    if (auto& state = the_regexp_state(); has_builtin_exec(*r, state))
                        return boolean(builtin_match(*r, aThis, s, the_heap().regexpCaptures, state));
            require_object(aThis, "test");
            auto const s = to_string(argument(aArguments, 0u));
            return boolean(regexp_exec(aThis, s, exec_kind::Test) != Null);
        }

        value regexp_prototype_to_string(value aThis, arguments, value, function_object&)
        {
            require_object(aThis, "toString");
            auto const pattern = to_string(get(aThis, the_keys().source));
            auto const flags = to_string(get(aThis, the_keys().flags));
            return concat(concat(concat(ascii("/"), pattern), ascii("/")), flags);
        }

        value regexp_prototype_compile(value aThis, arguments aArguments, value, function_object&)
        {
            auto const& r = this_regexp(aThis, "compile");
            if (!r.legacyFeaturesEnabled)
                throw_type_error("RegExp.prototype.compile cannot be called on a subclass instance");
            auto const pattern = argument(aArguments, 0u);
            auto const flags = argument(aArguments, 1u);
            value p = pattern;
            value f = flags;
            if (auto* const r = as_regexp(pattern); r != nullptr && r->matcher)
            {
                if (flags != Undefined)
                    throw_type_error("Cannot supply flags when constructing one RegExp from another");
                p = r->source;
                f = r->flags;
            }
            return regexp_initialize(aThis, p, f);
        }

        value regexp_prototype_flags(value aThis, arguments, value, function_object&)
        {
            if (!is_object(aThis))
                throw_type_error("RegExp.prototype.flags getter called on a non-object");
            std::u16string result;
            auto const flag = [&](value aKey, char16_t aFlag)
            {
                if (to_boolean(get(aThis, aKey)))
                    result.push_back(aFlag);
            };
            flag(the_keys().hasIndices, u'd');
            flag(the_keys().global, u'g');
            flag(the_keys().ignoreCase, u'i');
            flag(the_keys().multiline, u'm');
            flag(the_keys().dotAll, u's');
            flag(the_keys().unicode, u'u');
            flag(the_keys().unicodeSets, u'v');
            flag(the_keys().sticky, u'y');
            return make_string(std::move(result));
        }

        // (RegExpHasFlag)
        value has_flag(value aThis, std::uint32_t aFlag, char const* aName)
        {
            if (!is_object(aThis))
                throw_type_error(std::string{ "RegExp.prototype." } + aName + " getter called on a non-object");
            auto* const r = as_regexp(aThis);
            if (r == nullptr || !r->matcher)
            {
                if (aThis == the_realm().regexpPrototype)
                    return Undefined;
                throw_type_error(std::string{ "RegExp.prototype." } + aName + " getter called on an incompatible receiver");
            }
            return boolean((r->flagBits & aFlag) != 0u);
        }

        value regexp_prototype_has_indices(value aThis, arguments, value, function_object&) { return has_flag(aThis, regexp::FlagHasIndices, "hasIndices"); }
        value regexp_prototype_global(value aThis, arguments, value, function_object&) { return has_flag(aThis, regexp::FlagGlobal, "global"); }
        value regexp_prototype_ignore_case(value aThis, arguments, value, function_object&) { return has_flag(aThis, regexp::FlagIgnoreCase, "ignoreCase"); }
        value regexp_prototype_multiline(value aThis, arguments, value, function_object&) { return has_flag(aThis, regexp::FlagMultiline, "multiline"); }
        value regexp_prototype_dot_all(value aThis, arguments, value, function_object&) { return has_flag(aThis, regexp::FlagDotAll, "dotAll"); }
        value regexp_prototype_unicode(value aThis, arguments, value, function_object&) { return has_flag(aThis, regexp::FlagUnicode, "unicode"); }
        value regexp_prototype_unicode_sets(value aThis, arguments, value, function_object&) { return has_flag(aThis, regexp::FlagUnicodeSets, "unicodeSets"); }
        value regexp_prototype_sticky(value aThis, arguments, value, function_object&) { return has_flag(aThis, regexp::FlagSticky, "sticky"); }

        // (EscapeRegExpPattern: / and line terminators are escaped (so that /source/flags is a literal that is the same pattern))
        value escape_pattern(value aSource, std::uint32_t aFlags)
        {
            auto const& source = str(aSource);
            if (source.empty())
                return ascii("(?:)");
            bool const nested = (aFlags & regexp::FlagUnicodeSets) != 0u;
            std::u16string result;
            result.reserve(source.size());
            std::size_t classDepth = 0u;
            auto const line_terminator = [&](char16_t aChar, bool aEscaped)
            {
                if (!aEscaped)
                    result.push_back(u'\\');
                switch (aChar)
                {
                case 0x0Au: result.push_back(u'n'); break;
                case 0x0Du: result.push_back(u'r'); break;
                case 0x2028u: result += u"u2028"; break;
                default: result += u"u2029"; break;
                }
            };
            for (std::size_t i = 0u; i < source.size(); ++i)
            {
                auto const c = source[i];
                if (c == u'\\')
                {
                    result.push_back(c);
                    if (i + 1u < source.size())
                    {
                        auto const next = source[++i];
                        if (next == 0x0Au || next == 0x0Du || next == 0x2028u || next == 0x2029u)
                            line_terminator(next, true);
                        else
                            result.push_back(next);
                    }
                    continue;
                }
                if (c == 0x0Au || c == 0x0Du || c == 0x2028u || c == 0x2029u)
                {
                    line_terminator(c, false);
                    continue;
                }
                if (c == u'/' && classDepth == 0u)
                {
                    result += u"\\/";
                    continue;
                }
                if (c == u'[')
                {
                    if (classDepth == 0u || nested)
                        ++classDepth;
                }
                else if (c == u']' && classDepth > 0u)
                    --classDepth;
                result.push_back(c);
            }
            return make_string(std::move(result));
        }

        value regexp_prototype_source(value aThis, arguments, value, function_object&)
        {
            if (!is_object(aThis))
                throw_type_error("RegExp.prototype.source getter called on a non-object");
            auto* const r = as_regexp(aThis);
            if (r == nullptr || !r->matcher)
            {
                if (aThis == the_realm().regexpPrototype)
                    return ascii("(?:)");
                throw_type_error("RegExp.prototype.source getter called on an incompatible receiver");
            }
            return escape_pattern(r->source, r->flagBits);
        }

        // (GetLegacyRegExpStaticProperty: aWhich: 0 lastMatch, 1-9 $1-$9, 10 lastParen, 11 leftContext, 12 rightContext, 13 input)
        template <int Which>
        value legacy_get(value aThis, arguments, value, function_object&)
        {
            if (aThis != the_realm().regexp)
                throw_type_error("RegExp legacy static property getter called on an incompatible receiver");
            if (!the_legacy().valid)
                throw_type_error("RegExp legacy static property is not available");
            if constexpr (Which == 13)
                return the_legacy().input == Undefined ? the_names().empty : the_legacy().input;
            else
            {
                if (the_legacy().subject == Undefined)
                    return the_names().empty;
                auto const& text = str(the_legacy().subject);
                if constexpr (Which == 11)
                    return substring(text, 0, the_legacy().captures[0]);
                else if constexpr (Which == 12)
                    return substring(text, the_legacy().captures[1], static_cast<std::int32_t>(text.size()));
                else
                {
                    if (Which != 10 && Which >= static_cast<int>(the_legacy().count))
                        return the_names().empty;
                    auto const* const c = Which == 10 ? the_legacy().lastParen : the_legacy().captures + Which * 2;
                    return c[0] == -1 ? the_names().empty : substring(text, c[0], c[1]);
                }
            }
        }

        value legacy_set_input(value aThis, arguments aArguments, value, function_object&)
        {
            if (aThis != the_realm().regexp)
                throw_type_error("RegExp.input setter called on an incompatible receiver");
            the_legacy().input = to_string(argument(aArguments, 0u));
            return Undefined;
        }

        bool flags_contain(value aFlags, char16_t aFlag)
        {
            return str(aFlags).find(aFlag) != std::u16string::npos;
        }

        // (for the global loops: an empty match advances lastIndex)
        void advance_after_empty_match(value aR, value aMatch, std::u16string_view aText, bool aFullUnicode)
        {
            auto const matchStr = to_string(get(aMatch, index_key(0u)));
            if (string_length(matchStr) == 0u)
            {
                auto const thisIndex = to_length(get(aR, the_names().lastIndex));
                auto const nextIndex = advance_string_index(aText, thisIndex, aFullUnicode);
                set(aR, the_names().lastIndex, number(nextIndex), true);
            }
        }

        value regexp_prototype_match(value aThis, arguments aArguments, value, function_object&)
        {
            require_object(aThis, "[Symbol.match]");
            auto const s = to_string(argument(aArguments, 0u));
            auto const flags = to_string(get(aThis, the_keys().flags));
            if (!flags_contain(flags, u'g'))
                return regexp_exec(aThis, s);
            bool const fullUnicode = flags_contain(flags, u'u') || flags_contain(flags, u'v');
            set(aThis, the_names().lastIndex, number(0.0), true);
            auto const& text = str(s);
            value_list results;
            if (has_builtin_exec(aThis))
            {
                // (%RegExp.prototype.exec%: the matches without their arrays (which are not observable))
                auto& r = *as_regexp(aThis);
                thread_local std::vector<std::int32_t> tCaptures;
                tCaptures.clear();
                while (builtin_match(r, aThis, s, tCaptures))
                {
                    results.push_back(substring(text, tCaptures[0], tCaptures[1]));
                    if (tCaptures[1] <= tCaptures[0])
                    {
                        auto const thisIndex = to_length(get(aThis, the_names().lastIndex));
                        set(aThis, the_names().lastIndex, number(advance_string_index(text, thisIndex, fullUnicode)), true);
                    }
                }
                return results.empty() ? Null : make_array(std::move(results));
            }
            for (;;)
            {
                auto const result = regexp_exec(aThis, s);
                if (result == Null)
                    return results.empty() ? Null : make_array(std::move(results));
                auto const matchStr = to_string(get(result, index_key(0u)));
                results.push_back(matchStr);
                if (string_length(matchStr) == 0u)
                {
                    auto const thisIndex = to_length(get(aThis, the_names().lastIndex));
                    auto const nextIndex = advance_string_index(text, thisIndex, fullUnicode);
                    set(aThis, the_names().lastIndex, number(nextIndex), true);
                }
            }
        }

        value regexp_prototype_match_all(value aThis, arguments aArguments, value, function_object&)
        {
            require_object(aThis, "[Symbol.matchAll]");
            auto const s = to_string(argument(aArguments, 0u));
            auto const c = species_constructor(aThis, the_realm().regexp);
            auto const flags = to_string(get(aThis, the_keys().flags));
            value const args[] = { aThis, flags };
            auto const matcher = construct(c, args);
            auto const lastIndex = to_length(get(aThis, the_names().lastIndex));
            set(matcher, the_names().lastIndex, number(lastIndex), true);
            auto& it = the_heap().make<regexp_string_iterator>(the_realm().regexpStringIteratorPrototype);
            it.iteratingRegExp = matcher;
            it.iteratedString = s;
            it.global = flags_contain(flags, u'g');
            it.fullUnicode = flags_contain(flags, u'u') || flags_contain(flags, u'v');
            return value_of(it);
        }

        value regexp_string_iterator_next(value aThis, arguments, value, function_object&)
        {
            if (!is_object(aThis) || obj(aThis).cls != object_class::RegExpStringIterator)
                throw_type_error("%RegExpStringIteratorPrototype%.next requires that 'this' be a RegExp String Iterator");
            auto& it = static_cast<regexp_string_iterator&>(obj(aThis));
            if (it.done)
                return create_iter_result_object(Undefined, true);
            auto const r = it.iteratingRegExp;
            auto const s = it.iteratedString;
            auto const match = regexp_exec(r, s);
            if (match == Null)
            {
                it.done = true;
                return create_iter_result_object(Undefined, true);
            }
            if (!it.global)
            {
                it.done = true;
                return create_iter_result_object(match, false);
            }
            advance_after_empty_match(r, match, str(s), it.fullUnicode);
            return create_iter_result_object(match, false);
        }

        value regexp_prototype_replace(value aThis, arguments aArguments, value, function_object&)
        {
            require_object(aThis, "[Symbol.replace]");
            auto const s = to_string(argument(aArguments, 0u));
            auto const& text = str(s);
            auto const lengthS = text.size();
            auto replaceValue = argument(aArguments, 1u);
            bool const functionalReplace = is_callable(replaceValue);
            if (!functionalReplace)
                replaceValue = to_string(replaceValue);
            auto const flags = to_string(get(aThis, the_keys().flags));
            bool const global = flags_contain(flags, u'g');
            bool fullUnicode = false;
            if (global)
            {
                fullUnicode = flags_contain(flags, u'u') || flags_contain(flags, u'v');
                set(aThis, the_names().lastIndex, number(0.0), true);
            }
            std::u16string accumulatedResult;
            std::size_t nextSourcePosition = 0u;
            value_list captures;
            auto const replace = [&](value aMatched, std::size_t aPosition, value aNamedCaptures)
            {
                auto const matchLength = string_length(aMatched);
                std::u16string replacement;
                if (functionalReplace)
                {
                    value_list replacerArgs;
                    replacerArgs.reserve(captures.size() + 4u);
                    replacerArgs.push_back(aMatched);
                    replacerArgs.insert(replacerArgs.end(), captures.begin(), captures.end());
                    replacerArgs.push_back(number(static_cast<double>(aPosition)));
                    replacerArgs.push_back(s);
                    if (aNamedCaptures != Undefined)
                        replacerArgs.push_back(aNamedCaptures);
                    replacement = str(to_string(call(replaceValue, Undefined, replacerArgs)));
                }
                else
                {
                    if (aNamedCaptures != Undefined)
                        aNamedCaptures = to_object(aNamedCaptures);
                    replacement = get_substitution_text(str(aMatched), text, aPosition, captures, aNamedCaptures, str(replaceValue));
                }
                if (aPosition >= nextSourcePosition)
                {
                    accumulatedResult.append(text, nextSourcePosition, aPosition - nextSourcePosition);
                    accumulatedResult += replacement;
                    nextSourcePosition = aPosition + matchLength;
                }
            };
            if (has_builtin_exec(aThis))
            {
                // (%RegExp.prototype.exec%: the matches without their arrays (which are not observable))
                auto& r = *as_regexp(aThis);
                auto const program = r.matcher;
                std::vector<std::int32_t> matches;
                thread_local std::vector<std::int32_t> tSlots;
                tSlots.clear();
                auto const slotCount = (program->captureCount + 1u) * 2u;
                while (builtin_match(r, aThis, s, tSlots))
                {
                    matches.insert(matches.end(), tSlots.begin(), tSlots.begin() + slotCount);
                    if (!global)
                        break;
                    if (tSlots[1] <= tSlots[0])
                    {
                        auto const thisIndex = to_length(get(aThis, the_names().lastIndex));
                        set(aThis, the_names().lastIndex, number(advance_string_index(text, thisIndex, fullUnicode)), true);
                    }
                }
                for (std::size_t m = 0u; m < matches.size(); m += slotCount)
                {
                    auto const* const c = matches.data() + m;
                    captures.clear();
                    for (std::uint32_t i = 1u; i <= program->captureCount; ++i)
                        captures.push_back(c[i * 2u] == -1 ? Undefined : substring(text, c[i * 2u], c[i * 2u + 1u]));
                    value namedCaptures = Undefined;
                    if (program->hasNamedGroups)
                    {
                        value_list elements{ Undefined };
                        elements.insert(elements.end(), captures.begin(), captures.end());
                        namedCaptures = make_groups(*program, elements);
                    }
                    replace(substring(text, c[0], c[1]), static_cast<std::size_t>(c[0]), namedCaptures);
                }
            }
            else
            {
                value_list results;
                for (;;)
                {
                    auto const result = regexp_exec(aThis, s);
                    if (result == Null)
                        break;
                    results.push_back(result);
                    if (!global)
                        break;
                    advance_after_empty_match(aThis, result, text, fullUnicode);
                }
                for (auto const result : results)
                {
                    auto const resultLength = length_of_array_like(result);
                    auto const nCaptures = std::max(resultLength - 1.0, 0.0);
                    auto const matched = to_string(get(result, index_key(0u)));
                    auto const positionValue = to_integer_or_infinity(get(result, the_names().index));
                    auto const position = static_cast<std::size_t>(std::clamp(positionValue, 0.0, static_cast<double>(lengthS)));
                    captures.clear();
                    for (double n = 1.0; n <= nCaptures; n += 1.0)
                    {
                        auto capN = get(result, index_key(static_cast<std::uint64_t>(n)));
                        if (capN != Undefined)
                            capN = to_string(capN);
                        captures.push_back(capN);
                    }
                    replace(matched, position, get(result, the_names().groups));
                }
            }
            if (nextSourcePosition >= lengthS)
                return make_string(std::move(accumulatedResult));
            accumulatedResult.append(text, nextSourcePosition);
            return make_string(std::move(accumulatedResult));
        }

        value regexp_prototype_search(value aThis, arguments aArguments, value, function_object&)
        {
            require_object(aThis, "[Symbol.search]");
            auto const s = to_string(argument(aArguments, 0u));
            auto const previousLastIndex = get(aThis, the_names().lastIndex);
            if (!same_value(previousLastIndex, number(0.0)))
                set(aThis, the_names().lastIndex, number(0.0), true);
            auto const result = regexp_exec(aThis, s);
            auto const currentLastIndex = get(aThis, the_names().lastIndex);
            if (!same_value(currentLastIndex, previousLastIndex))
                set(aThis, the_names().lastIndex, previousLastIndex, true);
            if (result == Null)
                return number(-1.0);
            return get(result, the_names().index);
        }

        value regexp_prototype_split(value aThis, arguments aArguments, value, function_object&)
        {
            require_object(aThis, "[Symbol.split]");
            auto const s = to_string(argument(aArguments, 0u));
            auto const c = species_constructor(aThis, the_realm().regexp);
            auto const flags = to_string(get(aThis, the_keys().flags));
            bool const unicodeMatching = flags_contain(flags, u'u') || flags_contain(flags, u'v');
            auto const newFlags = flags_contain(flags, u'y') ? flags : concat(flags, ascii("y"));
            value const args[] = { aThis, newFlags };
            auto const splitter = construct(c, args);
            value_list a;
            auto const limit = argument(aArguments, 1u);
            std::uint32_t const lim = limit == Undefined ? 0xFFFFFFFFu : to_uint32(limit);
            if (lim == 0u)
                return make_array(std::move(a));
            auto const& text = str(s);
            auto const size = static_cast<double>(text.size());
            if (text.empty())
            {
                auto const z = regexp_exec(splitter, s, exec_kind::Test);
                if (z != Null)
                    return make_array(std::move(a));
                a.push_back(s);
                return make_array(std::move(a));
            }
            double p = 0.0;
            double q = p;
            if (c == the_realm().regexp && has_builtin_exec(splitter))
            {
                // (%RegExp% and %RegExp.prototype.exec%: the splitter is not observable, so its sticky matches at each position
                // from q are a search from q)
                auto& r = *as_regexp(splitter);
                auto const program = r.matcher;
                thread_local std::vector<std::int32_t> tCaptures;
                tCaptures.clear();
                while (q < size)
                {
                    auto const result = regexp::exec(*program, text, static_cast<std::size_t>(q), false, tCaptures);
                    if (result == regexp::match_result::Overflow)
                        throw_range_error("Maximum regular expression backtracking stack size exceeded");
                    if (result == regexp::match_result::NoMatch || tCaptures[0] >= static_cast<std::int32_t>(text.size()))
                        break;
                    update_legacy_statics(s, tCaptures, tCaptures[0], program->captureCount);
                    q = tCaptures[0];
                    auto const e = std::min(static_cast<double>(tCaptures[1]), size);
                    if (e == p)
                    {
                        q = advance_string_index(text, q, unicodeMatching);
                        continue;
                    }
                    a.push_back(substring(text, static_cast<std::int32_t>(p), static_cast<std::int32_t>(q)));
                    if (a.size() == lim)
                        return make_array(std::move(a));
                    p = e;
                    for (std::uint32_t i = 1u; i <= program->captureCount; ++i)
                    {
                        a.push_back(tCaptures[i * 2u] == -1 ? Undefined : substring(text, tCaptures[i * 2u], tCaptures[i * 2u + 1u]));
                        if (a.size() == lim)
                            return make_array(std::move(a));
                    }
                    q = p;
                }
                a.push_back(substring(text, static_cast<std::int32_t>(p), static_cast<std::int32_t>(size)));
                return make_array(std::move(a));
            }
            while (q < size)
            {
                set(splitter, the_names().lastIndex, number(q), true);
                auto const z = regexp_exec(splitter, s);
                if (z == Null)
                {
                    q = advance_string_index(text, q, unicodeMatching);
                    continue;
                }
                auto const e = std::min(to_length(get(splitter, the_names().lastIndex)), size);
                if (e == p)
                {
                    q = advance_string_index(text, q, unicodeMatching);
                    continue;
                }
                a.push_back(substring(text, static_cast<std::int32_t>(p), static_cast<std::int32_t>(q)));
                if (a.size() == lim)
                    return make_array(std::move(a));
                p = e;
                auto const numberOfCaptures = std::max(length_of_array_like(z) - 1.0, 0.0);
                for (double i = 1.0; i <= numberOfCaptures; i += 1.0)
                {
                    a.push_back(get(z, index_key(static_cast<std::uint64_t>(i))));
                    if (a.size() == lim)
                        return make_array(std::move(a));
                }
                q = p;
            }
            a.push_back(substring(text, static_cast<std::int32_t>(p), static_cast<std::int32_t>(size)));
            return make_array(std::move(a));
        }
    }

    value make_regexp(value aPattern, value aFlags)
    {
        auto const o = regexp_alloc(the_realm().regexp);
        return regexp_initialize(o, aPattern, aFlags);
    }

    namespace
    {
#if defined(_MSC_VER)
        __declspec(noinline)
#else
        [[gnu::noinline]]
#endif
        value regexp_test_call(value aCallee, value aThis, value aString)
        {
            value const parameters[] = { aString };
            return regexp_prototype_test(aThis, parameters, Undefined, *as_function(aCallee));
        }
    }

    value regexp_test_direct(value aCallee, value aThis, value aString)
    {
        // (as the built-in has it (its fast path first; else the built-in called))
        if (is_string(aString))
            if (auto* const r = as_regexp(aThis); r != nullptr)
                if (auto& state = the_regexp_state(); has_builtin_exec(*r, state))
                {
                    // (a set of words (see regexp::compile), neither global nor sticky, whose lastIndex is a number (its ToLength calls 
                    // nothing, and it is not used): RegExpBuiltinExec's match here (one of its words: the whole string))
                    auto const& program = *r->matcher;
                    if (program.wordSet && (r->flagBits & (regexp::FlagGlobal | regexp::FlagSticky)) == 0u)
                        if (auto const* k = known_last_index(*r, state); k != nullptr && is_number(r->properties.value_at(k->position)))
                        {
                            auto const text = str(aString);
                            auto const length = text.size();
                            if (length < program.shortestWord || length > program.longestWord)
                                return False;
                            // (the words of its length: their first and last code units compared first, then the others)
                            bool found = false;
                            auto const& same = program.words[length];
                            if (length == 0u)
                                found = !same.empty();
                            else
                            {
                                auto const* const t = text.data();
                                auto const first = t[0];
                                auto const last = t[length - 1u];
                                for (auto const& word : same)
                                {
                                    auto const* const w = word.data();
                                    if (w[0] == first && w[length - 1u] == last && std::memcmp(w, t, length * sizeof(char16_t)) == 0)
                                    {
                                        found = true;
                                        break;
                                    }
                                }
                            }
                            if (!found)
                                return False;
                            if (r->legacyFeaturesEnabled)
                            {
                                // (the match, and no captures)
                                auto& legacy = state.legacy;
                                legacy.valid = true;
                                legacy.input = aString;
                                legacy.subject = aString;
                                legacy.captures[0] = 0;
                                legacy.captures[1] = static_cast<std::int32_t>(length);
                                legacy.count = 1u;
                                legacy.lastParen[0] = -1;
                                legacy.lastParen[1] = -1;
                            }
                            else
                                state.legacy.valid = false;
                            return True;
                        }
                    return boolean(builtin_match(*r, aThis, aString, the_heap().regexpCaptures, state));
                }
        return regexp_test_call(aCallee, aThis, aString);
    }

    void init_regexp(realm& aRealm)
    {
        the_keys().source = ascii("source");
        the_keys().flags = ascii("flags");
        the_keys().exec = ascii("exec");
        the_keys().global = ascii("global");
        the_keys().ignoreCase = ascii("ignoreCase");
        the_keys().multiline = ascii("multiline");
        the_keys().dotAll = ascii("dotAll");
        the_keys().unicode = ascii("unicode");
        the_keys().unicodeSets = ascii("unicodeSets");
        the_keys().sticky = ascii("sticky");
        the_keys().hasIndices = ascii("hasIndices");

        aRealm.regexpPrototype = make_object(aRealm.objectPrototype);
        auto const p = aRealm.regexpPrototype;
        auto const c = make_native("RegExp", 2u, regexp_constructor, true);
        aRealm.regexp = c;
        obj(c).put(the_names().prototype, p, 0u);
        obj(p).put(the_names().constructor, c);
        getter(c, aRealm.symSpecies, regexp_species);
        method(c, "escape", 1u, regexp_escape);

        the_keys().execFunction = aRealm.regexpExec = method(p, "exec", 1u, regexp_prototype_exec);
        method(p, "compile", 2u, regexp_prototype_compile);
        getter(p, the_keys().dotAll, regexp_prototype_dot_all);
        getter(p, the_keys().flags, regexp_prototype_flags);
        getter(p, the_keys().global, regexp_prototype_global);
        getter(p, the_keys().hasIndices, regexp_prototype_has_indices);
        getter(p, the_keys().ignoreCase, regexp_prototype_ignore_case);
        method(p, aRealm.symMatch, 1u, regexp_prototype_match);
        method(p, aRealm.symMatchAll, 1u, regexp_prototype_match_all);
        getter(p, the_keys().multiline, regexp_prototype_multiline);
        method(p, aRealm.symReplace, 2u, regexp_prototype_replace);
        method(p, aRealm.symSearch, 1u, regexp_prototype_search);
        getter(p, the_keys().source, regexp_prototype_source);
        method(p, aRealm.symSplit, 2u, regexp_prototype_split);
        getter(p, the_keys().sticky, regexp_prototype_sticky);
        aRealm.regexpTest = method(p, "test", 1u, regexp_prototype_test);
        method(p, "toString", 0u, regexp_prototype_to_string);
        getter(p, the_keys().unicode, regexp_prototype_unicode);
        getter(p, the_keys().unicodeSets, regexp_prototype_unicode_sets);

        aRealm.regexpStringIteratorPrototype = make_object(aRealm.iteratorPrototype);
        method(aRealm.regexpStringIteratorPrototype, "next", 0u, regexp_string_iterator_next);
        obj(aRealm.regexpStringIteratorPrototype).put(aRealm.symToStringTag, ascii("RegExp String Iterator"), AttributeConfigurable);

        // (the legacy static properties)
        getter(c, ascii("input"), legacy_get<13>, legacy_set_input);
        getter(c, ascii("$_"), legacy_get<13>, legacy_set_input);
        getter(c, ascii("lastMatch"), legacy_get<0>);
        getter(c, ascii("$&"), legacy_get<0>);
        getter(c, ascii("lastParen"), legacy_get<10>);
        getter(c, ascii("$+"), legacy_get<10>);
        getter(c, ascii("leftContext"), legacy_get<11>);
        getter(c, ascii("$`"), legacy_get<11>);
        getter(c, ascii("rightContext"), legacy_get<12>);
        getter(c, ascii("$'"), legacy_get<12>);
        getter(c, ascii("$1"), legacy_get<1>);
        getter(c, ascii("$2"), legacy_get<2>);
        getter(c, ascii("$3"), legacy_get<3>);
        getter(c, ascii("$4"), legacy_get<4>);
        getter(c, ascii("$5"), legacy_get<5>);
        getter(c, ascii("$6"), legacy_get<6>);
        getter(c, ascii("$7"), legacy_get<7>);
        getter(c, ascii("$8"), legacy_get<8>);
        getter(c, ascii("$9"), legacy_get<9>);

        obj(aRealm.global).put(ascii("RegExp"), c);
    }
}
