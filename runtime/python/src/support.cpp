/*
  support.cpp

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

  Portions of this file are derived from CPython 3.13 (Objects/genericaliasobject.c and Lib/__future__.py), Copyright
  (c) 2001-2024 Python Software Foundation; All Rights Reserved. They are used under the Python Software Foundation
  License Version 2 (see runtime/python/LICENSE.PSF). Changes: translated from C and Python to C++ for the neos Python
  runtime plugin and adapted to its object model (see runtime/python/NOTICE).
*/

// The runtime support of statements that the Python compiler compiles as others (its statements.cpp):
//
// - __with_items(mask, manager, ...): the items of a with statement of several items; each manager is a function that
//   returns the item's context manager (called once the items before it have been entered, with the values of their
//   targets); its __enter__ enters them in turn and returns the values of the items that have targets (mask: '1' for an
//   item with a target); its __exit__ exits them as nested with statements would.
// - __match(subject, pattern, value, ...): a case of a match statement whose pattern is not one of literals and values (a 
//   descriptor of it (see below); value: the values of its literals, values and classes, by index); its captures (a tuple, by
//   their slots) if the subject matches (True if it has none), else False.

#include <algorithm>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <vector>
#include "objects.hpp"

namespace neos::python_runtime
{
    namespace detail
    {
        namespace
        {
            struct with_items_object
            {
                header h;
                std::string mask;
                std::vector<object*> factories;
                std::vector<object*> managers;  ///< (the items entered)
                ~with_items_object() { for (auto f : factories) release(f); for (auto m : managers) release(m); }
            };

            neos_py_type make_with_items_type();
            neos_py_type sWithItemsType = make_with_items_type();

            with_items_object& as_with_items(object* aObject) { return *reinterpret_cast<with_items_object*>(aObject); }

            // (the exception being raised: taken (no longer raised))
            ref take_exception()
            {
                std::string const text = error_text();
                clear_error();
                ref result{ fn_caught(text.c_str()) };
                fn_handled();
                return result;
            }

            // (the items' exits, innermost first, with aException (null: none): the exception that then propagates (null: none,
            // or suppressed))
            ref exit_items(with_items_object& aItems, ref aException)
            {
                while (!aItems.managers.empty())
                {
                    ref const manager{ aItems.managers.back() };
                    aItems.managers.pop_back();
                    std::vector<object*> args;
                    if (aException)
                    {
                        args.push_back(class_of(aException.get()));
                        args.push_back((retain(aException.get()), aException.get()));
                        ref tb{ getattr(aException.get(), "__traceback__") };
                        if (!tb)
                        {
                            clear_error();
                            tb = ref{ (retain(none()), none()) };
                        }
                        args.push_back(tb.release_ownership());
                    }
                    else
                        for (int i = 0; i < 3; ++i)
                            args.push_back((retain(none()), none()));
                    ref const r{ call_method_named(manager.get(), "__exit__", std::move(args)) };
                    if (!r)
                        aException = take_exception();
                    else if (aException && truth(r.get()) == 1)
                        aException = ref{};
                }
                return aException;
            }

            object* items_enter(neos_py_context*, void*, object* aArgs, object*)
            {
                auto const self = as<tuple_object>(aArgs)->items.at(0u);
                auto& items = as_with_items(self);
                std::vector<object*> targets;
                for (std::size_t i = 0u; i < items.factories.size(); ++i)
                {
                    std::vector<object*> parameters;
                    for (auto t : targets)
                        parameters.push_back((retain(t), t));
                    ref const args{ make_tuple(std::move(parameters)) };
                    ref const manager{ call(items.factories[i], args.get(), nullptr) };
                    ref value{ manager ? call_method_named(manager.get(), "__enter__", {}) : nullptr };
                    if (!value)
                    {
                        // (the items entered are exited, then the exception propagates)
                        for (auto t : targets)
                            release(t);
                        if (auto e = exit_items(items, take_exception()))
                            fn_raise(e.get());
                        return nullptr;
                    }
                    items.managers.push_back(manager.get());
                    retain(manager.get());
                    if (i < items.mask.size() && items.mask[i] == '1')
                        targets.push_back(value.release_ownership());
                }
                return make_tuple(std::move(targets));
            }

            object* items_exit(neos_py_context*, void*, object* aArgs, object*)
            {
                auto const& args = as<tuple_object>(aArgs)->items;
                auto& items = as_with_items(args.at(0u));
                ref exception;
                if (args.size() > 2u && kind_of(args[2]) != NEOS_PY_NONE)
                    exception = ref::borrowed(args[2]);
                bool const raised = static_cast<bool>(exception);
                auto const e = exit_items(items, exception);
                if (e && (!raised || e.get() != args[2]))
                {
                    // (an exit raised an exception of its own)
                    fn_raise(e.get());
                    return nullptr;
                }
                return make_bool(raised && !e);
            }

            neos_py_type make_with_items_type()
            {
                auto type = make_type("python.with_items", "with_items", retain_object, release_object<with_items_object>);
                type.getattr = [](neos_py_context*, object* aSelf, char const* aName) -> object*
                {
                    if (std::strcmp(aName, "__enter__") == 0)
                        return make_function("__enter__", items_enter, nullptr, aSelf);
                    if (std::strcmp(aName, "__exit__") == 0)
                        return make_function("__exit__", items_exit, nullptr, aSelf);
                    return error("AttributeError", std::string{ "'with_items' object has no attribute '" } + aName + "'");
                };
                return type;
            }
        }

        // {*iterable} and {**mapping} (displays' unpackings)
        void fn_set_update(object* aSet, object* aIterable)
        {
            std::vector<object*> items;
            bool const ok = collect(aIterable, items);
            for (auto i : items)
            {
                if (ok && !failed())
                    fn_set_add(aSet, i);
                release(i);
            }
        }

        void fn_dict_merge(object* aDict, object* aMapping)
        {
            auto const value = builtin_value(aMapping);
            if (kind_of(value) != NEOS_PY_DICT)
            {
                ref const keys{ getattr(aMapping, "keys") };
                if (!keys)
                {
                    clear_error();
                    error("TypeError", std::string{ "'" } + type_name(aMapping) + "' object is not a mapping");
                    return;
                }
            }
            dict_update(aDict, aMapping);
        }

        // (a name that is not bound: NameError (aKind 0), or UnboundLocalError (1))
        void fn_unbound(char const* aName, std::int32_t aKind)
        {
            if (aKind == 1)
                error("UnboundLocalError", std::string{ "cannot access local variable '" } + aName + "' where it is not associated with a value");
            else
                error("NameError", std::string{ "name '" } + aName + "' is not defined");
        }

        // (the depth of calls of the program's functions (its module's frame is not counted: CPython's limit counts it))
        void fn_depth_check(std::int32_t aDepth)
        {
            if (aDepth >= recursion_limit())
            {
                // (raised as the function is entered: its frame is not one the exception passes through (as CPython's frame is 
                // not made))
                error("RecursionError", "maximum recursion depth exceeded");
                entering_frame();
            }
            else
                signals_check();
        }

        // (the namespace of a class's body)
        void fn_ns_set(object* aDict, char const* aName, object* aValue)
        {
            if (aValue == nullptr)
                return;
            ref const key{ make_str(aName) };
            dict_set(aDict, key.get(), aValue);
        }

        void fn_ns_cell(object* aDict, char const* aName, object* aCell)
        {
            if (aCell == nullptr)
                return;
            ref const value{ getattr(aCell, "cell_contents") };
            if (!value)
            {
                clear_error();
                return;
            }
            fn_ns_set(aDict, aName, value.get());
        }

        void fn_ns_classcell(object* aDict, object* aCell)
        {
            fn_ns_set(aDict, "__classcell__", aCell);
        }

        // --- frames and tracebacks: the program's frames are the host's (see neos_rt_host::frames); an exception's __traceback__ is 
        // the frames that it passed through, from the frame that caught it to the frame that raised it (made when it is caught: 
        // see frames_caught)

