add_library(dut STATIC
    src/measure_engine.cpp
    include/dut/adapter.hpp        # header-only
    include/dut/measure_engine.hpp # header-only
)

target_include_directories(dut PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(dut PUBLIC hal core)

if(BUILD_TESTING_LAYERS)
    add_layer_tests(dut dut
        tests/test_adapter.cpp
        tests/test_measure_engine.cpp
    )
endif()
