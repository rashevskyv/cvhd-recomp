# cvhd - ReXGlue Recompiled Project
#
# SDK boilerplate lives in generated/rexglue.cmake.

cmake_minimum_required(VERSION 3.25)
project(cvhd LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# The SDK's byte-swap helpers use SSSE3 / SSE4.1 intrinsics (stuntdouble does the same).
if(NOT MSVC AND CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
    add_compile_options(-mssse3 -msse4.1)
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
