#include "core/journal/journal.hpp"

#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <vector>

namespace
{
    //
    // A sink that keeps everything it is told, so the journal's own behaviour
    // (ordering, stamping, test attribution, fan-out) can be asserted without
    // going through a file format.
    //
    // Not core::EveryHookSink, which every other test file in this framework
    // uses and which takes onEvent alone. The difference is the subject: those
    // files assert on what a *verb* recorded and reach for the events, while
    // this one is about the journal's dispatch itself -- that begin() stamps a
    // RunInfo, that a group opens before its tests and closes after them, that
    // a phase nests where the catalog says. Those are the hooks, not the
    // events, so this sink overrides all nine and keeps each separately.
    //
    class EveryHookSink : public core::IJournalSink
    {
        public:
            struct Boundary
            {
                std::string  Group;
                std::string  Test;
                bool         Passed{ false };
            };

            auto onRunStart( const core::RunInfo & info) -> void override
            {
                Started = info;
                ++RunStarts;
            }

            auto onGroupStart( const std::string_view group, const std::string_view description) -> void override
            {
                GroupStarts.push_back( Boundary{ std::string( group), {}, false });
                Descriptions.emplace_back( description);
            }

            auto onGroupEnd( const std::string_view group) -> void override
            {
                GroupEnds.emplace_back( group);
            }

            auto onTestStart( const std::string_view test, const std::string_view description) -> void override
            {
                TestStarts.push_back( Boundary{ {}, std::string( test), false });
                Descriptions.emplace_back( description);
            }

            auto onPhaseStart( const std::string_view group, const std::string_view phase, const std::string_view title) -> void override
            {
                PhaseStarts.push_back( Boundary{ std::string( group), std::string( phase), false });
                Descriptions.emplace_back( title);
            }

            auto onPhaseEnd( const std::string_view phase) -> void override
            {
                PhaseEnds.emplace_back( phase);
            }

            auto onEvent( const core::JournalEvent & event) -> void override
            {
                Events.push_back( event);
            }

            auto onTestEnd( const std::string_view group, const std::string_view test, const bool passed) -> void override
            {
                TestEnds.push_back( Boundary{ std::string( group), std::string( test), passed });
            }

            auto onRunEnd( const bool allPassed) -> void override
            {
                RunEnds.push_back( allPassed);
            }

            core::RunInfo                  Started;
            int                            RunStarts{ 0 };
            std::vector<Boundary>          GroupStarts;
            std::vector<std::string>       GroupEnds;
            std::vector<Boundary>          TestStarts;
            std::vector<Boundary>          PhaseStarts;
            std::vector<std::string>       PhaseEnds;
            std::vector<std::string>       Descriptions;
            std::vector<core::JournalEvent> Events;
            std::vector<Boundary>          TestEnds;
            std::vector<bool>              RunEnds;
    };

    //
    // The journal is process-wide by design (see core/journal/journal.hpp on why), so
    // every test here has to leave it as it found it -- otherwise one test's
    // sink receives the next test's events. Sequence numbering is reset by
    // begin(), which each test calls.
    //
    class JournalTest : public ::testing::Test
    {
        protected:
            auto SetUp() -> void override
            {
                core::journal().clearSinks();
                core::journal().add( mSink);
            }

            auto TearDown() -> void override
            {
                core::journal().clearSinks();
            }

            EveryHookSink mSink;
    };
} // namespace

TEST_F( JournalTest, EventsAreSequencedFromZeroPerRun)
{
    core::journal().begin( core::RunInfo{});

    core::journal().post( core::JournalRecord{ .Method = core::Verb::Measure, .Subject = "A" });
    core::journal().post( core::JournalRecord{ .Method = core::Verb::Measure, .Subject = "B" });

    ASSERT_EQ( mSink.Events.size(), 2u);
    EXPECT_EQ( mSink.Events[ 0].Sequence, 0u);
    EXPECT_EQ( mSink.Events[ 1].Sequence, 1u);

    // A second run restarts the numbering, so two runs of the same script
    // produce the same sequence for the same event and stay diffable.
    core::journal().begin( core::RunInfo{});
    core::journal().post( core::JournalRecord{ .Method = core::Verb::Measure, .Subject = "A" });

    ASSERT_EQ( mSink.Events.size(), 3u);
    EXPECT_EQ( mSink.Events[ 2].Sequence, 0u);
}

