#
# Resolves wxWidgets for framework/ui: vcpkg first, a copy the machine already
# has second, nothing at all third.
#
# ---------------------------------------------------------------------------
# Why vcpkg rather than the arrangement cmake/FetchGTest.cmake uses
# ---------------------------------------------------------------------------
# GoogleTest is vendored into third_party/ and compiled by this project's own
# compiler alongside the code it tests, and that works because it is 4.5 MB
# across 252 files. wxWidgets trimmed to what a library build actually needs is
# 121 MB across 6,752 files -- a repository an order of magnitude larger, in its
# history forever -- and unlike GoogleTest it could never be compiled by this
# project's compiler anyway: see the note at the bottom of this file.
#
# The three ways a machine could otherwise get wxWidgets are brew, apt and
# vcpkg, and only the last is available on all three platforms this framework
# targets. "brew install wxwidgets / apt install libwxgtk3.2-dev / vcpkg install
# wxwidgets" is not a portable instruction, it is three instructions and a
# guess about which one a reader needs -- and two of them cannot be followed on
# a Windows bench at all.
#
# So the dependency is declared once, in framework/ui/vcpkg.json, and vcpkg
# satisfies it the same way on Windows, Linux and macOS. The manifest pins a
# builtin-baseline, so the version is a property of this repository rather than
# of when somebody last ran `git pull` in their vcpkg checkout -- a bench
# console that builds differently in March than it did in February is a console
# nobody can say anything about.
#
# ---------------------------------------------------------------------------
# Two halves, and the ordering between them is not negotiable
# ---------------------------------------------------------------------------
# A toolchain file is read by project(), and only by project(). So this file has
# to be include()d *before* project() in order to set one, and the find_package
# it enables cannot run until after. Hence a file that does something on
# inclusion and then offers a macro for the rest:
#
#     include(WxWidgets)          # before project() -- points at vcpkg
#     project(thorium_ui ...)
#     thorium_resolve_wxwidgets() # after  project() -- finds the toolkit
#
# A macro rather than a function, deliberately. FindwxWidgets hands back a
# wxWidgets_USE_FILE whose job is to call include_directories(), and inside a
# function that call would scope the include paths to the function and vanish
# on return -- leaving a configure that succeeds and a compile that cannot find
# wx/app.h.
#

#
# Nothing is imposed on a caller who has already chosen a toolchain: an explicit
# -DCMAKE_TOOLCHAIN_FILE on the command line, or a cross-compiling setup, is a
# decision that outranks this convenience and is left alone.
#
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE AND NOT DEFINED ENV{CMAKE_TOOLCHAIN_FILE})
    #
    # VCPKG_ROOT is the variable vcpkg's own documentation tells people to set,
    # so it is the one looked for. The fallbacks after it are the two places a
    # checkout most often ends up next to this repository -- enough to make a
    # fresh clone work without ceremony, and few enough not to be a search path
    # that finds something surprising.
    #
    # Deliberately NOT a checkout inside this repository. A vcpkg tree is
    # several gigabytes once it has built anything (3.5 GB on the machine this
    # was written on, of which 2.6 GB is buildtrees) and is shared between every
    # project on a developer's machine -- so a per-repository copy is both waste
    # and a second thing to keep current. One checkout, many projects, is what
    # vcpkg is designed for.
    #
    set(THORIUM_VCPKG_CANDIDATES
        "$ENV{VCPKG_ROOT}"
        "${CMAKE_CURRENT_LIST_DIR}/../../vcpkg"
        "${CMAKE_CURRENT_LIST_DIR}/../../../vcpkg")

    foreach(candidate IN LISTS THORIUM_VCPKG_CANDIDATES)
        if(candidate AND EXISTS "${candidate}/scripts/buildsystems/vcpkg.cmake")
            get_filename_component(THORIUM_VCPKG_ROOT "${candidate}" ABSOLUTE)

            set(CMAKE_TOOLCHAIN_FILE "${THORIUM_VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
                CACHE FILEPATH "vcpkg toolchain, found by cmake/WxWidgets.cmake")

            message(STATUS "vcpkg: ${THORIUM_VCPKG_ROOT}")

            break()
        endif()
    endforeach()

    if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
        #
        # Not fatal. A machine with a wxWidgets installed some other way still
        # builds, and a CI job that only wants the protocol tests wants neither
        # -- see THORIUM_UI_REQUIRE_WX below. Said out loud rather than silently
        # skipped, because "no window was built" is otherwise discovered at the
        # end of a build rather than at the start of one.
        #
        message(STATUS "vcpkg: not found -- set VCPKG_ROOT to build the window from "
                       "framework/ui/vcpkg.json, or install wxWidgets some other way")
    endif()
endif()

