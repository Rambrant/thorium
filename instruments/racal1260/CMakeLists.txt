# --- hal::Racal1260 -- Racal 1260-series matrix-routed RS232 port ---
# One instrument driver, self-contained: its header, its tests, its README,
# and this file. Nothing outside this directory names any of them -- the tree
# above discovers this directory and hands libs/hal the path (see the top-level
# CMakeLists.txt and libs/hal/CMakeLists.txt) -- so the whole directory can be
# zipped, versioned and dropped into another rig's instruments/ as it stands.
#
# INTERFACE, not STATIC: the driver is header-only, as all four of this repo's
# original drivers are. That is not an accident of these particular models --
# an instrument driver is a type whose Port-returning members are consumed as
# types by the call site (see core/port.hpp), so its definitions have to be
# visible to whoever measures through it. A driver that grows a real .cpp
# (talking to a VISA library, say) becomes STATIC here and nothing outside this
# file needs to know.
add_library(hal_racal1260 INTERFACE)

# include/hal/racal1260.hpp, not include/racal1260.hpp: the driver stays in namespace
# hal and keeps its "hal/racal1260.hpp" spelling at every call site. It *is* a hal
# driver -- it implements hal's instrument API and is useless without it -- and
# renaming it on the way out of libs/hal would have churned every consumer, and
# every comment referring to it, to say nothing new.
target_include_directories(hal_racal1260 INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

# hal, never hal_rig: a driver compiles against the generic half of the
# hardware abstraction layer and nothing else. That is the whole point of the
# two-target split -- see libs/hal/CMakeLists.txt's own comment on it, and on
# the explicit check that rejects a driver reaching for hal_rig. (The check is
# explicit because the cycle it closes is one CMake tolerates silently, which is
# not what you would guess.)
target_link_libraries(hal_racal1260 INTERFACE Thorium::hal)

add_library(Thorium::hal_racal1260 ALIAS hal_racal1260)

# How libs/hal learns this directory's target name, rather than deriving one
# from the directory name -- see its own comment on the property. Appended, so
# a driver package holding more than one target registers each.
set_property(GLOBAL APPEND PROPERTY THORIUM_INSTRUMENT_TARGETS hal_racal1260)

install(TARGETS hal_racal1260
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
    # belongs in rig/tests instead -- see instruments/README.md.
    add_layer_tests(racal1260 hal_racal1260 ${CMAKE_CURRENT_SOURCE_DIR}/tests)
endif()
