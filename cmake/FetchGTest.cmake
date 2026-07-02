# Fetches GoogleTest once at the top level so every layer's tests/ folder
# can just do: target_link_libraries(<layer>_tests PRIVATE GTest::gtest_main)
include(FetchContent)

FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.15.2
)

# Match the parent project's compiler settings, avoid installing gtest system-wide
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(googletest)

include(GoogleTest)

# Convenience helper: add_layer_tests(<layer_name> <lib_to_test> <test_source_files...>)
# Creates a <layer_name>_tests executable, links it against gtest + the layer's
# own library, and registers it with CTest via gtest_discover_tests.
function(add_layer_tests LAYER_NAME LINK_LIB)
    set(TEST_SOURCES ${ARGN})
    add_executable(${LAYER_NAME}_tests ${TEST_SOURCES})
    target_link_libraries(${LAYER_NAME}_tests PRIVATE
        ${LINK_LIB}
        GTest::gtest_main
    )
    gtest_discover_tests(${LAYER_NAME}_tests)
endfunction()
