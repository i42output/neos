// The test harness's runtime (NEOS_ADA_TEST_HARNESS): the standard library's primitives (lib_*.cpp) linked into a program that
// another Ada compiler compiled (an address is a pointer: memory() is null)

#include <cstdio>
#include "ada.hpp"

namespace neos::ada_runtime
{
    neos_rt_host const* host() { return nullptr; }
    std::uint8_t* memory() { return nullptr; }
    function_table::function_table(std::initializer_list<neos_rt_function>) {}
    std::vector<neos_rt_function>& functions() { static std::vector<neos_rt_function> s; return s; }
    std::size_t std_write(int aStream, char const* aData, std::size_t aCount) { return std::fwrite(aData, 1u, aCount, aStream == 2 ? stderr : stdout); }
    std::size_t std_read(int, char* aBuffer, std::size_t aCount) { return std::fread(aBuffer, 1u, aCount, stdin); }
    int std_getc(int) { return std::fgetc(stdin); }
    int std_ungetc(int aCharacter, int) { return std::ungetc(aCharacter, stdin); }
    int std_flush(int aStream) { return std::fflush(aStream == 2 ? stderr : stdout); }
    address allocate(std::uint64_t aSize) { return reinterpret_cast<address>(::operator new(aSize)); }
    void deallocate(address aAddress) { ::operator delete(reinterpret_cast<void*>(aAddress)); }
    address allocate_segment(std::uint64_t aSize) { return allocate(aSize); }
    void deallocate_segment(address aAddress, std::uint64_t) { deallocate(aAddress); }
}
