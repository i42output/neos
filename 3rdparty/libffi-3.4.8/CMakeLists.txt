# CMake build of libffi 3.4.8 (a static library), for builds that have no libffi of their own (e.g. Windows):
# - Windows x64 and x86 with MSVC (or clang-cl): the Intel syntax assembly (win64_intel.S, sysv_intel.S) is preprocessed
#   by the compiler and assembled by MASM (ml64, ml)
# - x86-64 and AArch64 with GCC or Clang (Linux, macOS, MinGW)
# The headers that configure would generate (ffi.h, ffitarget.h, fficonfig.h) are generated from their templates in the
# build tree. Users of the library (target ffi, alias libffi::libffi) see FFI_STATIC_BUILD (no dllimport declarations).

cmake_minimum_required(VERSION 3.20)
project(libffi VERSION 3.4.8 LANGUAGES C)

# --- the target (as configure.host)

if(MSVC)
    set(FFI_ARCHITECTURE "${CMAKE_C_COMPILER_ARCHITECTURE_ID}")
else()
    set(FFI_ARCHITECTURE "${CMAKE_SYSTEM_PROCESSOR}")
endif()
string(TOLOWER "${FFI_ARCHITECTURE}" FFI_ARCHITECTURE)

set(FFI_SOURCES src/prep_cif.c src/types.c src/raw_api.c src/java_raw_api.c src/closures.c src/tramp.c)
if(MSVC AND FFI_ARCHITECTURE MATCHES "^(x64|amd64|x86_64)$")
    set(FFI_TARGET X86_WIN64)
    set(FFI_TARGETDIR x86)
    list(APPEND FFI_SOURCES src/x86/ffiw64.c)
    set(FFI_MASM_SOURCE src/x86/win64_intel.S)
elseif(MSVC AND FFI_ARCHITECTURE MATCHES "^(x86|i[3-6]86)$")
    set(FFI_TARGET X86_WIN32)
    set(FFI_TARGETDIR x86)
    list(APPEND FFI_SOURCES src/x86/ffi.c)
    set(FFI_MASM_SOURCE src/x86/sysv_intel.S)
elseif(NOT MSVC AND FFI_ARCHITECTURE MATCHES "^(x86_64|amd64)$")
    set(FFI_TARGETDIR x86)
    if(WIN32)
        set(FFI_TARGET X86_WIN64)
        list(APPEND FFI_SOURCES src/x86/ffiw64.c src/x86/win64.S)
    else()
        set(FFI_TARGET X86_64)
        list(APPEND FFI_SOURCES src/x86/ffi64.c src/x86/unix64.S src/x86/ffiw64.c src/x86/win64.S)
    endif()
elseif(NOT MSVC AND NOT WIN32 AND FFI_ARCHITECTURE MATCHES "^(aarch64|arm64)$")
    set(FFI_TARGET AARCH64)
    set(FFI_TARGETDIR aarch64)
    list(APPEND FFI_SOURCES src/aarch64/ffi.c src/aarch64/sysv.S)
else()
    message(FATAL_ERROR "libffi: no CMake build for ${FFI_ARCHITECTURE} with ${CMAKE_C_COMPILER_ID}")
endif()

# --- the generated headers

set(FFI_GENERATED "${CMAKE_CURRENT_BINARY_DIR}/include")
if(MSVC)
    set(FFI_LONG_DOUBLE_SIZE 8)
elseif(FFI_TARGET STREQUAL "X86_WIN64")
    set(FFI_LONG_DOUBLE_SIZE 16)
else()
    include(CheckTypeSize)
    check_type_size("long double" FFI_LONG_DOUBLE_SIZE BUILTIN_TYPES_ONLY LANGUAGE C)
endif()

# ffi.h (its @VARIABLES@)
set(TARGET ${FFI_TARGET})
set(VERSION ${PROJECT_VERSION})
set(FFI_EXEC_TRAMPOLINE_TABLE 0)
if(FFI_LONG_DOUBLE_SIZE GREATER 8)
    set(HAVE_LONG_DOUBLE 1)
else()
    set(HAVE_LONG_DOUBLE 0)
endif()
configure_file(include/ffi.h.in "${FFI_GENERATED}/ffi.h" @ONLY)
configure_file(src/${FFI_TARGETDIR}/ffitarget.h "${FFI_GENERATED}/ffitarget.h" COPYONLY)

# fficonfig.h (its #undef NAME lines that configure would define)
set(FFI_CONFIG
    "STDC_HEADERS 1" "HAVE_MEMCPY 1" "HAVE_STDLIB_H 1" "HAVE_STRING_H 1" "HAVE_STDINT_H 1" "HAVE_INTTYPES_H 1"
    "HAVE_SYS_TYPES_H 1" "HAVE_SYS_STAT_H 1" "HAVE_STDIO_H 1"
    "SIZEOF_DOUBLE 8" "SIZEOF_LONG_DOUBLE ${FFI_LONG_DOUBLE_SIZE}" "SIZEOF_SIZE_T ${CMAKE_SIZEOF_VOID_P}"
    "PACKAGE \"libffi\"" "PACKAGE_NAME \"libffi\"" "PACKAGE_TARNAME \"libffi\"" "PACKAGE_VERSION \"${PROJECT_VERSION}\""
    "PACKAGE_STRING \"libffi ${PROJECT_VERSION}\"" "PACKAGE_BUGREPORT \"http://github.com/libffi/libffi/issues\""
    "PACKAGE_URL \"\"" "VERSION \"${PROJECT_VERSION}\"" "EH_FRAME_FLAGS \"a\"")
