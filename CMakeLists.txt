cmake_minimum_required(VERSION 3.30)

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

# --- Which compiler is a preset's business; which *kind* of compiler is this file's ---
# This file used to name the toolchain itself, in an if(APPLE)/elseif(WIN32)
# block before project() that set CMAKE_C/CXX_COMPILER to Homebrew's GCC or to
# C:/mingw64. That duplicated what CMakePresets.json's two platform halves
# already say, and duplicated it in the weaker position: a preset's cache entry
# wins, so on every configure that went through a preset -- which is what
# README.md tells you to do, and the only way to get the right generator -- the
# block was dead code that still read as authoritative. A compiler path is now
# written in exactly one place, per host, in CMakePresets.json.
#
# What stays here is the requirement rather than the path, because it is not a
# host fact and no preset should be able to disagree with it: -freflection and
# -fcontracts below are GCC 16+ only, so a configure holding anything else is
# already doomed. Failing here says which compiler arrived and where to change
# it; failing later says `unrecognized command line option '-freflection'` from
# the middle of a translation unit.
#
# The likely way to land here is configuring without a preset at all -- a bare
# cmake -B, or an IDE profile that manages its own build directory. Point it at
# a preset (cmake --list-presets), or give it the compiler explicitly.
if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_VERSION VERSION_LESS 16)
    message(FATAL_ERROR
        "Thorium needs GCC 16 or newer: it is built on C++26 reflection "
        "(-freflection) and contracts (-fcontracts), which nothing else "
        "implements yet. This configure got ${CMAKE_CXX_COMPILER_ID} "
        "${CMAKE_CXX_COMPILER_VERSION} (${CMAKE_CXX_COMPILER}), which was not "
        "chosen by any of this repo's presets -- see cmake --list-presets and "
        "README.md, or set CMAKE_CXX_COMPILER yourself.")
endif()

# -freflection and -fcontracts are experimental C++26 features (GCC 16+ only)
# and require -std=c++26 (set above). -fcontracts must ALSO be passed at
# link time -- CMake compiles/links as separate steps, unlike invoking g++
# directly -- or you'll get missing-symbol errors for the default contract
# violation handler.
add_compile_options(-Wall -Wextra -Wpedantic -Werror -fcontracts -freflection)
add_link_options(-fcontracts)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

# GNUInstallDirs: standard, override-able CMAKE_INSTALL_{BINDIR,LIBDIR,
# INCLUDEDIR} for everything installed below (framework/core, framework/hal,
# framework/runner's run_scripts). CMakePackageConfigHelpers: configure_package_config_file()/
# write_basic_package_version_file(), used once below to generate the
# Thorium CMake package a separate rig repo would find_package()/
# FetchContent this framework through.
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

# --- Which deployment this build is ---
# The framework half of this tree (framework/, instruments/) is portable and reaches
# its rig, DUT and suite only through file paths (see the layer comment further
# down, and framework/hal/README.md). Until now those paths were written out
# longhand as ${CMAKE_SOURCE_DIR}/rig, /dut and /suite in this file and in
# the runner's CMakeLists.txt, which made the portability structural but not
# *reachable*: the framework could be pointed elsewhere, and this repo had no
# way to point it.
#
# These three are that knob. A deployment is a directory triple -- which bench,
# which device, which suite of scripts -- and everything else derives from it:
# hal's four rig files below, the criteria variant tables, the test catalog,
# and which tests/ directories the layer test targets glob.
#
# The default triple is this repo's own bench, so an ordinary configure is
# unchanged. dev/ is the second one: a PC and one instrument, no switching
# hardware and no DUT, for developing an instrument driver against real
# hardware at a desk -- see dev/README.md. Select it with a preset
# (cmake --list-presets) or by hand:
#
#     cmake -B build/dev -DTHORIUM_RIG_DIR=dev/rig \
#         -DTHORIUM_DUT_DIR=dev/dut -DTHORIUM_SUITE_DIR=dev/suite
#
# PATH rather than STRING so a relative value is resolved against the build
# directory the way CMake resolves every other path variable, and so the GUIs
# offer a directory picker.
#
# One deployment per build directory, deliberately -- not two sets of targets
# in one build. Every one of the derived facts above is a compile definition
# baked into hal, core and the scripts library at *their* compile time (see
# framework/hal/README.md's note on why an installed libhal.a belongs to one rig),
# so two deployments in one build would need two copies of every library, and
# `run_scripts` would need a name per deployment. Two build directories cost
# nothing and keep the binary's identity unambiguous, which for a thing that
# writes traceability headers is the property that matters.
set(THORIUM_RIG_DIR "${CMAKE_SOURCE_DIR}/rig" CACHE PATH
    "This deployment's rig data: instrument.inc, devices.inc, wiring.inc, active_instruments.hpp")
