#pragma once

//
// Declarations for the hook-fixture suite -- what core/active_test_catalog.hpp
// includes via THORIUM_TEST_SCRIPTS when building run_scripts_hooked (see
// app/CMakeLists.txt), and what hooked_catalog.inc's SETUP/TEARDOWN/TEST
// entries are name-checked against.
//
// At global scope for the same reason suite/scripts.hpp is: so a catalog can
// name one directly, with no "using namespace" of any suite-specific name.
//

[[nodiscard]] auto fixtureSetup()    -> bool;
[[nodiscard]] auto fixtureTeardown() -> bool;
[[nodiscard]] auto fixtureScript()   -> bool;
