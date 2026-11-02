/*
  ada.hpp

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

// The Ada runtime plugin (neos.runtime.ada): the functions that code compiled from Ada calls (as imports named "ada::f") and
// the native primitives of the Ada standard library (languages/packages/ada), which its bodies import with
//
//     pragma Import (Neos, Name, "ada::f");
//
// The Ada memory: a region of address space that is the external memory at index 3 (see neos::language::ada::layout), which
// generated code reads and writes with 64 bit addresses (offsets in the region; 0 is null). An Ada System.Address is such an
// offset: a primitive that takes an address reads or writes the region at memory() + address.
//
// The primitives are extern "C" functions named neos_ada_<f> whose parameters and results are 64 bit integers (Interfaces.
// Integer_64 and System.Address in Ada) and doubles (Long_Float): each source file registers its own with a function_table
// (static object). The standard library's primitives can also be compiled into a test harness for the library's Ada bodies with 
// another compiler (NEOS_ADA_TEST_HARNESS: no host, and memory() is null, so that an address is a pointer).

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <vector>
#include <neos/runtime/plugin.h>

#define NEOS_ADA_PRIMITIVE extern "C"

namespace neos::ada_runtime
{
    using address = std::int64_t;

    neos_rt_host const* host();                 ///< (null in the test harness)
    std::uint8_t* memory();                     ///< (the region's base; null in the test harness)

    template <typename T>
    inline T* at(address aAddress) { return reinterpret_cast<T*>(memory() + aAddress); }
    inline char* chars(address aAddress) { return reinterpret_cast<char*>(memory() + aAddress); }

    // the runtime's functions: each source file registers its own (a static function_table)
    struct function_table
    {
        function_table(std::initializer_list<neos_rt_function> aFunctions);
    };
    std::vector<neos_rt_function>& functions();

    // the running program's standard streams (0: input, 1: output, 2: error): its host's (see neos_rt_host::std_write), else the 
    // process's
    std::size_t std_write(int aStream, char const* aData, std::size_t aCount);
    std::size_t std_read(int aStream, char* aBuffer, std::size_t aCount);
    int std_getc(int aStream);
    int std_ungetc(int aCharacter, int aStream);
    int std_flush(int aStream);

    // the memory: allocation of the region's storage (aligned to 16 bytes; 0: none)
    address allocate(std::uint64_t aSize);
    void deallocate(address aAddress);
    address allocate_segment(std::uint64_t aSize);  ///< (a large block, e.g. a task's stack (page aligned))
    void deallocate_segment(address aAddress, std::uint64_t aSize);
}