TEST_F( JournalTest, EventsAreStampedWithTheRunningTest)
{
    core::journal().begin( core::RunInfo{});
    core::journal().beginGroup( "OutputVoltage", "Output rail tests");
    core::journal().beginTest( "SupplyRail", "Verify supply rails");

    core::journal().post( core::JournalRecord{ .Method = core::Verb::Measure, .Subject = "Output5V" });

    ASSERT_EQ( mSink.Events.size(), 1u);
    EXPECT_EQ( mSink.Events[ 0].Group, "OutputVoltage");
    EXPECT_EQ( mSink.Events[ 0].Test,  "SupplyRail");

    ASSERT_EQ( mSink.GroupStarts.size(), 1u);
    EXPECT_EQ( mSink.GroupStarts[ 0].Group, "OutputVoltage");

    ASSERT_EQ( mSink.TestStarts.size(), 1u);
    EXPECT_EQ( mSink.TestStarts[ 0].Test, "SupplyRail");

    // Group description, then test description -- in boundary order.
    ASSERT_EQ( mSink.Descriptions.size(), 2u);
    EXPECT_EQ( mSink.Descriptions[ 0], "Output rail tests");
    EXPECT_EQ( mSink.Descriptions[ 1], "Verify supply rails");
}

//
// The two boundaries close independently, and the difference matters: between
// endTest and endGroup a further test may still follow, so an event there
// belongs to the group but to no test. This is what keeps hal::RigSafingGuard's
// Safe event -- posted from a destructor after everything has finished, see
// framework/runner/src/main.cpp -- attributed to the run rather than to whichever test
// happened to be last.
//
TEST_F( JournalTest, EventAttributionFollowsWhicheverBoundariesAreStillOpen)
{
    core::journal().begin( core::RunInfo{});
    core::journal().beginGroup( "OutputVoltage", {});
    core::journal().beginTest( "SupplyRail", {});

    //
    // A check inside the bracket, because endTest derives the verdict from
    // exactly these and a bracket holding none of them posts one of its own
    // saying so (see Journal::endTest). Either way there is an event here; one
    // the test posted deliberately is the clearer thing to then assert about.
    //
    core::journal().post( core::JournalRecord{ .Method = core::Verb::Verify, .Subject = "5V", .Passed = true });

    EXPECT_TRUE( core::journal().endTest());

    // Inside the group, outside any test.
    core::journal().post( core::JournalRecord{ .Method = core::Verb::Note, .Subject = "between tests" });

    core::journal().endGroup();

    // Outside both -- where a safing pass lands.
    core::journal().post( core::JournalRecord{ .Method = core::Verb::Safe, .Subject = "rig" });

    ASSERT_EQ( mSink.Events.size(), 3u);

    // Inside both.
    EXPECT_EQ( mSink.Events[ 0].Group, "OutputVoltage");
    EXPECT_EQ( mSink.Events[ 0].Test,  "SupplyRail");

    EXPECT_EQ( mSink.Events[ 1].Group, "OutputVoltage");
    EXPECT_TRUE( mSink.Events[ 1].Test.empty());

    EXPECT_TRUE( mSink.Events[ 2].Group.empty());
    EXPECT_TRUE( mSink.Events[ 2].Test.empty());

    // Each boundary still names what it is closing.
    ASSERT_EQ( mSink.TestEnds.size(), 1u);
    EXPECT_EQ( mSink.TestEnds[ 0].Test, "SupplyRail");
    EXPECT_TRUE( mSink.TestEnds[ 0].Passed);

    ASSERT_EQ( mSink.GroupEnds.size(), 1u);
    EXPECT_EQ( mSink.GroupEnds[ 0], "OutputVoltage");
}

TEST_F( JournalTest, EveryRegisteredSinkSeesEveryEvent)
{
    EveryHookSink second;
    core::journal().add( second);

    core::journal().begin( core::RunInfo{});
    core::journal().post( core::JournalRecord{ .Method = core::Verb::Apply, .Subject = "DcP1" });
    core::journal().end( true);

    EXPECT_EQ( mSink.Events.size(),  1u);
    EXPECT_EQ( second.Events.size(), 1u);
    EXPECT_EQ( second.RunStarts,     1);
    ASSERT_EQ( second.RunEnds.size(), 1u);
    EXPECT_TRUE( second.RunEnds[ 0]);
}

//
// A verb must never have to check whether logging is configured -- a unit-test
// binary registers no sinks at all, and that has to be silently fine.
//
TEST_F( JournalTest, PostingWithNoSinksIsHarmless)
{
    core::journal().clearSinks();

    core::journal().begin( core::RunInfo{});
    core::journal().post( core::JournalRecord{ .Method = core::Verb::Measure, .Subject = "Output5V" });
    core::journal().end( true);

    EXPECT_TRUE( mSink.Events.empty());
}

