add_library(dut STATIC
    src/measure.cpp
    include/dut/adapter.hpp             # header-only
    include/dut/device_x_profile.hpp    # header-only
    include/dut/measure.hpp             # header-only
    criteria_production.inc             # content only; not compiled directly, see README.md
    criteria_stress.inc                 # content only; not compiled directly, see README.md
    criteria_aged.inc                   # content only; not compiled directly, see README.md
)

target_include_directories(dut PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(dut PUBLIC hal core)

if(BUILD_TESTING_LAYERS)
    add_layer_tests(dut dut
        tests/test_adapter.cpp
    )
endif()
