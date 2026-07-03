add_library(dut STATIC
    src/device.cpp
    include/dut/matrix_map.hpp   # header-only
    include/dut/rig_device.hpp   # header-only
)

target_include_directories(dut PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(dut PUBLIC hal core)

if(BUILD_TESTING_LAYERS)
    add_layer_tests(dut dut
        tests/test_device.cpp
        tests/test_matrix_map.cpp
        tests/test_rig_device.cpp
    )
endif()
