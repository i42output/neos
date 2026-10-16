/*
  vm.cpp

  Copyright (c) 2025 Leigh Johnston.  All Rights Reserved.

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

#include <neos/neos.hpp>
#include <sstream>
#include <cstring>
#include <cmath>
#include <limits>
#include <unordered_map>
#include <charconv>
#include <cstdlib>
#include <neos/bytecode/vm/vm.hpp>
#include <neos/bytecode/vm/jit.hpp>
#include <neos/bytecode/vm/runtime_plugins.hpp>

namespace neos
{
    namespace bytecode
    {
        namespace vm
        {
            namespace
            {
                struct trap : std::runtime_error { using std::runtime_error::runtime_error; };

                using value = std::uint64_t;

                std::int32_t as_i32(value v) { return static_cast<std::int32_t>(static_cast<std::uint32_t>(v)); }
                std::uint32_t as_u32(value v) { return static_cast<std::uint32_t>(v); }
                std::int64_t as_i64(value v) { return static_cast<std::int64_t>(v); }
                float as_f32(value v) { float f; auto const u = static_cast<std::uint32_t>(v); std::memcpy(&f, &u, sizeof(f)); return f; }
                double as_f64(value v) { double d; std::memcpy(&d, &v, sizeof(d)); return d; }
                value of_i32(std::int32_t i) { return static_cast<std::uint32_t>(i); }
                value of_u32(std::uint32_t i) { return i; }
                value of_i64(std::int64_t i) { return static_cast<std::uint64_t>(i); }
                value of_f32(float f) { std::uint32_t u; std::memcpy(&u, &f, sizeof(u)); return u; }
                value of_f64(double d) { value v; std::memcpy(&v, &d, sizeof(v)); return v; }
                value of_bool(bool b) { return b ? 1u : 0u; }

                class reader
                {
                public:
                    reader(std::vector<std::uint8_t> const& aBytes, std::size_t aPosition = 0u) : iBytes{ aBytes }, iPosition{ aPosition } {}
                public:
                    std::size_t position() const { return iPosition; }
                    void seek(std::size_t aPosition) { iPosition = aPosition; }
                    std::uint8_t byte()
                    {
                        if (iPosition >= iBytes.size())
                            throw exceptions::out_of_text();
                        return iBytes[iPosition++];
                    }
                    std::uint64_t u()
                    {
                        std::uint64_t result = 0u;
                        std::uint32_t shift = 0u;
                        std::uint8_t b;
                        do
                        {
                            b = byte();
                            result |= static_cast<std::uint64_t>(b & 0x7Fu) << shift;
                            shift += 7u;
                        } while ((b & 0x80u) != 0u);
                        return result;
                    }
                    std::int64_t s()
                    {
                        std::int64_t result = 0;
                        std::uint32_t shift = 0u;
                        std::uint8_t b;
                        do
                        {
                            b = byte();
                            result |= static_cast<std::int64_t>(b & 0x7Fu) << shift;
                            shift += 7u;
                        } while ((b & 0x80u) != 0u);
                        if (shift < 64u && (b & 0x40u) != 0u)
                            result |= -(static_cast<std::int64_t>(1) << shift);
                        return result;
                    }
                    template <typename T>
                    T raw()
                    {
                        T result;
                        if (iPosition + sizeof(T) > iBytes.size())
                            throw exceptions::out_of_text();
                        std::memcpy(&result, &iBytes[iPosition], sizeof(T));
                        iPosition += sizeof(T);
                        return result;
                    }
                    std::string name()
                    {
                        auto const length = u();
                        if (iPosition + length > iBytes.size())
                            throw exceptions::out_of_text();
                        std::string result{ reinterpret_cast<char const*>(&iBytes[iPosition]), length };
                        iPosition += length;
                        return result;
                    }
                private:
                    std::vector<std::uint8_t> const& iBytes;
                    std::size_t iPosition;
                };

                // function bodies are decoded once: each opcode and each immediate becomes one token so that executing an 
                // instruction does not decode LEB128 immediates (code positions are token indices)
                class decoded_reader
                {
                public:
                    decoded_reader(std::vector<std::uint64_t> const& aTokens) : iTokens{ aTokens }, iPosition{ 0u } {}
                public:
                    std::size_t position() const { return iPosition; }
                    void seek(std::size_t aPosition) { iPosition = aPosition; }
                    std::uint8_t byte() { return static_cast<std::uint8_t>(iTokens[iPosition++]); }
                    std::uint64_t u() { return iTokens[iPosition++]; }
                    std::int64_t s() { return static_cast<std::int64_t>(iTokens[iPosition++]); }
                    template <typename T>
                    T raw()
                    {
                        T result;
                        std::memcpy(&result, &iTokens[iPosition++], sizeof(T));
                        return result;
                    }
                private:
                    std::vector<std::uint64_t> const& iTokens;
                    std::size_t iPosition;
                };

                struct function_type
                {
                    std::vector<std::uint8_t> parameters;
                    std::vector<std::uint8_t> results;
                };

                struct function
                {
                    std::uint32_t type = 0u;
                    std::optional<std::string> import;
                    std::vector<std::uint8_t> locals;
                    std::size_t codeBegin = 0u; ///< (token index)
                    std::size_t codeEnd = 0u;   ///< (token index)
                };

                struct label
                {
                    std::size_t continuation;
                    std::size_t height;
                    std::size_t arity;
                    bool loop;
                };

                struct frame
                {
                    std::uint32_t function;
                    std::vector<value> locals;
                    std::size_t base;
                    std::vector<label> labels;
                    std::size_t pc;
                };

                class machine
                {
                public:
                    static constexpr std::size_t MaxCallDepth = 100000u;
                public:
                    machine(text const& aText, std::ostream& aOutput, std::istream& aInput, std::uint64_t& aInstructions, bool aRestricted) :
                        iOutput{ aOutput }, iInput{ aInput }, iInstructions{ aInstructions }, iRestricted{ aRestricted }
                    {
                        for (auto b : aText)
                            iModule.push_back(static_cast<std::uint8_t>(b));
                        parse();
                    }
                public:
                    std::optional<std::uint32_t> export_function(std::string const& aName) const
                    {
                        auto existing = iFunctionExports.find(aName);
                        if (existing != iFunctionExports.end())
                            return existing->second;
                        return std::nullopt;
                    }
                    // compile the module to native code (null if not supported: the interpreter is then used)
                    jit::compiled_module_ptr compile()
                    {
                        if (!jit::available())
                            return {};
                        jit::module_description module;
                        for (auto const& f : iFunctions)
                        {
                            jit::function_description d;
                            d.parameters = &iTypes.at(f.type).parameters;
                            d.results = &iTypes.at(f.type).results;
                            d.locals = &f.locals;
                            d.imported = f.import.has_value();
                            d.codeBegin = f.codeBegin;
                            d.codeEnd = f.codeEnd;
                            module.functions.push_back(d);
                        }
                        module.tokens = &iTokens;
                        module.globals = &iGlobals;
                        module.globalTypes = &iGlobalTypes;
                        return jit::compile(module, [this](std::uint32_t aFunction, std::vector<value> const& aArguments)
                        {
                            return call_import(aFunction, aArguments);
                        });
                    }
                    function_type const& type_of(std::uint32_t aFunction) const
                    {
                        return iTypes.at(iFunctions.at(aFunction).type);
                    }
                    std::vector<value> invoke(std::uint32_t aFunction, std::vector<value> aArguments)
                    {
                        if (iFunctions.at(aFunction).import)
                            return call_import(aFunction, aArguments);
                        std::vector<value> stack;
                        std::vector<frame> frames;
                        decoded_reader code{ iTokens };
                        auto enter = [&](std::uint32_t aCallee, std::vector<value> aCallArguments)
                        {
                            if (frames.size() >= MaxCallDepth)
                                throw trap{ "call stack exhausted" };
                            auto const& callee = iFunctions.at(aCallee);
                            frames.push_back(frame{ aCallee, std::move(aCallArguments), stack.size(), {}, callee.codeBegin });
                            frames.back().locals.resize(frames.back().locals.size() + callee.locals.size(), 0u);
                            code.seek(callee.codeBegin);
                        };
                        auto leave = [&]() -> bool // returns true when the outermost function returns
                        {
                            auto const& returning = frames.back();
                            auto const arity = type_of(returning.function).results.size();
                            std::vector<value> r{ stack.end() - static_cast<std::ptrdiff_t>(arity), stack.end() };
                            stack.resize(returning.base);
                            stack.insert(stack.end(), r.begin(), r.end());
                            frames.pop_back();
                            if (frames.empty())
                                return true;
                            code.seek(frames.back().pc);
                            return false;
                        };
                        enter(aFunction, std::move(aArguments));
                        auto pop = [&]() -> value
                        {
                            if (stack.size() <= frames.back().base)
                                throw trap{ "stack underflow" };
                            auto const v = stack.back();
                            stack.pop_back();
                            return v;
                        };
                        auto branch = [&](std::size_t aDepth) -> bool // returns true if the function returns
                        {
                            auto& labels = frames.back().labels;
                            if (aDepth >= labels.size())
                                return true;
                            auto const target = labels[labels.size() - 1u - aDepth];
                            auto const arity = target.loop ? 0u : target.arity;
                            if (arity == 0u)
                                stack.resize(target.height);
                            else
                            {
                                auto const result = stack.back(); // (block results are at most one value)
                                stack.resize(target.height);
                                stack.push_back(result);
                            }
                            labels.resize(labels.size() - aDepth - (target.loop ? 0u : 1u));
                            code.seek(target.continuation);
                            return false;
                        };
                        auto block_arity = [&](std::uint8_t aBlockType) -> std::size_t
                        {
                            return aBlockType == 0x40u ? 0u : 1u;
                        };
                        auto binary = [&](auto aOperation)
                        {
                            auto const rhs = pop();
                            auto const lhs = pop();
                            stack.push_back(aOperation(lhs, rhs));
                        };
                        auto unary = [&](auto aOperation)
                        {
                            stack.push_back(aOperation(pop()));
                        };
                        for (;;)
                        {
                            ++iInstructions;
                            auto& current = frames.back();
                            auto& f = iFunctions[current.function];
                            auto& labels = current.labels;
                            auto& locals = current.locals;
                            auto const opPosition = code.position();
                            if (opPosition >= f.codeEnd)
                            {
                                if (leave())
                                    return stack;
                                continue;
                            }
                            auto const op = code.byte();
                            switch (op)
                            {
                            case 0x00: throw trap{ "unreachable executed" };
                            case 0x01: break;
                            case 0x02:
                                {
                                    auto const bt = code.byte();
                                    labels.push_back({ iEnds[opPosition], stack.size(), block_arity(bt), false });
                                }
                                break;
                            case 0x03:
                                {
                                    code.byte();
                                    labels.push_back({ code.position(), stack.size(), 0u, true });
                                }
                                break;
                            case 0x04:
                                {
                                    auto const bt = code.byte();
                                    auto const condition = as_i32(pop());
                                    if (condition != 0)
                                        labels.push_back({ iEnds[opPosition], stack.size(), block_arity(bt), false });
                                    else if (iElses[opPosition] != 0u)
                                    {
                                        labels.push_back({ iEnds[opPosition], stack.size(), block_arity(bt), false });
                                        code.seek(iElses[opPosition]);
                                    }
                                    else
                                        code.seek(iEnds[opPosition]);
                                }
                                break;
                            case 0x05: // reached else at end of then-branch
                                {
                                    auto const target = labels.back();
                                    labels.pop_back();
                                    code.seek(target.continuation);
                                }
                                break;
                            case 0x0B:
                                if (labels.empty())
                                {
                                    if (leave())
                                        return stack;
                                    break;
                                }
                                labels.pop_back();
                                break;
                            case 0x0C:
                                if (branch(static_cast<std::size_t>(code.u())) && leave())
                                    return stack;
                                break;
                            case 0x0D:
                                {
                                    auto const depth = static_cast<std::size_t>(code.u());
                                    if (as_i32(pop()) != 0 && branch(depth) && leave())
                                        return stack;
                                }
                                break;
                            case 0x0F:
                                if (leave())
                                    return stack;
                                break;
                            case 0x10:
                                {
                                    auto const callee = static_cast<std::uint32_t>(code.u());
                                    auto const& calleeType = type_of(callee);
                                    if (stack.size() < current.base + calleeType.parameters.size())
                                        throw trap{ "stack underflow" };
                                    std::vector<value> arguments{ stack.end() - static_cast<std::ptrdiff_t>(calleeType.parameters.size()), stack.end() };
                                    stack.resize(stack.size() - calleeType.parameters.size());
                                    if (iFunctions.at(callee).import)
                                    {
                                        auto const r = call_import(callee, arguments);
                                        stack.insert(stack.end(), r.begin(), r.end());
                                    }
                                    else
                                    {
                                        current.pc = code.position();
                                        enter(callee, std::move(arguments)); // note: invalidates 'current'
                                    }
                                }
                                break;
                            case 0x1A: pop(); break;
                            case 0x1B:
                                {
                                    auto const condition = as_i32(pop());
                                    auto const b = pop();
                                    auto const a = pop();
                                    stack.push_back(condition != 0 ? a : b);
                                }
                                break;
                            case 0x20: stack.push_back(locals.at(code.u())); break;
                            case 0x21: locals.at(code.u()) = pop(); break;
                            case 0x22: locals.at(code.u()) = stack.back(); break;
                            case 0x23: stack.push_back(iGlobals.at(code.u())); break;
                            case 0x24: iGlobals.at(code.u()) = pop(); break;
                            case 0x41: stack.push_back(of_i32(static_cast<std::int32_t>(code.s()))); break;
                            case 0x42: stack.push_back(of_i64(code.s())); break;
                            case 0x43: stack.push_back(of_f32(code.raw<float>())); break;
                            case 0x44: stack.push_back(of_f64(code.raw<double>())); break;
                            // i32 comparison
                            case 0x45: unary([](value a) { return of_bool(as_i32(a) == 0); }); break;
                            case 0x46: binary([](value a, value b) { return of_bool(as_i32(a) == as_i32(b)); }); break;
                            case 0x47: binary([](value a, value b) { return of_bool(as_i32(a) != as_i32(b)); }); break;
                            case 0x48: binary([](value a, value b) { return of_bool(as_i32(a) < as_i32(b)); }); break;
                            case 0x49: binary([](value a, value b) { return of_bool(as_u32(a) < as_u32(b)); }); break;
                            case 0x4A: binary([](value a, value b) { return of_bool(as_i32(a) > as_i32(b)); }); break;
                            case 0x4B: binary([](value a, value b) { return of_bool(as_u32(a) > as_u32(b)); }); break;
                            case 0x4C: binary([](value a, value b) { return of_bool(as_i32(a) <= as_i32(b)); }); break;
                            case 0x4D: binary([](value a, value b) { return of_bool(as_u32(a) <= as_u32(b)); }); break;
                            case 0x4E: binary([](value a, value b) { return of_bool(as_i32(a) >= as_i32(b)); }); break;
                            case 0x4F: binary([](value a, value b) { return of_bool(as_u32(a) >= as_u32(b)); }); break;
                            // i64 comparison
                            case 0x50: unary([](value a) { return of_bool(a == 0u); }); break;
                            case 0x51: binary([](value a, value b) { return of_bool(a == b); }); break;
                            case 0x52: binary([](value a, value b) { return of_bool(a != b); }); break;
                            case 0x53: binary([](value a, value b) { return of_bool(as_i64(a) < as_i64(b)); }); break;
                            case 0x54: binary([](value a, value b) { return of_bool(a < b); }); break;
                            case 0x55: binary([](value a, value b) { return of_bool(as_i64(a) > as_i64(b)); }); break;
                            case 0x56: binary([](value a, value b) { return of_bool(a > b); }); break;
                            case 0x57: binary([](value a, value b) { return of_bool(as_i64(a) <= as_i64(b)); }); break;
                            case 0x58: binary([](value a, value b) { return of_bool(a <= b); }); break;
                            case 0x59: binary([](value a, value b) { return of_bool(as_i64(a) >= as_i64(b)); }); break;
                            case 0x5A: binary([](value a, value b) { return of_bool(a >= b); }); break;
                            // f32/f64 comparison
                            case 0x5B: binary([](value a, value b) { return of_bool(as_f32(a) == as_f32(b)); }); break;
                            case 0x5C: binary([](value a, value b) { return of_bool(as_f32(a) != as_f32(b)); }); break;
                            case 0x5D: binary([](value a, value b) { return of_bool(as_f32(a) < as_f32(b)); }); break;
                            case 0x5E: binary([](value a, value b) { return of_bool(as_f32(a) > as_f32(b)); }); break;
                            case 0x5F: binary([](value a, value b) { return of_bool(as_f32(a) <= as_f32(b)); }); break;
                            case 0x60: binary([](value a, value b) { return of_bool(as_f32(a) >= as_f32(b)); }); break;
                            case 0x61: binary([](value a, value b) { return of_bool(as_f64(a) == as_f64(b)); }); break;
                            case 0x62: binary([](value a, value b) { return of_bool(as_f64(a) != as_f64(b)); }); break;
                            case 0x63: binary([](value a, value b) { return of_bool(as_f64(a) < as_f64(b)); }); break;
                            case 0x64: binary([](value a, value b) { return of_bool(as_f64(a) > as_f64(b)); }); break;
                            case 0x65: binary([](value a, value b) { return of_bool(as_f64(a) <= as_f64(b)); }); break;
                            case 0x66: binary([](value a, value b) { return of_bool(as_f64(a) >= as_f64(b)); }); break;
                            // i32 arithmetic
                            case 0x6A: binary([](value a, value b) { return of_u32(as_u32(a) + as_u32(b)); }); break;
                            case 0x6B: binary([](value a, value b) { return of_u32(as_u32(a) - as_u32(b)); }); break;
                            case 0x6C: binary([](value a, value b) { return of_u32(as_u32(a) * as_u32(b)); }); break;
                            case 0x6D: binary([](value a, value b) 
                                { 
                                    if (as_i32(b) == 0) throw trap{ "integer divide by zero" };
                                    if (as_i32(a) == std::numeric_limits<std::int32_t>::min() && as_i32(b) == -1) throw trap{ "integer overflow" };
                                    return of_i32(as_i32(a) / as_i32(b)); 
                                }); break;
                            case 0x6E: binary([](value a, value b) { if (as_u32(b) == 0u) throw trap{ "integer divide by zero" }; return of_u32(as_u32(a) / as_u32(b)); }); break;
                            case 0x6F: binary([](value a, value b) 
                                { 
                                    if (as_i32(b) == 0) throw trap{ "integer divide by zero" };
                                    if (as_i32(b) == -1) return of_i32(0);
                                    return of_i32(as_i32(a) % as_i32(b)); 
                                }); break;
                            case 0x70: binary([](value a, value b) { if (as_u32(b) == 0u) throw trap{ "integer divide by zero" }; return of_u32(as_u32(a) % as_u32(b)); }); break;
                            case 0x71: binary([](value a, value b) { return of_u32(as_u32(a) & as_u32(b)); }); break;
                            case 0x72: binary([](value a, value b) { return of_u32(as_u32(a) | as_u32(b)); }); break;
                            case 0x73: binary([](value a, value b) { return of_u32(as_u32(a) ^ as_u32(b)); }); break;
                            case 0x74: binary([](value a, value b) { return of_u32(as_u32(a) << (as_u32(b) & 31u)); }); break;
                            case 0x75: binary([](value a, value b) { return of_i32(as_i32(a) >> (as_u32(b) & 31u)); }); break;
                            case 0x76: binary([](value a, value b) { return of_u32(as_u32(a) >> (as_u32(b) & 31u)); }); break;
                            // i64 arithmetic
                            case 0x7C: binary([](value a, value b) { return a + b; }); break;
                            case 0x7D: binary([](value a, value b) { return a - b; }); break;
                            case 0x7E: binary([](value a, value b) { return a * b; }); break;
                            case 0x7F: binary([](value a, value b) 
                                { 
                                    if (b == 0u) throw trap{ "integer divide by zero" };
                                    if (as_i64(a) == std::numeric_limits<std::int64_t>::min() && as_i64(b) == -1) throw trap{ "integer overflow" };
                                    return of_i64(as_i64(a) / as_i64(b)); 
                                }); break;
                            case 0x80: binary([](value a, value b) { if (b == 0u) throw trap{ "integer divide by zero" }; return a / b; }); break;
                            case 0x81: binary([](value a, value b) 
                                { 
                                    if (b == 0u) throw trap{ "integer divide by zero" };
                                    if (as_i64(b) == -1) return of_i64(0);
                                    return of_i64(as_i64(a) % as_i64(b)); 
                                }); break;
                            case 0x82: binary([](value a, value b) { if (b == 0u) throw trap{ "integer divide by zero" }; return a % b; }); break;
                            case 0x83: binary([](value a, value b) { return a & b; }); break;
                            case 0x84: binary([](value a, value b) { return a | b; }); break;
                            case 0x85: binary([](value a, value b) { return a ^ b; }); break;
                            case 0x86: binary([](value a, value b) { return a << (b & 63u); }); break;
                            case 0x87: binary([](value a, value b) { return of_i64(as_i64(a) >> (b & 63u)); }); break;
                            case 0x88: binary([](value a, value b) { return a >> (b & 63u); }); break;
                            // f32 arithmetic
                            case 0x8B: unary([](value a) { return of_f32(std::fabs(as_f32(a))); }); break;
                            case 0x8C: unary([](value a) { return of_f32(-as_f32(a)); }); break;
                            case 0x8D: unary([](value a) { return of_f32(std::ceil(as_f32(a))); }); break;
                            case 0x8E: unary([](value a) { return of_f32(std::floor(as_f32(a))); }); break;
                            case 0x8F: unary([](value a) { return of_f32(std::trunc(as_f32(a))); }); break;
                            case 0x90: unary([](value a) { return of_f32(std::nearbyint(as_f32(a))); }); break;
                            case 0x91: unary([](value a) { return of_f32(std::sqrt(as_f32(a))); }); break;
                            case 0x92: binary([](value a, value b) { return of_f32(as_f32(a) + as_f32(b)); }); break;
                            case 0x93: binary([](value a, value b) { return of_f32(as_f32(a) - as_f32(b)); }); break;
                            case 0x94: binary([](value a, value b) { return of_f32(as_f32(a) * as_f32(b)); }); break;
                            case 0x95: binary([](value a, value b) { return of_f32(as_f32(a) / as_f32(b)); }); break;
                            case 0x96: binary([](value a, value b) { return of_f32(wasm_min(as_f32(a), as_f32(b))); }); break;
                            case 0x97: binary([](value a, value b) { return of_f32(wasm_max(as_f32(a), as_f32(b))); }); break;
                            case 0x98: binary([](value a, value b) { return of_f32(std::copysign(as_f32(a), as_f32(b))); }); break;
                            // f64 arithmetic
                            case 0x99: unary([](value a) { return of_f64(std::fabs(as_f64(a))); }); break;
                            case 0x9A: unary([](value a) { return of_f64(-as_f64(a)); }); break;
                            case 0x9B: unary([](value a) { return of_f64(std::ceil(as_f64(a))); }); break;
                            case 0x9C: unary([](value a) { return of_f64(std::floor(as_f64(a))); }); break;
                            case 0x9D: unary([](value a) { return of_f64(std::trunc(as_f64(a))); }); break;
                            case 0x9E: unary([](value a) { return of_f64(std::nearbyint(as_f64(a))); }); break;
                            case 0x9F: unary([](value a) { return of_f64(std::sqrt(as_f64(a))); }); break;
                            case 0xA0: binary([](value a, value b) { return of_f64(as_f64(a) + as_f64(b)); }); break;
                            case 0xA1: binary([](value a, value b) { return of_f64(as_f64(a) - as_f64(b)); }); break;
                            case 0xA2: binary([](value a, value b) { return of_f64(as_f64(a) * as_f64(b)); }); break;
                            case 0xA3: binary([](value a, value b) { return of_f64(as_f64(a) / as_f64(b)); }); break;
                            case 0xA4: binary([](value a, value b) { return of_f64(wasm_min(as_f64(a), as_f64(b))); }); break;
                            case 0xA5: binary([](value a, value b) { return of_f64(wasm_max(as_f64(a), as_f64(b))); }); break;
                            case 0xA6: binary([](value a, value b) { return of_f64(std::copysign(as_f64(a), as_f64(b))); }); break;
                            // conversions
                            case 0xA7: unary([](value a) { return of_u32(static_cast<std::uint32_t>(a)); }); break;
                            case 0xA8: unary([](value a) { return of_i32(truncate<std::int32_t>(as_f32(a))); }); break;
                            case 0xA9: unary([](value a) { return of_u32(truncate<std::uint32_t>(as_f32(a))); }); break;
                            case 0xAA: unary([](value a) { return of_i32(truncate<std::int32_t>(as_f64(a))); }); break;
                            case 0xAB: unary([](value a) { return of_u32(truncate<std::uint32_t>(as_f64(a))); }); break;
                            case 0xAC: unary([](value a) { return of_i64(as_i32(a)); }); break;
                            case 0xAD: unary([](value a) { return static_cast<value>(as_u32(a)); }); break;
                            case 0xAE: unary([](value a) { return of_i64(truncate<std::int64_t>(as_f32(a))); }); break;
                            case 0xAF: unary([](value a) { return static_cast<value>(truncate<std::uint64_t>(as_f32(a))); }); break;
                            case 0xB0: unary([](value a) { return of_i64(truncate<std::int64_t>(as_f64(a))); }); break;
                            case 0xB1: unary([](value a) { return static_cast<value>(truncate<std::uint64_t>(as_f64(a))); }); break;
                            case 0xB2: unary([](value a) { return of_f32(static_cast<float>(as_i32(a))); }); break;
                            case 0xB3: unary([](value a) { return of_f32(static_cast<float>(as_u32(a))); }); break;
                            case 0xB4: unary([](value a) { return of_f32(static_cast<float>(as_i64(a))); }); break;
                            case 0xB5: unary([](value a) { return of_f32(static_cast<float>(a)); }); break;
                            case 0xB6: unary([](value a) { return of_f32(static_cast<float>(as_f64(a))); }); break;
                            case 0xB7: unary([](value a) { return of_f64(static_cast<double>(as_i32(a))); }); break;
                            case 0xB8: unary([](value a) { return of_f64(static_cast<double>(as_u32(a))); }); break;
                            case 0xB9: unary([](value a) { return of_f64(static_cast<double>(as_i64(a))); }); break;
                            case 0xBA: unary([](value a) { return of_f64(static_cast<double>(a)); }); break;
                            case 0xBB: unary([](value a) { return of_f64(static_cast<double>(as_f32(a))); }); break;
                            case 0xC0: unary([](value a) { return of_i32(static_cast<std::int8_t>(as_u32(a))); }); break;
                            case 0xC1: unary([](value a) { return of_i32(static_cast<std::int16_t>(as_u32(a))); }); break;
                            default:
                                throw trap{ "unsupported instruction 0x" + to_hex(op) };
                            }
                        }
                    }
                    std::uint32_t global(std::string const& aName) const
                    {
                        auto existing = iGlobalExports.find(aName);
                        if (existing == iGlobalExports.end())
                            return 0u;
                        return as_u32(iGlobals.at(existing->second));
                    }
                private:
                    template <typename T, typename F>
                    static T truncate(F aValue)
                    {
                        if (std::isnan(aValue))
                            throw trap{ "invalid conversion to integer" };
                        auto const t = static_cast<long double>(std::trunc(aValue));
                        long double const lowest = static_cast<long double>(std::numeric_limits<T>::min());
                        long double const limit = static_cast<long double>(std::numeric_limits<T>::max()) + 1.0L;
                        if (t < lowest || t >= limit)
                            throw trap{ "integer overflow" };
                        return static_cast<T>(t);
                    }
                    template <typename F>
                    static F wasm_min(F aLhs, F aRhs)
                    {
                        if (std::isnan(aLhs) || std::isnan(aRhs))
                            return std::numeric_limits<F>::quiet_NaN();
                        if (aLhs == aRhs)
                            return std::signbit(aLhs) ? aLhs : aRhs;
                        return aLhs < aRhs ? aLhs : aRhs;
                    }
                    template <typename F>
                    static F wasm_max(F aLhs, F aRhs)
                    {
                        if (std::isnan(aLhs) || std::isnan(aRhs))
                            return std::numeric_limits<F>::quiet_NaN();
                        if (aLhs == aRhs)
                            return std::signbit(aLhs) ? aRhs : aLhs;
                        return aLhs > aRhs ? aLhs : aRhs;
                    }
                    static std::string to_hex(std::uint32_t aValue)
                    {
                        std::ostringstream oss;
                        oss << std::hex << aValue;
                        return oss.str();
                    }
                    void parse()
                    {
                        reader r{ iModule };
                        if (r.raw<std::uint32_t>() != 0x6D736100u || r.raw<std::uint32_t>() != 1u)
                            throw exceptions::invalid_instruction();
                        std::vector<std::uint32_t> definedTypes;
                        while (r.position() < iModule.size())
                        {
                            auto const id = r.byte();
                            auto const size = static_cast<std::size_t>(r.u());
                            auto const end = r.position() + size;
                            switch (id)
                            {
                            case 1:
                                for (auto n = r.u(); n > 0u; --n)
                                {
                                    if (r.byte() != 0x60u)
                                        throw exceptions::invalid_instruction();
                                    function_type t;
                                    for (auto p = r.u(); p > 0u; --p)
                                        t.parameters.push_back(r.byte());
                                    for (auto p = r.u(); p > 0u; --p)
                                        t.results.push_back(r.byte());
                                    iTypes.push_back(t);
                                }
                                break;
                            case 2:
                                for (auto n = r.u(); n > 0u; --n)
                                {
                                    r.name();
                                    auto const field = r.name();
                                    if (r.byte() != 0x00u)
                                        throw trap{ "unsupported import kind" };
                                    function f;
                                    f.type = static_cast<std::uint32_t>(r.u());
                                    f.import = field;
                                    iImportTypes[field] = f.type;
                                    iFunctions.push_back(f);
                                }
                                break;
                            case 3:
                                for (auto n = r.u(); n > 0u; --n)
                                    definedTypes.push_back(static_cast<std::uint32_t>(r.u()));
                                break;
                            case 5:
                                for (auto n = r.u(); n > 0u; --n)
                                {
                                    auto const flags = r.byte();
                                    iMemory.resize(static_cast<std::size_t>(r.u()) * 65536u);
                                    if (flags & 1u)
                                        r.u();
                                }
                                break;
                            case 6:
                                for (auto n = r.u(); n > 0u; --n)
                                {
                                    iGlobalTypes.push_back(r.byte());
                                    r.byte();
                                    iGlobals.push_back(constant(r));
                                }
                                break;
                            case 7:
                                for (auto n = r.u(); n > 0u; --n)
                                {
                                    auto const name = r.name();
                                    auto const kind = r.byte();
                                    auto const index = static_cast<std::uint32_t>(r.u());
                                    if (kind == 0x00u)
                                        iFunctionExports[name] = index;
                                    else if (kind == 0x03u)
                                        iGlobalExports[name] = index;
                                }
                                break;
                            case 10:
                                {
                                    auto const count = r.u();
                                    for (std::uint64_t i = 0u; i < count; ++i)
                                    {
                                        function f;
                                        f.type = definedTypes.at(i);
                                        auto const bodySize = static_cast<std::size_t>(r.u());
                                        auto const bodyEnd = r.position() + bodySize;
                                        for (auto groups = r.u(); groups > 0u; --groups)
                                        {
                                            auto const n = r.u();
                                            auto const t = r.byte();
                                            f.locals.insert(f.locals.end(), static_cast<std::size_t>(n), t);
                                        }
                                        f.codeBegin = r.position();
                                        f.codeEnd = bodyEnd;
                                        match_blocks(f);
                                        iFunctions.push_back(f);
                                        r.seek(bodyEnd);
                                    }
                                }
                                break;
                            case 11:
                                for (auto n = r.u(); n > 0u; --n)
                                {
                                    if (r.u() != 0u)
                                        throw trap{ "unsupported data segment" };
                                    auto const offset = static_cast<std::size_t>(as_u32(constant(r)));
                                    auto const length = static_cast<std::size_t>(r.u());
                                    if (offset + length > iMemory.size())
                                        throw trap{ "data segment out of bounds" };
                                    for (std::size_t i = 0u; i < length; ++i)
                                        iMemory[offset + i] = r.byte();
                                }
                                break;
                            default:
                                break;
                            }
                            r.seek(end);
                        }
                    }
                    static value constant(reader& r)
                    {
                        value result = 0u;
                        switch (r.byte())
                        {
                        case 0x41: result = of_i32(static_cast<std::int32_t>(r.s())); break;
                        case 0x42: result = of_i64(r.s()); break;
                        case 0x43: result = of_f32(r.raw<float>()); break;
                        case 0x44: result = of_f64(r.raw<double>()); break;
                        default: throw trap{ "unsupported constant expression" };
                        }
                        if (r.byte() != 0x0Bu)
                            throw trap{ "unsupported constant expression" };
                        return result;
                    }
                    void match_blocks(function& f)
                    {
                        // decode the body into tokens, matching blocks with their ends (and ifs with their elses)
                        reader r{ iModule, f.codeBegin };
                        auto const begin = iTokens.size();
                        auto token = [&](std::uint64_t aValue) { iTokens.push_back(aValue); iEnds.push_back(0u); iElses.push_back(0u); };
                        std::vector<std::size_t> open;
                        while (r.position() < f.codeEnd)
                        {
                            auto const position = iTokens.size();
                            auto const op = r.byte();
                            token(op);
                            switch (op)
                            {
                            case 0x02: case 0x03: case 0x04:
                                token(r.byte());
                                open.push_back(position);
                                break;
                            case 0x05:
                                iElses[open.back()] = iTokens.size();
                                break;
                            case 0x0B:
                                if (!open.empty())
                                {
                                    iEnds[open.back()] = iTokens.size();
                                    open.pop_back();
                                }
                                break;
                            case 0x0C: case 0x0D: case 0x10: case 0x20: case 0x21: case 0x22: case 0x23: case 0x24:
                                token(r.u());
                                break;
                            case 0x0E:
                                {
                                    auto const n = r.u();
                                    token(n);
                                    for (auto i = n + 1u; i > 0u; --i)
                                        token(r.u());
                                }
                                break;
                            case 0x11:
                                token(r.u());
                                token(r.u());
                                break;
                            case 0x3F: case 0x40:
                                token(r.byte());
                                break;
                            case 0x41: case 0x42:
                                token(static_cast<std::uint64_t>(r.s()));
                                break;
                            case 0x43:
                                {
                                    std::uint64_t bits = 0u;
                                    auto const v = r.raw<float>();
                                    std::memcpy(&bits, &v, sizeof(v));
                                    token(bits);
                                }
                                break;
                            case 0x44:
                                {
                                    std::uint64_t bits = 0u;
                                    auto const v = r.raw<double>();
                                    std::memcpy(&bits, &v, sizeof(v));
                                    token(bits);
                                }
                                break;
                            default:
                                if (op >= 0x28u && op <= 0x3Eu)
                                {
                                    token(r.u());
                                    token(r.u());
                                }
                                break;
                            }
                        }
                        f.codeBegin = begin;
                        f.codeEnd = iTokens.size();
                    }
                    // host functions (module "env")
                    std::string read_string(std::uint32_t aAddress)
                    {
                        if (aAddress + 4u > iMemory.size())
                            throw trap{ "out of bounds memory access" };
                        std::uint32_t length;
                        std::memcpy(&length, &iMemory[aAddress], sizeof(length));
                        if (aAddress + 4u + length > iMemory.size())
                            throw trap{ "out of bounds memory access" };
                        return std::string{ reinterpret_cast<char const*>(&iMemory[aAddress + 4u]), length };
                    }
                    std::uint32_t allocate_string(std::string const& aValue)
                    {
                        if (!iHeap)
                            iHeap = global("__heap_base");
                        auto const address = *iHeap;
                        auto const size = 4u + static_cast<std::uint32_t>(aValue.size());
                        *iHeap = (address + size + 7u) & ~7u;
                        if (*iHeap > iMemory.size())
                            iMemory.resize((*iHeap / 65536u + 1u) * 65536u);
                        auto const length = static_cast<std::uint32_t>(aValue.size());
                        std::memcpy(&iMemory[address], &length, sizeof(length));
                        std::memcpy(&iMemory[address + 4u], aValue.data(), aValue.size());
                        return address;
                    }
                    std::vector<value> host(std::string const& aName, std::vector<value> const& aArguments)
                    {
                        if (aName == "neo::print")
                        {
                            iOutput << read_string(as_u32(aArguments.at(0))) << std::flush;
                            return {};
                        }
                        else if (aName == "neo::input")
                        {
                            std::string line;
                            std::getline(iInput, line);
                            if (!line.empty() && line.back() == '\r')
                                line.pop_back();
                            return { of_u32(allocate_string(line)) };
                        }
                        else if (aName == "neo::length")
                            return { of_u32(static_cast<std::uint32_t>(read_string(as_u32(aArguments.at(0))).size())) };
                        else if (aName == "neo::to_integer")
                        {
                            try
                            {
                                return { of_i32(std::stoi(read_string(as_u32(aArguments.at(0))))) };
                            }
                            catch (...)
                            {
                                return { of_i32(0) };
                            }
                        }
                        else if (aName == "neo::to_string")
                            return { of_u32(allocate_string(std::to_string(as_i32(aArguments.at(0))))) };
                        else if (aName == "neos::string_concat")
                            return { of_u32(allocate_string(read_string(as_u32(aArguments.at(0))) + read_string(as_u32(aArguments.at(1))))) };
                        else if (aName == "neos::fmod")
                            return { of_f64(std::fmod(as_f64(aArguments.at(0)), as_f64(aArguments.at(1)))) };
                        else if (aName == "neos::string_compare")
                        {
                            auto const c = read_string(as_u32(aArguments.at(0))).compare(read_string(as_u32(aArguments.at(1))));
                            return { of_i32(c < 0 ? -1 : c > 0 ? 1 : 0) };
                        }
                        // python builtins (see packages/python/builtins.py)
                        auto const python_float = [](double aValue)
                        {
                            if (std::isnan(aValue))
                                return std::string{ "nan" };
                            if (std::isinf(aValue))
                                return std::string{ aValue < 0.0 ? "-inf" : "inf" };
                            // repr(): the shortest round-tripping digits; scientific notation if the exponent is < -4 or >= 16
                            char buffer[64];
                            auto const result = std::to_chars(buffer, buffer + sizeof(buffer), aValue, std::chars_format::scientific);
                            std::string_view const scientific{ buffer, result.ptr };
                            std::string text = std::signbit(aValue) ? "-" : "";
                            auto const mantissa = scientific.substr(text.size(), scientific.find('e') - text.size());
                            int const exponent = std::stoi(std::string{ scientific.substr(scientific.find('e') + 1u) });
                            std::string digits;
                            for (auto c : mantissa)
                                if (c != '.')
                                    digits += c;
                            if (exponent < -4 || exponent >= 16)
                            {
                                text += digits.substr(0u, 1u);
                                if (digits.size() > 1u)
                                    text += "." + digits.substr(1u);
                                auto const e = std::to_string(std::abs(exponent));
                                text += std::string{ "e" } + (exponent < 0 ? "-" : "+") + (e.size() < 2u ? "0" : "") + e;
                            }
                            else if (exponent < 0)
                                text += "0." + std::string(static_cast<std::size_t>(-exponent - 1), '0') + digits;
                            else
                            {
                                auto const integral = static_cast<std::size_t>(exponent) + 1u;
                                if (digits.size() < integral)
                                    digits.append(integral - digits.size(), '0');
                                text += digits.substr(0u, integral) + "." + (digits.size() > integral ? digits.substr(integral) : "0");
                            }
                            return text;
                        };
                        auto const trimmed = [](std::string const& aText)
                        {
                            auto const begin = aText.find_first_not_of(" \t\r\n\f\v");
                            auto const end = aText.find_last_not_of(" \t\r\n\f\v");
                            return begin == std::string::npos ? std::string{} : aText.substr(begin, end - begin + 1u);
                        };
                        auto const python_bool = [](value aValue) { return std::string{ as_i32(aValue) != 0 ? "True" : "False" }; };
                        if (aName == "print(string)")
                            iOutput << read_string(as_u32(aArguments.at(0))) << std::endl;
                        else if (aName == "print(i64)")
                            iOutput << as_i64(aArguments.at(0)) << std::endl;
                        else if (aName == "print(f64)")
                            iOutput << python_float(as_f64(aArguments.at(0))) << std::endl;
                        else if (aName == "print(bool)")
                            iOutput << python_bool(aArguments.at(0)) << std::endl;
                        else if (aName == "input()")
                        {
                            std::string line;
                            std::getline(iInput, line);
                            if (!line.empty() && line.back() == '\r')
                                line.pop_back();
                            return { of_u32(allocate_string(line)) };
                        }
                        else if (aName == "str(i64)")
                            return { of_u32(allocate_string(std::to_string(as_i64(aArguments.at(0))))) };
                        else if (aName == "str(f64)")
                            return { of_u32(allocate_string(python_float(as_f64(aArguments.at(0))))) };
                        else if (aName == "str(bool)")
                            return { of_u32(allocate_string(python_bool(aArguments.at(0)))) };
                        else if (aName == "int(string)")
                        {
                            // [whitespace] [sign] digits (with optional single underscores between digits) [whitespace]
                            auto const text = trimmed(read_string(as_u32(aArguments.at(0))));
                            std::string digits;
                            std::size_t i = (!text.empty() && (text[0] == '+' || text[0] == '-')) ? 1u : 0u;
                            for (bool digitBefore = false; i < text.size(); ++i)
                            {
                                if (text[i] >= '0' && text[i] <= '9')
                                    digits += text[i], digitBefore = true;
                                else if (text[i] == '_' && digitBefore && i + 1u < text.size() && text[i + 1u] >= '0' && text[i + 1u] <= '9')
                                    digitBefore = false;
                                else
                                    throw trap{ "invalid literal for int() with base 10: '" + text + "'" };
                            }
                            if (!text.empty() && text[0] == '-')
                                digits.insert(digits.begin(), '-');
                            std::int64_t result = 0;
                            if (digits.empty() || digits == "-" || std::from_chars(digits.data(), digits.data() + digits.size(), result).ec != std::errc{})
                                throw trap{ "int() result out of range or invalid literal: '" + text + "'" };
                            return { of_i64(result) };
                        }
                        else if (aName == "float(string)")
                        {
                            // the whole of the (trimmed) string must be a number (single underscores may separate digits)
                            auto text = trimmed(read_string(as_u32(aArguments.at(0))));
                            auto const digit = [&](std::size_t aIndex) { return aIndex < text.size() && text[aIndex] >= '0' && text[aIndex] <= '9'; };
                            for (std::size_t i = 0u; i < text.size(); ++i)
                                if (text[i] == '_' && i > 0u && digit(i - 1u) && digit(i + 1u))
                                    text.erase(i--, 1u);
                            if (text.find_first_of("_(") != std::string::npos)
                                throw trap{ "could not convert string to float: '" + text + "'" };
                            auto const sign = (!text.empty() && (text[0] == '+' || text[0] == '-')) ? 1u : 0u;
                            double result = 0.0;
                            auto const converted = std::from_chars(text.data() + sign, text.data() + text.size(), result);
                            if (sign == text.size() || text[sign] == '+' || text[sign] == '-' || 
                                converted.ec == std::errc::invalid_argument || converted.ptr != text.data() + text.size())
                                throw trap{ "could not convert string to float: '" + text + "'" };
                            if (converted.ec == std::errc::result_out_of_range)
                                result = std::strtod(text.c_str() + sign, nullptr);
                            return { of_f64(text[0] == '-' ? -result : result) };
                        }
                        else if (aName == "len(string)")
                        {
                            // code points
                            std::int64_t length = 0;
                            for (auto c : read_string(as_u32(aArguments.at(0))))
                                if ((static_cast<unsigned char>(c) & 0xC0u) != 0x80u)
                                    ++length;
                            return { of_i64(length) };
                        }
                        else if (aName == "abs(i64)")
                            return { of_i64(as_i64(aArguments.at(0)) < 0 ? -as_i64(aArguments.at(0)) : as_i64(aArguments.at(0))) };
                        else if (aName == "abs(f64)")
                            return { of_f64(std::fabs(as_f64(aArguments.at(0)))) };
                        else
                            return runtime_call(aName, aArguments);
                        return {};
                    }
                    // an imported function: a host function or a function of a runtime plugin (the latter is resolved 
                    // once and then called directly)
                    std::vector<value> call_import(std::uint32_t aFunction, std::vector<value> const& aArguments)
                    {
                        if (iImportRuntime.size() <= aFunction)
                            iImportRuntime.resize(iFunctions.size(), nullptr);
                        if (auto const runtime = iImportRuntime[aFunction])
                            return runtime::call(*runtime, aArguments, iMarshaller);
                        iLastRuntimeFunction = nullptr;
                        auto result = host(*iFunctions.at(aFunction).import, aArguments);
                        if (iLastRuntimeFunction != nullptr)
                            iImportRuntime[aFunction] = iLastRuntimeFunction;
                        return result;
                    }
                    // a function of a runtime plugin (called through libffi)
                    std::vector<value> runtime_call(std::string const& aName, std::vector<value> const& aArguments)
                    {
                        auto resolved = iRuntimeFunctions.find(aName);
                        if (resolved == iRuntimeFunctions.end())
                        {
                            auto const type = iImportTypes.find(aName);
                            if (type == iImportTypes.end())
                                throw trap{ "unresolved import '" + aName + "'" };
                            try
                            {
                                auto const& t = iTypes.at(type->second);
                                auto const& f = runtime::resolve(aName, t.parameters, t.results);
                                if (iRestricted && runtime::restricted(f))
                                    throw trap{ "'" + aName + "' is restricted: it can change the host environment and the program is running in restricted mode" };
                                resolved = iRuntimeFunctions.emplace(aName, &f).first;
                            }
                            catch (std::exception const& e)
                            {
                                throw trap{ e.what() };
                            }
                        }
                        iLastRuntimeFunction = resolved->second;
                        return runtime::call(*resolved->second, aArguments, iMarshaller);
                    }
                private:
                    std::ostream& iOutput;
                    std::istream& iInput;
                    std::uint64_t& iInstructions;
                    bool const iRestricted; ///< runtime plugin functions that can change the host environment cannot be called
                    std::vector<std::uint8_t> iModule;
                    std::vector<std::uint64_t> iTokens;   ///< decoded function bodies
                    std::vector<std::size_t> iEnds;       ///< block/loop/if token -> token after its end
                    std::vector<std::size_t> iElses;      ///< if token -> token after its else (0 if none)
                    std::vector<function_type> iTypes;
                    std::vector<function> iFunctions;
                    std::vector<std::uint8_t> iMemory;
                    std::vector<value> iGlobals;
                    std::vector<std::uint8_t> iGlobalTypes;
                    std::unordered_map<std::string, std::uint32_t> iImportTypes;
                    std::unordered_map<std::string, runtime::function const*> iRuntimeFunctions;
                    std::vector<runtime::function const*> iImportRuntime;
                    runtime::function const* iLastRuntimeFunction = nullptr;
                    runtime::marshaller const iMarshaller
                    {
                        [this](std::uint32_t aAddress) { return read_string(aAddress); },
                        [this](std::string const& aString) { return allocate_string(aString); }
                    };
                    std::unordered_map<std::string, std::uint32_t> iFunctionExports;
                    std::unordered_map<std::string, std::uint32_t> iGlobalExports;
                    std::optional<std::uint32_t> iHeap;
                };
            }

            namespace
            {
                std::optional<std::string> environment_variable(char const* aName)
                {
#if defined(_MSC_VER)
                    char* buffer = nullptr;
                    std::size_t size = 0u;
                    if (::_dupenv_s(&buffer, &size, aName) != 0 || buffer == nullptr)
                        return {};
                    std::string const result{ buffer };
                    std::free(buffer);
                    return result;
#else
                    if (auto const value = std::getenv(aName))
                        return std::string{ value };
                    return {};
#endif
                }
            }

            void thread::execute()
            {
                auto const start = std::chrono::steady_clock::now();
                try
                {
                    if (iText.empty())
                        throw exceptions::no_text();
                    machine m{ iText, iOutput, iInput, iInstructions, iRestricted };
                    auto const entry = m.export_function("main");
                    if (!entry)
                        throw std::runtime_error("no entry point (main)");
                    // native code (JIT) except when debugging: debug builds interpret (NEOS_JIT=0/1 overrides)
#if defined(NDEBUG)
                    bool useJit = true;
#else
                    bool useJit = false;
#endif
                    if (auto const setting = environment_variable("NEOS_JIT"))
                        useJit = *setting != "0";
                    jit::compiled_module_ptr native;
                    if (useJit)
                        native = m.compile();
                    iJit = static_cast<bool>(native);
                    auto const results = native ? jit::invoke(*native, *entry, {}) : m.invoke(*entry, {});
                    auto const& type = m.type_of(*entry);
                    if (!results.empty())
                    {
                        switch (type.results[0])
                        {
                        case 0x7F: iResult = language::data<language::i32>{ as_i32(results[0]) }; break;
                        case 0x7E: iResult = language::data<language::i64>{ as_i64(results[0]) }; break;
                        case 0x7D: iResult = language::data<language::f32>{ as_f32(results[0]) }; break;
                        case 0x7C: iResult = language::data<language::f64>{ as_f64(results[0]) }; break;
                        }
                    }
                }
                catch (std::exception const& e)
                {
                    iError = e.what();
                    iOutput << "Runtime error: " << e.what() << std::endl;
                }
                iExecutionTime = std::chrono::steady_clock::now() - start;
            }

            std::string thread::metrics() const
            {
                std::ostringstream oss;
                oss << "Instructions executed: ";
                if (iJit)
                    oss << "(native code)";
                else
                    oss << iInstructions;
                oss << ", execution time: " <<
                    std::chrono::duration_cast<std::chrono::microseconds>(iExecutionTime).count() / 1000.0 << "ms" << std::endl;
                return oss.str();
            }
        }
    }
}