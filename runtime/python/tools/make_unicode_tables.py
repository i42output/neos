#!/usr/bin/env python3
# make_unicode_tables.py: the Python runtime plugin's Unicode database (CPython 3.13's: UCD 15.1.0), generated from CPython's
# unicodedata and str methods (the host Python must be 3.13) and UCD's NameAliases.txt and NamedSequences.txt:
#   make_unicode_tables.py <ucd directory> <unicode_names.hpp> <unicode_tables.hpp>
import sys, unicodedata as U, collections

assert U.unidata_version == '15.1.0', U.unidata_version
ucd_dir, names_path, tables_path = sys.argv[1:4]
MAX = 0x110000

HEADER = """/*
  {name}

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
{notice}*/

"""

NAMES_NOTICE = "\n  Generated from the Unicode Character Database (its names, as CPython 3.13's unicodedata has them): Copyright ©\n  1991-2026 Unicode, Inc., used under the Unicode License V3 (see runtime/python/NOTICE).\n"
TABLES_NOTICE = "\n  Portions of this file are derived from CPython 3.13 (Modules/unicodedata.c and str's methods: the Unicode Character\n  Database as they have it), Copyright (c) 2001-2024 Python Software Foundation; All Rights Reserved. They are used under\n  the Python Software Foundation License Version 2 (see runtime/python/LICENSE.PSF). Changes: generated as C++ tables for\n  the neos Python runtime plugin (tools/make_unicode_tables.py; see runtime/python/NOTICE).\n\n  The Unicode Character Database: Copyright © 1991-2026 Unicode, Inc., used under the Unicode License V3 (see\n  runtime/python/NOTICE).\n"

def array(out, ctype, name, values, per_line=24):
    out.append(f"    inline constexpr {ctype} {name}[] =\n    {{\n")
    values = list(values)
    if not values:
        values = [0]
    for i in range(0, len(values), per_line):
        out.append("        " + ",".join(str(v) for v in values[i:i + per_line]) + ",\n")
    out.append("    };\n")

# --- names

names = {}
algorithmic = collections.defaultdict(list)    # prefix -> [cps]
for cp in range(MAX):
    try:
        n = U.name(chr(cp))
    except ValueError:
        continue
    if n.endswith('-%04X' % cp) or n.endswith('-%05X' % cp):
        algorithmic[n.rsplit('-', 1)[0]].append(cp)
    elif n.startswith('HANGUL SYLLABLE ') and 0xAC00 <= cp <= 0xD7A3:
        pass
    else:
        names[cp] = n
ranges = []
for prefix, cps in algorithmic.items():
    start = prev = cps[0]
    for cp in cps[1:] + [None]:
        if cp is None or cp != prev + 1:
            ranges.append((prefix, start, prev))
            if cp is not None:
                start = cp
        if cp is not None:
            prev = cp
ranges.sort(key=lambda r: r[1])
prefixes = sorted(set(r[0] for r in ranges))

words = collections.Counter(w for n in names.values() for w in n.split(' '))
word_order = [w for w, _ in sorted(words.items(), key=lambda kv: (-kv[1], kv[0]))]
word_index = {w: i for i, w in enumerate(word_order)}
assert len(word_order) < 0x8000
word_data = bytearray()
word_offsets = []
for w in word_order:
    word_offsets.append(len(word_data))
    word_data += w.encode('ascii')
word_offsets.append(len(word_data))
name_cps = sorted(names)
name_data = bytearray()
name_offsets = []
for cp in name_cps:
    name_offsets.append(len(name_data))
    for w in names[cp].split(' '):
        i = word_index[w]
        if i < 0x80:
            name_data.append(i)
        else:
            name_data += bytes([0x80 | (i >> 8), i & 0xFF])
name_offsets.append(len(name_data))

aliases = []
for line in open(f'{ucd_dir}/NameAliases.txt'):
    line = line.split('#')[0].strip()
    if not line:
        continue
    cp, alias, kind = line.split(';')
    aliases.append((alias, int(cp, 16)))
    assert U.lookup(alias) == chr(int(cp, 16)), alias
sequences = []
for line in open(f'{ucd_dir}/NamedSequences.txt'):
    line = line.split('#')[0].strip()
    if not line:
        continue
    name, cps = line.split(';')
    seq = [int(c, 16) for c in cps.split()]
    sequences.append((name, seq))
    assert U.lookup(name) == ''.join(map(chr, seq)), name