        frame_stack capture_frames()
        {
            auto const* h = host();
            if (h == nullptr || h->version < NEOS_RUNTIME_PLUGIN_VERSION || h->frames == nullptr)
                return std::make_shared<std::vector<neos_rt_frame> const>();
            thread_local std::vector<neos_rt_frame> tBuffer(32u);
            auto count = static_cast<std::size_t>(h->frames(tBuffer.data(), static_cast<unsigned>(tBuffer.size())));
            while (count == tBuffer.size())
            {
                tBuffer.resize(tBuffer.size() * 2u);
                count = static_cast<std::size_t>(h->frames(tBuffer.data(), static_cast<unsigned>(tBuffer.size())));
            }
            return std::make_shared<std::vector<neos_rt_frame> const>(tBuffer.begin(), tBuffer.begin() + static_cast<std::ptrdiff_t>(count));
        }

        namespace
        {
            // (the calling thread's innermost frame: false if it has none)
            bool innermost_frame(neos_rt_frame& aFrame)
            {
                auto const* h = host();
                return h != nullptr && h->version >= NEOS_RUNTIME_PLUGIN_VERSION && h->frames != nullptr && h->frames(&aFrame, 1u) == 1u;
            }
        }

        namespace
        {
            // (the frames that an exception was raised in (when the program raised it: see frames_reported); whether a raise 
            // statement raised it (else it was re-raised, e.g. by a bare raise or a with statement))
            struct raised_frames
            {
                std::string text;
                frame_stack frames;
                bool explicitRaise = false;
            };
            thread_local raised_frames tRaisedFrames;
            thread_local bool tExplicitRaise = false;
            thread_local bool tEnteringFrame = false;   ///< (the exception raised next is raised as the innermost frame is entered)

            bool same_frame(neos_rt_frame const& aLhs, neos_rt_frame const& aRhs)
            {
                return aLhs.id == aRhs.id && aLhs.program == aRhs.program && aLhs.function == aRhs.function;
            }

            // (the frames aCurrent (innermost first) are the outermost of aRaised: the stack that it was raised in is that of 
            // the code that catches it)
            bool within(std::vector<neos_rt_frame> const& aRaised, std::vector<neos_rt_frame> const& aCurrent)
            {
                if (aCurrent.size() > aRaised.size())
                    return false;
                auto const base = aRaised.size() - aCurrent.size();
                for (std::size_t i = 0u; i < aCurrent.size(); ++i)
                    if (!same_frame(aRaised[base + i], aCurrent[i]))
                        return false;
                return true;
            }

            // --- code objects of frames (f_code)

            struct frame_code_object
            {
                header h;
                std::string name;
                std::string filename;
                long long firstLine = 0;
            };

            neos_py_type make_frame_code_type()
            {
                auto type = make_type("code", "code", retain_object, release_object<frame_code_object>);
                type.repr = [](neos_py_context*, object* aSelf) -> object*
                {
                    auto const& c = *reinterpret_cast<frame_code_object*>(aSelf);
                    return make_str("<code object " + c.name + " at " + object_address(aSelf) + ", file \"" + c.filename + "\", line " + 
                        std::to_string(c.firstLine) + ">");
                };
                type.getattr = [](neos_py_context*, object* aSelf, char const* aName) -> object*
                {
                    auto const& c = *reinterpret_cast<frame_code_object*>(aSelf);
                    std::string_view const name{ aName };
                    if (name == "co_filename")
                        return make_str(c.filename);
                    if (name == "co_name" || name == "co_qualname")
                        return make_str(c.name);
                    if (name == "co_firstlineno")
                        return make_int(c.firstLine);
                    if (name == "co_argcount" || name == "co_posonlyargcount" || name == "co_kwonlyargcount" || name == "co_flags" || name == "co_nlocals" ||
                        name == "co_stacksize")
                        return make_int(0);
                    if (name == "co_names" || name == "co_varnames" || name == "co_consts" || name == "co_freevars" || name == "co_cellvars")
                        return make_tuple();
                    return error("AttributeError", std::string{ "'code' object has no attribute '" } + aName + "'");
                };
                return type;
            }

            neos_py_type sFrameCodeType = make_frame_code_type();

            // --- frames (sys._getframe(), a traceback's tb_frame)

            struct frame_object
            {
                header h;
                frame_stack stack;
                std::size_t index = 0u;
                object* back = nullptr;     ///< (once made)
                object* code = nullptr;     ///< (once made)
                ~frame_object() { release(back); release(code); }
            };

            frame_object& as_frame(object* aObject) { return *reinterpret_cast<frame_object*>(aObject); }

            neos_rt_frame const& frame_of(frame_object const& aFrame) { return (*aFrame.stack)[aFrame.index]; }

            long long frame_line(neos_rt_frame const& aFrame)
            {
                return aFrame.line != 0u ? aFrame.line : aFrame.firstLine;
            }

            // (the module whose source a frame's is: its name (sys.modules: the module whose file it is))
            std::string frame_module(neos_rt_frame const& aFrame)
            {
                std::string const source = aFrame.source != nullptr ? aFrame.source : "";
                if (source.empty() || source.front() == '<')
                    return "__main__";
                ref const modules{ imported_modules() };
                // (the modules' names and objects: copied (reading a module's __file__ can import modules, which sys.modules gets))
                std::vector<std::pair<ref, ref>> items;
                if (modules && kind_of(builtin_value(modules.get())) == NEOS_PY_DICT)
                    for (auto const& item : as<dict_object>(builtin_value(modules.get()))->items)
                        if (item.first != nullptr && kind_of(item.first) == NEOS_PY_STR && item.second != nullptr)
                            items.emplace_back(ref::borrowed(item.first), ref::borrowed(item.second));
                for (auto const& item : items)
                {
                    ref const file{ getattr(item.second.get(), "__file__") };
                    if (!file)
                    {
                        clear_error();
                        continue;
                    }
                    if (kind_of(file.get()) == NEOS_PY_STR)
                    {
                        std::error_code ec;
                        if (std::filesystem::path{ as<str_object>(file.get())->value }.lexically_normal() == std::filesystem::path{ source }.lexically_normal())
                            return as<str_object>(item.first.get())->value;
                    }
                }
                clear_error();
                return "__main__";
            }

            // (f_globals: exec's globals of the program the frame belongs to, else its module's namespace)
            object* frame_globals(neos_rt_frame const& aFrame)
            {
                if (auto const globals = exec_globals_of(aFrame.program))
                    return (retain(globals), globals);
                return fn_module_namespace(frame_module(aFrame).c_str(), nullptr);
            }

            builtin_entry const sFrameClear{ "clear", [](std::vector<object*> const&, object*) -> object* { return (retain(none()), none()); } };

            neos_py_type make_frame_type()
            {
                auto type = make_type("frame", "frame", retain_object, release_object<frame_object>);
                type.repr = [](neos_py_context*, object* aSelf) -> object*
                {
                    auto const& f = frame_of(as_frame(aSelf));
                    return make_str("<frame at " + object_address(aSelf) + ", file '" + std::string{ f.source != nullptr ? f.source : "" } + "', line " +
                        std::to_string(frame_line(f)) + ", code " + std::string{ f.function != nullptr ? f.function : "" } + ">");
                };
                type.getattr = [](neos_py_context*, object* aSelf, char const* aName) -> object*
                {
                    auto& self = as_frame(aSelf);
                    auto const& f = frame_of(self);
                    std::string_view const name{ aName };
                    if (name == "f_lineno")
                    {
                        // (a frame that is running: its line now)
                        auto const current = capture_frames();
                        for (auto const& c : *current)
                            if (same_frame(c, f))
                                return make_int(frame_line(c));
                        return make_int(frame_line(f));
                    }
                    if (name == "f_lasti")
                        return make_int(-1);
                    if (name == "f_back")
                    {
                        if (self.back == nullptr && self.index + 1u < self.stack->size())
                            self.back = make_frame(self.stack, self.index + 1u);
                        auto const back = self.back != nullptr ? self.back : none();
                        return (retain(back), back);
                    }
                    if (name == "f_code")
                    {
                        if (self.code == nullptr)
                            self.code = &(new frame_code_object{ { { &sFrameCodeType.base } }, f.function != nullptr ? f.function : "", 
                                f.source != nullptr ? f.source : "", static_cast<long long>(f.firstLine) })->h.base;
                        return (retain(self.code), self.code);
                    }
                    if (name == "f_globals")
                        return frame_globals(f);
                    if (name == "f_locals")
                    {
                        // (a module's: its globals; a function's locals are not objects)
                        if (f.function != nullptr && std::string_view{ f.function } == "<module>")
                            return frame_globals(f);
                        return make_dict();
                    }
                    if (name == "f_builtins")
                        return module_attribute("builtins", "__dict__");
                    if (name == "f_trace" || name == "f_generator")
                        return (retain(none()), none());
                    if (name == "f_trace_lines")
                        return make_bool(true);
                    if (name == "f_trace_opcodes")
                        return make_bool(false);
                    if (name == "clear")
                    {
                        ref const function{ make_builtin(sFrameClear) };
                        return make_bound_method(function.get(), aSelf);
                    }
                    return error("AttributeError", std::string{ "'frame' object has no attribute '" } + aName + "'");
                };
                return type;
            }

