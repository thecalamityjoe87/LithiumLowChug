cmake_minimum_required(VERSION 3.25)

project(LithiumLowChug VERSION 0.5.0 LANGUAGES C CXX)

if(APPLE)
    # JUCE's macOS sources are Objective-C++.
    enable_language(OBJC OBJCXX)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    # DSP code is unusably slow (and CPU numbers meaningless) in Debug.
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(LOWCHUG_BUILD_PLUGIN "Build the JUCE plugin (VST3/LV2/AU/Standalone)" ON)
option(LOWCHUG_BUILD_TOOLS  "Build the engine A/B evaluation harness and the CPU profiler" ON)
option(LOWCHUG_BUILD_TESTS  "Build the unit tests (run with ctest)" ON)
option(LOWCHUG_COPY_PLUGINS "Copy built plugins to the user plugin folders" OFF)

# JUCE's module files take 1-2 GB of RAM each to compile; cap parallel compiles
# so a many-core machine with modest RAM doesn't run out of memory.
cmake_host_system_information(RESULT lowchug_ram_mb QUERY TOTAL_PHYSICAL_MEMORY)
math(EXPR lowchug_compile_jobs "${lowchug_ram_mb} / 2048")
if(lowchug_compile_jobs LESS 1)
    set(lowchug_compile_jobs 1)
endif()
set_property(GLOBAL APPEND PROPERTY JOB_POOLS lowchug_compile=${lowchug_compile_jobs} lowchug_link=2)
set(CMAKE_JOB_POOL_COMPILE lowchug_compile)
set(CMAKE_JOB_POOL_LINK lowchug_link)

find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
    set(CMAKE_C_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
    set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
endif()

include(cmake/PluginIdentity.cmake)
include(cmake/Dependencies.cmake)

# The DSP core: plain C++, no JUCE, so it can be dropped into other hosts
# (e.g. as a built-in effect in a DAW) without the plugin wrapper.
add_subdirectory(src/dsp)

if(LOWCHUG_BUILD_PLUGIN)
    add_subdirectory(src/plugin)

    # The graphical installer in the Linux download (scripts/package.sh).
    if(UNIX AND NOT APPLE)
        add_subdirectory(tools/installer)
    endif()
endif()

if(LOWCHUG_BUILD_TOOLS)
    add_subdirectory(tools/engine-eval)
    add_subdirectory(tools/profile)

    # Draws the icon and installer artwork; uses JUCE, so needs the plugin build.
    if(LOWCHUG_BUILD_PLUGIN)
        add_subdirectory(tools/icongen)
    endif()
endif()

if(LOWCHUG_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
