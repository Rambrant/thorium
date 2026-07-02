# The test scripts themselves, written against the dsl layer.
# Split into a library (so scripts are unit-testable) + a thin runner exe.
add_library(scripts STATIC
    src/power_cycle_script.cpp
    src/value_script.cpp
)

target_include_directories(scripts PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(scripts PUBLIC dsl dut hal core)

# Runner: executes all scripts against a real (or simulated) device
# and exits non-zero if any script fails.
add_executable(run_scripts src/main.cpp)
target_link_libraries(run_scripts PRIVATE scripts)

if(BUILD_TESTING_LAYERS)
    add_layer_tests(scripts scripts
        tests/test_power_cycle_script.cpp
        tests/test_value_script.cpp
    )
endif()
