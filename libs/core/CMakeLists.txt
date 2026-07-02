add_library(core STATIC
    src/logger.cpp
)

target_include_directories(core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if(BUILD_TESTING_LAYERS)
    add_layer_tests(core core tests/test_logger.cpp)
endif()
