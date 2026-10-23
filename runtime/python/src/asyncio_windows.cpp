/*
  asyncio_windows.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/asyncio/windows_events.py), Copyright (c) 2001-2024 Python
  Software Foundation; All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from Python to C++ for the neos Python runtime plugin and adapted to its
  object model (see runtime/python/NOTICE).
*/

// asyncio's module windows_events (Windows): its selector event loop (SelectorEventLoop) and the policy that makes it, which is
// the default policy. (CPython's default on Windows is the proactor event loop (IOCP: the module _overlapped), which the runtime
// does not have: ProactorEventLoop, IocpProactor and WindowsProactorEventLoopPolicy are not defined.)

#include "asyncio.hpp"

namespace neos::python_runtime::detail::asyncio
{
    void make_windows_events()
    {
#if defined(_WIN32)
        static module_writer const w{ "asyncio.windows_events", [](char const* aName) -> char const*
        {
            if (std::string_view{ aName }.empty())
                return "Selector and proactor event loops for Windows.";
            if (std::string_view{ aName } == "_WindowsSelectorEventLoop")
                return "Windows version of selector event loop.";
            return nullptr;
        } };
        define("windows_events", "__all__", py_tuple({ S("SelectorEventLoop"), S("DefaultEventLoopPolicy"), S("WindowsSelectorEventLoopPolicy"), S("EventLoop") }));
        py const selectorLoop = w.make_class("_WindowsSelectorEventLoop", { G<"selector_events.BaseSelectorEventLoop">() }, {});
        define("windows_events", "_WindowsSelectorEventLoop", selectorLoop);
        define("windows_events", "SelectorEventLoop", selectorLoop);
        py const policy = w.make_class("WindowsSelectorEventLoopPolicy", { G<"events.BaseDefaultEventLoopPolicy">() }, { { "_loop_factory", selectorLoop } });
        define("windows_events", "WindowsSelectorEventLoopPolicy", policy);
        define("windows_events", "DefaultEventLoopPolicy", policy);
        define("windows_events", "EventLoop", selectorLoop);
#endif
    }
}