            neos_py_type sFrameType = make_frame_type();

            // --- tracebacks

            struct traceback_object
            {
                header h;
                frame_stack stack;          ///< (null: it has no frame (it has not been caught))
                std::size_t index = 0u;
                object* next = nullptr;
                object* frame = nullptr;    ///< (once made)
                ~traceback_object() { release(next); release(frame); }
            };

            traceback_object& as_traceback(object* aObject) { return *reinterpret_cast<traceback_object*>(aObject); }

            neos_py_type make_traceback_type()
            {
                auto type = make_type("traceback", "traceback", retain_object, release_object<traceback_object>);
                type.getattr = [](neos_py_context*, object* aSelf, char const* aName) -> object*
                {
                    auto& self = as_traceback(aSelf);
                    if (std::strcmp(aName, "tb_lineno") == 0)
                        return make_int(self.stack ? frame_line((*self.stack)[self.index]) : 0);
                    if (std::strcmp(aName, "tb_lasti") == 0)
                        return make_int(-1);
                    if (std::strcmp(aName, "tb_next") == 0)
                    {
                        auto const next = self.next != nullptr ? self.next : none();
                        return (retain(next), next);
                    }
                    if (std::strcmp(aName, "tb_frame") == 0)
                    {
                        if (!self.stack)
                            return (retain(none()), none());
                        if (self.frame == nullptr)
                            self.frame = make_frame(self.stack, self.index);
                        return (retain(self.frame), self.frame);
                    }
                    return error("AttributeError", std::string{ "'traceback' object has no attribute '" } + aName + "'");
                };
                return type;
            }

            neos_py_type sTracebackType = make_traceback_type();
        }

        object* make_traceback()
        {
            return &(new traceback_object{ { { &sTracebackType.base } } })->h.base;
        }

        object* make_traceback(frame_stack const& aStack, std::size_t aIndex, object* aNext)
        {
            return &(new traceback_object{ { { &sTracebackType.base } }, aStack, aIndex, aNext })->h.base;
        }

        neos_rt_frame const* traceback_frame(object* aTraceback)
        {
            if (aTraceback == nullptr || aTraceback->type != &sTracebackType.base || !as_traceback(aTraceback).stack)
                return nullptr;
            auto const& t = as_traceback(aTraceback);
            return &(*t.stack)[t.index];
        }

        object* make_frame(frame_stack const& aStack, std::size_t aIndex)
        {
            return &(new frame_object{ { { &sFrameType.base } }, aStack, aIndex })->h.base;
        }

        void explicit_raise()
        {
            tExplicitRaise = true;
        }

        void entering_frame()
        {
            tEnteringFrame = true;
        }

        // (an exception (its text) that the program raised: the frames it was raised in; the same text again from frames that those 
        // were within (e.g. the exception of a program that exec ran, raised in the program that ran it) is the same exception)
        void frames_reported(std::string const& aText)
        {
            bool const explicitRaise = tExplicitRaise;
            tExplicitRaise = false;
            bool const entering = tEnteringFrame;
            tEnteringFrame = false;
            auto current = capture_frames();
            if (entering && !current->empty())
                current = std::make_shared<std::vector<neos_rt_frame> const>(std::next(current->begin()), current->end());
            if (tRaisedFrames.frames && tRaisedFrames.text == aText && tRaisedFrames.frames->size() > current->size() && within(*tRaisedFrames.frames, *current))
                return;
            tRaisedFrames = raised_frames{ aText, std::move(current), explicitRaise };
        }

        // (a caught exception's traceback: the frames that it passed through (the frames it was raised in, from the frame that 
        // catches it), then the traceback it had (if it was raised again: the frame that raised it again is not repeated if it 
        // was re-raised))
        object* frames_caught(object* aTraceback, std::string const& aText, bool aPropagates)
        {
            raised_frames raised;
            if (aPropagates)
                raised = tRaisedFrames;
            else
                std::swap(raised, tRaisedFrames);
            // (the frame that catches it: one of the frames it was raised in (else it was not raised by the program: the frames 
            // now))
            neos_rt_frame catching{};
            if (!innermost_frame(catching))
                return nullptr;
            std::size_t catcher = 0u;
            bool found = false;
            if (raised.frames && raised.text == aText)
                for (; catcher < raised.frames->size() && !found; ++catcher)
                    found = same_frame((*raised.frames)[catcher], catching);
            if (found)
                --catcher;
            else
            {
                raised = raised_frames{ aText, capture_frames(), false };
                catcher = 0u;
            }
            auto const& stack = *raised.frames;
            if (stack.empty())
                return nullptr;
            auto const* const oldHead = traceback_frame(aTraceback);
            ref old{ oldHead != nullptr ? (retain(aTraceback), aTraceback) : nullptr };
            std::size_t first = 0u;
            if (oldHead != nullptr && !raised.explicitRaise && same_frame(stack[0], *oldHead))
                first = 1u;
            if (first > catcher)
                return nullptr;
            ref chain{ old ? old.release_ownership() : nullptr };
            for (auto i = first; i <= catcher; ++i)
                chain = ref{ make_traceback(raised.frames, i, chain.release_ownership()) };
            return chain.release_ownership();
        }

        // --- types.GenericAlias (e.g. list[int])

        namespace
        {
            struct generic_alias_object
            {
                header h;
                object* origin;
                object* args;   ///< (a tuple)
                ~generic_alias_object() { release(origin); release(args); }
            };

            neos_py_type make_generic_alias_type();
            neos_py_type sGenericAliasType = make_generic_alias_type();
            // (its special methods (e.g. GenericAlias.__repr__): its slots)
            bool const sGenericAliasObserved = (observed_types().try_emplace("types.GenericAlias", &sGenericAliasType), true);

            // (a type's repr as typing shows it: its qualified name (without "builtins."), ... (Ellipsis), or its repr)
            bool type_repr(object* aObject, std::string& aResult)
            {
                if (kind_of(aObject) == NEOS_PY_ELLIPSIS)
                {
                    aResult = "...";
                    return true;
                }
                if (aObject->type != &sGenericAliasType.base && (is_type_object(aObject) || is_class(aObject)))
                {
                    ref const qualname{ getattr(aObject, "__qualname__") };
                    ref const module{ getattr(aObject, "__module__") };
                    if (qualname && module && kind_of(qualname.get()) == NEOS_PY_STR && kind_of(module.get()) == NEOS_PY_STR)
                    {
                        auto const& m = as<str_object>(module.get())->value;
                        aResult = (m == "builtins" ? std::string{} : m + ".") + as<str_object>(qualname.get())->value;
                        return true;
                    }
                    clear_error();
                }
                return repr_of(aObject, aResult);
            }