TEST_F( JournalTest, RunInfoIsKeptForSinksThatNeedItAtCloseTime)
{
    core::RunInfo info;
    info.DutName  = "DeviceX";
    info.RigName  = "bench-7";

    core::journal().begin( info);

    // SarifSink reads this back when it writes its tool/invocation blocks.
    EXPECT_EQ( core::journal().runInfo().DutName, "DeviceX");
    EXPECT_EQ( mSink.Started.RigName,             "bench-7");
}

TEST( CoreJournalVerb, ToStringUsesTheEnumeratorSpelling)
{
    EXPECT_EQ( core::to_string( core::Verb::Measure),    "Measure");
    EXPECT_EQ( core::to_string( core::Verb::Apply),      "Apply");
    EXPECT_EQ( core::to_string( core::Verb::Disconnect), "Disconnect");
    EXPECT_EQ( core::to_string( core::Verb::Verify),     "Verify");
    EXPECT_EQ( core::to_string( core::Verb::Safe),       "Safe");
}

TEST( CoreJournalTime, IsoTimestampsAreMillisecondPreciseUtc)
{
    EXPECT_EQ( core::isoUtcFromUnixMillis( 0),             "1970-01-01T00:00:00.000Z");
    EXPECT_EQ( core::isoUtcFromUnixMillis( 1'000),         "1970-01-01T00:00:01.000Z");
    EXPECT_EQ( core::isoUtcFromUnixMillis( 1'769'000'123), "1970-01-21T11:23:20.123Z");
}

//
// A machine with its clock set before the epoch is exactly when a timestamp
// gets scrutinised, so the millisecond remainder must not come out negative.
//
// ---------------------------------------------------------------------------
// The derived verdict
// ---------------------------------------------------------------------------
//
// endTest answers whether the test passed, from what was posted inside its
// bracket -- a script no longer returns a verdict of its own (see
// core/catalog/test_catalog.hpp). These pin the rule down: at least one check, and no
// failed one.
//

TEST_F( JournalTest, ATestPassesWhenEveryCheckInItPassed)
{
    core::journal().begin( core::RunInfo{});
    core::journal().beginTest( "SupplyRail", {});

    core::journal().post( core::JournalRecord{ .Method = core::Verb::Verify, .Subject = "5V",  .Passed = true });
    core::journal().post( core::JournalRecord{ .Method = core::Verb::Verify, .Subject = "3V3", .Passed = true });

    EXPECT_TRUE( core::journal().endTest());

    ASSERT_EQ( mSink.TestEnds.size(), 1u);
    EXPECT_TRUE( mSink.TestEnds[ 0].Passed) << "the sinks are told the same verdict the caller gets";
}

//
// One failed check is enough, wherever it falls -- and the checks after it
// still run and are still recorded, which is what the non-short-circuiting
// fold a script used to write by hand was for.
//
TEST_F( JournalTest, ATestFailsWhenAnyCheckInItFailed)
{
    core::journal().begin( core::RunInfo{});
    core::journal().beginTest( "SupplyRail", {});

    core::journal().post( core::JournalRecord{ .Method = core::Verb::Verify, .Subject = "5V",  .Passed = true });
    core::journal().post( core::JournalRecord{ .Method = core::Verb::Verify, .Subject = "3V3", .Passed = false });
    core::journal().post( core::JournalRecord{ .Method = core::Verb::Verify, .Subject = "12V", .Passed = true });

    EXPECT_FALSE( core::journal().endTest());

    ASSERT_EQ( mSink.TestEnds.size(), 1u);
    EXPECT_FALSE( mSink.TestEnds[ 0].Passed);
}

//
// The half that is a deliberate rule rather than arithmetic: a test that
// checked nothing has established nothing, and calling that a pass is exactly
// how a script whose checks were removed used to go green.
//
// It says so in the log as well as in the verdict -- a bare RESULT [FAIL] with
// nothing above it reads like a bug in the runner rather than a finding about
// the script.
//
TEST_F( JournalTest, ATestThatRecordedNoCheckFailsAndSaysSo)
{
    core::journal().begin( core::RunInfo{});
    core::journal().beginTest( "SupplyRail", {});

    EXPECT_FALSE( core::journal().endTest());

    ASSERT_EQ( mSink.Events.size(), 1u);

    const auto & event = mSink.Events.front();

    EXPECT_EQ( event.Test,   "SupplyRail") << "posted inside the bracket it is about";
    EXPECT_EQ( event.Value,  core::kUncheckedValue);
    EXPECT_EQ( event.Passed, std::optional<bool>{ false });
    EXPECT_NE( event.Detail.find( "no check was recorded"), std::string::npos);
}

//
// Only events carrying a pass/fail notion count as checks. A script that
// energised a rail and routed a path and then measured nothing has still
// checked nothing -- and a Measure is not a check either: it produces a
// reading, and what a reading is worth is what a criterion says about it.
//
TEST_F( JournalTest, EventsWithNoVerdictOfTheirOwnDoNotCountAsChecks)
{
    core::journal().begin( core::RunInfo{});
    core::journal().beginTest( "SupplyRail", {});

    core::journal().post( core::JournalRecord{ .Method = core::Verb::Apply,   .Subject = "DcP1" });
    core::journal().post( core::JournalRecord{ .Method = core::Verb::Connect, .Subject = "DcP1" });
    core::journal().post( core::JournalRecord{ .Method = core::Verb::Measure, .Subject = "Output5V" });

    EXPECT_FALSE( core::journal().endTest());
}

//
// A hook's events are attributable without being a test's. The bracket runs
// alongside the group's own -- inside it for a group's SETUP/TEARDOWN, outside
// every group for the catalog's RUN_ pair -- and stamps a phase id that is
// deliberately not a test id (see core::JournalEvent::Phase, and
// core::kRunScope in core/session/recording.hpp for what depends on that).
//
TEST_F( JournalTest, EventsInsideAHookAreStampedWithItsPhase)
{
    core::journal().begin( core::RunInfo{});
    core::journal().beginGroup( "OutputVoltage", {});

    core::journal().beginPhase( "setup", "SETUP, bracketing this group's tests");
    core::journal().post( core::JournalRecord{ .Method = core::Verb::Apply, .Subject = "DcP1" });
    core::journal().endPhase();

    // After the bracket closes, and still inside the group.
    core::journal().post( core::JournalRecord{ .Method = core::Verb::Note, .Subject = "after" });

    ASSERT_EQ( mSink.Events.size(), 2u);

    EXPECT_EQ( mSink.Events[ 0].Group, "OutputVoltage");
    EXPECT_EQ( mSink.Events[ 0].Phase, "setup");

    // The phase is not a test, and must not be mistaken for one anywhere.
    EXPECT_TRUE( mSink.Events[ 0].Test.empty());

    EXPECT_TRUE( mSink.Events[ 1].Phase.empty());
    EXPECT_EQ( mSink.Events[ 1].Group, "OutputVoltage");

    ASSERT_EQ( mSink.PhaseStarts.size(), 1u);
    EXPECT_EQ( mSink.PhaseStarts[ 0].Test,  "setup");

    // The group the bracket ran inside -- what tells a group's own SETUP from
    // the catalog's RUN_SETUP, since both spell their id the same way.
    EXPECT_EQ( mSink.PhaseStarts[ 0].Group, "OutputVoltage");

    ASSERT_EQ( mSink.PhaseEnds.size(), 1u);
    EXPECT_EQ( mSink.PhaseEnds[ 0], "setup");

    ASSERT_EQ( mSink.Descriptions.size(), 2u);
    EXPECT_EQ( mSink.Descriptions[ 1], "SETUP, bracketing this group's tests");
}

//
// The whole reason a phase is not beginTest with the id "setup": that bracket
// opens the per-test tally, and endTest turns a bracket holding no check into a
// failure that says so. A hook that verifies nothing is doing its job, and a
// run whose only sin was a quiet SETUP must not go red for it.
//
TEST_F( JournalTest, AHookThatChecksNothingIsNotAFailedTest)
{
    core::journal().begin( core::RunInfo{});

    core::journal().beginPhase( "setup", {});
    core::journal().post( core::JournalRecord{ .Method = core::Verb::Apply, .Subject = "DcP1" });
    core::journal().endPhase();

    // One event, the one that was posted -- no "no check was recorded" verdict
    // anywhere, because nothing here was a test.
    ASSERT_EQ( mSink.Events.size(), 1u);
    EXPECT_FALSE( mSink.Events[ 0].Passed.has_value());
    EXPECT_TRUE( mSink.TestEnds.empty());
}

//
// A check inside a hook is still nobody's test check -- the tally is opened by
// beginTest and by nothing else, so a RUN_SETUP that verifies its rails cannot
// satisfy the "at least one check" half of the next test's verdict.
//
TEST_F( JournalTest, ChecksInsideAHookDoNotCountTowardsATest)
{
    core::journal().begin( core::RunInfo{});

    core::journal().beginPhase( "setup", {});
    core::journal().post( core::JournalRecord{ .Method = core::Verb::Verify, .Subject = "28V", .Passed = true });
    core::journal().endPhase();

    core::journal().beginTest( "SupplyRail", {});

    // Nothing was checked in the bracket that matters, so this fails.
    EXPECT_FALSE( core::journal().endTest());
}

//
// The tally belongs to one test, not to the run: a failure in one must not
// darken the next, and a run's own verdict is the runner's fold over these
// (see framework/runner/src/main.cpp).
//
TEST_F( JournalTest, EachTestsVerdictIsIndependentOfTheOneBeforeIt)
{
    core::journal().begin( core::RunInfo{});

    core::journal().beginTest( "Failing", {});
    core::journal().post( core::JournalRecord{ .Method = core::Verb::Verify, .Subject = "5V", .Passed = false });
    EXPECT_FALSE( core::journal().endTest());

    core::journal().beginTest( "Passing", {});
    core::journal().post( core::JournalRecord{ .Method = core::Verb::Verify, .Subject = "5V", .Passed = true });
    EXPECT_TRUE( core::journal().endTest());
}

//
// An event posted outside any test bracket is nobody's check. The Safe a
// safing pass posts after the last test has closed is the real case (see
// hal/verbs/safing.hpp), and counting it would attach it to whichever test happened
// to run last.
//
TEST_F( JournalTest, ChecksPostedOutsideATestDoNotReachTheNextOne)
{
    core::journal().begin( core::RunInfo{});

    core::journal().post( core::JournalRecord{ .Method = core::Verb::Verify, .Subject = "orphan", .Passed = false });

    core::journal().beginTest( "SupplyRail", {});
    core::journal().post( core::JournalRecord{ .Method = core::Verb::Verify, .Subject = "5V", .Passed = true });

    EXPECT_TRUE( core::journal().endTest());
}

//
// The backstop on the value column -- see kMaxJournalValueLength in
// core/journal/journal.hpp. Nothing this framework produces today reaches it
// (core::describeValue abridges a payload well below it), which is the point:
// it exists for the verb that has not been written yet.
//
TEST_F( JournalTest, AnOversizedValueIsTrimmedBeforeAnySinkSeesIt)
{
    core::journal().begin( core::RunInfo{});

    core::journal().post( core::JournalRecord{
        .Method  = core::Verb::Read,
        .Subject = "Ser1.Data",
        .Value   = std::string( 5000, 'x')
    });

    ASSERT_EQ( mSink.Events.size(), 1u);

    const auto & value = mSink.Events.front().Value;

    EXPECT_EQ( value.size(), core::kMaxJournalValueLength);
    EXPECT_TRUE( value.ends_with( "...")) << value;
}

TEST_F( JournalTest, AValueInsideTheBoundIsUntouched)
{
    core::journal().begin( core::RunInfo{});

    core::journal().post( core::JournalRecord{
        .Method  = core::Verb::Measure,
        .Subject = "Output5V",
        .Value   = "5.021 V"
    });

    ASSERT_EQ( mSink.Events.size(), 1u);
    EXPECT_EQ( mSink.Events.front().Value, "5.021 V");
}

TEST( CoreJournalTime, PreEpochTimestampsDoNotProduceNegativeMilliseconds)
{
    EXPECT_EQ( core::isoUtcFromUnixMillis( -1), "1969-12-31T23:59:59.999Z");
}

TEST( CoreJournalRunInfo, DefaultsCarryWhatTheBuildKnowsAboutItself)
{
    const auto info = core::defaultRunInfo();

    EXPECT_EQ( info.FrameworkName, "Thorium");

    // Set from CMake's PROJECT_VERSION / THORIUM_CRITERIA_VARIANT / the DUT and
    // rig cache variables -- non-empty here is the assertion that the compile
    // definitions actually reached this translation unit.
    EXPECT_FALSE( info.FrameworkVersion.empty());
    EXPECT_FALSE( info.CriteriaVariant.empty());
    EXPECT_FALSE( info.DutName.empty());
    EXPECT_FALSE( info.RigName.empty());

    // Not derivable here -- the caller supplies them; see defaultRunInfo.
    EXPECT_TRUE( info.DutSerial.empty());
    EXPECT_TRUE( info.CommandLine.empty());

    EXPECT_EQ( info.StartedUtc.size(), std::string( "1970-01-01T00:00:00.000Z").size());
}
