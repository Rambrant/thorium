#
# The macOS launcher -- a sibling program to the Windows one in ../src, not
# a port of it. The two share the command-line flags and the three menu
# actions and nothing else: see ../README.md's "Why one launcher per
# platform" for why there is no portability layer between them.
#
# Built by GCC, like the rest of Thorium, even though its menu bar item is
# AppKit: AppKit is reached through the Objective-C runtime's C API rather
# than its Clang-only headers. See objc.hpp for how, and for what that trades
# away -- the compiler no longer checks any AppKit call -- and tests/ for what
# checks them instead.
#

#
# Everything but main(), as a library, so the tests below link the same
# compiled objects the launcher does rather than a copy of their own.
add_library(launcher_macos STATIC
    objc.cpp
    config.cpp
    single_instance.cpp
    process_group.cpp
    health_check.cpp
    browser_launch.cpp
    rig_client.cpp
    status_item.cpp
)
target_include_directories(launcher_macos PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# AppKit is linked, not included: the runtime resolves every class by name at
# startup, and linking is what loads the framework those names live in.
target_link_libraries(launcher_macos PUBLIC
    "-framework AppKit"
)

add_executable(thorium_launcher main.cpp)
target_link_libraries(thorium_launcher PRIVATE launcher_macos)

# --- Tests -------------------------------------------------------------
# What each one catches, and why the menu and teardown tests skip rather than
# fail without a logged-in desktop, is in ../README.md's "Testing the macOS
# launcher". enable_testing() is in ../CMakeLists.txt, where ctest needs it.

add_executable(launcher_macos_tests
    tests/main.cpp
    tests/test_signatures.cpp
    tests/test_menu.cpp
)
target_link_libraries(launcher_macos_tests PRIVATE launcher_macos)

# CoreGraphics, for CGSessionCopyCurrentDictionary -- how test_menu.cpp tells
# a logged-in desktop from a headless runner.
target_link_libraries(launcher_macos_tests PRIVATE
    "-framework CoreGraphics"
    "-framework CoreFoundation"
)

add_test(NAME launcher_macos.signatures COMMAND launcher_macos_tests signatures)

add_test(NAME launcher_macos.menu COMMAND launcher_macos_tests menu)
set_tests_properties(launcher_macos.menu PROPERTIES SKIP_RETURN_CODE 77 RUN_SERIAL TRUE)

add_test(NAME launcher_macos.no_raw_runtime_calls
    COMMAND ${CMAKE_COMMAND}
        -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_raw_runtime_calls.cmake
)

add_test(NAME launcher_macos.teardown
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tests/teardown.sh $<TARGET_FILE:thorium_launcher>
)
set_tests_properties(launcher_macos.teardown PROPERTIES
    SKIP_RETURN_CODE 77
    TIMEOUT 60
    # It starts real Chrome windows; one at a time with the menu test.
    RUN_SERIAL TRUE
)