            neos_py_type make_generic_alias_type()
            {
                auto type = make_type("types.GenericAlias", "GenericAlias", retain_object, release_object<generic_alias_object>);
                type.repr = [](neos_py_context*, object* aSelf) -> object*
                {
                    auto const& g = *reinterpret_cast<generic_alias_object*>(aSelf);
                    std::string text;
                    if (!type_repr(g.origin, text))
                        return nullptr;
                    text += "[";
                    auto const& args = as<tuple_object>(g.args)->items;
                    for (std::size_t i = 0u; i < args.size(); ++i)
                    {
                        std::string a;
                        if (kind_of(args[i]) == NEOS_PY_TUPLE && as<tuple_object>(args[i])->items.empty())
                            a = "()";
                        else if (!type_repr(args[i], a))
                            return nullptr;
                        text += (i == 0u ? "" : ", ") + a;
                    }
                    if (args.empty())
                        text += "()";
                    return make_str(text + "]");
                };
                type.getattr = [](neos_py_context*, object* aSelf, char const* aName) -> object*
                {
                    auto const& g = *reinterpret_cast<generic_alias_object*>(aSelf);
                    if (std::strcmp(aName, "__origin__") == 0)
                        return (retain(g.origin), g.origin);
                    if (std::strcmp(aName, "__args__") == 0)
                        return (retain(g.args), g.args);
                    if (std::strcmp(aName, "__parameters__") == 0)
                    {
                        // (the type variables of its arguments (theirs, if they are generic; a tuple's or a list's items'): CPython's 
                        // _Py_make_parameters)
                        std::vector<object*> parameters;
                        auto const add = [&](object* aParameter)
                        {
                            for (auto p : parameters)
                                if (p == aParameter)
                                    return;
                            parameters.push_back((retain(aParameter), aParameter));
                        };
                        std::function<void(std::vector<object*> const&)> collect = [&](std::vector<object*> const& aArgs)
                        {
                            for (auto a : aArgs)
                            {
                                if (is_type_object(a) || is_class(a))
                                    continue;
                                if (kind_of(a) == NEOS_PY_TUPLE)
                                {
                                    collect(as<tuple_object>(a)->items);
                                    continue;
                                }
                                if (kind_of(a) == NEOS_PY_LIST)
                                {
                                    collect(as<list_object>(a)->items);
                                    continue;
                                }
                                if (ref const subst{ getattr(a, "__typing_subst__") })
                                {
                                    add(a);
                                    continue;
                                }
                                clear_error();
                                if (ref const sub{ getattr(a, "__parameters__") }; sub && kind_of(sub.get()) == NEOS_PY_TUPLE)
                                    for (auto p : as<tuple_object>(sub.get())->items)
                                        add(p);
                                else
                                    clear_error();
                            }
                        };
                        collect(as<tuple_object>(g.args)->items);
                        return make_tuple(std::move(parameters));
                    }
                    if (std::strcmp(aName, "__class__") == 0)
                        return type_object_for("GenericAlias", "types.GenericAlias");
                    return getattr(g.origin, aName);
                };
                type.call = [](neos_py_context*, object* aSelf, object* aArgs, object* aKwargs) -> object*
                {
                    return call(reinterpret_cast<generic_alias_object*>(aSelf)->origin, aArgs, aKwargs);
                };
                // (alias[item]: its type variables substituted)
                type.getitem = [](neos_py_context*, object* aSelf, object* aKey) -> object*
                {
                    ref const args{ types_generic_alias_subs(aSelf, aKey) };
                    if (!args)
                        return nullptr;
                    return make_generic_alias(reinterpret_cast<generic_alias_object*>(aSelf)->origin, args.get());
                };
                type.compare = [](neos_py_context*, int aOperation, object* aLhs, object* aRhs) -> object*
                {
                    if ((aOperation != NEOS_PY_EQ && aOperation != NEOS_PY_NE) || aRhs->type != aLhs->type)
                        return not_implemented();
                    auto const& a = *reinterpret_cast<generic_alias_object*>(aLhs);
                    auto const& b = *reinterpret_cast<generic_alias_object*>(aRhs);
                    auto const origins = equal(a.origin, b.origin);
                    auto const args = origins == 1 ? equal(a.args, b.args) : origins;
                    if (args < 0)
                        return nullptr;
                    return make_bool((args == 1) == (aOperation == NEOS_PY_EQ));
                };
                type.hash = [](neos_py_context*, object* aSelf, long long* aResult) -> int
                {
                    auto const& g = *reinterpret_cast<generic_alias_object*>(aSelf);
                    long long a = 0, b = 0;
                    if (!hash_of(g.origin, a) || !hash_of(g.args, b))
                        return -1;
                    *aResult = a ^ b;
                    return 0;
                };
                return type;
            }
        }

        bool is_generic_alias(object* aObject)
        {
            return aObject != nullptr && aObject->type == &sGenericAliasType.base;
        }

        // (origin[args])
        object* make_generic_alias(object* aOrigin, object* aArgs)
        {
            object* args = kind_of(aArgs) == NEOS_PY_TUPLE ? (retain(aArgs), aArgs) : make_tuple({ (retain(aArgs), aArgs) });
            retain(aOrigin);
            return &(new generic_alias_object{ { { &sGenericAliasType.base } }, aOrigin, args })->h.base;
        }

        // --- types.MappingProxyType (a class's __dict__): a read-only view of a mapping

        namespace
        {
            struct mapping_proxy_object
            {
                header h;
                object* mapping;
                ~mapping_proxy_object() { release(mapping); }
            };

            neos_py_type make_mapping_proxy_type();
            neos_py_type sMappingProxyType = make_mapping_proxy_type();
            // (its special methods (e.g. mappingproxy.__repr__): its slots)
            bool const sMappingProxyObserved = (observed_types().try_emplace("mappingproxy", &sMappingProxyType), true);

            object* proxied(object* aSelf) { return reinterpret_cast<mapping_proxy_object*>(aSelf)->mapping; }

            neos_py_type make_mapping_proxy_type()
            {
                auto type = make_type("mappingproxy", "mappingproxy", retain_object, release_object<mapping_proxy_object>);
                type.repr = [](neos_py_context*, object* aSelf) -> object*
                {
                    std::string text;
                    if (!repr_of(proxied(aSelf), text))
                        return nullptr;
                    return make_str("mappingproxy(" + text + ")");
                };
                type.str = [](neos_py_context*, object* aSelf) -> object*
                {
                    std::string text;
                    return str_of(proxied(aSelf), text) ? make_str(text) : nullptr;
                };
                type.getitem = [](neos_py_context*, object* aSelf, object* aKey) -> object* { return getitem(proxied(aSelf), aKey); };
                type.setitem = [](neos_py_context*, object*, object*, object*) -> int
                {
                    error("TypeError", "'mappingproxy' object does not support item assignment");
                    return -1;
                };
                type.length = [](neos_py_context*, object* aSelf) -> long long { return length(proxied(aSelf)); };
                type.iter = [](neos_py_context*, object* aSelf) -> object* { return iter(proxied(aSelf)); };
                type.contains = [](neos_py_context*, object* aSelf, object* aItem) -> int { return contains(proxied(aSelf), aItem); };
                type.compare = [](neos_py_context*, int aOperation, object* aLhs, object* aRhs) -> object*
                {
                    return compare(aOperation, aLhs->type == &sMappingProxyType.base ? proxied(aLhs) : aLhs, aRhs->type == &sMappingProxyType.base ? proxied(aRhs) : aRhs);
                };
                type.binary = [](neos_py_context*, int aOperation, object* aLhs, object* aRhs, int) -> object*
                {
                    if (aOperation != NEOS_PY_OR)
                        return not_implemented();
                    // (| : a dict)
                    ref const lhs{ aLhs->type == &sMappingProxyType.base ? call_method_named(proxied(aLhs), "copy", {}) : new_reference(aLhs) };
                    if (!lhs)
                        return nullptr;
                    return binary(NEOS_PY_OR, lhs.get(), aRhs->type == &sMappingProxyType.base ? proxied(aRhs) : aRhs, false);
                };
                type.getattr = [](neos_py_context*, object* aSelf, char const* aName) -> object*
                {
                    static std::set<std::string, std::less<>> const sMethods = { "keys", "values", "items", "get", "copy", "__getitem__", "__contains__",
                        "__len__", "__iter__", "__reversed__" };
                    if (sMethods.contains(aName))
                        return getattr(proxied(aSelf), aName);
                    return error("AttributeError", std::string{ "'mappingproxy' object has no attribute '" } + aName + "'");
                };
                return type;
            }
        }

