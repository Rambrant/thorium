# --- Rig-specific data this library needs but does not itself define ---
# hal is a generic instrument-driver + switching-fabric library -- meant to
# be linked by many rigs testing many DUTs, not just this repo's (see
# README.md) -- so it cannot hardcode which instruments a given rig has,
# how they're wired, what switching hardware sits between them, or what to
# call any of it. Whoever configures a build that includes this directory
# supplies that as three file paths: this repo's own
# top-level CMakeLists.txt does it below (pointing at ./rig/), the same way
# a separate rig's own top-level CMakeLists.txt would after pulling this
# library in via add_subdirectory/FetchContent. Same compile-definition-
# swap mechanism core/criteria/active_criteria.hpp already uses for
# THORIUM_CRITERIA_VARIANT_TABLES/THORIUM_TEST_CATALOG -- see rig/README.md for
# what each of these three files actually contains.
#
# Three, not four: the driver headers the instrument table's type column needs
# used to be a fourth file the rig wrote by hand, and are now generated from
# that column (see cmake/InstrumentDrivers.cmake, called below). A rig states
# what instruments it has once, in the table, and everything follows.
set(THORIUM_INSTRUMENT_TABLE "" CACHE FILEPATH
    "Path to the linking rig's instrument.inc (its fixed instrument list, and hal::InstrumentId's enumerators)")
set(THORIUM_WIRING_TABLE "" CACHE FILEPATH
    "Path to the linking rig's wiring.inc (its fixed instrument/connector wiring)")
set(THORIUM_DEVICE_TABLE "" CACHE FILEPATH
    "Path to the linking rig's devices.inc (its switching devices, and hal::SwitchDeviceId's enumerators)")

foreach(_thorium_rig_var THORIUM_INSTRUMENT_TABLE THORIUM_WIRING_TABLE THORIUM_DEVICE_TABLE)
    if(NOT ${_thorium_rig_var} OR NOT EXISTS "${${_thorium_rig_var}}")
        message(FATAL_ERROR
            "${_thorium_rig_var} must point at an existing file -- hal is a generic "
            "library and needs the linking rig to supply its own instrument/wiring "
            "data before it will configure; see framework/hal/README.md.")
    endif()
endforeach()

# --- Two targets, not one: the generic half and the rig-coupled half ---
# This library used to be a single "hal" target, and that hid a real
# asymmetry. Four of its translation units (vpc_location, switch_fabric,
# instrument, wiring) compile against nothing but core and their own
# generic headers. Three others (route, measure, safing) open
# hal/topology/active_instruments.hpp -- which declares the linking rig's
# instrument globals, and so #includes that rig's concrete driver headers
# through THORIUM_INSTRUMENT_DRIVERS. One target meant one compile-time
# dependency set, so the generic four inherited the rig coupling of the
# other three whether they used it or not. (src/verbs/source.cpp is the one file on the rig-coupled side that opens
# neither -- it is there for the boundary reason given below, not a
# technical one.)
#
# That distinction is what makes an instruments/ tree possible at all. A
# driver has to compile against hal (it needs InstrumentId, the Port
# machinery, the fabric, the describe customization point), so hal must be
# configurable and buildable *before* any driver is. But
# active_instruments.hpp reaches the drivers, so anything including it must
# come *after* them -- which is why that header, alone among hal/topology/,
# belongs to hal_rig below rather than to hal here.
# Both cannot be true of one target -- hence hal (drivers depend on it) and
# hal_rig (depends on the drivers), with the instruments/ tree configured
# between them below.
#
# The split also names, in the build rather than in a comment, what a
# driver is allowed to assume: exactly what hal exports. Nothing a driver
# needs lives in hal_rig, and that is now enforced by the target graph
# rather than by reviewers noticing.
#
add_library(hal STATIC
    src/driver/address.cpp
    src/driver/instrument.cpp
    src/fabric/switch_device.cpp
    src/fabric/switch_fabric.cpp
    src/topology/vpc_location.cpp
    src/topology/wiring.cpp
    # --- hal/driver: the driver-authoring kit ---
    # What a driver under instruments/ names directly: its API version gate,
    # how the PC reaches hardware, the instrument identity enum, the base its
    # fluent config chain is built on, and the describe customization point
    # its config is logged through. NOT the complete surface a driver may use
    # -- that is everything this target exports, enforced by the hal/hal_rig
    # split above, and the drivers do also reach fabric/ and topology/
    # (connectDriver takes a SwitchFabric & and both wiring tables). These
    # five are simply the ones that are about being a driver rather than about
    # this bench.
    include/hal/driver/api_version.hpp    # header-only
    include/hal/driver/address.hpp        # header-only apart from src/driver/address.cpp
    include/hal/driver/instrument.hpp     # header-only apart from src/driver/instrument.cpp
    include/hal/driver/builder.hpp        # header-only
    include/hal/driver/describe.hpp       # header-only
    # --- hal/fabric: the switching hardware itself ---
    # The devices a rig has, the elements they are made of, and the Path
    # through them that a route closes. Knows nothing about VPC pins or DUTs:
    # topology/ maps onto this, not the other way round.
    include/hal/fabric/switch_device.hpp  # header-only apart from src/fabric/switch_device.cpp
    include/hal/fabric/switch_fabric.hpp  # header-only apart from src/fabric/switch_fabric.cpp
    # --- hal/topology: how this bench is put together ---
    # The VPC coordinate every DUT pin is named by, the adapter/bundle tables
    # that name them, and the wiring that maps instruments and connector pins
    # onto fabric/'s Paths. wiring.hpp holds all three of the rig's static
    # wiring facts because rig/wiring.inc declares all three in one file --
    # including SourceWiring, which is the one that is deliberately NOT a
    # fabric path (see its own comment).
    include/hal/topology/vpc_location.hpp  # header-only apart from src/topology/vpc_location.cpp
    include/hal/topology/adapter.hpp       # header-only
    include/hal/topology/bundle.hpp        # header-only
    # active_instruments.hpp is the fourth table mechanism in this folder and is
    # deliberately NOT here -- it reaches the drivers, so it is listed on
    # hal_rig below. See this file's own comment on the split.
    include/hal/topology/wiring.hpp        # header-only apart from src/topology/wiring.cpp
)

