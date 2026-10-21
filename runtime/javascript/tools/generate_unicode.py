#!/usr/bin/env python3
# generate_unicode.py
#
# Copyright (c) 2026 Leigh Johnston.  All Rights Reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#
#    * Redistributions of source code must retain the above copyright
#      notice, this list of conditions and the following disclaimer.
#
#    * Redistributions in binary form must reproduce the above copyright
#      notice, this list of conditions and the following disclaimer in the
#      documentation and/or other materials provided with the distribution.
#
#    * Neither the name of the copyright holder nor the names of its
#      contributors may be used to endorse or promote products derived from
#      this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
# IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
# THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
# PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
# CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
# EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
# PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
# PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
# LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
# NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
# SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#
# Generates runtime/javascript/src/unicode_data.cpp (the JavaScript runtime's Unicode character tables) from the Unicode Character
# Database (UCD) and the Unicode emoji data files.
#
# The input files are Unicode data files: Copyright (c) 1991-2025 Unicode, Inc., distributed under the Unicode License v3 (see
# runtime/javascript/NOTICE and https://www.unicode.org/license.txt). They are not part of the neos repository; get them from
#   https://www.unicode.org/Public/17.0.0/ucd/          (UnicodeData.txt, ..., extracted/, emoji/emoji-data.txt)
#   https://www.unicode.org/Public/17.0.0/emoji/        (emoji-sequences.txt, emoji-zwj-sequences.txt)
# (or the same files in the unicode-org/unicodetools repository: unicodetools/data/ucd/17.0.0 and unicodetools/data/emoji/17.0).
#
# Usage: generate_unicode.py <ucd-directory> <emoji-directory> [<output-file>]

import os
import re
import sys

UNICODE_VERSION = "17.0.0"
MAX_CP = 0x10FFFF

# ECMAScript: table-binary-unicode-properties (the property names and aliases RegExp property escapes accept; matching is exact)
BINARY_PROPERTIES = [
    ("ASCII",), ("ASCII_Hex_Digit", "AHex"), ("Alphabetic", "Alpha"), ("Any",), ("Assigned",), ("Bidi_Control", "Bidi_C"),
    ("Bidi_Mirrored", "Bidi_M"), ("Case_Ignorable", "CI"), ("Cased",), ("Changes_When_Casefolded", "CWCF"),
    ("Changes_When_Casemapped", "CWCM"), ("Changes_When_Lowercased", "CWL"), ("Changes_When_NFKC_Casefolded", "CWKCF"),
    ("Changes_When_Titlecased", "CWT"), ("Changes_When_Uppercased", "CWU"), ("Dash",), ("Default_Ignorable_Code_Point", "DI"),
    ("Deprecated", "Dep"), ("Diacritic", "Dia"), ("Emoji",), ("Emoji_Component", "EComp"), ("Emoji_Modifier", "EMod"),
    ("Emoji_Modifier_Base", "EBase"), ("Emoji_Presentation", "EPres"), ("Extended_Pictographic", "ExtPict"), ("Extender", "Ext"),
    ("Grapheme_Base", "Gr_Base"), ("Grapheme_Extend", "Gr_Ext"), ("Hex_Digit", "Hex"), ("IDS_Binary_Operator", "IDSB"),
    ("IDS_Trinary_Operator", "IDST"), ("ID_Continue", "IDC"), ("ID_Start", "IDS"), ("Ideographic", "Ideo"), ("Join_Control", "Join_C"),
    ("Logical_Order_Exception", "LOE"), ("Lowercase", "Lower"), ("Math",), ("Noncharacter_Code_Point", "NChar"),
    ("Pattern_Syntax", "Pat_Syn"), ("Pattern_White_Space", "Pat_WS"), ("Quotation_Mark", "QMark"), ("Radical",),
    ("Regional_Indicator", "RI"), ("Sentence_Terminal", "STerm"), ("Soft_Dotted", "SD"), ("Terminal_Punctuation", "Term"),
    ("Unified_Ideograph", "UIdeo"), ("Uppercase", "Upper"), ("Variation_Selector", "VS"), ("White_Space", "space"),
    ("XID_Continue", "XIDC"), ("XID_Start", "XIDS"),
]