out = [HEADER.format(name='unicode_names.hpp', notice=NAMES_NOTICE),
"""// The names of Unicode's characters (UCD 15.1.0, as CPython 3.13's unicodedata has them), their aliases and its named sequences:
// the Python compiler's (\\N{name} escapes) and the Python runtime's (unicodedata); generated (make_unicode_tables.py)

#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace neos::language::python::unicode_names
{
"""]
array(out, "std::uint8_t", "sWordData", word_data, 40)
array(out, "std::uint32_t", "sWordOffsets", word_offsets)
array(out, "std::uint32_t", "sNameCodePoints", name_cps, 16)
array(out, "std::uint32_t", "sNameOffsets", name_offsets, 16)
array(out, "std::uint8_t", "sNameData", name_data, 40)
out.append("    inline constexpr char const* sAlgorithmicPrefixes[] = { " + ", ".join('"%s"' % p for p in prefixes) + " };\n")
out.append("    struct algorithmic_range { std::uint8_t prefix; std::uint32_t first; std::uint32_t last; };\n")
out.append("    inline constexpr algorithmic_range sAlgorithmicRanges[] =\n    {\n")
for p, a, b in ranges:
    out.append(f"        {{ {prefixes.index(p)}, 0x{a:X}, 0x{b:X} }},\n")
out.append("    };\n")
out.append("    struct alias { char const* name; std::uint32_t codePoint; };\n")
out.append("    inline constexpr alias sAliases[] =\n    {\n")
for a, cp in aliases:
    out.append(f'        {{ "{a}", 0x{cp:X} }},\n')
out.append("    };\n")
seq_data = []
out.append("    struct named_sequence { char const* name; std::uint16_t offset; std::uint8_t length; };\n")
out.append("    inline constexpr named_sequence sNamedSequences[] =\n    {\n")
for n, seq in sequences:
    out.append(f'        {{ "{n}", {len(seq_data)}, {len(seq)} }},\n')
    seq_data += seq
