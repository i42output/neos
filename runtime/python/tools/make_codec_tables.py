#!/usr/bin/env python3
# make_codec_tables.py: the Python runtime plugin's codec tables (CPython 3.13's encodings package): its aliases, its codecs'
# names and the decoding tables of its single-byte (charmap) codecs (the host Python must be 3.13):
#   make_codec_tables.py <codec_tables.hpp>
import sys, codecs, encodings, encodings.aliases, importlib, pkgutil

HEADER = """/*
  codec_tables.hpp

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

  Portions of this file are derived from CPython 3.13 (Lib/encodings: its aliases and its codecs' tables), Copyright (c)
  2001-2024 Python Software Foundation; All Rights Reserved. They are used under the Python Software Foundation License
  Version 2 (see runtime/python/LICENSE.PSF). Changes: generated as C++ tables for the neos Python runtime plugin
  (tools/make_codec_tables.py; see runtime/python/NOTICE).
*/

// CPython 3.13's encodings package: its aliases (encodings.aliases), its modules' codecs' names, and the decoding tables of its
// single-byte codecs (0xFFFE: undefined); generated (make_codec_tables.py)

#pragma once

#include <cstdint>

namespace neos::python_runtime::detail::codec_tables
{
"""
modules = sorted(m.name for m in pkgutil.iter_modules(encodings.__path__) if m.name not in ('aliases', 'mbcs', 'oem', 'cp65001'))
infos = {}
charmaps = {}
for m in modules:
    try:
        mod = importlib.import_module('encodings.' + m)
    except ImportError:
        continue
    info = mod.getregentry()
    infos[m] = info
    if hasattr(mod, 'decoding_table') or hasattr(mod, 'decoding_map'):
        table = []
        for b in range(256):
            try:
                table.append(ord(codecs.decode(bytes([b]), m)))
            except UnicodeDecodeError:
                table.append(0xFFFE)
        # (the encoder: the decoder's inverse (charmap_build), or the module's encoding_map)
        encoding = {}
        if hasattr(mod, 'encoding_table'):
            for cp in range(0x110000):
                pass
        charmaps[m] = table
# (each charmap codec's encoder: its module's encoding_map, or its decoding table's inverse (charmap_build: a character that several
# bytes decode to is the last's)), checked
encodings_of = {}
for m, table in charmaps.items():
    mod = importlib.import_module('encodings.' + m)
    if hasattr(mod, 'encoding_map'):
        enc = {k: v for k, v in mod.encoding_map.items() if v is not None}
    else:
        enc = {}
        for b, cp in enumerate(table):
            if cp != 0xFFFE:
                enc[cp] = b
    for cp, b in enc.items():
        assert codecs.encode(chr(cp), m) == bytes([b]), (m, hex(cp))
    for b, cp in enumerate(table):
        if cp != 0xFFFE and cp not in enc:
            try:
                codecs.encode(chr(cp), m)
                assert False, (m, hex(cp))
            except UnicodeEncodeError:
                pass
    encodings_of[m] = sorted(enc.items())
out = [HEADER]
out.append("    struct codec_module { char const* module; char const* name; bool text; };\n")
out.append("    inline constexpr codec_module sModules[] =\n    {\n")
for m in modules:
    if m in infos:
        out.append(f'        {{ "{m}", "{infos[m].name}", {"true" if infos[m]._is_text_encoding else "false"} }},\n')
out.append("    };\n")
out.append("    struct alias { char const* alias; char const* module; };\n")
out.append("    inline constexpr alias sAliases[] =\n    {\n")
for a, m in sorted(encodings.aliases.aliases.items()):
    out.append(f'        {{ "{a}", "{m}" }},\n')
out.append("    };\n")
out.append("    // (a single-byte codec: its decoding table and its encoding (characters and their bytes, in order: sEncodings[offset, offset + count)))\n")
offsets = {}
total = 0
for m in sorted(charmaps):
    offsets[m] = total
    total += len(encodings_of[m])
out.append("    struct charmap { char const* module; std::uint16_t table[256]; std::uint32_t offset; std::uint16_t count; };\n")
out.append("    inline constexpr charmap sCharmaps[] =\n    {\n")
for m, table in sorted(charmaps.items()):
    out.append(f'        {{ "{m}", {{\n')
    for i in range(0, 256, 16):
        out.append("            " + ", ".join("0x%04X" % v for v in table[i:i + 16]) + ",\n")
    out.append(f"        }}, {offsets[m]}, {len(encodings_of[m])} }},\n")
out.append("    };\n")
out.append("    struct encoding { std::uint32_t codePoint; std::uint8_t byte; };\n")
out.append("    inline constexpr encoding sEncodings[] =\n    {\n")
for m in sorted(charmaps):
    pairs = encodings_of[m]
    for i in range(0, len(pairs), 8):
        out.append("        " + " ".join("{ 0x%X, 0x%02X }," % p for p in pairs[i:i + 8]) + "\n")
out.append("    };\n}\n")
open(sys.argv[1], 'w', encoding='utf-8', newline='\n').write(''.join(out))
print(len(modules), len(infos), len(charmaps), len(encodings.aliases.aliases))
