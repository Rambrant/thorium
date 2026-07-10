add_library(core STATIC
        src/logger.cpp
        src/verify.cpp
        src/measure.cpp
        include/core/quantity.hpp        # header-only; listed so it shows up in IDEs
        include/core/instrument.hpp      # header-only
        include/core/switch_matrix.hpp   # header-only
        include/core/predicates.hpp      # header-only
        include/core/criterion.hpp       # header-only
        include/core/criterions.hpp      # header-only
)

target_include_directories(core PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if(BUILD_TESTING_LAYERS)
    add_layer_tests(core core
            tests/test_logger.cpp
            tests/test_quantity.cpp
            tests/test_switch_matrix.cpp
            tests/test_predicates.cpp
            tests/test_criterion.cpp
            tests/test_verify.cpp
            tests/test_measure.cpp
            tests/test_static_constraints.cpp
    )
endif()