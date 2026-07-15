add_library(hal STATIC
    src/vpc_location.cpp
    src/switch_fabric.cpp
    src/instrument.cpp
    src/route_table.cpp
    src/measure.cpp
    include/hal/vpc_location.hpp   # header-only
    include/hal/switch_fabric.hpp  # header-only
    include/hal/instrument.hpp     # header-only
    include/hal/instruments.hpp    # header-only
    include/hal/route_table.hpp    # header-only
    include/hal/adapter.hpp        # header-only
    include/hal/measure.hpp        # header-only
)

target_include_directories(hal PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(hal PUBLIC core)

# src/measure.cpp #includes dut/device_x_profile.inc directly (a flat DUT
# data file, not a dut:: C++ symbol -- see hal/measure.cpp's own comment and
# libs/dut/README.md), so this target needs the repo root on its include
# path to resolve that #include. This is a file dependency only: hal does
# not target_link_libraries against dut, and there is no dut library target
# to link against any more (see libs/dut/CMakeLists.txt).
target_include_directories(hal PRIVATE ${CMAKE_SOURCE_DIR})

if(BUILD_TESTING_LAYERS)
    add_layer_tests(hal hal
        tests/test_vpc_location.cpp
        tests/test_switch_fabric.cpp
        tests/test_instrument.cpp
        tests/test_route_table.cpp
        tests/test_adapter.cpp
    )
endif()
