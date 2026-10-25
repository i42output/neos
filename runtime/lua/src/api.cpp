/*
  api.cpp

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

// The interface between the runtime and compiled code: the functions that code compiled from Lua calls (as imports named "lua::f";
// see the Lua compiler's codegen.cpp). A function that fails returns Exception (its error is pending: the state's pending).

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include "lua.hpp"
#include "api.hpp"

namespace neos::lua_runtime
{
    namespace
    {
        template <typename F>
        value guard(F&& aFunction)
        {
            try
            {
                return aFunction();
            }
            catch (lua_error const& e)
            {
                the_state().pending = e.v;
                return Exception;
            }
            catch (std::bad_alloc const&)
            {
                try
                {
                    the_state().pending = make_string("not enough memory");
                }
                catch (...)
                {
                    the_state().pending = Nil;
                }
                return Exception;
            }
        }

        // (the program that is running: its information (cached))
        program_info& running_program()
        {
            NEOS_LUA_FAST_TLS thread_local program_info* tLast = nullptr;
            auto* const program = current_program();
            if (tLast != nullptr && tLast->program == program)
                return *tLast;
            tLast = find_program(program);
            if (tLast == nullptr)
            {
                std::fprintf(stderr, "lua: a program that the runtime does not know called it\n");
                std::abort();
            }
            return *tLast;
        }

        std::size_t program_results()
        {
            return static_cast<std::size_t>(*running_program().results);
        }

        value env_of(value aCallee)
        {
            auto const e = fun(aCallee).environment;
            return is_nil(e) ? the_state().globals : cell_of<box_cell>(e).v;
        }

        // (a numeric for loop's state (see forprep))
        struct for_state
        {
            std::int64_t integers[3] = {};
            double floats[3] = {};
        };
        thread_local for_state tFor;

        std::string text_of_literal(char const* aText)
        {
            // (01 02: NUL, 01 01: 01 (see the compiler's literal_encoding))
            std::string result;
            for (auto p = aText; *p != '\0'; ++p)
            {
                if (*p == '\x01' && (p[1] == '\x01' || p[1] == '\x02'))
                {
                    result.push_back(p[1] == '\x02' ? '\0' : '\x01');
                    ++p;
                }
                else
                    result.push_back(*p);
            }
            return result;
        }

        // (the cells' fields that generated code reads (see neos::language::lua::layout) are where it reads them)
        void check_layout()
        {
            auto const at = [](void const* aCell, void const* aField) { return static_cast<std::uint32_t>(static_cast<std::byte const*>(aField) - static_cast<std::byte const*>(aCell)); };
            string_cell const s{ std::string{} };
            table const t;
            box_cell const b{ Nil };
            function_cell const f;
            bool const ok = at(&s, &s.hashValue) == layout::StringHash &&
                at(&t, &t.array) == layout::TableArray && at(&t, &t.metatable) == layout::TableMetatable &&
                at(&t, &t.nodes) == layout::TableNodes && at(&t, &t.used) == layout::TableUsed && sizeof(table::node) == layout::NodeSize &&
                sizeof(t.used) == 8u && at(&b, &b.v) == layout::BoxValue &&
                at(&f, &f.upvalues) == layout::FunctionUpvalues && at(&f, &f.environment) == layout::FunctionEnvironment;
            if (!ok)
            {
                std::fputs("Lua runtime: its cells' layout is not the compiler's\n", stderr);
                std::abort();
            }
        }

        void initialize()
        {
            auto& s = the_state();
            if (s.initialized)
                return;
            check_layout();
            gc_pause const paused;
            char const* const names[] = { "__index", "__newindex", "__gc", "__mode", "__len", "__eq", "__add", "__sub", "__mul", "__mod", "__pow",
                "__div", "__idiv", "__band", "__bor", "__bxor", "__shl", "__shr", "__unm", "__bnot", "__lt", "__le", "__concat", "__call",
                "__close", "__tostring", "__name", "__metatable", "__pairs" };
            for (std::size_t i = 0u; i < NameCount; ++i)
                s.names[i] = intern(names[i], true);
            s.globals = make_table(0u, 64u);
            s.registry = make_table();
            s.mainThread = make_thread(Nil);
            cell_of<thread_cell>(s.mainThread).status = thread_cell::status_t::Running;
            open_libraries();
            s.initialized = true;
        }

        void report(value aError)
        {
            // (an error that the program did not handle: as Lua's standalone interpreter reports it)
            std::string message;
            auto const& traceback = uncaught_traceback();
            if (!traceback.empty())
                message = traceback;
            else if (is_string(aError) || is_number(aError))
                message = is_string(aError) ? str(aError) : number_text(aError);
            else
                message = "(error object is a " + type_name(aError) + " value)";
            flush_standard(1);
            write_standard(2, "lua: " + message + "\n");
            flush_standard(2);
        }
    }

    namespace api
    {
        value init(std::int32_t aDispatcher, std::int32_t aResultsGlobal)
        {
            // (the program's identity (see invoke))
            return guard([&]() -> value
            {
                register_program(static_cast<std::uint32_t>(aDispatcher), static_cast<std::uint32_t>(aResultsGlobal));
                initialize();
                return static_cast<value>(reinterpret_cast<std::uintptr_t>(current_program()));
            });
        }

        value string(char const* aText)
        {
            return guard([&]() -> value { return intern(text_of_literal(aText), true); });
        }

        value main_closure(std::int32_t aNumber, std::int32_t aIndex)
        {
            return guard([&]() -> value
            {
                auto& f = the_heap().make<function_cell>();
                auto const result = value_of(f);
                value_list keep{ result };
                f.number = static_cast<std::uint32_t>(aNumber);
                f.index = static_cast<std::uint32_t>(aIndex);
                f.program = current_program();
                f.environment = make_box(the_state().globals);
                return result;
            });
        }

        value load_complete(value aFunction)
        {
            loaded_chunk(aFunction);
            return Nil;
        }

        std::int32_t run_main(value aFunction)
        {
            value_list keep{ aFunction };
            try
            {
                set_script_arguments();
                value_list arguments;
                auto const a = index(the_state().globals, intern("arg"));
                if (is_table(a))
                    for (std::int64_t i = 1; ; ++i)
                    {
                        auto const v = tab(a).get_integer(i);
                        if (is_nil(v))
                            break;
                        arguments.push_back(v);
                    }
                value_list results;
                call(aFunction, arguments.span(), results);
                flush_standard(1);
                return 0;
            }
            catch (lua_error const& e)
            {
                auto& s = the_state();
                if (s.exitRequested)
                {
                    flush_standard(1);
                    return s.exitCode;
                }
                report(e.v);
                s.exitCode = 1;
                return 1;
            }
            catch (std::bad_alloc const&)
            {
                write_standard(2, "lua: not enough memory\n");
                return 1;
            }
        }

        std::int32_t exit_status()
        {
            return the_state().exitCode;
        }

        std::int32_t args_base(std::int32_t aCount)
        {
            auto const& extra = stacks().extra;
            if (aCount <= 4)
                return static_cast<std::int32_t>(extra.size());
            return static_cast<std::int32_t>(extra.size() - static_cast<std::size_t>(aCount - 4));
        }

        value argument(std::int32_t aCount, std::int32_t aBase, std::int32_t aIndex)
        {
            if (aIndex >= aCount)
                return Nil;
            return stacks().extra[static_cast<std::size_t>(aBase + aIndex - 4)];
        }

        value varargs(std::int32_t aCount, std::int32_t aParameters, value a0, value a1, value a2, value a3, std::int32_t aBase)
        {
            if (aCount <= aParameters)
                return Nil;
            return guard([&]() -> value
            {
                value const registers[] = { a0, a1, a2, a3 };
                auto& v = the_heap().make<varargs_cell>();
                auto const result = value_of(v);
                for (auto i = aParameters; i < aCount; ++i)
                    v.values.push_back(i < 4 ? registers[i] : stacks().extra[static_cast<std::size_t>(aBase + i - 4)]);
                return result;
            });
        }

        value vararg_get(value aVarargs, std::int32_t aIndex)
        {
            if (is_nil(aVarargs))
                return Nil;
            auto const& values = cell_of<varargs_cell>(aVarargs).values;
            return static_cast<std::size_t>(aIndex) < values.size() ? values[static_cast<std::size_t>(aIndex)] : Nil;
        }

        value vararg_results(value aVarargs)
        {
            return guard([&]() -> value
            {
                if (is_nil(aVarargs))
                {
                    set_program_results({});
                    return Nil;
                }
                auto const& values = cell_of<varargs_cell>(aVarargs).values;
                set_program_results(values);
                return values.empty() ? Nil : values[0];
            });
        }

        value result(std::int32_t aIndex)
        {
            auto const count = program_results();
            if (static_cast<std::size_t>(aIndex) >= count)
                return Nil;
            auto const& r = stacks().results;
            return static_cast<std::size_t>(aIndex) - 1u < r.size() ? r[static_cast<std::size_t>(aIndex) - 1u] : Nil;
        }

        value ret_extra(std::int32_t aCount, value a1, value a2, value a3, value a4)
        {
            auto& r = stacks().results;
            value const values[] = { a1, a2, a3, a4 };
            r.assign(values, values + aCount);
            return Nil;
        }

        value ret_begin()
        {
            stacks().results.clear();
            return Nil;
        }

        value ret_push(value aValue)
        {
            return guard([&]() -> value
            {
                stacks().results.push_back(aValue);
                return Nil;
            });
        }

        std::int32_t stack_begin()
        {
            return static_cast<std::int32_t>(stacks().arguments.size());
        }

        value stack_push(value aValue)
        {
            return guard([&]() -> value
            {
                stacks().arguments.push_back(aValue);
                return Nil;
            });
        }

        value stack_push_results(value aFirst)
        {
            return guard([&]() -> value
            {
                auto const count = program_results();
                auto& s = stacks();
                if (count >= 1u)
                    s.arguments.push_back(aFirst);
                for (std::size_t i = 1u; i < count; ++i)
                    s.arguments.push_back(i - 1u < s.results.size() ? s.results[i - 1u] : Nil);
                return Nil;
            });
        }

        namespace
        {
            value_list pop_arguments(std::int32_t aBase)
            {
                auto& a = stacks().arguments;
                auto const base = static_cast<std::size_t>(aBase);
                value_list result{ std::span<value const>{ a.data() + base, a.size() - base } };
                a.resize(base);
                return result;
            }
        }

        value stack_call(value aFunction, std::int32_t aBase, value aDescription)
        {
            return guard([&]() -> value
            {
                auto const arguments = pop_arguments(aBase);
                value_list results;
                call(aFunction, arguments.span(), results, aDescription, true);
                set_program_results(results.span());
                return results.empty() ? Nil : results[0];
            });
        }

        value stack_return(std::int32_t aBase)
        {
            return guard([&]() -> value
            {
                auto const values = pop_arguments(aBase);
                set_program_results(values.span());
                return values.empty() ? Nil : values[0];
            });
        }

        value stack_concat(std::int32_t aBase)
        {
            return guard([&]() -> value
            {
                auto const values = pop_arguments(aBase);
                return concat(values.span());
            });
        }

        value push_arg(value aValue)
        {
            return guard([&]() -> value
            {
                stacks().extra.push_back(aValue);
                return Nil;
            });
        }

        value pop_args(std::int32_t aCount)
        {
            auto& extra = stacks().extra;
            extra.resize(extra.size() - std::min(extra.size(), static_cast<std::size_t>(aCount)));
            return Nil;
        }

        std::int32_t callee_number(value aCallee)
        {
            // (the program's own Lua function: its number for the dispatcher; else -1 (the runtime calls it))
            if (!is_function(aCallee))
                return -1;
            auto& f = fun(aCallee);
            if (f.native || f.program != current_program())
                return -1;
            return static_cast<std::int32_t>(f.number);
        }

        value call_(value aCallee, std::int32_t aCount, value a0, value a1, value a2, value a3, value aDescription);

        value invoke(value aCallee, value aProgram, std::int32_t aCount, value a0, value a1, value a2, value a3, value aDescription)
        {
            // (the calling program's own Lua function: a dispatch (the program calls it); others are called)
            if (is_function(aCallee))
            {
                auto const& f = fun(aCallee);
                if (!f.native)
                {
                    if (reinterpret_cast<std::uintptr_t>(f.program) == aProgram)
                        return Dispatch | f.number;
                }
                else if (f.fast != nullptr && aCount <= 4)
                {
                    // (a native function's fast version (if it does not decline): its results (one or two))
                    value const arguments[] = { a0, a1, a2, a3 };
                    value second = Empty;
                    auto const result = f.fast(arguments, static_cast<std::size_t>(aCount), second);
                    if (result != Empty)
                    {
                        if (second == Empty)
                            set_results_count(1u, reinterpret_cast<void*>(aProgram));
                        else
                        {
                            auto& r = stacks().results;
                            r.clear();
                            r.push_back(second);
                            set_results_count(2u, reinterpret_cast<void*>(aProgram));
                        }
                        return result;
                    }
                }
            }
            return call_(aCallee, aCount, a0, a1, a2, a3, aDescription);
        }

        value call_(value aCallee, std::int32_t aCount, value a0, value a1, value a2, value a3, value aDescription)
        {
            return guard([&]() -> value
            {
                // (up to four arguments: on this stack (which the collector scans); more: a list)
                value const registers[] = { a0, a1, a2, a3 };
                std::span<value const> span{ registers, static_cast<std::size_t>(std::min(aCount, 4)) };
                value_list arguments;
                if (aCount > 4)
                {
                    auto const& extra = stacks().extra;
                    auto const base = extra.size() - static_cast<std::size_t>(aCount - 4);
                    arguments->reserve(static_cast<std::size_t>(aCount));
                    for (std::int32_t i = 0; i < aCount; ++i)
                        arguments.push_back(i < 4 ? registers[i] : extra[base + static_cast<std::size_t>(i - 4)]);
                    span = arguments.span();
                }
                value_list results;
                call(aCallee, span, results, aDescription, true);
                set_program_results(results.span());
                return results.empty() ? Nil : results[0];
            });
        }

        value closure(std::int32_t aNumber, std::int32_t aIndex, std::int32_t aUpvalues, value aParent)
        {
            return guard([&]() -> value
            {
                auto& f = the_heap().make<function_cell>();
                f.number = static_cast<std::uint32_t>(aNumber);
                f.index = static_cast<std::uint32_t>(aIndex);
                f.program = current_program();
                f.upvalues.resize(static_cast<std::size_t>(aUpvalues), Nil);
                f.environment = fun(aParent).environment;
                return value_of(f);
            });
        }

        value closure_upval(value aClosure, std::int32_t aIndex, value aBox)
        {
            fun(aClosure).upvalues[static_cast<std::size_t>(aIndex)] = aBox;
            return aClosure;
        }

        value box_new(value aValue)
        {
            return guard([&]() -> value { return make_box(aValue); });
        }

        value box_get(value aBox)
        {
            return cell_of<box_cell>(aBox).v;
        }

        value box_set(value aBox, value aValue)
        {
            cell_of<box_cell>(aBox).v = aValue;
            return Nil;
        }

        value upval_get(value aCallee, std::int32_t aIndex)
        {
            return cell_of<box_cell>(fun(aCallee).upvalues[static_cast<std::size_t>(aIndex)]).v;
        }

        value upval_set(value aCallee, std::int32_t aIndex, value aValue)
        {
            cell_of<box_cell>(fun(aCallee).upvalues[static_cast<std::size_t>(aIndex)]).v = aValue;
            return Nil;
        }

        value upval_box(value aCallee, std::int32_t aIndex)
        {
            return fun(aCallee).upvalues[static_cast<std::size_t>(aIndex)];
        }

        value getglobal(value aCallee, value aName)
        {
            auto const env = env_of(aCallee);
            if (is_table(env))
            {
                auto& t = tab(env);
                auto const v = t.get_string(aName);
                if (v != Nil || !is_table(t.metatable))
                    return v;
            }
            return guard([&]() -> value { return index(env, aName, make_string("upvalue '_ENV'")); });
        }

        value setglobal(value aCallee, value aName, value aValue)
        {
            return guard([&]() -> value
            {
                set_index(env_of(aCallee), aName, aValue, make_string("upvalue '_ENV'"));
                return Nil;
            });
        }

        value getenv(value aCallee)
        {
            return env_of(aCallee);
        }

        value setenv(value aCallee, value aValue)
        {
            return guard([&]() -> value
            {
                auto& f = fun(aCallee);
                if (is_nil(f.environment))
                    f.environment = make_box(aValue);
                else
                    cell_of<box_cell>(f.environment).v = aValue;
                return Nil;
            });
        }

        value index_(value aObject, value aKey, value aDescription)
        {
            if (is_table(aObject))
            {
                auto& t = tab(aObject);
                auto const v = t.get(aKey);
                if (v != Nil || !is_table(t.metatable))
                    return v;
                return guard([&]() -> value { return index(aObject, aKey, aDescription, true); });
            }
            return guard([&]() -> value { return index(aObject, aKey, aDescription); });
        }

        value setindex(value aObject, value aKey, value aValue, value aDescription)
        {
            return guard([&]() -> value
            {
                set_index(aObject, aKey, aValue, aDescription);
                return Nil;
            });
        }

        std::int64_t int_value(value aInteger)
        {
            // (an integer's value (an integer variable's i64: see the compiler's infer_integers))
            return integer_of(aInteger);
        }

        value int_box(std::int64_t aValue)
        {
            return guard([&]() -> value { return make_integer(aValue); });
        }

        value forprep(value aStart, value aLimit, value aStep)
        {
            // (follows forprep and forlimit in the Lua reference implementation's lvm.c (Copyright (c) 1994-2023 Lua.org, PUC-Rio, MIT
            // licence; see runtime/lua/NOTICE))
            // (Lua's numeric for: an integer loop if its start and step are integers (its limit is clipped to an integer), else a float
            // loop (numerals are converted); the result: 0 (no iterations), 1 (an integer loop) or 2 (a float loop), as an inline integer)
            return guard([&]() -> value
            {
                auto const for_error = [](value aValue, char const* aWhat)
                {
                    runtime_error(std::string{ "bad 'for' " } + aWhat + " (number expected, got " + object_type_name(aValue) + ")");
                };
                if (is_integer(aStart) && is_integer(aStep))
                {
                    auto const start = integer_of(aStart);
                    auto const step = integer_of(aStep);
                    if (step == 0)
                        runtime_error("'for' step is zero");
                    std::int64_t limit;
                    value limitNumber;
                    if (!to_number(aLimit, limitNumber))
                        for_error(aLimit, "limit");
                    if (is_integer(limitNumber))
                        limit = integer_of(limitNumber);
                    else
                    {
                        auto const f = float_of(limitNumber);
                        if (std::isnan(f))
                            return make_integer(0);
                        if (step > 0)
                        {
                            auto const fl = std::floor(f);
                            if (fl >= 9223372036854775808.0)
                                limit = std::numeric_limits<std::int64_t>::max();
                            else if (fl < -9223372036854775808.0)
                                return make_integer(0);
                            else
                                limit = static_cast<std::int64_t>(fl);
                        }
                        else
                        {
                            auto const cl = std::ceil(f);
                            if (cl < -9223372036854775808.0)
                                limit = std::numeric_limits<std::int64_t>::min();
                            else if (cl >= 9223372036854775808.0)
                                return make_integer(0);
                            else
                                limit = static_cast<std::int64_t>(cl);
                        }
                    }
                    if (step > 0 ? start > limit : start < limit)
                        return make_integer(0);
                    std::uint64_t count;
                    if (step > 0)
                        count = (static_cast<std::uint64_t>(limit) - static_cast<std::uint64_t>(start)) / static_cast<std::uint64_t>(step);
                    else
                        count = (static_cast<std::uint64_t>(start) - static_cast<std::uint64_t>(limit)) /
                            (static_cast<std::uint64_t>(-(step + 1)) + 1u);
                    tFor.integers[0] = start;
                    tFor.integers[1] = static_cast<std::int64_t>(count);
                    tFor.integers[2] = step;
                    return make_integer(1);
                }
                value limit, step, start;
                if (!to_number(aLimit, limit))
                    for_error(aLimit, "limit");
                if (!to_number(aStep, step))
                    for_error(aStep, "step");
                if (!to_number(aStart, start))
                    for_error(aStart, "initial value");
                auto const fstart = number_of(start);
                auto const flimit = number_of(limit);
                auto const fstep = number_of(step);
                if (fstep == 0.0)
                    runtime_error("'for' step is zero");
                if (0.0 < fstep ? flimit < fstart : fstart < flimit)
                    return make_integer(0);
                tFor.floats[0] = fstart;
                tFor.floats[1] = flimit;
                tFor.floats[2] = fstep;
                return make_integer(2);
            });
        }

        std::int64_t for_integer(std::int32_t aIndex)
        {
            return tFor.integers[aIndex];
        }

        double for_float(std::int32_t aIndex)
        {
            return tFor.floats[aIndex];
        }

        std::int32_t tbc_mark()
        {
            return static_cast<std::int32_t>(stacks().tbc.size());
        }

        value tbc_push(value aValue, value aName)
        {
            return guard([&]() -> value
            {
                if (truthy(aValue) && is_nil(metamethod(aValue, NameClose)))
                    runtime_error("variable '" + str(aName) + "' got a non-closable value");
                stacks().tbc.push_back(aValue);
                return Nil;
            });
        }

        value tbc_close(std::int32_t aDepth)
        {
            return guard([&]() -> value
            {
                close_tbc(static_cast<std::size_t>(aDepth), Nil, false);
                return Nil;
            });
        }

        value tbc_close_error(std::int32_t aDepth)
        {
            // (an error: the function's to-be-closed variables are closed with it (an error that they raise replaces it))
            auto& s = the_state();
            value_list keep{ s.pending };
            auto const error = s.pending;
            try
            {
                close_tbc(static_cast<std::size_t>(aDepth), error, true);
                s.pending = error;
            }
            catch (lua_error const& e)
            {
                s.pending = e.v;
            }
            catch (...)
            {
                s.pending = error;
            }
            return Exception;
        }

        value table_new(std::int32_t aArray, std::int32_t aHash)
        {
            return guard([&]() -> value { return make_table(static_cast<std::size_t>(aArray), static_cast<std::size_t>(aHash)); });
        }

        value table_fields(value aTable, value aKey1, value aValue1, value aKey2, value aValue2, value aKey3, value aValue3)
        {
            // (a table constructor's fields (a key Empty: none))
            return guard([&]() -> value
            {
                for (auto const& [k, v] : { std::pair{ aKey1, aValue1 }, std::pair{ aKey2, aValue2 }, std::pair{ aKey3, aValue3 } })
                    if (k != Empty)
                        raw_set(aTable, k, v);
                return Nil;
            });
        }

        value table_seti4(value aTable, std::int32_t aIndex, std::int32_t aCount, value aValue1, value aValue2, value aValue3, value aValue4)
        {
            return guard([&]() -> value
            {
                auto& t = tab(aTable);
                value const values[] = { aValue1, aValue2, aValue3, aValue4 };
                for (std::int32_t i = 0; i < aCount; ++i)
                    t.set_integer(aIndex + i, values[i]);
                return Nil;
            });
        }

        value table_set(value aTable, value aKey, value aValue)
        {
            return guard([&]() -> value
            {
                raw_set(aTable, aKey, aValue);
                return Nil;
            });
        }

        value table_seti(value aTable, std::int32_t aIndex, value aValue)
        {
            return guard([&]() -> value
            {
                tab(aTable).set_integer(aIndex, aValue);
                return Nil;
            });
        }

        value table_append_results(value aTable, std::int32_t aIndex, value aFirst)
        {
            return guard([&]() -> value
            {
                auto const count = program_results();
                auto& t = tab(aTable);
                value_list values;
                if (count >= 1u)
                    values.push_back(aFirst);
                auto const& r = stacks().results;
                for (std::size_t i = 1u; i < count; ++i)
                    values.push_back(i - 1u < r.size() ? r[i - 1u] : Nil);
                for (std::size_t i = 0u; i < values.size(); ++i)
                    t.set_integer(aIndex + static_cast<std::int64_t>(i), values[i]);
                return Nil;
            });
        }

        value arith_(std::int32_t aOperation, value aLeft, value aRight, value aLeftDescription, value aRightDescription)
        {
            return guard([&]() -> value { return arith(aOperation, aLeft, aRight, aLeftDescription, aRightDescription); });
        }

        value unm(value aValue, value aDescription)
        {
            return guard([&]() -> value { return unary_minus(aValue, aDescription); });
        }

        value len(value aValue, value aDescription)
        {
            return guard([&]() -> value { return length(aValue, aDescription); });
        }

        value bnot(value aValue, value aDescription)
        {
            return guard([&]() -> value { return bitwise_not(aValue, aDescription); });
        }

        value concat_(value aLeft, value aRight)
        {
            return guard([&]() -> value { return concat(aLeft, aRight); });
        }

        value eq(value aLeft, value aRight)
        {
            return guard([&]() -> value { return boolean(equal(aLeft, aRight)); });
        }

        value lt(value aLeft, value aRight)
        {
            return guard([&]() -> value { return boolean(less_than(aLeft, aRight)); });
        }

        value le(value aLeft, value aRight)
        {
            return guard([&]() -> value { return boolean(less_equal(aLeft, aRight)); });
        }
    }

    std::span<neos_rt_function const> api_functions()
    {
        constexpr auto I32 = NEOS_RT_I32;
        constexpr auto I64 = NEOS_RT_I64;
        constexpr auto F64 = NEOS_RT_F64;
        constexpr auto STR = NEOS_RT_STRING;
        static neos_rt_function const sFunctions[] =
        {
            { "init", NEOS_RT_FUNCTION(api::init), I64, 2u, { I32, I32 }, 0u },
            { "string", NEOS_RT_FUNCTION(api::string), I64, 1u, { STR }, 0u },
            { "main_closure", NEOS_RT_FUNCTION(api::main_closure), I64, 2u, { I32, I32 }, 0u },
            { "load_complete", NEOS_RT_FUNCTION(api::load_complete), I64, 1u, { I64 }, 0u },
            { "run_main", NEOS_RT_FUNCTION(api::run_main), I32, 1u, { I64 }, 0u },
            { "exit_status", NEOS_RT_FUNCTION(api::exit_status), I32, 0u, {}, 0u },
            { "args_base", NEOS_RT_FUNCTION(api::args_base), I32, 1u, { I32 }, 0u },
            { "argument", NEOS_RT_FUNCTION(api::argument), I64, 3u, { I32, I32, I32 }, 0u },
            { "varargs", NEOS_RT_FUNCTION(api::varargs), I64, 7u, { I32, I32, I64, I64, I64, I64, I32 }, 0u },
            { "vararg_get", NEOS_RT_FUNCTION(api::vararg_get), I64, 2u, { I64, I32 }, 0u },
            { "vararg_results", NEOS_RT_FUNCTION(api::vararg_results), I64, 1u, { I64 }, 0u },
            { "result", NEOS_RT_FUNCTION(api::result), I64, 1u, { I32 }, 0u },
            { "ret_extra", NEOS_RT_FUNCTION(api::ret_extra), I64, 5u, { I32, I64, I64, I64, I64 }, 0u },
            { "ret_begin", NEOS_RT_FUNCTION(api::ret_begin), I64, 0u, {}, 0u },
            { "ret_push", NEOS_RT_FUNCTION(api::ret_push), I64, 1u, { I64 }, 0u },
            { "stack_begin", NEOS_RT_FUNCTION(api::stack_begin), I32, 0u, {}, 0u },
            { "stack_push", NEOS_RT_FUNCTION(api::stack_push), I64, 1u, { I64 }, 0u },
            { "stack_push_results", NEOS_RT_FUNCTION(api::stack_push_results), I64, 1u, { I64 }, 0u },
            { "stack_call", NEOS_RT_FUNCTION(api::stack_call), I64, 3u, { I64, I32, I64 }, 0u },
            { "stack_return", NEOS_RT_FUNCTION(api::stack_return), I64, 1u, { I32 }, 0u },
            { "stack_concat", NEOS_RT_FUNCTION(api::stack_concat), I64, 1u, { I32 }, 0u },
            { "push_arg", NEOS_RT_FUNCTION(api::push_arg), I64, 1u, { I64 }, 0u },
            { "pop_args", NEOS_RT_FUNCTION(api::pop_args), I64, 1u, { I32 }, 0u },
            { "callee_number", NEOS_RT_FUNCTION(api::callee_number), I32, 1u, { I64 }, 0u },
            { "call", NEOS_RT_FUNCTION(api::call_), I64, 7u, { I64, I32, I64, I64, I64, I64, I64 }, 0u },
            { "invoke", NEOS_RT_FUNCTION(api::invoke), I64, 8u, { I64, I64, I32, I64, I64, I64, I64, I64 }, 0u },
            { "table_fields", NEOS_RT_FUNCTION(api::table_fields), I64, 7u, { I64, I64, I64, I64, I64, I64, I64 }, 0u },
            { "table_seti4", NEOS_RT_FUNCTION(api::table_seti4), I64, 7u, { I64, I32, I32, I64, I64, I64, I64 }, 0u },
            { "closure", NEOS_RT_FUNCTION(api::closure), I64, 4u, { I32, I32, I32, I64 }, 0u },
            { "closure_upval", NEOS_RT_FUNCTION(api::closure_upval), I64, 3u, { I64, I32, I64 }, 0u },
            { "box_new", NEOS_RT_FUNCTION(api::box_new), I64, 1u, { I64 }, 0u },
            { "box_get", NEOS_RT_FUNCTION(api::box_get), I64, 1u, { I64 }, 0u },
            { "box_set", NEOS_RT_FUNCTION(api::box_set), I64, 2u, { I64, I64 }, 0u },
            { "upval_get", NEOS_RT_FUNCTION(api::upval_get), I64, 2u, { I64, I32 }, 0u },
            { "upval_set", NEOS_RT_FUNCTION(api::upval_set), I64, 3u, { I64, I32, I64 }, 0u },
            { "upval_box", NEOS_RT_FUNCTION(api::upval_box), I64, 2u, { I64, I32 }, 0u },
            { "getglobal", NEOS_RT_FUNCTION(api::getglobal), I64, 2u, { I64, I64 }, 0u },
            { "setglobal", NEOS_RT_FUNCTION(api::setglobal), I64, 3u, { I64, I64, I64 }, 0u },
            { "getenv", NEOS_RT_FUNCTION(api::getenv), I64, 1u, { I64 }, 0u },
            { "setenv", NEOS_RT_FUNCTION(api::setenv), I64, 2u, { I64, I64 }, 0u },
            { "index", NEOS_RT_FUNCTION(api::index_), I64, 3u, { I64, I64, I64 }, 0u },
            { "setindex", NEOS_RT_FUNCTION(api::setindex), I64, 4u, { I64, I64, I64, I64 }, 0u },
            { "int_box", NEOS_RT_FUNCTION(api::int_box), I64, 1u, { I64 }, 0u },
            { "int_value", NEOS_RT_FUNCTION(api::int_value), I64, 1u, { I64 }, 0u },
            { "forprep", NEOS_RT_FUNCTION(api::forprep), I64, 3u, { I64, I64, I64 }, 0u },
            { "for_integer", NEOS_RT_FUNCTION(api::for_integer), I64, 1u, { I32 }, 0u },
            { "for_float", NEOS_RT_FUNCTION(api::for_float), F64, 1u, { I32 }, 0u },
            { "tbc_mark", NEOS_RT_FUNCTION(api::tbc_mark), I32, 0u, {}, 0u },
            { "tbc_push", NEOS_RT_FUNCTION(api::tbc_push), I64, 2u, { I64, I64 }, 0u },
            { "tbc_close", NEOS_RT_FUNCTION(api::tbc_close), I64, 1u, { I32 }, 0u },
            { "tbc_close_error", NEOS_RT_FUNCTION(api::tbc_close_error), I64, 1u, { I32 }, 0u },
            { "table_new", NEOS_RT_FUNCTION(api::table_new), I64, 2u, { I32, I32 }, 0u },
            { "table_set", NEOS_RT_FUNCTION(api::table_set), I64, 3u, { I64, I64, I64 }, 0u },
            { "table_seti", NEOS_RT_FUNCTION(api::table_seti), I64, 3u, { I64, I32, I64 }, 0u },
            { "table_append_results", NEOS_RT_FUNCTION(api::table_append_results), I64, 3u, { I64, I32, I64 }, 0u },
            { "arith", NEOS_RT_FUNCTION(api::arith_), I64, 5u, { I32, I64, I64, I64, I64 }, 0u },
            { "unm", NEOS_RT_FUNCTION(api::unm), I64, 2u, { I64, I64 }, 0u },
            { "len", NEOS_RT_FUNCTION(api::len), I64, 2u, { I64, I64 }, 0u },
            { "bnot", NEOS_RT_FUNCTION(api::bnot), I64, 2u, { I64, I64 }, 0u },
            { "concat", NEOS_RT_FUNCTION(api::concat_), I64, 2u, { I64, I64 }, 0u },
            { "eq", NEOS_RT_FUNCTION(api::eq), I64, 2u, { I64, I64 }, 0u },
            { "lt", NEOS_RT_FUNCTION(api::lt), I64, 2u, { I64, I64 }, 0u },
            { "le", NEOS_RT_FUNCTION(api::le), I64, 2u, { I64, I64 }, 0u }
        };
        return sFunctions;
    }

    void program_ended()
    {
        flush_standard(1);
        flush_standard(2);
    }
}
