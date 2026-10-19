/*
  module.cpp

  Copyright (c) 2026 Leigh Johnston.  All Rights Reserved.

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

// the numpy module and the plugin's entry point (neos.runtime.numpy: "import numpy")

#include <cmath>
#include <map>
#include <limits>
#include <numbers>
#include "numpy.hpp"
#include "functions.hpp"

namespace neos::numpy
{
    void register_math();
    void register_format();
    void register_lib();
    void register_lib2();
    void register_lib3();
    object* module_constant(std::string_view aName);

    namespace
    {
        void retain_static(neos_rt_object*) {}
        void release_static(neos_rt_object*) {}

        object* module_repr_slot(neos_py_context*, object*)
        {
            return make_str("<module 'numpy' (neos.runtime.numpy)>");
        }

        object* module_getattr_slot(neos_py_context*, object*, char const* aName)
        {
            return module_getattr(aName);
        }

        neos_py_type make_module_type()
        {
            neos_py_type type{};
            type.base = neos_rt_object_type{ "numpy.module", retain_static, release_static };
            type.magic = NEOS_PY_TYPE_MAGIC;
            type.name = "module";
            type.repr = module_repr_slot;
            type.getattr = module_getattr_slot;
            return type;
        }

        neos_py_type const sModuleType = make_module_type();
        neos_rt_object sModule{ &sModuleType.base };
    }

    object* module_getattr(char const* aName)
    {
        std::string_view const name{ aName };
        if (name == "pi")
            return make_float(std::numbers::pi);
        if (name == "e")
            return make_float(std::numbers::e);
        if (name == "euler_gamma")
            return make_float(std::numbers::egamma);
        if (name == "inf")
            return make_float(std::numeric_limits<double>::infinity());
        if (name == "nan")
            return make_float(std::numeric_limits<double>::quiet_NaN());
        if (name == "newaxis")
            return new_none();
        if (name == "True_" || name == "False_")
        {
            scalar_value v;
            v.type = dtype::Bool;
            v.b = name == "True_";
            return as_object(new_scalar(dtype::Bool, v));
        }
        if (name == "__name__")
            return make_str("numpy");
        if (name == "__version__")
            return make_str("2.5.3");
        // (ufuncs with other names)
        static std::map<std::string_view, char const*> const sAliases =
        {
            { "abs", "absolute" }, { "acos", "arccos" }, { "acosh", "arccosh" }, { "asin", "arcsin" }, { "asinh", "arcsinh" }, { "atan", "arctan" },
            { "atanh", "arctanh" }, { "atan2", "arctan2" }, { "pow", "power" }, { "bitwise_left_shift", "left_shift" }, { "bitwise_right_shift", "right_shift" },
            { "true_divide", "divide" }, { "mod", "remainder" }, { "conj", "conjugate" }, { "bitwise_not", "invert" }, { "bitwise_invert", "invert" }
        };
        if (auto const alias = sAliases.find(name); alias != sAliases.end())
            if (auto const* u = find_ufunc(alias->second))
                return make_ufunc_object(*u);
        if (auto const c = module_constant(name))
            return c;
        if (failed())
            return nullptr;
        if (has_function(name))
            return make_function_object(aName);
        if (auto const* u = find_ufunc(name))
            return make_ufunc_object(*u);
        if (auto const c = find_class(name))
            return c;
        if (name == "PINF" || name == "NINF" || name == "PZERO" || name == "NZERO" || name == "Inf" || name == "Infinity" || name == "NaN" || name == "NAN" ||
            name == "infty" || name == "float_" || name == "complex_" || name == "round_" || name == "product" || name == "cumproduct" || name == "alltrue" ||
            name == "sometrue")
            return error("AttributeError", "`np." + std::string{ name } + "` was removed in the NumPy 2.0 release.");
        return error("AttributeError", "module 'numpy' has no attribute '" + std::string{ name } + "'");
    }

    namespace
    {
        object* fn_module(object* aContext)
        {
            if (ctx == nullptr)
            {
                ctx = reinterpret_cast<neos_py_context_object*>(aContext)->context;
                register_math();
                register_format();
                register_lib();
                register_lib2();
                register_lib3();
            }
            return &sModule;
        }

        neos_rt_function const sFunctions[] =
        {
            { "module", NEOS_RT_FUNCTION(fn_module), NEOS_RT_OBJECT, 1u, { NEOS_RT_OBJECT }, 0u }
        };

        neos_rt_plugin const sPlugin =
        {
            NEOS_RUNTIME_PLUGIN_VERSION,
            "numpy",
            "NumPy",
            static_cast<unsigned>(std::size(sFunctions)),
            sFunctions
        };
    }
}

NEOS_RUNTIME_PLUGIN_ENTRY
{
    return &neos::numpy::sPlugin;
}

NEOS_RUNTIME_PLUGIN_ERROR
{
    // (errors are raised through the Python runtime plugin's context)
    return nullptr;
}
