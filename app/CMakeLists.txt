# The test-suite runner: main.cpp, using core/active_test_catalog.hpp (the
# framework-side scaffolding that turns suite/ content into something
# runnable). Nothing DUT-specific lives in this folder -- see suite/ for
# that -- which is also why it isn't called "scripts" anymore: it doesn't
# hold any.
#
# The actual test scripts (suite/scripts/*.cpp) are NOT built here as a
# fixed list -- they're discovered from suite/, kept clean of any
# CMakeLists.txt of its own, the same way suite/criteria_*.inc and
# suite/test_catalog.inc are just content, not build machinery.
# CONFIGURE_DEPENDS makes CMake re-glob and reconfigure whenever that
# directory's contents change, so a new suite/scripts/*.cpp just needs to
# exist to be picked up on the next build -- no CMakeLists.txt edit.
file(GLOB THORIUM_SUITE_SCRIPT_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/suite/scripts/*.cpp")

# Target is still named "scripts" -- it's a library of compiled test
# scripts, which is still an accurate name, even though the folder
# defining it (this one) no longer is.
add_library(scripts STATIC
    ${THORIUM_SUITE_SCRIPT_SOURCES}
)

# hal_rig, not hal: a script names this rig's instrument globals (Dmm1, DcP1,
# ...) and calls Measure/Apply/safeRig on them, all of which live in hal_rig --
# see libs/hal/CMakeLists.txt on why that is a separate target from the generic
# hal the drivers themselves compile against. hal comes along transitively.
target_link_libraries(scripts PUBLIC hal_rig core)

target_include_directories(scripts PUBLIC ${CMAKE_SOURCE_DIR})

# --- Criteria/tolerance variants ---
# Points core/active_criteria.hpp at the two files it reads.
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
# core/active_criteria.hpp's pass 2 on why it is the right one for that).
#
# Absolute paths, so no extra include-directory wrangling. The criteria live
# under dut/, not here, because the tolerances they hold are a property of the
# DUT being tested (the same reasoning that put adapter.inc there) -- see
# dut/README.md.
target_compile_definitions(scripts PRIVATE
    THORIUM_PRODUCTION_CRITERIA="${CMAKE_SOURCE_DIR}/dut/criteria_production.inc"
    THORIUM_CRITERIA_VARIANT_TABLES="${THORIUM_CRITERIA_VARIANT_TABLES}"
)

# --- Test catalog ---
# THORIUM_TEST_CATALOG points core/active_test_catalog.hpp at
# suite/test_catalog.inc; THORIUM_TEST_SCRIPTS points it at
# suite/scripts.hpp (the declarations every TEST(...) is checked
# against -- declarations only, which is why pointing main.cpp at it costs
# nothing; what a script body needs is in suite/prelude.hpp instead, and
# nothing here points at that). Both absolute paths, both PUBLIC (unlike the criteria defines
# above): main.cpp includes active_test_catalog.hpp too, and it's compiled
# as part of run_scripts, not this scripts library, so it needs these
# definitions to reach it via target_link_libraries below.
target_compile_definitions(scripts PUBLIC
    THORIUM_TEST_CATALOG="${CMAKE_SOURCE_DIR}/suite/test_catalog.inc"
    THORIUM_TEST_SCRIPTS="${CMAKE_SOURCE_DIR}/suite/scripts.hpp"
)

# Runner: executes all scripts against a real (or simulated) device
# and exits non-zero if any script fails.
add_executable(run_scripts src/main.cpp)
target_link_libraries(run_scripts PRIVATE scripts)

install(TARGETS run_scripts RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

# --- manifest.json, for a web server discovering installed suite binaries ---
# The web server that supervises test runs picks which suite to run, which
# tests within it, and which criteria variant, by scanning an install prefix
# for run_scripts binaries and reading a manifest next to each one -- rather
# than parsing this build's C++ or knowing anything about THORIUM_CRITERIA_
# VARIANT/suite/test_catalog.inc itself. Generated at install time, not
# build time or configure time: it has to reflect the actual installed
# binary (--list-tests is the catalog's own source of truth -- see
# app/src/main.cpp's listTests()), and install is the first point a fully
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
    # These test suite/scripts/*.cpp and suite/criteria_*.inc -- suite
    # content, not this runner -- so their sources live in suite/tests/,
    # not here. This is still where the target itself is defined, same as
    # suite/scripts/*.cpp is discovered from here rather than built via a
    # CMakeLists.txt of its own.
    add_layer_tests(scripts scripts
        ${CMAKE_SOURCE_DIR}/suite/tests/test_fuse_register_script.cpp
        ${CMAKE_SOURCE_DIR}/suite/tests/test_supply_rail_script.cpp
        ${CMAKE_SOURCE_DIR}/suite/tests/test_criteria_variants_compile.cpp
    )

    # --- Acceptance tests: the run_scripts binary, driven as a subprocess ---
    # Runs the built run_scripts the way a rig console or a CI job does -- real
    # flags, real log files, exit statuses -- and asserts on what comes back
    # out. That's a different question from anything core_tests/hal_tests can
    # ask: they exercise the sinks and the verbs with hand-built inputs, which
    # proves each piece works but not that the *command* wires them together.
    #
    # Defined here rather than through add_layer_tests() because it is unlike
    # every other test target in this tree: it links no Thorium library at all
    # (it is strictly black-box -- the binary's stdout and its log files are the
    # entire interface under test), and it needs two paths baked in that only
    # this scope knows.
    add_executable(acceptance_tests tests/test_acceptance.cpp)
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
    # --- A second runner, over a catalog that declares SETUP/TEARDOWN ---
    # suite/test_catalog.inc declares neither hook, on purpose, so no ordinary
    # run exercises the runner's hook path at all. This target is the same
    # main.cpp over a fixture catalog that does declare them, which is what
    # lets the acceptance tests assert the things only a real run can show:
    # that SETUP precedes every script and TEARDOWN follows the last one, that
    # both happen once around all of --repeat's passes rather than once per
    # pass, and that TEARDOWN still runs when a script throws.
    #
    # Same main.cpp deliberately, not a stand-in: a test binary that reimplemented
    # the run loop would be testing the reimplementation. The catalog is the only
    # thing that differs, which is exactly the variable under test.
    add_library(hooked_scripts STATIC tests/fixtures/hooked_scripts.cpp)
    target_link_libraries(hooked_scripts PUBLIC hal_rig core)
    target_include_directories(hooked_scripts PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures)
    target_compile_definitions(hooked_scripts PUBLIC
        THORIUM_TEST_CATALOG="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/hooked_catalog.inc"
        THORIUM_TEST_SCRIPTS="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/hooked_scripts.hpp"
    )

    add_executable(run_scripts_hooked src/main.cpp)
    target_link_libraries(run_scripts_hooked PRIVATE hooked_scripts)

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

    # test_criteria_variants_compile.cpp #includes every variant .inc file
    # directly, into namespaces of its own, rather than through
    # core/active_criteria.hpp -- it is checking the variants against each
    # other, not building the merged table scripts use, so it needs
    # core::criterion machinery but neither the scripts library nor the
    # THORIUM_CRITERIA_VARIANT_TABLES compile definition above.
    # test_fuse_register_script.cpp / test_supply_rail_script.cpp also rely on
    # this to reach suite/scripts.hpp by a plain quoted #include.
    target_include_directories(scripts_tests PRIVATE ${CMAKE_SOURCE_DIR})
endif()
