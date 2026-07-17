add_library(hal STATIC
    src/vpc_location.cpp
    src/switch_fabric.cpp
    src/instrument.cpp
    src/wiring.cpp
    src/measure.cpp
    src/apply.cpp
    include/hal/vpc_location.hpp   # header-only
    include/hal/switch_fabric.hpp  # header-only
    include/hal/instrument.hpp     # header-only
    include/hal/instruments.hpp    # header-only
    include/hal/wiring.hpp         # header-only
    include/hal/adapter.hpp        # header-only
    include/hal/measure.hpp        # header-only
    include/hal/apply.hpp          # header-only
    include/hal/n6701a.hpp         # header-only
    include/hal/ac6677a.hpp        # header-only
    include/hal/l4411a.hpp         # header-only
    wiring.inc                     # content only; not compiled directly, see README.md
    instrument.inc                 # content only; not compiled directly, see README.md
)

target_include_directories(hal PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(hal PUBLIC core)

# src/measure.cpp/src/apply.cpp #include wiring.inc (this rig's fixed wiring
# data), and include/hal/instruments.hpp #includes instrument.inc (this
# rig's fixed instrument list), via a repo-root-relative path, the same
# convention libs/dut/*.inc already use -- see wiring.inc's own comment.
target_include_directories(hal PRIVATE ${CMAKE_SOURCE_DIR})

if(BUILD_TESTING_LAYERS)
    add_layer_tests(hal hal
        tests/test_vpc_location.cpp
        tests/test_switch_fabric.cpp
        tests/test_instrument.cpp
        tests/test_wiring.cpp
        tests/test_source_instruments.cpp
    )
endif()
