#pragma once

//
// The declarations side of the fixture catalog next to this file -- what
// core/active_test_catalog.hpp includes via THORIUM_TEST_SCRIPTS, and what
// its RUN_SETUP/RUN_TEARDOWN/TEST entries are name-checked against.
//
// At global scope, for the same reason suite/scripts.hpp is: that is what
// lets a catalog name one of these directly, with no "using namespace" of
// any suite-specific name.
//

//
// The hooks keep a verdict of their own; the scripts do not. A script's
// outcome is derived from the checks it records (see core::Journal::endTest and
// core/test_catalog.hpp), which is why only the hooks return bool.
//
// Two levels of hook, declared the same way and telling nothing apart here:
// which of them brackets a run and which brackets one group is the catalog's
// statement to make, not this file's (see fixtures/hooked_catalog.inc).
//
[[nodiscard]] auto fixtureSetup()         -> bool;
[[nodiscard]] auto fixtureTeardown()      -> bool;
[[nodiscard]] auto fixtureGroupSetup()    -> bool;
[[nodiscard]] auto fixtureGroupTeardown() -> bool;

auto fixtureScript()      -> void;
auto otherFixtureScript() -> void;
