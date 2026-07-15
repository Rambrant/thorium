add_library(hal STATIC
    src/vpc_location.cpp
    src/switch_fabric.cpp
    src/instrument.cpp
    src/route_table.cpp
    include/hal/vpc_location.hpp   # header-only
    include/hal/switch_fabric.hpp  # header-only
    include/hal/instrument.hpp     # header-only
    include/hal/instruments.hpp    # header-only
    include/hal/route_table.hpp    # header-only
)

target_include_directories(hal PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(hal PUBLIC core)

if(BUILD_TESTING_LAYERS)
    add_layer_tests(hal hal
        tests/test_vpc_location.cpp
        tests/test_switch_fabric.cpp
        tests/test_instrument.cpp
        tests/test_route_table.cpp
    )
endif()
