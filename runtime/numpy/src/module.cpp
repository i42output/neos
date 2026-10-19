/*
  module.cpp

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
