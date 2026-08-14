cmake_minimum_required(VERSION 3.30)

if(APPLE)
    set(CMAKE_C_COMPILER "/opt/homebrew/bin/gcc-16")
    set(CMAKE_CXX_COMPILER "/opt/homebrew/bin/g++-16")
elseif(WIN32)
    set(CMAKE_C_COMPILER "C:/mingw64/bin/gcc.exe")
    set(CMAKE_CXX_COMPILER "C:/mingw64/bin/g++.exe")
endif()

project(Thorium LANGUAGES CXX VERSION 0.1.0)

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

# GNUInstallDirs: standard, override-able CMAKE_INSTALL_{BINDIR,LIBDIR,
# INCLUDEDIR} for everything installed below (libs/core, libs/hal, app's
# run_scripts). CMakePackageConfigHelpers: configure_package_config_file()/
# write_basic_package_version_file(), used once below to generate the
# Thorium CMake package a separate rig repo would find_package()/
# FetchContent this framework through.
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

# --- Criteria/tolerance variants ---
# Which tolerance tables the scripts layer's CRIT/CRITERIA checks are built
# against: e.g. production hardware fresh off the line vs. a stress-chamber
# run vs. equipment that's been in service for years.
#
# EVERY variant in this list is compiled into the binary, and which one a run
# actually applies is chosen at runtime by run_scripts' --criteria= flag. That
# is a change from how this used to work -- one variant was baked in per build,
# and three variants meant three build directories and three binaries -- and it
# costs nothing in compile-time checking, because the merged table a script
# sees is still a real struct with real members (see core/active_criteria.hpp
# for the mechanism, and suite/README.md for the whole story). It in fact
# checks *more*: every variant's predicates are now type-checked against the
# reading each script actually takes, where before only the one built-against
# variant was.
#
# Still a single, CMake-validated list -- not a raw environment variable read
# via #ifdef -- so a typo fails the *configure* step loudly, the same way a
# misspelled CRIT id fails to compile. Adding a variant is this one line plus
# the matching dut/criteria_<name>.inc; everything the compiler and the runtime
# selector need is generated from here (cmake/CriteriaVariants.cmake).
set(THORIUM_KNOWN_CRITERIA_VARIANTS production stress aged)

# Which of them a run uses when --criteria= is not given. A default rather than
# a build-time restriction: a bench operator who forgets the flag gets the
# variant this deployment was configured for, not an arbitrary one.
set(THORIUM_CRITERIA_VARIANT "production" CACHE STRING
    "Which criteria/tolerance variant a run uses when --criteria= is not given (${THORIUM_KNOWN_CRITERIA_VARIANTS})")
set_property(CACHE THORIUM_CRITERIA_VARIANT PROPERTY STRINGS ${THORIUM_KNOWN_CRITERIA_VARIANTS})

if(NOT THORIUM_CRITERIA_VARIANT IN_LIST THORIUM_KNOWN_CRITERIA_VARIANTS)
    message(FATAL_ERROR
        "THORIUM_CRITERIA_VARIANT='${THORIUM_CRITERIA_VARIANT}' is not one of: ${THORIUM_KNOWN_CRITERIA_VARIANTS}")
endif()

include(CriteriaVariants)

# Sets THORIUM_CRITERIA_VARIANT_TABLES / THORIUM_CRITERIA_VARIANT_NAMES to the
# two generated files' paths; libs/core and app hand those to the translation
# units that read them, as compile definitions, exactly the way
# THORIUM_PRODUCTION_CRITERIA and THORIUM_TEST_CATALOG already work.
thorium_generate_criteria_variants(
    "${THORIUM_KNOWN_CRITERIA_VARIANTS}"
    "${CMAKE_SOURCE_DIR}/dut"
    "${CMAKE_BINARY_DIR}/generated/thorium"
)

message(STATUS "Criteria variants: ${THORIUM_KNOWN_CRITERIA_VARIANTS} (default: ${THORIUM_CRITERIA_VARIANT})")

