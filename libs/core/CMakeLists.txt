add_library(core STATIC
        src/logger.cpp
        src/verify.cpp
        src/quantity_kind.cpp
        src/recording.cpp
        src/session.cpp
        src/stimulus.cpp
        src/format.cpp
        src/bench.cpp
        src/interlock.cpp
        src/bytes.cpp
        src/waveform.cpp
        src/journal.cpp
        src/criteria_variants.cpp
        src/report.cpp
        src/console_sink.cpp
        src/rtf_sink.cpp
        src/sarif_sink.cpp
        include/core/bench.hpp           # header-only apart from src/bench.cpp
        include/core/quantity.hpp        # header-only; listed so it shows up in IDEs
        include/core/quantity_kind.hpp   # header-only
        include/core/recording.hpp       # header-only
        include/core/session.hpp         # header-only
        include/core/stimulus.hpp        # header-only apart from src/stimulus.cpp
        include/core/port.hpp            # header-only
        include/core/measure.hpp         # header-only
        include/core/adapter.hpp         # header-only
        include/core/at.hpp              # header-only
        include/core/source.hpp          # header-only
        include/core/route.hpp           # header-only
        include/core/describe.hpp        # header-only
        include/core/transfer.hpp        # header-only
        include/core/predicates.hpp      # header-only
        include/core/criterion.hpp       # header-only
        include/core/active_criteria.hpp # header-only
        include/core/criteria_variants.hpp
        include/core/meta.hpp            # header-only
        include/core/format.hpp          # header-only apart from src/format.cpp
        include/core/bytes.hpp           # header-only apart from src/bytes.cpp
        include/core/waveform.hpp        # header-only apart from src/waveform.cpp
        include/core/trace.hpp           # header-only
        include/core/predicate_text.hpp  # header-only
        include/core/journal.hpp
        include/core/report.hpp
        include/core/console_sink.hpp
        include/core/rtf_sink.hpp
        include/core/sarif_sink.hpp
)

