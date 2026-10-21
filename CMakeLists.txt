# cvhd - ReXGlue Recompiled Project
#
# SDK boilerplate lives in generated/rexglue.cmake.

cmake_minimum_required(VERSION 3.25)
project(cvhd LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# The SDK's byte-swap helpers use SSSE3 / SSE4.1 intrinsics (stuntdouble does the same).
if(NOT MSVC AND CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
    add_compile_options(-mssse3 -msse4.1)
endif()

if(NINTENDO_SWITCH)
    # Handed to the SDK (switch/sdk-edits.py): the Horizon replacements for its POSIX sources.
    set(CVHD_SWITCH_HOST "${CMAKE_CURRENT_SOURCE_DIR}/switch/host")
    set(REXGLUE_SWITCH_INCLUDE_DIR "${CVHD_SWITCH_HOST}/include")
    set(REXGLUE_SWITCH_CORE_SOURCES
        ${CVHD_SWITCH_HOST}/memory_nx.cpp
        ${CVHD_SWITCH_HOST}/mapped_memory_nx.cpp
        ${CVHD_SWITCH_HOST}/fiber_nx.cpp
        ${CVHD_SWITCH_HOST}/exception_handler_nx.cpp
        ${CVHD_SWITCH_HOST}/seh_nx.cpp)
    set(REXGLUE_SWITCH_UI_SOURCES
        ${CVHD_SWITCH_HOST}/ui/window_nx.cpp
        ${CVHD_SWITCH_HOST}/ui/windowed_app_context_nx.cpp)
    set(REXGLUE_SWITCH_MAIN_SOURCE ${CVHD_SWITCH_HOST}/ui/windowed_app_main_nx.cpp)
    # newlib hides POSIX declarations (fileno, fdopen, tm_gmtoff) under strict -std=c++23.
    add_compile_definitions(_GNU_SOURCE)
    # Horizon has no time zones (newlib has no tm_gmtoff): spdlog logs in UTC.
    add_compile_definitions(SPDLOG_NO_TZ_OFFSET)
    # vulkan.hpp has no dlopen for this platform; the driver is linked statically.
    add_compile_definitions(VULKAN_HPP_ENABLE_DYNAMIC_LOADER_TOOL=0)
    # newlib/libnx gaps, in every translation unit.
    add_compile_options("$<$<COMPILE_LANGUAGE:C,CXX>:-include>"
                        "$<$<COMPILE_LANGUAGE:C,CXX>:${REXGLUE_SWITCH_INCLUDE_DIR}/switch_compat.h>")
    include_directories(${REXGLUE_SWITCH_INCLUDE_DIR})
endif()

include(generated/rexglue.cmake)

# Sources
set(CVHD_SOURCES
    src/main.cpp
)

if(WIN32)
    add_executable(cvhd WIN32 ${CVHD_SOURCES})
else()
    add_executable(cvhd ${CVHD_SOURCES})
endif()

rexglue_setup_target(cvhd GPU_PLUGINS xenos)

# The SDK's UI headers include imgui.h (headers only: ImGui is compiled into rexruntime).
if(REXSDK_DIR)
    target_include_directories(cvhd PRIVATE "${REXSDK_DIR}/thirdparty/imgui")
endif()

# --- Nintendo Switch (devkitA64 + libnx + mesa-switch NVK), see CLAUDE.md step 4 ---------------
if(NINTENDO_SWITCH)
    set(NVK_SDK "" CACHE PATH "mesa-switch NVK install (lib/libvulkan.a)")
    set(CVHD_SWITCH_HOST "${CMAKE_CURRENT_SOURCE_DIR}/switch/host")
    target_sources(cvhd PRIVATE
        ${CVHD_SWITCH_HOST}/switch_nx.cpp
        ${CVHD_SWITCH_HOST}/switch_compat.c
    )
    target_include_directories(cvhd PRIVATE ${CVHD_SWITCH_HOST}/include)
    target_link_options(cvhd PRIVATE
        -Wl,--wrap=__syscall_thread_create
        -Wl,-u,vk_icdGetInstanceProcAddr
    )
    # zstd, expat and zlib from devkitPro portlibs (the SDK links them on Linux through the system).
    target_link_libraries(cvhd PRIVATE "${NVK_SDK}/lib/libvulkan.a" zstd expat z nx m stdc++)
    nx_create_nro(cvhd NAME "Castlevania HD" AUTHOR "cvhd-recomp" VERSION "0.1")
endif()
