#
# The bench console -- framework, and a separate CMake project.
#
# Both halves of that sentence matter, and they are answers to different
# questions.
#
# It is under framework/ because that is what it is: it names no DUT point, no
# criterion and no instrument, and works against any installed suite. The
# position is the same argument framework/runner/CMakeLists.txt makes for having
# been moved here out of a top-level app/ -- a deployment reading the layer list
# and seeing rig/, dut/, suite/ and ui/ at one level could reasonably conclude
# it was expected to bring a window of its own. It is not. It brings content.
#
# It is nevertheless NOT built by the framework's build, is not reachable from
# its presets, and is not add_subdirectory'd from the top-level CMakeLists.txt.
# `cmake -S framework/ui -B build/ui` is the whole entry point. This is the file
# where somebody would first be tempted to undo that, so: the framework refuses
# to configure with anything but GCC 16, and on macOS GCC cannot compile a GUI
# toolkit at all -- wxWidgets' Cocoa port includes SDK headers written in
# Clang's blocks extension, which GCC does not implement. A file containing only
# `#include <CoreGraphics/CoreGraphics.h>` does not parse under g++-16. That is
# not a warning to suppress or an ABI to be careful about; it is a thing that
# cannot be done. See README.md §1 and cmake/WxWidgets.cmake.
#
# So: ordinary compiler, ordinary flags, no reflection, no framework libraries
# linked. What crosses between the two is the run_scripts binary's stdout.
#
cmake_minimum_required(VERSION 3.24)

#
# Before project(), because that is when a toolchain file is read and
# cmake/WxWidgets.cmake's first half sets one. Its own comment explains the
# split and why the second half is a macro rather than a function.
#
# Self-locating: this is a top-level project of its own, so CMAKE_SOURCE_DIR
# here is this directory and not the repository.
#
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/../../cmake")

include(WxWidgets)

