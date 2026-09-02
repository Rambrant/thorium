# --- hal::keysight_edu36311a::EDU36311A -- Keysight EDU36311A triple-output DC power supply ---
# One instrument driver, self-contained: its header, its tests, its README, and
# this file. Nothing outside this directory names any of them -- the tree above
# discovers this directory and hands framework/hal the path (see the top-level
# CMakeLists.txt and framework/hal/CMakeLists.txt) -- so the whole directory can
# be zipped, versioned and dropped into another rig's instruments/ as it stands.
#
# STATIC, and the third driver here that is, after keysight_edu34450a and
# keysight_dsox1202g. Same line drawn in the same place: an instrument driver is
# a type whose Port-returning members are consumed as types by the call site
# (see core/driver/port.hpp), so the builders and the ports stay in the header,
# and everything that does not depend on which C++ quantity a reading gets
# wrapped in goes into src/.
#
# What is different here, and it is worth knowing before assuming this is a copy
# of the meter's arrangement: this driver's class is a *template* (which output
# of the three, and whether its lead has an isolation relay), so its .cpp holds
# free functions over an int channel rather than out-of-line members. Members
# would only link for the combinations the .cpp explicitly instantiated, and
# this driver promises that a new isolation tag needs no change to it -- see the
# header's own comment on namespace detail.
add_library(hal_keysight_edu36311a STATIC
    src/keysight_edu36311a.cpp
)

# include/hal/keysight_edu36311a.hpp, not include/keysight_edu36311a.hpp: this
# driver keeps its "hal/keysight_edu36311a.hpp" spelling at every call site. It
# *is* a hal driver -- it implements hal's instrument API and is useless without
# it -- so it stays under hal/ on the include path even though its own
# declarations live in the nested namespace hal::keysight_edu36311a (see the
# header's own comment on why they are nested rather than sitting directly in
# hal).
#
# The directory, the header and the namespace all carry the same name, and it
# carries the manufacturer as well as the model: a model number is not unique
# across vendors, so a bare model name is a collision waiting for the second
# vendor to ship one. Here it is not even unique across one vendor's own
# catalogue -- see the header on the E36311A, which is a different instrument
# with the same command set.
#
# PUBLIC rather than INTERFACE, this target having a translation unit of its
# own: the .cpp includes its own header the same way a consumer does.
target_include_directories(hal_keysight_edu36311a PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

# hal, never hal_rig: a driver compiles against the generic half of the hardware
# abstraction layer and nothing else. That is the whole point of the two-target
# split -- see framework/hal/CMakeLists.txt's own comment on it, and on the
# explicit check that rejects a driver reaching for hal_rig. (The check is
# explicit because the cycle it closes is one CMake tolerates silently, which is
# not what you would guess.)
#
# And not hal_keysight_n6701a either, which is the near miss this driver
# specifically had to avoid: it wants the same DirectWiring/RelayIsolated
# distinction that driver already spells, and declares its own two tags rather
# than linking a second driver for them. See the header's comment on those tags,
# which also says what to do when a third supply wants them.
target_link_libraries(hal_keysight_edu36311a PUBLIC Thorium::hal)

add_library(Thorium::hal_keysight_edu36311a ALIAS hal_keysight_edu36311a)

# How framework/hal learns this directory's target name, rather than deriving
# one from the directory name -- see its own comment on the property. Appended,
# so a driver package holding more than one target registers each.
set_property(GLOBAL APPEND PROPERTY THORIUM_INSTRUMENT_TARGETS hal_keysight_edu36311a)

install(TARGETS hal_keysight_edu36311a
    EXPORT ThoriumTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

if(BUILD_TESTING_LAYERS)
    # Linked against this driver alone -- which transitively means hal and core
    # and nothing else. No hal_rig, no rig data, no other driver, and in
    # particular not the N6701A whose isolation tags this one deliberately
    # re-declares. That is worth more than it looks: it is the build refusing to
    # let this driver's tests quietly start depending on some other instrument
    # or on this rig's wiring, which is exactly what would make the directory no
    # longer packageable on its own. If a test here ever genuinely needs the
    # fabric or an Apply, it belongs in rig/tests/ instead -- see rig/README.md.
    add_layer_tests(keysight_edu36311a hal_keysight_edu36311a ${CMAKE_CURRENT_SOURCE_DIR}/tests)
endif()
