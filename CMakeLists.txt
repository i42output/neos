cmake_minimum_required(VERSION 3.20)
cmake_policy(VERSION 3.20)

# meta
project(neos VERSION 0.0.1 LANGUAGES C CXX)

# (single-configuration generators (e.g. Makefiles, Ninja): Release unless a build type is given)
if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type (Debug, Release, RelWithDebInfo, MinSizeRel)" FORCE)
endif()

# deployment
IF(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
  SET(CMAKE_INSTALL_PREFIX "/usr/local" CACHE PATH "Default install prefix" FORCE)
ENDIF(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
# (dependencies installed to the same prefix are found: e.g. on Windows "/usr/local" (C:/usr/local) is not searched by default)
list(APPEND CMAKE_PREFIX_PATH "${CMAKE_INSTALL_PREFIX}")

include(GNUInstallDirs)
set(CMAKE_INSTALL_CMAKEDIR "${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME}")

# compiler support
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED YES)
set(CMAKE_CXX_EXTENSIONS NO)

if(MSVC)
    # (C and C++ options only: the bundled libffi's assembly is assembled by MASM, which rejects them)
    add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:/std:c++latest;/Zc:__cplusplus>" "$<$<COMPILE_LANGUAGE:C,CXX>:/bigobj;/utf-8>")
    add_definitions(-D_UNICODE)
    # (executables' main thread stack: 16MB, as the stacks of coroutines and generated code are at least 8MB: a parse may go deep (see the
    # code parsers' max level in src/schema.cpp))
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /STACK:16777216")
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    # (members named as the types they have (e.g. "opcode opcode;"): accepted by MSVC and Clang)
    add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-Wno-changes-meaning>)
endif()