out.append("    };\n")
array(out, "std::uint32_t", "sNamedSequenceData", seq_data, 16)
out.append(r'''
    inline constexpr char const* sJamoL[] = { "G", "GG", "N", "D", "DD", "R", "M", "B", "BB", "S", "SS", "", "J", "JJ", "C", "K", "T", "P", "H" };
    inline constexpr char const* sJamoV[] = { "A", "AE", "YA", "YAE", "EO", "E", "YEO", "YE", "O", "WA", "WAE", "OE", "YO", "U", "WEO", "WE", "WI",
        "YU", "EU", "YI", "I" };
    inline constexpr char const* sJamoT[] = { "", "G", "GG", "GS", "N", "NJ", "NH", "D", "L", "LG", "LM", "LB", "LS", "LT", "LP", "LH", "M", "B",
        "BS", "S", "SS", "NG", "J", "C", "K", "T", "P", "H" };

    // (a character's name: false if it has none)
    inline bool name_of(char32_t aCodePoint, std::string& aResult)
    {
        auto const cp = static_cast<std::uint32_t>(aCodePoint);
        if (cp >= 0xAC00u && cp <= 0xD7A3u)
        {
            auto const s = cp - 0xAC00u;
            aResult = std::string{ "HANGUL SYLLABLE " } + sJamoL[s / (21u * 28u)] + sJamoV[(s % (21u * 28u)) / 28u] + sJamoT[s % 28u];
            return true;
        }
        for (auto const& r : sAlgorithmicRanges)
            if (cp >= r.first && cp <= r.last)
            {
                char hex[16];
                auto n = 0;
                for (auto v = cp; v != 0u || n < 4; v >>= 4)
                    hex[n++] = "0123456789ABCDEF"[v & 15u];
                aResult = std::string{ sAlgorithmicPrefixes[r.prefix] } + "-";
                while (n > 0)
                    aResult += hex[--n];
                return true;
            }
        auto const i = std::lower_bound(std::begin(sNameCodePoints), std::end(sNameCodePoints), cp);
        if (i == std::end(sNameCodePoints) || *i != cp)
            return false;
        auto const index = static_cast<std::size_t>(i - std::begin(sNameCodePoints));
        aResult.clear();
        for (auto p = sNameOffsets[index]; p < sNameOffsets[index + 1u];)
        {
            std::uint32_t w = sNameData[p++];
            if (w >= 0x80u)
                w = ((w & 0x7Fu) << 8) | sNameData[p++];
            if (!aResult.empty())
                aResult += ' ';
            aResult.append(reinterpret_cast<char const*>(&sWordData[sWordOffsets[w]]), sWordOffsets[w + 1u] - sWordOffsets[w]);
        }
        return true;
    }

    // (the characters of a name (case-insensitive), an alias's (aAliases), or a named sequence's (aSequences))
    inline std::optional<std::u32string> lookup(std::string_view aName, bool aAliases, bool aSequences)
    {
        std::string name;
        for (auto c : aName)
            name += (c >= 'a' && c <= 'z') ? static_cast<char>(c - 32) : c;
        // (Hangul syllables and the algorithmic names)
        if (name.starts_with("HANGUL SYLLABLE "))
        {
            std::string_view rest{ name };
            rest.remove_prefix(16u);
            // (the longest jamo that matches each part, as CPython's find_syllable)
            auto const find = [&](char const* const* aJamo, std::size_t aCount, std::size_t& aIndex)
            {
                std::size_t best = aCount, length = 0u;
                for (std::size_t j = 0u; j < aCount; ++j)
                {
                    std::string_view const jamo{ aJamo[j] };
                    if (rest.starts_with(jamo) && (best == aCount || jamo.size() > length))
                    {
                        best = j;
                        length = jamo.size();
                    }
                }
                if (best == aCount)
                    return false;
                aIndex = best;
                rest.remove_prefix(length);
                return true;
            };
            std::size_t l = 0u, v = 0u, t = 0u;
            if (find(sJamoL, std::size(sJamoL), l) && find(sJamoV, std::size(sJamoV), v) && find(sJamoT, std::size(sJamoT), t) && rest.empty())
                return std::u32string(1u, static_cast<char32_t>(0xAC00u + (l * 21u + v) * 28u + t));
        }
        for (std::uint8_t p = 0u; p < std::size(sAlgorithmicPrefixes); ++p)
        {
            std::string const prefix = std::string{ sAlgorithmicPrefixes[p] } + "-";
            if (name.starts_with(prefix) && (name.size() == prefix.size() + 4u || name.size() == prefix.size() + 5u))
            {
                std::uint32_t cp = 0u;
                bool ok = true;
                for (auto c : std::string_view{ name }.substr(prefix.size()))
                {
                    if (c >= '0' && c <= '9') cp = cp * 16u + static_cast<std::uint32_t>(c - '0');
                    else if (c >= 'A' && c <= 'F') cp = cp * 16u + static_cast<std::uint32_t>(c - 'A' + 10);
                    else ok = false;
                }
                if (ok)
                    for (auto const& r : sAlgorithmicRanges)
                        if (r.prefix == p && cp >= r.first && cp <= r.last)
                            return std::u32string(1u, static_cast<char32_t>(cp));
            }
        }
        // (the names: a map made when it is first needed)
        static std::unordered_map<std::string, std::uint32_t> const sNames = []()
        {
            std::unordered_map<std::string, std::uint32_t> result;
            result.reserve(std::size(sNameCodePoints));
            std::string n;
            for (auto cp : sNameCodePoints)
                if (name_of(static_cast<char32_t>(cp), n))
                    result.emplace(n, cp);
            return result;
        }();
        if (auto const n = sNames.find(name); n != sNames.end())
            return std::u32string(1u, static_cast<char32_t>(n->second));
        if (aAliases)
            for (auto const& a : sAliases)
                if (name == a.name)
                    return std::u32string(1u, static_cast<char32_t>(a.codePoint));
        if (aSequences)
            for (auto const& s : sNamedSequences)
                if (name == s.name)
                {
                    std::u32string result;
                    for (std::uint8_t i = 0u; i < s.length; ++i)
                        result += static_cast<char32_t>(sNamedSequenceData[s.offset + i]);
                    return result;
                }
        return std::nullopt;
    }
}
''')
open(names_path, 'w', encoding='utf-8', newline='\n').write(''.join(out))

# --- character properties