        object* make_mapping_proxy(object* aMapping)
        {
            return &(new mapping_proxy_object{ { { &sMappingProxyType.base } }, new_reference(aMapping) })->h.base;
        }

        object* proxied_mapping(object* aObject)
        {
            return aObject != nullptr && aObject->type == &sMappingProxyType.base ? proxied(aObject) : aObject;
        }

        // --- types.UnionType (X | Y)

        namespace
        {
            struct union_object
            {
                header h;
                object* args;   ///< (a tuple: its distinct types (None: NoneType))
                ~union_object() { release(args); }
            };

            neos_py_type make_union_type();
            neos_py_type sUnionType = make_union_type();

            neos_py_type make_union_type()
            {
                auto type = make_type("types.UnionType", "UnionType", retain_object, release_object<union_object>);
                type.repr = [](neos_py_context*, object* aSelf) -> object*
                {
                    std::string text;
                    ref const noneType{ type_object_for("NoneType", "NoneType") };
                    for (auto a : as<tuple_object>(reinterpret_cast<union_object*>(aSelf)->args)->items)
                    {
                        std::string t;
                        if (a == noneType.get())
                            t = "None";
                        else if (!type_repr(a, t))
                            return nullptr;
                        text += (text.empty() ? "" : " | ") + t;
                    }
                    return make_str(text);
                };
                type.getattr = [](neos_py_context*, object* aSelf, char const* aName) -> object*
                {
                    auto const args = reinterpret_cast<union_object*>(aSelf)->args;
                    if (std::strcmp(aName, "__args__") == 0)
                        return (retain(args), args);
                    if (std::strcmp(aName, "__parameters__") == 0)
                    {
                        std::vector<object*> parameters;
                        for (auto a : as<tuple_object>(args)->items)
                        {
                            ref const p{ getattr(a, "__parameters__") };
                            if (p && kind_of(p.get()) == NEOS_PY_TUPLE)
                            {
                                for (auto x : as<tuple_object>(p.get())->items)
                                    if (std::find(parameters.begin(), parameters.end(), x) == parameters.end())
                                        parameters.push_back((retain(x), x));
                            }
                            else if (ref const tv{ getattr(a, "__typing_is_typevar__") })
                            {
                                if (std::find(parameters.begin(), parameters.end(), a) == parameters.end())
                                    parameters.push_back((retain(a), a));
                            }
                            clear_error();
                        }
                        return make_tuple(std::move(parameters));
                    }
                    if (std::strcmp(aName, "__class__") == 0)
                        return type_object_for("UnionType", "types.UnionType");
                    if (std::strcmp(aName, "__module__") == 0)
                        return make_str("types");
                    return error("AttributeError", std::string{ "'types.UnionType' object has no attribute '" } + aName + "'");
                };
                type.binary = [](neos_py_context*, int aOperation, object* aLhs, object* aRhs, int) -> object*
                {
                    if (aOperation != NEOS_PY_OR || !is_union_operand(aLhs) || !is_union_operand(aRhs))
                        return not_implemented();
                    return make_union(aLhs, aRhs);
                };
                type.compare = [](neos_py_context*, int aOperation, object* aLhs, object* aRhs) -> object*
                {
                    if ((aOperation != NEOS_PY_EQ && aOperation != NEOS_PY_NE) || aRhs->type != aLhs->type)
                        return not_implemented();
                    // (equal: the same set of types)
                    auto const& a = as<tuple_object>(reinterpret_cast<union_object*>(aLhs)->args)->items;
                    auto const& b = as<tuple_object>(reinterpret_cast<union_object*>(aRhs)->args)->items;
                    bool same = a.size() == b.size();
                    for (auto x : a)
                    {
                        if (!same)
                            break;
                        bool found = false;
                        for (auto y : b)
                        {
                            auto const e = equal(x, y);
                            if (e < 0)
                                return nullptr;
                            found = found || e == 1;
                        }
                        same = found;
                    }
                    return make_bool(same == (aOperation == NEOS_PY_EQ));
                };
                type.hash = [](neos_py_context*, object* aSelf, long long* aResult) -> int
                {
                    long long result = 0x345678;
                    for (auto a : as<tuple_object>(reinterpret_cast<union_object*>(aSelf)->args)->items)
                    {
                        long long h = 0;
                        if (!hash_of(a, h))
                            return -1;
                        result ^= h;   // (order independent)
                    }
                    *aResult = result == -1 ? -2 : result;
                    return 0;
                };
                type.getitem = [](neos_py_context*, object* aSelf, object* aKey) -> object*
                {
                    // (a union of type variables (e.g. (T | None)[int]): its types with the variables substituted)
                    ref const parameters{ getattr(aSelf, "__parameters__") };
                    if (!parameters)
                        return nullptr;
                    auto const& p = as<tuple_object>(parameters.get())->items;
                    if (p.empty())
                        return error("TypeError", "There are no type variables left in " + [&]() { std::string t; repr_of(aSelf, t); return t; }());
                    std::vector<object*> values = kind_of(aKey) == NEOS_PY_TUPLE ? as<tuple_object>(aKey)->items : std::vector<object*>{ aKey };
                    if (values.size() != p.size())
                        return error("TypeError", std::string{ "Too " } + (values.size() > p.size() ? "many" : "few") + " arguments for " +
                            [&]() { std::string t; repr_of(aSelf, t); return t; }() + "; actual " + std::to_string(values.size()) + ", expected " + std::to_string(p.size()));
                    ref result;
                    for (auto a : as<tuple_object>(reinterpret_cast<union_object*>(aSelf)->args)->items)
                    {
                        ref item{ (retain(a), a) };
                        for (std::size_t i = 0u; i < p.size(); ++i)
                            if (p[i] == a)
                                item = ref{ (retain(values[i]), values[i]) };
                        if (!result)
                            result = item;
                        else
                        {
                            result = ref{ binary(NEOS_PY_OR, result.get(), item.get(), false) };
                            if (!result)
                                return nullptr;
                        }
                    }
                    return result.release_ownership();
                };
                return type;
            }
        }

        bool is_union(object* aObject)
        {
            return aObject != nullptr && aObject->type == &sUnionType.base;
        }

        object* union_args(object* aUnion)
        {
            return reinterpret_cast<union_object*>(aUnion)->args;
        }

        // (an operand of |: a type, a class, None, a generic alias, a union or a type variable)
        bool is_union_operand(object* aObject)
        {
            if (kind_of(aObject) == NEOS_PY_NONE || is_type_object(aObject) || is_class(aObject) || is_generic_alias(aObject) || is_union(aObject) || is_type_alias_type(aObject))
                return true;
            return false;
        }

        // (aLhs | aRhs: a union (or the type, if they are the same))
        object* make_union(object* aLhs, object* aRhs)
        {
            std::vector<object*> types;
            auto const add = [&](object* aType) -> bool
            {
                ref const t{ kind_of(aType) == NEOS_PY_NONE ? type_object_for("NoneType", "NoneType") : (retain(aType), aType) };
                for (auto existing : types)
                {
                    auto const e = equal(existing, t.get());
                    if (e < 0)
                        return false;
                    if (e == 1)
                        return true;
                }
                types.push_back((retain(t.get()), t.get()));
                return true;
            };
            for (auto operand : { aLhs, aRhs })
            {
                if (is_union(operand))
                {
                    for (auto a : as<tuple_object>(union_args(operand))->items)
                        if (!add(a))
                            return (std::for_each(types.begin(), types.end(), release), nullptr);
                }
                else if (!add(operand))
                    return (std::for_each(types.begin(), types.end(), release), nullptr);
            }
            if (types.size() == 1u)
                return types[0];
            return &(new union_object{ { { &sUnionType.base } }, make_tuple(std::move(types)) })->h.base;
        }

