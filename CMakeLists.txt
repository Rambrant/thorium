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

# --- Layers ---
# Two different things share this tree: a portable, rig-and-DUT-agnostic
# framework (libs/), and this one deployment's content -- one rig testing
# one DUT (rig/, dut/, dsl/, suite/, app/, all at the top level rather than
# under libs/). A separate rig -- its own repo, its own bench, its own
# DUT -- would reuse libs/core and libs/hal unchanged and write its own
# rig/dut/dsl/suite/app instead; nothing here should ever make libs/
# content depend on anything outside it.
#
# core  : foundational utilities, no dependencies on other layers
# hal   : generic hardware abstraction layer (instrument drivers, switching
#         fabric, wiring/measure/apply/safing mechanism) -- depends on core,
#         and on this rig's data below, but knows nothing rig-specific
#         itself; see libs/hal/README.md
# rig   : this rig's data only (which instruments it has, how they're
#         wired, what to call them) -- content only, no library of its own,
#         the same shape dut/ uses for DUT-specific data; see rig/README.md.
#         No add_subdirectory: nothing here has a build target yet (no
#         rig-level tests), only the files pointed at below.
# dut   : DUT-specific data only (adapter profile + criteria variants, both
#         flat macro-driven .inc files -- see dut/README.md) -- no library
#         of its own, just tests over that data linked against hal
# dsl   : this DUT's test vocabulary -- methods named after the test
#         description document's own terms, for traceability between the
#         requirement/test/development disciplines -- depends on dut + hal
#         + core, the same reason it lives alongside them rather than under
#         libs/
# app   : the runner (main.cpp) plus the "scripts" library it runs -- the
#         scripts themselves, the test catalog, and scripts.hpp live under
#         suite/ rather than in app/, which holds no DUT-specific content at
#         all; the criteria/tolerance variants live under dut/ instead,
#         since tolerances are a property of the DUT being tested (see
#         dut/README.md)

# --- This rig's data for the generic hal library ---
# libs/hal is a generic instrument-driver + switching-fabric library with no
# instruments plugged into it at all -- it needs a rig to supply which
# instruments exist, how they're wired, and what to call them, as four file
# paths (see libs/hal/CMakeLists.txt's own comment). This repo IS that one
# rig for now, so it points them at ./rig/ here; a separate rig repo pulling
# hal in later would set these same four variables pointing at its own
# rig/-equivalent instead -- see rig/README.md.
set(THORIUM_INSTRUMENT_IDS "${CMAKE_SOURCE_DIR}/rig/instrument_id.inc" CACHE FILEPATH "" FORCE)
set(THORIUM_ACTIVE_INSTRUMENTS "${CMAKE_SOURCE_DIR}/rig/active_instruments.hpp" CACHE FILEPATH "" FORCE)
set(THORIUM_INSTRUMENT_TABLE "${CMAKE_SOURCE_DIR}/rig/instrument.inc" CACHE FILEPATH "" FORCE)
set(THORIUM_WIRING_TABLE "${CMAKE_SOURCE_DIR}/rig/wiring.inc" CACHE FILEPATH "" FORCE)

add_subdirectory(libs/core)
add_subdirectory(libs/hal)
add_subdirectory(dut)
#add_subdirectory(dsl)
add_subdirectory(app)
