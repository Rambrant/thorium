# --- Rig-specific data this library needs but does not itself define ---
# hal is a generic instrument-driver + switching-fabric library -- meant to
# be linked by many rigs testing many DUTs, not just this repo's (see
# README.md) -- so it cannot hardcode which instruments a given rig has,
# how they're wired, or what to call them. Whoever configures a build that
# includes this directory supplies that as three file paths: this repo's own
# top-level CMakeLists.txt does it below (pointing at ./rig/), the same way
# a separate rig's own top-level CMakeLists.txt would after pulling this
# library in via add_subdirectory/FetchContent. Same compile-definition-
# swap mechanism core/active_criteria.hpp already uses for
# THORIUM_CRITERIA_VARIANT_TABLES/THORIUM_TEST_CATALOG -- see rig/README.md for
# what each of these three files actually contains.
set(THORIUM_ACTIVE_INSTRUMENTS "" CACHE FILEPATH
    "Path to the linking rig's active_instruments.hpp (its concrete instrument globals + fabric)")
set(THORIUM_INSTRUMENT_TABLE "" CACHE FILEPATH
    "Path to the linking rig's instrument.inc (its fixed instrument list, and hal::InstrumentId's enumerators)")
set(THORIUM_WIRING_TABLE "" CACHE FILEPATH
    "Path to the linking rig's wiring.inc (its fixed instrument/connector wiring)")

foreach(_thorium_rig_var THORIUM_ACTIVE_INSTRUMENTS THORIUM_INSTRUMENT_TABLE THORIUM_WIRING_TABLE)
    if(NOT ${_thorium_rig_var} OR NOT EXISTS "${${_thorium_rig_var}}")
        message(FATAL_ERROR
            "${_thorium_rig_var} must point at an existing file -- hal is a generic "
            "library and needs the linking rig to supply its own instrument/wiring "
            "data before it will configure; see libs/hal/README.md.")
    endif()
endforeach()

# --- Two targets, not one: the generic half and the rig-coupled half ---
# This library used to be a single "hal" target, and that hid a real
# asymmetry. Four of its translation units (vpc_location, switch_fabric,
# instrument, wiring) compile against nothing but core and their own
# generic headers. The other three (apply, measure, safing) open the
# linking rig's active_instruments.hpp -- which declares that rig's
# instrument globals, and so #includes that rig's concrete driver headers.
# One target meant one compile-time dependency set, so the generic four
# inherited the rig coupling of the other three whether they used it or
# not.
#
# That distinction is what makes an instruments/ tree possible at all. A
# driver has to compile against hal (it needs InstrumentId, the Port
# machinery, the fabric, the describe customization point), so hal must be
# configurable and buildable *before* any driver is. But active_instruments.hpp
# reaches the drivers, so anything expanding it must come *after* them.
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
    src/vpc_location.cpp
    src/switch_fabric.cpp
    src/instrument.cpp
    src/wiring.cpp
    include/hal/vpc_location.hpp   # header-only
    include/hal/switch_fabric.hpp  # header-only
    include/hal/instrument.hpp     # header-only
    include/hal/wiring.hpp         # header-only
    include/hal/adapter.hpp        # header-only
    include/hal/describe.hpp       # header-only

    # Drivers still awaiting their move out to instruments/ -- listed here so
    # they stay visible in an IDE until they go, not because they belong to
    # this target. Each leaves as its own directory, the way l4411a already
    # has; see instruments/README.md.
    include/hal/n6701a.hpp         # header-only
    include/hal/ac6677a.hpp        # header-only
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

