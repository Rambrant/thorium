#
# The bench-console launcher -- one small native program per platform, and a
# separate CMake project.
#
# It is under framework/ for the same reason framework/ui was (deleted; see
# framework/webui/README.md's "History"): it names no DUT point, no
# criterion and no instrument, and drives whatever console server is installed
# alongside it. It is NOT built by the framework's build, is not reachable
# from its presets, and is not add_subdirectory'd from the top-level
# CMakeLists.txt -- `cmake --preset windows-launcher` or `cmake --preset
# macos-launcher` from this directory is the whole entry point.
#
# src/ is the Windows launcher and macos/ the macOS one. They are two
# programs, not one made portable: each one's whole job is its platform's
# own API (a Job object or a watched process group, Shell_NotifyIcon or
# NSStatusItem), and what they share -- four command-line flags and three menu
# actions -- is smaller than any abstraction over them would be. See
# README.md.
#
cmake_minimum_required(VERSION 3.24)

project(thorium_launcher LANGUAGES CXX)

if(NOT WIN32 AND NOT APPLE)
    message(FATAL_ERROR
        "framework/launcher has a Windows launcher (src/) and a macOS one "
        "(macos/), and nothing for this platform yet. See README.md -- a "
        "Linux launcher would be a third small program, not a port of either.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# C++20, not 26: nothing here needs reflection or contracts, and pinning the
# lowest standard that does the job is what lets this build with whatever
# compiler is on hand.

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

if(MSVC)
    add_compile_options(/W4)
else()
    # -Werror, unlike framework/ui before it: everything this target compiles is either
    # this program's own code or the platform SDK's headers (Windows, or macOS's
    # AppKit), which are warning-clean under -Wall -Wextra -Wpedantic. There is no
    # third-party toolkit here whose release schedule could break the build out from
    # under this flag.
    add_compile_options(-Wall -Wextra -Wpedantic -Werror)
endif()

if(APPLE)
    enable_testing()
    add_subdirectory(macos)
    return()
endif()

add_executable(thorium_launcher WIN32
    src/main.cpp
    src/config.cpp
    src/single_instance.cpp
    src/process_job.cpp
    src/child_process.cpp
    src/browser_launch.cpp
    src/health_check.cpp
    src/rig_client.cpp
    src/tray.cpp
)

target_compile_definitions(thorium_launcher PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

if(MINGW)
    target_compile_options(thorium_launcher PRIVATE -municode)
    target_link_options(thorium_launcher PRIVATE -municode)
endif()

# shell32   : CommandLineToArgvW, Shell_NotifyIconW, the tray's popup menu
# user32    : windows, messages, the tray's message loop
# ws2_32    : the TCP health check (health_check.cpp)
# winhttp   : the "Safe the rig" HTTP call (rig_client.cpp)
# advapi32  : the registry lookup for Chrome (browser_launch.cpp)
target_link_libraries(thorium_launcher PRIVATE
    shell32
    user32
    ws2_32
    winhttp
    advapi32
)
