#include "runner/testing/acceptance.hpp"

using namespace thorium_acceptance;

//
// The runner's own ordering and failure rules, black-box, against
// run_scripts_hooked -- the same main.cpp as every deployment's run_scripts, over
// a catalog whose hooks announce themselves on stdout and can be made to fail on
// demand (framework/runner/tests/fixtures/).
//
// These are framework tests and live here rather than in a deployment's
// acceptance/ because nothing in them names a deployment: not an instrument, not
// a DUT point, not a suite. They were once the AcceptanceHooks section of the
// bench's acceptance tests, where the runner's behaviour was pinned by the
// deployment that happened to be built, and so would have gone with it when that
// deployment left this repository. The shipped hooks' own scenarios -- the power
// cycle a deployment's RUN_SETUP/RUN_TEARDOWN actually perform -- stay with the
// deployment whose rig they power.
//
// The harness is runner/testing/acceptance.hpp, shared with the deployment-side
// tests; read its header for the artifacts each scenario keeps.
//
namespace
{
    struct RunnerHooks : Acceptance {};
} // namespace

// ---------------------------------------------------------------------------
// Bracketing a run: RUN_SETUP / RUN_TEARDOWN
// ---------------------------------------------------------------------------
//
// Driven against run_scripts_hooked -- the same main.cpp over a catalog whose
// hooks announce themselves on stdout and can be made to fail on demand (see
// framework/runner/CMakeLists.txt). The shipped suite now declares both run hooks too
// (rigPowerOn/rigPowerOff), but neither can stand in here: they print no
// ordering markers, and rigPowerOff has no failure to report at all. So the
// claims below -- ordering around the scripts, bracketing every --repeat pass
// once, a failing hook failing the run -- still need the fixture.
//
// The fixture's first group declares SETUP/TEARDOWN and its second
// declares none, so every ordering below reads
// setup, group-setup, script, group-teardown, other-script, teardown: the group
// pair inside the run pair, around the tests of their own group only. The
// group-level section further down is where that nesting is the subject rather
// than the backdrop.
//
// What the shipped hooks do cover, on the real binary, is the scenario at the
// end of this section: that the power-up and power-down actually happen, in
// order, around the scripts.
//

TEST_F( RunnerHooks, SetupRunsBeforeTheScriptsAndTeardownAfterThem)
{
    EXPECT_EQ( runHooked( { "--no-color" }), 0);

    EXPECT_EQ( hookOrder(), ( std::vector<std::string>{
        "setup", "group-setup", "script", "group-teardown", "other-script", "teardown" }));
}

//
// The property that makes a pass a meaningful unit: the hooks bracket the
// selection *once*, not once per repetition. A rig is powered up, the scripts
// run three times, and it is powered down -- not powered up and down three
// times over.
//
TEST_F( RunnerHooks, TheHooksBracketEveryRepeatPassRatherThanEachOne)
{
    EXPECT_EQ( runHooked( { "--repeat=3", "--no-color" }), 0);

    EXPECT_EQ( hookOrder(), ( std::vector<std::string>{
        "setup",
        "group-setup", "script", "group-teardown", "other-script",
        "group-setup", "script", "group-teardown", "other-script",
        "group-setup", "script", "group-teardown", "other-script",
        "teardown" }));
}

//
// A setup that fails means no test runs at all -- but teardown still does,
// since a setup that got half way is exactly the case with something to undo.
//
// And no group is reached, so no group hook runs either: a RUN_SETUP that
// failed is a rig that is not ready for any group, not for some of them.
//
TEST_F( RunnerHooks, AFailingSetupStopsTheRunButStillTearsDown)
{
    EXPECT_EQ( runHooked( { "--no-color" }, { "THORIUM_FIXTURE_SETUP_FAILS=1" }), 1);

    EXPECT_EQ( hookOrder(), ( std::vector<std::string>{ "setup", "teardown" }));

    EXPECT_TRUE( containsText( errPath(), mErr, "RUN_SETUP reported failure"));
}