set(THORIUM_DUT_DIR "${CMAKE_SOURCE_DIR}/dut" CACHE PATH
    "This deployment's DUT data: adapter.inc and the criteria_<variant>.inc tables")
set(THORIUM_SUITE_DIR "${CMAKE_SOURCE_DIR}/suite" CACHE PATH
    "This deployment's suite: scripts/, test_catalog.inc, scripts.hpp, prelude.hpp")

foreach(_thorium_deployment_var THORIUM_RIG_DIR THORIUM_DUT_DIR THORIUM_SUITE_DIR)
    if(NOT IS_DIRECTORY "${${_thorium_deployment_var}}")
        message(FATAL_ERROR
            "${_thorium_deployment_var}='${${_thorium_deployment_var}}' is not a directory. "
            "It must name one of this repository's deployment directories (rig/, dut/, "
            "suite/ for the bench; dev/rig, dev/dut, dev/suite for the desk bench) or an "
            "equivalent supplied from outside -- see dev/README.md.")
    endif()
endforeach()

message(STATUS "Deployment: rig=${THORIUM_RIG_DIR} dut=${THORIUM_DUT_DIR} suite=${THORIUM_SUITE_DIR}")

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
# sees is still a real struct with real members (see core/criteria/active_criteria.hpp
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
#
# A cache entry rather than a plain set(), because the list is a property of the
# DUT rather than of this repository: which tolerance tables exist is decided by
# whoever writes THORIUM_DUT_DIR's criteria_*.inc files, and the dev deployment
# has exactly one where the bench has three. The default is still spelled out
# longhand here -- a glob over the DUT directory would discard the thing worth
# reviewing, and the order matters (it is the order --criteria= indices mean).
set(THORIUM_KNOWN_CRITERIA_VARIANTS "production;stress;aged" CACHE STRING
    "Which criteria/tolerance tables THORIUM_DUT_DIR supplies, in index order")

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
# two generated files' paths; framework/core and framework/runner hand those to the translation
# units that read them, as compile definitions, exactly the way
# THORIUM_PRODUCTION_CRITERIA and THORIUM_TEST_CATALOG already work.
thorium_generate_criteria_variants(
    "${THORIUM_KNOWN_CRITERIA_VARIANTS}"
    "${THORIUM_DUT_DIR}"
    "${CMAKE_BINARY_DIR}/generated/thorium"
)

message(STATUS "Criteria variants: ${THORIUM_KNOWN_CRITERIA_VARIANTS} (default: ${THORIUM_CRITERIA_VARIANT})")

