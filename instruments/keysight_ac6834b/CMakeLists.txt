# --- hal::keysight_ac6834b::Ac6834B -- Agilent/Keysight 6834B AC Power Source/Analyzer ---
# One instrument driver, self-contained: its header, its tests, its README,
# and this file. Nothing outside this directory names any of them -- the tree
# above discovers this directory and hands framework/hal the path (see the top-level
# CMakeLists.txt and framework/hal/CMakeLists.txt) -- so the whole directory can be
# zipped, versioned and dropped into another rig's instruments/ as it stands.
#
# INTERFACE, not STATIC: the driver is header-only, as every driver in this tree
# is -- an instrument driver is a type whose config and Port members are consumed
# as types by the call site (see core/driver/port.hpp and core/verbs/source.hpp), so its
# definitions have to be visible to whoever sources through it.
add_library(hal_keysight_ac6834b INTERFACE)

# include/hal/keysight_ac6834b.hpp, not include/keysight_ac6834b.hpp: this driver keeps its
# "hal/keysight_ac6834b.hpp" spelling at every call site. It *is* a hal driver -- it
# implements hal's instrument API and is useless without it -- so it stays under
# hal/ on the include path even though its own declarations live in the nested
# namespace hal::keysight_ac6834b (see the header's own comment on why they are
# nested rather than sitting directly in hal).
#
# The directory, the header and the namespace all carry the same name, and it
# carries the manufacturer as well as the model: a model number is not unique
# across vendors, so a bare model name is a collision waiting for the second
# vendor to ship one.
target_include_directories(hal_keysight_ac6834b INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

# hal, never hal_rig: a driver compiles against the generic half of the
# hardware abstraction layer and nothing else. That is the whole point of the
# two-target split -- see framework/hal/CMakeLists.txt's own comment on it, and on
# the explicit check that rejects a driver reaching for hal_rig. (The check is
# explicit because the cycle it closes is one CMake tolerates silently, which is
# not what you would guess.)
target_link_libraries(hal_keysight_ac6834b INTERFACE Thorium::hal)

add_library(Thorium::hal_keysight_ac6834b ALIAS hal_keysight_ac6834b)

# How framework/hal learns this directory's target name, rather than deriving one
# from the directory name -- see its own comment on the property. Appended, so
# a driver package holding more than one target registers each.
set_property(GLOBAL APPEND PROPERTY THORIUM_INSTRUMENT_TARGETS hal_keysight_ac6834b)

install(TARGETS hal_keysight_ac6834b
    EXPORT ThoriumTargets
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

if(BUILD_TESTING_LAYERS)
    # Linked against this driver alone -- which transitively means hal and core
    # and nothing else. No hal_rig, no rig data, no other driver. That is worth
    # more than it looks: it is the build refusing to let this driver's tests
    # quietly start depending on some other instrument or on this rig's wiring,
    # which is exactly what would make the directory no longer packageable on
    # its own. If a test here ever genuinely needs the fabric or an Apply, it
    # belongs in rig/tests/ instead -- see rig/README.md.
    add_layer_tests(keysight_ac6834b hal_keysight_ac6834b ${CMAKE_CURRENT_SOURCE_DIR}/tests)
endif()
