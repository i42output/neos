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

// C: code generation: the program (its globals, static data, functions and entry point) and a function's code primitives (its 
// locals, frame, control flow, loads and stores). Expressions are in gen_expr.cpp, statements in gen_stmt.cpp.

#include <neolib/neolib.hpp>
#include <neolib/core/pair.hpp>
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <set>
#include <neos/language/emit.hpp>
#include <neos/language/c_library.hpp>
#include <neos/bytecode/bytecode.hpp>
#include "codegen.hpp"

namespace neos::compilers::c
{
    using namespace emitter;

    namespace
    {
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

        type signature_type(char aCode)
        {
            switch (aCode)
            {
            case 'I': return type::I32;
            case 'L': return type::I64;
            case 'F': return type::F32;
            case 'D': return type::F64;
            default: return type::Void;
            }
        }

        language::c::library_function const* library_function(std::string const& aName)
        {
            static std::map<std::string, language::c::library_function const*> const sFunctions = []()
            {
                std::map<std::string, language::c::library_function const*> result;
                for (auto const& f : language::c::LibraryFunctions)
                    result[f.name] = &f;
                return result;
            }();
            auto const found = sFunctions.find(aName);
            return found != sFunctions.end() ? found->second : nullptr;
        }
    }

    // --- the program

    program::program(compiler& aCompiler, std::vector<translation_unit*> const& aUnits) :
        iCompiler{ aCompiler }, iUnits{ aUnits }
    {
    }

    program::~program() = default;

    void program::error(node const* aWhere, std::string const& aMessage) const
    {
        throw compile_error{ aWhere, aMessage };
    }

    void program::warning(node const* aWhere, std::string const& aMessage) const
    {
        iCompiler.warning(aWhere, aMessage);
    }

    entity* program::function_entity(std::string const& aName) const
    {
        auto const found = iExternals.find(aName);
        return found != iExternals.end() && found->second->kind == entity_kind::Function ? found->second : nullptr;
    }

    void program::generate()
    {
        for (auto* unit : iUnits)
            analyse_unit(*unit);
        auto* main = function_entity("main");
        // (a translation unit of a linked program (see i_code_generator::linked) need not have main: its functions are called by the 
        // code of the program's others)
        if ((main == nullptr || !main->defined) && !gen().linked())
        {
            node const* where = nullptr;
            for (auto* unit : iUnits)
                if (!unit->prelude)
                    where = unit->root;
            error(where, "undefined reference to 'main'");
        }
        assign_static_addresses();
        generate_code();
    }

    type program::wasm(vkind aKind) const
    {
        switch (aKind)
        {
        case vkind::I32: return type::I32;
        case vkind::I64: return type::I64;
        case vkind::F32: return type::F32;
        case vkind::F64: return type::F64;
        default: return type::Void;
        }
    }

    bool program::returns_record(c_type const* aFunctionType) const
    {
        return aFunctionType->base && aFunctionType->base->is_record();
    }

    std::vector<type> program::wasm_parameters(c_type const* aFunctionType) const
    {
        // (a structure or union result's address; the parameters (a structure or union: its address); the variable arguments' address)
        std::vector<type> result;
        if (returns_record(aFunctionType))
            result.push_back(type::I64);
        for (auto const& p : aFunctionType->parameters)
            result.push_back(p.type->is_record() ? type::I64 : wasm(kind_of(p.type)));
        if (aFunctionType->variadic)
            result.push_back(type::I64);
        return result;
    }

    type program::wasm_result(c_type const* aFunctionType) const
    {
        if (returns_record(aFunctionType))
            return type::Void;
        return wasm(kind_of(aFunctionType->base));
    }

    std::uint32_t program::runtime(std::string const& aName, std::vector<type> const& aParameters, type aResult)
    {
        auto const index = gen().runtime_function(neolib::string{ aName }, neolib::vector<type>{ aParameters.begin(), aParameters.end() }, aResult);
        note_function_type(index, aParameters, aResult);
        return index;
    }

    std::uint32_t program::global(std::string const& aName, type aType)
    {
        auto existing = iGlobals.find(aName);
        if (existing != iGlobals.end())
            return existing->second;
        gen().add_global(neolib::string{ aName }, aType);
        gen().function_global(neolib::string{ aName });
        // (C's globals are a thread's state (its stack, its pending longjmp, its thread-local objects): each thread has its own)
        gen().thread_global(neolib::string{ aName });
        auto const index = static_cast<std::uint32_t>(gen().global_count());
        iGlobals[aName] = index;
        note_global_type(index, aType);
        return index;
    }

    std::uint64_t program::static_data(std::uint64_t aSize, std::uint64_t aAlignment)
    {
        aAlignment = std::max<std::uint64_t>(aAlignment, 1u);
        auto offset = (iStatic.size() + aAlignment - 1u) / aAlignment * aAlignment;
        iStatic.resize(offset + std::max<std::uint64_t>(aSize, 1u), 0u);
        return language::c::layout::StaticBase + offset;
    }

    std::uint64_t program::string_literal_address(node* aStrings)
    {
        // (string literals with the same bytes and element type share their array)
        auto const key = aStrings->bytes + std::string(1u, static_cast<char>(size_of(aStrings->type->base)));
        auto const existing = iStringLiterals.find(key);
        if (existing != iStringLiterals.end())
            return existing->second;
        auto const address = static_data(aStrings->bytes.size(), align_of(aStrings->type->base));
        std::memcpy(iStatic.data() + (address - language::c::layout::StaticBase), aStrings->bytes.data(), aStrings->bytes.size());
        iStringLiterals[key] = address;
        return address;
    }

    std::uint64_t program::descriptor_of(entity* aFunction)
    {
        // (a function's address: its descriptor, 8 bytes in the static data that hold its function's index (set by the entry point))
        aFunction = aFunction->self();
        auto* f = aFunction->function;
        if (f->descriptor)
            return *f->descriptor;
        auto const address = static_data(8u, 8u);
        f->descriptor = address;
        iDescriptors.emplace_back(address, aFunction);
        if (!f->defined && !f->runtime && aFunction->definition != nullptr)
        {
            define_function(aFunction);
            iToGenerate.push_back(aFunction);
        }
        return address;
    }

    void program::assign_static_addresses()
    {
        // (thread-local objects: in the thread-local block's template (in the static data, initialized as static objects are: a new thread's
        // block is a copy of it (see the runtime's threads)), at their offsets in a thread's block (whose address is in %c.tls); then the
        // others)
        std::vector<std::pair<entity*, std::uint64_t>> threadLocals;
        std::uint64_t tlsSize = 0u;
        std::uint64_t tlsAlignment = 16u;
        for (int pass = 0; pass < 2; ++pass)
            for (auto* o : iObjects)
            {
                auto* e = o->self();
                if (e != o || e->address != 0u || e->threadLocal != (pass == 0))
                    continue;
                if (!e->defined && !e->tentative)
                    continue;
                auto alignment = align_of(e->type);
                if (e->value > 0 && e->kind == entity_kind::Object)
                    alignment = std::max(alignment, static_cast<std::uint64_t>(e->value));
                auto size = size_of(e->type, e->decl);
                if (e->type->is_record() && e->type->record->flexible && e->initializer != nullptr)
                {
                    // (a structure with a flexible array member initialized: room for the member's initializer (an extension, as GCC's))
                    size += 4096u;
                }
                if (pass == 0)
                {
                    tlsSize = (tlsSize + alignment - 1u) / alignment * alignment;
                    threadLocals.emplace_back(e, tlsSize);
                    tlsSize += std::max<std::uint64_t>(size, 1u);
                    tlsAlignment = std::max(tlsAlignment, alignment);
                    continue;
                }
                e->address = static_data(size, alignment);
            }
        if (!threadLocals.empty())
        {
            iTlsSize = (tlsSize + 15u) & ~std::uint64_t{ 15u };
            iTlsAlignment = tlsAlignment;
            iTlsTemplate = static_data(iTlsSize, tlsAlignment);
            for (auto const& [e, offset] : threadLocals)
                e->address = iTlsTemplate + offset;
        }
    }

