/*
  modules.cpp

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

// Modules (ECMAScript's Source Text Module Records): loading (a module's source file, compiled while the program runs as the compiler's
// module code), linking (its environment and imported bindings), evaluation (with top-level await: asynchronous modules), namespace objects,
// dynamic import() and import.meta
//
// A module's compiled code defines it (javascript::module_define): its function (which, called with its environment and true, initializes
// the environment's bindings and creates its functions; called with its environment alone, evaluates its code) and a descriptor of its
// requests, imports and exports (see the compiler's module_descriptor). Its environment is an environment whose bindings can be found by
// name; an imported binding's slot holds an import binding (the module and binding name it refers to) or a namespace object.

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include "js.hpp"
#include "api.hpp"

namespace neos::javascript_runtime
{
    namespace
    {
        enum class module_status : std::uint8_t { New, Unlinked, Linking, Linked, Evaluating, EvaluatingAsync, Evaluated };

        struct module_record;

        struct import_entry
        {
            std::string local;
            std::size_t request = 0u;
            std::string name;
            bool ns = false;
        };

        struct export_entry
        {
            enum kind_t { Local, Indirect, StarAs, Star } kind = Local;
            std::string name;           ///< (the export name (none for export *))
            std::string local;          ///< (a local export's binding)
            std::size_t request = 0u;
            std::string importName;     ///< (an indirect export's import name)
        };

        struct resolved_binding
        {
            enum kind_t { None, Ambiguous, Binding, Namespace } kind = None;
            module_record* module = nullptr;
            std::string name;
        };

        struct module_record
        {
            std::string path;
            void* program = nullptr;
            std::uint32_t function = 0u;
            std::vector<std::string> requests;
            std::vector<std::string> requestTypes;      ///< (their import attributes' type ("" if none))
            std::vector<std::string> unsupportedAttributes;
            std::vector<module_record*> loaded;
            bool synthetic = false;                     ///< (a JSON module: its default export is its value)
            value syntheticValue = Undefined;
            std::vector<import_entry> imports;
            std::vector<export_entry> exports;
            std::string names;
            bool hasTopLevelAwait = false;
            bool defined = false;
            value env = Undefined;
            value ns = Undefined;
            value meta = Undefined;
            module_status status = module_status::New;
            std::uint32_t dfsIndex = 0u;
            std::uint32_t dfsAncestorIndex = 0u;
            module_record* cycleRoot = nullptr;
            bool hasEvaluationError = false;
            value evaluationError = Undefined;
            bool asyncEvaluation = false;
            std::uint64_t asyncEvaluationOrder = 0u;
            std::vector<module_record*> asyncParents;
            std::uint32_t pendingAsyncDependencies = 0u;
            value capability = Undefined;   ///< ([[TopLevelCapability]]: its promise (resolve and reject below))
            value resolve = Undefined;
            value reject = Undefined;
            std::map<std::string, std::uint32_t> slots;     ///< (the slots of its environment's bindings, by name)
        };

        std::map<std::string, module_record*>& registry()
        {
            static std::map<std::string, module_record*> sRegistry;
            return sRegistry;
        }

        std::map<void*, module_record*>& by_program()
        {
            static std::map<void*, module_record*> sModules;
            return sModules;
        }

        module_record* tLoading = nullptr;          // (the module whose program is being run to define it)

        // (the modules live as long as the program)
        root_tracer const sModuleRoots{ [](tracer& aTracer)
        {
            auto const trace = [&](module_record const& aModule)
            {
                aTracer.mark(aModule.syntheticValue);
                aTracer.mark(aModule.env);
                aTracer.mark(aModule.ns);
                aTracer.mark(aModule.meta);
                aTracer.mark(aModule.evaluationError);
                aTracer.mark(aModule.capability);
                aTracer.mark(aModule.resolve);
                aTracer.mark(aModule.reject);
            };
            for (auto const& entry : registry())
                trace(*entry.second);
            for (auto const& entry : by_program())
                trace(*entry.second);
            if (tLoading != nullptr)
                trace(*tLoading);
        } };
        std::uint64_t sAsyncEvaluationOrder = 1u;

        std::string main_path()
        {
            char const* arguments[1] = {};
            if (host() != nullptr && host()->arguments != nullptr && host()->arguments(arguments, 1u) > 0u && arguments[0] != nullptr)
                return std::filesystem::absolute(arguments[0]).lexically_normal().string();
            return std::filesystem::absolute("main.mjs").string();
        }

        [[noreturn]] void syntax_error(std::string const& aMessage)
        {
            throw_syntax_error(aMessage);
        }

        // --- loading

        std::string resolve_specifier(std::string const& aReferrer, std::string const& aSpecifier)
        {
            if (!(aSpecifier.starts_with("./") || aSpecifier.starts_with("../") || aSpecifier.starts_with("/")))
                throw_type_error("Cannot resolve module specifier '" + aSpecifier + "' (only relative and absolute paths are supported)");
            std::filesystem::path const base = std::filesystem::path{ aReferrer }.parent_path();
            auto const path = aSpecifier.starts_with("/") ? std::filesystem::path{ aSpecifier } : base / aSpecifier;
            return std::filesystem::absolute(path).lexically_normal().string();
        }

        void define(module_record& aModule, std::string const& aDescriptor)
        {
            // (the descriptor: records separated by \x02, fields by \x01 (see the compiler's module_descriptor))
            std::size_t start = 0u;
            while (start < aDescriptor.size())
            {
                auto end = aDescriptor.find('\x02', start);
                if (end == std::string::npos)
                    end = aDescriptor.size();
                std::vector<std::string> fields;
                std::size_t f = start;
                while (f <= end)
                {
                    auto const next = aDescriptor.find('\x01', f);
                    auto const e = (next == std::string::npos || next > end) ? end : next;
                    fields.push_back(aDescriptor.substr(f, e - f));
                    f = e + 1u;
                }
                start = end + 1u;
                if (fields.empty() || fields[0].empty())
                    continue;
                auto const& k = fields[0];
                auto const index = [&](std::size_t i) { return static_cast<std::size_t>(std::stoul(fields.at(i))); };
                if (k == "R")
                {
                    aModule.requests.push_back(fields.at(1));
                    std::string type;
                    for (std::size_t a = 2u; a < fields.size(); ++a)
                    {
                        auto const eq = fields[a].find('=');
                        auto const key = fields[a].substr(0u, eq);
                        if (key == "type")
                            type = fields[a].substr(eq + 1u);
                        else
                            aModule.unsupportedAttributes.push_back(key);
                    }
                    aModule.requestTypes.push_back(type);
                }
                else if (k == "I")
                    aModule.imports.push_back(import_entry{ fields.at(1), index(2), fields.at(3), false });
                else if (k == "N" && fields.size() == 3u)
                    aModule.imports.push_back(import_entry{ fields.at(1), index(2), {}, true });
                else if (k == "N")
                    aModule.names = fields.at(1);
                else if (k == "E")
                    aModule.exports.push_back(export_entry{ export_entry::Local, fields.at(1), fields.at(2) });
                else if (k == "X")
                    aModule.exports.push_back(export_entry{ export_entry::Indirect, fields.at(1), {}, index(2), fields.at(3) });
                else if (k == "Y")
                    aModule.exports.push_back(export_entry{ export_entry::StarAs, fields.at(1), {}, index(2) });
                else if (k == "S")
                    aModule.exports.push_back(export_entry{ export_entry::Star, {}, {}, index(1) });
                else if (k == "A")
                    aModule.hasTopLevelAwait = true;
            }
            // (its bindings' slots: the names' order)
            std::uint32_t slot = 0u;
            std::size_t i = 1u;
            auto const& names = aModule.names;
            while (i < names.size())
            {
                ++i;
                auto const end = names.find(',', i);
                auto const entry = names.substr(i, end == std::string::npos ? std::string::npos : end - i);
                if (!entry.empty())
                    aModule.slots[entry.substr(1u)] = slot++;
                i = end == std::string::npos ? names.size() : end;
            }
            aModule.defined = true;
        }

        module_record& load(std::string const& aPath, std::string const& aType = {})
        {
            // (HostLoadImportedModule: a module is loaded once (by its path (and type)))
            if (!aType.empty() && aType != "json")
                throw_type_error("Unsupported module type '" + aType + "'");
            auto& r = registry();
            auto const key = aType.empty() ? aPath : aPath + "#" + aType;
            if (auto const existing = r.find(key); existing != r.end())
                return *existing->second;
            std::ifstream file{ aPath, std::ios::binary };
            if (!file)
                throw_type_error("Cannot find module '" + aPath + "'");
            std::ostringstream text;
            text << file.rdbuf();
            auto* m = new module_record{};
            m->path = aPath;
            if (aType == "json")
            {
                // (a JSON module: its value (JSON.parse's) is its default export)
                auto const json = get(the_realm().global, ascii("JSON"));
                value const argument[] = { make_string(utf16(text.str())) };
                m->syntheticValue = call(get(json, ascii("parse")), json, argument);
                m->synthetic = true;
                m->exports.push_back(export_entry{ export_entry::Local, "default", "*default*" });
                m->names = "-,l*default*";
                m->slots["*default*"] = 0u;
                m->defined = true;
                r.emplace(key, m);
                return *m;
            }
            r.emplace(key, m);
            auto const* h = host();
            char const* failure = nullptr;
            auto const source = text.str();
            auto const program = h->load_n("javascript", source.data(), source.size(), aPath.c_str(), "module", &failure);
            if (program == nullptr)
            {
                r.erase(aPath);
                std::string message = failure != nullptr ? failure : "invalid module";
                if (auto const at = message.find("SyntaxError: "); at != std::string::npos)
                    message = message.substr(at + 13u);
                while (!message.empty() && (message.back() == '\n' || message.back() == '\r'))
                    message.pop_back();
                syntax_error(message);
            }
            auto* const previous = tLoading;
            tLoading = m;
            neos_rt_value result{};
#if defined(_WIN32)
            // (generated code runs on a fiber of its own: this stack's live part is scanned while it runs)
            NEOS_JS_SPILL_REGISTERS();
            stack_switch const switching{ stack_pointer() };
#endif
            auto const trap = h->run(program, NEOS_RT_VOID, &result);
            tLoading = previous;
            if (trap != nullptr || !m->defined)
                throw_error(the_realm().errorPrototype, "internal error: module '" + aPath + "' did not define itself");
            return *m;
        }

        void load_requested(module_record& aModule, std::vector<module_record*>& aVisited)
        {
            // (LoadRequestedModules: the module's requests and theirs, depth first)
            if (std::find(aVisited.begin(), aVisited.end(), &aModule) != aVisited.end())
                return;
            aVisited.push_back(&aModule);
            if (!aModule.unsupportedAttributes.empty())
                syntax_error("Unsupported import attribute '" + aModule.unsupportedAttributes.front() + "'");
            if (aModule.loaded.size() != aModule.requests.size())
            {
                aModule.loaded.clear();
                for (std::size_t i = 0u; i < aModule.requests.size(); ++i)
                    aModule.loaded.push_back(&load(resolve_specifier(aModule.path, aModule.requests[i]), aModule.requestTypes[i]));
            }
            if (aModule.status == module_status::New)
                aModule.status = module_status::Unlinked;
            for (auto* m : aModule.loaded)
                load_requested(*m, aVisited);
        }

        // --- resolution

        resolved_binding resolve_export(module_record& aModule, std::string const& aName, std::vector<std::pair<module_record*, std::string>>& aResolveSet)
        {
            for (auto const& [m, n] : aResolveSet)
                if (m == &aModule && n == aName)
                    return {};      // (a circular import request)
            aResolveSet.emplace_back(&aModule, aName);
            for (auto const& e : aModule.exports)
                if (e.kind == export_entry::Local && e.name == aName)
                    return resolved_binding{ resolved_binding::Binding, &aModule, e.local };
            for (auto const& e : aModule.exports)
            {
                if (e.kind == export_entry::StarAs && e.name == aName)
                    return resolved_binding{ resolved_binding::Namespace, aModule.loaded.at(e.request), {} };
                if (e.kind == export_entry::Indirect && e.name == aName)
                    return resolve_export(*aModule.loaded.at(e.request), e.importName, aResolveSet);
            }
            if (aName == "default")
                return {};
            resolved_binding star;
            for (auto const& e : aModule.exports)
            {
                if (e.kind != export_entry::Star)
                    continue;
                auto const resolution = resolve_export(*aModule.loaded.at(e.request), aName, aResolveSet);
                if (resolution.kind == resolved_binding::Ambiguous)
                    return resolution;
                if (resolution.kind == resolved_binding::None)
                    continue;
                if (star.kind == resolved_binding::None)
                    star = resolution;
                else if (resolution.module != star.module || resolution.kind != star.kind || resolution.name != star.name)
                    return resolved_binding{ resolved_binding::Ambiguous };
            }
            return star;
        }

        resolved_binding resolve_export(module_record& aModule, std::string const& aName)
        {
            std::vector<std::pair<module_record*, std::string>> resolveSet;
            return resolve_export(aModule, aName, resolveSet);
        }

        void exported_names(module_record& aModule, std::vector<module_record*>& aStarSet, std::vector<std::string>& aNames)
        {
            if (std::find(aStarSet.begin(), aStarSet.end(), &aModule) != aStarSet.end())
                return;
            aStarSet.push_back(&aModule);
            for (auto const& e : aModule.exports)
                if (e.kind != export_entry::Star)
                    aNames.push_back(e.name);
            for (auto const& e : aModule.exports)
                if (e.kind == export_entry::Star)
                {
                    std::vector<std::string> starNames;
                    exported_names(*aModule.loaded.at(e.request), aStarSet, starNames);
                    for (auto const& n : starNames)
                        if (n != "default" && std::find(aNames.begin(), aNames.end(), n) == aNames.end())
                            aNames.push_back(n);
                }
        }

        // --- import bindings

        class import_binding : public cell
        {
        public:
            import_binding(module_record* aModule, std::string aName) : cell{ cell_type::Internal }, module{ aModule }, name{ std::move(aName) } {}
        public:
            module_record* module;
            std::string name;
        };

        value binding_value(module_record& aModule, std::string const& aName)
        {
            if (aModule.env == Undefined)
                throw_reference_error("Cannot access '" + aName + "' before initialization");
            auto const slot = aModule.slots.find(aName);
            if (slot == aModule.slots.end())
                throw_reference_error(aName + " is not defined");
            auto v = cell_of<environment>(aModule.env).slots[slot->second];
            // (an imported binding re-exported (export { x } of an import))
            if (is_internal(v))
            {
                auto& b = cell_of<import_binding>(v);
                return binding_value(*b.module, b.name);
            }
            if (v == Empty)
                throw_reference_error("Cannot access '" + aName + "' before initialization");
            return v;
        }

        // --- namespace objects

        value module_namespace(module_record& aModule);

        class namespace_object : public object
        {
        public:
            namespace_object(module_record& aModule, std::vector<std::u16string> aExports) :
                object{ Null, object_class::ModuleNamespace }, module{ aModule }, exports{ std::move(aExports) }
            {
                extensible = false;
            }
        public:
            bool exported(value aKey) const
            {
                if (!is_string(aKey))
                    return false;
                auto const& k = str(aKey);
                return std::binary_search(exports.begin(), exports.end(), k);
            }
            value get_prototype_of() override { return Null; }
            bool set_prototype_of(value aPrototype) override { return aPrototype == Null; }
            bool is_extensible() override { return false; }
            bool prevent_extensions() override { return true; }
            std::optional<property_descriptor> get_own_property(value aKey) override
            {
                if (is_symbol(aKey))
                    return ordinary_get_own_property(aKey);
                if (!exported(aKey))
                    return {};
                auto const v = get(aKey, value_of(*this));
                return property_descriptor::data(v, AttributeWritable | AttributeEnumerable);
            }
            bool define_own_property(value aKey, property_descriptor const& aDescriptor) override
            {
                if (is_symbol(aKey))
                    return ordinary_define_own_property(aKey, aDescriptor);
                auto const current = get_own_property(aKey);
                if (!current)
                    return false;
                if (aDescriptor.configurable.value_or(false) || (aDescriptor.enumerable && !*aDescriptor.enumerable) || aDescriptor.is_accessor() ||
                    (aDescriptor.writable && !*aDescriptor.writable))
                    return false;
                if (aDescriptor.valueField)
                    return same_value(*aDescriptor.valueField, *current->valueField);
                return true;
            }
            bool has_property(value aKey) override
            {
                if (is_symbol(aKey))
                    return ordinary_get_own_property(aKey).has_value();
                return exported(aKey);
            }
            value get(value aKey, value aReceiver) override
            {
                if (is_symbol(aKey))
                    return ordinary_get(aKey, aReceiver);
                if (!exported(aKey))
                    return Undefined;
                auto const name = utf8(aKey);
                auto const binding = resolve_export(module, name);
                if (binding.kind == resolved_binding::Namespace)
                    return module_namespace(*binding.module);
                if (binding.kind != resolved_binding::Binding)
                    throw_reference_error(name + " is not defined");
                return binding_value(*binding.module, binding.name);
            }
            bool set(value, value, value) override { return false; }
            bool delete_property(value aKey) override
            {
                if (is_symbol(aKey))
                    return ordinary_delete(aKey);
                return !exported(aKey);
            }
            value_list own_property_keys() override
            {
                value_list result;
                for (auto const& e : exports)
                    result.push_back(intern(std::u16string_view{ e }));
                for (auto const& k : ordinary_own_property_keys())
                    if (is_symbol(k))
                        result.push_back(k);
                return result;
            }
        public:
            module_record& module;
            std::vector<std::u16string> exports;    ///< (sorted (by code units))
        };

        value module_namespace(module_record& aModule)
        {
            if (aModule.ns != Undefined)
                return aModule.ns;
            std::vector<module_record*> starSet;
            std::vector<std::string> names;
            exported_names(aModule, starSet, names);
            std::vector<std::u16string> unambiguous;
            for (auto const& n : names)
            {
                auto const resolution = resolve_export(aModule, n);
                if (resolution.kind == resolved_binding::Binding || resolution.kind == resolved_binding::Namespace)
                    unambiguous.push_back(str(make_string(utf16(n))));
            }
            std::sort(unambiguous.begin(), unambiguous.end());
            auto& ns = the_heap().make<namespace_object>(aModule, std::move(unambiguous));
            ns.properties.add(the_realm().symToStringTag, ascii("Module"), Undefined, 0u);
            aModule.ns = value_of(ns);
            return aModule.ns;
        }

        // --- linking

        void initialize_environment(module_record& aModule)
        {
            for (auto const& e : aModule.exports)
                if (e.kind == export_entry::Indirect)
                {
                    auto const resolution = resolve_export(aModule, e.name);
                    if (resolution.kind == resolved_binding::None || resolution.kind == resolved_binding::Ambiguous)
                        syntax_error("The requested module does not provide an export named '" + e.importName + "'" +
                            (resolution.kind == resolved_binding::Ambiguous ? " (it is ambiguous)" : ""));
                }
            auto& env = the_heap().make<environment>(Undefined, static_cast<std::uint32_t>(aModule.slots.size()));
            aModule.env = value_of(env);
            api::env_names(aModule.env, make_string(utf16(aModule.names)));
            if (aModule.synthetic)
            {
                env.slots[0] = aModule.syntheticValue;
                return;
            }
            for (auto const& i : aModule.imports)
            {
                auto& imported = *aModule.loaded.at(i.request);
                auto const slot = aModule.slots.at(i.local);
                if (i.ns)
                {
                    env.slots[slot] = module_namespace(imported);
                    continue;
                }
                auto const resolution = resolve_export(imported, i.name);
                if (resolution.kind == resolved_binding::None || resolution.kind == resolved_binding::Ambiguous)
                    syntax_error("The requested module '" + aModule.requests.at(i.request) + "' does not provide an export named '" + i.name + "'" +
                        (resolution.kind == resolved_binding::Ambiguous ? " (it is ambiguous)" : ""));
                if (resolution.kind == resolved_binding::Namespace)
                    env.slots[slot] = module_namespace(*resolution.module);
                else
                    env.slots[slot] = value_of(the_heap().make<import_binding>(resolution.module, resolution.name));
            }
            // (its bindings initialized and its functions created: its function, instantiating)
            value const arguments[] = { aModule.env, True };
            call_compiled_index(aModule.program, aModule.function, Undefined, Undefined, arguments, Undefined);
        }

        std::uint32_t inner_link(module_record& aModule, std::vector<module_record*>& aStack, std::uint32_t aIndex)
        {
            if (aModule.status == module_status::Linking || aModule.status == module_status::Linked || aModule.status == module_status::EvaluatingAsync ||
                aModule.status == module_status::Evaluated || aModule.status == module_status::Evaluating)
                return aIndex;
            aModule.status = module_status::Linking;
            aModule.dfsIndex = aModule.dfsAncestorIndex = aIndex++;
            aStack.push_back(&aModule);
            for (auto* required : aModule.loaded)
            {
                aIndex = inner_link(*required, aStack, aIndex);
                if (required->status == module_status::Linking)
                    aModule.dfsAncestorIndex = std::min(aModule.dfsAncestorIndex, required->dfsAncestorIndex);
            }
            initialize_environment(aModule);
            if (aModule.dfsAncestorIndex == aModule.dfsIndex)
            {
                for (;;)
                {
                    auto* m = aStack.back();
                    aStack.pop_back();
                    m->status = module_status::Linked;
                    if (m == &aModule)
                        break;
                }
            }
            return aIndex;
        }

        void link(module_record& aModule)
        {
            std::vector<module_record*> stack;
            try
            {
                inner_link(aModule, stack, 0u);
            }
            catch (...)
            {
                for (auto* m : stack)
                {
                    m->status = module_status::Unlinked;
                    m->env = Undefined;
                }
                throw;
            }
        }

        // --- evaluation

        void execute_async(module_record& aModule);
        void async_rejected(module_record& aModule, value aError);

        void execute(module_record& aModule)
        {
            // (a module without top-level await: its code, synchronously)
            if (aModule.synthetic)
                return;
            value const arguments[] = { aModule.env };
            call_compiled_index(aModule.program, aModule.function, Undefined, Undefined, arguments, Undefined);
        }

        void settle(module_record& aModule, bool aResolve, value aValue)
        {
            if (aModule.capability == Undefined)
                return;
            value const argument[] = { aValue };
            call(aResolve ? aModule.resolve : aModule.reject, Undefined, argument);
        }

        void gather_available_ancestors(module_record& aModule, std::vector<module_record*>& aList)
        {
            for (auto* m : aModule.asyncParents)
            {
                if (std::find(aList.begin(), aList.end(), m) != aList.end() || (m->cycleRoot != nullptr && m->cycleRoot->hasEvaluationError))
                    continue;
                if (--m->pendingAsyncDependencies == 0u)
                {
                    aList.push_back(m);
                    if (!m->hasTopLevelAwait)
                        gather_available_ancestors(*m, aList);
                }
            }
        }

        void async_fulfilled(module_record& aModule)
        {
            if (aModule.status == module_status::Evaluated)
                return;
            aModule.asyncEvaluation = false;
            aModule.status = module_status::Evaluated;
            settle(aModule, true, Undefined);
            std::vector<module_record*> list;
            gather_available_ancestors(aModule, list);
            std::sort(list.begin(), list.end(), [](module_record* a, module_record* b) { return a->asyncEvaluationOrder < b->asyncEvaluationOrder; });
            for (auto* m : list)
            {
                if (m->status == module_status::Evaluated)
                    continue;
                if (m->hasTopLevelAwait)
                    execute_async(*m);
                else
                {
                    try
                    {
                        execute(*m);
                    }
                    catch (js_exception const& e)
                    {
                        async_rejected(*m, e.thrown);
                        continue;
                    }
                    m->asyncEvaluation = false;
                    m->status = module_status::Evaluated;
                    settle(*m, true, Undefined);
                }
            }
        }

        void async_rejected(module_record& aModule, value aError)
        {
            if (aModule.status == module_status::Evaluated)
                return;
            aModule.hasEvaluationError = true;
            aModule.evaluationError = aError;
            aModule.status = module_status::Evaluated;
            aModule.asyncEvaluation = false;
            settle(aModule, false, aError);     // (its top-level capability is rejected before its ancestors')
            for (auto* m : aModule.asyncParents)
                async_rejected(*m, aError);
        }

        module_record& record_of(function_object& aFunction)
        {
            return *reinterpret_cast<module_record*>(static_cast<std::uintptr_t>(as_number(aFunction.internal)));
        }

        value async_module_fulfilled(value, arguments, value, function_object& aSelf)
        {
            async_fulfilled(record_of(aSelf));
            return Undefined;
        }

        value async_module_rejected(value, arguments aArguments, value, function_object& aSelf)
        {
            async_rejected(record_of(aSelf), argument(aArguments, 0u));
            return Undefined;
        }

        value module_handler(native_function aFunction, module_record& aModule)
        {
            auto const f = make_native(the_names().empty, 1u, aFunction);
            obj(f).internal = number(static_cast<double>(reinterpret_cast<std::uintptr_t>(&aModule)));
            return f;
        }

        void execute_async(module_record& aModule)
        {
            // (a module with top-level await: its code runs as an async function's body)
            auto const promise = start_function_of(aModule.program, aModule.env, aModule.function);
            perform_promise_then(promise, module_handler(async_module_fulfilled, aModule), module_handler(async_module_rejected, aModule));
        }

        std::uint32_t inner_evaluate(module_record& aModule, std::vector<module_record*>& aStack, std::uint32_t aIndex)
        {
            if (aModule.status == module_status::EvaluatingAsync || aModule.status == module_status::Evaluated)
            {
                if (aModule.hasEvaluationError)
                    throw js_exception{ aModule.evaluationError };
                return aIndex;
            }
            if (aModule.status == module_status::Evaluating)
                return aIndex;
            aModule.status = module_status::Evaluating;
            aModule.dfsIndex = aModule.dfsAncestorIndex = aIndex++;
            aModule.pendingAsyncDependencies = 0u;
            aStack.push_back(&aModule);
            for (auto* r : aModule.loaded)
            {
                auto* required = r;
                aIndex = inner_evaluate(*required, aStack, aIndex);
                if (required->status == module_status::Evaluating)
                    aModule.dfsAncestorIndex = std::min(aModule.dfsAncestorIndex, required->dfsAncestorIndex);
                else
                {
                    required = required->cycleRoot != nullptr ? required->cycleRoot : required;
                    if (required->hasEvaluationError)
                        throw js_exception{ required->evaluationError };
                }
                if (required->asyncEvaluation)
                {
                    ++aModule.pendingAsyncDependencies;
                    required->asyncParents.push_back(&aModule);
                }
            }
            if (aModule.pendingAsyncDependencies > 0u || aModule.hasTopLevelAwait)
            {
                aModule.asyncEvaluation = true;
                aModule.asyncEvaluationOrder = sAsyncEvaluationOrder++;
                if (aModule.pendingAsyncDependencies == 0u)
                    execute_async(aModule);
            }
            else
                execute(aModule);
            if (aModule.dfsAncestorIndex == aModule.dfsIndex)
            {
                for (;;)
                {
                    auto* m = aStack.back();
                    aStack.pop_back();
                    m->status = m->asyncEvaluation ? module_status::EvaluatingAsync : module_status::Evaluated;
                    m->cycleRoot = &aModule;
                    if (m == &aModule)
                        break;
                }
            }
            return aIndex;
        }

        value evaluate(module_record& aModule)
        {
            auto* module = &aModule;
            if ((module->status == module_status::EvaluatingAsync || module->status == module_status::Evaluated) && module->cycleRoot != nullptr)
                module = module->cycleRoot;
            if (module->capability != Undefined)
                return module->capability;
            module->capability = new_promise_capability(the_realm().promise, module->resolve, module->reject);
            std::vector<module_record*> stack;
            try
            {
                inner_evaluate(*module, stack, 0u);
            }
            catch (js_exception const& e)
            {
                for (auto* m : stack)
                {
                    m->status = module_status::Evaluated;
                    m->hasEvaluationError = true;
                    m->evaluationError = e.thrown;
                }
                settle(*module, false, e.thrown);
                return module->capability;
            }
            if (!module->asyncEvaluation)
                settle(*module, true, Undefined);
            return module->capability;
        }

        std::string referrer_path()
        {
            auto const& modules = by_program();
            if (auto const m = modules.find(current_program()); m != modules.end())
                return m->second->path;
            return main_path();
        }

        // (dynamic import: once the module is evaluated, its namespace)
        value import_fulfilled(value, arguments, value, function_object& aSelf)
        {
            return module_namespace(record_of(aSelf));
        }
    }

    value import_dynamic(value aSpecifier, value aOptions)
    {
        value resolve, reject;
        auto const promise = new_promise_capability(the_realm().promise, resolve, reject);
        try
        {
            auto const specifier = utf8(to_string(aSpecifier));
            std::string type;
            if (aOptions != Undefined)
            {
                if (!is_object(aOptions))
                    throw_type_error("The second argument of import() must be an object");
                auto const attributes = get(aOptions, ascii("with"));
                if (attributes != Undefined)
                {
                    if (!is_object(attributes))
                        throw_type_error("The 'with' option of import() must be an object");
                    for (auto const& k : enumerable_own_properties(attributes, 0))
                    {
                        auto const v = get(attributes, k);
                        if (!is_string(v))
                            throw_type_error("Import attribute values must be strings");
                        if (utf8(k) == "type")
                            type = utf8(v);
                        else
                            throw_syntax_error("Unsupported import attribute '" + utf8(k) + "'");
                    }
                }
            }
            // (the module is loaded (and linked and evaluated) by a job of its own (HostLoadImportedModule completes asynchronously): a
            // module that the referrer's graph is evaluating is not evaluated within that evaluation)
            auto const path = resolve_specifier(referrer_path(), specifier);
            enqueue_job([](value const* aValues)
            {
                // (aValues: the promise, its resolving functions, the module's path and type)
                auto const promise = aValues[0], resolve = aValues[1], reject = aValues[2];
                try
                {
                    auto& m = load(utf8(aValues[3]), utf8(aValues[4]));
                    std::vector<module_record*> visited;
                    load_requested(m, visited);
                    link(m);
                    auto const evaluated = evaluate(m);
                    perform_promise_then(evaluated, module_handler(import_fulfilled, m), Undefined, promise, resolve, reject);
                }
                catch (js_exception const& e)
                {
                    value const argument[] = { e.thrown };
                    call(reject, Undefined, argument);
                }
            }, { promise, resolve, reject, make_string_utf8(path), make_string_utf8(type) });
        }
        catch (js_exception const& e)
        {
            value const argument[] = { e.thrown };
            call(reject, Undefined, argument);
        }
        return promise;
    }

    namespace api
    {
        template <typename F>
        value module_guard(F&& aFunction)
        {
            try
            {
                return aFunction();
            }
            catch (js_exception const& e)
            {
                pending_exception() = e.thrown;
                return Exception;
            }
        }

        value module_define(std::int32_t aFunction, value aDescriptor)
        {
            return module_guard([&]() -> value
            {
                auto* m = tLoading;
                if (m == nullptr)
                {
                    // (the program's own module)
                    auto const path = main_path();
                    m = new module_record{};
                    m->path = path;
                    registry()[path] = m;
                }
                m->program = current_program();
                m->function = static_cast<std::uint32_t>(aFunction);
                by_program()[m->program] = m;
                define(*m, utf8(aDescriptor));
                return Undefined;
            });
        }

        std::int32_t module_main()
        {
            // (the program's module: loaded (with its requests), linked and evaluated; then the jobs run; an exception that it does not
            // handle is reported (the program's exit status is then 1))
            module_record* main = nullptr;
            for (auto const& [program, m] : by_program())
                if (program == current_program())
                    main = m;
            if (main == nullptr)
                return 1;
            value promise = Undefined;
            try
            {
                std::vector<module_record*> visited;
                load_requested(*main, visited);
                link(*main);
                promise = evaluate(*main);
            }
            catch (js_exception const& e)
            {
                report_uncaught(e.thrown);
                return 1;
            }
            // (its promise's rejection is reported as an uncaught exception (not an unhandled rejection))
            perform_promise_then(promise, Undefined, make_native(the_names().empty, 1u, [](value, arguments, value, function_object&) -> value { return Undefined; }));
            bool const ok = run_jobs();
            value result;
            if (promise_state_of(promise, result) == 2)
            {
                report_uncaught(result);
                return 1;
            }
            return ok ? 0 : 1;
        }

        value import_get(value aEnvironment, std::int32_t aHops, std::int32_t aSlot, value aName)
        {
            return module_guard([&]() -> value
            {
                auto e = aEnvironment;
                for (auto hops = aHops; hops > 0; --hops)
                    e = cell_of<environment>(e).parent;
                auto const v = cell_of<environment>(e).slots[static_cast<std::size_t>(aSlot)];
                if (is_internal(v))
                {
                    auto& b = cell_of<import_binding>(v);
                    return binding_value(*b.module, b.name);
                }
                if (v == Empty)
                    throw_reference_error("Cannot access '" + utf8(aName) + "' before initialization");
                return v;
            });
        }

        value import_meta(value aEnvironment)
        {
            return module_guard([&]() -> value
            {
                // (the module's environment: the outermost)
                auto e = aEnvironment;
                while (e != Undefined && cell_of<environment>(e).parent != Undefined)
                    e = cell_of<environment>(e).parent;
                module_record* module = nullptr;
                for (auto const& [path, m] : registry())
                    if (m->env == e)
                        module = m;
                // (code whose environment does not reach its module's (it has no bindings of it): the module of the running program)
                if (module == nullptr)
                    if (auto const m = by_program().find(current_program()); m != by_program().end())
                        module = m->second;
                if (module == nullptr)
                    throw_syntax_error("import.meta is only valid in a module");
                if (module->meta == Undefined)
                    module->meta = make_object(Null);
                return module->meta;
            });
        }

        value import_dynamic_(value aSpecifier, value aOptions)
        {
            return module_guard([&]() -> value { return import_dynamic(aSpecifier, aOptions); });
        }
    }
}
