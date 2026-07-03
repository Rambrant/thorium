cmake_minimum_required(VERSION 3.30)  # 3.30+ needed: CXX_STANDARD 26 support

# --- Force GCC 16 (Homebrew) instead of Apple Clang ---
# Must be set BEFORE the project() call.
if(NOT CMAKE_C_COMPILER)
    set(CMAKE_C_COMPILER "/opt/homebrew/bin/gcc-16")
endif()
if(NOT CMAKE_CXX_COMPILER)
    set(CMAKE_CXX_COMPILER "/opt/homebrew/bin/g++-16")
endif()

project(TestFramework LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

message(STATUS "Compiler: ${CMAKE_CXX_COMPILER} (${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION})")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")

# -freflection and -fcontracts are experimental C++26 features (GCC 16+ only)
# and require -std=c++26 (set above). -fcontracts must ALSO be passed at
# link time -- CMake compiles/links as separate steps, unlike invoking g++
# directly -- or you'll get missing-symbol errors for the default contract
# violation handler.
add_compile_options(-Wall -Wextra -Wpedantic -Werror -fcontracts -freflection)
add_link_options(-fcontracts)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

# --- Testing setup (GoogleTest, fetched once, shared by every layer) ---
option(BUILD_TESTING_LAYERS "Build unit tests for each layer" ON)
if(BUILD_TESTING_LAYERS)
    enable_testing()
    include(cmake/FetchGTest.cmake)
endif()

# --- Layers, in dependency order ---
# core  : foundational utilities, no dependencies on other layers
# hal   : hardware abstraction layer, depends on core
# dut   : device-under-test model, depends on hal + core
# dsl   : domain-specific language for writing tests, depends on dut + hal + core
# scripts: the actual test scripts, written using the dsl
add_subdirectory(libs/core)
add_subdirectory(libs/hal)
add_subdirectory(libs/dut)
add_subdirectory(libs/dsl)
add_subdirectory(scripts)
