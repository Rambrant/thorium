add_library(core STATIC
        src/quantities/quantity_kind.cpp
        src/quantities/format.cpp
        src/quantities/bytes.cpp
        src/quantities/waveform.cpp
        src/session/session.cpp
        src/session/recording.cpp
        src/session/stimulus.cpp
        src/session/bench.cpp
        src/journal/journal.cpp
        src/journal/report.cpp
        src/journal/human_sink.cpp
        src/journal/console_sink.cpp
        src/journal/rtf_sink.cpp
        src/journal/sarif_sink.cpp
        src/criteria/verify.cpp
        src/criteria/criteria_variants.cpp
        src/verbs/interlock.cpp
        # --- Ungrouped ---
        # meta.hpp is generic enum reflection: it belongs to no layer, is used
        # by several, and is the one file in core with no group.
        include/core/meta.hpp   # header-only
        # --- core/driver: what an instrument driver names from core ---
        # Every driver under instruments/ includes both, and neither is a verb:
        # a driver supplies ports and says how its config is logged, the verbs
        # drive them. Named to match hal/driver/, which is the other half of
        # the same kit. Not the complete surface a driver may use -- that also
        # takes quantities/ and meta.hpp -- but these are the two that are
        # about being a driver rather than about values.
        include/core/driver/port.hpp      # header-only
        include/core/driver/describe.hpp  # header-only
        # --- core/topology: the generic half of the DUT-point machinery,
        # whose concrete half is hal/topology/adapter.hpp. Loc is a template
        # parameter and not hal::VpcLocation on purpose (see its own comment)
        # -- that genericity is exactly what keeps this half in core, and why
        # it could never move to hal: core/verbs/route.hpp and
        # core/verbs/measure.hpp both include it.
        include/core/topology/adapter.hpp  # header-only
        # --- core/catalog: the GROUP/TEST tables a suite declares its scripts
        # in, and the build-time injection point that pulls the active one in.
        # active_test_catalog.hpp is to test_catalog.hpp what
        # criteria/active_criteria.hpp is to criteria/criterion.hpp -- see its
        # own comment, which says exactly that.
        include/core/catalog/test_catalog.hpp         # header-only
        include/core/catalog/active_test_catalog.hpp  # header-only
        # --- core/quantities: the unit-tagged value types and the arithmetic
        # over them, plus the two non-quantity payload types a recorded sample
        # can also hold (Bytes, Waveform) and the rendering of all of them.
        # The leaf layer: nothing here includes anything else in core, and
        # core::quantities is the namespace a criteria file opens by name.
        include/core/quantities/quantity.hpp       # header-only
        include/core/quantities/transducer.hpp     # header-only
        include/core/quantities/quantity_kind.hpp  # header-only apart from src/quantities/quantity_kind.cpp
        include/core/quantities/format.hpp         # header-only apart from src/quantities/format.cpp
        include/core/quantities/bytes.hpp          # header-only apart from src/quantities/bytes.cpp
        include/core/quantities/waveform.hpp       # header-only apart from src/quantities/waveform.cpp
        # --- core/session: where a reading comes from, and whether an
        # instruction goes anywhere. session.hpp is live/replayed/injected
        # value sourcing, recording.hpp the on-disk sample format, stimulus.hpp
        # the injection side, and bench.hpp the attachment flag -- the two
        # halves --replay/--inject/--skeleton set together (see bench.hpp's own
        # comment on why they are separate facts).
        include/core/session/session.hpp    # header-only apart from src/session/session.cpp
        include/core/session/recording.hpp  # header-only apart from src/session/recording.cpp
        include/core/session/stimulus.hpp   # header-only apart from src/session/stimulus.cpp
        include/core/session/bench.hpp      # header-only apart from src/session/bench.cpp
        # --- core/verbs: the generic engines, each of which hal instantiates
        # for this rig under the same name (core/verbs/measure.hpp <->
        # hal/verbs/measure.hpp, and so on for route/source/acquire/trace). at.hpp is
        # the call-site sugar their arguments are spelled with; interlock.hpp
        # is the electrical gate measure and route consult before energising.
        include/core/verbs/measure.hpp   # header-only
        include/core/verbs/route.hpp     # header-only
        include/core/verbs/source.hpp    # header-only
        include/core/verbs/acquire.hpp   # header-only
        include/core/verbs/trace.hpp     # header-only
        include/core/verbs/transfer.hpp  # header-only
        include/core/verbs/at.hpp        # header-only
        include/core/verbs/interlock.hpp # header-only apart from src/verbs/interlock.cpp
        # --- core/journal: the machine-readable event stream, and the sinks
        # that render it. journal.hpp is the one every other layer posts to and
        # is the group's only inward-facing header; report.hpp and
        # human_sink.hpp are the two halves the human-readable sinks (console,
        # rtf) share -- what the stream says, and which hook says it -- leaving
        # each sink only its own medium, while sarif_sink writes the
        # machine-readable stream. The group depends on nothing else in core.
        include/core/journal/journal.hpp
        include/core/journal/report.hpp
        include/core/journal/human_sink.hpp
        include/core/journal/console_sink.hpp
        include/core/journal/rtf_sink.hpp
        include/core/journal/sarif_sink.hpp
        # --- core/criteria: the CRITERIA/CRIT tables, the predicate vocabulary
        # they are written in, how a run picks a tolerance variant, and Verify.
        # Closed against the rest of core: only verify.hpp is reached from
        # outside the group, and the group reaches out only to quantities/
        # and journal/.
        include/core/criteria/criterion.hpp        # header-only
        include/core/criteria/active_criteria.hpp  # header-only
        include/core/criteria/predicates.hpp       # header-only
        include/core/criteria/predicate_text.hpp   # header-only
        include/core/criteria/verify.hpp           # header-only apart from src/criteria/verify.cpp
        include/core/criteria/criteria_variants.hpp
        # --- core/testing: what a deployment's own tests are written with ---
        # Shipped and installed like any other header, because a deployment's
        # rig/ and suite/ tests are not this framework's tests and cannot reach
        # into framework/core/tests/ -- see core::CapturingSink's own comment.
        # Nothing in core links against it; it is here so that the three test
        # targets that need it (core's, the rig's, the suite's) all have it.
        include/core/testing/capturing_sink.hpp    # header-only
)