if(HAVE_LONG_DOUBLE)
    list(APPEND FFI_CONFIG "HAVE_LONG_DOUBLE 1")
endif()
if(NOT MSVC)
    list(APPEND FFI_CONFIG "HAVE_AS_CFI_PSEUDO_OP 1")
    if(FFI_TARGET STREQUAL "X86_64")
        list(APPEND FFI_CONFIG "HAVE_AS_X86_PCREL 1")
    endif()
endif()
if(NOT WIN32)
    list(APPEND FFI_CONFIG "HAVE_UNISTD_H 1" "HAVE_DLFCN_H 1" "HAVE_SYS_MMAN_H 1" "HAVE_MMAP 1" "HAVE_MMAP_ANON 1"
        "HAVE_HIDDEN_VISIBILITY_ATTRIBUTE 1")
    if(NOT APPLE)
        list(APPEND FFI_CONFIG "HAVE_ALLOCA_H 1")
        if(FFI_TARGET STREQUAL "X86_64")
            list(APPEND FFI_CONFIG "HAVE_AS_X86_64_UNWIND_SECTION_TYPE 1")
        endif()
    endif()
endif()
if(FFI_TARGET STREQUAL "X86_WIN32")
    list(APPEND FFI_CONFIG "SYMBOL_UNDERSCORE 1")
endif()
file(READ fficonfig.h.in FFI_CONFIG_TEXT)
foreach(definition IN LISTS FFI_CONFIG)
    string(REGEX MATCH "^[A-Z_0-9]+" name "${definition}")
    string(REGEX REPLACE "#undef ${name}(\r?\n)" "#define ${definition}\\1" FFI_CONFIG_TEXT "${FFI_CONFIG_TEXT}")
endforeach()
file(CONFIGURE OUTPUT "${FFI_GENERATED}/fficonfig.h" CONTENT "${FFI_CONFIG_TEXT}" @ONLY)

# --- the library

set(FFI_INCLUDES "${FFI_GENERATED}" "${CMAKE_CURRENT_SOURCE_DIR}/include" "${CMAKE_CURRENT_SOURCE_DIR}/src/${FFI_TARGETDIR}")
if(FFI_MASM_SOURCE)
    # (MASM: the preprocessed assembly (cl /EP /P))
    enable_language(ASM_MASM)
    get_filename_component(name "${FFI_MASM_SOURCE}" NAME_WE)
    set(FFI_MASM_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/${name}.asm")
    set(includes)
    foreach(directory IN LISTS FFI_INCLUDES)
        list(APPEND includes "/I${directory}")
    endforeach()
    add_custom_command(OUTPUT "${FFI_MASM_OUTPUT}"
        COMMAND "${CMAKE_C_COMPILER}" /nologo /EP /P /DFFI_STATIC_BUILD ${includes} "/Fi${FFI_MASM_OUTPUT}" /TC "${CMAKE_CURRENT_SOURCE_DIR}/${FFI_MASM_SOURCE}"
        DEPENDS "${FFI_MASM_SOURCE}" "${FFI_GENERATED}/ffi.h" "${FFI_GENERATED}/fficonfig.h" "${FFI_GENERATED}/ffitarget.h"
        COMMENT "Preprocessing ${FFI_MASM_SOURCE}"
        VERBATIM)
    set_source_files_properties("${FFI_MASM_OUTPUT}" PROPERTIES LANGUAGE ASM_MASM GENERATED TRUE)
    if(FFI_TARGET STREQUAL "X86_WIN32")
        set_source_files_properties("${FFI_MASM_OUTPUT}" PROPERTIES COMPILE_OPTIONS "/safeseh")
    endif()
    list(APPEND FFI_SOURCES "${FFI_MASM_OUTPUT}")
else()
    enable_language(ASM)
endif()

add_library(ffi STATIC ${FFI_SOURCES})
add_library(libffi::libffi ALIAS ffi)
target_include_directories(ffi PUBLIC "$<BUILD_INTERFACE:${FFI_GENERATED}>" PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include" "${CMAKE_CURRENT_SOURCE_DIR}/src/${FFI_TARGETDIR}")
target_compile_definitions(ffi PUBLIC FFI_STATIC_BUILD)
set_property(TARGET ffi PROPERTY POSITION_INDEPENDENT_CODE ON)
if(MSVC)
    # (libffi.lib, the name its users' #pragma comment(lib) gives, in every configuration; the static C runtime, as neos)
    set_target_properties(ffi PROPERTIES OUTPUT_NAME libffi DEBUG_POSTFIX ""
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    target_compile_definitions(ffi PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
