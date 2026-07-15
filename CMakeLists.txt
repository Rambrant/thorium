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

# --- Criteria/tolerance variant selection ---
# Which tolerance table the scripts layer's CRIT/CRITERIA checks are built
# against: e.g. production hardware fresh off the line vs. a stress-chamber
# run vs. equipment that's been in service for years. This is a single,
# CMake-validated choice -- not a raw environment variable read via #ifdef --
# so a typo fails the *configure* step loudly, the same way a misspelled
# CRIT id fails to compile. See suite/README.md for the full mechanism.
set(THORIUM_KNOWN_CRITERIA_VARIANTS production stress aged)
set(THORIUM_CRITERIA_VARIANT "production" CACHE STRING
    "Which criteria/tolerance variant to build the scripts layer against (${THORIUM_KNOWN_CRITERIA_VARIANTS})")
set_property(CACHE THORIUM_CRITERIA_VARIANT PROPERTY STRINGS ${THORIUM_KNOWN_CRITERIA_VARIANTS})

if(NOT THORIUM_CRITERIA_VARIANT IN_LIST THORIUM_KNOWN_CRITERIA_VARIANTS)
    message(FATAL_ERROR
        "THORIUM_CRITERIA_VARIANT='${THORIUM_CRITERIA_VARIANT}' is not one of: ${THORIUM_KNOWN_CRITERIA_VARIANTS}")
endif()
message(STATUS "Criteria variant: ${THORIUM_CRITERIA_VARIANT}")

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
# app   : the runner (main.cpp) plus the "scripts" library it runs -- the
#         scripts themselves, the test catalog, and scripts.hpp live under
#         suite/ rather than in app/, which holds no DUT-specific content at
#         all; the criteria/tolerance variants live under libs/dut/ instead,
#         since tolerances are a property of the DUT being tested (see
#         libs/dut/README.md)
add_subdirectory(libs/core)
add_subdirectory(libs/hal)
add_subdirectory(libs/dut)
#add_subdirectory(libs/dsl)
add_subdirectory(app)
