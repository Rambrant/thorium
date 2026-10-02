#
# The bench-console launcher -- one small native program per platform.
#
# It is under framework/ for the same reason framework/ui was (deleted; see
# framework/webui/README.md's "History"): it names no DUT point, no
# criterion and no instrument, and drives whatever console server is installed
# alongside it. Part of the ordinary build, beside framework/webui, on the two
# platforms it exists for -- the top-level CMakeLists.txt only adds it on
# Windows and macOS. It used to be a CMake project of its own, from when the
# macOS half needed a different compiler; it no longer does (see README.md's
# "Why one launcher per platform"), and a separate project was only one more
# build to forget after changing the console.
#
# src/ is the Windows launcher and macos/ the macOS one. They are two
# programs, not one made portable: each one's whole job is its platform's
# own API (a Job object or a watched process group, Shell_NotifyIcon or
# NSStatusItem), and what they share -- four command-line flags and three menu
# actions -- is smaller than any abstraction over them would be. See
# README.md.
#
# The standard and the warnings are the build's own (C++26, -Werror, set at
# the top level): everything compiled here is this program's code or the
# platform SDK's C headers, which are clean under them.
#
if(APPLE)
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
