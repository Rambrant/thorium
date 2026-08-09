// ReSharper disable once CppMissingIncludeGuard

//
// The catalog for run_scripts_hooked -- the acceptance fixture that actually
// declares run hooks, which the shipped suite/test_catalog.inc deliberately
// does not. See app/tests/test_acceptance.cpp for what is asserted about it.
//
// The group is named OutputVoltage because core/active_test_catalog.hpp's
// Catalog array names its groups one at a time (see its own comment); a
// fixture catalog therefore has to offer the same group name the shipped one
// does, which costs nothing here.
//

SETUP(    fixtureSetup)
TEARDOWN( fixtureTeardown)

GROUP( OutputVoltage, "The hook fixture's one group")
    TEST( FixtureTest, fixtureScript, "Announces itself so ordering can be asserted")
END_GROUP