CATEGORIES = ["Cn", "Lu", "Ll", "Lt", "Mn", "Mc", "Me", "Nd", "Nl", "No", "Zs", "Zl", "Zp", "Cc", "Cf", "Cs", "Co", "Cn", "Lm", "Lo", "Pc",
    "Pd", "Ps", "Pe", "Pi", "Pf", "Po", "Sm", "Sc", "Sk", "So"]
CATEGORIES = ["Cn", "Lu", "Ll", "Lt", "Mn", "Mc", "Me", "Nd", "Nl", "No", "Zs", "Zl", "Zp", "Cc", "Cf", "Cs", "Co", "Lm", "Lo", "Pc",
    "Pd", "Ps", "Pe", "Pi", "Pf", "Po", "Sm", "Sc", "Sk", "So"]
BIDIRECTIONAL = ["", "L", "LRE", "LRO", "R", "AL", "RLE", "RLO", "PDF", "EN", "ES", "ET", "AN", "CS", "NSM", "BN", "B", "S", "WS", "ON", "LRI",
    "RLI", "FSI", "PDI"]
EAST_ASIAN_WIDTH = ["F", "H", "W", "Na", "A", "N"]
ALPHA, DECIMAL, DIGIT, NUMERIC, LOWER, UPPER, TITLE, SPACE, LINEBREAK, PRINTABLE, CASED, CASE_IGNORABLE, XID_START, XID_CONTINUE, \
    EXTENDED_CASE, MIRRORED = [1 << i for i in range(16)]

numerics = []
extended = []
records = []
record_index = {}
cp_records = []

def mapping(c, full):
    return [ord(x) for x in full]

for cp in range(MAX):
    c = chr(cp)
    cat = U.category(c)
    flags = 0
    if c.isalpha(): flags |= ALPHA
    if c.isdecimal(): flags |= DECIMAL
    if c.isdigit(): flags |= DIGIT
    if c.isnumeric(): flags |= NUMERIC
    if c.islower(): flags |= LOWER
    if c.isupper(): flags |= UPPER
    if cat == 'Lt': flags |= TITLE
    if c.isspace(): flags |= SPACE
    if len(('a' + c + 'b').splitlines()) == 2: flags |= LINEBREAK
    if c.isprintable(): flags |= PRINTABLE
    cased = c.islower() or c.isupper() or cat == 'Lt'
    if cased: flags |= CASED
    if cased:
        ignorable = ('AΣ' + c).lower()[1] == 'ς'
    else:
        ignorable = ('A' + c + 'Σ').lower()[-1] == 'ς'
    if ignorable: flags |= CASE_IGNORABLE
    if c.isidentifier(): flags |= XID_START
    if ('a' + c).isidentifier(): flags |= XID_CONTINUE
    if U.mirrored(c): flags |= MIRRORED
    decimal = U.decimal(c, -1)
    digit = U.digit(c, -1)
    numeric = U.numeric(c, None)
    if numeric is None:
        ni = -1
    else:
        if numeric not in numerics:
            numerics.append(numeric)
        ni = numerics.index(numeric)
    up, lo, ti, fo = (mapping(c, f) for f in (c.upper(), c.lower(), c.title(), c.casefold()))
    if all(len(m) == 1 for m in (up, lo, ti, fo)):
        maps = tuple(m[0] - cp for m in (up, lo, ti, fo))
    else:
        flags |= EXTENDED_CASE
        def ext(m):
            # (shared runs)
            for i in range(len(extended) - len(m) + 1):
                if extended[i:i + len(m)] == m:
                    return i | (len(m) << 24)
            extended.extend(m)
            return (len(extended) - len(m)) | (len(m) << 24)
        maps = tuple(ext(m) for m in (up, lo, ti, fo))
    rec = (CATEGORIES.index(cat), U.combining(c), BIDIRECTIONAL.index(U.bidirectional(c)), EAST_ASIAN_WIDTH.index(U.east_asian_width(c)),
        flags, decimal, digit, ni) + maps
    if rec not in record_index:
        record_index[rec] = len(records)
        records.append(rec)
    cp_records.append(record_index[rec])

SHIFT = 7
blocks = []
block_index = {}
index1 = []
for start in range(0, MAX, 1 << SHIFT):
    block = tuple(cp_records[start:start + (1 << SHIFT)])
    if block not in block_index:
        block_index[block] = len(blocks)
        blocks.append(block)
    index1.append(block_index[block])