# PUBLIC: hal/instrument.hpp (a public header) expands THORIUM_INSTRUMENT_TABLE
# itself, to generate hal::InstrumentId's enumerators -- see that header's own
# comment -- so it has to reach anyone who includes that header, which is every
# driver and every consumer of this library. Same reason
# THORIUM_TEST_CATALOG/THORIUM_TEST_SCRIPTS are PUBLIC on app/'s scripts target
# rather than PRIVATE like THORIUM_CRITERIA_VARIANT_TABLES (whose only consumer
# is scripts' own .cpp files).
#
# Note this is only the instrument *list* (rig/instrument.inc: ids and driver
# types as bare tokens), not the instrument *globals*. Expanding it needs no
# driver header, which is precisely why generic hal can carry it and why
# hal::InstrumentId exists before any driver does. THORIUM_ACTIVE_INSTRUMENTS
# and THORIUM_WIRING_TABLE belong to hal_rig below, for the opposite reason.
target_compile_definitions(hal PUBLIC
    THORIUM_INSTRUMENT_TABLE="${THORIUM_INSTRUMENT_TABLE}"
)

# --- The rig's instrument drivers ---
# Each driver is its own self-contained directory with its own CMakeLists.txt,
# its own include/hal/<model>.hpp and its own tests -- packageable and
# distributable one driver at a time, which is the whole point (see
# instruments/README.md). This library does not go looking for them: the
# linking rig supplies the list as directory paths in THORIUM_INSTRUMENT_DIRS,
# exactly the way it already supplies its three .inc/.hpp file paths above,
# so libs/ still reaches nothing outside itself. This repo's own top-level
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
    # file merely configures, so burying its output under libs/hal/ would send
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

# --- hal_rig: the half that knows which instruments this rig actually has ---
# apply/measure/safing all expand THORIUM_ACTIVE_INSTRUMENTS, so all three
# reach the rig's concrete driver globals (Dmm1, DcP1, ...) and therefore the
# driver headers behind them -- see this file's own comment on the split above.
# hal::safeRig() goes furthest: it reflects over the global namespace for every
# hal::InstrumentTag-derived variable, which only works once those globals have
# actually been declared (see hal/src/safing.cpp).
add_library(hal_rig STATIC
    src/measure.cpp
    src/apply.cpp
    src/safing.cpp
    include/hal/measure.hpp        # header-only
    include/hal/apply.hpp          # header-only
    include/hal/safing.hpp         # header-only
)

# PUBLIC on both counts. The driver targets carry the include directories the
# driver headers are found through, and active_instruments.hpp needs those
# wherever it is expanded -- which is not only here: hal_tests' test_safing.cpp
# expands it too, and so would a rig-level test or a scripts layer that names
# an instrument global directly. THORIUM_ACTIVE_INSTRUMENTS is PUBLIC for that
# same reason.
target_link_libraries(hal_rig PUBLIC hal ${THORIUM_INSTRUMENT_TARGETS})

target_compile_definitions(hal_rig PUBLIC
    THORIUM_ACTIVE_INSTRUMENTS="${THORIUM_ACTIVE_INSTRUMENTS}"
)

# PRIVATE, unlike the two above: rig/wiring.inc is expanded by src/apply.cpp
# and src/measure.cpp and nowhere else at all -- it builds file-local wiring
# tables inside those two translation units, and no header exposes it.
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
    # Linked against hal_rig, not hal: test_safing.cpp expands
    # THORIUM_ACTIVE_INSTRUMENTS and the rest exercise Apply/Measure/safeRig,
    # all of which live there. The four genuinely generic test files
    # (vpc_location, switch_fabric, wiring, and what is left of
    # test_instrument.cpp) would link against bare hal quite happily -- see
    # libs/hal/README.md on splitting this target so that stays true by
    # construction rather than by accident.
    add_layer_tests(hal hal_rig
        tests/test_vpc_location.cpp
        tests/test_switch_fabric.cpp
        tests/test_instrument.cpp
        tests/test_wiring.cpp
        tests/test_source_instruments.cpp
        tests/test_call_syntax.cpp
        tests/test_safing.cpp
        tests/test_describe.cpp
        tests/test_source_readback.cpp
    )
endif()
