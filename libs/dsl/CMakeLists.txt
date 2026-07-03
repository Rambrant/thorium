add_library(dsl STATIC
    src/test_case.cpp
    src/verify.cpp
    include/dsl/predicates.hpp   # header-only
    include/dsl/criterion.hpp    # header-only
    include/dsl/measure.hpp      # header-only
)

target_include_directories(dsl PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(dsl PUBLIC dut hal core)

if(BUILD_TESTING_LAYERS)
    add_layer_tests(dsl dsl
        tests/test_test_case.cpp
        tests/test_predicates.cpp
        tests/test_verify.cpp
        tests/test_measure.cpp
    )
endif()
