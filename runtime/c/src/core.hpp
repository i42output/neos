/*
  core.hpp

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

// The C language's runtime (in the C runtime plugin): the C memory (the external memory that C programs' code uses: see 
// neos/language/c.hpp), its heap, the program's arguments and exit, and the function tables of the native parts of the C 
// standard library (lib_*.cpp)

#pragma once

#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <string>
#include <vector>
#include <neos/runtime/plugin.h>
#include <neos/language/c.hpp>

namespace neos::c_runtime
{
    using address = std::uint64_t;      ///< (an address in the C memory (an offset in its region); 0 is null)

    neos_rt_host const* host();
    std::uint8_t* memory();
    template <typename T>
    inline T* at(address aAddress) { return reinterpret_cast<T*>(memory() + aAddress); }
    inline char* str(address aAddress) { return aAddress == 0u ? nullptr : at<char>(aAddress); }
    inline address address_of(void const* aPointer) { return aPointer == nullptr ? 0u : static_cast<address>(static_cast<std::uint8_t const*>(aPointer) - memory()); }
    address thread_errno();     ///< (the calling thread's errno (the program's thread's: at layout::Errno; see lib_threads.cpp))
    inline int& errno_value() { return *at<int>(thread_errno()); }

    // the heap (allocated storage is zeroed)
    address allocate(std::uint64_t aSize);
    address allocate_aligned(std::uint64_t aAlignment, std::uint64_t aSize);
    void deallocate(address aAddress);
    address reallocate(address aAddress, std::uint64_t aSize);
    std::uint64_t allocated_size(address aAddress);
    // the memory's pages (whole granules: e.g. a thread's stack)
    address take_pages(std::uint64_t aSize);
    void give_pages(address aAddress, std::uint64_t aSize);
    address copy_string(std::string const& aText);      ///< (a new NUL terminated copy in the C memory (heap allocated))

    // the standard streams (the program's, as its host gives them (see neos_rt_host::std_write))
    std::size_t std_write(int aStream, char const* aData, std::size_t aCount);
    std::size_t std_read(int aStream, char* aBuffer, std::size_t aCount);
    int std_getc(int aStream);
    int std_ungetc(int aCharacter, int aStream);
    int std_flush(int aStream);

    // calls of the program's functions (by their descriptors' addresses: see program::descriptor_of)
    std::uint32_t function_of(address aDescriptor);
    std::int64_t call_function(address aDescriptor, std::initializer_list<std::int64_t> aArguments, neos_rt_type aResult = NEOS_RT_I32, 
        std::initializer_list<neos_rt_type> aTypes = {});
    void trap(char const* aMessage);
    long const* trap_pending();     ///< (see NEOS_RUNTIME_PLUGIN_ERROR_PENDING)
    char const* trapped();          ///< (the calling thread's trap's error (cleared), if trap was called; else null)

    // the native functions' tables (each lib_*.cpp's)
    std::vector<neos_rt_function>& functions();
    struct function_table
    {
        function_table(std::initializer_list<neos_rt_function> aFunctions);
    };
    // (a library function's description: its name ("libc." + the C name) and signature (see neos/language/c_library.hpp))
    neos_rt_function library(char const* aName, neos_rt_function_pointer aFunction);

    // the streams (lib_stdio.cpp): flushed and closed at the program's end
    void streams_end();
    void streams_init();
    // the functions that atexit (and at_quick_exit) registered
    void exit_handlers(bool aQuick);
    // threads (lib_threads.cpp): the globals of a thread's stack (see rt_init); the program ending: if threads other than the calling one
    // are running, the process ends (as a C program's does when it ends: they stop), its output flushed
    void stack_globals(unsigned& aSp, unsigned& aLimit);
    void end_threads(int aStatus);
}

#if defined(_WIN32)
#define NEOS_C_PRIMITIVE extern "C"
#else
#define NEOS_C_PRIMITIVE extern "C" __attribute__((visibility("hidden")))
#endif
