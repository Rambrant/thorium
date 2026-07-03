add_library(core STATIC
    src/logger.cpp
    include/core/quantity.hpp        # header-only; listed so it shows up in IDEs
    include/core/instrument.hpp      # header-only
    include/core/switch_matrix.hpp   # header-only
)

target_include_directories(core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if(BUILD_TESTING_LAYERS)
    add_layer_tests(core core
        tests/test_logger.cpp
        tests/test_quantity.cpp
        tests/test_switch_matrix.cpp
    )
endif()
