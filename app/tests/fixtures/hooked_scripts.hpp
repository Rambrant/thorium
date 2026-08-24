#pragma once

//
// Declarations for the hook-fixture suite -- what core/active_test_catalog.hpp
// includes via THORIUM_TEST_SCRIPTS when building run_scripts_hooked (see
// app/CMakeLists.txt), and what hooked_catalog.inc's RUN_SETUP/RUN_TEARDOWN/TEST
// entries are name-checked against.
//
// At global scope for the same reason suite/scripts.hpp is: so a catalog can
// name one directly, with no "using namespace" of any suite-specific name.
//

//
// The hooks keep a verdict of their own; the scripts do not. A script's
// outcome is derived from the checks it records (see core::Journal::endTest and
// core/test_catalog.hpp), which is why only the hooks return bool.
//
// Nothing here says which pair brackets the run and which brackets one group --
// that is the catalog's statement (see hooked_catalog.inc), and a hook's type
// is the same either way.
//
[[nodiscard]] auto fixtureSetup()         -> bool;
[[nodiscard]] auto fixtureTeardown()      -> bool;
[[nodiscard]] auto fixtureGroupSetup()    -> bool;
[[nodiscard]] auto fixtureGroupTeardown() -> bool;

auto fixtureScript()      -> void;
auto otherFixtureScript() -> void;
