/*
  javascript.hpp

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
