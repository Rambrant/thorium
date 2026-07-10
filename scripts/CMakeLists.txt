# The test scripts themselves, written against the dsl layer.
# Split into a library (so scripts are unit-testable) + a thin runner exe.
add_library(scripts STATIC
    src/power_cycle_script.cpp
    src/value_script.cpp
    src/fuse_register_script.cpp
    src/supply_rail_script.cpp
    include/scripts/device_x_profile.hpp   # header-only
)

target_include_directories(scripts PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(scripts PUBLIC dsl dut hal core)

# --- Criteria/tolerance variant selection ---
# Points core/criterions.hpp at the active variant's single flat .inc file
# (every script's GROUP/CRIT side by side), per THORIUM_CRITERIA_VARIANT
# (validated in the top-level CMakeLists.txt). Absolute path, so no extra
# include-directory wrangling. See criteria/README.md.
target_compile_definitions(scripts PRIVATE
    THORIUM_ACTIVE_CRITERIA="${CMAKE_SOURCE_DIR}/criteria/${THORIUM_CRITERIA_VARIANT}.inc"
)

# Runner: executes all scripts against a real (or simulated) device
# and exits non-zero if any script fails.
add_executable(run_scripts src/main.cpp)
target_link_libraries(run_scripts PRIVATE scripts)

if(BUILD_TESTING_LAYERS)
    add_layer_tests(scripts scripts
        tests/test_power_cycle_script.cpp
        tests/test_value_script.cpp
        tests/test_fuse_register_script.cpp
        tests/test_supply_rail_script.cpp
        tests/test_criteria_variants_compile.cpp
    )

    # test_criteria_variants_compile.cpp #includes every variant .inc file
    # directly (not through core/criterions.hpp, which only ever sees the
    # one active variant) -- it needs core::criterion machinery but not the
    # scripts library itself, and it doesn't go through the
    # THORIUM_ACTIVE_CRITERIA compile definition above.
    target_include_directories(scripts_tests PRIVATE ${CMAKE_SOURCE_DIR})
endif()
