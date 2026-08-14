// ReSharper disable once CppMissingIncludeGuard

//
// A catalog that declares both run hooks, for test_run_hooks.cpp. The shipped
// suite/test_catalog.inc declares TEARDOWN alone, so its absent SETUP covers
// the nullptr half of the resolution described in core/active_test_catalog.hpp
// and this fixture covers the declared half of both.
//

SETUP(    fixtureSetup)
TEARDOWN( fixtureTeardown)

//
// Named OutputVoltage rather than something fixture-flavoured because
// core/active_test_catalog.hpp's Catalog array names its groups one by one
// (see its own comment: a wholly new GROUP means a line added there). A
// fixture catalog therefore has to offer the same group name the shipped one
// does -- which costs this test nothing, since its subject is the hooks.
//
GROUP( OutputVoltage, "A catalog group that exists only for the hook tests")
    TEST( FixtureTest, fixtureScript, "Does nothing but be listed")
END_GROUP
