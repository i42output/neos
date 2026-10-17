/*
  python.hpp

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
    };
}