    void program::store_constant(std::vector<std::uint8_t>& aImage, std::uint64_t aOffset, qtype aType, constant const& aValue, node const* aWhere)
    {
        auto const size = size_of(aType);
        std::uint64_t bits = 0u;
        if (aType->is_floating())
        {
            double value = aValue.k == constant::kind::Floating ? aValue.floating :
                (aValue.k == constant::kind::Integer ? static_cast<double>(static_cast<std::int64_t>(aValue.integer)) : 0.0);
            if (aType->kind == type_kind::Float)
            {
                float f = static_cast<float>(value);
                std::uint32_t b = 0u;
                std::memcpy(&b, &f, 4u);
                bits = b;
            }
            else
                std::memcpy(&bits, &value, 8u);
        }
        else if (aValue.k == constant::kind::Address)
        {
            bits = aValue.integer;
            if (aValue.object != nullptr)
            {
                auto* target = aValue.object->self();
                if (target->kind == entity_kind::Function)
                    bits += descriptor_of(target);
                else
                {
                    if (target->address == 0u)
                        error(aWhere, "undefined reference to '" + target->name + "'");
                    if (target->threadLocal)
                        error(aWhere, "initializer element is not constant (the address of thread-local '" + target->name + "')");
                    bits += target->address;
                }
            }
            else if (aValue.literal != nullptr)
                bits += string_literal_address(aValue.literal);
        }
        else if (aValue.k == constant::kind::Floating)
            bits = aType->is_signed() ? static_cast<std::uint64_t>(static_cast<std::int64_t>(aValue.floating)) : static_cast<std::uint64_t>(aValue.floating);
        else
            bits = aValue.integer;
        if (aType->kind == type_kind::Bool)
            bits = bits != 0u ? 1u : 0u;
        for (std::uint64_t i = 0u; i < size && i < 8u; ++i)
            aImage[aOffset + i] = static_cast<std::uint8_t>(bits >> (8u * i));
    }

    void program::initialize_static(std::vector<std::uint8_t>& aImage, std::uint64_t aOffset, qtype aType, node* aInitializer)
    {
        // (a static object's initializer: its value's bytes at the object's offset in the image (zero elsewhere))
        initializer_visitor visitor;
        visitor.scalar = [&](qtype aT, node* aValue, std::uint64_t aAt)
        {
            auto v = evaluate(aValue, false);
            if (!v)
                error(aValue, "initializer element is not constant");
            auto const from = value_type(aValue);
            if (aT->is_integer() && v->k == constant::kind::Floating)
            {
                constant c;
                c.k = constant::kind::Integer;
                c.integer = aT->is_signed() ? static_cast<std::uint64_t>(static_cast<std::int64_t>(v->floating)) : static_cast<std::uint64_t>(v->floating);
                v = c;
            }
            else if (aT->is_floating() && v->k == constant::kind::Integer)
            {
                constant c;
                c.k = constant::kind::Floating;
                c.floating = from->is_signed() ? static_cast<double>(v->s()) : static_cast<double>(v->integer);
                v = c;
            }
            else if (aT->kind == type_kind::Bool && v->k == constant::kind::Address)
            {
                constant c;
                c.k = constant::kind::Integer;
                c.integer = (v->object != nullptr || v->literal != nullptr || v->integer != 0u) ? 1u : 0u;
                v = c;
            }
            else if (aT->is_integer() && v->k == constant::kind::Address && size_of(aT) < 8u)
                error(aValue, "initializer element is not computable at load time");
            store_constant(aImage, aAt, aT, *v, aValue);
        };
        visitor.bits = [&](member_info const& aMember, node* aValue, std::uint64_t aAt)
        {
            auto v = evaluate(aValue, false);
            if (!v || v->k == constant::kind::Address)
                error(aValue, "initializer element is not constant");
            if (v->k == constant::kind::Floating)
                v->integer = static_cast<std::uint64_t>(static_cast<std::int64_t>(v->floating));
            // (the field's bits, from its storage unit's bit bitOffset)
            for (std::uint32_t b = 0u; b < *aMember.bitWidth; ++b)
            {
                auto const position = aMember.bitOffset + b;
                auto const at = aAt + position / 8u;
                if (at >= aImage.size())
                    break;
                auto const bit = static_cast<std::uint8_t>(1u << (position % 8u));
                if (b < 64u && ((v->integer >> b) & 1u) != 0u)
                    aImage[at] |= bit;
                else
                    aImage[at] &= static_cast<std::uint8_t>(~bit);
            }
        };
        visitor.characters = [&](qtype aT, node* aStrings, std::uint64_t aAt)
        {
            auto const& bytes = aStrings->bytes;
            auto const n = std::min<std::uint64_t>(bytes.size(), aT->length ? *aT->length * size_of(aT->base) : bytes.size());
            if (aImage.size() < aAt + n)
                aImage.resize(aAt + n, 0u);
            std::memcpy(aImage.data() + aAt, bytes.data(), n);
        };
        visitor.record = [&](qtype aT, node* aValue, std::uint64_t aAt)
        {
            // (a record from an expression of its type: a compound literal's, or a constexpr object's)
            auto* x = aValue;
            while (x->is(construct::Paren))
                x = x->child(0);
            if (x->is(construct::CompoundLiteral) && x->ent != nullptr && x->ent->initializer != nullptr)
                return initialize_static(aImage, aAt, aT, x->ent->initializer);
            if (x->is(construct::Identifier) && x->ent != nullptr && x->ent->isConstexpr && x->ent->self()->initializer != nullptr)
                return initialize_static(aImage, aAt, aT, x->ent->self()->initializer);
            error(aValue, "initializer element is not constant");
        };
        walk_initializer(aType, aInitializer, aOffset, visitor);
    }

    void program::static_initializer(entity* aObject)
    {
        auto* e = aObject;
        if (e->address == 0u)
            return;
        auto const offset = e->address - language::c::layout::StaticBase;
        if (e->name == "__func__" && e->definition != nullptr && e->definition->is(construct::Strings))
        {
            std::memcpy(iStatic.data() + offset, e->definition->source.data(), e->definition->source.size());
            return;
        }
        if (e->initializer == nullptr)
            return;
        // (the image may grow (string literals, descriptors) while the initializer is evaluated: a copy of the object's part)
        std::vector<std::uint8_t> image(size_of(e->type) + (e->type->is_record() && e->type->record->flexible ? 4096u : 0u), 0u);
        initialize_static(image, 0u, e->type, e->initializer);
        if (iStatic.size() < offset + image.size())
            iStatic.resize(offset + image.size(), 0u);
        std::memcpy(iStatic.data() + offset, image.data(), image.size());
    }

    void program::define_function(entity* aFunction)
    {
        auto* f = aFunction->function;
        if (f->defined)
            return;
        function_signature signature;
        function_parameters parameters;
        for (auto t : wasm_parameters(aFunction->type.type))
        {
            function_parameter p;
            p.set_parameter_type(t);
            parameters.push_back(p);
        }
        signature.set_parameters(parameters);
        signature.set_return_type(wasm_result(aFunction->type.type));
        auto const name = exported(aFunction) ? aFunction->name : "%c." + aFunction->name + "." + std::to_string(iFunctionCount++);
        if (exported(aFunction))
            comp().linked_functions().insert(name);
        signature.set_name(neolib::string{ name });
        std::uint32_t line = 1u;
        if (aFunction->definition != nullptr && aFunction->definition->unit != nullptr)
            line = aFunction->definition->unit->location_of(aFunction->definition->source).line;
        f->index = gen().define_function(neolib::string{ name }, signature, neolib::vector<type>{}, neolib::string{ aFunction->name }, line);
        note_function_type(f->index, wasm_parameters(aFunction->type.type), wasm_result(aFunction->type.type));
        iCompiler.frame_names()[f->index] = aFunction->name;
        f->defined = true;
    }

