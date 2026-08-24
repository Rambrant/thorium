// ReSharper disable once CppMissingIncludeGuard

//
// The catalog for run_scripts_hooked -- the acceptance fixture that actually
// declares hooks, which the shipped suite/test_catalog.inc does only at the run
// level. See app/tests/test_acceptance.cpp for what is asserted about it.
//
// Two groups, differing in exactly one thing: the first declares hooks of its
// own and the second declares none. That is what makes the group-level pair's
// defining property observable -- a --select naming a test in one group must
// leave the other group's hooks alone -- and it needs a group with hooks to skip
// and a group without to select instead.
//
// Every hook and script here announces itself on stdout, because that ordering
// is the whole subject: a hook posts no journal event of its own, so the logs
// cannot show what ran when.
//

RUN_SETUP(    fixtureSetup)
RUN_TEARDOWN( fixtureTeardown)

GROUP( OutputVoltage, "The hook fixture's group with hooks of its own")
    SETUP(    fixtureGroupSetup)
    TEST( FixtureTest, fixtureScript, "Announces itself so ordering can be asserted")
    TEARDOWN( fixtureGroupTeardown)
END_GROUP

GROUP( Console, "The hook fixture's group with no hooks of its own")
    TEST( OtherFixtureTest, otherFixtureScript, "Announces itself from a group nothing brackets")
END_GROUP
