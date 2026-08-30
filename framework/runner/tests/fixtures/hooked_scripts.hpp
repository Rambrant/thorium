#pragma once

//
// Declarations for the hook-fixture suite -- what core/catalog/active_test_catalog.hpp
// includes via THORIUM_TEST_SCRIPTS, and what hooked_catalog.inc's
// RUN_SETUP/RUN_TEARDOWN/TEST entries are name-checked against.
//
// Declarations only, and that is what lets two very different consumers share
// one fixture. run_scripts_hooked links the definitions beside this file
// (hooked_scripts.cpp), whose bodies announce themselves on stdout so the
// acceptance tests can assert an ordering; framework/core/tests/catalog/test_run_hooks.cpp
// defines its own bodies in its own translation unit, whose hooks set a flag it
// can check. What both need to agree on is the table and these names -- see
// framework/runner/CMakeLists.txt and framework/core/CMakeLists.txt for the two ways in.
//
// At global scope for the same reason suite/scripts.hpp is: so a catalog can
// name one directly, with no "using namespace" of any suite-specific name.
//

//
// The hooks keep a verdict of their own; the scripts do not. A script's
// outcome is derived from the checks it records (see core::Journal::endTest and
// core/catalog/test_catalog.hpp), which is why only the hooks return bool.
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