index2 = [r for b in blocks for r in b]
assert len(records) < 0x10000 and len(blocks) < 0x10000

# (decompositions)
TAGS = [""]
decomposition_cps = []
decomposition_offsets = []
decomposition_data = []
def decomposition_of(d):
    parts = d.split()
    tag = 0
    if parts and parts[0].startswith('<'):
        if parts[0] not in TAGS:
            TAGS.append(parts[0])
        tag = TAGS.index(parts[0])
        parts = parts[1:]
    return [tag] + [int(p, 16) for p in parts]
for cp in range(MAX):
    d = U.decomposition(chr(cp))
    if d:
        decomposition_cps.append(cp)
        decomposition_offsets.append(len(decomposition_data))
        decomposition_data += decomposition_of(d)
decomposition_offsets.append(len(decomposition_data))
# (canonical compositions: the pairs that NFC composes)
compositions = []
for cp in decomposition_cps:
    d = U.decomposition(chr(cp))
    if d.startswith('<'):
        continue
    parts = [int(p, 16) for p in d.split()]
    if len(parts) == 2 and U.normalize('NFC', chr(parts[0]) + chr(parts[1])) == chr(cp):
        compositions.append(((parts[0] << 21) | parts[1], cp))
compositions.sort()

# (UCD 3.2.0's differences: category, bidirectional, east_asian_width, mirrored, decimal, digit, numeric, combining, decomposition
# and whether it is assigned)
OLD = U.ucd_3_2_0
old_records = []
old_decompositions = {}
for cp in range(MAX):
    c = chr(cp)
    cur = (U.category(c), U.bidirectional(c), U.east_asian_width(c), U.mirrored(c), U.decimal(c, -1), U.digit(c, -1), U.numeric(c, None), U.combining(c))
    old = (OLD.category(c), OLD.bidirectional(c), OLD.east_asian_width(c), OLD.mirrored(c), OLD.decimal(c, -1), OLD.digit(c, -1), OLD.numeric(c, None), OLD.combining(c))
    if U.decomposition(c) != OLD.decomposition(c):
        old_decompositions[cp] = OLD.decomposition(c)
    if cur != old:
        n = old[6]
        if n is not None and n not in numerics:
            numerics.append(n)
        old_records.append((cp, CATEGORIES.index(old[0]), BIDIRECTIONAL.index(old[1]), EAST_ASIAN_WIDTH.index(old[2]), int(old[3]), old[4], old[5],
            -1 if n is None else numerics.index(n), old[7]))
# (names that 3.2.0 does not have: those of the characters it does not have (a check))
for cp in range(MAX):
    c = chr(cp)
    try:
        a = U.name(c)
    except ValueError:
        a = None
    try:
        b = OLD.name(c)
    except ValueError:
        b = None
    assert b is None or a == b, hex(cp)
    if (b is None) != (a is None or OLD.category(c) == 'Cn'):
        print('name difference', hex(cp), a, b, OLD.category(c))
old_decomposition_cps = sorted(old_decompositions)
old_decomposition_offsets = []
old_decomposition_data = []
for cp in old_decomposition_cps:
    old_decomposition_offsets.append(len(old_decomposition_data))
    d = old_decompositions[cp]
    old_decomposition_data += decomposition_of(d) if d else [0xFF]
old_decomposition_offsets.append(len(old_decomposition_data))

out = [HEADER.format(name='unicode_tables.hpp', notice=TABLES_NOTICE),
"""// The properties of Unicode's characters (UCD 15.1.0, as CPython 3.13's unicodedata and str have them), and UCD 3.2.0's
// differences (unicodedata.ucd_3_2_0); generated (make_unicode_tables.py)

#pragma once

#include <cstdint>

namespace neos::python_runtime::detail::unicode_tables
{
"""]
out.append("    inline constexpr char const* sCategories[] = { " + ", ".join('"%s"' % c for c in CATEGORIES) + " };\n")
out.append("    inline constexpr char const* sBidirectional[] = { " + ", ".join('"%s"' % c for c in BIDIRECTIONAL) + " };\n")
out.append("    inline constexpr char const* sEastAsianWidth[] = { " + ", ".join('"%s"' % c for c in EAST_ASIAN_WIDTH) + " };\n")
out.append("    inline constexpr char const* sDecompositionTags[] = { " + ", ".join('"%s"' % c for c in TAGS) + " };\n")
for name, value in zip(["ALPHA", "DECIMAL", "DIGIT", "NUMERIC", "LOWER", "UPPER", "TITLE", "SPACE", "LINEBREAK", "PRINTABLE", "CASED",
    "CASE_IGNORABLE", "XID_START", "XID_CONTINUE", "EXTENDED_CASE", "MIRRORED"], range(16)):
    out.append(f"    inline constexpr std::uint16_t {name} = 0x{1 << value:X}u;\n")