# ECMAScript: table-binary-unicode-properties-of-strings
STRING_PROPERTIES = ["Basic_Emoji", "Emoji_Keycap_Sequence", "RGI_Emoji_Modifier_Sequence", "RGI_Emoji_Flag_Sequence",
    "RGI_Emoji_Tag_Sequence", "RGI_Emoji_ZWJ_Sequence"]     # (and RGI_Emoji: their union)

# ---------------------------------------------------------------------------------------------------------------------------------
# reading


def open_ucd(path):
    with open(path, encoding="utf-8") as f:
        text = f.read()
    return text


def check_version(path, text):
    head = text[:400]
    name = os.path.basename(path)[:-4]
    if name in ("emoji-data", "emoji-sequences", "emoji-zwj-sequences"):
        m = re.search(r"Version:\s*([0-9.]+)", text[:4000]) or re.search(r"Emoji Version ([0-9.]+)", text[:4000])
        if m is None or not UNICODE_VERSION.startswith(m.group(1)):
            sys.exit("%s: not emoji version %s" % (path, UNICODE_VERSION))
    elif name == "UnicodeData":
        return
    elif ("%s-%s.txt" % (name, UNICODE_VERSION)) not in head:
        sys.exit("%s: not Unicode %s" % (path, UNICODE_VERSION))


def data_lines(path):
    """yields the semicolon-separated fields (stripped, comment removed) of each data line"""
    text = open_ucd(path)
    check_version(path, text)
    for line in text.splitlines():
        line = line.split("#", 1)[0].strip()
        if line:
            yield [field.strip() for field in line.split(";")]


def code_points(field):
    """'0041' or '0041..005A' -> range(first, last + 1)"""
    if ".." in field:
        first, last = field.split("..")
        return range(int(first, 16), int(last, 16) + 1)
    cp = int(field, 16)
    return range(cp, cp + 1)


def read_property_file(path, wanted=None):
    """property file of 'cps ; Property' lines -> { property: set of code points }"""
    result = {}
    for fields in data_lines(path):
        name = fields[1]
        if wanted is not None and name not in wanted:
            continue
        result.setdefault(name, set()).update(code_points(fields[0]))
    return result


# ---------------------------------------------------------------------------------------------------------------------------------
# sets as sorted, disjoint ranges

def to_ranges(cps):
    result = []
    for cp in sorted(cps):
        if result and result[-1][1] + 1 == cp:
            result[-1][1] = cp
        else:
            result.append([cp, cp])
    return [tuple(r) for r in result]


