/*
  compiler.hpp

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

// The Ada compiler plugin's language compiler: it builds a syntax tree from the Ada constructs that the schema's concepts emit
// (see neos/language/ada.hpp) for each source file of the program (the main file, the units that its context clauses name, their
// bodies and the units that those name, found in the main file's folder, the folders of ADA_INCLUDE_PATH and the language's
// packages folder), and when the last of them has been parsed it analyses the program's units (in an order in which each unit
// follows the units it depends on) and generates a WebAssembly function for each subprogram, and the entry point (the units'
// elaboration, then the main subprogram).

#pragma once

#include <deque>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>
#include <neos/language/language_compiler.hpp>
#include <neos/language/ada.hpp>
#include <neos/bytecode/opcodes.hpp>
#include "syntax.hpp"

namespace neos::compilers::ada
{
    using namespace neos::language;
    using bytecode::opcode;

    class program;

    // a compile error: the node (its position) and the message
    struct compile_error : std::runtime_error
    {
        compile_error(node const* aNode, std::string const& aMessage, bool aPrivatePartToo = false) : std::runtime_error{ aMessage }, where{ aNode }, private_part_too{ aPrivatePartToo } {}
        node const* where;
        bool private_part_too;      ///< (a rule that applies in the private part of an instance too (e.g. 3.4(5.1/3)))
    };

    class compiler : public language_compiler
    {
    public:
        compiler();
        ~compiler();
        // i_language_compiler
    public:
        void begin_compilation() override;
        void begin_pass() override;
        void begin_fragment(i_source_fragment& aFragment) override;
        void end_fragment() override;
        bool parsable_source(neolib::i_string_view const& aSource, neolib::i_string& aResult, neolib::i_vector<std::uint32_t>& aPositions) const override;
        void emit_construct(std::uint32_t aConstruct, neolib::i_string_view const& aSource) override;
        bool frame_name(std::uint32_t aFunction, neolib::i_string& aName) override;
        bool linked_function(neolib::i_string_view const& aName) const override;
        void diagnose(i_source_fragment const& aFragment, std::uint32_t& aLine, std::uint32_t& aColumn, std::uint32_t& aEndLine, 
            std::uint32_t& aEndColumn, neolib::i_string& aType, neolib::i_string& aMessage, neolib::i_string& aReport) override;
    public:
        i_code_generator& gen() { return generator(); }
        std::map<std::uint32_t, std::string>& frame_names() { return iFrameNames; }
        std::set<std::string>& linked_functions() { return iLinkedFunctions; }
        std::optional<std::string> const& packages_folder() const { return iPackagesFolder; }
        static constexpr std::string_view ConfigurationUnit = "neos_configuration_pragmas";  ///< (the unit of the configuration pragmas' file)
        void emit_entry(fragment const& aStatement) { push_operand(operand_type{ aStatement }); generator().end_indented(); }   ///< (the entry point's code)
    private:
        // the syntax tree
        node* make_node(construct aKind, std::string_view aSource);
        void build(construct aKind, std::string_view aSource);
        void compilation_parsed(node* aCompilation);
        void dump(node* aNode, int aDepth = 0) const;
        // the program's source files
        void find_units(std::string const& aMainFile, std::string const& aMainSource);
        std::optional<std::string> unit_file(std::string const& aUnit, bool aBody) const;
        void generate();
        void report(compile_error const& aError);
    private:
        struct fragment_state
        {
            std::string_view base;          ///< (the fragment's source, as the parser has it)
            source_file* file = nullptr;    ///< (the compiler's copy)
        };
        std::vector<fragment_state> iFragments;
        std::deque<node> iNodes;
        std::vector<node*> iStack;
        std::deque<source_file> iFiles;
        std::vector<node*> iCompilations;           ///< (the files' compilations, in the order parsed)
        std::vector<std::string> iSearchPath;       ///< (the folders that units' files are found in)
        std::optional<std::string> iPackagesFolder; ///< (the language's packages)
        std::set<std::string> iImported;
        std::size_t iExpected = 0u;                 ///< (the compilations (files) of the program: generated when all have been parsed)
        bool iMainSeen = false;
        std::map<std::uint32_t, std::string> iFrameNames;
        std::set<std::string> iLinkedFunctions;     ///< (the functions that the code of a linked program's other translation units can 
                                                    ///< call by their names: exported subprograms and objects (see program::exports))
        std::unique_ptr<program> iProgram;
        struct pending_error
        {
            std::string file;
            std::uint32_t line = 0u;
            std::uint32_t column = 0u;
            std::string message;
        };
        std::optional<pending_error> iPendingError;
        std::optional<std::uint32_t> iSyntaxErrorLine;     ///< (the first syntax error's line)
        source_file const* iSyntaxErrorFile = nullptr;      ///< (... in this file)
    };
}