# --- Content revision, for the run logs' traceability header ---
# Both log streams record which revision of this deployment's *content* -- the
# test scripts, the DUT profile and criteria, the rig's wiring -- a run was
# performed against (see core::RunInfo in framework/core/include/core/journal/journal.hpp).
# That is a different question from THORIUM_FRAMEWORK_VERSION, which versions
# framework/: this versions what is being tested and how, which is the half that
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
# framework/core/src/journal.cpp makes for the rest of the header.
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
# framework (framework/), and this one deployment's content -- one rig testing
# one DUT (rig/, dut/, dsl/, suite/, all at the top level rather than under
# framework/). A separate rig -- its own repo, its own bench, its own DUT -- would
# reuse framework/ unchanged and write its own rig/dut/dsl/suite instead; nothing
# here should ever make framework/ content depend on anything outside it.
#
# A deployment brings content, not a program. There used to be an app/ in that
# list -- the runner, main.cpp and the "scripts" library -- and its presence at
# the top level said, without meaning to, that a second deployment was expected
# to write a main() of its own. It never was: app/ held nothing DUT-specific,
# and the only thing tying it to this deployment was two compile definitions
# naming files under suite/. That is the same parameterisation framework/hal already
# uses for its four rig files, so the runner moved to framework/runner, where it is
# packaged and versioned with core and hal. See framework/runner/CMakeLists.txt.
#
# core  : foundational utilities, no dependencies on other layers
# hal   : generic hardware abstraction layer (switching fabric, wiring,
#         the measure/apply/safing mechanism, and the API instrument
#         drivers are written against) -- depends on core, and on this rig's
#         data below, but knows nothing rig-specific itself. Two targets:
#         hal, which drivers compile against, and hal_rig, which compiles
#         against the drivers -- see framework/hal/README.md
# instruments
#       : one directory per instrument driver, each independently
#         packageable, each declaring its own target and carrying its own
#         tests -- framework content, as portable as framework/ is, but not
#         part of the same release. A directory here is zipped, versioned and
#         handed to another rig on its own (see instruments/README.md), and
#         that is why it sits at the top level: placement is what says what
#         ships together, and a driver does not ship with the framework
# rig   : this rig's data only (which instruments it has, how they're
#         wired, what to call them) -- content only, no library of its own,
#         the same shape dut/ uses for DUT-specific data; see rig/README.md.
#         Its add_subdirectory declares no library, only rig_tests: the
#         integration tests that need more than one instrument, or this
#         rig's own globals, and so belong to neither generic hal nor any
#         single driver directory.
# dut   : DUT-specific data only (adapter profile + criteria variants, both
#         flat macro-driven .inc files -- see dut/README.md) -- no library
#         of its own, just tests over that data linked against hal
# dsl   : this DUT's test vocabulary -- methods named after the test
#         description document's own terms, for traceability between the
#         requirement/test/development disciplines -- depends on dut + hal
#         + core, the same reason it lives alongside them rather than under
#         framework/
# runner: the runner (main.cpp, its argument parser) plus the "scripts"
#         library it runs and the run_scripts binary both produce -- framework,
#         under framework/ with core and hal. What it runs comes from the three
#         deployment directories as file paths: the scripts themselves, the test
#         catalog, scripts.hpp and prelude.hpp from suite/, and the
#         criteria/tolerance variants from dut/, since tolerances are a property
#         of the DUT being tested (see dut/README.md)

# --- This rig's data for the generic hal library ---
# framework/hal is a generic instrument-driver + switching-fabric library with no
# instruments plugged into it at all -- it needs a rig to supply which
# instruments exist, how they're wired, and what to call them, as three file
# paths (see framework/hal/CMakeLists.txt's own comment). This repo supplies them
# from whichever deployment THORIUM_RIG_DIR names above; a separate rig repo
# pulling hal in later would set these same four variables pointing at its own
# rig/-equivalent instead -- see rig/README.md.
#
# Derived from THORIUM_RIG_DIR rather than being four knobs of their own, and
# FORCE'd because of it: the four are one fact (which bench), and a build with
# three of them from one rig and the fourth from another is not a configuration
# anyone wants to be able to reach by accident. Point THORIUM_RIG_DIR somewhere
# else to change them. A rig whose files are not named the way these four lines
# expect is a rig consuming this framework from outside, which sets the four
# directly before hal configures and never reaches this file at all.
set(THORIUM_ACTIVE_INSTRUMENTS "${THORIUM_RIG_DIR}/active_instruments.hpp" CACHE FILEPATH "" FORCE)
set(THORIUM_INSTRUMENT_TABLE "${THORIUM_RIG_DIR}/instrument.inc" CACHE FILEPATH "" FORCE)
set(THORIUM_WIRING_TABLE "${THORIUM_RIG_DIR}/wiring.inc" CACHE FILEPATH "" FORCE)
set(THORIUM_DEVICE_TABLE "${THORIUM_RIG_DIR}/devices.inc" CACHE FILEPATH "" FORCE)

# --- Which instrument drivers are available to build against ---
# The fourth thing a rig supplies framework/hal, alongside the three files above:
# where its instrument drivers live. One directory per driver, each with its
# own CMakeLists.txt declaring its own target -- see instruments/README.md and
# framework/hal/CMakeLists.txt's own comment on why the drivers are configured
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

