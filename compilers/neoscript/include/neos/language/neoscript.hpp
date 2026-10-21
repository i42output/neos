/*
  neoscript.hpp

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

#pragma once

// neoscript's constructs: the neoscript concepts (neos.neoscript) emit them with i_compiler::emit_language_construct(Language, 
// construct, source); the neoscript compiler (neoscript.ncp) compiles them (its types: structs, classes and interfaces, and 
// their members) with the compiler's own code generation of the rest of the language (functions, statements, expressions)

#include <cstdint>
#include <string_view>

namespace neos::language::neoscript
{
    // (the language as its schema names it)
    inline constexpr char Language[] = "neoscript";

#define NEOS_NEOSCRIPT_CONSTRUCTS(X) \
    X(Class, "neoscript.class") \
    X(Interface, "neoscript.interface") \
    X(ClassEnd, "neoscript.class.end") \
    X(Field, "neoscript.field") \
    X(AbstractMethod, "neoscript.method.abstract") \
    X(Alias, "neoscript.alias") \
    X(ObjectInitialize, "neoscript.initialize") \
    X(Declaration, "neoscript.declaration") \
    X(AutoInitialize, "neoscript.auto") \
    X(Assignment, "neoscript.assignment") \
    X(Call, "neoscript.call") \
    X(MethodCall, "neoscript.call.method") \
    X(Member, "neoscript.member") \
    X(Postfix, "neoscript.postfix")

    enum class construct : std::uint32_t
    {
#define NEOS_NEOSCRIPT_CONSTRUCT_ENUM(aConstruct, aName) aConstruct,
        NEOS_NEOSCRIPT_CONSTRUCTS(NEOS_NEOSCRIPT_CONSTRUCT_ENUM)
#undef NEOS_NEOSCRIPT_CONSTRUCT_ENUM
        COUNT
    };

    struct construct_info
    {
        construct id;
        std::string_view conceptName;   ///< (the concept's name)
        std::string_view name;          ///< (the construct's name)
    };

    inline constexpr construct_info Constructs[] =
    {
#define NEOS_NEOSCRIPT_CONSTRUCT_INFO(aConstruct, aName) { construct::aConstruct, aName, #aConstruct },
        NEOS_NEOSCRIPT_CONSTRUCTS(NEOS_NEOSCRIPT_CONSTRUCT_INFO)
#undef NEOS_NEOSCRIPT_CONSTRUCT_INFO
    };

    inline constexpr construct_info const& info(construct aConstruct)
    {
        return Constructs[static_cast<std::uint32_t>(aConstruct)];
    }

    // (the concept of a type that a program defines (a struct, class or interface) or an alias of one: an object reference)
    inline constexpr char TypeConcept[] = "neoscript.type";
}
