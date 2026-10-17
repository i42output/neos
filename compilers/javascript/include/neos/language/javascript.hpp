/*
  javascript.hpp

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

// JavaScript's own constructs and operators: the JavaScript concepts (neos.javascript) emit them with
// i_compiler::emit_language_construct(Language, construct, source); only those concepts and the compiler's JavaScript
// code generation use them

#include <cstdint>

namespace neos::language::javascript
{
    // (the language as its schema names it)
    inline constexpr char Language[] = "javascript";

    enum class construct : std::uint32_t
    {
        Module,             ///< the end of the module
        Function,           ///< 'function': the function being declared has inferred parameter and return types
        Let,                ///< the kind of declaration that follows
        Const,
        Var,
        GlobalLet,          ///< (a top-level declaration declares global variables)
        GlobalConst,
        GlobalVar,
        Declarator,         ///< name [= value]
        Statement,          ///< expression statement
        EmptyStatement,
        GlobalStatement,    ///< a top-level statement (part of the entry point)
        If,
        While,
        DoWhile,
        For,
        ForTest,            ///< a for or while loop's test
        Return,
        Break,
        Continue,
        Assignment,         ///< name op= value
        PrefixIncrement,
        PrefixDecrement,
        PostfixIncrement,
        PostfixDecrement,
        Conditional,        ///< condition ? value : value
        Call,               ///< name(arguments)
        Member,             ///< .name or .name(arguments) of the preceding operand
        Index,              ///< [index] of the preceding operand
        Variable,
        TemplateBegin,      ///< the start of a template literal (template text and substitutions are appended)
        TemplateSubstitution,
        Switch,
        Case,               ///< case value: statements
        DefaultCase,        ///< default: statements
        // operators: values are numbers (f64), strings or booleans, converted as JavaScript converts them (e.g. a string 
        // plus a number is a string)
        Add,
        Subtract,
        Multiply,
        Divide,
        Remainder,
        Power,
        Equal,
        NotEqual,
        StrictEqual,
        StrictNotEqual,
        LessThan,
        GreaterThan,
        LessThanOrEqual,
        GreaterThanOrEqual,
        LogicalAnd,
        LogicalOr,
        LogicalNot,
        BitwiseAnd,
        BitwiseOr,
        BitwiseXor,
        BitwiseNot,
        ShiftLeft,
        ShiftRight,
        ShiftRightUnsigned,
        Negate,
        Plus,
        TypeOf
    };
}