# --- ...unless the deployment says which of them it wants ---
# The glob above settles availability, and for the bench deployment that is the
# whole story: a driver no INSTRUMENT() row names contributes no global, no
# hal::InstrumentId enumerator and no object code, so building all five costs
# nothing but compile time.
#
# It does cost one thing, and a second deployment is what made it visible: a
# driver package's *tests* are object code, and every one of them constructs its
# driver with an id from this bench's instrument list -- InstrumentId::Osc1 in
# instruments/dso8064a/tests, ::AcP1 in ac6834b, ::Ser1 in racal1260. Those
# enumerators do not exist on a bench that has no scope, no AC source and no
# serial port, so a deployment declaring one instrument cannot build the other
# four packages' tests at all. That is a real defect against the claim in
# instruments/README.md that each directory is independently packageable, and it
# is not fixed here -- see dev/README.md for what fixing it looks like.
#
# What this list does is let a deployment stop paying for packages it has no
# instrument for. Empty means the glob, so the bench deployment and any existing
# build directory are unchanged; a non-empty list names directories under
# instruments/ and nothing else is configured.
set(THORIUM_INSTRUMENT_PACKAGES "" CACHE STRING
    "Which instruments/<dir> packages this deployment builds; empty means all of them")

set(THORIUM_INSTRUMENT_DIRS "")

if(THORIUM_INSTRUMENT_PACKAGES)
    foreach(_thorium_package IN LISTS THORIUM_INSTRUMENT_PACKAGES)
        if(NOT EXISTS "${CMAKE_SOURCE_DIR}/instruments/${_thorium_package}/CMakeLists.txt")
            message(FATAL_ERROR
                "THORIUM_INSTRUMENT_PACKAGES names '${_thorium_package}', but "
                "instruments/${_thorium_package}/CMakeLists.txt does not exist.")
        endif()

        list(APPEND THORIUM_INSTRUMENT_DIRS "${CMAKE_SOURCE_DIR}/instruments/${_thorium_package}")
    endforeach()
else()
    foreach(_thorium_manifest IN LISTS _thorium_instrument_manifests)
        get_filename_component(_thorium_dir "${_thorium_manifest}" DIRECTORY)
        list(APPEND THORIUM_INSTRUMENT_DIRS "${_thorium_dir}")
    endforeach()
endif()

add_subdirectory(framework/core)
add_subdirectory(framework/hal)

# --- Package export: core + hal + the drivers as the reusable "Thorium" framework ---
# core, hal, hal_rig and every instrument driver register themselves into the
# ThoriumTargets export set via their own install(TARGETS ... EXPORT
# ThoriumTargets ...) calls (see framework/core/CMakeLists.txt,
# framework/hal/CMakeLists.txt, instruments/*/CMakeLists.txt) -- this is what turns
# that into an actual find_package()-able CMake package: ThoriumTargets.cmake
# (the compiled targets, namespaced Thorium::core/Thorium::hal/...) plus
# ThoriumConfig.cmake/ThoriumConfigVersion.cmake (what find_package(Thorium)
# actually looks for).
#
# One thing this does NOT give a consumer: a hal that works for their rig
# out of the box. hal's THORIUM_ACTIVE_INSTRUMENTS/THORIUM_INSTRUMENT_TABLE/
# THORIUM_WIRING_TABLE/THORIUM_DEVICE_TABLE (see framework/hal/CMakeLists.txt) are baked in via
# #include at hal's OWN compile time, so an installed libhal.a is only ever
# built for the one rig that supplied those three paths when IT was
# configured -- this repo's rig/, in this build. A different rig consuming
# this package needs to build hal from source against its own rig/-
# equivalent (FetchContent/add_subdirectory, setting those same three
# variables before hal configures), not link against a prebuilt hal from
# somewhere else's install -- see framework/hal/README.md.
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

# After framework/hal, not before: rig/ declares no library of its own, and the one
# target it does declare -- rig_tests -- links hal_rig, which does not exist
# until framework/hal has been configured (and, before that, the drivers it links).
#
# Two arguments rather than one, because a deployment directory need not sit
# below this one: add_subdirectory() can only infer a binary directory for a
# source directory inside the tree, and THORIUM_RIG_DIR is free to point
# outside it. Naming the binary directory after the layer rather than after the
# path also keeps the build layout identical whichever deployment is selected,
# so build/dev/rig and build/debug/rig hold the same thing for their own rigs.
add_subdirectory(${THORIUM_RIG_DIR} ${CMAKE_BINARY_DIR}/rig)

add_subdirectory(${THORIUM_DUT_DIR} ${CMAKE_BINARY_DIR}/dut)
#add_subdirectory(dsl)

# Last, and after the two deployment directories above rather than beside
# framework/core and framework/hal, despite being framework like them: it globs
# THORIUM_SUITE_DIR for the scripts to compile and links hal_rig, so both the
# content and the target have to exist first. Where a framework/ directory is
# configured has never been about where it sits on disk.
add_subdirectory(framework/runner)