target_include_directories(hal PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_link_libraries(hal PUBLIC core)

# Thorium::hal -- see framework/core/CMakeLists.txt's own comment on Thorium::core
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

# PUBLIC: hal/driver/instrument.hpp (a public header) expands THORIUM_INSTRUMENT_TABLE
# itself, to generate hal::InstrumentId's enumerators -- see that header's own
# comment -- so it has to reach anyone who includes that header, which is every
# driver and every consumer of this library. Same reason
# THORIUM_TEST_CATALOG/THORIUM_TEST_SCRIPTS are PUBLIC on framework/runner's scripts target
# rather than PRIVATE like THORIUM_CRITERIA_VARIANT_TABLES (whose only consumer
# is scripts' own .cpp files).
#
# Note this is only the instrument *list* (rig/instrument.inc: ids and driver
# types as bare tokens), not the instrument *globals*. Expanding it needs no
# driver header, which is precisely why generic hal can carry it and why
# hal::InstrumentId exists before any driver does. THORIUM_INSTRUMENT_DRIVERS
# and THORIUM_WIRING_TABLE belong to hal_rig below, for the opposite reason.
# THORIUM_DEVICE_TABLE is PUBLIC for exactly the same reason as the instrument
# table beside it: hal/fabric/switch_device.hpp is a public header and expands it
# itself, to generate hal::SwitchDeviceId's enumerators plus each device's
# kind and address. That is also why the device list could not simply be
# another block inside rig/wiring.inc -- THORIUM_WIRING_TABLE is PRIVATE to
# hal_rig below, and an enum that hal::SwitchElementId is built from has to be
# visible wherever that header is included.
target_compile_definitions(hal PUBLIC
    THORIUM_INSTRUMENT_TABLE="${THORIUM_INSTRUMENT_TABLE}"
    THORIUM_DEVICE_TABLE="${THORIUM_DEVICE_TABLE}"
)

# --- The rig's instrument drivers ---
# Each driver is its own self-contained directory with its own CMakeLists.txt,
# its own include/hal/<model>.hpp and its own tests -- packageable and
# distributable one driver at a time, which is the whole point (see
# instruments/README.md). This library does not go looking for them: the
# linking rig supplies the list as directory paths in THORIUM_INSTRUMENT_DIRS,
# exactly the way it already supplies its three .inc/.hpp file paths above,
# so framework/ still reaches nothing outside itself. This repo's own top-level
# CMakeLists.txt fills it by globbing ./instruments/.
#
# Each directory registers its target on the THORIUM_INSTRUMENT_TARGETS global
# property rather than having its target name derived from its directory name
# here. A folder that names its own target can hold more than one, or name it
# something other than its folder, and -- more to the point -- a dropped-in
# driver package declares what it provides instead of having this file guess.
foreach(_thorium_instrument_dir IN LISTS THORIUM_INSTRUMENT_DIRS)
    get_filename_component(_thorium_instrument_name "${_thorium_instrument_dir}" NAME)
    # Binary dir explicitly named because the source dir is outside this one, and
    # CMAKE_BINARY_DIR rather than CMAKE_CURRENT_BINARY_DIR so the build tree
    # mirrors the source tree: instruments/ is a top-level concept that this
    # file merely configures, so burying its output under framework/hal/ would send
    # anyone looking for a driver's test binary to the wrong place.
    add_subdirectory("${_thorium_instrument_dir}"
        "${CMAKE_BINARY_DIR}/instruments/${_thorium_instrument_name}")
endforeach()

get_property(THORIUM_INSTRUMENT_TARGETS GLOBAL PROPERTY THORIUM_INSTRUMENT_TARGETS)
message(STATUS "Instrument drivers: ${THORIUM_INSTRUMENT_TARGETS}")

# --- The driver API boundary, actually enforced ---
# A driver depends on hal and never on hal_rig: it is written against the
# generic mechanism, not against this rig's instrument set (see
# instruments/README.md). The tempting assumption is that the target graph
# enforces this for free -- hal_rig links the drivers, so a driver linking
# hal_rig closes a cycle -- but it does not. CMake tolerates cycles among
# static libraries, and it accepts this one without a word: configure
# succeeds, the driver silently acquires the whole rig as a dependency, and
# the directory has quietly stopped being distributable on its own.
#
# So the rule is checked here rather than assumed. Direct link libraries only,
# which is the mistake that actually happens (a driver reaching for an Apply or
# for safeRig and adding hal_rig to make it compile); a driver that reaches
# hal_rig through some third target it invented would slip past this, and is
# not worth the generator-expression evaluation it would take to catch.
foreach(_thorium_instrument_target IN LISTS THORIUM_INSTRUMENT_TARGETS)
    get_target_property(_thorium_instrument_deps
        ${_thorium_instrument_target} INTERFACE_LINK_LIBRARIES)

    if(_thorium_instrument_deps MATCHES "hal_rig")
        message(FATAL_ERROR
            "Instrument driver target '${_thorium_instrument_target}' links hal_rig. "
            "A driver may depend on Thorium::hal (and core, transitively) and "
            "nothing else -- hal_rig is this rig's own instrument set, which "
            "depends on the drivers, not the other way round. Whatever the "
            "driver needs from hal_rig either belongs in generic hal or belongs "
            "in a rig-level test; see instruments/README.md.")
    endif()
endforeach()

# --- The driver headers the instrument table needs, generated from it ---
# Runs here because it needs both halves: the table (supplied by the rig, at
# the top of this file) and the driver package directories that must provide
# what the table names (configured just above). See
# cmake/InstrumentDrivers.cmake for what it reads out of the table and why
# this is generated rather than written alongside it.
#
# CMAKE_CONFIGURE_DEPENDS on the table itself, so adding an INSTRUMENT row for
# a driver not yet included re-runs this generation on the next build rather
# than failing to compile against a list from the previous configure.
include(InstrumentDrivers)

thorium_generate_instrument_drivers(
    "${THORIUM_INSTRUMENT_TABLE}"
    "${THORIUM_INSTRUMENT_DIRS}"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/hal"
)

set_property(DIRECTORY APPEND
    PROPERTY CMAKE_CONFIGURE_DEPENDS "${THORIUM_INSTRUMENT_TABLE}")

# --- hal_rig: the half that knows which instruments this rig actually has ---
# route/measure/safing/interlock all include hal/topology/active_instruments.hpp,
# so all four reach the rig's concrete driver globals (Dmm1, DcP1, ...) and therefore
# the driver headers behind them -- see this file's own comment on the split
# above. safing and interlock go furthest: both reflect over the global
# namespace for every hal::InstrumentTag-derived variable, which only works
# once those globals have actually been declared (see hal/src/verbs/safing.cpp and
# hal/src/verbs/interlock.cpp).
#
# src/verbs/source.cpp expands neither table -- Apply/Remove/Setup/Write hold no
# state at all (see core/verbs/source.hpp) -- and so would compile perfectly well in
# generic hal. It is here on purpose: hal is exactly what a driver is allowed
# to assume, and a driver reaching for an Apply is the mistake the check above
# exists to catch. Letting these four cross into hal would hand that reach to
# every driver, for no gain beyond one fewer file in this list.
# --- hal/verbs: the rig-bound instantiations of core's generic engines ---
# One per core/verbs header of the same name (core/verbs/measure.hpp <->
# hal/verbs/measure.hpp, and so on), which is why both trees spell the folder
# the same way. This set is exactly hal_rig: every one of them either opens the
# linking rig's active_instruments.hpp or is declared alongside something that
# does, which is the whole reason this target is separate from hal.
#
# acquire.hpp is listed here for the first time -- it was in neither target
# before, so it simply did not appear in an IDE's project tree. It belongs to
# this one: Arm and Await are defined in src/verbs/measure.cpp, not in a
# translation unit of their own (see that header's own comment on why).
add_library(hal_rig STATIC
    include/hal/topology/active_instruments.hpp  # header-only; the rig's instrument globals + fabric
    src/verbs/measure.cpp
    src/verbs/route.cpp
    src/verbs/source.cpp
    src/verbs/safing.cpp
    src/verbs/interlock.cpp
    include/hal/verbs/measure.hpp        # header-only
    include/hal/verbs/route.hpp          # header-only
    include/hal/verbs/source.hpp         # header-only
    include/hal/verbs/acquire.hpp        # header-only; Arm/Await defined in src/verbs/measure.cpp
    include/hal/verbs/trace.hpp          # header-only; Fetch defined in src/verbs/measure.cpp
    include/hal/verbs/safing.hpp         # header-only
    include/hal/verbs/interlock.hpp      # header-only
)

# PUBLIC on both counts. The driver targets carry the include directories the
# driver headers are found through, and the generated include list needs those
# wherever active_instruments.hpp is included -- which is not only here:
# rig_tests' test_safing.cpp includes it too, and so would any other rig-level
# test or a scripts layer that names an instrument global directly.
# THORIUM_INSTRUMENT_DRIVERS is PUBLIC for that same reason. (The generated
# file spells each include "hal/<ns>.hpp", exactly as every call site does, so
# it resolves through those same directories rather than through where it was
# generated.)
target_link_libraries(hal_rig PUBLIC hal ${THORIUM_INSTRUMENT_TARGETS})

target_compile_definitions(hal_rig PUBLIC
    THORIUM_INSTRUMENT_DRIVERS="${THORIUM_INSTRUMENT_DRIVERS}"
)

# PRIVATE, unlike the two above: rig/wiring.inc is expanded by src/verbs/route.cpp,
# src/verbs/measure.cpp and src/verbs/interlock.cpp and nowhere else at all -- it builds
# file-local wiring tables inside those three translation units, and no header
# exposes it. That is exactly why hal::energisedSourceAt is an ordinary
# declared-here/defined-there function rather than a template: the interlock's
# answer needs this table, and the call sites that ask for it (every script
# that measures) must not gain it. See hal/verbs/interlock.hpp.
target_compile_definitions(hal_rig PRIVATE
    THORIUM_WIRING_TABLE="${THORIUM_WIRING_TABLE}"
)

add_library(Thorium::hal_rig ALIAS hal_rig)

install(TARGETS hal_rig
    EXPORT ThoriumTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

if(BUILD_TESTING_LAYERS)
    # Linked against hal, not hal_rig -- and that is the whole point of what is
    # left in this directory. The drivers took their own tests to
    # instruments/*/tests/, and the ones that needed this rig rather than a
    # mechanism (test_call_syntax, test_safing, test_interlock,
    # test_source_instruments, test_describe, test_source_readback) went to
    # rig/tests/. What remains
    # names no driver, includes neither active_instruments.hpp nor
    # THORIUM_WIRING_TABLE, and tests only hal's own generic machinery: the
    # fabric, the wiring tables, VpcLocation, the adapter macros, addresses,
    # and what is left of test_instrument.cpp.
    #
    # So the link line is now an assertion rather than a detail. A test added
    # here that reaches for an instrument global, an Apply or safeRig() fails to
    # link, and the failure says what it is: a rig-level test in the generic
    # library's directory. Its home is rig/tests/ -- see rig/README.md.
    add_layer_tests(hal hal ${CMAKE_CURRENT_SOURCE_DIR}/tests)
endif()
