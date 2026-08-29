# The test-suite runner: src/main.cpp and its command-line parser, plus the
# "scripts" library of whatever THORIUM_SUITE_DIR holds. Framework, all of it --
# which is why it sits under framework/ beside core and hal rather than at the top
# level as app/ did.
#
# The move was not a tidy-up. app/ held nothing DUT-specific and never had, but
# its position said otherwise: a deployment reading the layer list saw rig/,
# dut/, suite/ *and* app/ at the same level and could reasonably conclude it was
# expected to bring a main() of its own. It is not. A deployment brings content
# -- three directories of tables and scripts -- and this library brings
# everything that runs it.
#
# What made the position look justified was two compile definitions
# (THORIUM_TEST_CATALOG, THORIUM_TEST_SCRIPTS) naming files under the
# deployment's suite. That is not a dependency on the deployment; it is the same
# parameterisation framework/hal already uses for its four rig files -- a framework/
# directory that #includes a path whoever configures the build supplies (see
# framework/hal/CMakeLists.txt's own comment, and framework/hal/README.md). Reading a path
# is what makes this framework portable, not what makes it deployment-specific.
#
# The actual test scripts (THORIUM_SUITE_DIR/scripts/*.cpp) are NOT built here
# as a fixed list -- they're discovered from the deployment's suite, kept clean
# of any CMakeLists.txt of its own, the same way its criteria_*.inc and
# test_catalog.inc are just content, not build machinery. CONFIGURE_DEPENDS
# makes CMake re-glob and reconfigure whenever that directory's contents change,
# so a new scripts/*.cpp just needs to exist to be picked up on the next build --
# no CMakeLists.txt edit.
file(GLOB THORIUM_SUITE_SCRIPT_SOURCES CONFIGURE_DEPENDS "${THORIUM_SUITE_DIR}/scripts/*.cpp")

# Target named "scripts" rather than after this directory: it is a library of
# compiled test scripts, which is what it holds and what every reference to it
# calls it. The runner is the executable below.
add_library(scripts STATIC
    ${THORIUM_SUITE_SCRIPT_SOURCES}
)

# hal_rig, not hal: a script names this rig's instrument globals (Dmm1, DcP1,
# ...) and calls Measure/Apply/safeRig on them, all of which live in hal_rig --
# see framework/hal/CMakeLists.txt on why that is a separate target from the generic
# hal the drivers themselves compile against. hal comes along transitively.
#
# src/main.cpp needs the same three (Measure from hal/verbs/measure.hpp, safeRig and
# RigSafingGuard from hal/verbs/safing.hpp), and reaches them through this target,
# which run_scripts links below.
target_link_libraries(scripts PUBLIC hal_rig core)

target_include_directories(scripts PUBLIC ${CMAKE_SOURCE_DIR})

# --- Criteria/tolerance variants ---
# Points core/criteria/active_criteria.hpp at the two files it reads.
#
# THORIUM_CRITERIA_VARIANT_TABLES is generated (cmake/CriteriaVariants.cmake,
# called from the top-level CMakeLists.txt) and holds *every* known variant's
# .inc file, each in its own namespace, plus the X-macro that iterates them.
# There is no longer a THORIUM_ACTIVE_CRITERIA: no single variant is "the
# active one" at build time, since the binary carries all of them and a run
# picks with --criteria=.
#
# THORIUM_PRODUCTION_CRITERIA is a second, fixed path -- always
# criteria_production.inc -- and now has two jobs: other variants reference "the
# same value as production" through it via CRIT_FROM_PRODUCTION, and it is the
# reference table the merged criteria are generated from (see
# core/criteria/active_criteria.hpp's pass 2 on why it is the right one for that).
#
# Absolute paths, so no extra include-directory wrangling. The criteria live
# under the deployment's dut/, not here, because the tolerances they hold are a
# property of the DUT being tested (the same reasoning that put adapter.inc
# there) -- see dut/README.md.
target_compile_definitions(scripts PRIVATE
    THORIUM_PRODUCTION_CRITERIA="${THORIUM_DUT_DIR}/criteria_production.inc"
    THORIUM_CRITERIA_VARIANT_TABLES="${THORIUM_CRITERIA_VARIANT_TABLES}"
)

