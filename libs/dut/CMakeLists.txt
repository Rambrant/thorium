# Header-only: Adapter is a small value type with nothing to put in a .cpp.
# An INTERFACE library still gets a real target (for tests/IDE visibility
# and target_link_libraries) without inventing an empty translation unit.
add_library(dut INTERFACE)

target_sources(dut INTERFACE
    include/dut/adapter.hpp
)

target_include_directories(dut INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(dut INTERFACE hal core)

if(BUILD_TESTING_LAYERS)
    add_layer_tests(dut dut
        tests/test_adapter.cpp
    )
endif()