    bool program::exported(entity* aFunction)
    {
        // (a function with external linkage that a translation unit of a linked program (see i_code_generator::linked) defines (not the 
        // library's, and not main: the unit's entry point calls it): the code of the program's other translation units can call it by 
        // its name (its WebAssembly function's: see bytecode::LinksSection))
        return gen().linked() && aFunction->kind == entity_kind::Function && aFunction->linkage == linkage::External && 
            aFunction->definition != nullptr && aFunction->name != "main" && aFunction->definition->unit != nullptr && 
            !aFunction->definition->unit->prelude;
    }

    void program::linked_object(entity* aObject)
    {
        // (an object with external linkage: a function named '&' and its name, with no parameters, whose result is its address (an 
        // offset in the C memory: the shared memory (see neos::language::c::layout)), so that the code of a linked program's other 
        // translation units can use it by its name (see function_generator::object_address))
        auto const name = "&" + aObject->name;
        function_signature signature;
        signature.set_parameters(function_parameters{});
        signature.set_return_type(type::I64);
        signature.set_name(neolib::string{ name });
        auto const index = gen().define_function(neolib::string{ name }, signature, neolib::vector<type>{}, neolib::string{ name }, 1u);
        note_function_type(index, {}, type::I64);
        comp().linked_functions().insert(name);
        std::vector<std::uint8_t> code;
        put_op(code, opcode::I64Const);
        put_s64(code, static_cast<std::int64_t>(aObject->address));
        put_op(code, opcode::End);
        fragment body;
        body.fragmentCode.as_std_vector() = code;
        gen().set_function_body(index, body);
        gen().set_function_locals(index, neolib::vector<type>{});
    }

    std::uint32_t program::function_index(entity* aFunction)
    {
        aFunction = aFunction->self();
        auto* f = aFunction->function;
        if (f->runtime)
            return f->index;
        if (!f->defined)
        {
            if (aFunction->definition == nullptr)
            {
                auto const params = wasm_parameters(aFunction->type.type);
                auto const result = wasm_result(aFunction->type.type);
                auto const external = iExternals.find(aFunction->name);
                if (external != iExternals.end() && external->second->self() != aFunction && external->second->self()->kind == entity_kind::Function)
                {
                    // (a unit's declaration of a function that conflicts with another's (see program::declare): its calls adapted to 
                    // the function's type)
                    auto* target = external->second->self();
                    auto const targetIndex = function_index(target);
                    auto const* tf = target->function;
                    std::vector<type> targetParams;
                    type targetResult;
                    if (tf->runtime)
                    {
                        auto const* lf = library_function(target->name);
                        std::string const signature = lf->signature;
                        for (std::size_t i = signature.find(':') + 1u; i < signature.size(); ++i)
                            targetParams.push_back(signature_type(signature[i]));
                        targetResult = signature_type(signature[0]);
                    }
                    else
                    {
                        targetParams = wasm_parameters(target->type.type);
                        targetResult = wasm_result(target->type.type);
                    }
                    f->index = adapter(aFunction->name, params, result, targetIndex, targetParams, targetResult);
                    f->defined = true;
                    f->generated = true;
                    return f->index;
                }
                // (a function of the C library that the runtime implements)
                auto const* lf = library_function(aFunction->name);
                if (lf == nullptr && gen().linked())
                {
                    // (a function that another translation unit of the linked program defines: an import of its name (see 
                    // bytecode::LinkNamespace))
                    f->index = runtime(std::string{ bytecode::LinkNamespace } + "::" + aFunction->name, params, result);
                    f->linked = true;
                    f->defined = true;
                    f->generated = true;
                    return f->index;
                }
                if (lf == nullptr)
                    error(aFunction->decl, "undefined reference to '" + aFunction->name + "'");
                std::string const signature = lf->signature;
                auto const colon = signature.find(':');
                std::vector<type> libraryParams;
                for (std::size_t i = colon + 1u; i < signature.size(); ++i)
                    libraryParams.push_back(signature_type(signature[i]));
                auto const libraryResult = signature_type(signature[0]);
                auto const index = runtime(std::string{ "c::libc." } + aFunction->name, libraryParams, libraryResult);
                if (libraryParams != params || libraryResult != result)
                {
                    // (declared with a type that is not the C library's: its calls adapted)
                    warning(aFunction->decl, "declaration of '" + aFunction->name + "' does not match the C library's");
                    f->index = adapter(aFunction->name, params, result, index, libraryParams, libraryResult);
                    f->defined = true;
                    f->generated = true;
                    return f->index;
                }
                f->runtime = true;
                f->runtimeName = std::string{ "c::libc." } + aFunction->name;
                f->index = index;
                f->defined = true;
                return f->index;
            }
            define_function(aFunction);
            iToGenerate.push_back(aFunction);
        }
        return f->index;
    }

    std::uint32_t program::unprototyped_call(entity* aFunction, std::vector<type> const& aArguments)
    {
        // (a function without a prototype anywhere (so not defined: one of the C library's): called by an adapter that converts the 
        // arguments to the function's parameters (one with variable arguments cannot be called this way))
        aFunction = aFunction->self();
        auto const* lf = library_function(aFunction->name);
        if (lf == nullptr || aFunction->definition != nullptr)
            error(aFunction->decl, "undefined reference to '" + aFunction->name + "'");
        std::string const signature = lf->signature;
        std::vector<type> params;
        for (std::size_t i = signature.find(':') + 1u; i < signature.size(); ++i)
            params.push_back(signature_type(signature[i]));
        auto const result = signature_type(signature[0]);
        auto const key = aFunction->name + ":" + std::to_string(aArguments.size()) + ":" + 
            std::string(reinterpret_cast<char const*>(aArguments.data()), aArguments.size() * sizeof(type));
        auto existing = iUnprototypedCalls.find(key);
        if (existing != iUnprototypedCalls.end())
            return existing->second;
        auto const index = runtime(std::string{ "c::libc." } + aFunction->name, params, result);
        auto const wanted = wasm_result(aFunction->type.type);
        return iUnprototypedCalls[key] = adapter(aFunction->name, aArguments, wanted, index, params, result);
    }

    std::uint32_t program::descriptor_target(entity* aFunction)
    {
        aFunction = aFunction->self();
        auto const index = function_index(aFunction);
        auto* f = aFunction->function;
        if (!f->runtime && !f->linked)
            return index;
        // (a function of the runtime's (or an import) cannot be called indirectly: a function that calls it)
        if (!f->wrapper)
        {
            auto const params = wasm_parameters(aFunction->type.type);
            auto const result = wasm_result(aFunction->type.type);
            f->wrapper = adapter(aFunction->name, params, result, index, params, result);
        }
        return *f->wrapper;
    }

