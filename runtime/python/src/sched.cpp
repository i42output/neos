/*
  sched.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/sched.py), Copyright (c) 2001-2024 Python Software Foundation;
  All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from Python to C++ for the neos Python runtime plugin and adapted to
  its object model (see runtime/python/NOTICE).
*/

// The module sched (CPython 3.13's Lib/sched.py, translated from Python by py2neos, a Python-to-C++ translator): its classes are made (by type)
// of closures (pyclass.hpp), so a program's subclasses override their methods.

#include <map>
#include <string>
#include <vector>
#include "python.hpp"
#include "objects.hpp"
#include "values.hpp"
#include "pyclass.hpp"
#include "sched_docs.hpp"

namespace neos::python_runtime::detail::sched_module
{
    namespace
    {
        using pyclass::S;
        using pyclass::I;
        using pyclass::F;
        using pyclass::B;
        using pyclass::Mod;
        using pyclass::none;
        using pyclass::boolean;
        using pyclass::M;
        using pyclass::has_attr;
        using pyclass::get_attr;
        using pyclass::callable;
        using pyclass::module_writer;
        using pyclass::namespace_type;
        using pyclass::try_except;
        using pyclass::try_finally;
        using pyclass::with_;
        using pyclass::unpack2;
        using pyclass::format;
        using pyclass::handler_scope;
        using pyclass::matches;
        using pyclass::iterate;
        using pyclass::unpack;
        using pyclass::call_ex;
        using pyclass::dict_of;
        using pyclass::set_of;
        using pyclass::inplace;
        using pyclass::del_attr;
        using pyclass::del_item;
        using pyclass::formatted;
        using pyclass::yield_from;
        using pyclass::truth_of;
        using pyclass::module_of;
        // (the module's objects (made once, and never destroyed: they outlive the program's objects))
        struct state
        {
            std::vector<std::pair<std::string, py>> attributes;     ///< (the module's attributes, in order)
            std::map<std::string, py, std::less<>> objects;         ///< (its objects, by qualified name (e.g. "sched.name"))
            bool made = false;
        };
        state& the_state()
        {
            static state* sState = new state{};
            return *sState;
        }

        // (its entry: the object, as it is defined (or assigned) now)
        py const& object_entry(char const* aQualifiedName)
        {
            auto& s = the_state();
            auto const o = s.objects.find(aQualifiedName);
            if (o == s.objects.end())
                py_raise("SystemError", std::string{ "sched: no object " } + aQualifiedName);
            return o->second;
        }
        // (an object of the module: its entry, found once (G<"sched.name">()))
        template <pyclass::fixed_string Name>
        py const& G() { static py const* const sEntry = &object_entry(Name.value); return *sEntry; }
        // (an object made: one of the module's attributes)
        void define(char const* aModule, char const* aName, py const& aValue)
        {
            auto& s = the_state();
            s.objects[std::string{ aModule } + "." + aName] = aValue;
            for (auto& a : s.attributes)
                if (a.first == aName)
                {
                    a.second = aValue;
                    return;
                }
            s.attributes.emplace_back(aName, aValue);
        }
        // (del name: no longer one of the module's attributes (its object remains, for the module's functions))
        [[maybe_unused]] void undefine(char const* aModule, char const* aName)
        {
            auto& s = the_state();
            (void)aModule;
            std::erase_if(s.attributes, [&](auto const& a) { return a.first == aName; });
        }
        // (a module's global assigned (global name; name = value): its object, and the module's attribute (if it has been imported))
        void set_global(char const* aModule, char const* aName, py const& aValue)
        {
            define(aModule, aName, aValue);
            if (modules_imported(aModule))
                py{ fn_import(aModule) }.set_attr(aName, aValue);
        }
        module_writer const& writer(char const* aModule)
        {
            static module_writer const sWriter{ "sched", sched_docs::doc };
            (void)aModule;
            return sWriter;
        }
        // --- sched (Lib/sched.py)