        // --- match statements' patterns
        //
        // A pattern's descriptor (as the compiler's statements.cpp makes it; N: a number; P: a pattern):
        //   _              the wildcard
        //   cN;            a capture (its slot)
        //   vN;            a literal or a value (==)
        //   iN;            None, True or False (is)
        //   |N;P...        alternatives
        //   aN;P           P as a capture
        //   sN;P...        a sequence of N items (an item may be a star: *N; (a capture) or *_)
        //   mN,R;(K;P)...  a mapping of N keys (K: the key's value) (R: the slot of **rest, -1 if none)
        //   kC,N,M;P...(name=P)...  a class (C: its value) of N positional and M keyword patterns

        namespace
        {
            struct pattern_node
            {
                char kind = '_';
                long long index = -1;
                long long extra = -1;
                std::vector<pattern_node> children;
                std::vector<long long> keys;
                std::vector<std::string> names;
                std::size_t positional = 0u;
            };

            struct pattern_parser
            {
                std::string const& text;
                std::size_t position = 0u;
                long long slots = 0;

                long long number()
                {
                    bool const negative = position < text.size() && text[position] == '-';
                    if (negative)
                        ++position;
                    long long result = 0;
                    while (position < text.size() && text[position] >= '0' && text[position] <= '9')
                        result = result * 10 + (text[position++] - '0');
                    if (position < text.size() && (text[position] == ';' || text[position] == ','))
                        ++position;
                    return negative ? -result : result;
                }
                long long slot()
                {
                    auto const result = number();
                    slots = std::max(slots, result + 1);
                    return result;
                }
                pattern_node parse()
                {
                    pattern_node node;
                    if (position >= text.size())
                        return node;
                    node.kind = text[position++];
                    switch (node.kind)
                    {
                    case '_':
                        break;
                    case 'c':
                        node.index = slot();
                        break;
                    case 'v':
                    case 'i':
                        node.index = number();
                        break;
                    case '|':
                        for (auto n = number(); n > 0; --n)
                            node.children.push_back(parse());
                        break;
                    case 'a':
                        node.index = slot();
                        node.children.push_back(parse());
                        break;
                    case 's':
                        for (auto n = number(); n > 0; --n)
                        {
                            if (position < text.size() && text[position] == '*')
                            {
                                pattern_node star;
                                star.kind = '*';
                                ++position;
                                if (position < text.size() && text[position] == '_')
                                    ++position;
                                else
                                    star.index = slot();
                                node.children.push_back(std::move(star));
                            }
                            else
                                node.children.push_back(parse());
                        }
                        break;
                    case 'm':
                    {
                        auto const n = number();
                        node.extra = number();
                        if (node.extra >= 0)
                            slots = std::max(slots, node.extra + 1);
                        for (auto i = 0; i < n; ++i)
                        {
                            node.keys.push_back(number());
                            node.children.push_back(parse());
                        }
                        break;
                    }
                    case 'k':
                    {
                        node.index = number();
                        auto const positional = number();
                        auto const keywords = number();
                        node.positional = static_cast<std::size_t>(positional);
                        for (auto i = 0; i < positional; ++i)
                            node.children.push_back(parse());
                        for (auto i = 0; i < keywords; ++i)
                        {
                            auto const equals = text.find('=', position);
                            node.names.push_back(text.substr(position, equals - position));
                            position = equals + 1u;
                            node.children.push_back(parse());
                        }
                        break;
                    }
                    default:
                        break;
                    }
                    return node;
                }
            };

            struct pattern_entry
            {
                pattern_node root;
                long long slots = 0;
            };

            pattern_entry const& pattern_of(std::string const& aDescriptor)
            {
                static std::mutex sMutex;
                static std::map<std::string, pattern_entry, std::less<>> sPatterns;
                std::scoped_lock lock{ sMutex };
                auto existing = sPatterns.find(aDescriptor);
                if (existing != sPatterns.end())
                    return existing->second;
                pattern_parser parser{ aDescriptor };
                pattern_entry entry;
                entry.root = parser.parse();
                entry.slots = parser.slots;
                return sPatterns.emplace(aDescriptor, std::move(entry)).first->second;
            }

            struct match_context
            {
                std::vector<object*> const& values;
                std::vector<object*> captures;  ///< (new references (or null))
                ~match_context() { for (auto c : captures) release(c); }
                void capture(long long aSlot, object* aValue)
                {
                    if (aSlot < 0 || static_cast<std::size_t>(aSlot) >= captures.size())
                        return;
                    retain(aValue);
                    release(captures[aSlot]);
                    captures[aSlot] = aValue;
                }
            };

            // (a sequence (as a match statement's sequence patterns see one): not str, bytes or bytearray)
            bool match_sequence(object* aSubject, std::vector<object*>& aItems)
            {
                auto const value = builtin_value(aSubject);
                auto const kind = kind_of(value);
                if (kind == NEOS_PY_LIST || kind == NEOS_PY_TUPLE || kind == NEOS_PY_RANGE)
                    return collect(value, aItems);
                std::string_view const name = type_name(aSubject);
                if (name == "deque" || name == "array" || name == "memoryview")
                    return collect(aSubject, aItems);
                return false;
            }

            int match_node(pattern_node const& aNode, object* aSubject, match_context& aContext);

            int match_class(pattern_node const& aNode, object* aSubject, match_context& aContext)
            {
                auto const cls = aContext.values.at(static_cast<std::size_t>(aNode.index));
                if (!is_class(cls) && !is_type_object(cls))
                {
                    error("TypeError", "called match pattern must be a class");
                    return -1;
                }
                {
                    ref const is{ b_isinstance({ aSubject, cls }, nullptr) };
                    if (!is)
                        return -1;
                    if (truth(is.get()) != 1)
                        return 0;
                }
                auto const className = [&]()
                {
                    std::string result;
                    if (ref const n{ getattr(cls, "__name__") }; n && kind_of(n.get()) == NEOS_PY_STR)
                        result = as<str_object>(n.get())->value;
                    else
                        clear_error();
                    return result;
                };
                std::vector<std::string> attributes;
                if (aNode.positional > 0u)
                {
                    bool self = false;
                    if (is_type_object(cls))
                    {
                        static std::set<std::string_view> const sSelf = { "bool", "bytearray", "bytes", "dict", "float", "frozenset", "int", "list",
                            "set", "str", "tuple" };
                        self = sSelf.contains(className());
                    }
                    if (self)
                    {
                        if (aNode.positional > 1u)
                        {
                            error("TypeError", className() + "() accepts 1 positional sub-pattern (" + std::to_string(aNode.positional) + " given)");
                            return -1;
                        }
                        auto const r = match_node(aNode.children[0], aSubject, aContext);
                        if (r != 1)
                            return r;
                    }
                    else
                    {
                        ref const args{ getattr(cls, "__match_args__") };
                        if (!args)
                            clear_error();
                        std::vector<object*> names;
                        if (args)
                        {
                            if (kind_of(args.get()) != NEOS_PY_TUPLE)
                            {
                                error("TypeError", className() + ".__match_args__ must be a tuple (got " + type_name(args.get()) + ")");
                                return -1;
                            }
                            names = as<tuple_object>(args.get())->items;
                        }
                        if (aNode.positional > names.size())
                        {
                            error("TypeError", className() + "() accepts " + std::to_string(names.size()) + " positional sub-pattern" + 
                                (names.size() == 1u ? "" : "s") + " (" + std::to_string(aNode.positional) + " given)");
                            return -1;
                        }
                        for (std::size_t i = 0u; i < aNode.positional; ++i)
                        {
                            if (kind_of(names[i]) != NEOS_PY_STR)
                            {
                                error("TypeError", std::string{ "__match_args__ elements must be strings (got " } + type_name(names[i]) + ")");
                                return -1;
                            }
                            attributes.push_back(as<str_object>(names[i])->value);
                        }
                    }
                }
                for (auto const& n : aNode.names)
                {
                    if (std::find(attributes.begin(), attributes.end(), n) != attributes.end())
                    {
                        error("TypeError", className() + "() got multiple sub-patterns for attribute '" + n + "'");
                        return -1;
                    }
                    attributes.push_back(n);
                }
                // (the subject itself matched its positional pattern: its keyword patterns follow it)
                auto const skipped = aNode.positional > 0u && attributes.size() == aNode.names.size() ? aNode.positional : 0u;
                for (std::size_t i = 0u; i < attributes.size(); ++i)
                {
                    ref const value{ getattr(aSubject, attributes[i].c_str()) };
                    if (!value)
                    {
                        std::string const text = error_text();
                        if (text.rfind("AttributeError", 0u) != 0u)
                            return -1;
                        clear_error();
                        return 0;
                    }
                    auto const r = match_node(aNode.children[i + skipped], value.get(), aContext);
                    if (r != 1)
                        return r;
                }
                return 1;
            }

