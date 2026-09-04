# --- hal::keysight_34980a::Chassis -- Keysight 34980A switch/measure mainframe ---
# One driver, self-contained: its header, its tests, its README, and this file.
# Nothing outside this directory names any of them -- the tree above discovers
# this directory and hands framework/hal the path (see the top-level
# CMakeLists.txt and framework/hal/CMakeLists.txt) -- so the whole directory can
# be zipped, versioned and dropped into another rig's instruments/ as it stands.
#
# The first thing here that is NOT an instrument driver, and the directory it
# sits in is deliberate rather than convenient: hal/fabric/switch_device.hpp's
# own comment names instruments/ as where a switch card's driver goes when it
# gets one ("each model becomes its own directory with its own header"). See the
# header's preamble for the whole argument, including why the *mainframe* is
# what has a driver and a module is not.
#
# One consequence worth stating here, where a build reader will look for it:
# no INSTRUMENT() row names this package, so cmake/InstrumentDrivers.cmake
# generates no #include for it and it contributes no code to a rig binary. That
# is the "availability is not use" case instruments/README.md describes -- the
# same footing instruments/keysight_dso8064a is on -- and it is what this
# package will stay on until hal::SwitchFabric grows a transport seam to call
# it through.
#
# STATIC, like the three drivers that talk. Everything that speaks SCPI is in
# src/keysight_34980a.cpp; the header carries the types a call site names.
# Unlike keysight_edu36311a, nothing here is a template, so the .cpp holds
# ordinary out-of-line members.
add_library(hal_keysight_34980a STATIC
    src/keysight_34980a.cpp
)

# include/hal/keysight_34980a.hpp, not include/keysight_34980a.hpp: this driver
# keeps its "hal/keysight_34980a.hpp" spelling at every call site. It *is* a hal
# driver -- it implements against hal's address and I/O layers and is useless
# without them -- so it stays under hal/ on the include path even though its own
# declarations live in the nested namespace hal::keysight_34980a.
#
# The directory, the header and the namespace all carry the same name, and it
# carries the manufacturer as well as the model. Here the manufacturer token is
# doing something slightly different from usual: this mainframe shipped as an
# Agilent product and its own programmer's reference still says Agilent
# throughout, so `keysight_` is the company as it is today rather than the badge
# on the front panel -- the same treatment keysight_dso8064a, keysight_l4411a
# and keysight_n6701a get.
#
# PUBLIC rather than INTERFACE, this target having a translation unit of its
# own: the .cpp includes its own header the same way a consumer does.
target_include_directories(hal_keysight_34980a PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

# hal, never hal_rig -- and on this driver that line is load-bearing rather than
# conventional. A switching driver is the one thing in this tree that might
# plausibly want a rig's device table (which cards, which slots), and reaching
# for it would close a cycle CMake tolerates silently: hal_rig already depends
# on every instrument target. See framework/hal/CMakeLists.txt's own comment on
# the explicit check that rejects it.
#
# So this driver knows about slots and channels and nothing about which of them
# this bench uses. Which cards are in which slots is a rig fact, and it reaches
# a driver as arguments rather than as a dependency -- exactly as an
# instrument's InstrumentId does.
target_link_libraries(hal_keysight_34980a PUBLIC Thorium::hal)

add_library(Thorium::hal_keysight_34980a ALIAS hal_keysight_34980a)

# How framework/hal learns this directory's target name, rather than deriving
# one from the directory name -- see its own comment on the property. Appended,
# so a driver package holding more than one target registers each.
#
# This package is the one most likely to hold a second one day: the mainframe's
# optional internal DMM is an instrument, in the same box, sharing the same
# session (see the header's preamble on the Janus). When it arrives it may well
# be a second target here rather than a second directory, and this line already
# supports that.
set_property(GLOBAL APPEND PROPERTY THORIUM_INSTRUMENT_TARGETS hal_keysight_34980a)

install(TARGETS hal_keysight_34980a
    EXPORT ThoriumTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

if(BUILD_TESTING_LAYERS)
    # Linked against this driver alone -- which transitively means hal and core
    # and nothing else. No hal_rig, no rig data, no other driver. It is worth
    # more than it looks: it is the build refusing to let this driver's tests
    # quietly start depending on this bench's wiring, which is exactly what
    # would make the directory no longer packageable on its own.
    #
    # These tests are also the only ones here that need no hal::InstrumentId at
    # all, because a chassis has no id (see the header's preamble). So this is
    # the one driver package genuinely immune to the deployment coupling the
    # top-level CMakeLists.txt describes -- a bench with one meter and no
    # switching can still build and run them.
    add_layer_tests(keysight_34980a hal_keysight_34980a ${CMAKE_CURRENT_SOURCE_DIR}/tests)
endif()
