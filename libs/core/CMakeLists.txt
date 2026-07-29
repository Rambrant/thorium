add_library(core STATIC
        src/logger.cpp
        src/verify.cpp
        src/quantity_kind.cpp
        src/recording.cpp
        src/session.cpp
        include/core/quantity.hpp        # header-only; listed so it shows up in IDEs
        include/core/quantity_kind.hpp   # header-only
        include/core/recording.hpp       # header-only
        include/core/session.hpp         # header-only
        include/core/port.hpp            # header-only
        include/core/measure.hpp         # header-only
        include/core/adapter.hpp         # header-only
        include/core/at.hpp              # header-only
        include/core/apply.hpp           # header-only
        include/core/predicates.hpp      # header-only
        include/core/criterion.hpp       # header-only
        include/core/active_criteria.hpp # header-only
        include/core/meta.hpp            # header-only
)

target_include_directories(core PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

# Thorium::core -- usable under this name whether core is consumed in-tree
# (this repo's own dut/dsl/hal) or by a separate rig repo pulling this
# package in via add_subdirectory/FetchContent, which gets no automatic
# namespacing the way find_package(Thorium) + ThoriumTargets.cmake does; see
# the top-level CMakeLists.txt's own comment on the Thorium package export.
add_library(Thorium::core ALIAS core)

install(TARGETS core
    EXPORT ThoriumTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

if(BUILD_TESTING_LAYERS)
    add_layer_tests(core core
            tests/test_logger.cpp
            tests/test_quantity.cpp
            tests/test_quantity_kind.cpp
            tests/test_session.cpp
            tests/test_port.cpp
            tests/test_measure.cpp
            tests/test_adapter.cpp
            tests/test_predicates.cpp
            tests/test_criterion.cpp
            tests/test_verify.cpp
            tests/test_meta.cpp
            tests/test_static_constraints.cpp
    )
endif()