        void make_sched()
        {
            static module_writer const& w = writer("sched");
            define("sched", "__all__", py_list({ S<"scheduler">() }));
            define("sched", "Event", (Mod<"collections">().attr("namedtuple"))(S<"Event">(), S<"time, priority, sequence, action, argument, kwargs">()));
            (G<"sched.Event">()).attr("time").set_attr("__doc__", S<"Numeric type compatible with the return value of the\ntimefunc function passed to the constructor.">());
            (G<"sched.Event">()).attr("priority").set_attr("__doc__", S<"Events scheduled for the same time will be executed\nin the order of their priority.">());
            (G<"sched.Event">()).attr("sequence").set_attr("__doc__", S<"A continually increasing sequence number that\n    separates events if time and priority are equal.">());
            (G<"sched.Event">()).attr("action").set_attr("__doc__", S<"Executing the event means executing\naction(*argument, **kwargs)">());
            (G<"sched.Event">()).attr("argument").set_attr("__doc__", S<"argument is a sequence holding the positional\narguments for the action.">());
            (G<"sched.Event">()).attr("kwargs").set_attr("__doc__", S<"kwargs is a dictionary holding the keyword\narguments for the action.">());
            define("sched", "_sentinel", (B<"object">())());
            define("sched", "scheduler", w.make_class("scheduler", {  }, {
                { "__init__", w.method("scheduler", "__init__", { py_positional("timefunc", Mod<"time">().attr("monotonic")), py_positional("delayfunc", Mod<"time">().attr("sleep")) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py timefunc = a[1];
                        py delayfunc = a[2];
                        self.set_attr("_queue", py_list());
                        self.set_attr("_lock", (Mod<"threading">().attr("RLock"))());
                        self.set_attr("timefunc", timefunc);
                        self.set_attr("delayfunc", delayfunc);
                        self.set_attr("_sequence_generator", (Mod<"itertools">().attr("count"))());
                        return none();
                    }) },
                { "enterabs", w.method("scheduler", "enterabs", { py_positional("time"), py_positional("priority"), py_positional("action"), py_positional("argument", py_tuple()), py_positional("kwargs", G<"sched._sentinel">()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py time = a[1];
                        py priority = a[2];
                        py action = a[3];
                        py argument = a[4];
                        py kwargs = a[5];
                        py event;
                        if (kwargs.is((G<"sched._sentinel">())))
                        {
                            kwargs = py_dict();
                        }
                        {
                            py const manager1 = self.attr("_lock");
                            py const exit1 = pyclass::special_method(manager1, "__exit__");
                            py const value1 = pyclass::special_method(manager1, "__enter__")(manager1);
                            [[maybe_unused]] int flow1 = 0;
                            [[maybe_unused]] py result1;
                            bool exited1 = false;
                            try
                            {
                                event = (G<"sched.Event">())(time, priority, (B<"next">())(self.attr("_sequence_generator")), action, argument, kwargs);
                                (Mod<"heapq">().attr("heappush"))(self.attr("_queue"), event);
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler1;
                                exited1 = true;
                                if (!exit1(manager1, type_of(handler1.exception), handler1.exception, handler1.exception.attr("__traceback__")).truthy())
                                    py_reraise(handler1.exception);
                            }
                            finally1:
                            if (!exited1)
                                exit1(manager1, none(), none(), none());
                            if (flow1 == 1)
                                return result1;
                        }
                        return event;
                    }) },
                { "enter", w.method("scheduler", "enter", { py_positional("delay"), py_positional("priority"), py_positional("action"), py_positional("argument", py_tuple()), py_positional("kwargs", G<"sched._sentinel">()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py delay = a[1];
                        py priority = a[2];
                        py action = a[3];
                        py argument = a[4];
                        py kwargs = a[5];
                        py time;
                        time = self.method("timefunc") + delay;
                        return self.method("enterabs", { time, priority, action, argument, kwargs });
                    }) },
                { "cancel", w.method("scheduler", "cancel", { py_positional("event") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py event = a[1];
                        {
                            py const manager2 = self.attr("_lock");
                            py const exit2 = pyclass::special_method(manager2, "__exit__");
                            py const value2 = pyclass::special_method(manager2, "__enter__")(manager2);
                            [[maybe_unused]] int flow2 = 0;
                            [[maybe_unused]] py result2;
                            bool exited2 = false;
                            try
                            {
                                self.attr("_queue").method("remove", { event });
                                (Mod<"heapq">().attr("heapify"))(self.attr("_queue"));
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler2;
                                exited2 = true;
                                if (!exit2(manager2, type_of(handler2.exception), handler2.exception, handler2.exception.attr("__traceback__")).truthy())
                                    py_reraise(handler2.exception);
                            }
                            finally2:
                            if (!exited2)
                                exit2(manager2, none(), none(), none());
                            if (flow2 == 1)
                                return result2;
                        }
                        return none();
                    }) },
                { "empty", w.method("scheduler", "empty", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        {
                            py const manager3 = self.attr("_lock");
                            py const exit3 = pyclass::special_method(manager3, "__exit__");
                            py const value3 = pyclass::special_method(manager3, "__enter__")(manager3);
                            [[maybe_unused]] int flow3 = 0;
                            [[maybe_unused]] py result3;
                            bool exited3 = false;
                            try
                            {
                                result3 = boolean(!self.attr("_queue").truthy());
                                flow3 = 1;
                                goto finally3;
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler3;
                                exited3 = true;
                                if (!exit3(manager3, type_of(handler3.exception), handler3.exception, handler3.exception.attr("__traceback__")).truthy())
                                    py_reraise(handler3.exception);
                            }
                            finally3:
                            if (!exited3)
                                exit3(manager3, none(), none(), none());
                            if (flow3 == 1)
                                return result3;
                        }
                        return none();
                    }) },
                { "run", w.method("scheduler", "run", { py_positional("blocking", boolean(true)) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py blocking = a[1];
                        py action;
                        py argument;
                        py delay;
                        py delayfunc;
                        py kwargs;
                        py lock;
                        py now;
                        py pop;
                        py priority;
                        py q;
                        py sequence;
                        py time;
                        py timefunc;
                        // localize variable access to minimize overhead
                        // and to improve thread safety
                        lock = self.attr("_lock");
                        q = self.attr("_queue");
                        delayfunc = self.attr("delayfunc");
                        timefunc = self.attr("timefunc");
                        pop = Mod<"heapq">().attr("heappop");
                        while (boolean(true).truthy())
                        {
                            {
                                py const manager5 = lock;
                                py const exit5 = pyclass::special_method(manager5, "__exit__");
                                py const value5 = pyclass::special_method(manager5, "__enter__")(manager5);
                                [[maybe_unused]] int flow5 = 0;
                                [[maybe_unused]] py result5;
                                bool exited5 = false;
                                try
                                {
                                    if (!q.truthy())
                                    {
                                        flow5 = 2;
                                        goto finally5;
                                    }
                                    auto const items6 = unpack(q[I<0>()], 6);
                                    time = items6[0];
                                    priority = items6[1];
                                    sequence = items6[2];
                                    action = items6[3];
                                    argument = items6[4];
                                    kwargs = items6[5];
                                    now = timefunc();
                                    if (time > now)
                                    {
                                        delay = boolean(true);
                                    }
                                    else
                                    {
                                        delay = boolean(false);
                                        pop(q);
                                    }
                                }
                                catch (raised const&)
                                {
                                    pyclass::handler_scope handler5;
                                    exited5 = true;
                                    if (!exit5(manager5, type_of(handler5.exception), handler5.exception, handler5.exception.attr("__traceback__")).truthy())
                                        py_reraise(handler5.exception);
                                }
                                finally5:
                                if (!exited5)
                                    exit5(manager5, none(), none(), none());
                                if (flow5 == 1)
                                    return result5;
                                if (flow5 == 2)
                                    break;
                                if (flow5 == 3)
                                    continue;
                            }
                            if (delay.truthy())
                            {
                                if (!blocking.truthy())
                                {
                                    return time - now;
                                }
                                delayfunc(time - now);
                            }
                            else
                            {
                                call_ex(action, {  }, argument, kwargs);
                                delayfunc(I<0>());
                            }
                        }
                        return none();
                    }) },
                { "queue", B<"property">()(w.method("scheduler", "queue", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py events;
                        // Use heapq to sort the queue rather than using 'sorted(self._queue)'.
                        // With heapq, two events scheduled at the same time will show in
                        // the actual order they would be retrieved.
                        {
                            py const manager7 = self.attr("_lock");
                            py const exit7 = pyclass::special_method(manager7, "__exit__");
                            py const value7 = pyclass::special_method(manager7, "__enter__")(manager7);
                            [[maybe_unused]] int flow7 = 0;
                            [[maybe_unused]] py result7;
                            bool exited7 = false;
                            try
                            {
                                events = self.attr("_queue")[B<"slice">()(none(), none(), none())];
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler7;
                                exited7 = true;
                                if (!exit7(manager7, type_of(handler7.exception), handler7.exception, handler7.exception.attr("__traceback__")).truthy())
                                    py_reraise(handler7.exception);
                            }
                            finally7:
                            if (!exited7)
                                exit7(manager7, none(), none(), none());
                            if (flow7 == 1)
                                return result7;
                        }
                        return (B<"list">())((B<"map">())(Mod<"heapq">().attr("heappop"), py_list({ events }) * py{ events.size() }));
                    })) }, }));
        }

        bool sched_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            auto const result = py_guarded([&]()
            {
                auto& s = the_state();
                if (!s.made)
                {
                    s.made = true;
                    make_sched();
                }
                if (auto const doc = sched_docs::doc(""))
                    aAttributes.emplace_back("__doc__", py{ doc }.release());
                aAttributes.emplace_back("time", py{ fn_import("time") }.release());
                aAttributes.emplace_back("heapq", py{ fn_import("heapq") }.release());
                aAttributes.emplace_back("namedtuple", import_attribute("collections", "namedtuple").release());
                aAttributes.emplace_back("count", import_attribute("itertools", "count").release());
                aAttributes.emplace_back("threading", py{ fn_import("threading") }.release());
                aAttributes.emplace_back("_time", import_attribute("time", "monotonic").release());
                for (auto const& [name, value] : s.attributes)
                    aAttributes.emplace_back(name, value.reference());
                return py::none();
            });
            release(result);
            return result != nullptr;
        }

        inline bool const sModule = register_module("sched", sched_module);
    }
}