//
// Teardown is a guard destructor, so it runs on the way out of a script that
// threw just as it does on the ordinary path -- the case it most needs to
// cover, since that is when the rig is in an unknown state.
//
TEST_F( RunnerHooks, TeardownStillRunsWhenAScriptThrows)
{
    EXPECT_EQ( runHooked( { "--no-color" }, { "THORIUM_FIXTURE_SCRIPT_THROWS=1" }), 1);

    //
    // The group's own teardown is on the way out too, and ahead of the run's --
    // it is the guard nested inside, so it unwinds first. The second group never
    // starts: the exception left the pass entirely.
    //
    EXPECT_EQ( hookOrder(), ( std::vector<std::string>{
        "setup", "group-setup", "script", "group-teardown", "teardown" }));

    EXPECT_TRUE( containsText( errPath(), mErr, "Uncaught exception during test run"));
}

//
// A rig that did not shut down the way the suite says it should is not a clean
// run, however well the scripts themselves went.
//
TEST_F( RunnerHooks, AFailingTeardownFailsAnOtherwisePassingRun)
{
    EXPECT_EQ( runHooked( { "--no-color" }, { "THORIUM_FIXTURE_TEARDOWN_FAILS=1" }), 1);

    EXPECT_EQ( hookOrder(), ( std::vector<std::string>{
        "setup", "group-setup", "script", "group-teardown", "other-script", "teardown" }));

    EXPECT_TRUE( containsText( errPath(), mErr, "RUN_TEARDOWN reported failure"));
}

//
// --until-failure stops the passes; it must not skip the teardown on the way
// out.
//
TEST_F( RunnerHooks, StoppingEarlyOnFailureStillTearsDown)
{
    EXPECT_EQ( runHooked( { "--repeat=5", "--until-failure", "--no-color" },
                          { "THORIUM_FIXTURE_SCRIPT_FAILS=1" }), 1);

    //
    // The failing pass is finished, not abandoned -- a recorded failure is not
    // an exception -- so the rest of that pass, group teardown and second group
    // included, still runs before the passes stop.
    //
    EXPECT_EQ( hookOrder(), ( std::vector<std::string>{
        "setup", "group-setup", "script", "group-teardown", "other-script", "teardown" }));
}

//
// A selection matching nothing is reported without powering anything up: there
// is no run to bracket.
//
TEST_F( RunnerHooks, ASelectionMatchingNothingNeverReachesTheHooks)
{
    EXPECT_EQ( runHooked( { "--select=NoSuchTest" }), 1);

    EXPECT_TRUE( hookOrder().empty());
    EXPECT_TRUE( containsText( errPath(), mErr, "No catalog test named"));
}

// ---------------------------------------------------------------------------
// Bracketing one group: SETUP / TEARDOWN
// ---------------------------------------------------------------------------
//
// The group-level pair, whose one substantive difference from the run-level one
// is what these four tests are about: it is tied to the selection. A group whose
// tests were all filtered out is not set up, and a group that was selected is
// set up whether or not the rest of the catalog was.
//

//
// The property the construct exists for. Selecting the second group's test runs
// it with no group bracketing at all -- the first group's hooks are not "run and
// harmless", they do not run. On a rig that is the difference between a state
// established for tests that need it and a state imposed on tests written
// without it.
//
TEST_F( RunnerHooks, AGroupWhoseTestsAreAllDeselectedIsNeverBracketed)
{
    EXPECT_EQ( runHooked( { "--select=OtherFixtureTest", "--no-color" }), 0);

    EXPECT_EQ( hookOrder(), ( std::vector<std::string>{ "setup", "other-script", "teardown" }));
}

//
// The other direction: selecting the hooked group's one test brackets that
// group and reaches nothing else. Note that the run-level pair still runs --
// it brackets the selection, whatever the selection turned out to be.
//
TEST_F( RunnerHooks, SelectingATestBracketsItsOwnGroupAndNoOther)
{
    EXPECT_EQ( runHooked( { "--select=FixtureTest", "--no-color" }), 0);

    EXPECT_EQ( hookOrder(), ( std::vector<std::string>{
        "setup", "group-setup", "script", "group-teardown", "teardown" }));
}