    std::uint32_t program::adapter(std::string const& aName, std::vector<type> const& aParameters, type aResult, std::uint32_t aTarget,
        std::vector<type> const& aTargetParameters, type aTargetResult)
    {
        function_signature signature;
        function_parameters parameters;
        for (auto t : aParameters)
        {
            function_parameter p;
            p.set_parameter_type(t);
            parameters.push_back(p);
        }
        signature.set_parameters(parameters);
        signature.set_return_type(aResult);
        auto const name = "%c." + aName + "." + std::to_string(iFunctionCount++);
        signature.set_name(neolib::string{ name });
        auto const index = gen().define_function(neolib::string{ name }, signature, neolib::vector<type>{}, neolib::string{ aName }, 1u);
        note_function_type(index, aParameters, aResult);
        iCompiler.frame_names()[index] = aName;
        // (a value converted from one representation to another: integers sign extended or truncated, floating values converted)
        std::vector<std::uint8_t> code;
        auto const convert = [&](type aFrom, type aTo)
        {
            if (aFrom == aTo)
                return;
            if (aFrom == type::I32 && aTo == type::I64)
                put_op(code, opcode::I64SConvertI32);
            else if (aFrom == type::I64 && aTo == type::I32)
                put_op(code, opcode::I32ConvertI64);
            else if (aFrom == type::F32 && aTo == type::F64)
                put_op(code, opcode::F64ConvertF32);
            else if (aFrom == type::F64 && aTo == type::F32)
                put_op(code, opcode::F32ConvertF64);
            else if (aFrom == type::I32 && aTo == type::F64)
                put_op(code, opcode::F64SConvertI32);
            else if (aFrom == type::I64 && aTo == type::F64)
                put_op(code, opcode::F64SConvertI64);
            else if (aFrom == type::F64 && aTo == type::I32)
                put_op(code, opcode::I32SConvertF64);
            else if (aFrom == type::F64 && aTo == type::I64)
                put_op(code, opcode::I64SConvertF64);
            else
            {
                // (no sensible conversion: the value replaced by zero)
                put_op(code, opcode::Drop);
                switch (aTo)
                {
                case type::I32: put_op(code, opcode::I32Const); put_s64(code, 0); break;
                case type::I64: put_op(code, opcode::I64Const); put_s64(code, 0); break;
                case type::F32: put_op(code, opcode::F32Const); put_raw(code, 0.0f); break;
                case type::F64: put_op(code, opcode::F64Const); put_raw(code, 0.0); break;
                default: break;
                }
            }
        };
        for (std::size_t i = 0u; i < aTargetParameters.size(); ++i)
        {
            if (i < aParameters.size())
            {
                put_op(code, opcode::LocalGet);
                put_u32(code, static_cast<std::uint32_t>(i));
                convert(aParameters[i], aTargetParameters[i]);
            }
            else
            {
                put_op(code, opcode::I32Const);
                put_s64(code, 0);
                convert(type::I32, aTargetParameters[i]);
            }
        }
        put_op(code, opcode::CallFunction);
        std::vector<std::pair<std::uint32_t, std::uint32_t>> relocations;
        relocations.emplace_back(static_cast<std::uint32_t>(code.size()), aTarget);
        code.resize(code.size() + 5u);
        if (aTargetResult != type::Void && aResult == type::Void)
            put_op(code, opcode::Drop);
        else if (aTargetResult == type::Void && aResult != type::Void)
        {
            put_op(code, opcode::I32Const);
            put_s64(code, 0);
            convert(type::I32, aResult);
        }
        else if (aResult != type::Void)
            convert(aTargetResult, aResult);
        put_op(code, opcode::End);
        fragment body;
        body.fragmentCode.as_std_vector() = code;
        for (auto const& r : relocations)
            body.fragmentRelocations.push_back(neolib::pair<std::uint32_t, std::uint32_t>{ r.first, r.second });
        gen().set_function_body(index, body);
        gen().set_function_locals(index, neolib::vector<type>{});
        return index;
    }

