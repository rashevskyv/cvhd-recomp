# cvhd - ReXGlue Recompiled Project
#
# SDK boilerplate lives in generated/rexglue.cmake.

cmake_minimum_required(VERSION 3.25)
project(cvhd LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

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
