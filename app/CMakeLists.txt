# The test-suite runner: main.cpp, using core/active_test_catalog.hpp (the
# framework-side scaffolding that turns suite/ content into something
# runnable). Nothing DUT-specific lives in this folder -- see suite/ for
# that -- which is also why it isn't called "scripts" anymore: it doesn't
# hold any.
#
# The actual test scripts (suite/scripts/*.cpp) are NOT built here as a
# fixed list -- they're discovered from suite/, kept clean of any
# CMakeLists.txt of its own, the same way suite/criteria_*.inc and
# suite/test_catalog.inc are just content, not build machinery.
# CONFIGURE_DEPENDS makes CMake re-glob and reconfigure whenever that
# directory's contents change, so a new suite/scripts/*.cpp just needs to
# exist to be picked up on the next build -- no CMakeLists.txt edit.
file(GLOB THORIUM_SUITE_SCRIPT_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/suite/scripts/*.cpp")

# Target is still named "scripts" -- it's a library of compiled test
# scripts, which is still an accurate name, even though the folder
# defining it (this one) no longer is.
add_library(scripts STATIC
    ${THORIUM_SUITE_SCRIPT_SOURCES}
    ${CMAKE_SOURCE_DIR}/suite/measure.cpp
    ${CMAKE_SOURCE_DIR}/suite/measure.hpp            # header-only; listed for IDE visibility only
    ${CMAKE_SOURCE_DIR}/suite/device_x_profile.hpp   # header-only; listed for IDE visibility only
)

target_link_libraries(scripts PUBLIC dut hal core)

target_include_directories(scripts PUBLIC ${CMAKE_SOURCE_DIR})

# --- Criteria/tolerance variant selection ---
# Points core/active_criteria.hpp at the active variant's single flat .inc
# file (every script's CRITERIA/CRIT side by side), per THORIUM_CRITERIA_VARIANT
# (validated in the top-level CMakeLists.txt). THORIUM_PRODUCTION_CRITERIA is
# a second, fixed path -- always criteria_production.inc -- so other variants
# can reference "the same value as production" via CRIT_FROM_PRODUCTION
# without retyping it. Absolute paths, so no extra include-directory
# wrangling. See suite/README.md.
target_compile_definitions(scripts PRIVATE
    THORIUM_PRODUCTION_CRITERIA="${CMAKE_SOURCE_DIR}/suite/criteria_production.inc"
    THORIUM_ACTIVE_CRITERIA="${CMAKE_SOURCE_DIR}/suite/criteria_${THORIUM_CRITERIA_VARIANT}.inc"
)

# --- Test catalog ---
# THORIUM_TEST_CATALOG points core/active_test_catalog.hpp at
# suite/test_catalog.inc; THORIUM_TEST_SCRIPTS points it at
# suite/scripts.hpp (the declarations every TEST(...) is checked
# against). Both absolute paths, both PUBLIC (unlike the criteria defines
# above): main.cpp includes active_test_catalog.hpp too, and it's compiled
# as part of run_scripts, not this scripts library, so it needs these
# definitions to reach it via target_link_libraries below.
target_compile_definitions(scripts PUBLIC
    THORIUM_TEST_CATALOG="${CMAKE_SOURCE_DIR}/suite/test_catalog.inc"
    THORIUM_TEST_SCRIPTS="${CMAKE_SOURCE_DIR}/suite/scripts.hpp"
)

# Runner: executes all scripts against a real (or simulated) device
# and exits non-zero if any script fails.
add_executable(run_scripts src/main.cpp)
target_link_libraries(run_scripts PRIVATE scripts)

if(BUILD_TESTING_LAYERS)
    # These test suite/scripts/*.cpp and suite/criteria_*.inc -- suite
    # content, not this runner -- so their sources live in suite/tests/,
    # not here. This is still where the target itself is defined, same as
    # suite/scripts/*.cpp is discovered from here rather than built via a
    # CMakeLists.txt of its own.
    add_layer_tests(scripts scripts
        ${CMAKE_SOURCE_DIR}/suite/tests/test_fuse_register_script.cpp
        ${CMAKE_SOURCE_DIR}/suite/tests/test_supply_rail_script.cpp
        ${CMAKE_SOURCE_DIR}/suite/tests/test_criteria_variants_compile.cpp
    )

    # test_criteria_variants_compile.cpp #includes every variant .inc file
    # directly (not through core/active_criteria.hpp, which only ever sees
    # the one active variant) -- it needs core::criterion machinery but not
    # the scripts library itself, and it doesn't go through the
    # THORIUM_ACTIVE_CRITERIA compile definition above. test_fuse_register_
    # script.cpp / test_supply_rail_script.cpp also rely on this to reach
    # suite/scripts.hpp by a plain quoted #include.
    target_include_directories(scripts_tests PRIVATE ${CMAKE_SOURCE_DIR})
endif()
