# --- Rig-specific data this library needs but does not itself define ---
# hal is a generic instrument-driver + switching-fabric library -- meant to
# be linked by many rigs testing many DUTs, not just this repo's (see
# README.md) -- so it cannot hardcode which instruments a given rig has,
# how they're wired, or what to call them. Whoever configures a build that
# includes this directory supplies that as four file paths: this repo's own
# top-level CMakeLists.txt does it below (pointing at ./rig/), the same way
# a separate rig's own top-level CMakeLists.txt would after pulling this
# library in via add_subdirectory/FetchContent. Same compile-definition-
# swap mechanism core/active_criteria.hpp already uses for
# THORIUM_ACTIVE_CRITERIA/THORIUM_TEST_CATALOG -- see rig/README.md for
# what each of these four files actually contains.
set(THORIUM_INSTRUMENT_IDS "" CACHE FILEPATH
    "Path to the linking rig's instrument_id.inc (hal::InstrumentId's enumerators)")
set(THORIUM_ACTIVE_INSTRUMENTS "" CACHE FILEPATH
    "Path to the linking rig's active_instruments.hpp (its concrete instrument globals + fabric)")
set(THORIUM_INSTRUMENT_TABLE "" CACHE FILEPATH
    "Path to the linking rig's instrument.inc (its fixed instrument list)")
set(THORIUM_WIRING_TABLE "" CACHE FILEPATH
    "Path to the linking rig's wiring.inc (its fixed instrument/connector wiring)")

foreach(_thorium_rig_var THORIUM_INSTRUMENT_IDS THORIUM_ACTIVE_INSTRUMENTS THORIUM_INSTRUMENT_TABLE THORIUM_WIRING_TABLE)
    if(NOT ${_thorium_rig_var} OR NOT EXISTS "${${_thorium_rig_var}}")
        message(FATAL_ERROR
            "${_thorium_rig_var} must point at an existing file -- hal is a generic "
            "library and needs the linking rig to supply its own instrument/wiring "
            "data before it will configure; see libs/hal/README.md.")
    endif()
endforeach()

add_library(hal STATIC
    src/vpc_location.cpp
    src/switch_fabric.cpp
    src/instrument.cpp
    src/wiring.cpp
    src/measure.cpp
    src/apply.cpp
    src/safing.cpp
    include/hal/vpc_location.hpp   # header-only
    include/hal/switch_fabric.hpp  # header-only
    include/hal/instrument.hpp     # header-only
    include/hal/wiring.hpp         # header-only
    include/hal/adapter.hpp        # header-only
    include/hal/measure.hpp        # header-only
    include/hal/apply.hpp          # header-only
    include/hal/safing.hpp         # header-only
    include/hal/n6701a.hpp         # header-only
    include/hal/ac6677a.hpp        # header-only
    include/hal/l4411a.hpp         # header-only
    include/hal/dso8064.hpp        # header-only
)

target_include_directories(hal PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_link_libraries(hal PUBLIC core)

# Thorium::hal -- see libs/core/CMakeLists.txt's own comment on Thorium::core
# for why this alias exists alongside the plain "hal" target name.
add_library(Thorium::hal ALIAS hal)

install(TARGETS hal
    EXPORT ThoriumTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

# PUBLIC: hal/instrument.hpp (a public header) expands THORIUM_INSTRUMENT_IDS
# itself, and rig/active_instruments.hpp -- reached through
# THORIUM_ACTIVE_INSTRUMENTS by hal's own src/apply.cpp, src/measure.cpp,
# src/safing.cpp -- is also #included directly by consumers outside this
# library (suite/scripts.hpp, hal_tests' test_safing.cpp). All four need to
# reach anyone linking hal, transitively, the same reason
# THORIUM_TEST_CATALOG/THORIUM_TEST_SCRIPTS are PUBLIC on app/'s scripts
# target rather than PRIVATE like THORIUM_ACTIVE_CRITERIA (whose only
# consumer is scripts' own .cpp files).
target_compile_definitions(hal PUBLIC
    THORIUM_INSTRUMENT_IDS="${THORIUM_INSTRUMENT_IDS}"
    THORIUM_ACTIVE_INSTRUMENTS="${THORIUM_ACTIVE_INSTRUMENTS}"
    THORIUM_INSTRUMENT_TABLE="${THORIUM_INSTRUMENT_TABLE}"
    THORIUM_WIRING_TABLE="${THORIUM_WIRING_TABLE}"
)

if(BUILD_TESTING_LAYERS)
    add_layer_tests(hal hal
        tests/test_vpc_location.cpp
        tests/test_switch_fabric.cpp
        tests/test_instrument.cpp
        tests/test_wiring.cpp
        tests/test_source_instruments.cpp
        tests/test_call_syntax.cpp
        tests/test_safing.cpp
    )
endif()
