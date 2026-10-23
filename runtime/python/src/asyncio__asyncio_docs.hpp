/*
  asyncio__asyncio_docs.hpp

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

  The docstrings of CPython 3.13's module _asyncio (Modules/_asynciomodule.c and Modules/clinic/_asynciomodule.c.h),
  generated from CPython 3.13: Copyright (c) 2001-2024 Python Software Foundation; All Rights Reserved. They are used under
  the Python Software Foundation License Version 2 (see runtime/python/LICENSE.PSF). Changes: extracted from C as C++ string
  literals for the neos Python runtime plugin (see runtime/python/NOTICE).
*/

// (generated: the module _asyncio's docstring (""), its classes' and their methods' (by qualified name), and its functions')

#pragma once

#include <cstring>

namespace neos::python_runtime::detail::asyncio__asyncio_docs
{
    struct entry { char const* name; char const* doc; };
    inline entry const sDocs[] =
    {
        { "", "Accelerator module for asyncio" },
        { "Future", "This class is *almost* compatible with concurrent.futures.Future.\n\n    Differences:\n\n    - result() and exception() do not take a timeout argument and\n      raise an exception when the future isn't done yet.\n\n    - Callbacks registered with add_done_callback() are always called\n      via the event loop's call_soon_threadsafe().\n\n    - This class is not compatible with the wait() and as_completed()\n      methods in the concurrent.futures package." },
        { "Future.result", "Return the result this future represents.\n\nIf the future has been cancelled, raises CancelledError.  If the\nfuture's result isn't yet available, raises InvalidStateError.  If\nthe future is done and has an exception set, this exception is\nraised." },
        { "Future.exception", "Return the exception that was set on this future.\n\nThe exception (or None if no exception was set) is returned only if\nthe future is done.  If the future has been cancelled, raises\nCancelledError.  If the future isn't done yet, raises\nInvalidStateError." },
        { "Future.set_result", "Mark the future done and set its result.\n\nIf the future is already done when this method is called, raises\nInvalidStateError." },
        { "Future.set_exception", "Mark the future done and set an exception.\n\nIf the future is already done when this method is called, raises\nInvalidStateError." },
        { "Future.add_done_callback", "Add a callback to be run when the future becomes done.\n\nThe callback is called with a single argument - the future object.\nIf the future is already done when this is called, the callback is\nscheduled with call_soon." },
        { "Future.remove_done_callback", "Remove all instances of a callback from the \"call when done\" list.\n\nReturns the number of callbacks removed." },
        { "Future.cancel", "Cancel the future and schedule callbacks.\n\nIf the future is already done or cancelled, return False.\nOtherwise, change the future's state to cancelled, schedule the\ncallbacks and return True." },
        { "Future.cancelled", "Return True if the future was cancelled." },
        { "Future.done", "Return True if the future is done.\n\nDone means either that a result / exception are available, or that\nthe future was cancelled." },
        { "Future.get_loop", "Return the event loop the Future is bound to." },
        { "Future._make_cancelled_error", "Create the CancelledError to raise if the Future is cancelled.\n\nThis should only be called once when handling a cancellation since\nit erases the context exception value." },
        { "Future.__class_getitem__", "Futures are generic over the type of their results" },
        { "Task", "A coroutine wrapped in a Future." },
        { "Task.result", "Return the result this future represents.\n\nIf the future has been cancelled, raises CancelledError.  If the\nfuture's result isn't yet available, raises InvalidStateError.  If\nthe future is done and has an exception set, this exception is\nraised." },
        { "Task.exception", "Return the exception that was set on this future.\n\nThe exception (or None if no exception was set) is returned only if\nthe future is done.  If the future has been cancelled, raises\nCancelledError.  If the future isn't done yet, raises\nInvalidStateError." },
        { "Task.add_done_callback", "Add a callback to be run when the future becomes done.\n\nThe callback is called with a single argument - the future object.\nIf the future is already done when this is called, the callback is\nscheduled with call_soon." },
        { "Task.remove_done_callback", "Remove all instances of a callback from the \"call when done\" list.\n\nReturns the number of callbacks removed." },
        { "Task.cancelled", "Return True if the future was cancelled." },
        { "Task.done", "Return True if the future is done.\n\nDone means either that a result / exception are available, or that\nthe future was cancelled." },
        { "Task.cancel", "Request that this task cancel itself.\n\nThis arranges for a CancelledError to be thrown into the\nwrapped coroutine on the next cycle through the event loop.\nThe coroutine then has a chance to clean up or even deny\nthe request using try/except/finally.\n\nUnlike Future.cancel, this does not guarantee that the\ntask will be cancelled: the exception might be caught and\nacted upon, delaying cancellation of the task or preventing\ncancellation completely.  The task may also return a value or\nraise a different exception.\n\nImmediately after this method is called, Task.cancelled() will\nnot return True (unless the task was already cancelled).  A\ntask will be marked as cancelled when the wrapped coroutine\nterminates with a CancelledError exception (even if cancel()\nwas not called).\n\nThis also increases the task's count of cancellation requests." },
        { "Task.cancelling", "Return the count of the task's cancellation requests.\n\nThis count is incremented when .cancel() is called\nand may be decremented using .uncancel()." },
        { "Task.uncancel", "Decrement the task's count of cancellation requests.\n\nThis should be used by tasks that catch CancelledError\nand wish to continue indefinitely until they are cancelled again.\n\nReturns the remaining number of cancellation requests." },
        { "Task.get_stack", "Return the list of stack frames for this task's coroutine.\n\nIf the coroutine is not done, this returns the stack where it is\nsuspended.  If the coroutine has completed successfully or was\ncancelled, this returns an empty list.  If the coroutine was\nterminated by an exception, this returns the list of traceback\nframes.\n\nThe frames are always ordered from oldest to newest.\n\nThe optional limit gives the maximum number of frames to\nreturn; by default all available frames are returned.  Its\nmeaning differs depending on whether a stack or a traceback is\nreturned: the newest frames of a stack are returned, but the\noldest frames of a traceback are returned.  (This matches the\nbehavior of the traceback module.)\n\nFor reasons beyond our control, only one stack frame is\nreturned for a suspended coroutine." },
        { "Task.print_stack", "Print the stack or traceback for this task's coroutine.\n\nThis produces output similar to that of the traceback module,\nfor the frames retrieved by get_stack().  The limit argument\nis passed to get_stack().  The file argument is an I/O stream\nto which the output is written; by default output is written\nto sys.stderr." },
        { "Task._make_cancelled_error", "Create the CancelledError to raise if the Task is cancelled.\n\nThis should only be called once when handling a cancellation since\nit erases the context exception value." },
        { "Task.__class_getitem__", "Tasks are generic over the return type of their wrapped coroutines" },
        { "_enter_task", "Enter into task execution or resume suspended task.\n\nTask belongs to loop.\n\nReturns None." },
        { "_get_running_loop", "Return the running event loop or None.\n\nThis is a low-level function intended to be used by event loops.\nThis function is thread-specific." },
        { "_leave_task", "Leave task execution or suspend a task.\n\nTask belongs to loop.\n\nReturns None." },
        { "_register_eager_task", "Register a new task in asyncio as executed by loop.\n\nReturns None." },
        { "_register_task", "Register a new task in asyncio as executed by loop.\n\nReturns None." },
        { "_set_running_loop", "Set the running event loop.\n\nThis is a low-level function intended to be used by event loops.\nThis function is thread-specific." },
        { "_swap_current_task", "Temporarily swap in the supplied task and return the original one (or None).\n\nThis is intended for use during eager coroutine execution." },
        { "_unregister_eager_task", "Unregister a task.\n\nReturns None." },
        { "_unregister_task", "Unregister a task.\n\nReturns None." },
        { "current_task", "Return a currently executed task." },
        { "get_event_loop", "Return an asyncio event loop.\n\nWhen called from a coroutine or a callback (e.g. scheduled with\ncall_soon or similar API), this function will always return the\nrunning event loop.\n\nIf there is no running event loop set, the function will return\nthe result of `get_event_loop_policy().get_event_loop()` call." },
        { "get_running_loop", "Return the running event loop.  Raise a RuntimeError if there is none.\n\nThis function is thread-specific." },
    };
    inline char const* doc(char const* aName)
    {
        for (auto const& e : sDocs)
            if (std::strcmp(e.name, aName) == 0)
                return e.doc;
        return nullptr;
    }
}
