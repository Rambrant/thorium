# Header-only for now: Adapter/Bench are small value/reference types, and
# Measure is a template -- none of it needs a .cpp yet. An INTERFACE library
# still gets a real target (for tests/IDE visibility and target_link_libraries)
# without inventing an empty translation unit just to satisfy add_library(STATIC).
add_library(dut INTERFACE)

target_sources(dut INTERFACE
    include/dut/adapter.hpp
    include/dut/bench.hpp
    include/dut/measure.hpp
)

target_include_directories(dut INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(dut INTERFACE hal core)

if(BUILD_TESTING_LAYERS)
    add_layer_tests(dut dut
        tests/test_adapter.cpp
        tests/test_measure.cpp
    )
endif()