# --- Test catalog ---
# THORIUM_TEST_CATALOG points core/catalog/active_test_catalog.hpp at the deployment's
# test_catalog.inc; THORIUM_TEST_SCRIPTS points it at its scripts.hpp (the
# declarations every TEST(...) is checked against -- declarations only, which is
# why pointing main.cpp at it costs nothing; what a script body needs is in the
# suite's prelude.hpp instead, and nothing here points at that). Both absolute
# paths, both PUBLIC (unlike the criteria defines above): src/main.cpp includes
# active_test_catalog.hpp too, and it's compiled as part of run_scripts, not
# this scripts library, so it needs these definitions to reach it via
# target_link_libraries below.
target_compile_definitions(scripts PUBLIC
    THORIUM_TEST_CATALOG="${THORIUM_SUITE_DIR}/test_catalog.inc"
    THORIUM_TEST_SCRIPTS="${THORIUM_SUITE_DIR}/scripts.hpp"
)

# Runner: executes all scripts against a real (or simulated) device
# and exits non-zero if any script fails.
#
# src/cli.hpp is not in an include/ directory and is not installed, unlike every
# header in framework/core and framework/hal. That is the point its own comment makes: it
# is a general-purpose annotation-driven argument parser with exactly one
# consumer, and making it installed public surface would be committing this
# framework to maintaining it as such. It is this runner's parser and lives
# beside the only file that includes it.
add_executable(run_scripts src/main.cpp)
target_link_libraries(run_scripts PRIVATE scripts)