project(thorium_ui LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

#
# The same default the top-level CMakeLists.txt sets, and for one extra reason
# that only shows up on Windows.
#
# An empty CMAKE_BUILD_TYPE is merely untidy on macOS and Linux -- no -O, no -g,
# and a shared library found by rpath either way. On Windows it splits the build
# in half. vcpkg's toolchain hangs a post-build step on every executable that
# copies the DLLs it needs next to it, and the directory it copies them from is
# chosen by a generator expression:
#
#     --installed-bin-dir=.../${VCPKG_TARGET_TRIPLET}$<$<CONFIG:Debug>:/debug>/bin
#
# With no build type that expression is false, so the *release* DLLs are copied
# -- while the linker, asked for an imported target with no configuration to
# match, takes the first one wxWidgets' vcpkg config offers, which is the debug
# one. The executable then links against wxmsw33ud and is handed wxmsw33u, and
# refuses to start with 0xc0000135 (STATUS_DLL_NOT_FOUND) and no other clue.
#
# Debug rather than Release because this is the tree the tests are configured
# against: THORIUM_UI_TEST_BINARY below is pointed at the framework's debug
# build. An explicit -DCMAKE_BUILD_TYPE=Release still wins, as it does upstairs.
#
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

# C++20, not 26. Nothing here needs reflection or contracts -- the parts of this
# program that benefit from them are in the other build -- and pinning the
# lowest standard that does the job is what lets this compile with whatever
# compiler each of the three platforms ships, which is the entire point of the
# split. If this ever needs a newer standard, that is a decision to make on its
# own merits rather than one inherited from the framework.

if(MSVC)
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# -Werror is deliberately NOT set here, unlike the framework, and the difference
# is not laziness. The framework's warnings are all its own; this project
# compiles wxWidgets headers, whose warning cleanliness is a property of
# somebody else's release schedule. A toolkit update must not be able to make
# the bench console unbuildable on the morning of a test campaign.

# --- wxWidgets -----------------------------------------------------------
# Declared once, in vcpkg.json beside this file, and satisfied the same way on
# Windows, Linux and macOS. cmake/WxWidgets.cmake has the argument for vcpkg
# over the three-package-managers-and-a-guess alternative, and over vendoring
# the source the way third_party/googletest is vendored.
#
# A copy the machine already has is used if there is one, so nobody who has
# already installed wxWidgets is made to build it again.
option(THORIUM_UI_REQUIRE_WX "Fail the configure if wxWidgets is not found, instead of building only the protocol library" OFF)

thorium_resolve_wxwidgets()

# --- The protocol layer, which has no toolkit in it ----------------------
# A library of its own, and this is the load-bearing structural decision in
# this file. Everything that decides what run_scripts is asked and what its
# answers mean lives here, with no wx header in sight -- which is what lets
# tests/test_protocol.cpp drive a real run_scripts and assert on what comes
# back, with no display, no window and no toolkit installed.
#
# That matters more than testability in general. This library is one half of a
# contract between two processes, and the other half is generated from
# annotations in framework/runner/src/main.cpp. A change there that this cannot
# read has to fail somewhere, and the only useful somewhere is a test that runs
# the actual binary.
add_library(thorium_ui_protocol STATIC
    src/protocol/json.cpp
    src/protocol/options.cpp
    src/protocol/events.cpp
    src/protocol/suite.cpp
    src/protocol/command.cpp
)

target_include_directories(thorium_ui_protocol PUBLIC src)

# --- The window ----------------------------------------------------------
set(THORIUM_SUITE_PREFIX "/opt/thorium" CACHE PATH
    "Where installed suites are looked for by default (overridable with --prefix)")

if(THORIUM_UI_HAVE_WX)
    add_executable(thorium_ui WIN32 MACOSX_BUNDLE
        src/app/app.cpp
        src/app/frame.cpp
        src/app/options_dialog.cpp
        src/app/process.cpp
    )

    target_link_libraries(thorium_ui PRIVATE thorium_ui_protocol ${THORIUM_UI_WX_LIBRARIES})

    # WIN32/MACOSX_BUNDLE so this is a GUI application rather than a console one
    # on the two platforms that distinguish them -- without it, Windows opens a
    # console window behind the frame and macOS produces something that cannot
    # take focus or carry an icon.

    target_compile_definitions(thorium_ui PRIVATE
        THORIUM_SUITE_PREFIX="${THORIUM_SUITE_PREFIX}")
endif()

# --- Tests ---------------------------------------------------------------
# Built only when pointed at a run_scripts to drive. There is no fixture-only
# mode on purpose: a test that asserted on a hand-written copy of the event
# schema would keep passing after the schema changed, which is the one failure
# this layer cannot afford.
set(THORIUM_UI_TEST_BINARY "" CACHE FILEPATH
    "A built run_scripts for the tests to drive")

if(THORIUM_UI_TEST_BINARY)
    enable_testing()

    # GoogleTest from third_party/, the same vendored copy every other test
    # target in this repository uses -- but compiled by THIS project's compiler
    # rather than the framework's, which is the whole reason that library is
    # vendored as source instead of taken from a package manager. A binary one
    # would be built against the wrong standard library for one of the two
    # projects here; see cmake/FetchGTest.cmake.
    #
    # gtest_discover_tests comes with it, so these report one ctest entry per
    # TEST case exactly like core_tests and hal_tests do, rather than one entry
    # per binary. That matters for more than tidiness: a single opaque entry
    # turns ninety-odd assertions into one pass/fail, and a run that lost a
    # whole section would look identical to one that did not.
    include(FetchGTest)

    add_executable(thorium_ui_protocol_tests tests/test_protocol.cpp)

    target_link_libraries(thorium_ui_protocol_tests
        PRIVATE thorium_ui_protocol GTest::gtest_main)

    target_compile_definitions(thorium_ui_protocol_tests PRIVATE
        THORIUM_UI_TEST_BINARY="${THORIUM_UI_TEST_BINARY}")

    gtest_discover_tests(thorium_ui_protocol_tests)

    # --- ui::ChildProcess, headless ---------------------------------------
    # A console binary, not a GUI one, because everything ChildProcess touches
    # (wxProcess, wxTimer, the event loop) is wxBase. So this runs on a build
    # machine with no display and covers the one part of the window that cannot
    # be checked by pressing Run: what happens when a run does not finish.
    #
    # Needs a toolkit, unlike the protocol tests, which is why it is inside the
    # wx guard and they are not. GTest::gtest rather than gtest_main: the
    # binary brings its own main() so that wxEntry can initialise the app
    # before any test body runs (see tests/test_process.cpp).
    if(THORIUM_UI_HAVE_WX)
        add_executable(thorium_ui_process_tests tests/test_process.cpp
            src/app/process.cpp)

        target_link_libraries(thorium_ui_process_tests
            PRIVATE thorium_ui_protocol GTest::gtest ${THORIUM_UI_WX_LIBRARIES})

        target_compile_definitions(thorium_ui_process_tests PRIVATE
            THORIUM_UI_TEST_BINARY="${THORIUM_UI_TEST_BINARY}")

        gtest_discover_tests(thorium_ui_process_tests)
    endif()
endif()