    void program::generate_code()
    {
        iSpGlobal = global("%c.sp", type::I64);
        iLimitGlobal = global("%c.limit", type::I64);
        iJumpGlobal = global("%c.jmp", type::I64);
        iJumpValueGlobal = global("%c.jmpval", type::I64);
        iTlsGlobal = global("%c.tls", type::I64);
        // (the functions: main's, then those that it uses (directly or not), defined as their calls are generated)
        auto* main = function_entity("main");
        bool const hasMain = main != nullptr && main->defined;
        if (hasMain)
            function_index(main);
        // (a linked program (see i_code_generator::linked): the functions with external linkage that its units define, which the code
        // of the program's other translation units can call by their names, and its objects with external linkage (a function named 
        // '&' and the object's name: its address (see linked_object)))
        if (gen().linked())
        {
            std::vector<std::string> names;
            for (auto const& external : iExternals)
                names.push_back(external.first);
            std::sort(names.begin(), names.end());
            for (auto const& name : names)
            {
                auto* e = iExternals[name]->self();
                if (e->kind == entity_kind::Function && exported(e))
                    function_index(e);
                else if (e->kind == entity_kind::Object && e->address != 0u && !e->threadLocal && e->unit != nullptr && !e->unit->prelude)
                    linked_object(e);
            }
        }
        // (gnu::constructor and gnu::destructor functions: in the order of their priorities (then of their declarations))
        auto const by_priority = [](auto const& aA, auto const& aB) { return aA.first < aB.first; };
        std::stable_sort(iConstructors.begin(), iConstructors.end(), by_priority);
        std::stable_sort(iDestructors.begin(), iDestructors.end(), by_priority);
        for (auto const* list : { &iConstructors, &iDestructors })
            for (auto const& [priority, f] : *list)
            {
                if (!f->defined || f->definition == nullptr)
                    error(f->decl, "undefined reference to '" + f->name + "' (a constructor or destructor function)");
                if (list == &iDestructors && (!f->type->base->is_void() || (!f->type->parameters.empty() || f->type->variadic)))
                    error(f->decl, "destructor function '" + f->name + "' must return void and take no arguments");
                if (list == &iConstructors)
                    function_index(f);
                else
                    descriptor_of(f);
            }
        // (the static objects' data (their initializers may use functions' addresses and string literals))
        for (auto* o : iObjects)
            if (o->self() == o)
                static_initializer(o);
        // (the program's own thread's block of thread-local objects: a copy of the template)
        if (iTlsSize != 0u)
        {
            iTlsMain = static_data(iTlsSize, iTlsAlignment);
            auto const from = iTlsTemplate - language::c::layout::StaticBase;
            std::copy(iStatic.begin() + static_cast<std::ptrdiff_t>(from), iStatic.begin() + static_cast<std::ptrdiff_t>(from + iTlsSize),
                iStatic.begin() + static_cast<std::ptrdiff_t>(iTlsMain - language::c::layout::StaticBase));
        }
        for (std::size_t i = 0u; i < iToGenerate.size(); ++i)
        {
            auto* f = iToGenerate[i];
            if (f->function->generated)
                continue;
            f->function->generated = true;
            // (the structured method; if the function's control flow cannot be structured: the dispatch method (see 
            // function_generator::dispatch_body))
            // (the automatic objects' storage assigned again, for another generation)
            auto const reset = [&]()
            {
                for (auto* l : f->function->locals)
                    if (l->where != object_storage::Static)
                        l->where = object_storage::Unassigned;
            };
            bool dispatch = f->function->usesSetjmp || f->function->computedGoto;
            if (!dispatch)
            {
                try
                {
                    function_generator g{ *this, f };
                    g.generate();
                }
                catch (dispatch_needed const&)
                {
                    dispatch = true;
                    reset();
                }
            }
            if (dispatch)
            {
                // (the dispatch method: a generation that plans its blocks, then the generation with the plan)
                function_generator::dispatch_plan plan;
                {
                    function_generator planning{ *this, f, true, &plan, true };
                    planning.generate();
                }
                reset();
                function_generator g{ *this, f, true, &plan, false };
                g.generate();
            }
        }
        // the entry point: the runtime initialized (the stack, the heap after the static data), the static data, the functions' 
        // descriptors; then main called with its arguments, and the program ended with its result (exit's: the functions that 
        // atexit registered called, the streams flushed)
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
            call_index(runtime("c::rt." + aName, aParameters, aResult));
        };
        auto const i32const = [&](std::int32_t aValue) { put_op(code, opcode::I32Const); put_s64(code, aValue); };
        auto const i64const = [&](std::int64_t aValue) { put_op(code, opcode::I64Const); put_s64(code, aValue); };
        auto const strconst = [&](std::string const& aText) { i32const(static_cast<std::int32_t>(gen().string_literal(neolib::string{ aText }))); };
        i64const(static_cast<std::int64_t>(language::c::layout::StaticBase + iStatic.size()));
        i32const(static_cast<std::int32_t>(iSpGlobal));
        i32const(static_cast<std::int32_t>(iLimitGlobal));
        call("init", { type::I64, type::I32, type::I32 }, type::Void);
        // (the static data: its runs of bytes that are not zero (the memory is zeroed))
        {
            static char const sHex[] = "0123456789abcdef";
            std::size_t i = 0u;
            while (i < iStatic.size())
            {
                if (iStatic[i] == 0u)
                {
                    ++i;
                    continue;
                }
                std::size_t end = i;
                std::size_t zeros = 0u;
                while (end < iStatic.size() && zeros < 16u)
                {
                    zeros = iStatic[end] == 0u ? zeros + 1u : 0u;
                    ++end;
                }
                end -= zeros;
                std::string hex;
                hex.reserve((end - i) * 2u);
                for (std::size_t k = i; k < end; ++k)
                {
                    hex += sHex[iStatic[k] >> 4u];
                    hex += sHex[iStatic[k] & 15u];
                }
                i64const(static_cast<std::int64_t>(language::c::layout::StaticBase + i));
                strconst(hex);
                call("init_data", { type::I64, type::String }, type::Void);
                i = end;
            }
        }
        for (auto const& [address, function] : iDescriptors)
        {
            i64const(static_cast<std::int64_t>(address));
            put_op(code, opcode::I32Const);
            statement.fragmentRelocations.push_back(neolib::pair<std::uint32_t, std::uint32_t>{ static_cast<std::uint32_t>(code.size()), descriptor_target(function) });
            code.resize(code.size() + 5u);
            put_op(code, opcode::I64UConvertI32);
            put_op(code, opcode::I64StoreMem);
            put_u32(code, 0x43u);
            put_u32(code, language::c::layout::Memory);
            put_u32(code, 0u);
        }
        // (thread-local objects: the program's thread's block, and the template that other threads' blocks are copies of (see the 
        // runtime's threads))
        if (iTlsSize != 0u)
        {
            i64const(static_cast<std::int64_t>(iTlsMain));
            put_op(code, opcode::GlobalSet);
            put_u32(code, iTlsGlobal);
            i64const(static_cast<std::int64_t>(iTlsTemplate));
            i64const(static_cast<std::int64_t>(iTlsSize));
            i32const(static_cast<std::int32_t>(iTlsGlobal));
            call("tls", { type::I64, type::I64, type::I32 }, type::Void);
        }
        // (the constructor functions called; the destructor functions registered (as atexit's: called after those registered later, 
        // in the reverse order))
        for (auto const& [priority, f] : iConstructors)
        {
            auto const* ft = f->type.type;
            if (!ft->parameters.empty() || ft->variadic || returns_record(ft))
                error(f->decl, "constructor function '" + f->name + "' must take no arguments");
            call_index(function_index(f));
            if (!ft->base->is_void())
                put_op(code, opcode::Drop);
        }
        for (auto const& [priority, f] : iDestructors)
        {
            i64const(static_cast<std::int64_t>(descriptor_of(f)));
            call_index(runtime("c::libc.atexit", { type::I64 }, type::I32));
            put_op(code, opcode::Drop);
        }
        // (a linked program's translation unit without main: its entry point initializes it (its functions are called later, by
        // the code of the program's other translation units))
        if (!hasMain)
        {
            iCompiler.emit_entry(statement);
            return;
        }
        // (main: int main(void), int main(int, char**), or int main(int, char**, char**))
        auto const* mt = main->type.type;
        auto const parameters = mt->parameters.size();
        if (parameters >= 1u)
            call("argc", {}, type::I32);
        if (parameters >= 2u)
            call("argv", {}, type::I64);
        if (parameters >= 3u)
            call("envp", {}, type::I64);
        if (mt->variadic)
            i64const(0);
        call_index(function_index(main));
        if (mt->base->is_void())
            i32const(0);
        else if (kind_of(mt->base) == vkind::I64)
            put_op(code, opcode::I32ConvertI64);
        call("exit_prepare", { type::I32 }, type::I32);
        call_index(runtime("neos::exit", { type::I32 }, type::Void));
        iCompiler.emit_entry(statement);
    }

    // --- a function's code

    function_generator::function_generator(program& aProgram, entity* aFunction, bool aDispatch, dispatch_plan* aPlan, bool aPlanning) :
        iProgram{ aProgram }, iFunction{ aFunction }, iInfo{ *aFunction->function }, iType{ aFunction->type.type }, iDispatch{ aDispatch }, 
        iPlan{ aPlan }, iPlanning{ aPlanning }
    {
        iParameters = iProgram.wasm_parameters(iType);
        iReturnsRecord = iProgram.returns_record(iType);
    }

    void function_generator::op(opcode aOpcode)
    {
        put_op(iCode, aOpcode);
    }

    void function_generator::u32(std::uint32_t aValue)
    {
        put_u32(iCode, aValue);
    }

    void function_generator::i32(std::int32_t aValue)
    {
        op(opcode::I32Const);
        put_s64(iCode, aValue);
    }

    void function_generator::i64(std::int64_t aValue)
    {
        op(opcode::I64Const);
        put_s64(iCode, aValue);
    }

    void function_generator::f32(float aValue)
    {
        op(opcode::F32Const);
        put_raw(iCode, aValue);
    }

    void function_generator::f64(double aValue)
    {
        op(opcode::F64Const);
        put_raw(iCode, aValue);
    }

    void function_generator::get(std::uint32_t aLocal)
    {
        op(opcode::LocalGet);
        u32(aLocal);
    }

    void function_generator::set(std::uint32_t aLocal)
    {
        op(opcode::LocalSet);
        u32(aLocal);
    }

    void function_generator::tee(std::uint32_t aLocal)
    {
        op(opcode::LocalTee);
        u32(aLocal);
    }

    void function_generator::drop()
    {
        op(opcode::Drop);
    }

    std::uint32_t function_generator::local(type aType)
    {
        iLocals.push_back(aType);
        return static_cast<std::uint32_t>(iParameters.size() + iLocals.size() - 1u);
    }

    std::uint32_t function_generator::temporary(type aType)
    {
        auto const found = iFreeTemporaries.find(aType);
        if (found != iFreeTemporaries.end())
        {
            auto const result = found->second;
            iFreeTemporaries.erase(found);
            return result;
        }
        return local(aType);
    }

    void function_generator::release(std::uint32_t aLocal)
    {
        if (aLocal < iParameters.size())
            return;
        iFreeTemporaries.emplace(iLocals[aLocal - iParameters.size()], aLocal);
    }

    void function_generator::call(std::uint32_t aFunction)
    {
        op(opcode::CallFunction);
        iRelocations.emplace_back(static_cast<std::uint32_t>(iCode.size()), aFunction);
        iCode.resize(iCode.size() + 5u);
        jump_check();
    }

    void function_generator::jump_check()
    {
        // (a program that uses longjmp: a call may return with a jump pending: to a setjmp of this function's (this activation's): 
        // the block after it resumed; else (the jump's target is further out) this function returns too)
        if (!iProgram.uses_longjmp())
            return;
        op(opcode::GlobalGet);
        u32(iProgram.jump_global());
        op(opcode::I64Const);
        put_s64(iCode, 0);
        op(opcode::I64Ne);
        begin_if();
        if (iDispatch && iInfo.usesSetjmp)
        {
            qtype const longType{ iProgram.basic(type_kind::Long) };
            op(opcode::GlobalGet);
            u32(iProgram.jump_global());
            load(longType, 0u);
            get(iFramePointer);
            op(opcode::I64Eq);
            begin_if();
            op(opcode::GlobalGet);
            u32(iProgram.jump_value_global());
            op(opcode::I32ConvertI64);
            set(iSetjmpValue);
            op(opcode::GlobalGet);
            u32(iProgram.jump_global());
            load(longType, 16u);
            op(opcode::GlobalSet);
            u32(iProgram.sp_global());
            op(opcode::GlobalGet);
            u32(iProgram.jump_global());
            load(longType, 8u);
            op(opcode::I32ConvertI64);
            set(iContexts.front().state);
            i64(0);
            op(opcode::GlobalSet);
            u32(iProgram.jump_global());
            branch_to(iContexts.front().loop);
            end();
        }
        unwind_return();
        end();
    }

    void function_generator::unwind_return()
    {
        // (the result: a zero of its type)
        switch (iProgram.wasm_result(iType))
        {
        case type::I32: i32(0); break;
        case type::I64: i64(0); break;
        case type::F32: f32(0.0f); break;
        case type::F64: f64(0.0); break;
        default: break;
        }
        get(iEntryStack);
        op(opcode::GlobalSet);
        u32(iProgram.sp_global());
        op(opcode::Return);
    }

    void function_generator::call_indirect(std::vector<type> const& aParameters, type aResult)
    {
        auto const signature = iProgram.gen().indirect_signature(neolib::vector<type>{ aParameters.begin(), aParameters.end() }, aResult);
        iProgram.note_indirect_type(signature, aParameters, aResult);
        op(opcode::CallIndirect);
        iRelocations.emplace_back(static_cast<std::uint32_t>(iCode.size()), IndirectSignature | signature);
        iCode.resize(iCode.size() + 5u);
        iCode.push_back(0x00u);
        jump_check();
    }

    void function_generator::runtime(std::string const& aName, std::vector<type> const& aParameters, type aResult)
    {
        op(opcode::CallFunction);
        iRelocations.emplace_back(static_cast<std::uint32_t>(iCode.size()), iProgram.runtime(aName, aParameters, aResult));
        iCode.resize(iCode.size() + 5u);
    }

    void function_generator::libc(std::string const& aName)
    {
        auto const* lf = library_function(aName);
        std::string const signature = lf->signature;
        std::vector<type> params;
        for (std::size_t i = signature.find(':') + 1u; i < signature.size(); ++i)
            params.push_back(signature_type(signature[i]));
        runtime("c::libc." + aName, params, signature_type(signature[0]));
    }

    void function_generator::function_index(std::uint32_t aFunction)
    {
        op(opcode::I32Const);
        iRelocations.emplace_back(static_cast<std::uint32_t>(iCode.size()), aFunction);
        iCode.resize(iCode.size() + 5u);
    }

    void function_generator::load(qtype aType, std::uint32_t aOffset)
    {
        if (atomic_object(aType))
        {
            atomic_load(aType, aOffset);
            return;
        }
        if (aType->is_aggregate() || aType->is_function())
        {
            if (aOffset != 0u)
            {
                i64(aOffset);
                op(opcode::I64Add);
            }
            return;
        }
        opcode o = opcode::I32LoadMem;
        std::uint32_t align = 2u;
        auto const size = iProgram.size_of(aType);
        auto const* t = iProgram.integer_type(aType.type);
        bool const isSigned = t->is_signed();
        switch (kind_of(aType))
        {
        case vkind::F32: o = opcode::F32LoadMem; align = 2u; break;
        case vkind::F64: o = opcode::F64LoadMem; align = 3u; break;
        case vkind::I64: o = opcode::I64LoadMem; align = 3u; break;
        default:
            switch (size)
            {
            case 1u: o = isSigned ? opcode::I32LoadMem8S : opcode::I32LoadMem8U; align = 0u; break;
            case 2u: o = isSigned ? opcode::I32LoadMem16S : opcode::I32LoadMem16U; align = 1u; break;
            default: o = opcode::I32LoadMem; align = 2u; break;
            }
            break;
        }
        op(o);
        u32(0x40u | align);
        u32(language::c::layout::Memory);
        u32(aOffset);
        // (a bit-precise integer narrower than its storage, and bool: as stored (normalized when stored))
    }

    // --- atomic accesses

    bool function_generator::atomic_object(qtype aType) const
    {
        return (aType.qualifiers & QAtomic) != 0u && aType->is_scalar();
    }

    type function_generator::bits_of(qtype aType) const
    {
        return iProgram.size_of(aType) == 8u ? type::I64 : type::I32;
    }

    void function_generator::to_bits(qtype aType)
    {
        if (aType->kind == type_kind::Float)
            op(opcode::I32ReinterpretF32);
        else if (aType->is_floating())
            op(opcode::I64ReinterpretF64);
    }

    void function_generator::from_bits(qtype aType)
    {
        if (aType->kind == type_kind::Float)
            op(opcode::F32ReinterpretI32);
        else if (aType->is_floating())
            op(opcode::F64ReinterpretI64);
        else if (iProgram.size_of(aType) < 4u)
            normalize(aType);   // (a narrower integer: zero extended by the access)
    }

    void function_generator::atomic(std::uint32_t aBase, qtype aType, std::uint32_t aOffset)
    {
        // (the width's instruction of the group: i32, i64, i32 8 bits, i32 16 bits (i64's narrower ones are not used))
        auto const size = iProgram.size_of(aType);
        std::uint32_t k = 0u;
        std::uint32_t align = 2u;
        switch (size)
        {
        case 1u: k = 2u; align = 0u; break;
        case 2u: k = 3u; align = 1u; break;
        case 4u: k = 0u; align = 2u; break;
        case 8u: k = 1u; align = 3u; break;
        default:
            throw compile_error{ nullptr, "an atomic object of " + std::to_string(size) + " bytes (that is not lock free) is not supported" };
        }
        iCode.push_back(0xFEu);
        u32(aBase + k);
        u32(0x40u | align);
        u32(language::c::layout::Memory);
        u32(aOffset);
    }

    void function_generator::atomic_load(qtype aType, std::uint32_t aOffset)
    {
        atomic(0x10u, aType, aOffset);
        from_bits(aType);
    }

    void function_generator::atomic_store(qtype aType)
    {
        to_bits(aType);
        atomic(0x17u, aType);
    }

    void function_generator::atomic_update(qtype aType, std::uint32_t aWhere, std::function<void()> const& aCompute, bool aValueUsed, bool aOld)
    {
        // (old = *where; loop: new = compute(old); seen = cmpxchg(where, old, new); if seen != old: old = seen, again)
        auto const bits = bits_of(aType);
        auto const old = temporary(bits);
        auto const neu = temporary(bits);
        auto const seen = temporary(bits);
        get(aWhere);
        atomic(0x10u, aType);
        if (iProgram.size_of(aType) < 4u)
            normalize(aType);
        set(old);
        begin(control_kind::Loop);
        auto const top = iControls.size() - 1u;
        get(old);
        from_bits(aType);
        aCompute();
        to_bits(aType);
        set(neu);
        get(aWhere);
        get(old);
        get(neu);
        atomic(0x48u, aType);
        if (iProgram.size_of(aType) < 4u)
            normalize(aType);
        tee(seen);
        get(old);
        op(bits == type::I64 ? opcode::I64Ne : opcode::I32Ne);
        get(seen);
        set(old);
        branch_to(top, true);
        end();
        if (aValueUsed)
        {
            get(aOld ? old : neu);
            from_bits(aType);
        }
        release(seen);
        release(neu);
        release(old);
    }

    void function_generator::store(qtype aType, std::uint32_t aOffset)
    {
        opcode o = opcode::I32StoreMem;
        std::uint32_t align = 2u;
        auto const size = iProgram.size_of(aType);
        switch (kind_of(aType))
        {
        case vkind::F32: o = opcode::F32StoreMem; align = 2u; break;
        case vkind::F64: o = opcode::F64StoreMem; align = 3u; break;
        case vkind::I64: o = opcode::I64StoreMem; align = 3u; break;
        default:
            switch (size)
            {
            case 1u: o = opcode::I32StoreMem8; align = 0u; break;
            case 2u: o = opcode::I32StoreMem16; align = 1u; break;
            default: o = opcode::I32StoreMem; align = 2u; break;
            }
            break;
        }
        op(o);
        u32(0x40u | align);
        u32(language::c::layout::Memory);
        u32(aOffset);
    }

    void function_generator::copy(std::uint64_t aSize)
    {
        // ([destination, source] -> ()): small copies inline (8 bytes at a time), larger by memmove
        if (aSize <= 64u)
        {
            auto const source = temporary(type::I64);
            auto const destination = temporary(type::I64);
            set(source);
            set(destination);
            std::uint64_t offset = 0u;
            auto const piece = [&](std::uint64_t aBytes, opcode aLoad, opcode aStore, std::uint32_t aAlign)
            {
                get(destination);
                get(source);
                op(aLoad);
                u32(0x40u | aAlign);
                u32(language::c::layout::Memory);
                u32(static_cast<std::uint32_t>(offset));
                op(aStore);
                u32(0x40u | aAlign);
                u32(language::c::layout::Memory);
                u32(static_cast<std::uint32_t>(offset));
                offset += aBytes;
            };
            while (offset + 8u <= aSize)
                piece(8u, opcode::I64LoadMem, opcode::I64StoreMem, 0u);
            if (offset + 4u <= aSize)
                piece(4u, opcode::I32LoadMem, opcode::I32StoreMem, 0u);
            if (offset + 2u <= aSize)
                piece(2u, opcode::I32LoadMem16U, opcode::I32StoreMem16, 0u);
            if (offset + 1u <= aSize)
                piece(1u, opcode::I32LoadMem8U, opcode::I32StoreMem8, 0u);
            release(destination);
            release(source);
            return;
        }
        i64(static_cast<std::int64_t>(aSize));
        libc("memmove");
        drop();
    }

    void function_generator::zero(std::uint64_t aSize)
    {
        if (aSize <= 64u)
        {
            auto const destination = temporary(type::I64);
            set(destination);
            std::uint64_t offset = 0u;
            auto const piece = [&](std::uint64_t aBytes, opcode aStore, bool a64)
            {
                get(destination);
                if (a64)
                    i64(0);
                else
                    i32(0);
                op(aStore);
                u32(0x40u);
                u32(language::c::layout::Memory);
                u32(static_cast<std::uint32_t>(offset));
                offset += aBytes;
            };
            while (offset + 8u <= aSize)
                piece(8u, opcode::I64StoreMem, true);
            if (offset + 4u <= aSize)
                piece(4u, opcode::I32StoreMem, false);
            if (offset + 2u <= aSize)
                piece(2u, opcode::I32StoreMem16, false);
            if (offset + 1u <= aSize)
                piece(1u, opcode::I32StoreMem8, false);
            release(destination);
            return;
        }
        auto const destination = temporary(type::I64);
        set(destination);
        get(destination);
        i32(0);
        i64(static_cast<std::int64_t>(aSize));
        libc("memset");
        drop();
        release(destination);
    }

    // --- control

    void function_generator::begin(control_kind aKind, std::optional<type> aResult)
    {
        op(aKind == control_kind::Loop ? opcode::Loop : opcode::Block);
        iCode.push_back(aResult ? *wasm_type(*aResult) : 0x40u);
        iControls.push_back(control{ aKind });
    }

    void function_generator::begin_if(std::optional<type> aResult)
    {
        op(opcode::If);
        iCode.push_back(aResult ? *wasm_type(*aResult) : 0x40u);
        iControls.push_back(control{ control_kind::If });
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

    void function_generator::branch_to(std::size_t aControl, bool aConditional)
    {
        op(aConditional ? opcode::BrIf : opcode::Br);
        u32(static_cast<std::uint32_t>(iControls.size() - 1u - aControl));
    }

    void function_generator::branch_table(std::vector<std::size_t> const& aControls, std::size_t aDefault)
    {
        op(opcode::BrTable);
        u32(static_cast<std::uint32_t>(aControls.size()));
        for (auto const c : aControls)
            u32(static_cast<std::uint32_t>(iControls.size() - 1u - c));
        u32(static_cast<std::uint32_t>(iControls.size() - 1u - aDefault));
    }

    std::size_t function_generator::find_break(node const* aStatement) const
    {
        for (std::size_t i = iControls.size(); i-- > 0u;)
            if (iControls[i].breakOf == aStatement)
                return i;
        throw compile_error{ aStatement, "internal error: break target" };
    }

    std::size_t function_generator::find_continue(node const* aStatement) const
    {
        for (std::size_t i = iControls.size(); i-- > 0u;)
            if (iControls[i].continueOf == aStatement)
                return i;
        throw compile_error{ aStatement, "internal error: continue target" };
    }

    std::size_t function_generator::find_label(node const* aLabel) const
    {
        for (std::size_t i = iControls.size(); i-- > 0u;)
            if (iControls[i].label == aLabel)
                return i;
        return ~std::size_t{};
    }

    void function_generator::restore_stack_to(std::size_t aControl)
    {
        // (the outermost block with variable length arrays that the branch leaves: the stack pointer as it was at its start)
        for (std::size_t i = aControl + 1u; i < iControls.size(); ++i)
            if (iControls[i].stackMark)
            {
                get(*iControls[i].stackMark);
                op(opcode::GlobalSet);
                u32(iProgram.sp_global());
                return;
            }
    }

    // --- objects

    std::int64_t function_generator::frame_object(std::uint64_t aSize, std::uint64_t aAlignment)
    {
        iFrameUsed = true;
        aAlignment = std::max<std::uint64_t>(aAlignment, 1u);
        iFrameAlignment = std::max(iFrameAlignment, aAlignment);
        iFrameFixed = (iFrameFixed + aAlignment - 1u) / aAlignment * aAlignment;
        auto const result = static_cast<std::int64_t>(iFrameFixed);
        iFrameFixed += std::max<std::uint64_t>(aSize, 1u);
        return result;
    }

    std::int64_t function_generator::frame_temporary(std::uint64_t aSize, std::uint64_t aAlignment)
    {
        // (temporaries are after the fixed objects (whose storage is assigned before the body's code is generated))
        iFrameUsed = true;
        aAlignment = std::max<std::uint64_t>(aAlignment, 8u);
        iTemporaries = (iTemporaries + aAlignment - 1u) / aAlignment * aAlignment;
        auto const result = static_cast<std::int64_t>(((iFrameFixed + 15u) & ~std::uint64_t{ 15u }) + iTemporaries);
        iTemporaries += std::max<std::uint64_t>(aSize, 1u);
        iFrameSize = std::max(iFrameSize, iTemporaries);
        return result;
    }

    void function_generator::statement_end()
    {
        iTemporaries = iTemporaryBase;
    }

    void function_generator::object_address(entity* aObject)
    {
        auto* e = aObject->self();
        switch (e->where)
        {
        case object_storage::Static:
            if (e->address == 0u && e->linkage == linkage::External && iProgram.gen().linked())
            {
                // (an object that another translation unit of the linked program defines: its address, from that unit's function
                // named '&' and its name (see program::linked_object))
                call(iProgram.runtime(std::string{ bytecode::LinkNamespace } + "::&" + e->name, {}, type::I64));
                break;
            }
            if (e->address == 0u)
                throw compile_error{ e->decl, "undefined reference to '" + e->name + "'" };
            if (e->threadLocal)
            {
                // (a thread-local object: in the thread's block (%c.tls), at its offset in the template)
                op(opcode::GlobalGet);
                u32(iProgram.tls_global());
                if (auto const offset = e->address - iProgram.tls_template(); offset != 0u)
                {
                    i64(static_cast<std::int64_t>(offset));
                    op(opcode::I64Add);
                }
                break;
            }
            i64(static_cast<std::int64_t>(e->address));
            break;
        case object_storage::Frame:
            get(iFramePointer);
            if (e->frameOffset != 0)
            {
                i64(e->frameOffset);
                op(opcode::I64Add);
            }
            break;
        case object_storage::Dynamic:
            get(e->local);
            break;
        default:
            throw compile_error{ e->decl, "internal error: the address of '" + e->name + "' (not in memory)" };
        }
    }

    void function_generator::assign_storage(entity* aObject)
    {
        auto* e = aObject;
        if (e->where == object_storage::Static || e->where == object_storage::Frame || e->where == object_storage::Local || e->where == object_storage::Dynamic)
            return;
        auto const t = e->type;
        if (t->is_variably_modified() && t->is_array())
        {
            e->where = object_storage::Dynamic;
            e->local = local(type::I64);
            e->sizeLocal = local(type::I64);
            e->markLocal = local(type::I64);
            iDynamicStack = true;
            return;
        }
        if (t->is_scalar() && !e->addressTaken && !(iInfo.usesSetjmp && t.is_volatile() && false))
        {
            e->where = object_storage::Local;
            e->local = local(wasm_of(t));
            return;
        }
        e->where = object_storage::Frame;
        auto alignment = iProgram.align_of(t);
        if (e->value > 0)
            alignment = std::max(alignment, static_cast<std::uint64_t>(e->value));
        e->frameOffset = frame_object(iProgram.size_of(t), alignment);
    }

    // --- the function

    void function_generator::generate()
    {
        // (the parameters' locals: a structure or union result's address, the parameters, the variable arguments' address)
        std::uint32_t next = 0u;
        if (iReturnsRecord)
            iReturnAddress = next++;
        std::vector<std::uint32_t> parameterLocals;
        for (std::size_t i = 0u; i < iType->parameters.size(); ++i)
            parameterLocals.push_back(next++);
        if (iType->variadic)
            iVarargs = next++;
        iFramePointer = local(type::I64);
        iEntryStack = local(type::I64);
        auto const resultType = iProgram.wasm_result(iType);
        if (resultType != type::Void)
            iResult = local(resultType);
        // (the parameters: in their locals (a scalar whose address is taken: copied to the frame; a structure or union: at the address 
        // passed, a copy that the caller made))
        std::vector<std::pair<entity*, std::uint32_t>> copies;
        std::vector<std::pair<std::uint32_t, entity*>> conversions;     ///< (an old-style definition's parameters: from their arguments' types)
        for (std::size_t i = 0u; i < iInfo.parameters.size(); ++i)
        {
            auto* p = iInfo.parameters[i];
            if (!p->type->is_record() && i < iType->parameters.size() &&
                !iProgram.compatible(iType->parameters[i].type.unqualified(), p->type.unqualified()))
            {
                conversions.emplace_back(parameterLocals[i], p);
                parameterLocals[i] = local(wasm_of(p->type));
            }
            if (p->type->is_record())
            {
                p->where = object_storage::Dynamic;
                p->local = parameterLocals[i];
            }
            else if (p->addressTaken)
            {
                p->where = object_storage::Frame;
                p->frameOffset = frame_object(iProgram.size_of(p->type), iProgram.align_of(p->type));
                copies.emplace_back(p, parameterLocals[i]);
            }
            else
            {
                p->where = object_storage::Local;
                p->local = parameterLocals[i];
            }
        }
        // (the automatic objects)
        for (auto* l : iInfo.locals)
            if (l->where != object_storage::Static)
                assign_storage(l);
        // (the body, in a block that returns branch out of)
        begin(control_kind::Block);
        iControls.back().kind = control_kind::Exit;
        for (std::size_t i = 0u, c = 0u; i < iInfo.parameters.size() && c < conversions.size(); ++i)
            if (conversions[c].second == iInfo.parameters[i])
            {
                get(conversions[c].first);
                convert(iType->parameters[i].type, iInfo.parameters[i]->type);
                set(parameterLocals[i]);
                ++c;
            }
        for (auto const& [p, l] : copies)
        {
            object_address(p);
            get(l);
            store(p->type);
        }
        if (iDispatch)
            dispatch_body();
        else
            compound(iInfo.body);
        // (falling off the end: main returns 0; another function's result is unspecified (0))
        if (resultType != type::Void)
        {
            switch (resultType)
            {
            case type::I32: i32(0); break;
            case type::I64: i64(0); break;
            case type::F32: f32(0.0f); break;
            default: f64(0.0); break;
            }
            set(iResult);
        }
        end();
        std::vector<std::uint8_t> body;
        body.swap(iCode);
        std::vector<std::pair<std::uint32_t, std::uint32_t>> bodyRelocations;
        bodyRelocations.swap(iRelocations);
        // (the prologue: the frame (the fixed objects, then the temporaries), the stack checked)
        auto const fixed = (iFrameFixed + 15u) & ~std::uint64_t{ 15u };
        auto const frameSize = (fixed + iFrameSize + 15u) & ~std::uint64_t{ 15u };
        bool const needStack = iFrameUsed || iDynamicStack || iInfo.usesAlloca || (iProgram.uses_longjmp() && !iInfo.usesSetjmp) || iInfo.usesSetjmp;
        if (needStack)
        {
            op(opcode::GlobalGet);
            u32(iProgram.sp_global());
            tee(iEntryStack);
            if (frameSize != 0u)
            {
                i64(static_cast<std::int64_t>(frameSize));
                op(opcode::I64Sub);
                if (iFrameAlignment > 16u)
                {
                    // (an object aligned more than the stack (16): the frame aligned so)
                    i64(-static_cast<std::int64_t>(iFrameAlignment));
                    op(opcode::I64And);
                }
                tee(iFramePointer);
                op(opcode::GlobalSet);
                u32(iProgram.sp_global());
                get(iFramePointer);
                op(opcode::GlobalGet);
                u32(iProgram.limit_global());
                op(opcode::I64LtU);
                op(opcode::If);
                iCode.push_back(0x40u);
                runtime("c::rt.stack_overflow", {}, type::Void);
                op(opcode::Unreachable);
                op(opcode::End);
            }
            else
            {
                set(iFramePointer);
            }
        }
        auto const prologueSize = static_cast<std::uint32_t>(iCode.size());
        for (auto const& [offset, function] : bodyRelocations)
            iRelocations.emplace_back(offset + prologueSize, function);
        iCode.insert(iCode.end(), body.begin(), body.end());
        // (the epilogue: the stack pointer restored; the result)
        if (needStack)
        {
            get(iEntryStack);
            op(opcode::GlobalSet);
            u32(iProgram.sp_global());
        }
        if (resultType != type::Void)
            get(iResult);
        op(opcode::End);
        validate(iFunction->name);
        fragment code;
        code.fragmentCode.as_std_vector() = iCode;
        for (auto const& r : iRelocations)
            code.fragmentRelocations.push_back(neolib::pair<std::uint32_t, std::uint32_t>{ r.first, r.second });
        iProgram.gen().set_function_body(iInfo.index, code);
        iProgram.gen().set_function_locals(iInfo.index, neolib::vector<type>{ iLocals.begin(), iLocals.end() });
        (void)fixed;
    }
}