# Pinned rather than left to land beside this CMakeLists.txt, so the path a
# developer types is a property of the build tree instead of a property of where
# in the source tree the runner happens to be declared. It used to be
# build/<cfg>/app/run_scripts, which is exactly the coupling this move removes
# everywhere else; README.md and tools/run-tests.sh would otherwise have had to
# learn a new directory name for the same binary, and again the next time.
set_target_properties(run_scripts PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

install(TARGETS run_scripts RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

# --- manifest.json, for a web server discovering installed suite binaries ---
# The web server that supervises test runs picks which suite to run, which
# tests within it, and which criteria variant, by scanning an install prefix
# for run_scripts binaries and reading a manifest next to each one -- rather
# than parsing this build's C++ or knowing anything about THORIUM_CRITERIA_
# VARIANT/test_catalog.inc itself. Generated at install time, not
# build time or configure time: it has to reflect the actual installed
# binary (--list-tests is the catalog's own source of truth -- see
# src/main.cpp's listTests()), and install is the first point a fully
# linked run_scripts exists to ask.
#
# install(CODE ...) here only sets variables (note the generator expression
# $<TARGET_FILE:run_scripts> -- CMAKE_INSTALL_PREFIX below is intentionally
# left as the literal string \${CMAKE_INSTALL_PREFIX}, evaluated when
# cmake_install.cmake actually runs rather than at configure time, since
# --prefix can override it at install time); the actual work is
# cmake/GenerateManifest.cmake, run via install(SCRIPT ...) right after --
# both calls share cmake_install.cmake's variable scope, the ordinary way to
# hand values into an install-time script.
install(CODE "set(THORIUM_RUN_SCRIPTS_EXE \"$<TARGET_FILE:run_scripts>\")")
install(CODE "set(THORIUM_KNOWN_CRITERIA_VARIANTS \"${THORIUM_KNOWN_CRITERIA_VARIANTS}\")")
install(CODE "set(THORIUM_CRITERIA_VARIANT \"${THORIUM_CRITERIA_VARIANT}\")")
install(CODE "set(THORIUM_MANIFEST_OUTPUT \"\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}/manifest.json\")")
install(SCRIPT "${CMAKE_SOURCE_DIR}/cmake/GenerateManifest.cmake")

if(BUILD_TESTING_LAYERS)
    # These test the deployment's scripts/*.cpp and criteria_*.inc -- suite
    # content, not this runner -- so their sources live in the deployment's
    # tests/, not here. This is still where the target itself is defined, same
    # as its scripts/*.cpp are discovered from here rather than built via a
    # CMakeLists.txt of its own.
    #
    # Which is also why the directory is what gets passed: add_layer_tests
    # globs it (see cmake/FetchGTest.cmake), so a new suite tests/*.cpp needs
    # no edit here -- exactly like the scripts/*.cpp glob above, and for
    # a rather more pointed reason. A script this file failed to build fails to
    # link; a *test* this file failed to build just silently never runs.
    add_layer_tests(scripts scripts ${THORIUM_SUITE_DIR}/tests)

    # --- A second runner, over a catalog that declares hooks at both levels ---
    # The shipped suite's test_catalog.inc declares RUN_SETUP/RUN_TEARDOWN and no
    # group hooks, and neither of its two power scripts can show what the
    # acceptance tests need to see: they print no ordering markers and
    # rigPowerOff has no failure to report. This target is the same main.cpp over
    # a fixture catalog whose hooks -- both levels of them -- announce themselves
    # and can be made to fail on demand, which is what lets the tests assert the
    # things only a real run can show: that RUN_SETUP precedes every script and
    # RUN_TEARDOWN follows the last one, that both happen once around all of
    # --repeat's passes rather than once per pass, that a group's own
    # SETUP/TEARDOWN run per pass and only for a group the selection reached, and
    # that every teardown still runs when a script throws.
    #
    # Same main.cpp deliberately, not a stand-in: a test binary that reimplemented
    # the run loop would be testing the reimplementation. The catalog is the only
    # thing that differs, which is exactly the variable under test.
    #
    # Framework-side, and now visibly so: the fixture names no instrument and no
    # DUT point, what it tests is main.cpp's hook ordering, and it is built
    # unconditionally with the rest of BUILD_TESTING_LAYERS rather than under the
    # deployment-specific option below. It used to be gated with the acceptance
    # tests only because both halves lived in one directory; separating them was
    # the follow-up that comment asked for.
    add_library(hooked_scripts STATIC tests/fixtures/hooked_scripts.cpp)
    target_link_libraries(hooked_scripts PUBLIC hal_rig core)
    target_include_directories(hooked_scripts PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures)
    target_compile_definitions(hooked_scripts PUBLIC
        THORIUM_TEST_CATALOG="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/hooked_catalog.inc"
        THORIUM_TEST_SCRIPTS="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/hooked_scripts.hpp"
    )

    add_executable(run_scripts_hooked src/main.cpp)
    target_link_libraries(run_scripts_hooked PRIVATE hooked_scripts)

    set_target_properties(run_scripts_hooked PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

    # --- Acceptance tests: on for a deployment that brings its own ---
    # Black-box tests over the built run_scripts, and the one target in this
    # build that is deployment *content*: they drive the binary as a subprocess
    # and assert on what comes back -- which means asserting on this suite's
    # group and test names, this DUT's name in the report header, and this rig's
    # instruments in the log. None of that is true of a second deployment.
    #
    # So the sources come from the deployment, globbed out of
    # THORIUM_ACCEPTANCE_DIR the same way its scripts/ and tests/ are, and this
    # framework directory holds none of them. That is what the option is for: ON
    # by default, so a deployment that brings acceptance tests gets them with no
    # configuration at all, and a deployment that brings none says so once in
    # its preset (see dev/README.md and CMakePresets.json's dev-deployment).
    #
    # A directory of its own, beside rig/ dut/ suite/ rather than inside one of
    # them, because the facts it asserts are spread across all three and belong
    # to no single leg. Note in particular that it cannot live under
    # THORIUM_SUITE_DIR/tests: add_layer_tests() globs that path with
    # GLOB_RECURSE, so at any depth these sources would also be swept into
    # scripts_tests, where the compile definitions below are not defined. See
    # cmake/FetchGTest.cmake's note on why sources under a tests/ tree that
    # belong to another target must sit outside every path handed to it.
    option(THORIUM_ACCEPTANCE_TESTS
        "Build THORIUM_ACCEPTANCE_DIR/*.cpp -- black-box tests over this deployment's own run_scripts" ON)
endif()

if(BUILD_TESTING_LAYERS AND THORIUM_ACCEPTANCE_TESTS)
    # --- Acceptance tests: the run_scripts binary, driven as a subprocess ---
    # Runs the built run_scripts the way a rig console or a CI job does -- real
    # flags, real log files, exit statuses -- and asserts on what comes back
    # out. That's a different question from anything core_tests/rig_tests can
    # ask: they exercise the sinks and the verbs with hand-built inputs, which
    # proves each piece works but not that the *command* wires them together.
    #
    # Defined here rather than through add_layer_tests() because it is unlike
    # every other test target in this tree: it links no Thorium library at all
    # (it is strictly black-box -- the binary's stdout and its log files are the
    # entire interface under test), and it needs two paths baked in that only
    # this scope knows.
    file(GLOB THORIUM_ACCEPTANCE_SOURCES CONFIGURE_DEPENDS "${THORIUM_ACCEPTANCE_DIR}/*.cpp")

    # An empty glob would otherwise reach add_executable as "no sources", whose
    # diagnostic names the target rather than the reason. Fatal rather than
    # quietly skipped, for the reason add_layer_tests gives for the same check:
    # a test target that silently declines to exist is the one kind of missing
    # thing a compile-time-checked codebase cannot catch for you. A deployment
    # with no acceptance tests turns the option off and says so.
    if(NOT THORIUM_ACCEPTANCE_SOURCES)
        message(FATAL_ERROR
            "THORIUM_ACCEPTANCE_TESTS is ON but there is no *.cpp in "
            "THORIUM_ACCEPTANCE_DIR='${THORIUM_ACCEPTANCE_DIR}' -- either this "
            "deployment brings black-box tests over its own run_scripts and they "
            "belong there, or it brings none and should set "
            "THORIUM_ACCEPTANCE_TESTS=OFF (see CMakePresets.json's "
            "dev-deployment).")
    endif()

    add_executable(acceptance_tests ${THORIUM_ACCEPTANCE_SOURCES})
    target_link_libraries(acceptance_tests PRIVATE GTest::gtest_main)

    # THORIUM_RUN_SCRIPTS_EXE: a $<TARGET_FILE:...> generator expression rather
    # than a constructed path, so this keeps working under any generator or
    # build layout.
    #
    # THORIUM_ACCEPTANCE_OUTPUT_DIR: where each scenario's artifacts are kept --
    # its exact invocation, the console transcript, and both log files it
    # produced. Inside the build tree on purpose, and deliberately not cleaned
    # up afterwards: a failing assertion names the file to go and read, and a
    # passing run still leaves real specimens of both log formats to look at.
    # See the test file's own header comment for the per-scenario layout.
    target_compile_definitions(acceptance_tests PRIVATE
        THORIUM_RUN_SCRIPTS_EXE="$<TARGET_FILE:run_scripts>"
        THORIUM_RUN_SCRIPTS_HOOKED_EXE="$<TARGET_FILE:run_scripts_hooked>"
        THORIUM_ACCEPTANCE_OUTPUT_DIR="${CMAKE_CURRENT_BINARY_DIR}/acceptance"
    )

    # Nothing links either runner, so without this the acceptance binary could
    # be built (and run) before the things it tests exist.
    add_dependencies(acceptance_tests run_scripts run_scripts_hooked)

    # Labelled so a developer can run or skip this group as a whole --
    # `ctest -L acceptance` / `ctest -LE acceptance` -- since these are the only
    # tests here that shell out and touch the filesystem.
    gtest_discover_tests(acceptance_tests PROPERTIES LABELS acceptance)
endif()

if(BUILD_TESTING_LAYERS)
    # test_criteria_variants_compile.cpp #includes every variant .inc file
    # directly, into namespaces of its own, rather than through
    # core/criteria/active_criteria.hpp -- it is checking the variants against each
    # other, not building the merged table scripts use, so it needs
    # core::criterion machinery but neither the scripts library nor the
    # THORIUM_CRITERIA_VARIANT_TABLES compile definition above.
    # test_fuse_register_script.cpp / test_supply_rail_script.cpp also rely on
    # this to reach the suite's scripts.hpp by a plain quoted #include.
    target_include_directories(scripts_tests PRIVATE ${CMAKE_SOURCE_DIR})
endif()