            int match_node(pattern_node const& aNode, object* aSubject, match_context& aContext)
            {
                switch (aNode.kind)
                {
                case '_':
                    return 1;
                case 'c':
                    aContext.capture(aNode.index, aSubject);
                    return 1;
                case 'v':
                    return equal(aSubject, aContext.values.at(static_cast<std::size_t>(aNode.index)));
                case 'i':
                    return aSubject == aContext.values.at(static_cast<std::size_t>(aNode.index)) ? 1 : 0;
                case '|':
                    for (auto const& c : aNode.children)
                        if (auto const r = match_node(c, aSubject, aContext); r != 0)
                            return r;
                    return 0;
                case 'a':
                {
                    auto const r = match_node(aNode.children[0], aSubject, aContext);
                    if (r == 1)
                        aContext.capture(aNode.index, aSubject);
                    return r;
                }
                case 's':
                {
                    std::vector<object*> items;
                    if (!match_sequence(aSubject, items))
                    {
                        for (auto i : items)
                            release(i);
                        return failed() ? -1 : 0;
                    }
                    struct holder { std::vector<object*>& items; ~holder() { for (auto i : items) release(i); } } hold{ items };
                    std::optional<std::size_t> star;
                    for (std::size_t i = 0u; i < aNode.children.size(); ++i)
                        if (aNode.children[i].kind == '*')
                            star = i;
                    auto const fixed = aNode.children.size() - (star ? 1u : 0u);
                    if (star ? items.size() < fixed : items.size() != fixed)
                        return 0;
                    auto const before = star ? *star : fixed;
                    for (std::size_t i = 0u; i < before; ++i)
                        if (auto const r = match_node(aNode.children[i], items[i], aContext); r != 1)
                            return r;
                    if (star)
                    {
                        auto const after = fixed - before;
                        auto const middleEnd = items.size() - after;
                        if (aNode.children[*star].index >= 0)
                        {
                            std::vector<object*> middle;
                            for (auto i = before; i < middleEnd; ++i)
                                middle.push_back((retain(items[i]), items[i]));
                            ref const list{ make_list(std::move(middle)) };
                            aContext.capture(aNode.children[*star].index, list.get());
                        }
                        for (std::size_t i = 0u; i < after; ++i)
                            if (auto const r = match_node(aNode.children[*star + 1u + i], items[middleEnd + i], aContext); r != 1)
                                return r;
                    }
                    return 1;
                }
                case 'm':
                {
                    auto const value = builtin_value(aSubject);
                    bool const dict = kind_of(value) == NEOS_PY_DICT;
                    std::string_view const name = type_name(aSubject);
                    if (!dict && name != "mappingproxy" && name != "ChainMap")
                        return 0;
                    for (std::size_t i = 0u; i < aNode.keys.size(); ++i)
                    {
                        auto const key = aContext.values.at(static_cast<std::size_t>(aNode.keys[i]));
                        ref item;
                        if (dict && aSubject == value)
                        {
                            auto const position = dict_find(value, key);
                            if (position < 0)
                                return failed() ? -1 : 0;
                            item = ref::borrowed(as<dict_object>(value)->items[static_cast<std::size_t>(position)].second);
                        }
                        else
                        {
                            static object* const sMissing = make_tuple();
                            item = ref{ call_method_named(aSubject, "get", { (retain(key), key), (retain(sMissing), sMissing) }) };
                            if (!item)
                                return -1;
                            if (item.get() == sMissing)
                                return 0;
                        }
                        if (auto const r = match_node(aNode.children[i], item.get(), aContext); r != 1)
                            return r;
                    }
                    if (aNode.extra >= 0)
                    {
                        ref const rest{ make_dict() };
                        if (!dict_update(rest.get(), aSubject))
                            return -1;
                        for (auto k : aNode.keys)
                        {
                            auto const key = aContext.values.at(static_cast<std::size_t>(k));
                            auto const position = dict_find(rest.get(), key);
                            if (position >= 0)
                                dict_erase(*as<dict_object>(rest.get()), static_cast<std::size_t>(position));
                        }
                        aContext.capture(aNode.extra, rest.get());
                    }
                    return 1;
                }
                case 'k':
                    return match_class(aNode, aSubject, aContext);
                default:
                    error("SystemError", "invalid match pattern");
                    return -1;
                }
            }
        }

        object* b_match(std::vector<object*> const& aArgs, object*)
        {
            if (aArgs.size() < 2u || kind_of(aArgs[1]) != NEOS_PY_STR)
                return error("TypeError", "__match() expects a subject and a pattern");
            auto const& pattern = pattern_of(as<str_object>(aArgs[1])->value);
            std::vector<object*> const values(aArgs.begin() + 2, aArgs.end());
            match_context context{ values, std::vector<object*>(static_cast<std::size_t>(pattern.slots), nullptr) };
            auto const r = match_node(pattern.root, aArgs[0], context);
            if (r < 0)
                return nullptr;
            if (r == 0)
                return make_bool(false);
            if (context.captures.empty())
                return make_bool(true);
            std::vector<object*> captures;
            for (auto& c : context.captures)
            {
                captures.push_back(c != nullptr ? c : (retain(none()), none()));
                c = nullptr;
            }
            return make_tuple(std::move(captures));
        }

        // --- iter(callable, sentinel)

        namespace
        {
            struct callable_iterator_object
            {
                header h;
                object* callable;   ///< (null: exhausted)
                object* sentinel;
                ~callable_iterator_object() { release(callable); release(sentinel); }
            };

            neos_py_type make_callable_iterator_type()
            {
                auto type = make_type("callable_iterator", "callable_iterator", retain_object, release_object<callable_iterator_object>);
                type.iter = [](neos_py_context*, object* aSelf) -> object* { retain(aSelf); return aSelf; };
                type.next = [](neos_py_context*, object* aSelf) -> object*
                {
                    auto& it = *reinterpret_cast<callable_iterator_object*>(aSelf);
                    if (it.callable == nullptr)
                        return nullptr;
                    ref const args{ make_tuple() };
                    ref value{ call(it.callable, args.get(), nullptr) };
                    if (!value)
                    {
                        if (std::string_view{ error_text() }.starts_with("StopIteration"))
                        {
                            clear_error();
                            release(it.callable);
                            it.callable = nullptr;
                        }
                        return nullptr;
                    }
                    auto const same = equal(value.get(), it.sentinel);
                    if (same < 0)
                        return nullptr;
                    if (same == 1)
                    {
                        release(it.callable);
                        it.callable = nullptr;
                        return nullptr;
                    }
                    return value.release_ownership();
                };
                return type;
            }

            neos_py_type sCallableIteratorType = make_callable_iterator_type();
        }

