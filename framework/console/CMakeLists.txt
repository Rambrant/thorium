#
# The bench console's server -- framework, and part of the framework's own
# build, unlike framework/ui and framework/launcher.
#
# The difference from those two is not a style choice, it is the one framework/ui/README.md
# Sec.5 spent a paragraph on: wxWidgets cannot be built by this project's GCC
# on macOS (Cocoa's headers use Clang's block syntax), which is why
# framework/ui is a separate CMake project built by a separate compiler. This
# target has no toolkit in it -- cpp-httplib, checked in that same section,
# compiles clean under -freflection -fcontracts -Wall -Wextra -Wpedantic
# -Werror -- so there is nothing here that needs escaping the fence. It joins
# the ordinary add_subdirectory list in the top-level CMakeLists.txt and is
# compiled by whatever compiler configured the rest of framework/.
#
# It links neither core nor hal. Its job is the same one framework/ui/src/app/
# had: launch the installed run_scripts as a subprocess and relay what it
# says on stdout, never open the rig itself. See child_stream.hpp on why that
# subprocess is spawned as argv rather than as a shell string.
include(FetchHttplib)

add_executable(thorium_console
    src/main.cpp
    src/child_stream.cpp
    src/run_session.cpp

    # Reused, not copied -- see framework/ui/README.md Sec.5: "the protocol
    # layer ... survives untouched". These three never included a wx header
    # (framework/ui/src/protocol has none at all), so recompiling them here,
    # under this project's own compiler and flags, costs nothing and keeps
    # the JSON/manifest/command-line rules written down in exactly one place
    # while framework/ui/src/app still exists alongside this target.
    ${CMAKE_CURRENT_SOURCE_DIR}/../ui/src/protocol/json.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../ui/src/protocol/suite.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../ui/src/protocol/command.cpp
)

target_include_directories(thorium_console PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../ui/src
)

target_link_libraries(thorium_console PRIVATE httplib::httplib)

if(WIN32)
    target_link_libraries(thorium_console PRIVATE ws2_32)
    target_compile_definitions(thorium_console PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
endif()
