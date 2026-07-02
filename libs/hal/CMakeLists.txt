add_library(hal STATIC
    src/bus.cpp
)

target_include_directories(hal PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(hal PUBLIC core)

if(BUILD_TESTING_LAYERS)
    add_layer_tests(hal hal tests/test_bus.cpp)
endif()
