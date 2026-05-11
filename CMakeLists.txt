cmake_minimum_required(VERSION 3.21)
project(3dso LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(DSO_ENABLE_NATIVE "Enable host-specific CPU tuning for peak local performance" OFF)
option(DSO_ENABLE_IPO "Enable link-time optimization for release builds" ON)
option(DSO_ENABLE_AVX2 "Enable AVX2-capable code generation on supported compilers" ON)
option(DSO_ENABLE_FAST_MATH "Enable aggressive floating-point optimizations for benchmark-only builds" OFF)
set(DSO_PGO_MODE "OFF" CACHE STRING "Profile-guided optimization mode: OFF, INSTRUMENT, or OPTIMIZE")
set_property(CACHE DSO_PGO_MODE PROPERTY STRINGS OFF INSTRUMENT OPTIMIZE)

string(TOUPPER "${DSO_PGO_MODE}" DSO_PGO_MODE_UPPER)
if(NOT DSO_PGO_MODE_UPPER STREQUAL "OFF" AND
   NOT DSO_PGO_MODE_UPPER STREQUAL "INSTRUMENT" AND
   NOT DSO_PGO_MODE_UPPER STREQUAL "OPTIMIZE")
    message(FATAL_ERROR "DSO_PGO_MODE must be one of: OFF, INSTRUMENT, OPTIMIZE")
endif()

find_package(OpenMP)
include(CheckIPOSupported)

add_library(3dso_core STATIC
    src/cli_args.cpp
    src/csv_writer.cpp
    src/so3d_engine.cpp
)
add_library(3dso::core ALIAS 3dso_core)

if(OpenMP_CXX_FOUND)
    target_link_libraries(3dso_core PUBLIC OpenMP::OpenMP_CXX)
    target_compile_definitions(3dso_core PRIVATE DSO_HAS_OPENMP=1)
endif()

target_include_directories(3dso_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_compile_features(3dso_core PUBLIC cxx_std_20)
target_compile_definitions(3dso_core PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX)

add_executable(3dso
    src/main.cpp
)
target_link_libraries(3dso PRIVATE 3dso_core)
target_compile_features(3dso PRIVATE cxx_std_20)
target_compile_definitions(3dso PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX)

if(MSVC)
    foreach(dso_target IN ITEMS 3dso_core 3dso)
        target_compile_options(${dso_target} PRIVATE
            /permissive-
            /EHsc
            /bigobj
            /Gy
            /Gw
            $<$<CONFIG:Release>:/O2>
            $<$<CONFIG:Release>:/Ot>
            $<$<CONFIG:Release>:/Oi>
        )
    endforeach()
    if(DSO_ENABLE_FAST_MATH)
        target_compile_options(3dso_core PRIVATE $<$<CONFIG:Release>:/fp:fast>)
    else()
        target_compile_options(3dso_core PRIVATE $<$<CONFIG:Release>:/fp:precise>)
    endif()
    if(DSO_ENABLE_AVX2)
        target_compile_options(3dso_core PRIVATE $<$<CONFIG:Release>:/arch:AVX2>)
    endif()
    if(DSO_PGO_MODE_UPPER STREQUAL "INSTRUMENT")
        target_link_options(3dso PRIVATE $<$<CONFIG:Release>:/GENPROFILE>)
    elseif(DSO_PGO_MODE_UPPER STREQUAL "OPTIMIZE")
        target_link_options(3dso PRIVATE $<$<CONFIG:Release>:/USEPROFILE>)
    endif()
else()
    target_compile_options(3dso_core PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        $<$<CONFIG:Release>:-O3>
        $<$<CONFIG:Release>:-funroll-loops>
    )
    if(DSO_ENABLE_FAST_MATH)
        target_compile_options(3dso_core PRIVATE
            $<$<CONFIG:Release>:-ffast-math>
            $<$<CONFIG:Release>:-ffp-contract=fast>
        )
    else()
        target_compile_options(3dso_core PRIVATE
            $<$<CONFIG:Release>:-ffp-contract=off>
        )
    endif()
    if(DSO_ENABLE_AVX2)
        target_compile_options(3dso_core PRIVATE $<$<CONFIG:Release>:-mavx2>)
    endif()
endif()

if(DSO_ENABLE_NATIVE)
    if(MSVC)
        target_compile_options(3dso_core PRIVATE $<$<CONFIG:Release>:/arch:AVX2>)
    else()
        target_compile_options(3dso_core PRIVATE $<$<CONFIG:Release>:-march=native>)
    endif()
endif()

if(DSO_ENABLE_IPO)
    check_ipo_supported(RESULT ipo_supported OUTPUT ipo_error)
    if(ipo_supported)
        set_property(TARGET 3dso_core PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
        set_property(TARGET 3dso PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
    else()
        message(STATUS "IPO/LTO unavailable: ${ipo_error}")
    endif()
endif()

if(MSVC AND NOT DSO_PGO_MODE_UPPER STREQUAL "OFF")
    message(STATUS "3DSO PGO mode: ${DSO_PGO_MODE_UPPER}")
endif()