target_include_directories(core PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

# --- Build-stamped traceability for the run logs ---
# Both log streams carry a header saying which framework version, which DUT and
# which rig a run was performed with (see core::RunInfo in core/journal.hpp).
# These are the half of that header that has to come from the build rather than
# from the command line: they describe the binary itself, so baking them in is
# what makes them impossible for a caller to misreport.
#
# The criteria variant used to be in that half and no longer is: every variant
# is now compiled in and a run selects one with --criteria=, so the log reports
# core::activeCriteriaVariantName() -- what the run actually applied -- rather
# than a build-time constant that would now only describe the default. What the
# build still supplies is the two definitions below it: which names are legal,
# and which one applies when the flag is absent.
#
# PRIVATE, not PUBLIC: only src/journal.cpp reads them, and a consumer of
# Thorium::core has no reason to be recompiled when a rig renames itself.
# core/journal.cpp defaults each one to "unknown" if unset, so core still
# builds standalone -- see its own comment on why an obviously-absent value
# beats a plausible wrong one.
#
# THORIUM_DUT_NAME / THORIUM_RIG_NAME are set here rather than derived from
# dut/ or rig/: neither of those is reachable from here (libs/ knows nothing
# about this deployment's content -- see the top-level CMakeLists.txt's own
# comment on that split), and the DUT a rig tests is exactly the kind of fact a
# separate rig repo consuming this framework needs to set for itself.
#
# dut/adapter.inc could not supply THORIUM_DUT_NAME even if it were reachable:
# ADAPTER takes no name at all any more (see libs/hal/include/hal/adapter.hpp).
# The adapter table opens the fixed `dut` struct, because a build targets one
# DUT and every script call site would otherwise repeat a name the build has
# already fixed. THORIUM_DUT_NAME is the separate, display-only fact -- free to
# be "Device X rev B", spaces and all, since nothing compiles against it.
#
# Deliberately NOT `set(... CACHE STRING ...)`, which is what these were first
# written as and which is a trap for exactly this kind of value. A cache
# variable's default is consulted only when the entry does not already exist, so
# renaming the DUT here had no effect at all on an already-configured build
# directory: the cached old name stayed, the compile definition kept it, and both
# logs went on reporting the previous DUT. A stale name in a traceability field
# is the worst possible failure mode -- it is wrong, it is plausible, and nothing
# about the log says so.
#
# This form makes the repository the source of the default (edit it, reconfigure,
# done) while still letting a caller override per build:
#
#     cmake -B <build-dir> -DTHORIUM_DUT_NAME=DeviceY
#
# -D creates a real cache entry, which `DEFINED` sees, so an explicit override
# still wins and still persists across reconfigures.
if(NOT DEFINED THORIUM_DUT_NAME)
    set(THORIUM_DUT_NAME "DeviceX")
endif()

if(NOT DEFINED THORIUM_RIG_NAME)
    set(THORIUM_RIG_NAME "thorium-rig-1")
endif()

message(STATUS "DUT: ${THORIUM_DUT_NAME} on rig: ${THORIUM_RIG_NAME}")

# THORIUM_SUITE_VERSION / THORIUM_DUT_VERSION / THORIUM_RIG_VERSION are set by
# the top-level CMakeLists.txt (derived from git -- see its own comment on why
# they live there rather than here: detecting the *enclosing deployment's*
# revision is not something the framework can sensibly do for itself). Empty
# when core is configured standalone, which journal.cpp reports as "unknown".
target_compile_definitions(core PRIVATE
        THORIUM_FRAMEWORK_VERSION="${PROJECT_VERSION}"
        THORIUM_CRITERIA_VARIANT_NAMES="${THORIUM_CRITERIA_VARIANT_NAMES}"
        THORIUM_DEFAULT_CRITERIA_VARIANT="${THORIUM_CRITERIA_VARIANT}"
        THORIUM_DUT_NAME="${THORIUM_DUT_NAME}"
        THORIUM_RIG_NAME="${THORIUM_RIG_NAME}"
        THORIUM_SUITE_VERSION="${THORIUM_SUITE_VERSION}"
        THORIUM_DUT_VERSION="${THORIUM_DUT_VERSION}"
        THORIUM_RIG_VERSION="${THORIUM_RIG_VERSION}"
)

# Thorium::core -- usable under this name whether core is consumed in-tree
# (this repo's own dut/dsl/hal) or by a separate rig repo pulling this
# package in via add_subdirectory/FetchContent, which gets no automatic
# namespacing the way find_package(Thorium) + ThoriumTargets.cmake does; see
# the top-level CMakeLists.txt's own comment on the Thorium package export.
add_library(Thorium::core ALIAS core)

install(TARGETS core
    EXPORT ThoriumTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

if(BUILD_TESTING_LAYERS)
    add_layer_tests(core core ${CMAKE_CURRENT_SOURCE_DIR}/tests)

    # --- Fixture catalog, for tests/test_run_hooks.cpp ---
    # A catalog declaring both RUN_SETUP and RUN_TEARDOWN, where the shipped
    # suite/test_catalog.inc declares only the latter -- see that test's own
    # header comment for why the undeclared-hook half needs no fixture.
    #
    # Absolute paths for the same reason app/CMakeLists.txt uses them for
    # THORIUM_TEST_CATALOG proper: the #include naming them lives in
    # core/active_test_catalog.hpp, so a path relative to the test file that
    # sets them would be resolved from the wrong directory entirely.
    #
    # Separate names rather than defining THORIUM_TEST_CATALOG itself, so this
    # reaches exactly the one translation unit that opts in by redefining them,
    # and no other core test acquires a catalog it never asked for.
    target_compile_definitions(core_tests PRIVATE
        THORIUM_TEST_CATALOG_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/hooked_catalog.inc"
        THORIUM_TEST_SCRIPTS_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/hooked_scripts.hpp"
    )
endif()