        object* make_callable_iterator(object* aCallable, object* aSentinel)
        {
            ref const callable{ fn_builtin("callable") };
            ref const args{ make_tuple({ (retain(aCallable), aCallable) }) };
            ref const is{ call(callable.get(), args.get(), nullptr) };
            if (!is)
                return nullptr;
            if (truth(is.get()) != 1)
                return error("TypeError", "iter(v, w): v must be callable");
            retain(aCallable);
            retain(aSentinel);
            return &(new callable_iterator_object{ { { &sCallableIteratorType.base } }, aCallable, aSentinel })->h.base;
        }

        // __annotate(function, annotations, sources): a function's __annotations__ (annotations: a function that makes them (when 
        // they are first read); sources: name, source, ... (its annotations' sources: its __annotations__ if it raises))
        object* b_annotate(std::vector<object*> const& aArgs, object*)
        {
            if (aArgs.size() != 3u)
                return error("SystemError", "__annotate(function, annotations, sources)");
            if (!set_function_annotations(aArgs[0], aArgs[1], aArgs[2]))
                return nullptr;
            retain(aArgs[0]);
            return aArgs[0];
        }

        // --- __future__ (CPython 3.13's Lib/__future__.py)

        namespace
        {
            struct feature_object
            {
                header h;
                object* optional;
                object* mandatory;
                long long flag;
                ~feature_object() { release(optional); release(mandatory); }
            };

            neos_py_type make_feature_type()
            {
                auto type = make_type("__future__._Feature", "_Feature", retain_object, release_object<feature_object>);
                type.repr = [](neos_py_context*, object* aSelf) -> object*
                {
                    auto const& f = *reinterpret_cast<feature_object*>(aSelf);
                    std::string a, b;
                    if (!repr_of(f.optional, a) || !repr_of(f.mandatory, b))
                        return nullptr;
                    return make_str("_Feature(" + a + ", " + b + ", " + std::to_string(f.flag) + ")");
                };
                type.getattr = [](neos_py_context*, object* aSelf, char const* aName) -> object*
                {
                    auto const& f = *reinterpret_cast<feature_object*>(aSelf);
                    std::string_view const name{ aName };
                    if (name == "optional")
                        return (retain(f.optional), f.optional);
                    if (name == "mandatory")
                        return (retain(f.mandatory), f.mandatory);
                    if (name == "compiler_flag")
                        return make_int(f.flag);
                    if (name == "getOptionalRelease" || name == "getMandatoryRelease")
                        return make_function(aName, [](neos_py_context*, void* aData, object* aArgs, object*) -> object*
                            {
                                auto const& f = *reinterpret_cast<feature_object*>(as<tuple_object>(aArgs)->items.at(0u));
                                auto const value = aData != nullptr ? f.mandatory : f.optional;
                                retain(value);
                                return value;
                            }, name == "getMandatoryRelease" ? aSelf : nullptr, aSelf);
                    return error("AttributeError", std::string{ "'_Feature' object has no attribute '" } + aName + "'");
                };
                return type;
            }

            neos_py_type sFeatureType = make_feature_type();

            object* release_tuple(long long aMajor, long long aMinor, long long aMicro, char const* aLevel, long long aSerial)
            {
                return make_tuple({ make_int(aMajor), make_int(aMinor), make_int(aMicro), make_str(aLevel), make_int(aSerial) });
            }

            bool future_module(std::vector<std::pair<std::string, object*>>& aAttributes)
            {
                struct entry { char const* name; long long o[3]; char const* ol; long long os; long long m[3]; char const* ml; long long ms; long long flag; bool noMandatory; };
                static entry const sFeatures[] = {
                    { "nested_scopes", { 2, 1, 0 }, "beta", 1, { 2, 2, 0 }, "alpha", 0, 0x0010, false },
                    { "generators", { 2, 2, 0 }, "alpha", 1, { 2, 3, 0 }, "final", 0, 0, false },
                    { "division", { 2, 2, 0 }, "alpha", 2, { 3, 0, 0 }, "alpha", 0, 0x20000, false },
                    { "absolute_import", { 2, 5, 0 }, "alpha", 1, { 3, 0, 0 }, "alpha", 0, 0x40000, false },
                    { "with_statement", { 2, 5, 0 }, "alpha", 1, { 2, 6, 0 }, "alpha", 0, 0x80000, false },
                    { "print_function", { 2, 6, 0 }, "alpha", 2, { 3, 0, 0 }, "alpha", 0, 0x100000, false },
                    { "unicode_literals", { 2, 6, 0 }, "alpha", 2, { 3, 0, 0 }, "alpha", 0, 0x200000, false },
                    { "barry_as_FLUFL", { 3, 1, 0 }, "alpha", 2, { 4, 0, 0 }, "alpha", 0, 0x400000, false },
                    { "generator_stop", { 3, 5, 0 }, "beta", 1, { 3, 7, 0 }, "alpha", 0, 0x800000, false },
                    { "annotations", { 3, 7, 0 }, "beta", 1, { 0, 0, 0 }, "", 0, 0x1000000, true } };
                std::vector<object*> names;
                for (auto const& e : sFeatures)
                {
                    auto const mandatory = e.noMandatory ? (retain(none()), none()) : release_tuple(e.m[0], e.m[1], e.m[2], e.ml, e.ms);
                    auto const feature = new feature_object{ { { &sFeatureType.base } }, release_tuple(e.o[0], e.o[1], e.o[2], e.ol, e.os), mandatory, e.flag };
                    aAttributes.emplace_back(e.name, &feature->h.base);
                    names.push_back(make_str(e.name));
                }
                aAttributes.emplace_back("all_feature_names", make_list(std::move(names)));
                for (auto const& c : { std::pair{ "CO_NESTED", 0x0010 }, std::pair{ "CO_GENERATOR_ALLOWED", 0 }, std::pair{ "CO_FUTURE_DIVISION", 0x20000 },
                    std::pair{ "CO_FUTURE_ABSOLUTE_IMPORT", 0x40000 }, std::pair{ "CO_FUTURE_WITH_STATEMENT", 0x80000 }, std::pair{ "CO_FUTURE_PRINT_FUNCTION", 0x100000 },
                    std::pair{ "CO_FUTURE_UNICODE_LITERALS", 0x200000 }, std::pair{ "CO_FUTURE_BARRY_AS_BDFL", 0x400000 }, std::pair{ "CO_FUTURE_GENERATOR_STOP", 0x800000 },
                    std::pair{ "CO_FUTURE_ANNOTATIONS", 0x1000000 } })
                    aAttributes.emplace_back(c.first, make_int(c.second));
                return true;
            }

            inline bool const sFuture = register_module("__future__", future_module);
        }

        // --- object()

        namespace
        {
            struct plain_object
            {
                header h;
            };

            neos_py_type make_plain_object_type()
            {
                auto type = make_type("object", "object", retain_object, release_object<plain_object>);
                type.repr = [](neos_py_context*, object* aSelf) -> object*
                {
                    return make_str("<object object at " + object_address(aSelf) + ">");
                };
                // (its attributes: object's methods (getattr))
                type.setattr = [](neos_py_context*, object*, char const* aName, object*) -> int
                {
                    error("AttributeError", std::string{ "'object' object has no attribute '" } + aName + "' and no __dict__ for setting new attributes");
                    return -1;
                };
                return type;
            }

            neos_py_type sPlainObjectType = make_plain_object_type();
        }

        object* make_plain_object()
        {
            return &(new plain_object{ { { &sPlainObjectType.base } } })->h.base;
        }

        object* b_with_items(std::vector<object*> const& aArgs, object*)
        {
            if (aArgs.empty() || kind_of(aArgs[0]) != NEOS_PY_STR)
                return error("TypeError", "__with_items() expects a mask");
            auto* const items = new with_items_object{ { { &sWithItemsType.base } }, as<str_object>(aArgs[0])->value, {}, {} };
            for (std::size_t i = 1u; i < aArgs.size(); ++i)
                items->factories.push_back((retain(aArgs[i]), aArgs[i]));
            return &items->h.base;
        }
    }
}