# --- Content revision, for the run logs' traceability header ---
# Both log streams record which revision of this deployment's *content* -- the
# test scripts, the DUT profile and criteria, the rig's wiring -- a run was
# performed against (see core::RunInfo in libs/core/include/core/journal.hpp).
# That is a different question from THORIUM_FRAMEWORK_VERSION, which versions
# libs/: this versions what is being tested and how, which is the half that
# usually differs between two runs that disagree with each other.
#
# Derived from git, with --dirty, and that flag is the important part. This runs
# at *configure* time, so editing a criteria table and rebuilding (which
# recompiles, but does not reconfigure) would otherwise leave the logs claiming
# a revision that no longer describes what was built. "--dirty" means an edited
# working tree is visibly marked as such in every report it produces, so a
# reader is never handed a clean-looking sha for content that wasn't clean.
# Re-run cmake to refresh it after committing.
#
# --always so a repository with no tags still yields the short sha rather than
# failing; "unknown" if there is no git, or no repository, at all -- an
# obviously-absent value rather than a plausible wrong one, the same choice
# libs/core/src/journal.cpp makes for the rest of the header.
find_package(Git QUIET)

set(THORIUM_CONTENT_REVISION "unknown")

if(GIT_FOUND AND EXISTS "${CMAKE_SOURCE_DIR}/.git")
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" describe --always --dirty --tags
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        OUTPUT_VARIABLE THORIUM_GIT_DESCRIBE
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )

    if(THORIUM_GIT_DESCRIBE)
        set(THORIUM_CONTENT_REVISION "${THORIUM_GIT_DESCRIBE}")
    endif()
endif()

# Three variables, not one: suite/, dut/ and rig/ all come from this repository
# today and so share a revision, but they are separable by design (see the layer
# comment below, and rig/README.md). A rig repo consuming this framework sets
# its own THORIUM_RIG_VERSION while pulling suite/ and dut/ from elsewhere, and
# the log then reports three different revisions -- which is exactly when the
# distinction earns its keep. The human log collapses them back into one row
# whenever they agree, so the common case costs no extra lines.
#
# Deliberately NOT cache variables, and here that is not merely a convenience:
# caching a *computed* default freezes it permanently. A cache entry's default is
# only consulted when the entry doesn't exist, so the revision recorded at the
# first configure of a build directory would be the revision reported by every
# report that build directory ever produced -- through every commit afterwards,
# silently. For a field whose entire purpose is to say which content was tested,
# that is worse than not having the field.
#
# As plain variables they are recomputed on every reconfigure, while an explicit
# override still wins (a -D creates a cache entry, which `DEFINED` sees):
#
#     cmake -B <build-dir> -DTHORIUM_RIG_VERSION=bench7-2026-08
#
if(NOT DEFINED THORIUM_SUITE_VERSION)
    set(THORIUM_SUITE_VERSION "${THORIUM_CONTENT_REVISION}")
endif()

if(NOT DEFINED THORIUM_DUT_VERSION)
    set(THORIUM_DUT_VERSION "${THORIUM_CONTENT_REVISION}")
endif()

if(NOT DEFINED THORIUM_RIG_VERSION)
    set(THORIUM_RIG_VERSION "${THORIUM_CONTENT_REVISION}")
endif()

message(STATUS "Content revision: suite=${THORIUM_SUITE_VERSION} dut=${THORIUM_DUT_VERSION} rig=${THORIUM_RIG_VERSION}")

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
# hal   : generic hardware abstraction layer (switching fabric, wiring,
#         the measure/apply/safing mechanism, and the API instrument
#         drivers are written against) -- depends on core, and on this rig's
#         data below, but knows nothing rig-specific itself. Two targets:
#         hal, which drivers compile against, and hal_rig, which compiles
#         against the drivers -- see libs/hal/README.md
# instruments
#       : one directory per instrument driver, each independently
#         packageable, each declaring its own target and carrying its own
#         tests -- framework content like libs/, but versioned and shipped a
#         driver at a time rather than as one library, which is why it sits
#         at the top level rather than under libs/; see
#         instruments/README.md
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
# instruments exist, how they're wired, and what to call them, as three file
# paths (see libs/hal/CMakeLists.txt's own comment). This repo IS that one
# rig for now, so it points them at ./rig/ here; a separate rig repo pulling
# hal in later would set these same three variables pointing at its own
# rig/-equivalent instead -- see rig/README.md.
set(THORIUM_ACTIVE_INSTRUMENTS "${CMAKE_SOURCE_DIR}/rig/active_instruments.hpp" CACHE FILEPATH "" FORCE)
set(THORIUM_INSTRUMENT_TABLE "${CMAKE_SOURCE_DIR}/rig/instrument.inc" CACHE FILEPATH "" FORCE)
set(THORIUM_WIRING_TABLE "${CMAKE_SOURCE_DIR}/rig/wiring.inc" CACHE FILEPATH "" FORCE)

