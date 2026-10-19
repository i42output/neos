/*
  python.hpp

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

// Python's own constructs and operators: the Python concepts (neos.python) emit them with
// i_compiler::emit_language_construct(Language, construct, source); only those concepts and the compiler's Python code
// generation use them

#include <cstdint>

namespace neos::language::python
{
    // (the language as its schema names it)
    inline constexpr char Language[] = "python";

    enum class construct : std::uint32_t
    {
        Statement,          ///< (a simple statement: its temporaries are released)
        Condition,          ///< (the condition of an if, elif or while header)
        Function,           ///< (the start of a function's body)
        Return,
        Break,
        Continue,
        Import,             ///< import a[.b] [as c]
        ImportFrom,         ///< from a import b [as c], ...
        Assign,             ///< targets = value
        AugmentedAssign,    ///< target op= value
        Target,             ///< (an assignment target)
        ForTargets,         ///< (a for loop's targets: bound before its body)
        ForIn,              ///< for targets in iterable:
        Conditional,        ///< a if condition else b
        Or,
        And,
        Not,
        LessEqual,
        GreaterEqual,
        Equal,
        NotEqual,
        Less,
        Greater,
        NotIn,
        In,
        IsNot,
        Is,
        BitOr,
        BitXor,
        BitAnd,
        LeftShift,
        RightShift,
        Add,
        Subtract,
        Multiply,
        MatMul,
        FloorDivide,
        Divide,
        Modulo,
        Negative,
        Positive,
        Invert,
        Power,
        Call,               ///< (arguments) of the preceding operand
        Subscript,          ///< [items] of the preceding operand
        Slice,              ///< [start]:[stop][:[step]]
        Attribute,          ///< .name of the preceding operand
        EmptyTuple,
        Tuple,
        List,
        Dict,
        Ellipsis,
        None,
        Builtin,            ///< int, float, str, bool, range or object as a value (e.g. called)
        ComprehensionStart, ///< (the bracket that a comprehension starts with)
        ComprehensionFor,   ///< for targets in iterable (of a comprehension)
        ComprehensionIf,    ///< if condition (of a comprehension)
        ListComprehension,  ///< [expression for ... if ...] (or a generator expression: a list)
        ParameterDefault,   ///< (the default value of a parameter: evaluated when the function is defined)
        Raise,              ///< raise [exception [from cause]]
        Assert,             ///< assert condition [, message]
        Imaginary,          ///< an imaginary literal (e.g. 3j)
        Global,             ///< global name, ...
        DictComprehension,  ///< {key: value for ... if ...}
        Set,                ///< {a, b, ...}
        SetComprehension,   ///< {element for ... if ...}
        Del,                ///< del targets
        Walrus,             ///< name := value
        AsName,             ///< (the name of except ... as name and with ... as name: bound before a body on the header's line)
        TargetGroup,        ///< (a parenthesized or bracketed target list: (a, b) of "(a, b), c = ...")
    };
}