target_include_directories(core PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

# --- Build-stamped traceability for the run logs ---
# Both log streams carry a header saying which framework version, which DUT and
# which rig a run was performed with (see core::RunInfo in core/journal/journal.hpp).
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
# PRIVATE, not PUBLIC: only src/journal/journal.cpp reads them, and a consumer of
# Thorium::core has no reason to be recompiled when a rig renames itself.
# src/journal/journal.cpp defaults each one to "unknown" if unset, so core still
# builds standalone -- see its own comment on why an obviously-absent value
# beats a plausible wrong one.
#
# THORIUM_DUT_NAME / THORIUM_RIG_NAME are set here rather than derived from
# dut/ or rig/: neither of those is reachable from here (framework/ knows nothing
# about this deployment's content -- see the top-level CMakeLists.txt's own
# comment on that split), and the DUT a rig tests is exactly the kind of fact a
# separate rig repo consuming this framework needs to set for itself.
#
# dut/adapter.inc could not supply THORIUM_DUT_NAME even if it were reachable:
# ADAPTER takes no name at all any more (see framework/hal/include/hal/topology/adapter.hpp).
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

    # --- Fixture catalog, for tests/catalog/test_run_hooks.cpp ---
    # A catalog declaring all four hooks -- RUN_SETUP/RUN_TEARDOWN and, on one of
    # its two groups, SETUP/TEARDOWN -- where the shipped suite/test_catalog.inc
    # declares only the run-level pair. See that test's own header comment for
    # why the undeclared-hook half needs no fixture of its own.
    #
    # framework/runner's, not one of core's. There used to be a copy of it here, and
    # the copy was the problem rather than the duplication: the two files stated
    # the same catalog in the same shape and were free to drift, so a change to
    # the hook mechanism could be made to pass here against a fixture the runner
    # no longer matched. One fixture cannot do that. What it costs is that this
    # test's expected descriptions are the ones framework/runner's acceptance tests
    # print and assert on -- editing them for either reason fails the other,
    # loudly and in one obvious place, which is the trade this is making.
    #
    # A test-only path, and it points *up* the layer stack. Nothing in framework/core
    # itself acquires a dependency on framework/runner; core_tests reads one file from
    # it, the same way it reads a path handed in by whoever configures the build.
    #
    # Absolute paths for the same reason framework/runner/CMakeLists.txt uses them for
    # THORIUM_TEST_CATALOG proper: the #include naming them lives in
    # core/catalog/active_test_catalog.hpp, so a path relative to the test file that
    # sets them would be resolved from the wrong directory entirely.
    #
    # Separate names rather than defining THORIUM_TEST_CATALOG itself, so this
    # reaches exactly the one translation unit that opts in by redefining them,
    # and no other core test acquires a catalog it never asked for.
    target_compile_definitions(core_tests PRIVATE
        THORIUM_TEST_CATALOG_FIXTURE="${CMAKE_SOURCE_DIR}/framework/runner/tests/fixtures/hooked_catalog.inc"
        THORIUM_TEST_SCRIPTS_FIXTURE="${CMAKE_SOURCE_DIR}/framework/runner/tests/fixtures/hooked_scripts.hpp"
    )
endif()