# --- Which instrument drivers are available to build against ---
# The fourth thing a rig supplies libs/hal, alongside the three files above:
# where its instrument drivers live. One directory per driver, each with its
# own CMakeLists.txt declaring its own target -- see instruments/README.md and
# libs/hal/CMakeLists.txt's own comment on why the drivers are configured
# between hal and hal_rig rather than inside either.
#
# Discovered by glob rather than listed, and that is a smaller decision than it
# looks, because it settles *availability*, not *use*. What this rig actually
# instantiates is still declared, one explicit line at a time, in
# rig/instrument.inc -- a driver sitting in instruments/ that no INSTRUMENT()
# line names contributes no global, no hal::InstrumentId enumerator and no
# object code. So dropping a driver package into the tree makes it compilable;
# it does not make it part of the rig, and the single reviewable list of what
# this rig has stays exactly where it already was. (Contrast
# THORIUM_CRITERIA_VARIANT_NAMES below, which is spelled out longhand: there,
# the list *is* the semantic content, so a glob would be discarding the thing
# worth reviewing.)
#
# CONFIGURE_DEPENDS so unpacking a driver into instruments/ is picked up by the
# next build without a manual reconfigure. It costs a directory stat per build,
# and the usual objection to it -- that globbed *source lists* go stale in ways
# that are hard to see -- does not apply here: what is globbed is a set of
# directories that must each contain a CMakeLists.txt, and a missing one fails
# loudly at configure time rather than silently dropping a file from a target.
file(GLOB _thorium_instrument_manifests CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/instruments/*/CMakeLists.txt")

set(THORIUM_INSTRUMENT_DIRS "")

foreach(_thorium_manifest IN LISTS _thorium_instrument_manifests)
    get_filename_component(_thorium_dir "${_thorium_manifest}" DIRECTORY)
    list(APPEND THORIUM_INSTRUMENT_DIRS "${_thorium_dir}")
endforeach()

add_subdirectory(libs/core)
add_subdirectory(libs/hal)

# --- Package export: core + hal + the drivers as the reusable "Thorium" framework ---
# core, hal, hal_rig and every instrument driver register themselves into the
# ThoriumTargets export set via their own install(TARGETS ... EXPORT
# ThoriumTargets ...) calls (see libs/core/CMakeLists.txt,
# libs/hal/CMakeLists.txt, instruments/*/CMakeLists.txt) -- this is what turns
# that into an actual find_package()-able CMake package: ThoriumTargets.cmake
# (the compiled targets, namespaced Thorium::core/Thorium::hal/...) plus
# ThoriumConfig.cmake/ThoriumConfigVersion.cmake (what find_package(Thorium)
# actually looks for).
#
# One thing this does NOT give a consumer: a hal that works for their rig
# out of the box. hal's THORIUM_ACTIVE_INSTRUMENTS/THORIUM_INSTRUMENT_TABLE/
# THORIUM_WIRING_TABLE (see libs/hal/CMakeLists.txt) are baked in via
# #include at hal's OWN compile time, so an installed libhal.a is only ever
# built for the one rig that supplied those three paths when IT was
# configured -- this repo's rig/, in this build. A different rig consuming
# this package needs to build hal from source against its own rig/-
# equivalent (FetchContent/add_subdirectory, setting those same three
# variables before hal configures), not link against a prebuilt hal from
# somewhere else's install -- see libs/hal/README.md.
install(EXPORT ThoriumTargets
    FILE ThoriumTargets.cmake
    NAMESPACE Thorium::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Thorium
)

configure_package_config_file(
    cmake/ThoriumConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/ThoriumConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Thorium
)
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/ThoriumConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/ThoriumConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/ThoriumConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Thorium
)

add_subdirectory(dut)
#add_subdirectory(dsl)
add_subdirectory(app)
