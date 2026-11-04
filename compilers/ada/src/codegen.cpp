/*
  codegen.cpp

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

// Ada: code generation (the program: its globals, static data, functions and entry point; a function: its frame, control
// flow, exceptions, declarations and statements). Expressions are in gen_expr.cpp.

#include <neolib/neolib.hpp>
#include <neolib/core/pair.hpp>
#include <algorithm>
#include <cstring>
#include <set>
#include <neos/language/emit.hpp>
#include <neos/bytecode/bytecode.hpp>
#include "../../c/include/neos/language/c_library.hpp"   // (the C library's functions: see function_generator::foreign_call)
#include "codegen.hpp"

namespace neos::compilers::ada
{
    using namespace emitter;

    namespace
    {

        std::string upper(std::string aText)
        {
            for (auto& c : aText)
                if (c >= 'a' && c <= 'z')
                    c = static_cast<char>(c - 'a' + 'A');
            return aText;
        }

        void put_padded_s32(std::vector<std::uint8_t>& aCode, std::int32_t aValue)
        {
            auto v = static_cast<std::uint32_t>(aValue);
            for (int i = 0; i < 4; ++i)
            {
                aCode.push_back(static_cast<std::uint8_t>((v & 0x7Fu) | 0x80u));
                v >>= 7u;
            }
            aCode.push_back(static_cast<std::uint8_t>(v & 0x7Fu));
        }

        void patch_padded_s32(std::vector<std::uint8_t>& aCode, std::size_t aAt, std::int32_t aValue)
        {
            auto v = static_cast<std::uint32_t>(aValue);
            for (int i = 0; i < 4; ++i)
            {
                aCode[aAt + static_cast<std::size_t>(i)] = static_cast<std::uint8_t>((v & 0x7Fu) | 0x80u);
                v >>= 7u;
            }
            aCode[aAt + 4u] = static_cast<std::uint8_t>(v & 0x7Fu);
        }

        std::string file_name(node const* aNode)
        {
            if (aNode == nullptr || aNode->file == nullptr)
                return "?";
            auto file = aNode->file->path;
            auto const slash = file.find_last_of("/\\");
            return slash != std::string::npos ? file.substr(slash + 1u) : file;
        }
    }

    // --- the program

    std::uint32_t program::global(std::string const& aName, type aType)
    {
        auto existing = iGlobals.find(aName);
        if (existing != iGlobals.end())
            return existing->second;
        gen().add_global(neolib::string{ aName }, aType);
        gen().function_global(neolib::string{ aName });
        auto const index = static_cast<std::uint32_t>(gen().global_count());
        iGlobals[aName] = index;
        note_global_type(index, aType);
        return index;
    }

    std::uint32_t program::runtime(std::string const& aName, std::vector<type> const& aParameters, type aResult)
    {
        auto const index = iCompiler.gen().runtime_function(neolib::string{ aName }, neolib::vector<type>{ aParameters.begin(), aParameters.end() }, aResult);
        note_function_type(index, aParameters, aResult);
        return index;
    }

    std::uint64_t program::object_alignment(entity* aObject)
    {
        // (an object's alignment, if specified (for it, or for its subtype or type); else 0)
        if (aObject->alignment_clause != 0u)
            return aObject->alignment_clause;
        return type_alignment_clause(aObject->type);
    }

    std::uint64_t program::type_alignment_clause(ada_type* aType)
    {
        for (auto* t = aType; t != nullptr; t = t->base_type() != t ? t->base_type() : nullptr)
        {
            if (t->alignment_clause)
                return *t->alignment_clause;
            if (t->view() != t && t->view()->alignment_clause)
                return *t->view()->alignment_clause;
        }
        return 0u;
    }

    std::uint64_t program::static_data(std::uint64_t aSize, std::uint64_t aAlignment, std::uint64_t aSkew)
    {
        iStaticSize = ((iStaticSize + aSkew + aAlignment - 1u) & ~(aAlignment - 1u)) - aSkew;
        auto const result = StaticBase + iStaticSize;
        iStaticSize += aSize == 0u ? 1u : aSize;
        return result;
    }

    std::uint64_t program::static_string(std::string const& aBytes)
    {
        auto const existing = iStaticStrings.find(aBytes);
        if (existing != iStaticStrings.end())
            return existing->second;
        auto const address = static_data(24u + aBytes.size(), 8u);
        std::string image(24u, '\0');
        std::int64_t const data = 24;
        std::int64_t const first = 1;
        std::int64_t const last = static_cast<std::int64_t>(aBytes.size());
        std::memcpy(image.data(), &data, 8u);
        std::memcpy(image.data() + 8, &first, 8u);
        std::memcpy(image.data() + 16, &last, 8u);
        image += aBytes;
        iStaticBytes.emplace_back(address, image);
        iStaticStrings[aBytes] = address;
        return address;
    }

    std::uint64_t program::static_bytes(std::string const& aBytes)
    {
        auto const address = static_data(aBytes.size(), 8u);
        iStaticBytes.emplace_back(address, aBytes);
        return address;
    }

    std::uint64_t program::size_descriptor(ada_type* aBase)
    {
        // (a dynamically sized record type: its fixed part's size, its dynamic components' count, then for each (as its record_size
        // takes them) its kind (0: an array: its descriptor's offset, its dimensions, its elements' size; 1: a record: the offset of its
        // offset, its type's descriptor), for the runtime's record_size (of a class-wide object, its tag's size (negated)))
        auto* base = aBase->view()->base_type()->view();
        if (auto const existing = iSizeDescriptors.find(base); existing != iSizeDescriptors.end())
            return existing->second;
        layout(base);
        std::vector<std::int64_t> words{ static_cast<std::int64_t>(base->size.value_or(8u)), 0 };
        for (auto const& c : base->components)
        {
            if (!c.dynamic || !c.offset || c.type == nullptr)
                continue;
            auto* ct = c.type->view();
            if (ct->kind != type_kind::Array)
                words.insert(words.end(), { 1, static_cast<std::int64_t>(*c.offset), 0,
                    static_cast<std::int64_t>(size_descriptor(ct->base_type())) });
            else
                words.insert(words.end(), { 0, static_cast<std::int64_t>(*c.offset), static_cast<std::int64_t>(ct->base_type()->view()->indexes.size()),
                    packed_bits(ct) != 0u ? -static_cast<std::int64_t>(packed_bits(ct)) : static_cast<std::int64_t>(component_size(ct)) });
            ++words[1];
        }
        std::string bytes(words.size() * 8u, '\0');
        std::memcpy(bytes.data(), words.data(), bytes.size());
        auto const address = static_bytes(bytes);
        iSizeDescriptors[base] = address;
        return address;
    }

    std::uint64_t program::enumeration_names(ada_type* aType)
    {
        auto* t = aType->view()->base_type()->view();
        auto existing = iEnumerationNames.find(t);
        if (existing != iEnumerationNames.end())
            return existing->second;
        std::string names;
        for (auto* literal : t->literals)
        {
            names += literal->name.front() == '\'' ? literal->name : upper(literal->name);
            names += '\0';
        }
        auto const address = static_bytes(names);
        iEnumerationNames[t] = address;
        return address;
    }

    void program::define_subprogram(entity* aSubprogram)
    {
        auto& sub = *aSubprogram->sub;
        if (sub.defined)
            return;
        function_signature signature;
        function_parameters parameters;
        {
            function_parameter p;
            p.set_parameter_type(type::I64);
            parameters.push_back(p);
        }
        function_generator probe{ *this, aSubprogram, 0u, sub.level };
        auto const add = [&](type aType)
        {
            function_parameter p;
            p.set_parameter_type(aType);
            parameters.push_back(p);
        };
        auto const form = subprogram_form(aSubprogram);
        if (form == subprogram_kind::ProtectedEntry)
        {
            // (a protected entry's body: the object, the parameters' block, the family index)
            add(type::I64);
            add(type::I64);
            add(type::I64);
        }
        else if (form == subprogram_kind::Barrier)
        {
            add(type::I64);     // (the object)
            add(type::I64);     // (the family index)
        }
        else
        {
            if (self_type(aSubprogram) != nullptr)
                add(type::I64);     // (the task or protected object)
            for (auto const& formal : sub.parameters)
                add(probe.by_copy(formal) ? probe.wasm_of(formal.type) : type::I64);
        }
        signature.set_parameters(parameters);
        auto const resultType = form == subprogram_kind::ProtectedEntry ? type::Void : form == subprogram_kind::Barrier ? type::I64 :
            sub.result != nullptr ? probe.wasm_of(sub.result) : type::Void;
        signature.set_return_type(resultType);
        auto const name = "%ada." + full_name(aSubprogram) + "." + std::to_string(iFunctionCount++);
        signature.set_name(neolib::string{ name });
        std::uint32_t line = 1u;
        if (aSubprogram->decl != nullptr && aSubprogram->decl->file != nullptr)
            line = aSubprogram->decl->file->line_of(aSubprogram->decl->source);
        sub.function_index = gen().define_function(neolib::string{ name }, signature, neolib::vector<type>{}, neolib::string{ aSubprogram->name }, line);
        {
            std::vector<type> types;
            for (auto const& p : parameters)
                types.push_back(p.parameter_type());
            note_function_type(sub.function_index, types, resultType);
        }
        iCompiler.frame_names()[sub.function_index] = aSubprogram->name;
        sub.defined = true;
    }

    std::uint32_t program::define_procedure(std::string const& aName)
    {
        function_signature signature;
        function_parameters parameters;
        function_parameter p;
        p.set_parameter_type(type::I64);
        parameters.push_back(p);
        signature.set_parameters(parameters);
        signature.set_return_type(type::Void);
        auto const name = "%ada." + aName + "." + std::to_string(iFunctionCount++);
        signature.set_name(neolib::string{ name });
        auto const index = gen().define_function(neolib::string{ name }, signature, neolib::vector<type>{}, neolib::string{}, 1u);
        note_function_type(index, { type::I64 }, type::Void);
        return index;
    }

    void program::exports()
    {
        // Export (B.1) in a linked program (see i_code_generator::linked): functions named by the link names, that the code of the
        // program's other translation units (e.g. C's) calls (see bytecode::LinksSection): a library level subprogram's calls it 
        // (its parameters as its own: C's (B.3(68-75)) for those of a subprogram of convention C); an object's (a library level 
        // one's, in memory), named '&' and the link name, has no parameters and its result is the object's address (the memories are 
        // one: see neos::language::ada::layout)
        if (!gen().linked())
            return;
        std::set<std::string> defined;
        for (auto* e : iExports)
        {
            if (e->link_name.empty() || e->link_name.find("::") != std::string::npos)
                continue;
            std::vector<std::uint8_t> code;
            function_signature signature;
            function_parameters parameters;
            std::vector<type> types;
            type result = type::Void;
            std::string name;
            if (e->sub != nullptr && e->sub->defined && !e->imported && e->sub->level <= 1 && self_type(e) == nullptr &&
                subprogram_form(e) == subprogram_kind::Normal)
            {
                name = e->link_name;
                function_generator probe{ *this, e, 0u, e->sub->level };
                for (auto const& formal : e->sub->parameters)
                    types.push_back(probe.by_copy(formal) ? probe.wasm_of(formal.type) : type::I64);
                result = e->sub->result != nullptr ? probe.wasm_of(e->sub->result) : type::Void;
                if (e->sub->result != nullptr && e->sub->result->view()->is_composite())
                    continue;
                put_op(code, opcode::I64Const);
                put_s64(code, 0);       // (the static link: none, at library level)
                for (std::size_t i = 0u; i < types.size(); ++i)
                {
                    put_op(code, opcode::LocalGet);
                    put_u32(code, static_cast<std::uint32_t>(i));
                }
                put_op(code, opcode::CallFunction);
            }
            else if (e->is_object() && (e->where == storage::Static || e->where == storage::IndirectStatic))
            {
                name = "&" + e->link_name;
                result = type::I64;
                put_op(code, opcode::I64Const);
                put_s64(code, e->offset);
                if (e->where == storage::IndirectStatic)
                {
                    put_op(code, opcode::I64LoadMem);
                    put_u32(code, 0x43u);
                    put_u32(code, language::ada::layout::Memory);
                    put_u32(code, 0u);
                }
            }
            else
                continue;
            if (!defined.insert(name).second)
                continue;
            for (auto t : types)
            {
                function_parameter p;
                p.set_parameter_type(t);
                parameters.push_back(p);
            }
            signature.set_parameters(parameters);
            signature.set_return_type(result);
            signature.set_name(neolib::string{ name });
            auto const index = gen().define_function(neolib::string{ name }, signature, neolib::vector<type>{}, neolib::string{ name }, 1u);
            note_function_type(index, types, result);
            fragment body;
            if (e->sub != nullptr)
            {
                body.fragmentRelocations.push_back(neolib::pair<std::uint32_t, std::uint32_t>{ static_cast<std::uint32_t>(code.size()), e->sub->function_index });
                code.resize(code.size() + 5u);
            }
            put_op(code, opcode::End);
            body.fragmentCode.as_std_vector() = code;
            gen().set_function_body(index, body);
            gen().set_function_locals(index, neolib::vector<type>{});
            iCompiler.linked_functions().insert(name);
        }
    }

    void program::generate_code()
    {
        iSpGlobal = global("%ada.sp", type::I64);
        iLimitGlobal = global("%ada.limit", type::I64);
        iExcGlobal = global("%ada.exc", type::I64);
        iDenotedGlobal = global("%ada.denoted", type::I64);
        // (the subprograms' functions)
        for (auto* s : iSubprogramsToGenerate)
            define_subprogram(s);
        // (the exported subprograms' bodies: generated even if the program does not call them)
        for (auto* e : iExports)
            if (gen().linked() && e->sub != nullptr && e->sub->body != nullptr && !e->imported && 
                std::find(iSubprogramsToGenerate.begin(), iSubprogramsToGenerate.end(), e) == iSubprogramsToGenerate.end())
            {
                define_subprogram(e);
                iSubprogramsToGenerate.push_back(e);
            }
        // (the library units' elaboration procedures)
        std::vector<std::uint32_t> elaborations;
        for (auto const& [unit, r] : iPackageElaborations)
            elaborations.push_back(define_procedure("elab." + full_name(unit->ent != nullptr ? unit->ent : iStandardPackage)));
        // (code: the elaborations (library level objects' storage), then the subprograms, outermost first (the storage of the
        // objects that nested subprograms refer to is decided by the subprograms that declare them))
        for (std::size_t i = 0u; i < iPackageElaborations.size(); ++i)
        {
            auto const& [unit, r] = iPackageElaborations[i];
            function_generator g{ *this, nullptr, elaborations[i], 1 };
            g.generate_elaboration(unit, r, unit->is(construct::PackageBody));
        }
        // (and those that generating them adds (e.g. an entry's implementation of an interface's operation, see tag_of))
        for (std::size_t count = 0u; count != iSubprogramsToGenerate.size();)
        {
            count = iSubprogramsToGenerate.size();
            auto ordered = iSubprogramsToGenerate;
            std::stable_sort(ordered.begin(), ordered.end(), [](entity* a, entity* b) { return a->sub->level < b->sub->level; });
            for (auto* s : ordered)
            {
                if (s->sub->generated)
                    continue;
                s->sub->generated = true;
                function_generator g{ *this, s, s->sub->function_index, s->sub->level };
                g.generate_subprogram();
            }
        }
        exports();
        // the entry point: the runtime initialized, static data, exceptions registered, the library units elaborated, then the
        // main subprogram called (an exception it does not handle is reported: the exit status is 1)
        gen().ensure_function();
        fragment statement;
        statement.fragmentKind = fragment_kind::Statement;
        auto& code = statement.fragmentCode.as_std_vector();
        auto const call_index = [&](std::uint32_t aFunction)
        {
            put_op(code, opcode::CallFunction);
            statement.fragmentRelocations.push_back(neolib::pair<std::uint32_t, std::uint32_t>{ static_cast<std::uint32_t>(code.size()), aFunction });
            code.resize(code.size() + 5u);
        };
        auto const call = [&](std::string const& aName, std::vector<type> const& aParameters, type aResult)
        {
            call_index(runtime("ada::" + aName, aParameters, aResult));
        };
        auto const exit_call = [&]() { call_index(runtime("neos::exit", { type::I32 }, type::Void)); };
        auto const i32const = [&](std::int32_t aValue) { put_op(code, opcode::I32Const); put_s64(code, aValue); };
        auto const i64const = [&](std::int64_t aValue) { put_op(code, opcode::I64Const); put_s64(code, aValue); };
        auto const strconst = [&](std::string const& aText) { i32const(static_cast<std::int32_t>(gen().string_literal(neolib::string{ aText }))); };
        auto const unhandled = [&]()
        {
            put_op(code, opcode::GlobalGet);
            put_u32(code, iExcGlobal);
            i64const(0);
            put_op(code, opcode::I64Ne);
            put_op(code, opcode::If);
            code.push_back(0x40u);
            call("unhandled", {}, type::I32);
            exit_call();
            put_op(code, opcode::End);
        };
        if (iTagTableCell != 0u)
        {
            // (the table of the program's tags (Ada.Tags.Internal_Tag): its count, then the tags; its address in its cell)
            std::string table(8u + 8u * iTagAddresses.size(), '\0');
            std::int64_t const count = static_cast<std::int64_t>(iTagAddresses.size());
            std::memcpy(table.data(), &count, 8u);
            for (std::size_t i = 0u; i < iTagAddresses.size(); ++i)
                std::memcpy(table.data() + 8u + 8u * i, &iTagAddresses[i], 8u);
            auto const tableAddress = static_cast<std::int64_t>(static_data(table.size(), 8u));
            iStaticBytes.emplace_back(static_cast<std::uint64_t>(tableAddress), table);
            std::string cell(8u, '\0');
            std::memcpy(cell.data(), &tableAddress, 8u);
            iStaticBytes.emplace_back(iTagTableCell, cell);
        }
        i64const(static_cast<std::int64_t>(iStaticSize + 16u));
        i32const(static_cast<std::int32_t>(iSpGlobal));
        i32const(static_cast<std::int32_t>(iLimitGlobal));
        i32const(static_cast<std::int32_t>(iExcGlobal));
        call("init", { type::I64, type::I32, type::I32, type::I32 }, type::I64);
        put_op(code, opcode::Drop);
        for (auto const& [address, bytes] : iStaticBytes)
        {
            static char const sHex[] = "0123456789abcdef";
            std::string hex;
            for (unsigned char c : bytes)
            {
                hex += sHex[c >> 4u];
                hex += sHex[c & 15u];
            }
            i64const(static_cast<std::int64_t>(address));
            strconst(hex);
            call("init_data", { type::I64, type::String }, type::Void);
        }
        for (auto* e : iExceptions)
        {
            i64const(static_cast<std::int64_t>(e->exception_id));
            strconst(upper(full_name(e)));
            call("register_exception", { type::I64, type::String }, type::Void);
        }
        // (the protected procedures that can be interrupt handlers (C.3.1): their functions)
        for (auto* handler : iInterruptHandlers)
            if (handler->sub != nullptr && handler->sub->defined)
            {
                put_op(code, opcode::I32Const);
                statement.fragmentRelocations.push_back(neolib::pair<std::uint32_t, std::uint32_t>{ static_cast<std::uint32_t>(code.size()), handler->sub->function_index });
                code.resize(code.size() + 5u);
                put_op(code, opcode::I64UConvertI32);
                call("interrupt_handler_register", { type::I64 }, type::Void);
            }
        // (the partition's tasking policies (D.3, D.4), the environment task's priority (the main subprogram's, D.1(14)))
        if (iCeilingLocking || iPriorityQueuing)
        {
            i64const(iCeilingLocking ? 1 : 0);
            i64const(iPriorityQueuing ? 1 : 0);
            call("tasking_policies", { type::I64, type::I64 }, type::Void);
        }
        if (iMain != nullptr && (iMain->priority != nullptr || iMain->interrupt_priority))
        {
            std::int64_t priority = 31;
            if (iMain->priority != nullptr)
                if (auto const v = evaluate(iMain->priority, iMain->inner != nullptr ? iMain->inner : iMain->scope); v.is_integer())
                    priority = v.integer().to_int64();
            i64const(1);
            i64const(priority);
            call("task_priority", { type::I64, type::I64 }, type::Void);
        }
        // (dispatch tables and subprograms' addresses: their functions' (relocated) indexes)
        for (auto const& [address, function] : iFunctionSlots)
        {
            i64const(static_cast<std::int64_t>(address));
            put_op(code, opcode::I32Const);
            statement.fragmentRelocations.push_back(neolib::pair<std::uint32_t, std::uint32_t>{ static_cast<std::uint32_t>(code.size()), function });
            code.resize(code.size() + 5u);
            put_op(code, opcode::I64UConvertI32);
            put_op(code, opcode::I64StoreMem);
            put_u32(code, 0x43u);
            put_u32(code, language::ada::layout::Memory);
            put_u32(code, 0u);
        }
        for (auto e : elaborations)
        {
            i64const(0);
            call_index(e);
            unhandled();
        }
        if (iMain != nullptr && iMain->sub != nullptr && iMain->sub->defined)
        {
            i64const(0);
            call_index(iMain->sub->function_index);
            // (the environment task's main subprogram complete (the task no longer callable, C.7.1(17/3)); then the library level's
            // tasks awaited and controlled objects finalized)
            call("environment_completed", {}, type::Void);
            if (has_masters())
            {
                i64const(0);
                call("fin_release", { type::I64 }, type::Void);
            }
            if (iMain->sub->result != nullptr)
            {
                unhandled();
                put_op(code, opcode::I32ConvertI64);
                exit_call();
            }
            unhandled();
        }
        call("exit_status", {}, type::I32);
        exit_call();
        iCompiler.emit_entry(statement);
    }

    // --- a function

    function_generator::function_generator(program& aProgram, entity* aSubprogram, std::uint32_t aIndex, int aLevel) :
        iProgram{ aProgram }, iSubprogram{ aSubprogram }, iIndex{ aIndex }, iLevel{ aLevel }
    {
    }

    void function_generator::op(opcode aOpcode) { put_op(iCode, aOpcode); }
    void function_generator::u32(std::uint32_t aValue) { put_u32(iCode, aValue); }
    void function_generator::i32(std::int32_t aValue) { op(opcode::I32Const); put_s64(iCode, aValue); }
    void function_generator::i64(std::int64_t aValue) { op(opcode::I64Const); put_s64(iCode, aValue); }
    void function_generator::f32(float aValue) { op(opcode::F32Const); put_raw(iCode, aValue); }
    void function_generator::f64(double aValue) { op(opcode::F64Const); put_raw(iCode, aValue); }
    void function_generator::get(std::uint32_t aLocal) { op(opcode::LocalGet); u32(aLocal); }
    void function_generator::set(std::uint32_t aLocal) { op(opcode::LocalSet); u32(aLocal); }
    void function_generator::tee(std::uint32_t aLocal) { op(opcode::LocalTee); u32(aLocal); }
    void function_generator::drop() { op(opcode::Drop); }

    std::uint32_t function_generator::local(type aType)
    {
        iLocals.push_back(aType);
        return static_cast<std::uint32_t>(iParameters.size() + iLocals.size() - 1u);
    }

    std::uint32_t function_generator::temporary(type aType)
    {
        for (auto i = iFreeTemporaries.begin(); i != iFreeTemporaries.end(); ++i)
            if (iLocals[*i - iParameters.size()] == aType)
            {
                auto const result = *i;
                iFreeTemporaries.erase(i);
                return result;
            }
        return local(aType);
    }

    void function_generator::release(std::uint32_t aLocal)
    {
        if (aLocal >= iParameters.size() && std::find(iFreeTemporaries.begin(), iFreeTemporaries.end(), aLocal) == iFreeTemporaries.end())
            iFreeTemporaries.push_back(aLocal);
    }

    void function_generator::call(std::uint32_t aFunction)
    {
        op(opcode::CallFunction);
        iRelocations.emplace_back(static_cast<std::uint32_t>(iCode.size()), aFunction);
        iCode.resize(iCode.size() + 5u);
    }

    void function_generator::call_indirect(std::vector<type> const& aParameters, type aResult)
    {
        auto const aSignature = iProgram.gen().indirect_signature(neolib::vector<type>{ aParameters.begin(), aParameters.end() }, aResult);
        iProgram.note_indirect_type(aSignature, aParameters, aResult);
        op(opcode::CallIndirect);
        iRelocations.emplace_back(static_cast<std::uint32_t>(iCode.size()), IndirectSignature | aSignature);
        iCode.resize(iCode.size() + 5u);
        iCode.push_back(0x00u);
    }

    void function_generator::function_index(std::uint32_t aFunction)
    {
        op(opcode::I32Const);
        iRelocations.emplace_back(static_cast<std::uint32_t>(iCode.size()), aFunction);
        iCode.resize(iCode.size() + 5u);
    }

    void function_generator::runtime(std::string const& aName, std::vector<type> const& aParameters, type aResult)
    {
        call(iProgram.runtime("ada::" + aName, aParameters, aResult));
    }

    void function_generator::string_constant(std::string const& aText)
    {
        i32(static_cast<std::int32_t>(iProgram.gen().string_literal(neolib::string{ aText })));
    }

    void function_generator::load(vkind aKind, std::uint64_t aSize, std::uint32_t aOffset, bool aSigned)
    {
        opcode o = opcode::I64LoadMem;
        std::uint32_t align = 3u;
        switch (aKind)
        {
        case vkind::F32:
            o = opcode::F32LoadMem;
            align = 2u;
            break;
        case vkind::F64:
            o = opcode::F64LoadMem;
            break;
        default:
            switch (aSize)
            {
            case 1u: o = aSigned ? opcode::I64LoadMem8S : opcode::I64LoadMem8U; align = 0u; break;
            case 2u: o = aSigned ? opcode::I64LoadMem16S : opcode::I64LoadMem16U; align = 1u; break;
            case 4u: o = aSigned ? opcode::I64LoadMem32S : opcode::I64LoadMem32U; align = 2u; break;
            default: break;
            }
            break;
        }
        op(o);
        u32(0x40u | align);
        u32(language::ada::layout::Memory);
        u32(aOffset);
    }

    void function_generator::store(vkind aKind, std::uint64_t aSize, std::uint32_t aOffset)
    {
        opcode o = opcode::I64StoreMem;
        std::uint32_t align = 3u;
        switch (aKind)
        {
        case vkind::F32:
            o = opcode::F32StoreMem;
            align = 2u;
            break;
        case vkind::F64:
            o = opcode::F64StoreMem;
            break;
        default:
            switch (aSize)
            {
            case 1u: o = opcode::I64StoreMem8; align = 0u; break;
            case 2u: o = opcode::I64StoreMem16; align = 1u; break;
            case 4u: o = opcode::I64StoreMem32; align = 2u; break;
            default: break;
            }
            break;
        }
        op(o);
        u32(0x40u | align);
        u32(language::ada::layout::Memory);
        u32(aOffset);
    }

    // --- control

    void function_generator::begin(control_kind aKind, std::optional<type> aResult, entity* aLoop)
    {
        // (a loop that can be exited: the finalization chain's mark at its start (objects registered in it are finalized by an exit))
        std::int64_t finMark = -1;
        if (aKind == control_kind::Breakable && iProgram.has_masters())
        {
            finMark = local(type::I64);
            runtime("fin_mark", {}, type::I64);
            set(static_cast<std::uint32_t>(finMark));
        }
        op(aKind == control_kind::Loop ? opcode::Loop : opcode::Block);
        iCode.push_back(aResult ? *wasm_type(*aResult) : 0x40u);
        iControls.push_back(control{ aKind, aLoop });
        iControls.back().finMark = finMark;
    }

    void function_generator::leave(std::size_t aControl)
    {
        for (auto i = iControls.size(); i-- > aControl;)
            if (iControls[i].onExit != nullptr)
                (*iControls[i].onExit)();
        if (iControls[aControl].finMark >= 0)
        {
            get(static_cast<std::uint32_t>(iControls[aControl].finMark));
            runtime("fin_release", { type::I64 }, type::Void);
            check();
        }
        branch(aControl);
    }

    void function_generator::begin_if(std::optional<type> aResult)
    {
        op(opcode::If);
        iCode.push_back(aResult ? *wasm_type(*aResult) : 0x40u);
        iControls.push_back(control{ control_kind::Block });
    }

    void function_generator::else_()
    {
        op(opcode::Else);
    }

    void function_generator::end()
    {
        op(opcode::End);
        iControls.pop_back();
    }

    void function_generator::branch(std::size_t aControl, bool aConditional)
    {
        op(aConditional ? opcode::BrIf : opcode::Br);
        u32(static_cast<std::uint32_t>(depth_of(aControl)));
    }

    std::size_t function_generator::handler_target() const
    {
        for (std::size_t i = iControls.size(); i-- > 0u;)
            if (iControls[i].kind == control_kind::Handler || iControls[i].kind == control_kind::Exit)
                return i;
        return 0u;
    }

    void function_generator::check()
    {
        op(opcode::GlobalGet);
        u32(iProgram.exc_global());
        i64(0);
        op(opcode::I64Ne);
        branch(handler_target(), true);
    }

    void function_generator::raise_exception(entity* aException, node const* aWhere)
    {
        auto const* where = aWhere != nullptr ? aWhere : iWhere;
        while (aException->renamed != nullptr)
            aException = aException->renamed;
        i64(static_cast<std::int64_t>(aException->exception_id));
        string_constant(file_name(where));
        i32(static_cast<std::int32_t>(where != nullptr && where->file != nullptr ? where->file->line_of(where->source) : 0u));
        runtime("raise_at", { type::I64, type::String, type::I32 }, type::Void);
        branch(handler_target());
    }

    void function_generator::raise_check(int aKind, node const* aWhere, std::string const& aWhat)
    {
        auto const* where = aWhere != nullptr ? aWhere : iWhere;
        i32(aKind);
        string_constant(file_name(where));
        i32(static_cast<std::int32_t>(where != nullptr && where->file != nullptr ? where->file->line_of(where->source) : 0u));
        string_constant(aWhat);
        runtime("raise_check", { type::I32, type::String, type::I32, type::String }, type::Void);
        branch(handler_target());
    }

    // --- values and types

    vkind function_generator::kind_of(ada_type* aType)
    {
        if (aType == nullptr)
            return vkind::None;
        auto* t = aType->view()->base_type()->view();
        if (t->kind == type_kind::Void)
            return vkind::None;
        if (t->kind == type_kind::Float || t->kind == type_kind::UniversalReal)
            return t->size && *t->size == 4u ? vkind::F32 : vkind::F64;
        if (t->kind == type_kind::UniversalFixed)
            return vkind::F64;      // (a value of universal_fixed: its value (a product or quotient of fixed point values))
        return vkind::I64;
    }

    type function_generator::wasm(vkind aKind)
    {
        switch (aKind)
        {
        case vkind::F32: return type::F32;
        case vkind::F64: return type::F64;
        case vkind::None: return type::Void;
        default: return type::I64;
        }
    }

    bool function_generator::by_copy(parameter_info const& aParameter)
    {
        if (aParameter.mode != parameter_mode::In && aParameter.mode != parameter_mode::Access)
            return false;
        auto* t = aParameter.type != nullptr ? aParameter.type->view() : nullptr;
        return t != nullptr && (t->is_scalar() || t->is_access());
    }

    std::uint64_t function_generator::size_of(ada_type* aType)
    {
        return iProgram.object_size(aType);
    }

    void function_generator::to_boolean()
    {
        op(opcode::I64UConvertI32);
    }

    bool function_generator::is_unsigned(ada_type* aType)
    {
        if (aType == nullptr)
            return false;
        auto* t = aType->view()->base_type()->view();
        return t->kind == type_kind::Modular || t->kind == type_kind::Enumeration;
    }

    void function_generator::finish(std::vector<type> const&)
    {
        if (iFrameSizePatch != 0u)
            patch_padded_s32(iCode, iFrameSizePatch, static_cast<std::int32_t>((iFrameSize + 15u) & ~std::uint64_t{ 15u }));
        fragment code;
        code.fragmentCode.as_std_vector() = iCode;
        for (auto const& r : iRelocations)
            code.fragmentRelocations.push_back(neolib::pair<std::uint32_t, std::uint32_t>{ r.first, r.second });
        validate(iSubprogram != nullptr ? iProgram.full_name(iSubprogram) : std::string{ "elaboration" });
        iProgram.gen().set_function_body(iIndex, code);
        iProgram.gen().set_function_locals(iIndex, neolib::vector<type>{ iLocals.begin(), iLocals.end() });
    }

    void function_generator::prologue()
    {
        // (the frame: the stack pointer advanced by its size (patched later); the stack limit checked)
        iFrame = local(type::I64);
        op(opcode::GlobalGet);
        u32(iProgram.sp_global());
        tee(iFrame);
        op(opcode::I32Const);
        iFrameSizePatch = iCode.size();
        put_padded_s32(iCode, 0);
        op(opcode::I64UConvertI32);
        op(opcode::I64Add);
        iStackTop = local(type::I64);
        tee(iStackTop);
        op(opcode::GlobalSet);
        u32(iProgram.sp_global());
        // (the static link: kept in the frame (offset 0) for nested subprograms)
        get(iFrame);
        get(StaticLinkLocal);
        store64(0u);
        // (the secondary stack's mark)
        iSsMark = local(type::I64);
        runtime("ss_mark", {}, type::I64);
        set(iSsMark);
        // (the finalization chain's length: the controlled objects of this master are finalized when it ends)
        if (iProgram.has_masters() && iSubprogram != nullptr)
        {
            iFinMark = local(type::I64);
            runtime("fin_mark", {}, type::I64);
            set(iFinMark);
        }
    }

    void function_generator::generate_subprogram()
    {
        auto& sub = *iSubprogram->sub;
        auto const form = iProgram.subprogram_form(iSubprogram);
        iParameters.push_back(type::I64);
        if (form == subprogram_kind::ProtectedEntry)
            iParameters.insert(iParameters.end(), { type::I64, type::I64, type::I64 });
        else if (form == subprogram_kind::Barrier)
            iParameters.insert(iParameters.end(), { type::I64, type::I64 });
        else
        {
            if (iProgram.self_type(iSubprogram) != nullptr)
                iParameters.push_back(type::I64);
            for (auto const& p : sub.parameters)
                iParameters.push_back(by_copy(p) ? wasm_of(p.type) : type::I64);
        }
        iWhere = sub.body != nullptr ? sub.body : iSubprogram->decl;
        prologue();
        // (a subprogram whose completion is elaborated later: called (e.g. through an access value) before then: Program_Error
        // (3.11(14)))
        if (form == subprogram_kind::Normal && iProgram.elaboration_checked(iSubprogram))
        {
            i64(static_cast<std::int64_t>(iProgram.elaboration_flag(iSubprogram)));
            load64(0u);
            op(opcode::I64Eqz);
            begin_if();
            raise_check(1, iWhere, "access before elaboration");
            end();
        }
        // (a dispatching call's denoted operation (if it has class-wide preconditions): read (and reset) at once)
        std::uint32_t denoted = 0u;
        if (form == subprogram_kind::Normal)
            for (auto* o : iProgram.contract_chain(iSubprogram))
                if (!o->sub->class_preconditions.empty() && denoted == 0u)
                {
                    denoted = local(type::I64);
                    op(opcode::GlobalGet);
                    u32(iProgram.denoted_global());
                    set(denoted);
                    i64(0);
                    op(opcode::GlobalSet);
                    u32(iProgram.denoted_global());
                }
        iHasResult = sub.result != nullptr || form == subprogram_kind::Barrier;
        if (form == subprogram_kind::ProtectedEntry)
            iHasResult = false;
        if (iHasResult)
            iResult = local(form == subprogram_kind::Barrier ? type::I64 : wasm_of(sub.result));
        // (a task or protected object: kept in the frame (offset 8) for nested subprograms)
        std::uint32_t firstFormal = 1u;
        if (form != subprogram_kind::Normal || iProgram.self_type(iSubprogram) != nullptr)
        {
            iSelf = 1u;
            iHasSelf = true;
            get(iFrame);
            get(iSelf);
            store64(8u);
            firstFormal = 2u;
        }
        if (form == subprogram_kind::Barrier)
        {
            // (the barrier's value (the family index: its parameter))
            auto* entry = sub.barrier_of;
            if (auto* index = entry->sub->family_index)
            {
                index->where = storage::Local;
                index->local = 2u;
                index->function_level = static_cast<std::uint32_t>(iLevel);
            }
            begin(control_kind::Exit);
            stack_check();
            condition(entry->sub->barrier);
            op(opcode::I64UConvertI32);
            set(iResult);
            end();
            epilogue();
            finish(iParameters);
            return;
        }
        if (form == subprogram_kind::ProtectedEntry)
        {
            // (the parameters: their addresses in the block; the family index)
            for (std::size_t i = 0u; i < sub.parameters.size(); ++i)
                if (auto* e = sub.parameters[i].ent)
                {
                    auto const a = local(type::I64);
                    get(2u);
                    load64(static_cast<std::uint32_t>(8u * i));
                    set(a);
                    if (sub.parameters[i].mode != parameter_mode::In && discriminated_record(sub.parameters[i].type))
                        unconstrained_flag(e, a);
                    e->function_level = static_cast<std::uint32_t>(iLevel);
                    e->where = storage::Indirect;
                    e->local = a;
                }
            if (auto* index = sub.family_index)
            {
                index->where = storage::Local;
                index->local = 3u;
                index->function_level = static_cast<std::uint32_t>(iLevel);
            }
        }
        // (the parameters' storage)
        for (std::size_t i = 0u; i < sub.parameters.size() && form != subprogram_kind::ProtectedEntry; ++i)
        {
            auto const& p = sub.parameters[i];
            auto* e = p.ent;
            if (e == nullptr)
                continue;
            auto const param = static_cast<std::uint32_t>(i + firstFormal);
            e->function_level = static_cast<std::uint32_t>(iLevel);
            if (p.mode == parameter_mode::Access && p.type != nullptr && p.type->view()->kind == type_kind::Access && passes_levels(iSubprogram))
            {
                // (an access parameter: its actual's accessibility level (3.10.2(13/3)), passed in its value's bits LevelShift..)
                auto const level = local(type::I64);
                get(param);
                i64(LevelShift);
                op(opcode::I64ShrU);
                i64(0x3FFF);
                op(opcode::I64And);
                set(level);
                // (as seen from this subprogram: an object that exists before the call is not deeper than its body's master (so a
                // level deeper than this subprogram's body's (another master's, of the caller's): its body's level)
                auto const body = e->scope != nullptr ? iProgram.accessibility_level(e->scope) : 1;
                get(level);
                i64(body);
                get(level);
                i64(body);
                op(opcode::I64LtS);
                op(opcode::Select);
                set(level);
                get(param);
                i64((std::int64_t{ 1 } << LevelShift) - 1);
                op(opcode::I64And);
                set(param);
                iAccessLevels[e] = level;
            }
            if (by_copy(p))
            {
                if (e->captured || e->aliased)
                {
                    iFrameSize = (iFrameSize + 7u) & ~std::uint64_t{ 7u };
                    e->where = storage::Frame;
                    e->offset = static_cast<std::int64_t>(iFrameSize);
                    iFrameSize += 8u;
                    get(iFrame);
                    get(param);
                    store(kind_of(p.type), iProgram.scalar_size(p.type), static_cast<std::uint32_t>(e->offset));
                }
                else
                {
                    e->where = storage::Local;
                    e->local = param;
                }
            }
            else
            {
                if (p.mode != parameter_mode::In && discriminated_record(p.type) && !sub.import_name.size())
                {
                    // (whether the actual is unconstrained: its address's bit UnconstrainedBit)
                    unconstrained_flag(e, param);
                }
                if (e->captured)
                {
                    iFrameSize = (iFrameSize + 7u) & ~std::uint64_t{ 7u };
                    e->where = storage::IndirectFrame;
                    e->offset = static_cast<std::int64_t>(iFrameSize);
                    iFrameSize += 8u;
                    get(iFrame);
                    get(param);
                    store64(static_cast<std::uint32_t>(e->offset));
                }
                else
                {
                    e->where = storage::Indirect;
                    e->local = param;
                }
            }
        }
        // (its class-wide preconditions and postconditions: its own and those of the operations it overrides (6.1.1(18/4)))
        std::vector<entity*> const chain = form == subprogram_kind::Normal ? iProgram.contract_chain(iSubprogram) : std::vector<entity*>{};
        std::vector<node*> postconditions = sub.postconditions;
        for (auto* o : chain)
        {
            // (the parameters that the conditions name (the declarations', the overridden operations'): this body's)
            auto const map = [&](std::size_t aIndex, entity* aParameter)
            {
                auto* b = aIndex < sub.parameters.size() ? sub.parameters[aIndex].ent : nullptr;
                if (aParameter == nullptr || b == nullptr || aParameter == b)
                    return;
                aParameter->where = b->where;
                aParameter->local = b->local;
                aParameter->offset = b->offset;
                aParameter->function_level = b->function_level;
                aParameter->by_reference = b->by_reference;
                if (auto const level = iAccessLevels.find(b); level != iAccessLevels.end())
                    iAccessLevels[aParameter] = level->second;
            };
            for (std::size_t i = 0u; i < o->sub->spec_parameters.size(); ++i)
                map(i, o->sub->spec_parameters[i]);
            for (std::size_t i = 0u; i < o->sub->parameters.size(); ++i)
                map(i, o->sub->parameters[i].ent);
            postconditions.insert(postconditions.end(), o->sub->class_postconditions.begin(), o->sub->class_postconditions.end());
        }
        begin(control_kind::Exit);
        stack_check();
        // (an access before elaboration (see above): propagated)
        if (form == subprogram_kind::Normal && iProgram.elaboration_checked(iSubprogram))
            check();
        // (its preconditions (6.1.1(31/3)): the specific ones, then the class-wide ones (any of which holding suffices (6.1.1(32/3))),
        // then its postconditions' X'Old values (6.1.1(26/4)))
        if (form == subprogram_kind::Normal)
        {
            iContractType = sub.controlling != nullptr ? sub.controlling->view()->base_type()->view() : nullptr;
            for (auto* pre : sub.preconditions)
                assertion(pre, "failed precondition");
            if (denoted != 0u)
            {
                // (the class-wide ones of the denoted operation (the one a dispatching call names) and those it overrides (any
                // holding), else (a statically bound call) of this one and those it overrides)
                auto const active = local(type::I32);
                auto const evaluated = local(type::I32);
                auto const holds = local(type::I32);
                get(denoted);
                op(opcode::I64Eqz);
                set(active);
                i32(0);
                set(evaluated);
                i32(0);
                set(holds);
                node* first = nullptr;
                for (auto* o : chain)
                {
                    if (o->sub->controlling != nullptr)
                    {
                        get(active);
                        get(denoted);
                        i64(static_cast<std::int64_t>(iProgram.tag_of(o->sub->controlling->view()->base_type()->view())));
                        op(opcode::I64Eq);
                        op(opcode::I32Ior);
                        set(active);
                    }
                    for (auto* pre : o->sub->class_preconditions)
                    {
                        first = first != nullptr ? first : pre;
                        get(active);
                        get(holds);
                        op(opcode::I32Eqz);
                        op(opcode::I32And);
                        begin_if();
                        i32(1);
                        set(evaluated);
                        statement_condition(pre);
                        set(holds);
                        end();
                    }
                }
                get(evaluated);
                get(holds);
                op(opcode::I32Eqz);
                op(opcode::I32And);
                begin_if();
                failed_assertion(first, "failed precondition");
                end();
            }
            std::function<void(node*)> olds = [&](node* aNode)
            {
                if (aNode->is(construct::Attribute) && aNode->size() >= 2u && name_text(aNode->child(1)->source) == "old")
                {
                    auto* prefix = aNode->child(0);
                    auto* t = aNode->type != nullptr ? aNode->type : prefix->type;
                    expression(prefix, t);
                    auto const value = local(wasm_of(t));
                    set(value);
                    if (t != nullptr && t->view()->is_composite())
                    {
                        // (a tagged object's: all of it (of its tag's type, 6.1.1(26/4)))
                        auto* v = t->view();
                        copy_to_secondary(v->tagged && v->kind != type_kind::ClassWide && v->base_type()->view()->class_wide != nullptr ?
                            v->base_type()->view()->class_wide : t, value);
                        set(value);
                    }
                    release_temporaries();
                    iOldValues[aNode] = value;
                    return;
                }
                for (auto* c : aNode->children)
                    olds(c);
            };
            for (auto* post : postconditions)
                olds(post);
            iContractType = nullptr;
        }
        if (sub.is_null)
            ;
        else if (sub.expression != nullptr)
        {
            if (sub.expression->is(construct::Aggregate) || sub.expression->is(construct::AggregateBracket) || sub.expression->is(construct::AggregateNullRecord) ||
                sub.expression->is(construct::AggregateExtension))
                aggregate(sub.expression, sub.result);
            else
            {
                moved_result(sub.expression);
                expression(sub.expression->is(construct::Parenthesized) ? sub.expression->child(0) : sub.expression, sub.result);
            }
            return_value(sub.result, !is_temporary_value(sub.expression));
        }
        else if (sub.deep_type != nullptr)
        {
            // (a type's deep Adjust or Finalize: its controlled parts' (the object (its specific type's), the first parameter))
            auto const object = 1u;
            controlled_parts(sub.deep_type, object, sub.deep_finalize, [&](ada_type* aPartType, std::uint32_t aPart)
                {
                    controlled_call(aPartType, aPart, sub.deep_finalize ? "finalize" : "adjust");
                });
        }
        else if (sub.implementing_entry != nullptr && sub.implementing_entry->kind != entity_kind::Entry)
        {
            // (a protected subprogram implementing an interface's operation: called on the object (the first parameter), with the
            // others; a procedure's external call serves the entry calls whose barriers it opened)
            auto* target = sub.implementing_entry;
            get(1u);
            runtime("po_enter", { type::I64 }, type::Void);
            check();
            static_link_for(target);
            for (std::size_t i = 0u; i < sub.parameters.size(); ++i)
                get(static_cast<std::uint32_t>(i + 1u));
            call_function(target);
            get(1u);
            runtime(sub.result == nullptr ? "po_service" : "po_leave", { type::I64 }, type::Void);
            check();
            if (sub.result != nullptr)
                return_value(sub.result, false);
        }
        else if (sub.implementing_entry != nullptr)
        {
            // (an entry implementing an interface's operation: called on the object (the first parameter), with the others)
            auto const name = [&](parameter_info const& aParameter) -> node*
            {
                auto& n = iProgram.iClones.emplace_back();
                n.kind = construct::Identifier;
                n.ent = aParameter.ent;
                n.type = aParameter.type;
                if (sub.implementing_entry->decl != nullptr)
                {
                    n.source = sub.implementing_entry->decl->source;
                    n.file = sub.implementing_entry->decl->file;
                }
                set_meaning(&n, meaning::Object);
                return &n;
            };
            call_info info;
            info.callee = sub.implementing_entry;
            info.object = name(sub.parameters[0]);
            for (std::size_t i = 1u; i < sub.parameters.size(); ++i)
                info.actuals.push_back(name(sub.parameters[i]));
            entry_call(info.object, info, sub.implementing_entry, {}, nullptr, nullptr);
        }
        else if (sub.null_extension_result)
        {
            // (a null extension's inherited function with a controlling result: the parent's result, with the extension's tag)
            auto* parentOperation = sub.parent_operation;
            while (parentOperation->sub->inherited && parentOperation->sub->parent_operation != nullptr && !parentOperation->sub->null_extension_result)
                parentOperation = parentOperation->sub->parent_operation;
            auto* target = ultimate(parentOperation);
            static_link_for(target);
            for (std::size_t i = 0u; i < sub.parameters.size(); ++i)
                get(static_cast<std::uint32_t>(i + 1u));
            call_function(target);
            check();
            auto const value = temporary(type::I64);
            tee(value);
            i64(static_cast<std::int64_t>(iProgram.tag_of(sub.result->view()->base_type()->view())));
            store64(0u);
            get(value);
            release(value);
            return_value(sub.result, false);
        }
        else if (sub.body != nullptr && (sub.body->is(construct::RenamingSubprogram) || sub.prefixed_actual))
            renaming_body();
        else if (sub.body != nullptr && iSubprogram->imported && sub.op == intrinsic::None && !sub.body->is(construct::SubprogramBody))
        {
            // (a foreign subprogram (pragma or aspect Import of a convention other than neos or Intrinsic): of convention C, the
            // function of its external name (see foreign_call); else no program provides it (B.1(38)))
            if (!foreign_call())
                raise_check(1, sub.body, "imported subprogram " + (sub.external_name.empty() ? iSubprogram->name : sub.external_name) + " is not available");
        }
        else if (sub.body != nullptr)
        {
            if (sub.body->is(construct::TaskBody))
            {
                declarations(sub.body->first(construct::Declarations));
                runtime("task_activated", {}, type::Void);
            }
            else
                deferred_activation_declarations(sub.body->first(construct::Declarations));
            handled(sub.body->first(construct::Handled));
            // (a function whose body ends without a return statement: Program_Error (6.5(20)); a nonreturning procedure's
            // too (6.5.1(8/3)))
            if (sub.result != nullptr && form == subprogram_kind::Normal && sub.body->is(construct::SubprogramBody))
                raise_check(1, sub.body, "function ended without a return statement");
            else if (sub.no_return && form == subprogram_kind::Normal && sub.body->is(construct::SubprogramBody))
                raise_check(1, sub.body, "nonreturning procedure returned");
        }
        end();
        // (its postconditions (6.1.1(35/3)): checked when it returns normally)
        if (form == subprogram_kind::Normal && !postconditions.empty())
        {
            begin(control_kind::Exit);
            check();
            iContractType = sub.controlling != nullptr ? sub.controlling->view()->base_type()->view() : nullptr;
            for (auto* post : postconditions)
                assertion(post, "failed postcondition");
            iContractType = nullptr;
            end();
        }
        // (a boundary subprogram's access parameters and access result that designate objects of a type with invariants: those
        // objects checked when it returns normally (7.3.2(19/5)))
        if (form == subprogram_kind::Normal)
        {
            auto const checked = [&](ada_type* aType) -> ada_type*
            {
                if (aType == nullptr || aType->view()->kind != type_kind::Access)
                    return nullptr;
                auto* d = iProgram.designated_of(aType);
                if (d == nullptr || !d->view()->is_composite() || invariants_of(d).empty() || !boundary_of(iSubprogram, d))
                    return nullptr;
                return d;
            };
            std::vector<std::pair<entity*, ada_type*>> parameters;
            for (auto const& p : sub.parameters)
                if (auto* d = checked(p.type); d != nullptr && p.ent != nullptr)
                    parameters.emplace_back(p.ent, d);
            auto* resultDesignated = iHasResult ? checked(sub.result) : nullptr;
            if (!parameters.empty() || resultDesignated != nullptr)
            {
                begin(control_kind::Exit);
                check();
                auto const value = local(type::I64);
                auto const designated = [&](ada_type* aDesignated)
                {
                    get(value);
                    op(opcode::I64Eqz);
                    op(opcode::I32Eqz);
                    begin_if();
                    invariant_check(aDesignated, value, sub.body);
                    end();
                };
                for (auto const& [p, d] : parameters)
                {
                    load_object(p);
                    set(value);
                    designated(d);
                }
                if (resultDesignated != nullptr)
                {
                    get(iResult);
                    set(value);
                    designated(resultDesignated);
                }
                end();
            }
        }
        epilogue();
        finish(iParameters);
    }

    std::vector<entity*> program::contract_chain(entity* aOperation)
    {
        // (a primitive operation, then the operations of its ancestors that it overrides (or inherits): their declarations (an
        // inherited operation's: its parent's))
        std::vector<entity*> result;
        auto const add = [&](entity* aOp)
        {
            while (aOp->sub->inherited && aOp->sub->parent_operation != nullptr && aOp->sub->parent_operation->sub != nullptr)
                aOp = aOp->sub->parent_operation;
            if (std::find(result.begin(), result.end(), aOp) == result.end())
                result.push_back(aOp);
        };
        if (aOperation == nullptr || aOperation->sub == nullptr)
            return result;
        add(aOperation);
        auto const& sub = *aOperation->sub;
        // (the interfaces' operations it implements: theirs (of the same name and number of parameters))
        std::function<void(ada_type*)> interfaces = [&](ada_type* aType)
        {
            for (auto* i : aType->view()->base_type()->view()->interfaces)
            {
                auto* iv = i->view()->base_type()->view();
                for (auto* op : iv->primitives)
                    if (op != nullptr && op->sub != nullptr && op->name == aOperation->name && op->sub->parameters.size() == sub.parameters.size())
                        add(op);
                interfaces(iv);
            }
        };
        if (sub.controlling != nullptr && sub.slot >= 0)
        {
            interfaces(sub.controlling);
            for (auto* t = sub.controlling->view()->base_type()->view()->parent; t != nullptr; t = t->view()->base_type()->view()->parent)
            {
                auto* v = t->view()->base_type()->view();
                auto const slot = static_cast<std::size_t>(sub.slot);
                if (v->kind == type_kind::Interface)
                {
                    for (auto* op : v->primitives)
                        if (op != nullptr && op->sub != nullptr && op->name == aOperation->name && op->sub->parameters.size() == sub.parameters.size())
                            add(op);
                }
                else if (v->tagged && slot < v->slots.size() && v->slots[slot].operation != nullptr && v->slots[slot].operation->sub != nullptr)
                    add(v->slots[slot].operation);
                interfaces(v);
            }
        }
        return result;
    }

    void function_generator::renaming_body()
    {
        // (a renaming as body: a call of the renamed subprogram with the parameters)
        auto& sub = *iSubprogram->sub;
        if (sub.renamed_access != nullptr)
        {
            // (a renaming of an access-to-subprogram value's dereference: a call through the value (its descriptor: function index,
            // static link))
            auto const value = local(type::I64);
            load_object(sub.renamed_access);
            set(value);
            get(value);
            op(opcode::I64Eqz);
            begin_if();
            raise_check(0, iSubprogram->decl, "access check failed");
            end();
            get(value);
            load64(8u);
            std::vector<type> signature = { type::I64 };
            for (std::size_t i = 0u; i < sub.parameters.size(); ++i)
            {
                get(static_cast<std::uint32_t>(i + 1u));
                signature.push_back(by_copy(sub.parameters[i]) ? wasm_of(sub.parameters[i].type) : type::I64);
            }
            get(value);
            load64(0u);
            op(opcode::I32ConvertI64);
            call_indirect(signature, sub.result != nullptr ? wasm_of(sub.result) : type::Void);
            check();
            if (iHasResult)
                return_value(sub.result, false);
            return;
        }
        auto* target = ultimate(iSubprogram);
        if (auto const* info = sub.renames == nullptr && sub.renamed_name != nullptr ? iProgram.call_of(sub.renamed_name) : nullptr;
            info != nullptr && info->prefixed && info->object != nullptr && info->callee != nullptr && info->callee->sub != nullptr)
        {
            // (a prefixed view: the operation called with the object, then the parameters)
            auto* operation = ultimate(info->callee);
            auto* object = info->object;
            auto const& first = info->callee->sub->parameters[0];
            bool const accessObject = object->type != nullptr && object->type->view()->is_access();
            static_link_for(operation);
            if (first.type != nullptr && first.type->view()->is_access() && !accessObject)
                address(object);
            else if (by_copy(first))
                expression(object, first.type);
            else if (accessObject)
                expression(object, object->type);   // (its designated object's address)
            else
                address(object);
            for (std::size_t i = 0u; i < sub.parameters.size(); ++i)
                get(static_cast<std::uint32_t>(i + 1u));
            if (operation->sub->op != intrinsic::None && operation->sub->op != intrinsic::Import)
                intrinsic_values(operation, sub.result, iSubprogram->decl);
            else
                call_function(operation);
            if (iHasResult)
                return_value(sub.result, false);
            return;
        }
        if (target == nullptr || target == iSubprogram)
            return;
        std::vector<std::uint32_t> arguments;
        for (std::size_t i = 0u; i < sub.parameters.size(); ++i)
            arguments.push_back(static_cast<std::uint32_t>(i + 1u));
        if (target->sub != nullptr && target->sub->op != intrinsic::None && target->sub->op != intrinsic::Import)
        {
            for (auto a : arguments)
                get(a);
            intrinsic_values(target, sub.result, iSubprogram->decl);
        }
        else
        {
            static_link_for(target);
            for (auto a : arguments)
                get(a);
            call_function(target);
        }
        if (iHasResult)
            return_value(sub.result, false);    // (the renamed function's result: moved)
    }

    void function_generator::return_value(ada_type* aType, bool aAdjust)
    {
        // (the value on the stack: the result (a composite one copied to the secondary stack; its controlled parts adjusted unless
        // it is a new object))
        if (aType != nullptr && aType->view()->is_composite())
        {
            auto const value = temporary(type::I64);
            set(value);
            copy_to_secondary(aType, value);
            release(value);
            if (aAdjust && iProgram.needs_finalization(aType))
            {
                auto const result = local(type::I64);
                set(result);
                controlled_initialized(aType, result, false, true, false);
                get(result);
            }
        }
        set(iResult);
        branch(0u);
    }

    void function_generator::tag_link(node* aDecl)
    {
        // (a tagged type declared in a subprogram: its tag's static link (that of the primitive operations declared with it: this
        // frame), set when it is elaborated (the previous one restored when the subprogram returns))
        auto* e = aDecl->ent;
        if (e == nullptr || !e->is_type() || e->type == nullptr || e->scope == nullptr || e->scope->level <= 0 || iLevel <= 0)
            return;
        auto* t = e->type->view()->base_type()->view();
        if (!t->tagged || t->kind == type_kind::ClassWide || t->kind == type_kind::Interface)
            return;
        auto const tag = static_cast<std::int64_t>(iProgram.tag_of(t));
        auto const saved = local(type::I64);
        i64(tag);
        load64(static_cast<std::uint32_t>(program::TagLink));
        set(saved);
        i64(tag);
        frame_of_level(iLevel);
        store64(static_cast<std::uint32_t>(program::TagLink));
        iTagLinks.emplace_back(saved, tag);
    }

    void function_generator::stack_check()
    {
        // (the frame within the stack's limit and the call stack not near its end (neos::stack_low: a call from here could exhaust
        // it), else Storage_Error propagated (the frame is not entered: checked at the start of the subprogram's handled part, so that
        // its exit pops the frame))
        get(iStackTop);
        op(opcode::GlobalGet);
        u32(iProgram.limit_global());
        op(opcode::I64GtU);
        call(iProgram.runtime("neos::stack_low", {}, type::I32));
        op(opcode::I32Ior);
        begin_if();
        runtime("stack_overflow", {}, type::Void);
        branch(handler_target());
        end();
    }

    void function_generator::epilogue()
    {
        // (the static links of the tags of the tagged types declared here: restored)
        for (auto i = iTagLinks.rbegin(); i != iTagLinks.rend(); ++i)
        {
            i64(i->second);
            get(i->first);
            store64(static_cast<std::uint32_t>(program::TagLink));
        }
        // (the master's controlled objects finalized)
        if (iFinMark != 0u)
        {
            get(iFinMark);
            runtime("fin_release", { type::I64 }, type::Void);
        }
        // (the secondary stack: released to its mark at entry (a composite result is moved there); the frame popped)
        if (iHasResult && iSubprogram->sub->result->view()->is_composite())
        {
            // (unless an exception is propagated: then there's no result)
            op(opcode::GlobalGet);
            u32(iProgram.exc_global());
            i64(0);
            op(opcode::I64Ne);
            begin_if();
            get(iSsMark);
            runtime("ss_release", { type::I64 }, type::Void);
            else_();
            get(iSsMark);
            get(iResult);
            total_size(iSubprogram->sub->result, iResult);
            runtime("ss_return", { type::I64, type::I64, type::I64 }, type::I64);
            set(iResult);
            end();
        }
        else
        {
            get(iSsMark);
            runtime("ss_release", { type::I64 }, type::Void);
        }
        get(iFrame);
        op(opcode::GlobalSet);
        u32(iProgram.sp_global());
        if (iHasResult)
            get(iResult);
    }

    bool function_generator::foreign_call()
    {
        // an imported subprogram of convention C (B.3): a call of the function of its external name (else its name) that another 
        // translation unit of the program (e.g. C's) or a runtime plugin (e.g. the C library's) defines (see bytecode::LinkNamespace), 
        // its parameters passed as C's (B.3(68-75)): an elementary in parameter by value; an out or in out one, and a record, by 
        // reference (its address); an array, by its first component's address. Ada's memory and C's are one (the shared memory: see 
        // neos::language::ada::layout), so an address is the same object's in both. False: not of convention C, or its result is not
        // elementary
        auto& sub = *iSubprogram->sub;
        if (iSubprogram->convention != "c" || (sub.result != nullptr && sub.result->view()->is_composite()) || 
            iProgram.self_type(iSubprogram) != nullptr)
            return false;
        auto const name = sub.external_name.empty() ? iSubprogram->name : sub.external_name;
        // (a function of the C library (the C runtime's: its native functions, see c_library.hpp): called as the C library's (its
        // parameters' and result's types are the library's), as C's calls are; else the function of the name that another 
        // translation unit defines)
        struct library_function { std::string_view name; std::string_view signature; };
        static library_function const sLibrary[] = {
#define NEOS_ADA_C_LIBRARY_FUNCTION(aName, aSignature, aRestricted) { #aName, aSignature },
            NEOS_C_LIBRARY(NEOS_ADA_C_LIBRARY_FUNCTION)
#undef NEOS_ADA_C_LIBRARY_FUNCTION
        };
        std::string_view signature;
        for (auto const& f : sLibrary)
            if (f.name == name)
                signature = f.signature;
        auto const library_type = [](char aCode)
        {
            switch (aCode)
            {
            case 'I': return type::I32;
            case 'L': return type::I64;
            case 'F': return type::F32;
            case 'D': return type::F64;
            default: return type::Void;
            }
        };
        // (a value of one type as another: integers sign extended or wrapped, floating values converted)
        auto const convert = [&](type aFrom, type aTo)
        {
            if (aFrom == aTo || aTo == type::Void)
                return;
            if (aFrom == type::I32 && aTo == type::I64)
                op(opcode::I64SConvertI32);
            else if (aFrom == type::I64 && aTo == type::I32)
                op(opcode::I32ConvertI64);
            else if (aFrom == type::F32 && aTo == type::F64)
                op(opcode::F64ConvertF32);
            else if (aFrom == type::F64 && aTo == type::F32)
                op(opcode::F32ConvertF64);
        };
        bool const library = !signature.empty() && signature.size() == sub.parameters.size() + 2u;
        std::vector<type> parameters;
        for (std::size_t i = 0u; i < sub.parameters.size(); ++i)
        {
            auto const& p = sub.parameters[i];
            auto const parameter = static_cast<std::uint32_t>(i + 1u);
            get(parameter);
            auto t = type::I64;
            if (by_copy(p))
                t = wasm_of(p.type);
            else if (p.type != nullptr && p.type->view()->kind == type_kind::Array)
            {
                // (an array: its descriptor's data (the data offset is the descriptor's first word))
                get(parameter);
                load64(0u);
                op(opcode::I64Add);
            }
            if (library)
            {
                convert(t, library_type(signature[i + 2u]));
                t = library_type(signature[i + 2u]);
            }
            parameters.push_back(t);
        }
        auto const result = sub.result != nullptr ? wasm_of(sub.result) : type::Void;
        if (library)
        {
            auto const libraryResult = library_type(signature[0]);
            call(iProgram.runtime("c::libc." + name, parameters, libraryResult));
            if (sub.result != nullptr && libraryResult == type::Void)
                i64(0);
            else if (sub.result == nullptr && libraryResult != type::Void)
                op(opcode::Drop);
            else
                convert(libraryResult, result);
        }
        else
            call(iProgram.runtime(std::string{ bytecode::LinkNamespace } + "::" + name, parameters, result));
        if (sub.result != nullptr)
            return_value(sub.result, false);
        return true;
    }

    void function_generator::generate_elaboration(node* aUnit, region*, bool aBody)
    {
        iParameters.push_back(type::I64);
        iWhere = aUnit;
        prologue();
        begin(control_kind::Exit);
        stack_check();
        auto const created = iTasksCreated;
        // (a library instance's specification (or a library subprogram instance): its formal objects' values first (the
        // instantiation is the library unit))
        if (!aBody && aUnit->ent != nullptr && aUnit->ent->generic != nullptr)
            for (auto const& ic : iProgram.instance_constants())
                if (ic.instance == aUnit->ent)
                {
                    auto* c = ic.constant;
                    if (c->where == storage::Renaming && c->renamed_name != nullptr && !(is_simple_name(c->renamed_name) && c->renamed != nullptr))
                        renamed_address(c);
                    else if (c->where == storage::None)
                        initialize_object(c);
                }
        declarations(aUnit->first(construct::Declarations), aBody);
        if (!aBody)
        {
            if (auto* priv = aUnit->first(construct::Private))
                for (auto* d : priv->children)
                    declaration(d);
            if (iTasksCreated != created)
            {
                runtime("task_activate", {}, type::Void);
                check();
            }
        }
        else if (auto* h = aUnit->first(construct::Handled))
            handled(h);
        end();
        epilogue();
        finish(iParameters);
    }

    // --- objects

    bool function_generator::memory_resident(entity* aObject)
    {
        return aObject->where == storage::Frame || aObject->where == storage::Static || aObject->where == storage::Indirect ||
            aObject->where == storage::IndirectFrame || aObject->where == storage::IndirectStatic;
    }

    void function_generator::assign_storage(entity* aObject)
    {
        if (aObject->where != storage::None)
            return;
        auto* t = aObject->type;
        bool const composite = t != nullptr && t->view()->is_composite();
        bool const library = aObject->scope != nullptr && aObject->scope->level == 0 && iSubprogram == nullptr;
        aObject->function_level = static_cast<std::uint32_t>(iLevel);
        if (aObject->kind == entity_kind::Constant && aObject->value.known() && !aObject->aliased && !composite)
        {
            aObject->where = storage::Constant;
            return;
        }
        // (its size: its subtype's objects', at least its specified size)
        auto const size = std::max<std::uint64_t>(composite ? size_of(t) : std::max<std::uint64_t>(8u, size_of(t)),
            composite && size_of(t) == 0u ? 0u : (aObject->size_clause + 7u) / 8u);
        // (its alignment: specified (for it, or its subtype), else 8; the frame's (and the stack's) is 16, the most supported)
        auto const alignment = std::min<std::uint64_t>(std::max<std::uint64_t>(iProgram.object_alignment(aObject), 8u), 16u);
        // (an array's data (aligned) follows its descriptor)
        auto const skew = t != nullptr && t->view()->kind == type_kind::Array ? (8u + 16u * t->view()->base_type()->view()->indexes.size()) % alignment : 0u;     // (its descriptor's header)
        if (library || (aObject->scope != nullptr && aObject->scope->level == 0))
        {
            if (composite && size == 0u)
            {
                aObject->where = storage::IndirectStatic;
                aObject->offset = static_cast<std::int64_t>(iProgram.static_data(8u, 8u));
                return;
            }
            aObject->where = storage::Static;
            aObject->offset = static_cast<std::int64_t>(iProgram.static_data(std::max<std::uint64_t>(size, 8u), alignment, skew));
            return;
        }
        if (!composite && !aObject->aliased && !aObject->captured && alignment <= 8u && aObject->alignment_clause == 0u)
        {
            aObject->where = storage::Local;
            aObject->local = local(wasm_of(t));
            return;
        }
        if (composite && size == 0u)
        {
            if (aObject->captured)
            {
                iFrameSize = (iFrameSize + 7u) & ~std::uint64_t{ 7u };
                aObject->where = storage::IndirectFrame;
                aObject->offset = static_cast<std::int64_t>(iFrameSize);
                iFrameSize += 8u;
            }
            else
            {
                aObject->where = storage::Indirect;
                aObject->local = local(type::I64);
            }
            return;
        }
        iFrameSize = ((iFrameSize + skew + alignment - 1u) & ~(alignment - 1u)) - skew;
        aObject->where = storage::Frame;
        aObject->offset = static_cast<std::int64_t>(iFrameSize);
        iFrameSize += std::max<std::uint64_t>(size, 8u);
    }

    void function_generator::frame_of_level(int aLevel)
    {
        auto const hops = iLevel - aLevel;
        if (hops <= 0)
        {
            get(iFrame);
            return;
        }
        get(StaticLinkLocal);
        for (int i = 1; i < hops; ++i)
            load64(0u);
    }

    entity* function_generator::resolve_renaming(entity* aObject)
    {
        auto* o = aObject;
        while (o->where == storage::Renaming && o->renamed != nullptr && o->renamed != o && (o->renamed_name == nullptr || is_simple_name(o->renamed_name)))
            o = o->renamed;
        while (o->renamed != nullptr && o->kind == entity_kind::Constant && o->completed && o->where == storage::None)
            o = o->renamed;
        return o;
    }

    void function_generator::address_of(entity* aObject)
    {
        if (std::uint64_t offset = 0u; !iInstances.empty())
            if (std::uint32_t instance = 0u; instance_member(aObject, offset, instance))
            {
                for (auto i = iInstances.size(); i-- > 0u;)
                    if (iInstances[i].second == instance)
                    {
                        for (auto const& c : iInstances[i].first->components)
                            if (c.ent == aObject && c.offset)
                            {
                                component_address(iInstances[i].first, c, instance);
                                return;
                            }
                        break;
                    }
                get(instance);
                i64(static_cast<std::int64_t>(offset));
                op(opcode::I64Add);
                return;
            }
        if (is_self_member(aObject))
        {
            // (a component or discriminant of the task or protected object (within its body))
            auto* t = (aObject->owner != nullptr ? aObject->owner : aObject->scope->owner)->type;
            iProgram.layout(t->view()->base_type()->view());
            self_address(aObject);
            for (auto const& c : t->view()->base_type()->view()->components)
                if (c.ent == aObject && c.offset)
                {
                    if (c.dynamic && c.type != nullptr && c.type->view()->kind != type_kind::Array)
                    {
                        // (a dynamically sized record component: in the tail)
                        auto const self = temporary(type::I64);
                        tee(self);
                        load64(static_cast<std::uint32_t>(*c.offset));
                        get(self);
                        op(opcode::I64Add);
                        release(self);
                    }
                    else
                    {
                        i64(static_cast<std::int64_t>(*c.offset));
                        op(opcode::I64Add);
                    }
                }
            return;
        }
        auto* o = resolve_renaming(aObject);
        if (o->where == storage::None)
            assign_storage(o);
        switch (o->where)
        {
        case storage::Static:
            i64(o->offset);
            return;
        case storage::IndirectStatic:
            i64(o->offset);
            load64(0u);
            return;
        case storage::Frame:
            frame_of_level(static_cast<int>(o->function_level));
            if (o->offset != 0)
            {
                i64(o->offset);
                op(opcode::I64Add);
            }
            return;
        case storage::Indirect:
            if (static_cast<int>(o->function_level) != iLevel)
                iProgram.error(iWhere, "internal error: up-level reference to \"" + o->name + "\" not in a frame (level " + std::to_string(o->function_level) + " in " + std::to_string(iLevel) + ", of " + (o->scope != nullptr && o->scope->owner != nullptr ? o->scope->owner->name : std::string{ "?" }) + ")");
            get(o->local);
            return;
        case storage::IndirectFrame:
            frame_of_level(static_cast<int>(o->function_level));
            load64(static_cast<std::uint32_t>(o->offset));
            return;
        case storage::Renaming:
            address(o->renamed_name);
            return;
        default:
            iProgram.error(iWhere, "internal error: \"" + o->name + "\" has no address");
        }
    }

    void function_generator::load_object(entity* aObject)
    {
        // (a discriminant constraint's hidden constant not elaborated with a declaration (e.g. an extended return object's): now)
        if (aObject->where == storage::None && aObject->initial != nullptr && aObject->kind == entity_kind::Constant && aObject->name == "%discriminant")
        {
            assign_storage(aObject);
            expression(aObject->initial, aObject->type);
            store_object(aObject);
        }
        std::uint64_t instanceOffset = 0u;
        std::uint32_t instance = 0u;
        // (a discriminant of an ancestor of the record being initialized that its derivation constrains by a static value: that)
        if (aObject->kind == entity_kind::Discriminant && !iInstances.empty() && !instance_member(aObject, instanceOffset, instance))
            for (auto i = iInstances.size(); i-- > 0u;)
                for (auto* d = iInstances[i].first; d != nullptr && d->parent != nullptr;)
                {
                    auto* ps = d->parent->view();
                    auto* pb = ps->base_type()->view();
                    if (pb == d)
                        break;
                    std::size_t k = 0u;
                    for (auto const& pc : pb->components)
                        if (pc.discriminant)
                        {
                            if (pc.ent == aObject && k < ps->discriminant_values.size() && ps->discriminant_values[k].known())
                            {
                                constant_value(ps->discriminant_values[k], aObject->type);
                                return;
                            }
                            ++k;
                        }
                    d = pb;
                }
        if (is_self_member(aObject) || (!iInstances.empty() && instance_member(aObject, instanceOffset, instance)))
        {
            // (a bit field (see program::bit_field) of the record being initialized: its bits)
            for (auto i = iInstances.size(); i-- > 0u && !is_self_member(aObject);)
                if (iInstances[i].second == instance)
                {
                    for (auto const& c : iInstances[i].first->components)
                        if (c.ent == aObject && c.offset && iProgram.bit_field(c))
                        {
                            get(instance);
                            component_load(c);
                            return;
                        }
                    break;
                }
            address_of(aObject);
            if (aObject->type != nullptr && !aObject->type->view()->is_composite())
                load(kind_of(aObject->type), iProgram.scalar_size(aObject->type), 0u, !is_unsigned(aObject->type));
            return;
        }
        auto* o = resolve_renaming(aObject);
        if (o->where == storage::None)
            assign_storage(o);
        auto* t = o->type;
        if (o->where == storage::Constant)
        {
            constant_value(o->value, t);
            return;
        }
        if (t != nullptr && t->view()->is_composite())
        {
            address_of(o);
            return;
        }
        if (o->where == storage::Local)
        {
            if (static_cast<int>(o->function_level) != iLevel)
                iProgram.error(iWhere, "internal error: up-level reference to \"" + o->name + "\" not in a frame (in " + (iSubprogram != nullptr ? iSubprogram->name : std::string{ "?" }) + ")");
            get(o->local);
            return;
        }
        if (o->where == storage::Renaming)
        {
            expression(o->renamed_name, t);
            return;
        }
        address_of(o);
        if (o->packed_bits != 0u)
            packed_load(t, o->packed_bits);
        else
            load(kind_of(t), iProgram.scalar_size(t), 0u, !is_unsigned(t));
    }

    void function_generator::store_object(entity* aObject)
    {
        if (is_self_member(aObject))
        {
            auto* t = aObject->type;
            auto const value = temporary(wasm_of(t));
            set(value);
            address_of(aObject);
            if (t != nullptr && t->view()->is_composite())
            {
                auto const target = temporary(type::I64);
                set(target);
                copy_value(t, target, value);
                release(target);
                release(value);
                return;
            }
            get(value);
            release(value);
            store(kind_of(t), iProgram.scalar_size(t));
            return;
        }
        auto* o = resolve_renaming(aObject);
        if (o->where == storage::None)
            assign_storage(o);
        auto* t = o->type;
        if (o->where == storage::Local)
        {
            set(o->local);
            return;
        }
        auto const value = temporary(wasm_of(t));
        set(value);
        address_of(o);
        if (t != nullptr && t->view()->is_composite())
        {
            auto const target = temporary(type::I64);
            set(target);
            copy_value(t, target, value);
            release(target);
            release(value);
            return;
        }
        get(value);
        release(value);
        if (o->packed_bits != 0u)
            packed_store(o->packed_bits);
        else
            store(kind_of(t), iProgram.scalar_size(t));
    }

    bool function_generator::is_simple_name(node* aName)
    {
        return aName->is(construct::Identifier) || (aName->is(construct::Selected) && meaning_of(aName) != meaning::Component);
    }

    // --- declarations

    void function_generator::declarations(node* aDeclarations, bool aActivate)
    {
        if (aDeclarations == nullptr)
        {
            if (aActivate && iDeferredActivation)
            {
                iDeferredActivation = false;
                runtime("task_activate", {}, type::Void);
                check();
            }
            return;
        }
        auto const created = iTasksCreated;
        for (auto* d : aDeclarations->children)
            declaration(d);
        // (the tasks that the declarative part created: activated (a package specification's: at the end of the enclosing
        // declarative part, after its private part (9.2(2/2))))
        if (aActivate && (iTasksCreated != created || iDeferredActivation))
        {
            iDeferredActivation = false;
            runtime("task_activate", {}, type::Void);
            check();
        }
    }

    void function_generator::deferred_activation_declarations(node* aDeclarations, entity* aPackage)
    {
        // (a body's or block's declarative part: the tasks it creates (only those; a package body's, its specification's too)
        // activated by its handled part (see handled))
        bool const outerDeferred = iDeferredActivation;
        iDeferredActivation = false;
        auto const created = iTasksCreated;
        std::optional<std::uint32_t> mark;
        std::optional<std::pair<std::uint32_t, std::uint32_t>> specification;
        if (auto i = aPackage != nullptr ? iSpecActivationMarks.find(aPackage) : iSpecActivationMarks.end(); i != iSpecActivationMarks.end())
        {
            specification = i->second;
            iSpecActivationMarks.erase(i);
        }
        if (!iProgram.iTaskUnits.empty() && ((aDeclarations != nullptr && !aDeclarations->children.empty()) || specification))
        {
            mark = local(type::I64);
            runtime("task_pending", {}, type::I64);
            set(*mark);
        }
        declarations(aDeclarations, false);
        iActivateAtBegin = iTasksCreated != created || iDeferredActivation || specification.has_value();
        iActivationMark = mark;
        iSpecActivation = specification;
        iDeferredActivation = outerDeferred;
    }

    void function_generator::declaration(node* aDecl)
    {
        iWhere = aDecl;
        elaborate_dynamic_bounds(aDecl);
        // (a tagged type whose External_Tag aspect is specified: its external tag not another's (Program_Error, 13.3(76.1/3)))
        if (aDecl->is(construct::Type) && aDecl->first(construct::Aspects) != nullptr)
            if (auto* d = aDecl->first(construct::Defining); d != nullptr && d->ent != nullptr && d->ent->type != nullptr &&
                d->ent->type->view()->base_type()->view()->tagged)
                if (auto* external = iProgram.type_aspect(d->ent->type, "external_tag"))
                    for (auto const* n = external; n != nullptr; n = n->parent)
                        if (n == aDecl)
                        {
                            i64(static_cast<std::int64_t>(iProgram.tag_of(d->ent->type)));
                            runtime("tag_register_external", { type::I64 }, type::Void);
                            check();
                            break;
                        }
        switch (aDecl->kind)
        {
        case construct::Object:
            object_declaration(aDecl);
            break;
        case construct::Type:
            access_collection(aDecl);
            tag_link(aDecl);
            break;
        case construct::Package:
            {
                // (its tasks: activated at its body's begin (9.2(2/2)), else at the end of the enclosing declarative part)
                auto const created = iTasksCreated;
                std::optional<std::uint32_t> mark;
                if (!iProgram.iTaskUnits.empty())
                {
                    mark = local(type::I64);
                    runtime("task_pending", {}, type::I64);
                    set(*mark);
                }
                declarations(aDecl->first(construct::Declarations), false);
                if (auto* priv = aDecl->first(construct::Private))
                    for (auto* d : priv->children)
                        declaration(d);
                if (iTasksCreated != created)
                {
                    iDeferredActivation = true;
                    if (mark && aDecl->ent != nullptr)
                    {
                        // (its tasks: those created by its elaboration (not later declarations'), activated by its body)
                        auto const last = local(type::I64);
                        runtime("task_pending", {}, type::I64);
                        set(last);
                        iSpecActivationMarks[aDecl->ent] = std::make_pair(*mark, last);
                    }
                }
            }
            break;
        case construct::TaskBody:
            // (a nested task body: elaborated (its tasks can be activated from now on, 9.2(5)))
            if (aDecl->ent != nullptr && aDecl->ent->scope != nullptr && aDecl->ent->scope->level > 0)
                if (auto b = iProgram.iTaskBodies.find(aDecl->ent); b != iProgram.iTaskBodies.end())
                {
                    i64(static_cast<std::int64_t>(iProgram.elaboration_flag(b->second)));
                    i64(1);
                    store64(0u);
                }
            break;
        case construct::Stub:
            // (a body stub: its subunit's body elaborated here)
            if (auto* proper = iProgram.stub_body(aDecl))
                declaration(proper);
            break;
        case construct::PackageBody:
            if (aDecl->ent != nullptr && aDecl->ent->kind == entity_kind::GenericPackage)
            {
                // (a generic package's body: elaborated (instantiations are permitted from now on))
                if (aDecl->ent->scope != nullptr && aDecl->ent->scope->level > 0)
                {
                    i64(static_cast<std::int64_t>(iProgram.elaboration_flag(aDecl->ent)));
                    i64(1);
                    store64(0u);
                }
                break;
            }
            deferred_activation_declarations(aDecl->first(construct::Declarations), aDecl->ent);
            handled(aDecl->first(construct::Handled));
            break;
        case construct::Instantiation:
            if (auto const* instance = iProgram.instance_of(aDecl))
            {
                // (a generic unit (not a library unit) whose body is elaborated later: not yet (else Program_Error, 3.11(13)))
                if (auto* g = instance->instance != nullptr ? instance->instance->generic : nullptr;
                    g != nullptr && g->scope != nullptr && g->scope->level > 0 && iProgram.generic_body_follows(g, aDecl))
                {
                    i64(static_cast<std::int64_t>(iProgram.elaboration_flag(g)));
                    load64(0u);
                    op(opcode::I64Eqz);
                    begin_if();
                    raise_check(1, aDecl, "access before elaboration");
                    end();
                }
                for (auto const& ic : iProgram.instance_constants())
                    if (ic.instance == instance->instance)
                    {
                        auto* c = ic.constant;
                        if (c->where == storage::Renaming && c->renamed_name != nullptr && !(is_simple_name(c->renamed_name) && c->renamed != nullptr))
                            renamed_address(c);     // (an in out formal object: the actual's address)
                        else if (c->where == storage::None)
                            initialize_object(c);   // (a copy of the actual (an indefinite one's: allocated))
                    }
                if (instance->spec != nullptr)
                    declaration(instance->spec);
                if (instance->body != nullptr)
                    declaration(instance->body);
            }
            break;
        case construct::RenamingObject:
            if (auto* e = aDecl->ent; e != nullptr && e->where == storage::Renaming && e->renamed_name != nullptr && !(is_simple_name(e->renamed_name) && e->renamed != nullptr))
                renamed_address(e);
            break;
        case construct::TaskSingle:
        case construct::ProtectedSingle:
            object_declaration(aDecl);
            break;
        case construct::Pragma:
            pragma_statement(aDecl);
            break;
        case construct::RepresentationAttribute:
            // (an External_Tag specification: its external tag not another's (Program_Error, 13.3(76.1/3)))
            if (aDecl->size() >= 2u && aDecl->child(0)->is(construct::Attribute) && name_text(aDecl->child(0)->child(1)->source) == "external_tag" &&
                aDecl->child(0)->child(0)->type != nullptr && aDecl->child(0)->child(0)->type->view()->base_type()->view()->tagged)
            {
                i64(static_cast<std::int64_t>(iProgram.tag_of(aDecl->child(0)->child(0)->type)));
                runtime("tag_register_external", { type::I64 }, type::Void);
                check();
            }
            break;
        case construct::SubprogramExpression:
        case construct::SubprogramNull:
            // (an expression function or null procedure that completes a declaration: elaborated)
            if (aDecl->ent != nullptr && aDecl->ent->sub != nullptr && aDecl->ent->sub->body == aDecl && iProgram.elaboration_checked(aDecl->ent))
            {
                i64(static_cast<std::int64_t>(iProgram.elaboration_flag(aDecl->ent)));
                i64(1);
                store64(0u);
            }
            break;
        case construct::RenamingSubprogram:
            // (a renaming-as-body: likewise (8.5.4(7.1/1)))
            if (aDecl->ent != nullptr && aDecl->ent->sub != nullptr && aDecl->ent->sub->body == aDecl && iProgram.elaboration_checked(aDecl->ent))
            {
                i64(static_cast<std::int64_t>(iProgram.elaboration_flag(aDecl->ent)));
                i64(1);
                store64(0u);
            }
            break;
        case construct::SubprogramBody:
            // (a body that completes a declaration (or a generic subprogram's): elaborated (calls of the subprogram (its 
            // instantiations) are permitted from now on))
            if (iProgram.elaboration_checked(aDecl->ent) || (aDecl->ent != nullptr && aDecl->ent->is_generic() && aDecl->ent->scope != nullptr &&
                aDecl->ent->scope->level > 0))
            {
                i64(static_cast<std::int64_t>(iProgram.elaboration_flag(aDecl->ent)));
                i64(1);
                store64(0u);
            }
            break;
        default:
            break;
        }
    }

    void function_generator::evaluate_dynamic_bounds(node* aNode)
    {
        // (the bounds of the subtypes whose constraints are in an expression (e.g. an allocator's subtype indication): evaluated
        // when it is (each time))
        auto const within = [&](node const* aBound)
        {
            for (auto const* n = aBound; n != nullptr; n = n->parent)
                if (n == aNode)
                    return true;
            return false;
        };
        for (auto const& b : iProgram.dynamic_bounds())
            if (b.low->source.data() >= aNode->source.data() && b.low->source.data() < aNode->source.data() + aNode->source.size() && within(b.low) &&
                (b.lowObject->scope == nullptr || b.lowObject->scope->level <= iLevel))
            {
                if (b.lowObject->where == storage::None)
                    assign_storage(b.lowObject);
                expression(b.low, b.lowObject->type);
                store_object(b.lowObject);
                if (b.highObject->where == storage::None)
                    assign_storage(b.highObject);
                expression(b.high, b.highObject->type);
                store_object(b.highObject);
            }
    }

    void function_generator::elaborate_dynamic_bounds(node* aDecl)
    {
        // (a declaration that contains declarations: theirs are elaborated with them)
        if (aDecl->is(construct::Package) || aDecl->is(construct::PackageBody) || aDecl->is(construct::SubprogramBody) ||
            aDecl->is(construct::SubprogramExpression) || aDecl->is(construct::Generic) || aDecl->is(construct::TaskBody) ||
            aDecl->is(construct::ProtectedBody))
            return;
        auto const within = [&](node const* aNode)
        {
            // (a node of the declaration (not merely its source: an instance's copy has the generic's source), not of an
            // allocator in it (that evaluates them itself))
            for (auto const* n = aNode; n != nullptr; n = n->parent)
            {
                if (n == aDecl)
                    return true;
                if (n->is(construct::Allocator))
                    return false;
            }
            return false;
        };
        // (the bounds and the discriminant constraints' values, in the order in which they appear)
        std::vector<std::pair<char const*, std::function<void()>>> elaborations;
        for (auto const& b : iProgram.dynamic_bounds())
            if (b.low->source.data() >= aDecl->source.data() && b.low->source.data() < aDecl->source.data() + aDecl->source.size() &&
                b.lowObject->where == storage::None && b.highObject->where == storage::None &&
                (b.lowObject->scope == nullptr || b.lowObject->scope->level <= iLevel) && within(b.low))
                elaborations.emplace_back(b.low->source.data(), [this, &b]()
                    {
                        // (a bound that depends on a discriminant (a per-object expression): evaluated when an object is created
                        // (3.8(18/2)); the other, now)
                        for (auto [bound, object] : { std::pair{ b.low, b.lowObject }, std::pair{ b.high, b.highObject } })
                            if (!iProgram.refers_to_discriminant(bound))
                            {
                                assign_storage(object);
                                expression(bound, object->type);
                                store_object(object);
                            }
                    });
        for (auto const& c : iProgram.dynamic_constraints())
            if (c.value->source.data() >= aDecl->source.data() && c.value->source.data() < aDecl->source.data() + aDecl->source.size() &&
                c.object->where == storage::None && (c.object->scope == nullptr || c.object->scope->level <= iLevel) && within(c.value))
                elaborations.emplace_back(c.value->source.data(), [this, &c]()
                    {
                        assign_storage(c.object);
                        expression(c.value, c.object->type);
                        store_object(c.object);
                    });
        std::stable_sort(elaborations.begin(), elaborations.end(), [](auto const& a, auto const& b) { return a.first < b.first; });
        for (auto const& e : elaborations)
            e.second();
        compatibility_checks(aDecl, within);
    }

    void function_generator::compatibility_checks(node* aConstruct, std::function<bool(node const*)> const& aWithin)
    {
        // (the construct's ranges: compatible with their type marks (a non-null range within the mark's), else Constraint_Error)
        for (auto const& c : iProgram.iCompatibilityChecks)
        {
            if (!aWithin(c.range) || iCompatibilityChecked.contains(c.range))
                continue;
            iCompatibilityChecked.insert(c.range);
            if (c.mark == nullptr)
            {
                // (an incompatible delta or digits constraint)
                raise_check(0, c.range, "constraint check failed");
                continue;
            }
            if (c.subtype == nullptr)
            {
                // (a discriminant constraint's value: in its discriminant's subtype (an object declaration's: checked when the
                // object is initialized (evaluated once)))
                bool objectDeclaration = false;
                for (auto const* n = c.range->parent; n != nullptr && !objectDeclaration; n = n->parent)
                {
                    if (n->is(construct::Object))
                    {
                        // (not of an access subtype: its designated objects are created elsewhere)
                        auto const* d = n->first(construct::Defining);
                        objectDeclaration = !(d != nullptr && d->ent != nullptr && d->ent->type != nullptr && d->ent->type->view()->is_access());
                    }
                    if (n->is(construct::Type) || n->is(construct::Subtype) || n->is(construct::Allocator))
                        break;
                }
                if (objectDeclaration)
                    continue;
                expression(c.range, c.mark);
                range_check(c.mark, c.range->type, c.range);
                drop();
                continue;
            }
            auto* s = c.subtype->view();
            // (its bounds, if not yet elaborated (e.g. a loop parameter specification's))
            for (auto* bound : { s->low_object, s->high_object })
                if (bound != nullptr && bound->where == storage::None && bound->initial != nullptr)
                {
                    assign_storage(bound);
                    expression(bound->initial, bound->type);
                    store_object(bound);
                }
            auto* m = c.mark->view();
            auto const k = kind_of(s);
            bool const real = k == vkind::F32 || k == vkind::F64;
            bool const f32 = k == vkind::F32;
            bool const unsignedCompare = s->base_type()->view()->kind == type_kind::Modular;
            auto const low = local(wasm(k));
            auto const high = local(wasm(k));
            scalar_low(s);
            set(low);
            scalar_high(s);
            set(high);
            // (non-null: low <= high)
            get(low);
            get(high);
            op(real ? (f32 ? opcode::F32Le : opcode::F64Le) : (unsignedCompare ? opcode::I64LeU : opcode::I64LeS));
            get(low);
            scalar_low(m);
            op(real ? (f32 ? opcode::F32Lt : opcode::F64Lt) : (unsignedCompare ? opcode::I64LtU : opcode::I64LtS));
            get(high);
            scalar_high(m);
            op(real ? (f32 ? opcode::F32Gt : opcode::F64Gt) : (unsignedCompare ? opcode::I64GtU : opcode::I64GtS));
            op(opcode::I32Ior);
            op(opcode::I32And);
            begin_if();
            raise_check(0, c.range, "range check failed");
            end();
        }
    }

    void function_generator::renamed_address(entity* e)
    {
        // (a renamed object's address, evaluated once (a renaming declaration's, or a generic in out formal object's))
        auto const address = local(type::I64);
        this->address(e->renamed_name);
        set(address);
        e->packed_bits = static_cast<std::uint8_t>(packed_element(e->renamed_name));
        // (the function results that it renames (or of which it renames a part): finalized with the renaming (by its master),
        // not as the declaration's temporaries (3.10.2(10/5), 7.6.1(3/2)))
        if (iTemporaries && iTemporaryMark != 0u)
        {
            i64(0);
            set(iTemporaryMark);
            iTemporaries = false;
        }
        e->function_level = static_cast<std::uint32_t>(iLevel);
        if (e->scope != nullptr && e->scope->level == 0)
        {
            auto const slot = iProgram.static_data(8u, 8u);
            i64(static_cast<std::int64_t>(slot));
            get(address);
            store64(0u);
            e->where = storage::IndirectStatic;
            e->offset = static_cast<std::int64_t>(slot);
        }
        else if (e->captured)
        {
            iFrameSize = (iFrameSize + 7u) & ~std::uint64_t{ 7u };
            e->offset = static_cast<std::int64_t>(iFrameSize);
            iFrameSize += 8u;
            get(iFrame);
            get(address);
            store64(static_cast<std::uint32_t>(e->offset));
            e->where = storage::IndirectFrame;
        }
        else
        {
            e->where = storage::Indirect;
            e->local = address;
        }
    }

    void function_generator::object_declaration(node* aDecl)
    {
        std::vector<entity*> objects;
        for (auto* d : aDecl->all(construct::Defining))
            if (d->ent != nullptr && d->ent->is_object())
                objects.push_back(d->ent);
        if (objects.empty() && aDecl->ent != nullptr && aDecl->ent->is_object())
            objects.push_back(aDecl->ent);
        for (auto* e : objects)
        {
            if (auto own = iProgram.iObjectSubtypes.find(e); own != iProgram.iObjectSubtypes.end())
                elaborate_dynamic_bounds(own->second);
            if (e->address_clause != nullptr)
            {
                // (an object at a specified address: there (initialized unless imported))
                if (e->where == storage::None)
                {
                    e->function_level = static_cast<std::uint32_t>(iLevel);
                    if (e->scope != nullptr && e->scope->level == 0)
                    {
                        e->where = storage::IndirectStatic;
                        e->offset = static_cast<std::int64_t>(iProgram.static_data(8u, 8u));
                    }
                    else if (e->captured)
                    {
                        iFrameSize = (iFrameSize + 7u) & ~std::uint64_t{ 7u };
                        e->where = storage::IndirectFrame;
                        e->offset = static_cast<std::int64_t>(iFrameSize);
                        iFrameSize += 8u;
                    }
                    else
                    {
                        e->where = storage::Indirect;
                        e->local = local(type::I64);
                    }
                }
                if (auto* av = e->type != nullptr ? e->type->view() : nullptr; av != nullptr && av->kind == type_kind::Array && av->constrained)
                {
                    // (an array: a descriptor of its bounds, its data (at the address) as an offset from the descriptor)
                    auto const dimensions = av->base_type()->view()->indexes.size();
                    auto const size = 8u + 16u * dimensions;
                    auto const descriptor = local(type::I64);
                    if (e->scope != nullptr && e->scope->level == 0)
                        i64(static_cast<std::int64_t>(iProgram.static_data(size, 8u)));
                    else
                    {
                        iFrameSize = (iFrameSize + 7u) & ~std::uint64_t{ 7u };
                        get(iFrame);
                        i64(static_cast<std::int64_t>(iFrameSize));
                        op(opcode::I64Add);
                        iFrameSize += size;
                    }
                    set(descriptor);
                    get(descriptor);
                    expression(e->address_clause, e->address_clause->type);
                    get(descriptor);
                    op(opcode::I64Sub);
                    store64(0u);
                    for (std::size_t k = 0u; k < dimensions && k < av->indexes.size(); ++k)
                    {
                        get(descriptor);
                        index_bound(av->indexes[k], true);
                        store64(static_cast<std::uint32_t>(8u + 16u * k));
                        get(descriptor);
                        index_bound(av->indexes[k], false);
                        store64(static_cast<std::uint32_t>(16u + 16u * k));
                    }
                    get(descriptor);
                }
                else
                    expression(e->address_clause, e->address_clause->type);
                set_indirect(e);
                if (!e->imported && e->initial != nullptr)
                {
                    expression(e->initial, e->type);
                    if (e->type != nullptr && !e->type->view()->is_composite())
                        store_object(e);
                    else
                        drop();
                }
                continue;
            }
            if (e->imported || (e->kind == entity_kind::Constant && e->initial == nullptr && e->completion != nullptr))
                continue;
            initialize_object(e);
            // (an object with tasks initialized by a function's result (its return object's tasks): activated with the declarative
            // part's (9.2(2/2)))
            if (e->initial != nullptr && e->type != nullptr && iProgram.needs_tasks(e->type))
            {
                ++iTasksCreated;
                address_of(e);
                auto const object = local(type::I64);
                set(object);
                relocate_tasks(e->type, object);
            }
            store_access_level(e, e->initial);
            // (a default-initialized object of a type with invariants (unless its partial view has unknown discriminants): checked
            // (7.3.2(10/4)))
            if (e->initial == nullptr && e->type != nullptr && e->type->view()->is_composite() && !e->imported)
            {
                if (!invariants_of(e->type).empty() && !e->type->unknown_discriminants && !e->type->base_type()->unknown_discriminants)
                {
                    address_of(e);
                    auto const value = local(type::I64);
                    set(value);
                    invariant_check(e->type, value, aDecl);
                }
                else if (auto* v = e->type->view(); (v->kind == type_kind::Array && v->component != nullptr && !invariants_of(v->component).empty()) ||
                    v->kind == type_kind::Record)
                {
                    bool parts = v->kind == type_kind::Array;
                    if (v->kind == type_kind::Record)
                    {
                        iProgram.layout(v->base_type()->view());
                        for (auto const& c : v->base_type()->view()->components)
                            parts = parts || (c.type != nullptr && c.type->view()->is_composite() && !invariants_of(c.type).empty());
                    }
                    if (parts)
                    {
                        address_of(e);
                        auto const value = local(type::I64);
                        set(value);
                        parts_invariant_check(e->type, value, aDecl);
                    }
                }
            }
            // (a composite object of a subtype with a predicate: initialized explicitly, or by default with default values (3.2.4(31/5)))
            if (auto* t = e->type; t != nullptr && t->view()->is_composite() && (t->predicate != nullptr || t->view()->predicate != nullptr))
            {
                bool defaults = e->initial != nullptr;
                auto* b = t->view()->base_type()->view();
                if (b->kind == type_kind::Record)
                {
                    iProgram.layout(b);
                    for (auto const& c : b->components)
                        defaults = defaults || (c.default_expression != nullptr && !c.discriminant);
                }
                if (defaults)
                {
                    address_of(e);
                    predicate_check(t, nullptr, e->initial);
                    drop();
                }
            }
        }
    }

    void function_generator::object_declaration_of(entity* aObject)
    {
        initialize_object(aObject);
    }

    void function_generator::initialize_object(entity* e)
    {
        auto* t = e->type;
        auto* v = t != nullptr ? t->view() : nullptr;
        assign_storage(e);
        if (e->where == storage::Constant)
        {
            // (a static value outside its subtype: Constraint_Error when elaborated (3.3.1(18/2)))
            if (v != nullptr && v->is_scalar() && e->initial != nullptr)
                if (auto value = iProgram.evaluate(e->initial, nullptr); value.known() && !value.is_string() && v->low.known() && v->high.known())
                {
                    bool outside = false;
                    if (value.is_integer() && v->low.is_integer() && v->high.is_integer())
                        outside = value.integer() < v->low.integer() || value.integer() > v->high.integer();
                    else if (!value.is_integer() && !v->low.is_integer() && !v->high.is_integer())
                        outside = value.real() < v->low.real() || value.real() > v->high.real();
                    if (outside)
                        raise_check(0, e->initial, "range check failed");
                }
            return;
        }
        if (v != nullptr && v->is_composite())
        {
            bool const dynamic = e->where == storage::Indirect || e->where == storage::IndirectFrame || e->where == storage::IndirectStatic;
            bool const heap = e->where == storage::IndirectStatic;
            std::optional<std::uint32_t> value;
            // (the subtype's discriminant constraint: evaluated before the initial value)
            std::vector<std::uint32_t> constraint;
            if (e->initial != nullptr)
                constraint = discriminant_constraint_values(v);
            moved_result(e->initial);
            if (e->initial != nullptr && !(is_aggregate(e->initial) && !dynamic))
            {
                if (is_aggregate(e->initial))
                    aggregate(e->initial, t);
                else
                    expression(e->initial, t);
                value = temporary(type::I64);
                set(*value);
            }
            if (dynamic)
            {
                if (v->kind == type_kind::Array)
                {
                    if (v->constrained || !value)
                    {
                        subtype_descriptor(v);
                        auto const descriptor = temporary(type::I64);
                        set(descriptor);
                        allocate_array(v, descriptor, false, heap);
                        release(descriptor);
                    }
                    else
                        allocate_array(v, *value, false, heap);
                }
                else
                    allocate_record(v, value, heap);
                set_indirect(e);
                if (is_runtime_sized(v))
                {
                    // (initialized (a copy of the value, or by default))
                    address_of(e);
                    auto const target = temporary(type::I64);
                    set(target);
                    release_temporaries();
                    controlled_initialized(v, target, !value, value && !is_temporary_value(e->initial), true);
                    release(target);
                    if (value)
                        release(*value);
                    return;
                }
            }
            address_of(e);
            auto const target = temporary(type::I64);
            set(target);
            if (!dynamic && v->kind == type_kind::Array)
                init_descriptor(v, target);
            if (e->initial != nullptr && !value)
            {
                aggregate(e->initial, t, target);
                drop();
                discriminant_constraint_check(v, target, e->initial, &constraint);
                release_temporaries();
                controlled_initialized(v, target, false, false, true);
            }
            else if (value)
            {
                if (v->kind == type_kind::Array)
                    length_check(target, *value, v->base_type()->view()->indexes.size(), e->initial);
                discriminant_constraint_check(v, *value, e->initial, &constraint);
                if (v->kind == type_kind::Record && v->base_type()->view()->tagged)
                {
                    get(target);
                    i64(static_cast<std::int64_t>(iProgram.tag_of(v)));
                    store64(0u);
                }
                copy_value(v, target, *value);
                release(*value);
                release_temporaries();
                controlled_initialized(v, target, false, !is_temporary_value(e->initial), true);
            }
            else
            {
                default_initialize(v, target);
                controlled_initialized(v, target, true, false, true);
            }
            release(target);
            return;
        }
        if (e->initial != nullptr)
        {
            expression(e->initial, t);
            range_check(t, e->initial->type, e->initial);
            store_object(e);
            release_temporaries();
        }
        else if (v != nullptr && v->is_access())
        {
            // (null: not of a null-excluding subtype (3.10(14.1/2)))
            i64(0);
            if (v->not_null)
                range_check(t, nullptr, nullptr);
            store_object(e);
        }
        else if (auto* d = v != nullptr && v->is_scalar() && e->kind == entity_kind::Variable ? iProgram.default_value_of(v, false) : nullptr)
        {
            // (a scalar type's Default_Value: of the object's subtype (3.3.1(13/3)))
            expression(d, t);
            constraint_check(t, nullptr, d);
            store_object(e);
        }
        else if (auto invalid = iProgram.iNormalizeScalars && v != nullptr && v->is_scalar() && e->kind == entity_kind::Variable ?
            iProgram.invalid_value(v) : std::nullopt)
        {
            // (Normalize_Scalars: an invalid value (H.1(4)))
            i64(*invalid);
            store_object(e);
        }
    }

    bool function_generator::is_aggregate(node* aNode)
    {
        return aNode->is(construct::Aggregate) || aNode->is(construct::AggregateBracket) || aNode->is(construct::AggregateNullRecord) ||
            aNode->is(construct::AggregateExtension) || aNode->is(construct::AggregateDelta);
    }

    void function_generator::indirect_storage(entity* aObject)
    {
        // (an object at an address computed when it is elaborated (e.g. a loop parameter denoting an element): the address in a 
        // local, or in the frame if a nested subprogram refers to the object)
        aObject->function_level = static_cast<std::uint32_t>(iLevel);
        if (aObject->captured)
        {
            iFrameSize = (iFrameSize + 7u) & ~std::uint64_t{ 7u };
            aObject->where = storage::IndirectFrame;
            aObject->offset = static_cast<std::int64_t>(iFrameSize);
            iFrameSize += 8u;
        }
        else
        {
            aObject->where = storage::Indirect;
            aObject->local = local(type::I64);
        }
    }

    void function_generator::set_indirect(entity* aObject)
    {
        // ([address] -> (): a dynamically sized object's address)
        switch (aObject->where)
        {
        case storage::Indirect:
            set(aObject->local);
            break;
        case storage::IndirectFrame:
            {
                auto const a = temporary(type::I64);
                set(a);
                get(iFrame);
                get(a);
                store64(static_cast<std::uint32_t>(aObject->offset));
                release(a);
                break;
            }
        case storage::IndirectStatic:
            {
                auto const a = temporary(type::I64);
                set(a);
                i64(aObject->offset);
                get(a);
                store64(0u);
                release(a);
                break;
            }
        default:
            drop();
            break;
        }
    }

    // --- statements

    void function_generator::statements(node* aStatements)
    {
        if (aStatements == nullptr)
            return;
        // (a sequence with labels (goto targets): its segments (from each label) in blocks, dispatched by a state, in a loop:
        // block $exit loop $top block $sN .. block $s0 (state: br_if $sk) end s0 .. end sN br $exit end end;
        // goto Lk: state := k; br $top)
        std::vector<std::pair<std::string, std::size_t>> labels;
        std::vector<std::size_t> starts;    // (each label's statement index)
        for (std::size_t i = 0u; i < aStatements->children.size(); ++i)
            if (auto* s = aStatements->children[i]; s->is(construct::Label))
            {
                std::string text = name_text(s->source);
                std::erase_if(text, [](char c) { return c == '<' || c == '>'; });
                labels.emplace_back(text, labels.size() + 1u);
                starts.push_back(i);
            }
        if (labels.empty())
        {
            for (auto* s : aStatements->children)
                statement(s);
            return;
        }
        auto const state = local(type::I64);
        i64(0);
        set(state);
        begin(control_kind::Block);
        auto const exit = iControls.size() - 1u;
        begin(control_kind::Loop);
        auto const top = iControls.size() - 1u;
        iGotoScopes.push_back(goto_scope{ labels, top, state });
        auto const n = labels.size();
        std::vector<std::size_t> blocks(n + 1u);
        for (std::size_t k = n + 1u; k-- > 0u;)
        {
            begin(control_kind::Block);
            blocks[k] = iControls.size() - 1u;
        }
        for (std::size_t k = 1u; k <= n; ++k)
        {
            get(state);
            i64(static_cast<std::int64_t>(k));
            op(opcode::I64Eq);
            branch(blocks[k], true);
        }
        end();  // (s0)
        std::size_t next = 0u;
        for (std::size_t k = 0u; k <= n; ++k)
        {
            auto const stop = k < n ? starts[k] : aStatements->children.size();
            for (; next < stop; ++next)
                statement(aStatements->children[next]);
            if (k < n)
                end();  // (s(k + 1))
        }
        iGotoScopes.pop_back();
        branch(exit);
        end();  // (top)
        end();  // (exit)
    }

    void function_generator::goto_statement(node* aStatement)
    {
        // (an expanded name of a label: its selector)
        auto name = name_text(aStatement->child(0)->source);
        if (auto const dot = name.rfind('.'); dot != std::string::npos)
            name = name.substr(dot + 1u);
        for (auto i = iGotoScopes.size(); i-- > 0u;)
            for (auto const& [label, k] : iGotoScopes[i].labels)
                if (label == name)
                {
                    // (the masters left (blocks within the label's scope): their objects finalized (7.6.1(2)))
                    auto const top = iGotoScopes[i].top;
                    for (auto c = iControls.size(); c-- > top + 1u;)
                        if (iControls[c].onExit != nullptr)
                            (*iControls[c].onExit)();
                    for (auto c = top + 1u; c < iControls.size(); ++c)
                        if (iControls[c].finMark >= 0)
                        {
                            get(static_cast<std::uint32_t>(iControls[c].finMark));
                            runtime("fin_release", { type::I64 }, type::Void);
                            check();
                            break;
                        }
                    i64(static_cast<std::int64_t>(k));
                    set(iGotoScopes[i].state);
                    branch(top);
                    return;
                }
        iProgram.error(aStatement, "goto target \"" + name + "\" not found");
    }

    void function_generator::handled(node* aHandled)
    {
        bool const activate = iActivateAtBegin;
        auto const activationMark = iActivationMark;
        auto const specActivation = iSpecActivation;
        iActivateAtBegin = false;
        iActivationMark.reset();
        iSpecActivation.reset();
        auto const activation = [&]()
        {
            if (activate)
            {
                if (specActivation && activationMark)
                {
                    // (a package body's: its specification's tasks and its declarative part's (not those of declarations between them))
                    get(specActivation->first);
                    get(specActivation->second);
                    get(*activationMark);
                    runtime("task_activate_ranges", { type::I64, type::I64, type::I64 }, type::Void);
                }
                else if (activationMark)
                {
                    get(*activationMark);
                    runtime("task_activate_from", { type::I64 }, type::Void);
                }
                else
                    runtime("task_activate", {}, type::Void);
                check();
            }
        };
        if (aHandled == nullptr)
        {
            activation();
            return;
        }
        auto const handlers = aHandled->all(construct::Handler);
        if (handlers.empty())
        {
            activation();
            statements(aHandled->first(construct::Statements));
            return;
        }
        // block $done; block $dispatch (handler target); statements; br $done; end; (dispatch); end
        begin(control_kind::Block);
        auto const done = iControls.size() - 1u;
        begin(control_kind::Handler);
        activation();
        statements(aHandled->first(construct::Statements));
        branch(done);
        end();
        // (the temporaries of the statement that raised the exception: finalized)
        if (iTemporaryMark != 0u)
        {
            iTemporaries = true;
            release_temporaries(false);
        }
        auto const id = temporary(type::I64);
        op(opcode::GlobalGet);
        u32(iProgram.exc_global());
        set(id);
        for (auto* h : handlers)
        {
            bool others = false;
            std::vector<std::uint64_t> ids;
            for (auto* c : h->children)
            {
                if (c->is(construct::Others))
                    others = true;
                else if ((c->is(construct::Identifier) || c->is(construct::Selected)) && c->ent != nullptr && c->ent->kind == entity_kind::Exception)
                {
                    auto* x = c->ent;
                    while (x->renamed != nullptr)
                        x = x->renamed;
                    ids.push_back(x->exception_id);
                }
            }
            if (!others)
            {
                i32(0);
                for (auto x : ids)
                {
                    get(id);
                    i64(static_cast<std::int64_t>(x));
                    op(opcode::I64Eq);
                    op(opcode::I32Ior);
                }
                begin_if();
            }
            else
            {
                // (others: any exception but the abort signals (a task's, an asynchronous select's))
                get(id);
                i64(AtcSignalBase);
                op(opcode::I64LtU);
                begin_if();
            }
            // (handled: the occurrence saved, the pending flag cleared)
            auto const occurrence = local(type::I64);
            runtime("save_occurrence", {}, type::I64);
            set(occurrence);
            if (h->ent != nullptr)
            {
                auto* parameter = h->ent;
                parameter->where = storage::Local;
                parameter->local = occurrence;
                parameter->function_level = static_cast<std::uint32_t>(iLevel);
            }
            iOccurrences.push_back(occurrence);
            i64(0);
            op(opcode::GlobalSet);
            u32(iProgram.exc_global());
            statements(h->first(construct::Statements));
            iOccurrences.pop_back();
            get(occurrence);
            runtime("release_occurrence", { type::I64 }, type::Void);
            branch(done);
            end();
        }
        release(id);
        branch(handler_target());
        end();
    }

    void function_generator::statement(node* aStatement)
    {
        iWhere = aStatement;
        // (a simple statement's ranges (e.g. a slice's subtype indication): compatible with their type marks)
        if (aStatement->is(construct::Assignment) || aStatement->is(construct::Call) || aStatement->is(construct::Return))
            compatibility_checks(aStatement, [&](node const* aNode)
                {
                    for (auto const* n = aNode; n != nullptr; n = n->parent)
                        if (n == aStatement)
                            return true;
                    return false;
                });
        switch (aStatement->kind)
        {
        case construct::NullStatement:
        case construct::Label:
            break;
        case construct::Pragma:
            pragma_statement(aStatement);
            break;
        case construct::Assignment:
            with_secondary_stack([&]() { assignment(aStatement); });
            // (an assignment to a view conversion (to an ancestor) of an object of a type with invariants, outside its scope:
            // the object's (7.3.2(12/3)))
            if (auto* target = aStatement->child(0); target->is(construct::Apply) && meaning_of(target) == meaning::Conversion && target->size() > 1u &&
                target->child(1)->type != nullptr && !invariants_of(target->child(1)->type).empty() && outside_scope_of(target->child(1)->type))
            {
                address(target->child(1));
                auto const value = local(type::I64);
                set(value);
                invariant_check(target->child(1)->type, value, aStatement);
            }
            break;
        case construct::Call:
            if (auto* name = aStatement->child(0); name->is(construct::Attribute))
            {
                // (S'Write, S'Read, S'Output (a procedure attribute))
                auto const designator = name_text(name->child(1)->source);
                if (designator == "write" || designator == "read" || designator == "output")
                {
                    with_secondary_stack([&]() { stream_attribute(name, true); });
                    break;
                }
            }
            with_secondary_stack([&]() { call_expression(aStatement->child(0), true); });
            break;
        case construct::If:
            if_statement(aStatement);
            break;
        case construct::Case:
            case_statement(aStatement);
            break;
        case construct::Loop:
            loop_statement(aStatement);
            break;
        case construct::Block:
            block_statement(aStatement);
            break;
        case construct::Exit:
            exit_statement(aStatement);
            break;
        case construct::Return:
        case construct::ReturnExtended:
            if (iSubprogram != nullptr && iSubprogram->sub != nullptr && iSubprogram->sub->no_return && [&]()
                {
                    // (a return from the nonreturning procedure itself, not from an accept statement in it)
                    for (auto const* n = aStatement->parent; n != nullptr && !n->is(construct::SubprogramBody); n = n->parent)
                        if (n->is(construct::Accept))
                            return false;
                    return true;
                }())
            {
                raise_check(1, aStatement, "nonreturning procedure returned");
                break;
            }
            return_statement(aStatement);
            break;
        case construct::Raise:
            raise_statement(aStatement);
            break;
        case construct::Goto:
            goto_statement(aStatement);
            break;
        default:
            if (!tasking_statement(aStatement))
                iProgram.error(aStatement, "statement not supported yet");
            break;
        }
    }

    void function_generator::statement_condition(node* aExpression)
    {
        condition(aExpression);
        release_temporaries();
    }

    void function_generator::with_secondary_stack(std::function<void()> const& aGenerate)
    {
        // (the statement's code, then (if it calls anything but raise_check: it may allocate on the secondary stack) its mark put
        // before it, and its release after it)
        auto const start = iCode.size();
        auto const firstRelocation = iRelocations.size();
        aGenerate();
        release_temporaries();
        auto const raiseCheck = iProgram.runtime("ada::raise_check", { type::I32, type::String, type::I32, type::String }, type::Void);
        if (std::all_of(iRelocations.begin() + static_cast<std::ptrdiff_t>(firstRelocation), iRelocations.end(),
            [&](auto const& aRelocation) { return aRelocation.second == raiseCheck; }))
            return;
        auto const end = iCode.size();
        auto const mark = local(type::I64);
        runtime("ss_mark", {}, type::I64);
        set(mark);
        auto const length = iCode.size() - end;
        std::rotate(iCode.begin() + static_cast<std::ptrdiff_t>(start), iCode.begin() + static_cast<std::ptrdiff_t>(end), iCode.end());
        for (auto r = firstRelocation; r < iRelocations.size(); ++r)
            iRelocations[r].first = static_cast<std::uint32_t>(iRelocations[r].first >= end ? iRelocations[r].first - (end - start) : iRelocations[r].first + length);
        get(mark);
        runtime("ss_release", { type::I64 }, type::Void);
    }

    void function_generator::assignment(node* aStatement)
    {
        auto* target = aStatement->child(0);
        auto* value = aStatement->child(1);
        auto* t = target->type;
        auto* v = t != nullptr ? t->view() : nullptr;
        if (v != nullptr && v->is_composite())
        {
            // (the value (built in a temporary), then copied to the target)
            address(target);
            auto const targetAddress = local(type::I64);
            set(targetAddress);
            iTargetAddresses.push_back(targetAddress);
            moved_result(value);
            // (an aggregate for a component whose subtype depends on discriminants: of its record's (the target's prefix's))
            bool instance = false;
            if (is_aggregate(value) && target->is(construct::Selected) && meaning_of(target) == meaning::Component && target->ent != nullptr &&
                target->child(0)->type != nullptr)
            {
                auto* rt = target->child(0)->type->view();
                if (target->mark & MarkDeref)
                    rt = iProgram.designated_of(rt) != nullptr ? iProgram.designated_of(rt)->view() : rt;
                auto* rb = rt->base_type()->view();
                if (rb->kind == type_kind::Record)
                    for (auto const& c : rb->components)
                        if (c.ent == target->ent && c.depends)
                        {
                            auto const prefixAddress = local(type::I64);
                            if (target->mark & MarkDeref)
                                expression(target->child(0), target->child(0)->type);
                            else
                                address(target->child(0));
                            set(prefixAddress);
                            iInstances.emplace_back(rb, prefixAddress);
                            instance = true;
                            break;
                        }
            }
            auto const othersChoice = [](node* aAggregate)
            {
                for (auto* item : aAggregate->children)
                    if (item->is(construct::Association))
                        for (auto* choice : item->child(0)->children)
                            if (choice->is(construct::Others))
                                return true;
                return false;
            };
            if (is_aggregate(value) && v->kind == type_kind::Array &&
                (meaning_of(target) == meaning::Slice || othersChoice(value)))
            {
                // (an aggregate assigned to a slice, or with an others choice: built in a temporary of the target's bounds (the 
                // applicable index constraint, 4.3.3(15)))
                auto const dimensions = v->base_type()->view()->indexes.size();
                auto const temp = local(type::I64);
                data_size(v, targetAddress);
                i64(static_cast<std::int64_t>(8u + 16u * dimensions));
                op(opcode::I64Add);
                runtime("ss_allocate", { type::I64 }, type::I64);
                set(temp);
                get(temp);
                data_offset(v, dimensions);
                store64(0u);
                for (std::size_t d = 0u; d < dimensions; ++d)
                {
                    get(temp);
                    bounds(targetAddress, d, true);
                    store64(static_cast<std::uint32_t>(8u + 16u * d));
                    get(temp);
                    bounds(targetAddress, d, false);
                    store64(static_cast<std::uint32_t>(16u + 16u * d));
                }
                aggregate(value, t, temp);
            }
            else if (is_aggregate(value))
                aggregate(value, t);
            else
                expression(value, t);
            if (instance)
                iInstances.pop_back();
            iTargetAddresses.pop_back();
            auto const source = local(type::I64);
            set(source);
            // (the value: satisfies the target's predicate)
            if (t->predicate != nullptr || v->predicate != nullptr)
            {
                get(source);
                predicate_check(t, value->type, value);
                drop();
            }
            if (v->kind == type_kind::Array)
                length_check(targetAddress, source, v->base_type()->view()->indexes.size(), value);
            else if (v->kind == type_kind::Record && !is_runtime_sized(v))
            {
                // (a constrained target: the value's discriminants its (a subtype's constraint, or an indefinite subtype's object's own))
                auto* base = v->base_type()->view();
                bool discriminants = false;
                bool defaults = true;
                for (auto const& c : base->components)
                    if (c.discriminant)
                    {
                        discriminants = true;
                        defaults = defaults && c.default_expression != nullptr;
                    }
                // (an object created by an allocator (a dereference) is constrained by its discriminants (3.10(9/3)))
                bool const dereference = target->is(construct::All);
                if (!v->discriminant_constraint.empty())
                    discriminant_constraint_check(t, source, value);
                else if (discriminants && (!defaults || dereference))
                    discriminant_check(v, targetAddress, source, value);
                else if (auto* formal = target->is(construct::Identifier) ? target->ent : nullptr; formal != nullptr && iUnconstrainedFlags.contains(formal))
                {
                    // (a formal whose actual is constrained (3.7.2))
                    get(iUnconstrainedFlags[formal]);
                    op(opcode::I64Eqz);
                    begin_if();
                    discriminant_check(v, targetAddress, source, value);
                    end();
                }
            }
            if (iProgram.needs_finalization(v))
            {
                // (the target's controlled parts finalized, the value copied, then adjusted (unless it is a new object); not for 
                // an assignment of an object to itself)
                get(targetAddress);
                get(source);
                op(opcode::I64Ne);
                begin_if();
                controlled_finalize(v, targetAddress);
                copy_value(v, targetAddress, source);
                controlled_initialized(v, targetAddress, false, true, false);
                // (a new object (an aggregate's, a function's result): an anonymous object, finalized once assigned (7.6(17), 7.6.1(13/3)))
                if (is_temporary_value(value))
                    controlled_finalize(v, source);
                end();
                return;
            }
            copy_value(v, targetAddress, source);
            return;
        }
        // (an object's name (direct, or an expanded name))
        if ((target->is(construct::Identifier) || (target->is(construct::Selected) && meaning_of(target) == meaning::Object)) && target->ent != nullptr &&
            target->ent->is_object())
        {
            iTargetNames.push_back(target);
            expression(value, t);
            iTargetNames.pop_back();
            range_check(t, value->type, value);
            store_object(target->ent);
            if (iProgram.iAccessLevelObjects.contains(target->ent))
                store_access_level(target->ent, value);
            return;
        }
        address(target);
        auto const targetAddress = local(type::I64);
        set(targetAddress);
        iTargetNames.push_back(target);
        iTargetAddresses.push_back(targetAddress);
        expression(value, t);
        iTargetAddresses.pop_back();
        iTargetNames.pop_back();
        range_check(t, value->type, value);
        auto const val = temporary(wasm_of(t));
        set(val);
        get(targetAddress);
        get(val);
        if (auto const* bitField = bit_field_name(target))
            bit_field_store(*bitField);
        else if (auto const bits = packed_element(target); bits != 0u)
            packed_store(bits);
        else
            store(kind_of(t), iProgram.scalar_size(t));
        release(val);
    }

    void function_generator::if_statement(node* aStatement)
    {
        std::size_t opened = 0u;
        statement_condition(aStatement->child(0));
        begin_if();
        ++opened;
        statements(aStatement->child(1));
        for (std::size_t i = 2u; i < aStatement->size(); ++i)
        {
            auto* c = aStatement->child(i);
            else_();
            if (c->is(construct::Elsif))
            {
                statement_condition(c->child(0));
                begin_if();
                ++opened;
                statements(c->child(1));
            }
            else if (c->is(construct::Else))
                statements(c->child(0));
        }
        while (opened-- > 0u)
            end();
    }

    void function_generator::case_statement(node* aStatement)
    {
        auto* selector = aStatement->child(0);
        auto const value = local(type::I64);
        expression(selector, selector->type);
        set(value);
        release_temporaries();
        begin(control_kind::Block);
        auto const done = iControls.size() - 1u;
        bool anyOthers = false;
        for (auto* alternative : aStatement->all(construct::Alternative))
        {
            bool others = false;
            i32(0);
            for (auto* choice : alternative->child(0)->children)
            {
                if (choice->is(construct::Others))
                {
                    others = true;
                    continue;
                }
                choice_test(choice, value);
                op(opcode::I32Ior);
            }
            if (others)
            {
                drop();
                i32(1);
                anyOthers = true;
            }
            begin_if();
            statements(alternative->first(construct::Statements));
            branch(done);
            end();
        }
        // (no others choice: a value that none covers (e.g. of a subtype whose disabled predicate it doesn't satisfy) raises
        // Constraint_Error (5.4(13)))
        if (!anyOthers)
            raise_check(0, aStatement, "case check failed");
        end();
    }

    void function_generator::choice_test(node* aChoice, std::uint32_t aValue)
    {
        if (aChoice->is(construct::Range))
        {
            get(aValue);
            expression(aChoice->child(0), aChoice->child(0)->type);
            op(opcode::I64GeS);
            get(aValue);
            expression(aChoice->child(1), aChoice->child(1)->type);
            op(opcode::I64LeS);
            op(opcode::I32And);
            return;
        }
        if (aChoice->type != nullptr && (aChoice->is(construct::SubtypeIndication) || meaning_of(aChoice) == meaning::Type ||
            (aChoice->is(construct::Attribute) && name_text(aChoice->child(1)->source) == "range")))
        {
            auto* t = aChoice->type;
            get(aValue);
            scalar_low(t);
            op(opcode::I64GeS);
            get(aValue);
            scalar_high(t);
            op(opcode::I64LeS);
            op(opcode::I32And);
            // (a subtype with a predicate: its values that satisfy it)
            if (auto* predicate = t->predicate != nullptr ? t->predicate : t->view()->predicate)
            {
                begin_if(type::I32);
                predicate_test(predicate, aValue, false);
                else_();
                i32(0);
                end();
            }
            return;
        }
        get(aValue);
        expression(aChoice, aChoice->type);
        op(opcode::I64Eq);
    }

    void function_generator::loop_statement(node* aStatement)
    {
        auto* loopEntity = aStatement->ent;
        auto* forScheme = aStatement->first(construct::For);
        auto* whileScheme = aStatement->first(construct::While);
        auto* body = aStatement->first(construct::Statements);
        if (forScheme != nullptr)
        {
            auto* specification = forScheme->first(construct::ForRange);
            if (specification == nullptr)
            {
                iterator_loop(aStatement, forScheme->first(construct::ForIterator), body);
                return;
            }
            if (specification->mark == 1u)
            {
                generalized_iterator_loop(aStatement, specification, body);
                return;
            }
            auto* parameter = specification->first(construct::Defining)->ent;
            bool const reverse = specification->has_flag("reverse");
            auto* range = parameter->type;
            auto const low = local(type::I64);
            auto const high = local(type::I64);
            compatibility_checks(specification, [&](node const* aNode)
                {
                    for (auto const* n = aNode; n != nullptr; n = n->parent)
                        if (n == specification)
                            return true;
                    return false;
                });
            discrete_bounds(specification, range, low, high);
            release_temporaries();
            // (a subtype with static predicates: from its least to its greatest value that satisfies them)
            if (std::int64_t least = 0, greatest = 0; range != nullptr && (range->predicate != nullptr || range->view()->predicate != nullptr) &&
                static_predicate_bounds(range, least, greatest))
            {
                i64(least);
                get(low);
                get(low);
                i64(least);
                op(opcode::I64LtS);
                op(opcode::Select);
                set(low);
                i64(greatest);
                get(high);
                get(high);
                i64(greatest);
                op(opcode::I64GtS);
                op(opcode::Select);
                set(high);
            }
            parameter->where = storage::None;
            assign_storage(parameter);
            begin(control_kind::Breakable, {}, loopEntity);
            auto const exit = iControls.size() - 1u;
            get(low);
            get(high);
            op(opcode::I64GtS);
            branch(exit, true);
            get(reverse ? high : low);
            store_object(parameter);
            begin(control_kind::Loop);
            auto const head = iControls.size() - 1u;
            // (a subtype with a predicate: only the values that satisfy it (5.5(9/5)))
            auto* predicate = range != nullptr ? (range->predicate != nullptr ? range->predicate : range->view()->predicate) : nullptr;
            if (predicate != nullptr)
            {
                auto const value = local(type::I64);
                load_object(parameter);
                set(value);
                predicate_test(predicate, value, false);
                begin_if();
            }
            if (auto* filter = specification->first(construct::Filter))
            {
                condition(filter->child(0));
                begin_if();
                statements(body);
                end();
            }
            else
                statements(body);
            if (predicate != nullptr)
                end();
            load_object(parameter);
            get(reverse ? low : high);
            op(opcode::I64Eq);
            branch(exit, true);
            load_object(parameter);
            i64(reverse ? -1 : 1);
            op(opcode::I64Add);
            store_object(parameter);
            branch(head);
            end();
            end();
            return;
        }
        begin(control_kind::Breakable, {}, loopEntity);
        auto const exit = iControls.size() - 1u;
        begin(control_kind::Loop);
        auto const head = iControls.size() - 1u;
        if (whileScheme != nullptr)
        {
            statement_condition(whileScheme->child(0));
            op(opcode::I32Eqz);
            branch(exit, true);
        }
        statements(body);
        branch(head);
        end();
        end();
    }

    void function_generator::discrete_bounds(node* aSpecification, ada_type* aRange, std::uint32_t aLow, std::uint32_t aHigh)
    {
        node* range = nullptr;
        for (auto* c : aSpecification->children)
            if (!c->is(construct::Defining) && !c->is(construct::Flag) && !c->is(construct::Filter))
                range = c;
        if (range != nullptr && range->is(construct::SubtypeIndication) && range->size() == 1u && range->child(0)->is(construct::Attribute))
            range = range->child(0);
        // (a subtype indication with a range constraint: the constraint's bounds (evaluated here, they can be dynamic))
        if (range != nullptr && range->is(construct::SubtypeIndication))
            if (auto* constraint = range->first(construct::ConstraintRange); constraint != nullptr && constraint->size() > 0u &&
                (constraint->child(0)->is(construct::Range) || constraint->child(0)->is(construct::Attribute)))
                range = constraint->child(0);
        if (range != nullptr && range->is(construct::Range))
        {
            expression(range->child(0), aRange->base_type());
            set(aLow);
            expression(range->child(1), aRange->base_type());
            set(aHigh);
            return;
        }
        if (range != nullptr && range->is(construct::Attribute) && (name_text(range->child(1)->source) == "range"))
        {
            range_attribute_bounds(range, aLow, aHigh);
            return;
        }
        scalar_low(aRange);
        set(aLow);
        scalar_high(aRange);
        set(aHigh);
    }

    void function_generator::exit_statement(node* aStatement)
    {
        std::size_t target = iControls.size();
        node* when = nullptr;
        std::string name;
        for (auto* c : aStatement->children)
        {
            if (c->is(construct::Word))
                name = name_text(c->source);
            else
                when = c;
        }
        for (std::size_t i = iControls.size(); i-- > 0u;)
            if (iControls[i].kind == control_kind::Breakable && (name.empty() || (iControls[i].loop != nullptr && iControls[i].loop->name == name)))
            {
                target = i;
                break;
            }
        if (target == iControls.size())
            iProgram.error(aStatement, "exit statement not in a loop");
        if (when != nullptr)
        {
            statement_condition(when);
            bool exitActions = iControls[target].finMark >= 0;
            for (auto i = iControls.size(); i-- > target;)
                exitActions = exitActions || iControls[i].onExit != nullptr;
            if (exitActions)
            {
                begin_if();
                leave(target);
                end();
            }
            else
                branch(target, true);
        }
        else
            leave(target);
    }

    void function_generator::block_statement(node* aStatement)
    {
        // (its declarations' storage on the stack and on the secondary stack: released when it ends)
        auto const savedSp = local(type::I64);
        op(opcode::GlobalGet);
        u32(iProgram.sp_global());
        set(savedSp);
        auto const ssMark = local(type::I64);
        runtime("ss_mark", {}, type::I64);
        set(ssMark);
        // (a master of controlled objects: they are finalized when it ends, normally or by an exception (that then continues))
        std::uint32_t finMark = 0u;
        if (iProgram.has_masters())
        {
            finMark = local(type::I64);
            runtime("fin_mark", {}, type::I64);
            set(finMark);
        }
        begin(finMark != 0u ? control_kind::Handler : control_kind::Block);
        if (finMark != 0u)
            iControls.back().finMark = finMark;     // (a goto out of it: see goto_statement)
        auto* handledPart = aStatement->first(construct::Handled);
        auto* decls = aStatement->first(construct::Declarations);
        bool const handlers = handledPart != nullptr && !handledPart->all(construct::Handler).empty();
        (void)handlers;
        // (the tasks its declarative part creates: never activated if an exception is raised before they are (they become
        // terminated, 9.2(3/2)))
        std::optional<std::uint32_t> taskMark;
        if (!iProgram.iTaskUnits.empty() && finMark != 0u && decls != nullptr && !decls->children.empty())
        {
            taskMark = local(type::I64);
            runtime("task_pending", {}, type::I64);
            set(*taskMark);
        }
        deferred_activation_declarations(decls);
        handled(handledPart);
        end();
        if (taskMark)
        {
            get(*taskMark);
            runtime("task_discard_from", { type::I64 }, type::Void);
        }
        if (finMark != 0u)
        {
            get(finMark);
            runtime("fin_release", { type::I64 }, type::Void);
        }
        get(ssMark);
        runtime("ss_release", { type::I64 }, type::Void);
        get(savedSp);
        op(opcode::GlobalSet);
        u32(iProgram.sp_global());
        if (finMark != 0u)
            check();
    }

    void function_generator::return_statement(node* aStatement)
    {
        // (within an accept statement: it completes the accept statement (6.5(22/3)))
        if (!iAcceptControls.empty() && aStatement->is(construct::Return) && aStatement->size() == 0u)
        {
            branch(iAcceptControls.back());
            return;
        }
        // (a class-wide result: the type its tag identifies not deeper than the master that elaborated the function body (6.5(8/5)))
        auto const tagLevelCheck = [&](node const* aWhere)
        {
            auto* rt = iSubprogram->sub->result;
            if (rt == nullptr || rt->view()->kind != type_kind::ClassWide)
                return;
            auto const level = iSubprogram->scope != nullptr ? iProgram.accessibility_level(iSubprogram->scope) : 0;
            auto const r = temporary(type::I64);
            tee(r);
            load64(0u);
            load64(static_cast<std::uint32_t>(program::TagLevel));
            i64(level);
            op(opcode::I64GtS);
            begin_if();
            raise_check(1, aWhere, "accessibility check failed");
            end();
            get(r);
            release(r);
        };
        auto const returnObject = [&](entity* e)
        {
            // (the return object: moved into the result (not finalized by this master))
            if (e->type != nullptr && iProgram.needs_finalization(e->type))
            {
                address_of(e);
                auto const object = local(type::I64);
                set(object);
                controlled_parts(e->type, object, false, [&](ada_type*, std::uint32_t aPart) { controlled_unregister(aPart); });
            }
            // (its tasks: the caller's master's (9.2(2/2), 9.3(2)))
            if (e->type != nullptr && iProgram.needs_tasks(e->type))
            {
                address_of(e);
                auto const object = local(type::I64);
                set(object);
                relocate_tasks(e->type, object, true);
            }
            load_object(e);
            tagLevelCheck(aStatement);
            return_value(iSubprogram->sub->result, false);
        };
        if (aStatement->is(construct::ReturnExtended))
        {
            auto* e = aStatement->ent;
            // (the return object's subtype indication's dynamic bounds and discriminant constraints: elaborated before the object
            // (3.3.1(15)))
            for (auto* c : aStatement->children)
                if (!c->is(construct::Defining) && !c->is(construct::Flag) && !c->is(construct::Aspects) && !c->is(construct::Handled))
                {
                    elaborate_dynamic_bounds(c);
                    break;
                }
            // (a return object with tasks: they are the caller's to activate if it is returned; if the statement is left otherwise
            // (an exception, a goto), they are never activated (9.2(3/2)))
            if (e != nullptr && e->type != nullptr && iProgram.needs_tasks(e->type))
            {
                auto const mark = local(type::I64);
                runtime("task_pending", {}, type::I64);
                set(mark);
                std::function<void()> const discard = [this, mark]()
                {
                    get(mark);
                    runtime("task_discard_from", { type::I64 }, type::Void);
                };
                begin(control_kind::Handler);
                iControls.back().onExit = &discard;
                initialize_object(e);
                iReturnObjects.push_back(e);
                handled(aStatement->first(construct::Handled));
                iReturnObjects.pop_back();
                if (iHasResult)
                    returnObject(e);
                else
                    branch(0u);
                end();
                // (an exception: propagated)
                discard();
                check();
                return;
            }
            initialize_object(e);
            iReturnObjects.push_back(e);
            handled(aStatement->first(construct::Handled));
            iReturnObjects.pop_back();
            if (iHasResult)
                returnObject(e);
            else
                branch(0u);
            return;
        }
        // (a simple return statement within an extended return statement: its return object)
        if (aStatement->size() == 0u && iHasResult && !iReturnObjects.empty())
        {
            returnObject(iReturnObjects.back());
            return;
        }
        if (aStatement->size() > 0u && iHasResult)
        {
            auto* t = iSubprogram->sub->result;
            auto* value = aStatement->child(0);
            moved_result(value);
            if (iDispatchingTag != nullptr && meaning_of(value) == meaning::Call)
            {
                address_of(iDispatchingTag);
                auto const tagAddress = local(type::I64);
                set(tagAddress);
                iDispatchTagAddress = tagAddress;
            }
            if (is_aggregate(value))
                aggregate(value, t);
            else
                expression(value, t);
            iDispatchTagAddress = 0u;
            if (!t->view()->is_composite())
                range_check(t, value->type, value);
            if (iSubprogram->sub->result_not_null && !t->view()->not_null)
            {
                // (a null-excluding result)
                auto const r = temporary(type::I64);
                tee(r);
                get(r);
                op(opcode::I64Eqz);
                begin_if();
                raise_check(0, value, "access check failed");
                end();
                release(r);
            }
            if (t->view()->kind == type_kind::Access && (iSubprogram->sub->result_access || t->view()->anonymous_access))
                if (auto* dd = iProgram.designated_of(t->view()) != nullptr ? iProgram.designated_of(t->view())->view() : nullptr;
                    dd != nullptr && dd->tagged && dd->kind != type_kind::ClassWide)
                {
                    // (an anonymous access result designating a specific tagged type: the designated object (unless null) of that
                    // specific type (6.5(8/5)))
                    auto const r = temporary(type::I64);
                    tee(r);
                    op(opcode::I64Eqz);
                    op(opcode::I32Eqz);
                    begin_if();
                    get(r);
                    load64(0u);
                    i64(static_cast<std::int64_t>(iProgram.tag_of(dd->base_type()->view())));
                    op(opcode::I64Ne);
                    begin_if();
                    raise_check(0, value, "tag check failed");
                    end();
                    end();
                    get(r);
                    release(r);
                }
            tagLevelCheck(value);
            return_value(t, !is_temporary_value(value));
            return;
        }
        branch(0u);
    }

    void function_generator::raise_statement(node* aStatement)
    {
        if (aStatement->size() == 0u)
        {
            if (iOccurrences.empty())
                iProgram.error(aStatement, "raise statement with no exception name outside a handler");
            get(iOccurrences.back());
            runtime("reraise", { type::I64 }, type::Void);
            branch(handler_target());
            return;
        }
        auto* x = aStatement->child(0)->ent;
        while (x->renamed != nullptr)
            x = x->renamed;
        if (aStatement->size() >= 2u)
        {
            i64(static_cast<std::int64_t>(x->exception_id));
            expression(aStatement->child(1), iProgram.string_type());
            runtime("raise", { type::I64, type::I64 }, type::Void);
        }
        else
        {
            i64(static_cast<std::int64_t>(x->exception_id));
            string_constant(file_name(aStatement));
            i32(static_cast<std::int32_t>(aStatement->file != nullptr ? aStatement->file->line_of(aStatement->source) : 0u));
            runtime("raise_at", { type::I64, type::String, type::I32 }, type::Void);
        }
        branch(handler_target());
    }

    void function_generator::failed_assertion(node const* aWhere, std::string const& aWhat)
    {
        if (auto* assertion = iProgram.assertion_error())
        {
            i64(static_cast<std::int64_t>(assertion->exception_id));
            i64(static_cast<std::int64_t>(iProgram.static_string(aWhat + " at " + file_name(aWhere) + ":" +
                std::to_string(aWhere != nullptr && aWhere->file != nullptr ? aWhere->file->line_of(aWhere->source) : 0u))));
            runtime("raise", { type::I64, type::I64 }, type::Void);
            branch(handler_target());
        }
        else
            raise_check(1, aWhere, aWhat);
    }

    void function_generator::assertion(node* aCondition, std::string const& aWhat)
    {
        statement_condition(aCondition);
        op(opcode::I32Eqz);
        begin_if();
        failed_assertion(aCondition, aWhat);
        end();
    }

    void function_generator::pragma_statement(node* aStatement)
    {
        auto const name = name_text(aStatement->child(0)->source);
        if (name == "dispatching_tag")
        {
            // (implementation-defined: pragma Dispatching_Tag (local_name) names a Tag object (aliased) of this subprogram: a
            // tag-indeterminate call returned by it dispatches on that tag (Ada.Tags.Generic_Dispatching_Constructor))
            for (auto* a : aStatement->children)
                if (a->is(construct::PragmaArgument) && a->children.back()->ent != nullptr)
                    iDispatchingTag = a->children.back()->ent;
            return;
        }
        if (name != "assert" && name != "assume" && name != "loop_invariant")
            return;
        if (iProgram.iIgnoredAssertions.contains(aStatement))
            return;
        std::vector<node*> arguments;
        for (auto* a : aStatement->children)
            if (a->is(construct::PragmaArgument))
                arguments.push_back(a->children.back());
        if (arguments.empty())
            return;
        statement_condition(arguments[0]);
        op(opcode::I32Eqz);
        begin_if();
        if (auto* assertion = iProgram.assertion_error())
        {
            i64(static_cast<std::int64_t>(assertion->exception_id));
            if (arguments.size() >= 2u)
                expression(arguments[1], iProgram.string_type());
            else
                i64(static_cast<std::int64_t>(iProgram.static_string("failed assertion at " + file_name(aStatement) + ":" +
                    std::to_string(aStatement->file != nullptr ? aStatement->file->line_of(aStatement->source) : 0u))));
            runtime("raise", { type::I64, type::I64 }, type::Void);
            branch(handler_target());
        }
        else
            raise_check(1, aStatement, "failed assertion");
        end();
    }
}