out.append("""    inline constexpr unsigned SHIFT = %du;
    // (a character's record: its category, combining class, bidirectional class, east asian width, flags, decimal and digit values
    // (-1: none), numeric value (an index of sNumerics: -1: none) and case mappings (upper, lower, title and casefold): the differences
    // of the code points, or (EXTENDED_CASE) offsets of sExtendedCase (the length in their top 8 bits))
    struct record
    {
        std::uint8_t category;
        std::uint8_t combining;
        std::uint8_t bidirectional;
        std::uint8_t eastAsianWidth;
        std::uint16_t flags;
        std::int8_t decimal;
        std::int8_t digit;
        std::int16_t numeric;
        std::int32_t upper;
        std::int32_t lower;
        std::int32_t title;
        std::int32_t fold;
    };
    inline constexpr record sRecords[] =
    {
""" % SHIFT)
for r in records:
    out.append("        { " + ", ".join(str(v) for v in r) + " },\n")
out.append("    };\n")
array(out, "std::uint16_t", "sIndex1", index1, 32)
array(out, "std::uint16_t", "sIndex2", index2, 32)
array(out, "std::uint32_t", "sExtendedCase", extended, 16)
out.append("    inline constexpr double sNumerics[] = { " + ", ".join(repr(float(n)) for n in numerics) + " };\n")
array(out, "std::uint32_t", "sDecompositionCodePoints", decomposition_cps, 16)
array(out, "std::uint32_t", "sDecompositionOffsets", decomposition_offsets, 16)
array(out, "std::uint32_t", "sDecompositionData", decomposition_data, 16)
array(out, "std::uint64_t", "sCompositionKeys", [k for k, _ in compositions], 8)
array(out, "std::uint32_t", "sCompositionValues", [v for _, v in compositions], 16)
out.append("""    // (UCD 3.2.0's differences (ranges of characters, in order): a character's category (0: unassigned), bidirectional class, east asian width, mirrored, decimal, digit,
    // numeric (an index of sNumerics) and combining class)
    struct old_record { std::uint32_t first; std::uint32_t last; std::uint8_t category; std::uint8_t bidirectional; std::uint8_t eastAsianWidth; std::uint8_t mirrored;
        std::int8_t decimal; std::int8_t digit; std::int16_t numeric; std::uint8_t combining; };
    inline constexpr old_record sOldRecords[] =
    {
""")
old_ranges = []
for r in old_records:
    if old_ranges and old_ranges[-1][1] + 1 == r[0] and old_ranges[-1][2:] == list(r[1:]):
        old_ranges[-1][1] = r[0]
    else:
        old_ranges.append([r[0], r[0]] + list(r[1:]))
for r in old_ranges:
    out.append("        { 0x%X, 0x%X, %s },\n" % (r[0], r[1], ", ".join(str(v) for v in r[2:])))
out.append("    };\n")
out.append("    // (UCD 3.2.0's different decompositions (0xFF: none))\n")
array(out, "std::uint32_t", "sOldDecompositionCodePoints", old_decomposition_cps, 16)
array(out, "std::uint32_t", "sOldDecompositionOffsets", old_decomposition_offsets, 16)
array(out, "std::uint32_t", "sOldDecompositionData", old_decomposition_data, 16)
out.append("}\n")
open(tables_path, 'w', encoding='utf-8', newline='\n').write(''.join(out))
print('records', len(records), 'blocks', len(blocks), 'extended', len(extended), 'numerics', len(numerics), 'decompositions', len(decomposition_cps),
    'compositions', len(compositions), 'old', len(old_records), len(old_ranges), 'old decompositions', len(old_decomposition_cps), 'tags', TAGS)
