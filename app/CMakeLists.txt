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

target_link_libraries(scripts PUBLIC hal core)

target_include_directories(scripts PUBLIC ${CMAKE_SOURCE_DIR})

# --- Criteria/tolerance variant selection ---
# Points core/active_criteria.hpp at the active variant's single flat .inc
# file (every script's CRITERIA/CRIT side by side), per THORIUM_CRITERIA_VARIANT
# (validated in the top-level CMakeLists.txt). THORIUM_PRODUCTION_CRITERIA is
# a second, fixed path -- always criteria_production.inc -- so other variants
# can reference "the same value as production" via CRIT_FROM_PRODUCTION
# without retyping it. Absolute paths, so no extra include-directory
# wrangling. These live under dut/, not here, because the tolerances
# they hold are a property of the DUT being tested (the same reasoning that
# put adapter.inc there) -- see dut/README.md.
target_compile_definitions(scripts PRIVATE
    THORIUM_PRODUCTION_CRITERIA="${CMAKE_SOURCE_DIR}/dut/criteria_production.inc"
    THORIUM_ACTIVE_CRITERIA="${CMAKE_SOURCE_DIR}/dut/criteria_${THORIUM_CRITERIA_VARIANT}.inc"
)

# --- Test catalog ---
# THORIUM_TEST_CATALOG points core/active_test_catalog.hpp at
# suite/test_catalog.inc; THORIUM_TEST_SCRIPTS points it at
# suite/scripts.hpp (the declarations every TEST(...) is checked
# against). Both absolute paths, both PUBLIC (unlike the criteria defines
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
    target_compile_definitions(acceptance_tests PRIVATE
        THORIUM_RUN_SCRIPTS_EXE="$<TARGET_FILE:run_scripts>"
        THORIUM_ACCEPTANCE_OUTPUT_DIR="${CMAKE_CURRENT_BINARY_DIR}/acceptance"
    )

    # Nothing links run_scripts, so without this the acceptance binary could be
    # built (and run) before the thing it tests exists.
    add_dependencies(acceptance_tests run_scripts)

    # Labelled so a developer can run or skip this group as a whole --
    # `ctest -L acceptance` / `ctest -LE acceptance` -- since these are the only
    # tests here that shell out and touch the filesystem.
    gtest_discover_tests(acceptance_tests PROPERTIES LABELS acceptance)

    # test_criteria_variants_compile.cpp #includes every variant .inc file
    # directly (not through core/active_criteria.hpp, which only ever sees
    # the one active variant) -- it needs core::criterion machinery but not
    # the scripts library itself, and it doesn't go through the
    # THORIUM_ACTIVE_CRITERIA compile definition above. test_fuse_register_
    # script.cpp / test_supply_rail_script.cpp also rely on this to reach
    # suite/scripts.hpp by a plain quoted #include.
    target_include_directories(scripts_tests PRIVATE ${CMAKE_SOURCE_DIR})
endif()
