#pragma once

//
// The declarations side of the fixture catalog next to this file -- what
// core/active_test_catalog.hpp includes via THORIUM_TEST_SCRIPTS, and what
// its SETUP/TEARDOWN/TEST entries are name-checked against.
//
// At global scope, for the same reason suite/scripts.hpp is: that is what
// lets a catalog name one of these directly, with no "using namespace" of
// any suite-specific name.
//

//
// The hooks keep a verdict of their own; the script does not. A script's
// outcome is derived from the checks it records (see core::Journal::endTest and
// core/test_catalog.hpp), which is why only two of these three return bool.
//
[[nodiscard]] auto fixtureSetup()    -> bool;
[[nodiscard]] auto fixtureTeardown() -> bool;

auto fixtureScript() -> void;
