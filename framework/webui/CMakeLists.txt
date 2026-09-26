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
# into thorium_webui. Two consumers need it identically: the server and
# webui_protocol_tests below, and a library is what lets both link the same
# compiled objects rather than each compiling protocol/*.cpp for itself.
add_library(webui_protocol STATIC
    src/protocol/json.cpp
    src/protocol/suite.cpp
    src/protocol/command.cpp
    src/protocol/events.cpp
    src/protocol/options.cpp
)
target_include_directories(webui_protocol PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

#
# The console page, as the files a browser asks for. Compiled into
# thorium_webui by cmake/EmbedFiles.cmake -- see src/static_content.hpp on why
# embedded rather than served from a directory. Listed rather than globbed, so
# a new file is a change to this list and a reconfigure is never forgotten;
# index.html is also what GET / serves.
set(WEBUI_STATIC_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/static/index.html
    ${CMAKE_CURRENT_SOURCE_DIR}/static/console.css
    ${CMAKE_CURRENT_SOURCE_DIR}/static/js/main.js
    ${CMAKE_CURRENT_SOURCE_DIR}/static/js/dom.js
    ${CMAKE_CURRENT_SOURCE_DIR}/static/js/state.js
    ${CMAKE_CURRENT_SOURCE_DIR}/static/js/raw.js
    ${CMAKE_CURRENT_SOURCE_DIR}/static/js/results.js
    ${CMAKE_CURRENT_SOURCE_DIR}/static/js/catalog.js
    ${CMAKE_CURRENT_SOURCE_DIR}/static/js/header.js
    ${CMAKE_CURRENT_SOURCE_DIR}/static/js/logs.js
    ${CMAKE_CURRENT_SOURCE_DIR}/static/js/run.js
)
set(WEBUI_STATIC_CPP ${CMAKE_CURRENT_BINARY_DIR}/generated/static_content.cpp)
add_custom_command(
    OUTPUT  ${WEBUI_STATIC_CPP}
    COMMAND ${CMAKE_COMMAND}
            -DEMBED_ROOT=${CMAKE_CURRENT_SOURCE_DIR}/static
            "-DEMBED_INPUTS=${WEBUI_STATIC_FILES}"
            -DEMBED_OUTPUT=${WEBUI_STATIC_CPP}
            -P ${CMAKE_SOURCE_DIR}/cmake/EmbedFiles.cmake
    DEPENDS ${WEBUI_STATIC_FILES} ${CMAKE_SOURCE_DIR}/cmake/EmbedFiles.cmake
    COMMENT "Embedding the console page"
    VERBATIM
)

add_executable(thorium_webui
    src/main.cpp
    src/child_stream.cpp
    src/run_session.cpp
    ${WEBUI_STATIC_CPP}
)
target_link_libraries(thorium_webui PRIVATE webui_protocol httplib::httplib)

if(WIN32)
    target_link_libraries(thorium_webui PRIVATE ws2_32)
    target_compile_definitions(thorium_webui PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
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
# so there is nothing to point at by hand the way THORIUM_UI_TEST_BINARY once
# had to be -- it also means run_scripts must actually exist by the time the
# test runs, hence add_dependencies below rather than trusting build order.
if(BUILD_TESTING_LAYERS)
    add_layer_tests(webui_protocol webui_protocol ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    target_compile_definitions(webui_protocol_tests PRIVATE
        THORIUM_WEBUI_TEST_BINARY="$<TARGET_FILE:run_scripts>"
    )
    add_dependencies(webui_protocol_tests run_scripts)
endif()
