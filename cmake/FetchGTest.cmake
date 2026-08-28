# Fetches GoogleTest once at the top level so every layer's tests/ folder
# can just do: target_link_libraries(<layer>_tests PRIVATE GTest::gtest_main)
include(FetchContent)

#
# SOURCE_DIR only, and no GIT_REPOSITORY/GIT_TAG: GoogleTest is vendored into
# third_party/, so this "fetch" resolves to a directory already on disk and
# configuring never touches the network. The version is the directory name and
# nothing else -- bumping it means unpacking a new third_party/googletest-<v>
# and changing the path here.
#
# There used to be a GIT_TAG v1.15.2 alongside this, which was worse than
# redundant: SOURCE_DIR means the tag is never consulted, and it named a
# different version than the one actually vendored, so the only thing it could
# do was tell a reader the wrong GoogleTest version.
#
FetchContent_Declare(
    googletest
    SOURCE_DIR ${CMAKE_SOURCE_DIR}/third_party/googletest-1.18.0
)

# Match the parent project's compiler settings, avoid installing gtest system-wide
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(googletest)

include(GoogleTest)

# Convenience helper: add_layer_tests(<layer_name> <lib_to_test> <tests_dir>)
# Creates a <layer_name>_tests executable from every .cpp in <tests_dir>, links
# it against gtest + the layer's own library, and registers it with CTest via
# gtest_discover_tests.
#
# Globbed rather than listed, and the argument for that is the failure mode of
# the alternative rather than the typing it saves. A source file left out of a
# library fails to link -- something calls it, nothing defines it, the build
# stops. A *test* file left out of its target fails at nothing: it compiles
# nowhere, runs never, and reports no absence. The suite stays green precisely
# because the new test is not in it, which is the one kind of missing thing this
# project's compile-time-checking habit cannot catch for you. Every list this
# replaced was, in fact, an exact enumeration of its own tests/*.cpp -- so what
# was hand-maintained was a copy of a directory listing, and the copy was the
# only half that could ever be wrong.
#
# Same rule the top-level CMakeLists.txt already applies to instruments/: glob
# where the glob settles availability, spell it out longhand where the list *is*
# the semantic content (THORIUM_KNOWN_CRITERIA_VARIANTS, rig/instrument.inc).
# A tests/ directory is the first kind -- there is no such thing as a test file
# that exists but is deliberately not run. A layer that somehow needs one anyway
# should declare that target itself rather than teach this function about
# exceptions; framework/runner/CMakeLists.txt's acceptance_tests already does exactly
# that, for its own unrelated reasons.
#
# The directory is a parameter rather than assumed to be
# ${CMAKE_CURRENT_SOURCE_DIR}/tests, because for one caller it isn't: the
# scripts layer's tests are suite content and live under the deployment's
# tests/, while the target itself is declared in framework/runner (see
# framework/runner/CMakeLists.txt). Passing it also keeps this helper from globbing a
# caller's own tests/ directory behind its back -- framework/runner/tests/ holds the
# hook-ordering fixture, which belongs to a runner target rather than to a test
# executable of its own.
#
# CONFIGURE_DEPENDS so adding or deleting a tests/*.cpp is picked up by the next
# build with no manual reconfigure.
#
# GLOB_RECURSE rather than GLOB, so a layer can group its tests into
# subdirectories mirroring its own include/ layout (tests/criteria/ against
# include/core/criteria/, and so on) without each new subdirectory having to be
# remembered here. This is the same argument the paragraphs above make, applied
# one level down and with the same failure mode behind it: a flat glob does not
# fail when a test moves into a subdirectory, it silently stops compiling it,
# and the suite goes green by omission -- exactly the outcome globbing was
# adopted to prevent. A subdirectory under tests/ is grouping, never opt-out.
#
# The corollary is that a tests/ directory passed to this function may contain
# nothing but tests, at any depth. Sources that live under a tests/ tree but
# belong to some other target -- framework/runner/tests/fixtures/ is the one
# example in this repo -- must sit under a directory that is never handed to
# this function, which is already true there (the scripts layer's call passes
# ${THORIUM_SUITE_DIR}/tests, not framework/runner/tests). Deliberately not
# solved by teaching this function to skip a magic directory name: per the note
# above, a layer with a genuine exception declares its own target.
function(add_layer_tests LAYER_NAME LINK_LIB TESTS_DIR)
    file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPENDS "${TESTS_DIR}/*.cpp")

    # An empty glob would otherwise reach add_executable as "no sources", whose
    # diagnostic names the target rather than the directory that came up empty.
    if(NOT TEST_SOURCES)
        message(FATAL_ERROR
            "add_layer_tests(${LAYER_NAME}): no *.cpp under '${TESTS_DIR}' -- either "
            "that path is wrong, or the layer has no tests yet and should not be "
            "declaring an empty test target.")
    endif()

    add_executable(${LAYER_NAME}_tests ${TEST_SOURCES})
    target_link_libraries(${LAYER_NAME}_tests PRIVATE
        ${LINK_LIB}
        GTest::gtest_main
    )
    gtest_discover_tests(${LAYER_NAME}_tests)
endfunction()