//
// The same two levels, on the page. A group's own SETUP is indented with that
// group's tests; the catalog's RUN_SETUP is unindented with the groups.
//
TEST_F( RunnerHooks, TheHumanLogHeadsEachHookAtTheLevelItBrackets)
{
    EXPECT_EQ( runHooked( { "--select=FixtureTest", "--no-color" }), 0);

    // The run's pair, at the groups' level.
    EXPECT_TRUE( containsText( outPath(), mOut, "\nsetup Announce the run-level setup"));
    EXPECT_TRUE( containsText( outPath(), mOut, "\nteardown Announce the run-level teardown"));

    // The group's own, nested with its tests -- same id, different description,
    // which is the pair of facts that tells the two levels apart.
    EXPECT_TRUE( containsText( outPath(), mOut, "\n\tsetup Announce this group's own setup"));
    EXPECT_TRUE( containsText( outPath(), mOut, "\n\tteardown Announce this group's own teardown"));
}

//
// Both levels spell their id "setup", and what tells them apart in the machine
// log is what encloses them -- the group for a group's own pair, nothing for
// the catalog's RUN_ pair. This fixture is the only catalog in the repository
// that declares both, so it is the only place the distinction is observable.
//
TEST_F( RunnerHooks, TheMachineLogNamesBothLevelsOfBracketing)
{
    EXPECT_EQ( runHooked( { "--select=FixtureTest", "--no-color" }), 0);

    const auto sarif = findArtifact( ".sarif");

    ASSERT_FALSE( sarif.empty());

    const auto log = readFile( sarif);

    EXPECT_TRUE( looksComplete( log)) << "truncated machine log: " << sarif.string();

    // The run's pair, bracketing the selection.
    EXPECT_TRUE( containsText( sarif, log, R"("fullyQualifiedName": "setup")"));
    EXPECT_TRUE( containsText( sarif, log, R"("fullyQualifiedName": "teardown")"));

    // The group's own, bracketing its tests.
    EXPECT_TRUE( containsText( sarif, log, R"("fullyQualifiedName": "OutputVoltage/setup")"));
    EXPECT_TRUE( containsText( sarif, log, R"("fullyQualifiedName": "OutputVoltage/teardown")"));

    // The group nothing brackets was deselected, and claims no hooks it has not
    // got -- a log inventing a SETUP for a group that declared none would be
    // reporting a step that never happened.
    EXPECT_TRUE( omitsText( sarif, log, R"("fullyQualifiedName": "Console/setup")"));
}

//
// A failing SETUP is contained: its own group's tests do not run and the
// run fails, but the groups after it still run. That is the deliberate
// difference from a failing run-level RUN_SETUP (which stops everything) -- a rig
// state one group could not establish says nothing about another group's, and
// throwing away the rest of the selection would be reporting less than the run
// actually knows.
//
// Its own teardown still runs, for the same reason the run-level pair's does:
// the setup may have got half way.
//
TEST_F( RunnerHooks, AFailingGroupSetupSkipsItsOwnGroupOnly)
{
    EXPECT_EQ( runHooked( { "--no-color" }, { "THORIUM_FIXTURE_GROUP_SETUP_FAILS=1" }), 1);

    EXPECT_EQ( hookOrder(), ( std::vector<std::string>{
        "setup", "group-setup", "group-teardown", "other-script", "teardown" }));

    EXPECT_TRUE( containsText( errPath(), mErr,
        "SETUP for group 'OutputVoltage' reported failure"));
}

//
// And a failing TEARDOWN fails the run, exactly as the run-level one
// does: a group that did not undo what it set up has left the rig in a state
// the rest of the run was not written against, however well its tests went.
//
// The message names the group. There is one of these per group, so an
// unqualified "TEARDOWN reported failure" would not say which -- and it is the
// run-level pair, RUN_TEARDOWN, whose message needs no group in it.
//
TEST_F( RunnerHooks, AFailingGroupTeardownFailsAnOtherwisePassingRun)
{
    EXPECT_EQ( runHooked( { "--no-color" }, { "THORIUM_FIXTURE_GROUP_TEARDOWN_FAILS=1" }), 1);

    EXPECT_EQ( hookOrder(), ( std::vector<std::string>{
        "setup", "group-setup", "script", "group-teardown", "other-script", "teardown" }));

    EXPECT_TRUE( containsText( errPath(), mErr,
        "TEARDOWN for group 'OutputVoltage' reported failure"));
}
