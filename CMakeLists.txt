cmake_minimum_required(VERSION 3.20)
cmake_policy(VERSION 3.20)

# meta
project(neos VERSION 0.0.1 LANGUAGES C CXX)

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
    add_compile_options($<$<COMPILE_LANGUAGE:CXX>:/std:c++latest> /bigobj /utf-8 /Zc:__cplusplus)
    set(CMAKE_DEBUG_POSTFIX "d")
    add_definitions(-D_UNICODE)
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

# libffi: the VM calls runtime plugins' functions with it (without it neos runs scripts that do not use runtime plugins)
option(NEOS_LIBFFI "Call runtime plugin functions with libffi" ON)
option(NEOS_LIBFFI_STATIC "libffi is a static library" OFF)
if(NEOS_LIBFFI)
    find_package(PkgConfig QUIET)
    if(PKG_CONFIG_FOUND)
        pkg_check_modules(PC_LIBFFI QUIET libffi)
    endif()
    find_path(LIBFFI_INCLUDE_DIR ffi.h HINTS ${PC_LIBFFI_INCLUDE_DIRS}
        "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/libffi-3.3/include" PATH_SUFFIXES ffi)
    find_library(LIBFFI_LIBRARY NAMES ffi libffi HINTS ${PC_LIBFFI_LIBRARY_DIRS})
    if(NOT LIBFFI_INCLUDE_DIR OR NOT LIBFFI_LIBRARY)
        message(WARNING "libffi not found (set LIBFFI_INCLUDE_DIR and LIBFFI_LIBRARY): neos is built without runtime plugin calls")
        set(NEOS_LIBFFI OFF)
    endif()
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
target_link_libraries(neos PUBLIC ${NEOS_NEOLIB_TARGET} neonumeric::neonumeric PRIVATE ${CMAKE_DL_LIBS})
set_property(TARGET neos PROPERTY POSITION_INDEPENDENT_CODE ON) # (linked into the concept libraries)
set_property(TARGET neos PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
if(NEOS_LIBFFI)
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

# concept libraries (plugins with the extension ".ncl")
function(add_neos_concept_library TARGET)
    add_library(${TARGET} MODULE ${ARGN})
    target_link_libraries(${TARGET} PRIVATE neos)
    target_compile_definitions(${TARGET} PRIVATE CORE_EXPORTS)
    set_target_properties(${TARGET} PROPERTIES PREFIX "" SUFFIX ".ncl"
        LIBRARY_OUTPUT_DIRECTORY "${NEOS_OUTPUT_DIRECTORY}"
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    install(TARGETS ${TARGET} LIBRARY DESTINATION "${CMAKE_INSTALL_BINDIR}")
endfunction()

add_neos_concept_library(core
    concepts/src/common/dllmain.cpp
    concepts/src/core/plugin.cpp
    concepts/src/core/bitwise.cpp
    concepts/src/core/boolean.cpp
    concepts/src/core/core.cpp
    concepts/src/core/javascript.cpp
    concepts/src/core/language.cpp
    concepts/src/core/logic.cpp
    concepts/src/core/math.cpp
    concepts/src/core/module.cpp
    concepts/src/core/object.cpp
    concepts/src/core/python.cpp
    concepts/src/core/string.cpp)
add_neos_concept_library(core.math.universal
    concepts/src/common/dllmain.cpp
    concepts/src/core/plugin.math.universal.cpp
    concepts/src/core/math.universal.cpp)

# console (the neos executable)
add_executable(neos_console console/src/console.cpp)
target_link_libraries(neos_console PRIVATE neos Boost::program_options)
set_target_properties(neos_console PROPERTIES OUTPUT_NAME neos
    RUNTIME_OUTPUT_DIRECTORY "${NEOS_OUTPUT_DIRECTORY}"
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
add_dependencies(neos_console core core.math.universal)

# runtime plugins (C ABI: include/neos/runtime/plugin.h)
function(add_neos_runtime_plugin TARGET)
    add_library(${TARGET} MODULE ${ARGN})
    target_include_directories(${TARGET} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
    set_target_properties(${TARGET} PROPERTIES PREFIX "" OUTPUT_NAME "neos.runtime.${TARGET}" DEBUG_POSTFIX "" # (the name neos loads)
        C_VISIBILITY_PRESET hidden CXX_VISIBILITY_PRESET hidden
        LIBRARY_OUTPUT_DIRECTORY "${NEOS_PLUGIN_OUTPUT_DIRECTORY}"
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    if(MSVC)
        target_compile_definitions(${TARGET} PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()
    add_dependencies(neos_console ${TARGET})
    install(TARGETS ${TARGET} LIBRARY DESTINATION "${CMAKE_INSTALL_BINDIR}/plugins")
endfunction()

add_neos_runtime_plugin(c runtime/c/src/c.c)

add_neos_runtime_plugin(javascript runtime/javascript/src/javascript.cpp)

add_neos_runtime_plugin(python runtime/python/src/python.cpp runtime/python/src/objects.cpp)
target_include_directories(python PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/runtime/python/include")
target_link_libraries(python PRIVATE neonumeric::neonumeric) # (ints beyond 64 bits)

file(GLOB NEOS_NUMPY_SOURCE_FILES runtime/numpy/src/*.cpp)
list(FILTER NEOS_NUMPY_SOURCE_FILES EXCLUDE REGEX "/xss\\.[^/]*\\.cpp$")
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
add_neos_runtime_plugin(numpy ${NEOS_NUMPY_SOURCE_FILES} ${NEOS_NUMPY_XSS_SOURCE_FILES})
target_include_directories(numpy PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/runtime/python/include")
if(NOT NEOS_NUMPY_XSS)
    target_compile_definitions(numpy PRIVATE NEOS_NUMPY_NO_XSS)
endif()

# install
install(TARGETS neos_console RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
install(TARGETS neos ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
install(DIRECTORY include/neos DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
install(DIRECTORY runtime/python/include/neos DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
install(DIRECTORY languages/ DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/neos/languages")
install(FILES "LICENSE" DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/licenses/${PROJECT_NAME}")
