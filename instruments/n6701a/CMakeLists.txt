# --- hal::N6701A -- one channel of a Keysight N6701A DC supply mainframe ---
# One instrument driver, self-contained: its header, its tests, its README,
# and this file. Nothing outside this directory names any of them -- the tree
# above discovers this directory and hands framework/hal the path (see the top-level
# CMakeLists.txt and framework/hal/CMakeLists.txt) -- so the whole directory can be
# zipped, versioned and dropped into another rig's instruments/ as it stands.
#
# INTERFACE, not STATIC: header-only, and here necessarily so -- hal::N6701A is
# a template on its Isolation tag (hal::N6701ADirect/hal::N6701ARelay are the two
# instantiations this rig uses, and hal::SwitchableIsolation admits tags nobody
# has written yet), so there is nothing to compile ahead of a call site naming
# one. See core/port.hpp on why driver definitions have to be visible anyway.
add_library(hal_n6701a INTERFACE)

# include/hal/n6701a.hpp, not include/n6701a.hpp: the driver stays in namespace
# hal and keeps its "hal/n6701a.hpp" spelling at every call site. It *is* a hal
# driver -- it implements hal's instrument API and is useless without it -- and
# renaming it on the way out of framework/hal would have churned every consumer, and
# every comment referring to it, to say nothing new.
target_include_directories(hal_n6701a INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

# hal, never hal_rig: a driver compiles against the generic half of the
# hardware abstraction layer and nothing else. That is the whole point of the
# two-target split -- see framework/hal/CMakeLists.txt's own comment on it, and on
# the explicit check that rejects a driver reaching for hal_rig. (The check is
# explicit because the cycle it closes is one CMake tolerates silently, which is
# not what you would guess.)
target_link_libraries(hal_n6701a INTERFACE Thorium::hal)

add_library(Thorium::hal_n6701a ALIAS hal_n6701a)

# How framework/hal learns this directory's target name, rather than deriving one
# from the directory name -- see its own comment on the property. Appended, so
# a driver package holding more than one target registers each.
set_property(GLOBAL APPEND PROPERTY THORIUM_INSTRUMENT_TARGETS hal_n6701a)

install(TARGETS hal_n6701a
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
    add_layer_tests(n6701a hal_n6701a ${CMAKE_CURRENT_SOURCE_DIR}/tests)
endif()
