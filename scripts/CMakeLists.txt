# The test scripts themselves, written against the dsl layer.
# Split into a library (so scripts are unit-testable) + a thin runner exe.
#
# Sources are discovered, not listed by hand: CONFIGURE_DEPENDS makes CMake
# re-glob and reconfigure whenever this directory's contents change, so a
# new src/*.cpp just needs to exist to be picked up on the next build --
# no CMakeLists.txt edit. (This is the one CMake-recommended exception to
# "don't GLOB sources": without CONFIGURE_DEPENDS a stale build could
# silently miss a newly-added file until someone reruns cmake by hand.)
# main.cpp is excluded -- it's the runner's entry point (below), not part
# of the library; leaving it in would risk a duplicate main() wherever
# `scripts` also gets linked (e.g. scripts_tests).
file(GLOB THORIUM_SCRIPT_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
list(REMOVE_ITEM THORIUM_SCRIPT_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp")

add_library(scripts STATIC
    ${THORIUM_SCRIPT_SOURCES}
    include/scripts/device_x_profile.hpp   # header-only
)

target_include_directories(scripts PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(scripts PUBLIC dut hal core)

# --- Criteria/tolerance variant selection ---
# Points core/active_criteria.hpp at the active variant's single flat .inc
# file (every script's CRITERIA/CRIT side by side), per THORIUM_CRITERIA_VARIANT
# (validated in the top-level CMakeLists.txt). THORIUM_PRODUCTION_CRITERIA is
# a second, fixed path -- always production.inc -- so other variants can
# reference "the same value as production" via CRIT_FROM_PRODUCTION without
# retyping it. Absolute paths, so no extra include-directory wrangling.
# See suite/README.md.
target_compile_definitions(scripts PRIVATE
    THORIUM_PRODUCTION_CRITERIA="${CMAKE_SOURCE_DIR}/suite/production.inc"
    THORIUM_ACTIVE_CRITERIA="${CMAKE_SOURCE_DIR}/suite/${THORIUM_CRITERIA_VARIANT}.inc"
)

# --- Test catalog ---
# Points scripts/include/scripts/catalog.hpp at suite/catalog.inc, the
# same way THORIUM_ACTIVE_CRITERIA points active_criteria.hpp at a variant --
# an absolute path, so no extra include directory is needed to find it.
# PUBLIC (unlike the criteria defines above): main.cpp includes catalog.hpp
# too, and it's compiled as part of run_scripts, not this scripts library,
# so it needs this definition to reach it via target_link_libraries below.
target_compile_definitions(scripts PUBLIC
    THORIUM_TEST_CATALOG="${CMAKE_SOURCE_DIR}/suite/catalog.inc"
)

# Runner: executes all scripts against a real (or simulated) device
# and exits non-zero if any script fails.
add_executable(run_scripts src/main.cpp)
target_link_libraries(run_scripts PRIVATE scripts)

if(BUILD_TESTING_LAYERS)
    add_layer_tests(scripts scripts
        tests/test_fuse_register_script.cpp
        tests/test_supply_rail_script.cpp
        tests/test_criteria_variants_compile.cpp
    )

    # test_criteria_variants_compile.cpp #includes every variant .inc file
    # directly (not through core/active_criteria.hpp, which only ever sees
    # the one active variant) -- it needs core::criterion machinery but not
    # the scripts library itself, and it doesn't go through the
    # THORIUM_ACTIVE_CRITERIA compile definition above.
    target_include_directories(scripts_tests PRIVATE ${CMAKE_SOURCE_DIR})
endif()
