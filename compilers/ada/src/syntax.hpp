/*
  syntax.hpp

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

// Ada: the syntax tree (built from the constructs that the schema's concepts emit) and the source's own pass (case folding and
// the units that a compilation's context clauses name)

#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include <neos/language/ada.hpp>

namespace neos::compilers::ada
{
    using language::ada::construct;

    struct entity;
    struct ada_type;
    struct source_file;

    // a syntax tree node: its construct, its source (in the compiler's copy of its file) and its children; analysis annotates it
    struct node
    {
        construct kind;
        std::string_view source;
        std::vector<node*> children;
        node* parent = nullptr;
        source_file const* file = nullptr;
        // (analysis)
        entity* ent = nullptr;              ///< (a name: the entity it denotes; a declaration: the (first) entity it declares)
        ada_type* type = nullptr;           ///< (an expression: its type, once resolved)
        std::uint32_t mark = 0u;            ///< (analysis's own marks)

        bool is(construct aKind) const { return kind == aKind; }
        node* child(std::size_t aIndex) const { return aIndex < children.size() ? children[aIndex] : nullptr; }
        std::size_t size() const { return children.size(); }
        std::string text() const { return std::string{ source }; }
        node* first(construct aKind) const { for (auto* c : children) if (c->kind == aKind) return c; return nullptr; }
        std::vector<node*> all(construct aKind) const { std::vector<node*> r; for (auto* c : children) if (c->kind == aKind) r.push_back(c); return r; }
        bool has_flag(std::string_view aFlag) const;    ///< (it has a flag construct (a keyword) with the text)
    };

    // a source file whose compilation units are compiled: the compiler's copy of its (case folded) source
    struct source_file
    {
        std::string path;
        std::string text;
        std::string original;                       ///< (as written: for literals' and messages' cases)
        std::uint32_t line_of(std::string_view aSource) const;
        std::uint32_t column_of(std::string_view aSource) const;
        std::string_view original_of(std::string_view aSource) const;   ///< (the same span of the source as written)
    };

    // the source's own pass: identifiers and reserved words folded to lower case (literals and comments as written)
    void fold_case(std::string& aSource);

    // the units that a source's context clauses name (and the parent units of subunits and of child units that it declares): their
    // names (lower case, e.g. "ada.text_io")
    std::vector<std::string> units_named(std::string_view aFoldedSource);

    // the names of the body stubs ("is separate") in a source (lower case)
    std::vector<std::string> stub_names(std::string_view aFoldedSource);

    // the text of an identifier as a name (lower case, without white space or a trailing ':')
    std::string name_text(std::string_view aSource);
}