# output: neos loads its concept libraries (.ncl) from the folder of its executable (and the folders below it) and its
# runtime plugins from that folder's "plugins" folder: they are built there
if(CMAKE_CONFIGURATION_TYPES)
    set(NEOS_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>")
else()
    set(NEOS_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
endif()
set(NEOS_PLUGIN_OUTPUT_DIRECTORY "${NEOS_OUTPUT_DIRECTORY}/plugins")

# dependencies

# neonumeric and neolib: installed (find_package) or, if their source folders are given, built here
# (an installed package's build for this build's configuration: RelWithDebInfo and MinSizeRel use its own, else its Release build, 
# never its Debug build (e.g. neolibd.lib: CMake's default is the first configuration it was installed with))
set(CMAKE_MAP_IMPORTED_CONFIG_RELWITHDEBINFO RelWithDebInfo Release MinSizeRel "")
set(CMAKE_MAP_IMPORTED_CONFIG_MINSIZEREL MinSizeRel Release RelWithDebInfo "")
set(NEOS_NEONUMERIC_SOURCE_DIR "" CACHE PATH "neonumeric source folder (else an installed neonumeric is used)")
set(NEOS_NEOLIB_SOURCE_DIR "" CACHE PATH "neolib source folder (else an installed neolib is used)")

if(NEOS_NEONUMERIC_SOURCE_DIR)
    add_subdirectory("${NEOS_NEONUMERIC_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/neonumeric")
else()
    find_package(neonumeric REQUIRED)
endif()

if(NEOS_NEOLIB_SOURCE_DIR)
    set(NEOLIB_TESTS OFF CACHE BOOL "Build neolib tests" FORCE)
    add_subdirectory("${NEOS_NEOLIB_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/neolib")
    set_property(TARGET neolib PROPERTY POSITION_INDEPENDENT_CODE ON) # (linked into the concept libraries)
    set(NEOS_NEOLIB_TARGET neolib)
else()
    find_package(neolib REQUIRED)
    set(NEOS_NEOLIB_TARGET neolib::neolib)
endif()

if(WIN32)
    set(Boost_USE_STATIC_LIBS ON)
    set(Boost_USE_STATIC_RUNTIME ON)
endif()
find_package(Boost REQUIRED COMPONENTS program_options)

# libffi: the VM calls runtime plugins' functions with it (without it neos runs scripts that do not use runtime plugins);
# if there is no libffi to be found (e.g. on Windows) the bundled one (3rdparty/libffi-3.4.8) is built (a static library)
option(NEOS_LIBFFI "Call runtime plugin functions with libffi" ON)
option(NEOS_LIBFFI_STATIC "libffi is a static library" OFF)
option(NEOS_LIBFFI_BUNDLED "Build the bundled libffi (3rdparty/libffi-3.4.8) rather than use an installed one" OFF)
if(NEOS_LIBFFI AND NOT NEOS_LIBFFI_BUNDLED)
    find_package(PkgConfig QUIET)
    if(PKG_CONFIG_FOUND)
        pkg_check_modules(PC_LIBFFI QUIET libffi)
    endif()
    find_path(LIBFFI_INCLUDE_DIR ffi.h HINTS ${PC_LIBFFI_INCLUDE_DIRS} PATH_SUFFIXES ffi)
    find_library(LIBFFI_LIBRARY NAMES ffi libffi HINTS ${PC_LIBFFI_LIBRARY_DIRS})
    if(NOT LIBFFI_INCLUDE_DIR OR NOT LIBFFI_LIBRARY)
        message(STATUS "libffi not found (LIBFFI_INCLUDE_DIR and LIBFFI_LIBRARY): the bundled libffi is built")
        set(NEOS_LIBFFI_BUNDLED ON)
    endif()
endif()
if(NEOS_LIBFFI AND NEOS_LIBFFI_BUNDLED)
    add_subdirectory(3rdparty/libffi-3.4.8 "${CMAKE_BINARY_DIR}/libffi")
endif()

# x86-simd-sort (the NumPy runtime plugin's SIMD sorts): x86-64 with GCC or Clang
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$" AND (CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID MATCHES "Clang") AND NOT MSVC)
    set(NEOS_NUMPY_XSS ON)
else()
    set(NEOS_NUMPY_XSS OFF)
endif()

# neos (library)
file(GLOB NEOS_SOURCE_FILES src/*.cpp src/api/*.cpp src/bytecode/*.cpp)
file(GLOB_RECURSE NEOS_HEADER_FILES include/neos/*.hpp include/neos/*.h src/*.hpp)
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${NEOS_SOURCE_FILES} ${NEOS_HEADER_FILES})

add_library(neos STATIC ${NEOS_SOURCE_FILES} ${NEOS_HEADER_FILES})
target_include_directories(neos PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_compile_definitions(neos PUBLIC NEOLIB_HOSTED_ENVIRONMENT)
# (where the build installs neos (its executable, concept libraries, compiler plugins and runtime plugins): neos::installed_home(), 
# for a host program that is not neos (e.g. an IDE); absolute (on Windows: with the drive, as "/usr/local" has none))
get_filename_component(NEOS_INSTALLED_HOME "${CMAKE_INSTALL_FULL_BINDIR}" ABSOLUTE)
target_compile_definitions(neos PRIVATE "NEOS_INSTALLED_HOME=\"${NEOS_INSTALLED_HOME}\"")
target_link_libraries(neos PUBLIC ${NEOS_NEOLIB_TARGET} neonumeric::neonumeric PRIVATE ${CMAKE_DL_LIBS})
set_property(TARGET neos PROPERTY POSITION_INDEPENDENT_CODE ON) # (linked into the concept libraries)
if(MSVC)
    # (its debug library's name (neosd.lib); the plugins, which neos loads by name, have none)
    set_target_properties(neos PROPERTIES DEBUG_POSTFIX "d")
endif()
set_property(TARGET neos PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
if(NEOS_LIBFFI AND NEOS_LIBFFI_BUNDLED)
    target_link_libraries(neos PRIVATE libffi::libffi)
    target_compile_definitions(neos PRIVATE NEOS_LIBFFI_STATIC "NEOS_LIBFFI_LIB=\"$<TARGET_FILE:libffi::libffi>\"")
elseif(NEOS_LIBFFI)
    target_include_directories(neos PRIVATE "${LIBFFI_INCLUDE_DIR}")
    target_link_libraries(neos PRIVATE "${LIBFFI_LIBRARY}")
    if(MSVC)
        get_filename_component(NEOS_LIBFFI_LIB_NAME "${LIBFFI_LIBRARY}" NAME)
        target_compile_definitions(neos PRIVATE NEOS_LIBFFI_LIB="${NEOS_LIBFFI_LIB_NAME}")
    endif()
    if(NEOS_LIBFFI_STATIC)
        target_compile_definitions(neos PRIVATE NEOS_LIBFFI_STATIC)
    endif()
else()
    target_compile_definitions(neos PRIVATE NEOS_NO_LIBFFI)
endif()

# (a plugin's objects but for its entry point (its plugin*.cpp) and dllmain.cpp: a library of objects (TARGET.objects) that its plugin and the 
# compiler stubs (see add_neos_compiler_stub) link; NEOS_PLUGIN_SOURCES: the plugin's own sources (its entry point and dllmain.cpp))
function(add_neos_plugin_objects TARGET)
    set(objects)
    set(own)
    set(entry)
    foreach(source ${ARGN})
        if(source MATCHES "dllmain\\.cpp$")
            list(APPEND own "${source}")
        elseif(source MATCHES "/plugin[^/]*\\.cpp$")
            list(APPEND own "${source}")
            set(entry "${CMAKE_CURRENT_SOURCE_DIR}/${source}")
        else()
            list(APPEND objects "${source}")
        endif()
    endforeach()
    add_library(${TARGET}.objects OBJECT ${objects})
    target_link_libraries(${TARGET}.objects PRIVATE neos)
    target_compile_definitions(${TARGET}.objects PRIVATE CORE_EXPORTS)
    set_target_properties(${TARGET}.objects PROPERTIES POSITION_INDEPENDENT_CODE ON NEOS_PLUGIN_ENTRY "${entry}"
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    set(NEOS_PLUGIN_SOURCES ${own} PARENT_SCOPE)
endfunction()

# (a plugin's own functions are bound to it when it is linked (ELF): loading it then resolves only the symbols that it imports, not
# the many thousands that it defines (it has its own copy of neos and neolib), which made each program's start slow)
function(neos_plugin_link_options TARGET)
    if(NOT MSVC AND NOT APPLE)
        target_link_options(${TARGET} PRIVATE "LINKER:-Bsymbolic-functions")
    endif()
endfunction()

# concept libraries (plugins with the extension ".ncl")
function(add_neos_concept_library TARGET)
    add_neos_plugin_objects(${TARGET} ${ARGN})
    add_library(${TARGET} MODULE ${NEOS_PLUGIN_SOURCES} $<TARGET_OBJECTS:${TARGET}.objects>)
    target_link_libraries(${TARGET} PRIVATE neos)
    target_compile_definitions(${TARGET} PRIVATE CORE_EXPORTS)
    set_target_properties(${TARGET} PROPERTIES PREFIX "" SUFFIX ".ncl"
        LIBRARY_OUTPUT_DIRECTORY "${NEOS_OUTPUT_DIRECTORY}"
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    neos_plugin_link_options(${TARGET})
    install(TARGETS ${TARGET} LIBRARY DESTINATION "${CMAKE_INSTALL_BINDIR}")
endfunction()

add_neos_concept_library(core
    concepts/src/common/dllmain.cpp
    concepts/src/core/plugin.cpp
    concepts/src/core/bitwise.cpp
    concepts/src/core/boolean.cpp
    concepts/src/core/core.cpp
    concepts/src/core/language.cpp
    concepts/src/core/logic.cpp
    concepts/src/core/math.cpp
    concepts/src/core/module.cpp
    concepts/src/core/object.cpp
    concepts/src/core/string.cpp)
add_neos_concept_library(core.math.universal
    concepts/src/common/dllmain.cpp
    concepts/src/core/plugin.math.universal.cpp
    concepts/src/core/math.universal.cpp)
# (the concepts of a language: a library of its own)
add_neos_concept_library(python.core
    concepts/src/common/dllmain.cpp
    concepts/src/python/plugin.cpp
    concepts/src/python/python.cpp)
target_include_directories(python.core PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/compilers/python/include") # (the language's constructs)
target_include_directories(python.core.objects PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/compilers/python/include")
add_neos_concept_library(neoscript.core
    concepts/src/common/dllmain.cpp
    concepts/src/neoscript/plugin.cpp
    concepts/src/neoscript/neoscript.cpp)
target_include_directories(neoscript.core PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/compilers/neoscript/include")
target_include_directories(neoscript.core.objects PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/compilers/neoscript/include")
add_neos_concept_library(javascript.core
    concepts/src/common/dllmain.cpp
    concepts/src/javascript/plugin.cpp
    concepts/src/javascript/javascript.cpp)
target_include_directories(javascript.core PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/compilers/javascript/include")
target_include_directories(javascript.core.objects PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/compilers/javascript/include")
add_neos_concept_library(lua.core
    concepts/src/common/dllmain.cpp
    concepts/src/lua/plugin.cpp
    concepts/src/lua/lua.cpp)
target_include_directories(lua.core PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/compilers/lua/include")
target_include_directories(lua.core.objects PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/compilers/lua/include")

# compiler plugins (plugins with the extension ".ncp"): the code generation of a language (i_language_compiler)
function(add_neos_compiler_plugin TARGET LANGUAGE)
    add_neos_plugin_objects(${TARGET} concepts/src/common/dllmain.cpp ${ARGN})
    target_include_directories(${TARGET}.objects PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/compilers/${LANGUAGE}/include")
    add_library(${TARGET} MODULE ${NEOS_PLUGIN_SOURCES} $<TARGET_OBJECTS:${TARGET}.objects>)
    target_include_directories(${TARGET} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/compilers/${LANGUAGE}/include")
    target_link_libraries(${TARGET} PRIVATE neos)
    target_compile_definitions(${TARGET} PRIVATE CORE_EXPORTS)
    set_target_properties(${TARGET} PROPERTIES PREFIX "" OUTPUT_NAME "${LANGUAGE}" SUFFIX ".ncp"
        LIBRARY_OUTPUT_DIRECTORY "${NEOS_OUTPUT_DIRECTORY}"
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    neos_plugin_link_options(${TARGET})
    install(TARGETS ${TARGET} LIBRARY DESTINATION "${CMAKE_INSTALL_BINDIR}")
endfunction()
add_neos_compiler_plugin(compiler.javascript javascript
    compilers/javascript/src/plugin.cpp
    compilers/javascript/src/compiler.cpp
    compilers/javascript/src/codegen.cpp
    compilers/javascript/src/early_errors.cpp
    runtime/javascript/src/regexp_engine.cpp
    runtime/javascript/src/unicode.cpp
    runtime/javascript/src/unicode_data.cpp)
# (the compiler checks regular expression literals' patterns with the runtime's regular expression parser (and Unicode data))
target_include_directories(compiler.javascript.objects PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/runtime/javascript/src")
target_compile_definitions(compiler.javascript.objects PRIVATE NEOS_JAVASCRIPT_COMPILER)
add_neos_compiler_plugin(compiler.neoscript neoscript
    compilers/neoscript/src/plugin.cpp
    compilers/neoscript/src/compiler.cpp)
add_neos_compiler_plugin(compiler.lua lua
    compilers/lua/src/plugin.cpp
    compilers/lua/src/compiler.cpp
    compilers/lua/src/codegen.cpp)
add_neos_compiler_plugin(compiler.python python
    compilers/python/src/plugin.cpp
    compilers/python/src/compiler.cpp
    compilers/python/src/closures.cpp
    compilers/python/src/statements.cpp
    compilers/python/src/syntax.cpp)

# console (the neos executable)
add_executable(neos_console console/src/console.cpp)
target_link_libraries(neos_console PRIVATE neos Boost::program_options)
set_target_properties(neos_console PROPERTIES OUTPUT_NAME neos
    RUNTIME_OUTPUT_DIRECTORY "${NEOS_OUTPUT_DIRECTORY}"
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
add_dependencies(neos_console core core.math.universal python.core javascript.core neoscript.core lua.core compiler.javascript compiler.python compiler.neoscript compiler.lua)

# runtime stub (neos.stub): an image (an executable made by the console's "img" command) is a copy of it followed by a
# program (see neos/image.hpp); it is built beside the console, which finds it there
add_executable(neos_stub stub/src/stub.cpp)
target_link_libraries(neos_stub PRIVATE neos)
set_target_properties(neos_stub PROPERTIES OUTPUT_NAME neos.stub
    RUNTIME_OUTPUT_DIRECTORY "${NEOS_OUTPUT_DIRECTORY}"
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
add_dependencies(neos_console neos_stub)

# compiler stubs (neos.<language>.compiler.stub): the stub of an image of a program that compiles source while it runs (e.g. Python's exec), 
# with the language's compiler built in: its concept libraries and compiler plugin (the plugins' targets: ARGN) linked in, and its schema 
# and packages as resources (see stub/src/compiler_stub.hpp)
function(neos_write_if_changed FILE CONTENT)
    if(EXISTS "${FILE}")
        file(READ "${FILE}" existing)
        if(existing STREQUAL CONTENT)
            return()
        endif()
    endif()
    file(WRITE "${FILE}" "${CONTENT}")
endfunction()
function(add_neos_compiler_stub LANGUAGE)
    set(generated "${CMAKE_CURRENT_BINARY_DIR}/stub/${LANGUAGE}")
    set(sources stub/src/compiler_stub.cpp)
    set(declarations "")
    set(entries "")
    set(index 0)
    foreach(plugin ${ARGN})
        get_target_property(entry ${plugin}.objects NEOS_PLUGIN_ENTRY)
        neos_write_if_changed("${generated}/plugin${index}.cpp" "#define NEOS_PLUGIN_ENTRY_POINT neos_static_plugin_${index}\n#include \"${entry}\"\n")
        list(APPEND sources "${generated}/plugin${index}.cpp" $<TARGET_OBJECTS:${plugin}.objects>)
        string(APPEND declarations "void neos_static_plugin_${index}(neolib::i_application&, const neolib::i_string&, neolib::i_ref_ptr<neolib::i_plugin>&);\n")
        string(APPEND entries "    neos_static_plugin_${index},\n")
        math(EXPR index "${index} + 1")
    endforeach()
    # (the schema and the packages)
    set(schema "${CMAKE_CURRENT_SOURCE_DIR}/languages/${LANGUAGE}.neos")
    file(GLOB_RECURSE packages CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/languages/packages/${LANGUAGE}/*")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${schema}")
    set(data "")
    set(table "")
    set(count 0)
    foreach(file "${schema}" ${packages})
        if(file STREQUAL schema)
            set(name "${LANGUAGE}.neos")
        else()
            file(RELATIVE_PATH relative "${CMAKE_CURRENT_SOURCE_DIR}/languages" "${file}")
            set(name "${relative}")
        endif()
        file(READ "${file}" hex HEX)
        string(LENGTH "${hex}" length)
        math(EXPR size "${length} / 2")
        string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${hex}")
        string(APPEND data "static unsigned char const sResource${count}[] = { ${bytes} 0 };\n")
        string(APPEND table "    { \"${name}\", sResource${count}, ${size}u },\n")
        math(EXPR count "${count} + 1")
    endforeach()
    neos_write_if_changed("${generated}/resources.cpp"
"// (generated by CMake: the ${LANGUAGE} compiler stub's built-in plugins and resources)
#include \"${CMAKE_CURRENT_SOURCE_DIR}/stub/src/compiler_stub.hpp\"
${declarations}
namespace neos::stub
{
    char const* const language = \"${LANGUAGE}\";
    plugin_entry_point const plugins[] =
    {
${entries}    };
    std::size_t const pluginCount = ${index}u;
${data}
    resource const resources[] =
    {
${table}    };
    std::size_t const resourceCount = ${count}u;
}
")
    set(target neos_${LANGUAGE}_compiler_stub)
    add_executable(${target} ${sources} "${generated}/resources.cpp")
    target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/compilers/${LANGUAGE}/include")
    target_link_libraries(${target} PRIVATE neos)
    target_compile_definitions(${target} PRIVATE CORE_EXPORTS)
    set_target_properties(${target} PROPERTIES OUTPUT_NAME neos.${LANGUAGE}.compiler.stub
        RUNTIME_OUTPUT_DIRECTORY "${NEOS_OUTPUT_DIRECTORY}"
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    add_dependencies(neos_console ${target})
    install(TARGETS ${target} RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
endfunction()
add_neos_compiler_stub(python core core.math.universal python.core compiler.python)
add_neos_compiler_stub(javascript core core.math.universal javascript.core compiler.javascript)
add_neos_compiler_stub(lua core core.math.universal lua.core compiler.lua)

# runtime plugins (C ABI: include/neos/runtime/plugin.h)
function(add_neos_runtime_plugin TARGET)
    add_library(${TARGET} MODULE ${ARGN})
    target_include_directories(${TARGET} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
    set_target_properties(${TARGET} PROPERTIES PREFIX "" OUTPUT_NAME "neos.runtime.${TARGET}" DEBUG_POSTFIX "" # (the name neos loads)
        C_VISIBILITY_PRESET hidden CXX_VISIBILITY_PRESET hidden
        LIBRARY_OUTPUT_DIRECTORY "${NEOS_PLUGIN_OUTPUT_DIRECTORY}"
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    neos_plugin_link_options(${TARGET})
    if(MSVC)
        target_compile_definitions(${TARGET} PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()
    add_dependencies(neos_console ${TARGET})
    install(TARGETS ${TARGET} LIBRARY DESTINATION "${CMAKE_INSTALL_BINDIR}/plugins")
endfunction()

add_neos_runtime_plugin(c runtime/c/src/c.c)

# Lua runtime: Lua's values, tables, metatables, coroutines and standard library
add_neos_runtime_plugin(lua
    runtime/lua/src/plugin.cpp
    runtime/lua/src/api.cpp
    runtime/lua/src/call.cpp
    runtime/lua/src/gc.cpp
    runtime/lua/src/ops.cpp
    runtime/lua/src/table.cpp
    runtime/lua/src/value.cpp
    runtime/lua/src/lib_base.cpp
    runtime/lua/src/lib_coroutine.cpp
    runtime/lua/src/lib_package.cpp
    runtime/lua/src/lib_string.cpp
    runtime/lua/src/lib_table.cpp
    runtime/lua/src/lib_math.cpp
    runtime/lua/src/lib_io.cpp
    runtime/lua/src/lib_os.cpp
    runtime/lua/src/lib_utf8.cpp
    runtime/lua/src/lib_debug.cpp)
target_include_directories(lua PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/compilers/lua/include") # (Lua's values)

add_neos_runtime_plugin(javascript
    runtime/javascript/src/javascript.cpp
    runtime/javascript/src/api.cpp
    runtime/javascript/src/heap.cpp
    runtime/javascript/src/gc.cpp
    runtime/javascript/src/object.cpp
    runtime/javascript/src/operations.cpp
    runtime/javascript/src/operators.cpp
    runtime/javascript/src/functions.cpp
    runtime/javascript/src/realm.cpp
    runtime/javascript/src/console.cpp
    runtime/javascript/src/builtins_object.cpp
    runtime/javascript/src/builtins_error.cpp
    runtime/javascript/src/builtins_basic.cpp
    runtime/javascript/src/builtins_array.cpp
    runtime/javascript/src/numbers.cpp
    runtime/javascript/src/builtins_string.cpp
    runtime/javascript/src/builtins_regexp.cpp
    runtime/javascript/src/regexp_engine.cpp
    runtime/javascript/src/builtins_date.cpp
    runtime/javascript/src/temporal_abstract.cpp
    runtime/javascript/src/temporal_parse.cpp
    runtime/javascript/src/temporal_timezone.cpp
    runtime/javascript/src/temporal_objects.cpp
    runtime/javascript/src/temporal_plain.cpp
    runtime/javascript/src/temporal_zoned.cpp
    runtime/javascript/src/temporal_calendar.cpp
    runtime/javascript/src/builtins_json.cpp
    runtime/javascript/src/builtins_iterator.cpp
    runtime/javascript/src/builtins_disposable.cpp
    runtime/javascript/src/unicode.cpp
    runtime/javascript/src/unicode_data.cpp
    runtime/javascript/src/host262.cpp
    runtime/javascript/src/builtins_buffers.cpp
    runtime/javascript/src/builtins_typed_arrays.cpp
    runtime/javascript/src/builtins_atomics.cpp
    runtime/javascript/src/builtins_collections.cpp
    runtime/javascript/src/builtins_reflect.cpp
    runtime/javascript/src/proxy.cpp
    runtime/javascript/src/promise.cpp
    runtime/javascript/src/generators.cpp
    runtime/javascript/src/classes.cpp
    runtime/javascript/src/eval.cpp
    runtime/javascript/src/modules.cpp
    runtime/javascript/src/builtins_async.cpp
    runtime/javascript/src/bigint.cpp
    runtime/javascript/src/stubs.cpp
    runtime/javascript/src/intl.cpp
    runtime/javascript/src/intl_locale.cpp
    runtime/javascript/src/intl_collator.cpp
    runtime/javascript/src/intl_number_format.cpp
    runtime/javascript/src/intl_date_time_format.cpp
    runtime/javascript/src/intl_relative_time_format.cpp
    runtime/javascript/src/intl_list_format.cpp
    runtime/javascript/src/intl_display_names.cpp
    runtime/javascript/src/intl_segmenter.cpp
    runtime/javascript/src/intl_duration_format.cpp)
target_include_directories(javascript PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/compilers/javascript/include") # (ECMAScript's conversions)
# (its thread_locals (e.g. the calling thread's isolate, read all the time) through TLS descriptors: an access is not a call of
# __tls_get_addr (x86-64 ELF))
if(NOT WIN32 AND CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|amd64" AND (CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID MATCHES "Clang"))
    target_compile_options(javascript PRIVATE -mtls-dialect=gnu2)
endif()

# ICU (Intl, ECMA-402): its C API (on Windows the system's ICU (icu.dll: the Windows SDK's icu.h); elsewhere the system's libicu); without
# it the JavaScript runtime has no Intl
option(NEOS_ICU "JavaScript's Intl with ICU" ON)
if(NEOS_ICU)
    if(WIN32)
        include(CheckIncludeFileCXX)
        check_include_file_cxx(icu.h NEOS_HAVE_WINDOWS_ICU)
        if(NEOS_HAVE_WINDOWS_ICU)
            target_compile_definitions(javascript PRIVATE NEOS_JS_ICU NEOS_JS_WINDOWS_ICU)
            # (icu.dll delay-loaded: an older Windows' icu.dll without every function used means no Intl, not a plugin that cannot load)
            target_link_libraries(javascript PRIVATE icu delayimp)
            target_link_options(javascript PRIVATE /DELAYLOAD:icu.dll)
        else()
            message(STATUS "ICU (the Windows SDK's icu.h) not found: JavaScript has no Intl")
        endif()
    else()
        find_package(ICU COMPONENTS uc i18n data)
        if(ICU_FOUND AND ICU_VERSION VERSION_LESS 67)
            message(STATUS "ICU ${ICU_VERSION} is older than ICU 67: JavaScript has no Intl")
        elseif(ICU_FOUND)
            target_compile_definitions(javascript PRIVATE NEOS_JS_ICU)
            target_link_libraries(javascript PRIVATE ICU::i18n ICU::uc ICU::data)
        else()
            message(STATUS "ICU not found: JavaScript has no Intl")
        endif()
    endif()
endif()

# zlib (the Python runtime plugin's module zlib): the bundled source (3rdparty/zlib-1.3.1), compiled into the plugin (its names
# prefixed (Z_PREFIX): they are not those of any other zlib the process has)
set(NEOS_ZLIB_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/zlib-1.3.1")
add_library(neos_zlib OBJECT
    ${NEOS_ZLIB_DIRECTORY}/adler32.c ${NEOS_ZLIB_DIRECTORY}/compress.c ${NEOS_ZLIB_DIRECTORY}/crc32.c ${NEOS_ZLIB_DIRECTORY}/deflate.c
    ${NEOS_ZLIB_DIRECTORY}/infback.c ${NEOS_ZLIB_DIRECTORY}/inffast.c ${NEOS_ZLIB_DIRECTORY}/inflate.c ${NEOS_ZLIB_DIRECTORY}/inftrees.c
    ${NEOS_ZLIB_DIRECTORY}/trees.c ${NEOS_ZLIB_DIRECTORY}/uncompr.c ${NEOS_ZLIB_DIRECTORY}/zutil.c)
target_compile_definitions(neos_zlib PUBLIC Z_PREFIX)
set_target_properties(neos_zlib PROPERTIES POSITION_INDEPENDENT_CODE ON C_VISIBILITY_PRESET hidden
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
if(MSVC)
    target_compile_definitions(neos_zlib PRIVATE _CRT_SECURE_NO_DEPRECATE _CRT_NONSTDC_NO_DEPRECATE)
endif()

# Expat (the Python runtime plugin's module pyexpat): the bundled source (3rdparty/expat-2.8.5, as CPython 3.13 bundles it),
# compiled into the plugin (its names prefixed (pyexpatns.h's PyExpat_): they are not those of any other expat the process has)
set(NEOS_EXPAT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/expat-2.8.5")
add_library(neos_expat OBJECT ${NEOS_EXPAT_DIRECTORY}/xmlparse.c ${NEOS_EXPAT_DIRECTORY}/xmlrole.c ${NEOS_EXPAT_DIRECTORY}/xmltok.c)
target_compile_definitions(neos_expat PUBLIC XML_STATIC)
set_target_properties(neos_expat PROPERTIES POSITION_INDEPENDENT_CODE ON C_VISIBILITY_PRESET hidden
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
if(MSVC)
    target_compile_definitions(neos_expat PRIVATE _CRT_SECURE_NO_DEPRECATE _CRT_NONSTDC_NO_DEPRECATE)
endif()

# (the Python runtime plugin: its core objects and the modules of Python's standard library that it implements)
file(GLOB NEOS_PYTHON_SOURCE_FILES runtime/python/src/*.cpp)
add_neos_runtime_plugin(python ${NEOS_PYTHON_SOURCE_FILES} $<TARGET_OBJECTS:neos_zlib> $<TARGET_OBJECTS:neos_expat>)
target_include_directories(python PRIVATE "${NEOS_ZLIB_DIRECTORY}")
target_include_directories(python PRIVATE "${NEOS_EXPAT_DIRECTORY}")
target_compile_definitions(python PRIVATE XML_STATIC)
target_compile_definitions(python PRIVATE Z_PREFIX)
target_include_directories(python PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/runtime/python/include")
target_include_directories(python PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/compilers/python/include") # (the names of Unicode's characters)
target_link_libraries(python PRIVATE neonumeric::neonumeric) # (ints beyond 64 bits)
if(WIN32)
    target_link_libraries(python PRIVATE ws2_32) # (Winsock: the modules select and socket)
endif()
# (Visual Studio: compiled by clang-cl (the ClangCL toolset: Visual Studio's "C++ Clang tools for Windows" component), as cl.exe 
# (14.51) crashes compiling some of its modules (e.g. dataclasses.cpp and inspect.cpp); clang-cl takes cl.exe's options, so the 
# options CMake gives it as MSVC's apply)
if(CMAKE_GENERATOR MATCHES "^Visual Studio")
    set_target_properties(python PROPERTIES VS_PLATFORM_TOOLSET ClangCL)
endif()
# (its thread-locals (dynamic: it is loaded by dlopen) found through TLS descriptors: cheaper than calls of __tls_get_addr, and 
# every call of its functions uses some)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
    include(CheckCXXCompilerFlag)
    check_cxx_compiler_flag(-mtls-dialect=gnu2 NEOS_HAVE_TLS_DESCRIPTORS)
    if(NEOS_HAVE_TLS_DESCRIPTORS)
        target_compile_options(python PRIVATE -mtls-dialect=gnu2)
    endif()
endif()

# OpenBLAS (the NumPy runtime plugin's BLAS): the bundled source (3rdparty/OpenBLAS-0.3.34) built (a static library) as
# NumPy's (scipy-openblas) is: 64-bit integers, kernels for the same CPUs chosen at run time, threads, LAPACK from C (no
# Fortran); with MSVC it is compiled by clang-cl (Visual Studio's "C++ Clang tools for Windows" component): cl.exe cannot
# compile OpenBLAS's optimised kernels (GNU assembly) and the generic ones do not give NumPy's results
include(ExternalProject)
include(ProcessorCount)
find_package(Threads REQUIRED)
ProcessorCount(NEOS_OPENBLAS_JOBS)
if(NEOS_OPENBLAS_JOBS EQUAL 0)
    set(NEOS_OPENBLAS_JOBS 1)
endif()
set(NEOS_OPENBLAS_DIR "${CMAKE_BINARY_DIR}/openblas")
set(NEOS_OPENBLAS_LIBRARY "${NEOS_OPENBLAS_DIR}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}openblas_64${CMAKE_STATIC_LIBRARY_SUFFIX}")
set(NEOS_OPENBLAS_ARGS
    -DCMAKE_BUILD_TYPE=Release "-DCMAKE_INSTALL_PREFIX=${NEOS_OPENBLAS_DIR}" -DCMAKE_INSTALL_LIBDIR=lib -DCMAKE_POSITION_INDEPENDENT_CODE=ON
    -DBUILD_STATIC_LIBS=ON -DBUILD_SHARED_LIBS=OFF -DINTERFACE64=1 -DUSE_THREAD=1 -DUSE_OPENMP=0 -DNO_AFFINITY=1 -DBUFFERSIZE=20
    -DNOFORTRAN=ON -DC_LAPACK=ON -DBUILD_WITHOUT_LAPACKE=ON -DBUILD_TESTING=OFF)
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$")
    # (PRESCOTT, the target, is added to the list)
    list(APPEND NEOS_OPENBLAS_ARGS -DDYNAMIC_ARCH=ON -DTARGET=PRESCOTT "-DDYNAMIC_LIST=NEHALEM|SANDYBRIDGE|HASWELL|SKYLAKEX")
endif()
# (each step of OpenBLAS's build is a script (cmake -P) that runs its command, in the environment it sets: no environment
# or list values on the build's command lines)
set(NEOS_OPENBLAS_STEP_ENV "")
if(MSVC)
    # (clang-cl, Ninja and vcvars64.bat (the environment of Visual Studio's x64 command prompt) are found from cl.exe's
    # folder; the static C runtime, not named in the library's objects (/Zl, Release only: CMake's and OpenBLAS's test and
    # helper programs (Debug) are linked with it): the plugin's (/MT or /MTd) is linked)
    get_filename_component(NEOS_MSVC_BIN_DIR "${CMAKE_C_COMPILER}" DIRECTORY)
    get_filename_component(NEOS_VC_DIR "${NEOS_MSVC_BIN_DIR}/../../../../../.." ABSOLUTE)
    find_program(NEOS_CLANG_CL clang-cl HINTS "${NEOS_VC_DIR}/Tools/Llvm/x64/bin" "$ENV{ProgramFiles}/LLVM/bin")
    find_program(NEOS_NINJA ninja HINTS "${NEOS_VC_DIR}/../Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja")
    find_file(NEOS_VCVARS64 vcvars64.bat HINTS "${NEOS_VC_DIR}/Auxiliary/Build" NO_DEFAULT_PATH)
    if(NOT NEOS_CLANG_CL)
        message(FATAL_ERROR "clang-cl not found (OpenBLAS is compiled by clang-cl): install Visual Studio's \"C++ Clang tools for Windows\" component")
    endif()
    if(NOT NEOS_NINJA OR NOT NEOS_VCVARS64)
        message(FATAL_ERROR "Ninja or vcvars64.bat not found (OpenBLAS is built by Ninja in Visual Studio's x64 environment)")
    endif()
    file(WRITE "${CMAKE_BINARY_DIR}/openblas-vcvars.bat" "@call \"${NEOS_VCVARS64}\" >nul\r\n@set\r\n")
    execute_process(COMMAND "${CMAKE_BINARY_DIR}/openblas-vcvars.bat" OUTPUT_VARIABLE NEOS_VCVARS_ENV)
    foreach(NEOS_VCVAR INCLUDE LIB LIBPATH [Pp][Aa][Tt][Hh])
        if(NEOS_VCVARS_ENV MATCHES "(^|\n)(${NEOS_VCVAR})=([^\r\n]*)")
            string(APPEND NEOS_OPENBLAS_STEP_ENV "set(ENV{${CMAKE_MATCH_2}} [==[${CMAKE_MATCH_3}]==])\n")
        endif()
    endforeach()
    # (clang's C errors that OpenBLAS's CMake makes warnings for GCC only (e.g. its SkylakeX kernels' _mm_prefetch, which
    # winnt.h declares with a char pointer): warnings, by clang-cl's CL environment variable (prepended to its command
    # lines (_CL_'s are appended, after "--"): the build's commands are unchanged)
    string(APPEND NEOS_OPENBLAS_STEP_ENV "set(ENV{CL} [==[-Wno-error=incompatible-pointer-types -Wno-error=int-conversion]==])\n")
    if(NOT NEOS_OPENBLAS_STEP_ENV MATCHES "ENV{INCLUDE}")
        message(FATAL_ERROR "vcvars64.bat (${NEOS_VCVARS64}) gave no environment:\n${NEOS_VCVARS_ENV}")
    endif()
    list(APPEND NEOS_OPENBLAS_ARGS -G Ninja "-DCMAKE_MAKE_PROGRAM=${NEOS_NINJA}" "-DCMAKE_C_COMPILER=${NEOS_CLANG_CL}" "-DCMAKE_CXX_COMPILER=${NEOS_CLANG_CL}"
        -DCMAKE_MT=mt -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded -DCMAKE_C_FLAGS_RELEASE_INIT=/Zl -DNUM_THREADS=24 -DCONSISTENT_FPCSR=1)
    # (clang's run-time helpers, which clang-cl's objects may call)
    file(GLOB NEOS_CLANG_BUILTINS "${NEOS_VC_DIR}/Tools/Llvm/x64/lib/clang/*/lib/windows/clang_rt.builtins-x86_64.lib")
else()
    # (-O2, as OpenBLAS's makefiles (NumPy's build): GCC's -O3 vectorises the products of ddot's remainder loop, which
    # are then not fused with its sums, changing its results)
    list(APPEND NEOS_OPENBLAS_ARGS -G "${CMAKE_GENERATOR}" "-DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}" "-DCMAKE_C_FLAGS_RELEASE=-O2 -DNDEBUG" -DNUM_THREADS=64)
endif()
set(NEOS_OPENBLAS_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/OpenBLAS-0.3.34")
set(NEOS_OPENBLAS_BUILD_DIR "${CMAKE_BINARY_DIR}/openblas-build")
function(neos_openblas_step aStep)
    # (the arguments as bracket arguments: their "|"s are the lists' ";"s; the output is also written to openblas-<step>.log)
    set(command "")
    set(shown "")
    foreach(argument IN LISTS ARGN)
        string(REPLACE "|" ";" argument "${argument}")
        string(APPEND command " [==[${argument}]==]")
        string(APPEND shown " ${argument}")
    endforeach()
    # (configured afresh: the cache's flags are the first configuration's)
    set(fresh "")
    if(aStep STREQUAL "configure")
        set(fresh "file(REMOVE_RECURSE [==[${NEOS_OPENBLAS_BUILD_DIR}/CMakeCache.txt]==] [==[${NEOS_OPENBLAS_BUILD_DIR}/CMakeFiles]==])\n")
    endif()
    file(WRITE "${CMAKE_BINARY_DIR}/openblas-${aStep}.cmake" "${NEOS_OPENBLAS_STEP_ENV}" "${fresh}"
        "message(STATUS [===[OpenBLAS ${aStep}:${shown}]===])\n"
        "execute_process(COMMAND${command} RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE output ECHO_OUTPUT_VARIABLE ECHO_ERROR_VARIABLE)\n"
        "file(WRITE [==[${CMAKE_BINARY_DIR}/openblas-${aStep}.log]==] \"\${output}\")\n"
        "if(NOT result EQUAL 0)\n    message(FATAL_ERROR \"OpenBLAS ${aStep} failed: \${result} (${CMAKE_BINARY_DIR}/openblas-${aStep}.log)\")\nendif()\n")
endfunction()
neos_openblas_step(configure "${CMAKE_COMMAND}" ${NEOS_OPENBLAS_ARGS} -S "${NEOS_OPENBLAS_SOURCE_DIR}" -B "${NEOS_OPENBLAS_BUILD_DIR}")
neos_openblas_step(build "${CMAKE_COMMAND}" --build "${NEOS_OPENBLAS_BUILD_DIR}" --target openblas_64_static --parallel ${NEOS_OPENBLAS_JOBS})
neos_openblas_step(install "${CMAKE_COMMAND}" --install "${NEOS_OPENBLAS_BUILD_DIR}")
ExternalProject_Add(openblas
    SOURCE_DIR "${NEOS_OPENBLAS_SOURCE_DIR}"
    BINARY_DIR "${NEOS_OPENBLAS_BUILD_DIR}"
    INSTALL_DIR "${NEOS_OPENBLAS_DIR}"
    CONFIGURE_COMMAND "${CMAKE_COMMAND}" -P "${CMAKE_BINARY_DIR}/openblas-configure.cmake"
    BUILD_COMMAND "${CMAKE_COMMAND}" -P "${CMAKE_BINARY_DIR}/openblas-build.cmake"
    INSTALL_COMMAND "${CMAKE_COMMAND}" -P "${CMAKE_BINARY_DIR}/openblas-install.cmake"
    BUILD_BYPRODUCTS "${NEOS_OPENBLAS_LIBRARY}")
file(MAKE_DIRECTORY "${NEOS_OPENBLAS_DIR}/include/openblas64")
add_library(neos_openblas STATIC IMPORTED GLOBAL)
set_target_properties(neos_openblas PROPERTIES IMPORTED_LOCATION "${NEOS_OPENBLAS_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${NEOS_OPENBLAS_DIR}/include/openblas64" INTERFACE_LINK_LIBRARIES Threads::Threads)
if(NEOS_CLANG_BUILTINS)
    list(GET NEOS_CLANG_BUILTINS -1 NEOS_CLANG_BUILTINS)
    set_property(TARGET neos_openblas APPEND PROPERTY INTERFACE_LINK_LIBRARIES "${NEOS_CLANG_BUILTINS}")
endif()
add_dependencies(neos_openblas openblas)

file(GLOB NEOS_NUMPY_SOURCE_FILES runtime/numpy/src/*.cpp)
list(FILTER NEOS_NUMPY_SOURCE_FILES EXCLUDE REGEX "/(xss|simd)\\.[^/]*\\.cpp$")
# (the kernels of the element loops and the matrix product compiled for X86_V3 and X86_V4 (simd.hpp): x86-64, without
# fused multiply-adds (their results are the baseline's))
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$")
    set(NEOS_NUMPY_SIMD_SOURCE_FILES runtime/numpy/src/simd.avx2.cpp runtime/numpy/src/simd.avx512.cpp)
    if(MSVC)
        set_source_files_properties(runtime/numpy/src/simd.avx2.cpp PROPERTIES COMPILE_OPTIONS "/arch:AVX2;/fp:precise")
        set_source_files_properties(runtime/numpy/src/simd.avx512.cpp PROPERTIES COMPILE_OPTIONS "/arch:AVX512;/fp:precise")
    else()
        set_source_files_properties(runtime/numpy/src/simd.avx2.cpp PROPERTIES COMPILE_OPTIONS "-march=x86-64-v3;-ffp-contract=off")
        set_source_files_properties(runtime/numpy/src/simd.avx512.cpp PROPERTIES COMPILE_OPTIONS "-march=x86-64-v4;-ffp-contract=off")
    endif()
endif()
if(NEOS_NUMPY_XSS)
    # (x86-simd-sort compiled for each instruction set NumPy dispatches its sorts to: X86_V4, X86_V3 and AVX512_ICL)
    set(NEOS_XSS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/x86-simd-sort/src")
    set(NEOS_NUMPY_XSS_SOURCE_FILES runtime/numpy/src/xss.avx512.cpp runtime/numpy/src/xss.avx2.cpp runtime/numpy/src/xss.icl.cpp)
    set_source_files_properties(runtime/numpy/src/xss.avx512.cpp PROPERTIES COMPILE_OPTIONS "-march=x86-64-v4")
    set_source_files_properties(runtime/numpy/src/xss.avx2.cpp PROPERTIES COMPILE_OPTIONS "-march=x86-64-v3")
    set_source_files_properties(runtime/numpy/src/xss.icl.cpp PROPERTIES COMPILE_OPTIONS
        "-march=x86-64-v4;-mavx512vbmi;-mavx512vbmi2;-mavx512vnni;-mavx512bitalg;-mavx512vpopcntdq;-mavx512ifma")
    set_source_files_properties(${NEOS_NUMPY_XSS_SOURCE_FILES} PROPERTIES INCLUDE_DIRECTORIES "${NEOS_XSS_DIR}")
endif()
# (the baseline objects are linked before the ones compiled for other instruction sets: the baseline definitions of
# the inline functions they share are kept)
add_neos_runtime_plugin(numpy ${NEOS_NUMPY_SOURCE_FILES} ${NEOS_NUMPY_XSS_SOURCE_FILES} ${NEOS_NUMPY_SIMD_SOURCE_FILES})
target_include_directories(numpy PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/runtime/python/include")
target_link_libraries(numpy PRIVATE neos_openblas)
if(NOT MSVC AND NOT APPLE)
    # (OpenBLAS's symbols are the plugin's own: not exported)
    target_link_options(numpy PRIVATE "LINKER:--exclude-libs,${CMAKE_STATIC_LIBRARY_PREFIX}openblas_64${CMAKE_STATIC_LIBRARY_SUFFIX}")
endif()
if(NOT NEOS_NUMPY_XSS)
    target_compile_definitions(numpy PRIVATE NEOS_NUMPY_NO_XSS)
endif()
if(NOT NEOS_NUMPY_SIMD_SOURCE_FILES)
    target_compile_definitions(numpy PRIVATE NEOS_NUMPY_NO_SIMD)
endif()
if(NOT MSVC)
    # (no errno: the math functions are inlined (vectorised); the plugin does not read errno)
    target_compile_options(numpy PRIVATE -fno-math-errno)
endif()

# install
install(TARGETS neos_console neos_stub RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
install(TARGETS neos ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
install(DIRECTORY include/neos DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
install(DIRECTORY runtime/python/include/neos DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
install(DIRECTORY languages/ DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/neos/languages")
install(FILES "LICENSE" DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/licenses/${PROJECT_NAME}")
