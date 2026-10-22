#
# The bench-console launcher -- one small program, the same on every platform.
#
# It is under framework/ for the same reason framework/ui was (deleted; see
# framework/webui/README.md's "History"): it names no DUT point, no
# criterion and no instrument, and drives whatever console server is installed
# alongside it. Part of the ordinary build, beside framework/webui, on every
# host.
#
# Everything in src/ is portable C++ and one HTTP client (cpp-httplib, the
# server's own). What is not portable is behind src/platform.hpp -- the
# single-instance lock, the process group that dies with the launcher, finding
# Chrome, showing an error -- and is split by family, not by product: Windows
# in src/windows/ and POSIX in src/posix/, where the few things macOS and
# Linux differ on are one file each (system_macos.cpp, system_linux.cpp).
# See README.md.
#
# The standard and the warnings are the build's own (C++26, -Werror, set at
# the top level).
#
include(FetchHttplib)

if(WIN32)
    set(LAUNCHER_PLATFORM_SOURCES src/windows/platform.cpp)
elseif(APPLE)
    set(LAUNCHER_PLATFORM_SOURCES src/posix/platform.cpp src/posix/profile_lock.cpp src/posix/system_macos.cpp)
elseif(UNIX)
    set(LAUNCHER_PLATFORM_SOURCES src/posix/platform.cpp src/posix/profile_lock.cpp src/posix/system_linux.cpp)
else()
    message(FATAL_ERROR "framework/launcher has no platform file for this host (see README.md)")
endif()

# The platform seam as a library, so the tests link the same compiled objects
# the launcher does rather than a copy of their own.
add_library(launcher_platform STATIC ${LAUNCHER_PLATFORM_SOURCES})
target_include_directories(launcher_platform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
if(UNIX)
    # profile_lock.hpp, for the test of the stale-lock check.
    target_include_directories(launcher_platform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src/posix)
endif()

# WIN32: the GUI subsystem, so that starting the launcher from a shortcut does
# not open a console window. Ignored elsewhere.
add_executable(thorium_launcher WIN32
    src/main.cpp
    src/config.cpp
    src/console_client.cpp
)
target_link_libraries(thorium_launcher PRIVATE launcher_platform httplib::httplib)

if(WIN32)
    # shell32  : CommandLineToArgvW
    # user32   : MessageBoxW
    # advapi32 : the registry lookup for Chrome
    # ws2_32   : sockets, for httplib
    target_link_libraries(launcher_platform PUBLIC shell32 user32 advapi32 ws2_32)
    target_compile_definitions(launcher_platform PUBLIC UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_compile_definitions(thorium_launcher PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
endif()

# --- Tests -------------------------------------------------------------
# What each one catches is in README.md's "Testing". Built with the rest of
# the build's tests, under BUILD_TESTING_LAYERS; none of them needs GoogleTest.
if(NOT (BUILD_TESTING_LAYERS AND THORIUM_FRAMEWORK_TESTS) OR NOT UNIX)
    return()
endif()

add_executable(launcher_tests
    tests/main.cpp
    tests/test_profile_lock.cpp
)
target_link_libraries(launcher_tests PRIVATE launcher_platform)

add_test(NAME launcher.profile_lock COMMAND launcher_tests profile_lock)

# teardown.sh drives macOS tools (launchctl, mdfind, BSD ps); a Linux version of
# it is the first thing to write when this is first run on a Linux bench.
if(NOT APPLE)
    return()
endif()

add_test(NAME launcher.teardown
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tests/teardown.sh $<TARGET_FILE:thorium_launcher>
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/stand_in_server.py
)
set_tests_properties(launcher.teardown PROPERTIES
    SKIP_RETURN_CODE 77
    TIMEOUT 60
    # It starts a real Chrome window.
    RUN_SERIAL TRUE
)
