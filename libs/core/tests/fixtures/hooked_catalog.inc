// ReSharper disable once CppMissingIncludeGuard

//
// A catalog that declares every hook there is, for test_run_hooks.cpp: the
// run-level RUN_SETUP/RUN_TEARDOWN and, on the first group, SETUP/
// TEARDOWN. The shipped suite/test_catalog.inc declares the run-level
// pair and no group hooks at all, so its groups cover the nullptr half of the
// resolution described in core/active_test_catalog.hpp -- and so does the
// second group here, which is why the two groups differ.
//

RUN_SETUP(    fixtureSetup,    "The run-level setup this fixture resolves to")
RUN_TEARDOWN( fixtureTeardown, "The run-level teardown this fixture resolves to")

GROUP( OutputVoltage, "A catalog group that exists only for the hook tests")
    SETUP(    fixtureGroupSetup,    "This group's own setup")
    TEST( FixtureTest, fixtureScript, "Does nothing but be listed")
    TEARDOWN( fixtureGroupTeardown, "This group's own teardown")
END_GROUP

//
// Deliberately hookless, and deliberately not named after anything in the
// shipped suite. Both used to be impossible: core/active_test_catalog.hpp
// listed its groups by hand, so a fixture catalog had to declare the same group
// names the shipped one did. Reflection removed that (see its own comment), and
// the two claims this fixture now makes -- a group's hooks resolve to what it
// named, a group that named none resolves to nullptr -- need two groups that
// differ in exactly that.
//
GROUP( Console, "A second group, with no hooks of its own")
    TEST( OtherFixtureTest, otherFixtureScript, "Also does nothing but be listed")
END_GROUP
