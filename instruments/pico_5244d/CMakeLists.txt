# --- hal::pico_5244d::Oscilloscope -- Pico Technology PicoScope 5244D ---
# One instrument driver, self-contained: its headers, sources, tests, README and
# this file. Nothing outside this directory names any of them -- the tree above
# discovers the directory from the rig's instrument table and hands framework/hal
# the path (see cmake/InstrumentDrivers.cmake) -- so the whole directory can be
# zipped, versioned and dropped into another rig's instruments/ as it stands.
#
# STATIC: it has sources. src/library.cpp is the only place that touches a
# vendor binary, and it does so by dlopen/LoadLibrary at runtime, so this target
# links against the C++ standard library, hal and (on Unix) the loader -- never
# against the PicoSDK. A machine with no PicoSDK builds this and refuses, at the
# first reading, saying which library it looked for.
add_library(hal_pico_5244d STATIC
    src/library.cpp
    src/measurements.cpp
    src/oscilloscope.cpp
    src/unit.cpp
    src/waveform_generator.cpp
)

target_include_directories(hal_pico_5244d PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

# hal, never hal_rig: a driver compiles against the generic half of the
# hardware abstraction layer and nothing else (see framework/hal/CMakeLists.txt).
# ${CMAKE_DL_LIBS} is dlopen on glibc and empty everywhere else.
target_link_libraries(hal_pico_5244d PUBLIC Thorium::hal ${CMAKE_DL_LIBS})

add_library(Thorium::hal_pico_5244d ALIAS hal_pico_5244d)

set_property(GLOBAL APPEND PROPERTY THORIUM_INSTRUMENT_TARGETS hal_pico_5244d)

install(TARGETS hal_pico_5244d
    EXPORT ThoriumTargets
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

if(BUILD_TESTING_LAYERS AND THORIUM_FRAMEWORK_TESTS)
    # Linked against this driver alone, as every driver's tests are: hal and core
    # and nothing else, so the directory stays packageable on its own. The tests
    # need no PicoSDK and no scope -- the SDK is a fake behind hal::pico_5244d::Ps5000a.
    add_layer_tests(pico_5244d hal_pico_5244d ${CMAKE_CURRENT_SOURCE_DIR}/tests)
endif()
