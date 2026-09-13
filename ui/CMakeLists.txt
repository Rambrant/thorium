#
# The bench console -- a separate project, on purpose.
#
# This is NOT part of the framework build and is not reachable from its presets.
# `cmake -S ui -B build/ui` is the whole entry point. The reasoning is in
# README.md §1 and is worth restating in one line here, because this file is
# where somebody would first be tempted to undo it: the framework compiles with
# GCC 16 and -freflection -fcontracts -Werror -Wpedantic on every translation
# unit, and a GUI toolkit does not belong inside that fence -- on macOS it would
# also mean mixing Homebrew GCC's libstdc++ with a toolkit built against libc++.
#
# So: ordinary compiler, ordinary flags, no reflection, no framework libraries
# linked. What crosses between the two is the run_scripts binary's stdout.
#
cmake_minimum_required(VERSION 3.24)

project(thorium_ui LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

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
# A system copy if there is one, fetched and built if not. The fallback is what
# makes "clone and build" true on a machine nobody has prepared -- a bench PC,
# or CI -- and the system copy is what makes it fast everywhere else.
#
#   brew install wxwidgets            # macOS
#   sudo apt install libwxgtk3.2-dev  # Debian/Ubuntu
#   vcpkg install wxwidgets           # Windows
#
option(THORIUM_UI_FETCH_WX "Build wxWidgets from source when no system copy is found" ON)

find_package(wxWidgets QUIET COMPONENTS core base)

if(wxWidgets_FOUND)
    include(${wxWidgets_USE_FILE})
    set(THORIUM_UI_WX_LIBRARIES ${wxWidgets_LIBRARIES})
    set(THORIUM_UI_HAVE_WX ON)
    message(STATUS "wxWidgets: system copy (${wxWidgets_VERSION_STRING})")
elseif(THORIUM_UI_FETCH_WX)
    include(FetchContent)

    # A pinned tag, never a branch. A bench console that builds differently in
    # March than it did in February is a console nobody can say anything about,
    # and this program's whole job is to be believable about what a rig did.
    FetchContent_Declare(wxWidgets
        GIT_REPOSITORY https://github.com/wxWidgets/wxWidgets.git
        GIT_TAG        v3.2.6
        GIT_SHALLOW    TRUE)

    set(wxBUILD_SHARED OFF CACHE BOOL "" FORCE)

    FetchContent_MakeAvailable(wxWidgets)

    set(THORIUM_UI_WX_LIBRARIES wxcore wxbase)
    set(THORIUM_UI_HAVE_WX ON)
    message(STATUS "wxWidgets: fetched (v3.2.6)")
else()
    # No toolkit, and not allowed to fetch one: the protocol library and its
    # tests are still built, the window is not.
    #
    # This is for CI, and it is not a consolation prize. What most needs
    # checking on every commit is the *contract* -- that this program can still
    # read what run_scripts emits -- and that check needs a built run_scripts
    # and no display, no toolkit and no window at all. A CI job that had to
    # build wxWidgets to discover a renamed JSON field would be a check nobody
    # keeps.
    set(THORIUM_UI_HAVE_WX OFF)
    message(STATUS "wxWidgets: not found and THORIUM_UI_FETCH_WX=OFF -- protocol only, no window")
endif()

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
    "A built run_scripts for the protocol tests to drive")

if(THORIUM_UI_TEST_BINARY)
    enable_testing()

    add_executable(thorium_ui_protocol_tests tests/test_protocol.cpp)

    target_link_libraries(thorium_ui_protocol_tests PRIVATE thorium_ui_protocol)

    target_compile_definitions(thorium_ui_protocol_tests PRIVATE
        THORIUM_UI_TEST_BINARY="${THORIUM_UI_TEST_BINARY}")

    add_test(NAME ui_protocol COMMAND thorium_ui_protocol_tests)

    # --- ui::ChildProcess, headless ---------------------------------------
    # A console binary, not a GUI one, because everything ChildProcess touches
    # (wxProcess, wxTimer, the event loop) is wxBase. So this runs on a build
    # machine with no display and covers the one part of the window that cannot
    # be checked by pressing Run: what happens when a run does not finish.
    #
    # Needs a toolkit, unlike the protocol tests, which is why it is inside the
    # wx guard and they are not.
    if(THORIUM_UI_HAVE_WX)
        add_executable(thorium_ui_process_tests tests/test_process.cpp
            src/app/process.cpp)

        target_link_libraries(thorium_ui_process_tests
            PRIVATE thorium_ui_protocol ${THORIUM_UI_WX_LIBRARIES})

        target_compile_definitions(thorium_ui_process_tests PRIVATE
            THORIUM_UI_TEST_BINARY="${THORIUM_UI_TEST_BINARY}")

        add_test(NAME ui_process COMMAND thorium_ui_process_tests)
    endif()
endif()
