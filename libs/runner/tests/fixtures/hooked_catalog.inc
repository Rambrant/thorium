// ReSharper disable once CppMissingIncludeGuard

//
// The catalog for run_scripts_hooked -- the acceptance fixture that actually
// declares hooks, which the shipped suite/test_catalog.inc does only at the run
// level. See suite/acceptance/test_acceptance.cpp for what is asserted about it.
//
// Also the fixture libs/core/tests/test_run_hooks.cpp resolves, which is a
// second reason the descriptions below are worth writing carefully: that test
// asserts them literally, so this table is the single statement of what a hook's
// prose is, for both the unit test of the resolution and the acceptance test of
// the run. It used to be stated twice, once here and once under libs/core.
//
// Two groups, differing in exactly one thing: the first declares hooks of its
// own and the second declares none. That is what makes the group-level pair's
// defining property observable -- a --select naming a test in one group must
// leave the other group's hooks alone -- and it needs a group with hooks to skip
// and a group without to select instead.
//
// Every hook and script here announces itself on stdout, because that ordering
// is the whole subject and stdout is the one stream that carries hooks and
// scripts in a single order. The machine log brackets each hook too (see
// core::Journal::beginPhase), but a hook whose body posts nothing -- which is
// every hook here -- shows up there as a boundary and nothing else.
//

RUN_SETUP(    fixtureSetup,    "Announce the run-level setup")
RUN_TEARDOWN( fixtureTeardown, "Announce the run-level teardown")

GROUP( OutputVoltage, "The hook fixture's group with hooks of its own")
    SETUP(    fixtureGroupSetup,    "Announce this group's own setup")
    TEST( FixtureTest, fixtureScript, "Announces itself so ordering can be asserted")
    TEARDOWN( fixtureGroupTeardown, "Announce this group's own teardown")
END_GROUP

GROUP( Console, "The hook fixture's group with no hooks of its own")
    TEST( OtherFixtureTest, otherFixtureScript, "Announces itself from a group nothing brackets")
END_GROUP
