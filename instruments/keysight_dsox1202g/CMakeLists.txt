# --- hal::keysight_dsox1202g::DSOX1202G -- Keysight InfiniiVision DSOX1202G two-channel oscilloscope ---
# One instrument driver, self-contained: its header, its tests, its README,
# and this file. Nothing outside this directory names any of them -- the tree
# above discovers this directory and hands framework/hal the path (see the top-level
# CMakeLists.txt and framework/hal/CMakeLists.txt) -- so the whole directory can be
# zipped, versioned and dropped into another rig's instruments/ as it stands.
#
# STATIC, and the second driver here that is -- which is what this file said
# would happen the day it grew a .cpp, and it has.
#
# The split is not arbitrary. An instrument driver is a type whose Port-
# returning members are consumed as types by the call site (see
# core/driver/port.hpp), so the builders, the channel view and the ports have to
# stay visible in the header. What went into src/ is everything that does not
# depend on which C++ quantity a reading is about to be wrapped in: the SCPI
# strings, the arm-and-poll sequence, the waveform block parser. See that file's
# own comment, and instruments/keysight_edu34450a, which drew the same line
# first.
add_library(hal_keysight_dsox1202g STATIC
    src/keysight_dsox1202g.cpp
)

# include/hal/keysight_dsox1202g.hpp, not include/keysight_dsox1202g.hpp: this driver keeps its
# "hal/keysight_dsox1202g.hpp" spelling at every call site. It *is* a hal driver -- it
# implements hal's instrument API and is useless without it -- so it stays under
# hal/ on the include path even though its own declarations live in the nested
# namespace hal::keysight_dsox1202g (see the header's own comment on why they are
# nested rather than sitting directly in hal).
#
# The directory, the header and the namespace all carry the same name, and it
# carries the manufacturer as well as the model: a model number is not unique
# across vendors, so a bare model name is a collision waiting for the second
# vendor to ship one.
# PUBLIC rather than INTERFACE now that this target has a translation unit of
# its own: the .cpp includes its own header the same way a consumer does.
target_include_directories(hal_keysight_dsox1202g PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

# hal, never hal_rig: a driver compiles against the generic half of the
# hardware abstraction layer and nothing else. That is the whole point of the
# two-target split -- see framework/hal/CMakeLists.txt's own comment on it, and on
# the explicit check that rejects a driver reaching for hal_rig. (The check is
# explicit because the cycle it closes is one CMake tolerates silently, which is
# not what you would guess.)
target_link_libraries(hal_keysight_dsox1202g PUBLIC Thorium::hal)

add_library(Thorium::hal_keysight_dsox1202g ALIAS hal_keysight_dsox1202g)

# How framework/hal learns this directory's target name, rather than deriving one
# from the directory name -- see its own comment on the property. Appended, so
# a driver package holding more than one target registers each.
set_property(GLOBAL APPEND PROPERTY THORIUM_INSTRUMENT_TARGETS hal_keysight_dsox1202g)

install(TARGETS hal_keysight_dsox1202g
    EXPORT ThoriumTargets
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

if(BUILD_TESTING_LAYERS)
    # Linked against this driver alone -- which transitively means hal and core
    # and nothing else. No hal_rig, no rig data, no other driver, and in
    # particular not the Infiniium driver this one replaces on the bench: the
    # two are alternatives, not a pair, and a test here that reached for the
    # other would make neither directory packageable on its own. If a test here
    # ever genuinely needs the fabric or an Apply, it belongs in rig/tests/
    # instead -- see rig/README.md.
    add_layer_tests(keysight_dsox1202g hal_keysight_dsox1202g ${CMAKE_CURRENT_SOURCE_DIR}/tests)
endif()
