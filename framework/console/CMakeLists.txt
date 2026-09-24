#
# The bench console's server -- framework, and part of the framework's own
# build, unlike framework/launcher.
#
# The difference from framework/launcher is not a style choice: see
# README.md's "History" section for why framework/ui (wxWidgets, a separate
# CMake project built by a separate compiler) is gone, and why this target has
# nothing in it that needs escaping the fence its predecessor needed. It links
# neither core nor hal -- its job is the same one framework/ui/src/app/ had:
# launch the installed run_scripts as a subprocess and relay what it says on
# stdout, never open the rig itself. See child_stream.hpp on why that
# subprocess is spawned as argv rather than as a shell string.
include(FetchHttplib)

#
# The protocol layer -- JSON, the manifest/suite model, and run_scripts'
# command-line vocabulary -- as its own library, not sources listed straight
# into thorium_console. Two consumers need it identically: the server and
# console_protocol_tests below, and a library is what lets both link the same
# compiled objects rather than each compiling protocol/*.cpp for itself.
add_library(console_protocol STATIC
    src/protocol/json.cpp
    src/protocol/suite.cpp
    src/protocol/command.cpp
    src/protocol/events.cpp
    src/protocol/options.cpp
)
target_include_directories(console_protocol PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

add_executable(thorium_console
    src/main.cpp
    src/child_stream.cpp
    src/run_session.cpp
)
target_link_libraries(thorium_console PRIVATE console_protocol httplib::httplib)

if(WIN32)
    target_link_libraries(thorium_console PRIVATE ws2_32)
    target_compile_definitions(thorium_console PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
endif()

# --- Tests -------------------------------------------------------------
# Registered with ctest the ordinary way, unlike framework/ui's
# thorium_ui_protocol_tests: this is not a separate CMake project, so there is
# no second `ctest --test-dir` for tools/run-ctest.sh to know about.
#
# Black-box against a real run_scripts, not hand-written fixtures, for the
# same reason framework/ui's test kept to that rule: a fixture asserting on a
# schema nobody produces would pass forever after the schema changed, which is
# the one failure this layer cannot afford -- it is the whole contract between
# two processes. $<TARGET_FILE:run_scripts> is this same build's own binary,
# so there is nothing to point at by hand the way THORIUM_CONSOLE_TEST_BINARY once
# had to be -- it also means run_scripts must actually exist by the time the
# test runs, hence add_dependencies below rather than trusting build order.
if(BUILD_TESTING_LAYERS)
    add_layer_tests(console_protocol console_protocol ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    target_compile_definitions(console_protocol_tests PRIVATE
        THORIUM_CONSOLE_TEST_BINARY="$<TARGET_FILE:run_scripts>"
    )
    add_dependencies(console_protocol_tests run_scripts)
endif()
