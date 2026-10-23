/*
  graphlib.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/graphlib.py), Copyright (c) 2001-2024 Python Software
  Foundation; All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from Python to C++ for the neos Python runtime plugin and adapted to
  its object model (see runtime/python/NOTICE).
*/

// The module graphlib (CPython 3.13's Lib/graphlib.py, translated from Python by py2neos, a Python-to-C++ translator): its classes are made (by type)
// of closures (pyclass.hpp), so a program's subclasses override their methods.

#include <map>
#include <string>
#include <vector>
#include "python.hpp"
#include "objects.hpp"
#include "values.hpp"
#include "pyclass.hpp"
#include "graphlib_docs.hpp"

namespace neos::python_runtime::detail::graphlib_module
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
            std::map<std::string, py, std::less<>> objects;         ///< (its objects, by qualified name (e.g. "graphlib.name"))
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
                py_raise("SystemError", std::string{ "graphlib: no object " } + aQualifiedName);
            return o->second;
        }
        // (an object of the module: its entry, found once (G<"graphlib.name">()))
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
            static module_writer const sWriter{ "graphlib", graphlib_docs::doc };
            (void)aModule;
            return sWriter;
        }
        // --- graphlib (Lib/graphlib.py)

        void make_graphlib()
        {
            static module_writer const& w = writer("graphlib");
            define("graphlib", "__all__", py_list({ S<"TopologicalSorter">(), S<"CycleError">() }));
            define("graphlib", "_NODE_OUT", I<-1>());
            define("graphlib", "_NODE_DONE", I<-2>());
            define("graphlib", "_NodeInfo", w.make_class("_NodeInfo", {  }, {
                { "__slots__", py_tuple({ S<"node">(), S<"npredecessors">(), S<"successors">() }) },
                { "__init__", w.method("_NodeInfo", "__init__", { py_positional("node") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py node = a[1];
                        self.set_attr("node", node);
                        // Number of predecessors, generally >= 0. When this value falls to 0,
                        // and is returned by get_ready(), this is set to _NODE_OUT and when the
                        // node is marked done by a call to done(), set to _NODE_DONE.
                        self.set_attr("npredecessors", I<0>());
                        // List of successor nodes. The list can contain duplicated elements as
                        // long as they're all reflected in the successor's npredecessors attribute.
                        self.set_attr("successors", py_list());
                        return none();
                    }) }, }));

            define("graphlib", "CycleError", w.make_class("CycleError", { B<"ValueError">() }, {
 }));

            define("graphlib", "TopologicalSorter", w.make_class("TopologicalSorter", {  }, {
                { "__init__", w.method("TopologicalSorter", "__init__", { py_positional("graph", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py graph = a[1];
                        py node;
                        py predecessors;
                        self.set_attr("_node2info", py_dict());
                        self.set_attr("_ready_nodes", none());
                        self.set_attr("_npassedout", I<0>());
                        self.set_attr("_nfinished", I<0>());
                        if (!graph.is_none())
                        {
                            for (py const& item : iterate{ graph.method("items") })
                            {
                                auto const items2 = unpack(item, 2);
                                node = items2[0];
                                predecessors = items2[1];
                                call_ex(self.attr("add"), { node }, predecessors, py{});
                            }
                        }
                        return none();
                    }) },
                { "_get_nodeinfo", w.method("TopologicalSorter", "_get_nodeinfo", { py_positional("node") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py node = a[1];
                        py result;
                        if (([&]() -> py { result = self.attr("_node2info").method("get", { node }); return result; }()).is_none())
                        {
                            py const value3 = (G<"graphlib._NodeInfo">())(node);
                            self.attr("_node2info").set_item(node, value3);
                            result = value3;
                        }
                        return result;
                    }) },
                { "add", w.method("TopologicalSorter", "add", { py_positional("node") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py node = a[1];
                        py predecessors = a[2];
                        py nodeinfo;
                        py pred;
                        py pred_info;
                        if (!self.attr("_ready_nodes").is_none())
                        {
                            py_raise((B<"ValueError">())(S<"Nodes cannot be added after a call to prepare()">()));
                        }
                        // Create the node -> predecessor edges
                        nodeinfo = self.method("_get_nodeinfo", { node });
                        {
                            py const target4 = nodeinfo;
                            target4.set_attr("npredecessors", inplace(NEOS_PY_ADD, target4.attr("npredecessors"), py{ predecessors.size() }));
                        }
                        // Create the predecessor -> node edges
                        for (py const& item : iterate{ predecessors })
                        {
                            pred = item;
                            pred_info = self.method("_get_nodeinfo", { pred });
                            pred_info.attr("successors").method("append", { node });
                        }
                        return none();
                    }, true) },
                { "prepare", w.method("TopologicalSorter", "prepare", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py cycle;
                        if (!self.attr("_ready_nodes").is_none())
                        {
                            py_raise((B<"ValueError">())(S<"cannot prepare() more than once">()));
                        }
                        self.set_attr("_ready_nodes", [&]() -> py { py const comprehension6 = py_list(); py i; for (py const& item : iterate{ self.attr("_node2info").method("values") }) { i = item; if (!(i.attr("npredecessors") == (I<0>()))) continue; comprehension6.method("append", { i.attr("node") }); } return comprehension6; }());
                        // ready_nodes is set before we look for cycles on purpose:
                        // if the user wants to catch the CycleError, that's fine,
                        // they can continue using the instance to grab as many
                        // nodes as possible before cycles block more progress
                        cycle = self.method("_find_cycle");
                        if (cycle.truthy())
                        {
                            py_raise((G<"graphlib.CycleError">())(S("nodes are in a cycle"), cycle));
                        }
                        return none();
                    }) },
                { "get_ready", w.method("TopologicalSorter", "get_ready", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py n2i;
                        py node;
                        py result;
                        if (self.attr("_ready_nodes").is_none())
                        {
                            py_raise((B<"ValueError">())(S<"prepare() must be called first">()));
                        }
                        // Get the nodes that are ready and mark them
                        result = (B<"tuple">())(self.attr("_ready_nodes"));
                        n2i = self.attr("_node2info");
                        for (py const& item : iterate{ result })
                        {
                            node = item;
                            n2i[node].set_attr("npredecessors", G<"graphlib._NODE_OUT">());
                        }
                        // Clean the list of nodes that are ready and update
                        // the counter of nodes that we have returned.
                        self.attr("_ready_nodes").method("clear");
                        {
                            py const target8 = self;
                            target8.set_attr("_npassedout", inplace(NEOS_PY_ADD, target8.attr("_npassedout"), py{ result.size() }));
                        }
                        return result;
                    }) },
                { "is_active", w.method("TopologicalSorter", "is_active", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if (self.attr("_ready_nodes").is_none())
                        {
                            py_raise((B<"ValueError">())(S<"prepare() must be called first">()));
                        }
                        return [&]() -> py { py const t = boolean(self.attr("_nfinished") < self.attr("_npassedout")); return t.truthy() ? t : (B<"bool">())(self.attr("_ready_nodes")); }();
                    }) },
                { "__bool__", w.method("TopologicalSorter", "__bool__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.method("is_active");
                    }) },
                { "done", w.method("TopologicalSorter", "done", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py nodes = a[1];
                        py n2i;
                        py node;
                        py nodeinfo;
                        py stat;
                        py successor;
                        py successor_info;
                        if (self.attr("_ready_nodes").is_none())
                        {
                            py_raise((B<"ValueError">())(S<"prepare() must be called first">()));
                        }
                        n2i = self.attr("_node2info");
                        for (py const& item : iterate{ nodes })
                        {
                            node = item;
                            // Check if we know about this node (it was added previously using add()
                            if (([&]() -> py { nodeinfo = n2i.method("get", { node }); return nodeinfo; }()).is_none())
                            {
                                py_raise((B<"ValueError">())(S(std::string{ "node " } + node.repr() + " was not added using add()")));
                            }
                            // If the node has not being returned (marked as ready) previously, inform the user.
                            stat = nodeinfo.attr("npredecessors");
                            if (stat != (G<"graphlib._NODE_OUT">()))
                            {
                                if (stat >= (I<0>()))
                                {
                                    py_raise((B<"ValueError">())(S(std::string{ "node " } + node.repr() + " was not passed out (still not ready)")));
                                }
                                else if (stat == (G<"graphlib._NODE_DONE">()))
                                {
                                    py_raise((B<"ValueError">())(S(std::string{ "node " } + node.repr() + " was already marked done")));
                                }
                                else
                                {
                                    if (!boolean(false).truthy())
                                        py_raise(B<"AssertionError">()(S(std::string{ "node " } + node.repr() + ": unknown status " + stat.str())));
                                }
                            }
                            // Mark the node as processed
                            nodeinfo.set_attr("npredecessors", G<"graphlib._NODE_DONE">());
                            // Go to all the successors and reduce the number of predecessors, collecting all the ones
                            // that are ready to be returned in the next get_ready() call.
                            for (py const& item : iterate{ nodeinfo.attr("successors") })
                            {
                                successor = item;
                                successor_info = n2i[successor];
                                {
                                    py const target11 = successor_info;
                                    target11.set_attr("npredecessors", inplace(NEOS_PY_SUBTRACT, target11.attr("npredecessors"), I<1>()));
                                }
                                if (successor_info.attr("npredecessors") == (I<0>()))
                                {
                                    self.attr("_ready_nodes").method("append", { successor });
                                }
                            }
                            {
                                py const target12 = self;
                                target12.set_attr("_nfinished", inplace(NEOS_PY_ADD, target12.attr("_nfinished"), I<1>()));
                            }
                        }
                        return none();
                    }, true) },
                { "_find_cycle", w.method("TopologicalSorter", "_find_cycle", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py itstack;
                        py n2i;
                        py node;
                        py node2stacki;
                        py seen;
                        py stack;
                        n2i = self.attr("_node2info");
                        stack = py_list();
                        itstack = py_list();
                        seen = (B<"set">())();
                        node2stacki = py_dict();
                        for (py const& item : iterate{ n2i })
                        {
                            node = item;
                            if (seen.contains(node))
                            {
                                continue;
                            }
                            while (boolean(true).truthy())
                            {
                                if (seen.contains(node))
                                {
                                    // If we have seen already the node and is in the
                                    // current stack we have found a cycle.
                                    if (node2stacki.contains(node))
                                    {
                                        return stack[B<"slice">()(node2stacki[node], none(), none())] + py_list({ node });
                                    }
                                }
                                else
                                {
                                    // else go on to get next successor
                                    seen.method("add", { node });
                                    itstack.method("append", { (B<"iter">())(n2i[node].attr("successors")).attr("__next__") });
                                    node2stacki.set_item(node, py{ stack.size() });
                                    stack.method("append", { node });
                                }
                                // Backtrack to the topmost stack entry with
                                // at least another successor.
                                bool broke15 = false;
                                while (stack.truthy())
                                {
                                    try
                                    {
                                        node = itstack[I<-1>()]();
                                        broke15 = true;
                                        break;
                                    }
                                    catch (raised const&)
                                    {
                                        pyclass::handler_scope handler16;
                                        if (matches(handler16.exception, B<"StopIteration">()))
                                        {
                                            del_item(node2stacki, stack.method("pop"));
                                            itstack.method("pop");
                                        }
                                        else
                                            py_reraise(handler16.exception);
                                    }
                                }
                                if (!broke15)
                                {
                                    break;
                                }
                            }
                        }
                        return none();
                    }) },
                { "static_order", w.method("TopologicalSorter", "static_order", {  }, [](arguments a) -> py
                    {
                        return make_native_generator("static_order", "TopologicalSorter.static_order", w.module, [a = std::vector<py>(a.begin(), a.end())]() -> py
                        {
                            py self = a[0];
                            py node_group;
                            self.method("prepare");
                            while (self.method("is_active").truthy())
                            {
                                node_group = self.method("get_ready");
                                yield_from(node_group);
                                call_ex(self.attr("done"), {  }, node_group, py{});
                            }
                            return none();
                        });
                    }) },
                { "__class_getitem__", (B<"classmethod">())(Mod<"types">().attr("GenericAlias")) }, }));
        }

        bool graphlib_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            auto const result = py_guarded([&]()
            {
                auto& s = the_state();
                if (!s.made)
                {
                    s.made = true;
                    make_graphlib();
                }
                if (auto const doc = graphlib_docs::doc(""))
                    aAttributes.emplace_back("__doc__", py{ doc }.release());
                aAttributes.emplace_back("GenericAlias", import_attribute("types", "GenericAlias").release());
                for (auto const& [name, value] : s.attributes)
                    aAttributes.emplace_back(name, value.reference());
                return py::none();
            });
            release(result);
            return result != nullptr;
        }

        inline bool const sModule = register_module("graphlib", graphlib_module);
    }
}