def main():
    if len(sys.argv) < 3:
        sys.exit("usage: generate_unicode.py <ucd-directory> <emoji-directory> [<output-file>]")
    ucd = sys.argv[1]
    emoji_dir = sys.argv[2]
    here = os.path.dirname(os.path.abspath(__file__))
    output = sys.argv[3] if len(sys.argv) > 3 else os.path.join(here, "..", "src", "unicode_data.cpp")

    def u(name):
        return os.path.join(ucd, name)

    # -- UnicodeData.txt ----------------------------------------------------------------------------------------------------------
    gc_of = {}
    ccc_of = {}
    decomposition = {}      # cp -> (compat, [cps])
    simple_upper = {}
    simple_lower = {}
    pending_first = None
    for fields in data_lines(u("UnicodeData.txt")):
        cp = int(fields[0], 16)
        name = fields[1]
        if name.endswith(", First>"):
            pending_first = cp
            continue
        if name.endswith(", Last>"):
            cps = range(pending_first, cp + 1)
            pending_first = None
        else:
            cps = range(cp, cp + 1)
        for c in cps:
            gc_of[c] = fields[2]
            if fields[3] != "0":
                ccc_of[c] = int(fields[3])
        if fields[5]:
            parts = fields[5].split()
            compat = parts[0].startswith("<")
            if compat:
                parts = parts[1:]
            decomposition[cp] = (compat, [int(p, 16) for p in parts])
        if fields[12]:
            simple_upper[cp] = int(fields[12], 16)
        if fields[13]:
            simple_lower[cp] = int(fields[13], 16)

    # -- General_Category ---------------------------------------------------------------------------------------------------------
    gc_sets = {}
    for cp in range(MAX_CP + 1):
        gc_sets.setdefault(gc_of.get(cp, "Cn"), set()).add(cp)
    # cross-check with DerivedGeneralCategory.txt
    derived_gc = read_property_file(u("extracted/DerivedGeneralCategory.txt"))
    for value, cps in derived_gc.items():
        if cps != gc_sets.get(value, set()):
            sys.exit("General_Category %s: UnicodeData.txt and DerivedGeneralCategory.txt disagree" % value)

    # -- property value aliases ---------------------------------------------------------------------------------------------------
    gc_values = []      # (short, [names...], member short values)
    sc_values = []      # (short, [names...])
    pva_path = u("PropertyValueAliases.txt")
    pva_text = open_ucd(pva_path)
    check_version(pva_path, pva_text)
    for line in pva_text.splitlines():
        if line.startswith("gc ") or line.startswith("sc "):
            body, _, comment = line.partition("#")
            fields = [f.strip() for f in body.split(";")]
            names = [f for f in fields[1:] if f]
            if fields[0] == "gc":
                members = [m.strip() for m in comment.split("|")] if comment.strip() else [names[0]]
                gc_values.append((names[0], names, members))
            else:
                sc_values.append((names[0], names))

    # -- Script and Script_Extensions ---------------------------------------------------------------------------------------------
    long_script = {short: names[1] for short, names in sc_values}
    short_script = {names[1]: short for short, names in sc_values}
    scripts = read_property_file(u("Scripts.txt"))
    sc_sets = {short: set() for short, _ in sc_values}
    assigned_script = set()
    for long_name, cps in scripts.items():
        sc_sets[short_script[long_name]] |= cps
        assigned_script |= cps
    sc_sets["Zzzz"] = set(range(MAX_CP + 1)) - assigned_script
    sc_of = {}
    for short, cps in sc_sets.items():
        for cp in cps:
            sc_of[cp] = short
    scx_of = {}
    for fields in data_lines(u("ScriptExtensions.txt")):
        for cp in code_points(fields[0]):
            scx_of[cp] = fields[1].split()
    scx_sets = {short: set(cps) for short, cps in sc_sets.items()}
    for cp, values in scx_of.items():
        scx_sets[sc_of[cp]].discard(cp)
    for cp, values in scx_of.items():
        for value in values:
            scx_sets[value].add(cp)

    # -- binary properties --------------------------------------------------------------------------------------------------------
    binary = {}
    binary.update(read_property_file(u("PropList.txt")))
    binary.update(read_property_file(u("DerivedCoreProperties.txt")))
    binary.update(read_property_file(u("extracted/DerivedBinaryProperties.txt")))
    binary.update(read_property_file(u("emoji/emoji-data.txt")))
    normalization_props = read_property_file(u("DerivedNormalizationProps.txt"), {"Changes_When_NFKC_Casefolded", "Full_Composition_Exclusion"})
    binary.update(normalization_props)
    binary["ASCII"] = set(range(0x80))
    binary["Any"] = set(range(MAX_CP + 1))
    binary["Assigned"] = set(range(MAX_CP + 1)) - gc_sets["Cn"]
    for names in BINARY_PROPERTIES:
        if names[0] not in binary:
            sys.exit("binary property %s: not found" % names[0])

    # -- properties of strings ----------------------------------------------------------------------------------------------------
    string_props = {name: ([], set()) for name in STRING_PROPERTIES}
    for file in ("emoji-sequences.txt", "emoji-zwj-sequences.txt"):
        for fields in data_lines(os.path.join(emoji_dir, file)):
            strings, singles = string_props[fields[1]]
            if ".." in fields[0]:
                singles.update(code_points(fields[0]))
            else:
                seq = [int(p, 16) for p in fields[0].split()]
                if len(seq) == 1:
                    singles.add(seq[0])
                else:
                    strings.append(tuple(seq))

    # -- case mapping -------------------------------------------------------------------------------------------------------------
    fold = {}
    for fields in data_lines(u("CaseFolding.txt")):
        if fields[1] in ("C", "S"):
            fold[int(fields[0], 16)] = int(fields[2], 16)
    special_upper = {}
    special_lower = {}
    for fields in data_lines(u("SpecialCasing.txt")):
        if len(fields) > 4 and fields[4]:
            continue    # (conditional: Final_Sigma is implemented in code; language-sensitive mappings are not used)
        cp = int(fields[0], 16)
        lower = [int(p, 16) for p in fields[1].split()]
        upper = [int(p, 16) for p in fields[3].split()]
        if lower != [cp] and lower != [simple_lower.get(cp, cp)]:
            special_lower[cp] = lower
        if upper != [cp] and upper != [simple_upper.get(cp, cp)]:
            special_upper[cp] = upper
    for table in (special_lower, special_upper):
        for seq in table.values():
            if len(seq) > 3:
                sys.exit("SpecialCasing.txt: a mapping longer than three code points")

    # -- normalization ------------------------------------------------------------------------------------------------------------
    full_composition_exclusion = normalization_props["Full_Composition_Exclusion"]

    def full_decomposition(cp, compat):
        if 0xAC00 <= cp <= 0xD7A3:     # (Hangul syllables: algorithmic)
            index = cp - 0xAC00
            result = [0x1100 + index // 588, 0x1161 + (index % 588) // 28]
            if index % 28:
                result.append(0x11A7 + index % 28)
            return result
        entry = decomposition.get(cp)
        if entry is None or (entry[0] and not compat):
            return [cp]
        result = []
        for c in entry[1]:
            result.extend(full_decomposition(c, compat))
        return result

    canonical = {}
    compatibility = {}
    for cp in sorted(decomposition):
        canonical_full = full_decomposition(cp, False)
        compat_full = full_decomposition(cp, True)
        if canonical_full != [cp]:
            canonical[cp] = canonical_full
        if compat_full != canonical_full:
            compatibility[cp] = compat_full
    compositions = []
    for cp, (compat, parts) in decomposition.items():
        if not compat and cp not in full_composition_exclusion:
            if len(parts) != 2:
                sys.exit("U+%04X: a primary composite whose decomposition is not a pair" % cp)
            compositions.append((parts[0], parts[1], cp))
    compositions.sort()

    # -- emit ---------------------------------------------------------------------------------------------------------------------
    out = Emitter()
    ranges_index = {}

    def slice_of(cps):
        rs = tuple(to_ranges(cps))
        if rs not in ranges_index:
            ranges_index[rs] = (out.range_count, len(rs))
            out.ranges.extend(rs)
            out.range_count += len(rs)
        return ranges_index[rs]

    binary_entries = []
    for names in BINARY_PROPERTIES:
        s = slice_of(binary[names[0]])
        for name in names:
            binary_entries.append((name, s))
    gc_entries = []
    for short, names, members in gc_values:
        cps = set()
        for m in members:
            cps |= gc_sets.get(m, set())
        s = slice_of(cps)
        for name in names:
            gc_entries.append((name, s))
    sc_entries = []
    scx_entries = []
    for short, names in sc_values:
        s = slice_of(sc_sets[short])
        sx = slice_of(scx_sets[short])
        for name in names:
            sc_entries.append((name, s))
            scx_entries.append((name, sx))
    # (internal sets used by unicode.cpp)
    zs_slice = slice_of(gc_sets["Zs"])
    cased_slice = slice_of(binary["Cased"])
    case_ignorable_slice = slice_of(binary["Case_Ignorable"])
    id_start_slice = slice_of(binary["ID_Start"])
    id_continue_slice = slice_of(binary["ID_Continue"])

    # strings: one array; RGI_Emoji is all of them
    strings = []
    string_entries = []
    for name in STRING_PROPERTIES:
        seqs, singles = string_props[name]
        offset = len(strings)
        strings.extend(seqs)
        string_entries.append((name, (offset, len(seqs)), slice_of(singles)))
    all_singles = set()
    for name in STRING_PROPERTIES:
        all_singles |= string_props[name][1]
    string_entries.append(("RGI_Emoji", (0, len(strings)), slice_of(all_singles)))

    lines = out.lines
    lines.append(HEADER)
    lines.append("namespace neos::javascript_runtime::unicode::data")
    lines.append("{")
    lines.append("    // property code point ranges (each property's ranges are a slice of this array)")
    lines.append("    range const sRanges[] =")
    lines.append("    {")
    out.items(["{0x%X,0x%X}" % r for r in out.ranges], 8)
    lines.append("    };")
    lines.append("    std::size_t const sRangeCount = %d;" % len(out.ranges))
    lines.append("")

    def named_table(table_name, entries, comment):
        lines.append("    // %s" % comment)
        lines.append("    named_slice const %s[] =" % table_name)
        lines.append("    {")
        out.items(['{u"%s",%d,%d}' % (name, s[0], s[1]) for name, s in entries], 4)
        lines.append("    };")
        lines.append("    std::size_t const %sCount = %d;" % (table_name, len(entries)))
        lines.append("")

    named_table("sBinaryProperties", binary_entries, "binary properties (ECMAScript table-binary-unicode-properties)")
    named_table("sGeneralCategoryValues", gc_entries, "General_Category values and aliases (PropertyValueAliases.txt)")
    named_table("sScriptValues", sc_entries, "Script values and aliases (PropertyValueAliases.txt)")
    named_table("sScriptExtensionsValues", scx_entries, "Script_Extensions values and aliases (PropertyValueAliases.txt)")
    for name, s in (("sWhiteSpaceZs", zs_slice), ("sCased", cased_slice), ("sCaseIgnorable", case_ignorable_slice),
            ("sIdStart", id_start_slice), ("sIdContinue", id_continue_slice)):
        lines.append("    slice const %s = { %d, %d };" % (name, s[0], s[1]))
    lines.append("")

    lines.append("    // properties of strings: their strings (sequences of more than one code point)")
    lines.append("    std::u32string_view const sStrings[] =")
    lines.append("    {")
    out.items(['U"%s"sv' % "".join("\\x%X" % c for c in seq) for seq in strings], 4)
    lines.append("    };")
    lines.append("    string_property const sStringProperties[] =")
    lines.append("    {")
    out.items(['{u"%s",{%d,%d},{%d,%d}}' % (name, st[0], st[1], sr[0], sr[1]) for name, st, sr in string_entries], 1)
    lines.append("    };")
    lines.append("    std::size_t const sStringPropertiesCount = %d;" % len(string_entries))
    lines.append("")

    def mapping_table(table_name, mapping, comment):
        lines.append("    // %s" % comment)
        lines.append("    mapping const %s[] =" % table_name)
        lines.append("    {")
        out.items(["{0x%X,0x%X}" % (k, mapping[k]) for k in sorted(mapping)], 8)
        lines.append("    };")
        lines.append("    std::size_t const %sCount = %d;" % (table_name, len(mapping)))
        lines.append("")

    mapping_table("sSimpleUppercase", simple_upper, "simple uppercase mappings (UnicodeData.txt)")
    mapping_table("sSimpleLowercase", simple_lower, "simple lowercase mappings (UnicodeData.txt)")
    mapping_table("sSimpleCaseFolding", fold, "simple case folding (CaseFolding.txt statuses C and S)")

    def special_table(table_name, mapping, comment):
        lines.append("    // %s" % comment)
        lines.append("    special_casing const %s[] =" % table_name)
        lines.append("    {")
        out.items(["{0x%X,{%s},%d}" % (k, ",".join("0x%X" % c for c in (mapping[k] + [0, 0, 0])[:3]), len(mapping[k]))
            for k in sorted(mapping)], 4)
        lines.append("    };")
        lines.append("    std::size_t const %sCount = %d;" % (table_name, len(mapping)))
        lines.append("")

    special_table("sSpecialUppercase", special_upper, "full uppercase mappings that differ from the simple ones (SpecialCasing.txt, unconditional)")
    special_table("sSpecialLowercase", special_lower, "full lowercase mappings that differ from the simple ones (SpecialCasing.txt, unconditional)")

    ccc_ranges = []
    for cp in sorted(ccc_of):
        if ccc_ranges and ccc_ranges[-1][1] + 1 == cp and ccc_ranges[-1][2] == ccc_of[cp]:
            ccc_ranges[-1][1] = cp
        else:
            ccc_ranges.append([cp, cp, ccc_of[cp]])
    lines.append("    // canonical combining classes (non-zero) (UnicodeData.txt)")
    lines.append("    combining_class const sCombiningClasses[] =")
    lines.append("    {")
    out.items(["{0x%X,0x%X,%d}" % tuple(r) for r in ccc_ranges], 8)
    lines.append("    };")
    lines.append("    std::size_t const sCombiningClassesCount = %d;" % len(ccc_ranges))
    lines.append("")

    decomposition_data = []
    data_index = {}

    def decomposition_table(table_name, mapping, comment):
        entries = []
        for cp in sorted(mapping):
            seq = tuple(mapping[cp])
            if seq not in data_index:
                data_index[seq] = len(decomposition_data)
                decomposition_data.extend(seq)
            entries.append("{0x%X,%d,%d}" % (cp, data_index[seq], len(seq)))
        lines.append("    // %s" % comment)
        lines.append("    decomposition const %s[] =" % table_name)
        lines.append("    {")
        out.items(entries, 6)
        lines.append("    };")
        lines.append("    std::size_t const %sCount = %d;" % (table_name, len(entries)))
        lines.append("")

    decomposition_table("sCanonicalDecompositions", canonical, "full canonical decompositions (UnicodeData.txt; excluding Hangul syllables)")
    decomposition_table("sCompatibilityDecompositions", compatibility,
        "full compatibility decompositions where they differ from the full canonical ones (UnicodeData.txt)")
    if len(decomposition_data) > 0xFFFF:
        sys.exit("decomposition data too large")
    lines.append("    char32_t const sDecompositionData[] =")
    lines.append("    {")
    out.items(["0x%X" % c for c in decomposition_data], 12)
    lines.append("    };")
    lines.append("")
    lines.append("    // primary composites, by (first, second) (UnicodeData.txt less Full_Composition_Exclusion; excluding Hangul syllables)")
    lines.append("    composition const sCompositions[] =")
    lines.append("    {")
    out.items(["{0x%X,0x%X,0x%X}" % c for c in compositions], 6)
    lines.append("    };")
    lines.append("    std::size_t const sCompositionsCount = %d;" % len(compositions))
    lines.append("}")

    with open(output, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines) + "\n")
    print("wrote %s: %d ranges, %d strings, %d decomposition code points, %d bytes" % (output, len(out.ranges), len(strings),
        len(decomposition_data), os.path.getsize(output)))


class Emitter:
    def __init__(self):
        self.lines = []
        self.ranges = []
        self.range_count = 0

    def items(self, items, per_line):
        for i in range(0, len(items), per_line):
            self.lines.append("        " + ", ".join(items[i:i + per_line]) + ",")


HEADER = """/*
  unicode_data.cpp

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

// GENERATED by runtime/javascript/tools/generate_unicode.py from the Unicode Character Database and emoji data, Unicode %s: do not
// edit.
//
// The tables below are derived from Unicode data files: Copyright (c) 1991-2025 Unicode, Inc. Distributed under the Unicode License
// v3 (see runtime/javascript/NOTICE and https://www.unicode.org/license.txt).

#include "unicode_data.hpp"

using namespace std::string_view_literals;
""" % UNICODE_VERSION


if __name__ == "__main__":
    main()