#
# ---------------------------------------------------------------------------
# Windows: the triplet has to agree with the compiler, and by default it does
# not
# ---------------------------------------------------------------------------
# vcpkg's default triplet on a Windows host is x64-windows, and x64-windows
# means MSVC. This project is compiled by MinGW there -- framework/ui/
# CMakePresets.json names C:/mingw64, because the framework's own build already
# requires GCC 16 and putting a second compiler on a bench in order to build one
# window is a cost nobody agreed to.
#
# Left alone, those two facts produce a vcpkg that quietly builds an MSVC
# wxWidgets and a compiler that cannot link a byte of it. The import libraries
# are in the wrong format, and nothing says so until link time, where it arrives
# looking like a missing symbol rather than like a mismatched toolchain.
#
# So the preset names x64-mingw-dynamic as well, and this is the check that says
# so when a configure arrives without it. It deliberately does not *set* the
# triplet: a triplet is a host fact, it belongs beside the compiler path in
# CMakePresets.json for the same reason the top-level CMakeLists.txt stopped
# naming compilers, and inferring one here from the shape of a path is how a
# build ends up disagreeing with itself in a second place.
#
# Before project() rather than after, where CMAKE_CXX_COMPILER_ID would make the
# test exact: the toolchain file installs the manifest during project(), so a
# check that waited for the compiler ID would deliver its verdict on the far
# side of a wxWidgets build that then has to be thrown away.
#
if(CMAKE_HOST_WIN32
   AND CMAKE_TOOLCHAIN_FILE MATCHES "vcpkg"
   AND CMAKE_CXX_COMPILER MATCHES "(gcc|g\\+\\+|mingw)"
   AND NOT VCPKG_TARGET_TRIPLET MATCHES "mingw")
    message(FATAL_ERROR
        "This configure names a GCC compiler (${CMAKE_CXX_COMPILER}) but the "
        "vcpkg triplet is '${VCPKG_TARGET_TRIPLET}', which builds wxWidgets "
        "with MSVC. The two cannot be linked together. Configure through the "
        "preset -- cmake --preset windows-ui -- or pass "
        "-DVCPKG_TARGET_TRIPLET=x64-mingw-dynamic and "
        "-DVCPKG_HOST_TRIPLET=x64-mingw-dynamic yourself.")
endif()

#
# Finds the toolkit and reports what it found. Sets, in the caller's scope:
#
#   THORIUM_UI_HAVE_WX        whether a window can be built at all
#   THORIUM_UI_WX_LIBRARIES   what to link it against
#
macro(thorium_resolve_wxwidgets)
    #
    # Cleared first: this is a macro, so the variable is the caller's and a
    # value left over from a previous configure of the same build tree would
    # otherwise decide the outcome before either find_package runs.
    #
    set(THORIUM_UI_HAVE_WX OFF)

    #
    # CONFIG mode first, module mode second, and the order is load-bearing
    # rather than stylistic.
    #
    # vcpkg's wxwidgets port ships CMake config targets (wx::core, wx::base) and
    # deliberately moves wx-config out of bin/ into tools/wxwidgets/, where
    # find_program does not look. So the plain FindwxWidgets module cannot see a
    # vcpkg copy at all -- and on a machine that also has a system wxWidgets, it
    # does not fail, it quietly finds *that one* instead. The build would then
    # succeed against a version the manifest never asked for, which makes the
    # pinned builtin-baseline decorative and the whole argument above false.
    #
    # Module mode is still needed as the fallback: it is how a system copy that
    # ships no CMake config is found, which is most of them.
    #
    find_package(wxWidgets CONFIG QUIET)

    if(wxWidgets_FOUND AND TARGET wx::core)
        set(THORIUM_UI_WX_LIBRARIES wx::core wx::base)
        set(THORIUM_UI_HAVE_WX ON)

        message(STATUS "wxWidgets: ${wxWidgets_VERSION} (CMake package)")
    else()
        find_package(wxWidgets QUIET COMPONENTS core base)
    endif()

    if(NOT THORIUM_UI_HAVE_WX AND wxWidgets_FOUND)
        include(${wxWidgets_USE_FILE})

        set(THORIUM_UI_WX_LIBRARIES ${wxWidgets_LIBRARIES})
        set(THORIUM_UI_HAVE_WX ON)

        message(STATUS "wxWidgets: ${wxWidgets_VERSION_STRING} (wx-config)")
    endif()

    if(NOT THORIUM_UI_HAVE_WX)
        #
        # No toolkit: the protocol library and its tests are still built, the
        # window is not.
        #
        # This is for CI, and it is not a consolation prize. What most needs
        # checking on every commit is the *contract* -- that this program can
        # still read what run_scripts emits -- and that check needs a built
        # run_scripts and no display, no toolkit and no window at all. A CI job
        # that had to build wxWidgets to discover a renamed JSON field would be
        # a check nobody keeps.
        #
        # THORIUM_UI_REQUIRE_WX turns that into an error, for the build that is
        # supposed to produce a console and must not quietly produce half of
        # one.
        #
        set(THORIUM_UI_HAVE_WX OFF)

        if(THORIUM_UI_REQUIRE_WX)
            message(FATAL_ERROR
                "wxWidgets was not found and THORIUM_UI_REQUIRE_WX is ON. Set VCPKG_ROOT to a "
                "vcpkg checkout -- framework/ui/vcpkg.json declares the dependency and pins its "
                "version -- or install wxWidgets by some other means.")
        endif()

        message(STATUS "wxWidgets: not found -- building the protocol library and its tests, no window")
    endif()
endmacro()

#
# ---------------------------------------------------------------------------
# The fact underneath all of this
# ---------------------------------------------------------------------------
# framework/ui is a separate CMake project, built by a different compiler from
# the rest of framework/, and that is not a preference. The top-level
# CMakeLists.txt refuses to configure with anything but GCC 16; wxWidgets' Cocoa
# port includes the macOS SDK's CoreGraphics headers; those headers are written
# in Clang's blocks extension (the `^` block-pointer syntax); and GCC does not
# implement blocks. A file whose entire contents are
#
#     #include <CoreGraphics/CoreGraphics.h>
#
# fails to parse under g++-16 with "expected unqualified-id before '^' token"
# and compiles clean under Apple Clang.
#
# No flag fixes that, which is why this module is included by a project of its
# own rather than by the top-level build, and why nothing here tries to hand
# wxWidgets to the framework's compiler.
#
