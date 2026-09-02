#include "core/journal/sarif_sink.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace
{
    auto readFile( const std::filesystem::path & path) -> std::string
    {
        std::ifstream in( path, std::ios::in | std::ios::binary);
        std::ostringstream contents;

        contents << in.rdbuf();

        return contents.str();
    }

    //
    // Brace/bracket balance plus quote parity. Not a JSON parser -- core has no
    // dependency to bring one in, and full validation belongs to whichever
    // consumer reads the file. What matters here is the failure mode this sink
    // could plausibly have: a comma or a brace emitted in the wrong place while
    // walking a variable-length results array, which shows up as an imbalance.
    //
    auto isBalancedJson( const std::string & text) -> bool
    {
        int  depth    = 0;
        bool inString = false;

        for( std::size_t i = 0; i < text.size(); ++i)
        {
            const char c = text[ i];

            if( inString)
            {
                if( c == '\\')
                {
                    ++i;   // skip whatever this escapes, quote included
                }
                else if( c == '"')
                {
                    inString = false;
                }

                continue;
            }

            switch( c)
            {
                case '"': inString = true; break;
                case '{':
                case '[': ++depth; break;
                case '}':
                case ']': --depth; if( depth < 0) return false; break;
                default: break;
            }
        }

        return depth == 0 && !inString;
    }

    auto contains( const std::string & text, const std::string & needle) -> bool
    {
        return text.find( needle) != std::string::npos;
    }

    auto verifyEvent( const std::string & criteriaGroup, const std::string & id, const bool passed) -> core::JournalEvent
    {
        core::JournalEvent event;
        event.Method       = core::Verb::Verify;
        event.Subject      = id;
        event.SubjectGroup = criteriaGroup;
        event.Detail       = "a criterion description";
        event.Value        = "5.021 V";
        event.Numeric      = 5.021;
        event.Unit         = "V";
        event.CriterionText = "= 5 V +/-0.05 V";
        event.Passed       = passed;
        event.Group        = "OutputVoltage";
        event.Test         = "SupplyRail";
        return event;
    }

    class SarifSinkTest : public ::testing::Test
    {
        protected:
            auto SetUp() -> void override
            {
                mPath = std::filesystem::temp_directory_path() /
                        ( "thorium-sarif-test-" + std::string( ::testing::UnitTest::GetInstance()->current_test_info()->name()) + ".sarif");

                std::filesystem::remove( mPath);
            }

            auto TearDown() -> void override
            {
                std::filesystem::remove( mPath);
            }

            [[nodiscard]]
            static auto runInfo() -> core::RunInfo
            {
                core::RunInfo info;
                info.FrameworkName    = "Thorium";
                info.FrameworkVersion = "0.1.0";
                info.CriteriaVariant  = "stress";
                info.CriteriaMaster   = "production";
                info.DutName          = "DeviceX";
                info.DutSerial        = "SN-000123";
                info.RigName          = "bench-7";
                info.Operator         = "thomas";
                info.HostName         = "rig-console";
                info.CommandLine      = "run_scripts --select=SupplyRail";
                info.StartedUtc       = "2026-07-30T09:14:02.371Z";
                return info;
            }

            std::filesystem::path mPath;
    };
} // namespace

TEST_F( SarifSinkTest, LogIsWellFormedAndDeclaresItsSchema)
{
    {
        core::SarifSink sink( mPath.string());

        sink.onRunStart( runInfo());
        sink.onEvent( verifyEvent( "FS_Supply_1", "FS_Supply_5V0", true));
        sink.onEvent( verifyEvent( "FS_Supply_1", "FS_Supply_3V3", false));
        sink.onRunEnd( false);
    }

    const auto text = readFile( mPath);

    EXPECT_TRUE( isBalancedJson( text));
    EXPECT_TRUE( contains( text, "sarif-schema-2.1.0.json"));
    EXPECT_TRUE( contains( text, "\"version\": \"2.1.0\""));
}

//
// A results array of one, and of none, are where an "insert a comma between
// elements" loop goes wrong.
//
TEST_F( SarifSinkTest, LogIsWellFormedWithNoEventsAtAll)
{
    {
        core::SarifSink sink( mPath.string());
        sink.onRunStart( runInfo());
        sink.onRunEnd( true);
    }

    EXPECT_TRUE( isBalancedJson( readFile( mPath)));
}

TEST_F( SarifSinkTest, LogIsWellFormedWithExactlyOneEvent)
{
    {
        core::SarifSink sink( mPath.string());
        sink.onRunStart( runInfo());
        sink.onEvent( verifyEvent( "FS_Supply_1", "FS_Supply_5V0", true));
        sink.onRunEnd( true);
    }

    EXPECT_TRUE( isBalancedJson( readFile( mPath)));
}

//
// SARIF's level (how bad) and kind (what sort of finding) are separate axes, and
// getting them wrong is what makes a consumer report every measurement a run
// took as an issue.
//
TEST_F( SarifSinkTest, PassFailAndInformationalAreDistinguished)
{
    {
        core::SarifSink sink( mPath.string());

        sink.onRunStart( runInfo());
        sink.onEvent( verifyEvent( "FS_Supply_1", "FS_Supply_5V0", true));
        sink.onEvent( verifyEvent( "FS_Supply_1", "FS_Supply_3V3", false));

        core::JournalEvent measure;
        measure.Method     = core::Verb::Measure;
        measure.Subject    = "Output5V";
        measure.Instrument = "Dmm1";
        measure.Value      = "5.021 V";
        sink.onEvent( measure);

        sink.onRunEnd( false);
    }

    const auto text = readFile( mPath);

    EXPECT_TRUE( contains( text, "\"kind\": \"pass\""));
    EXPECT_TRUE( contains( text, "\"kind\": \"fail\""));
    EXPECT_TRUE( contains( text, "\"kind\": \"informational\""));
    EXPECT_TRUE( contains( text, "\"level\": \"error\""));
}

//
// Every verb reaches this log, unlike the human stream -- a routing step omitted
// for brevity is exactly the step that explains a failed reading.
//
TEST_F( SarifSinkTest, EveryVerbIsRecorded)
{
    {
        core::SarifSink sink( mPath.string());
        sink.onRunStart( runInfo());

        for( const auto method : { core::Verb::Measure, core::Verb::Apply, core::Verb::Remove,
                                   core::Verb::Connect, core::Verb::Disconnect, core::Verb::Safe })
        {
            core::JournalEvent event;
            event.Method  = method;
            event.Subject = "DcP1";
            sink.onEvent( event);
        }

        sink.onRunEnd( true);
    }

    const auto text = readFile( mPath);

    EXPECT_TRUE( contains( text, "Thorium/Measure"));
    EXPECT_TRUE( contains( text, "Thorium/Apply"));
    EXPECT_TRUE( contains( text, "Thorium/Remove"));
    EXPECT_TRUE( contains( text, "Thorium/Connect"));
    EXPECT_TRUE( contains( text, "Thorium/Disconnect"));
    EXPECT_TRUE( contains( text, "Thorium/Safe"));
}

TEST_F( SarifSinkTest, TraceabilityBagCarriesEveryRunInfoField)
{
    {
        core::SarifSink sink( mPath.string());
        sink.onRunStart( runInfo());
        sink.onRunEnd( true);
    }

    const auto text = readFile( mPath);

    // tool.driver + invocation: where a generic SARIF consumer looks.
    EXPECT_TRUE( contains( text, "\"name\": \"Thorium\""));
    EXPECT_TRUE( contains( text, "\"version\": \"0.1.0\""));
    EXPECT_TRUE( contains( text, "\"startTimeUtc\": \"2026-07-30T09:14:02.371Z\""));
    EXPECT_TRUE( contains( text, "run_scripts --select=SupplyRail"));
    EXPECT_TRUE( contains( text, "\"machine\": \"rig-console\""));
    EXPECT_TRUE( contains( text, "\"account\": \"thomas\""));

    // run.properties: the facts SARIF has no standard slot for.
    EXPECT_TRUE( contains( text, "\"dutName\": \"DeviceX\""));
    EXPECT_TRUE( contains( text, "\"dutSerial\": \"SN-000123\""));
    EXPECT_TRUE( contains( text, "\"rigName\": \"bench-7\""));
    EXPECT_TRUE( contains( text, "\"criteriaVariant\": \"stress\""));

    //
    // Both halves of "which tolerances were these": stress supplied the rows it
    // changes, production every row it does not (CRIT_FROM_MASTER). A consumer
    // trending a series needs the pair, not the applied name alone.
    //
    EXPECT_TRUE( contains( text, "\"criteriaMaster\": \"production\""));

    // automationDetails: what makes two runs comparable.
    EXPECT_TRUE( contains( text, "thorium/DeviceX/stress/"));

    //
    // A JSON boolean, not a quoted word -- so a consumer filtering out the runs
    // that never touched hardware writes benchAttached == false rather than a
    // string comparison against whichever spelling this sink chose.
    //
    EXPECT_TRUE( contains( text, "\"benchAttached\": true"));
}

//
// Present on every run rather than only when it is interesting, unlike its
// optional neighbours: those are empty when nobody supplied them, and this is a
// yes/no the framework always knows. A consumer that had to treat "absent" as
// "probably attached" would be guessing about the one field that says whether
// the results mean anything about a DUT.
//
TEST_F( SarifSinkTest, TheHeaderSaysWhetherARigWasThere)
{
    auto detached = runInfo();

    detached.BenchAttached = false;

    {
        core::SarifSink sink( mPath.string());
        sink.onRunStart( detached);
        sink.onRunEnd( true);
    }

    EXPECT_TRUE( contains( readFile( mPath), "\"benchAttached\": false"));
}

//
// A criterion checked from two different catalog tests is one rule with two
// results -- that is what makes "results for rule X" mean "every time this
// requirement was checked".
//
TEST_F( SarifSinkTest, ARepeatedCriterionIsOneRuleWithSeveralResults)
{
    {
        core::SarifSink sink( mPath.string());
        sink.onRunStart( runInfo());

        auto first  = verifyEvent( "FS_Supply_1", "FS_Supply_5V0", true);
        auto second = verifyEvent( "FS_Supply_1", "FS_Supply_5V0", false);
        second.Test = "SomeOtherTest";

        sink.onEvent( first);
        sink.onEvent( second);
        sink.onRunEnd( false);
    }

    const auto text = readFile( mPath);

    // Once in the rules array, twice as a result's ruleId.
    std::size_t occurrences = 0;

    for( std::size_t at = text.find( "FS_Supply_1/FS_Supply_5V0"); at != std::string::npos;
         at = text.find( "FS_Supply_1/FS_Supply_5V0", at + 1))
    {
        ++occurrences;
    }

    EXPECT_EQ( occurrences, 3u);
}

TEST_F( SarifSinkTest, ValueIsCarriedBothAsTextAndAsANumber)
{
    {
        core::SarifSink sink( mPath.string());
        sink.onRunStart( runInfo());
        sink.onEvent( verifyEvent( "FS_Supply_1", "FS_Supply_5V0", true));
        sink.onRunEnd( true);
    }

    const auto text = readFile( mPath);

    EXPECT_TRUE( contains( text, "\"value\": \"5.021 V\""));
    EXPECT_TRUE( contains( text, "\"unit\": \"V\""));
    EXPECT_TRUE( contains( text, "\"numericValue\": 5.021"));

    //
    // And the tolerance the run actually enforced, so a consumer reporting a
    // failure can say what was required without resolving the ruleId back to a
    // criteria file it may not have.
    //
    EXPECT_TRUE( contains( text, "\"criterion\": \"= 5 V +/-0.05 V\""));
}

//
// A run that died before onRunEnd is the one whose machine log is most worth
// having.
//
TEST_F( SarifSinkTest, LogIsWrittenFromTheDestructorIfTheRunNeverClosed)
{
    {
        core::SarifSink sink( mPath.string());
        sink.onRunStart( runInfo());
        sink.onEvent( verifyEvent( "FS_Supply_1", "FS_Supply_5V0", false));
    }

    const auto text = readFile( mPath);

    EXPECT_TRUE( isBalancedJson( text));
    EXPECT_TRUE( contains( text, "\"allPassed\": false"));
}

//
// The catalog's own shape has nowhere else to go: SARIF has no per-run
// structure section, and the titles a GROUP/TEST entry gives are otherwise
// nowhere in the document at all.
//
TEST_F( SarifSinkTest, GroupAndTestBoundariesCarryTheirIdsAndTitles)
{
    {
        core::SarifSink sink( mPath.string());

        sink.onRunStart( runInfo());
        sink.onGroupStart( "OutputVoltage", "Tests validating DUT output voltage rails");
        sink.onTestStart( "SupplyRail", "Verify supply rail voltages via matrix");
        sink.onEvent( verifyEvent( "FS_Supply_1", "FS_Supply_5V0", true));
        sink.onRunEnd( true);
    }

    const auto text = readFile( mPath);

    EXPECT_TRUE( isBalancedJson( text));

    EXPECT_TRUE( contains( text, R"("ruleId": "Thorium/Group")"));
    EXPECT_TRUE( contains( text, R"("ruleId": "Thorium/Test")"));

    EXPECT_TRUE( contains( text, R"("title": "Tests validating DUT output voltage rails")"));
    EXPECT_TRUE( contains( text, R"("title": "Verify supply rail voltages via matrix")"));

    // The test is qualified by the group it is nested in, the group by nothing.
    EXPECT_TRUE( contains( text, R"("fullyQualifiedName": "OutputVoltage")"));
    EXPECT_TRUE( contains( text, R"("fullyQualifiedName": "OutputVoltage/SupplyRail")"));
}

//
// A boundary is part of what the run did, not something the run found -- the
// same standing the "pass 2 of 3" note has. A consumer counting a run's issues
// must not count the catalog it walked.
//
TEST_F( SarifSinkTest, ABoundaryIsInformationalAndNotAFinding)
{
    {
        core::SarifSink sink( mPath.string());

        sink.onRunStart( runInfo());
        sink.onGroupStart( "OutputVoltage", "Output rail tests");
        sink.onTestStart( "SupplyRail", "Verify supply rails");
        sink.onPhaseStart( "OutputVoltage", "setup", "SETUP, bracketing this group's tests");
        sink.onRunEnd( true);
    }

    const auto text = readFile( mPath);

    EXPECT_FALSE( contains( text, R"("level": "error")"));
    EXPECT_FALSE( contains( text, R"("kind": "pass")"));
    EXPECT_FALSE( contains( text, R"("kind": "fail")"));

    // ...and no sequence invented for a result that was never posted.
    EXPECT_FALSE( contains( text, R"("sequence")"));
}

//
// A group whose every test was deselected, or whose SETUP refused, still ran --
// and a log that shows nothing at all for it cannot be told from a log for a
// run that never reached it.
//
TEST_F( SarifSinkTest, AGroupThatRanNoTestStillNamesItself)
{
    {
        core::SarifSink sink( mPath.string());

        sink.onRunStart( runInfo());
        sink.onGroupStart( "Transient", "Tests validating DUT behaviour while a supply is disturbed");
        sink.onGroupEnd( "Transient");
        sink.onRunEnd( true);
    }

    const auto text = readFile( mPath);

    EXPECT_TRUE( isBalancedJson( text));
    EXPECT_TRUE( contains( text, R"("group": "Transient")"));
}

//
// SETUP and TEARDOWN are what a group does to the rig before and after its
// tests, and their readings are otherwise indistinguishable here from the first
// test's -- neither carries a test id, deliberately (see
// core::JournalEvent::Phase).
//
TEST_F( SarifSinkTest, ASetupBoundaryIsIdentifiedAndItsEventsCarryThePhase)
{
    {
        core::SarifSink sink( mPath.string());

        sink.onRunStart( runInfo());
        sink.onGroupStart( "OutputVoltage", "Output rail tests");
        sink.onPhaseStart( "OutputVoltage", "setup", "SETUP, bracketing this group's tests");

        core::JournalEvent apply;
        apply.Method  = core::Verb::Apply;
        apply.Subject = "DcP1";
        apply.Group   = "OutputVoltage";
        apply.Phase   = "setup";
        sink.onEvent( apply);

        sink.onRunEnd( true);
    }

    const auto text = readFile( mPath);

    EXPECT_TRUE( contains( text, R"("ruleId": "Thorium/Phase")"));
    EXPECT_TRUE( contains( text, R"("phase": "setup")"));
    EXPECT_TRUE( contains( text, R"("title": "SETUP, bracketing this group's tests")"));

    // The hook is filed under the group it brackets, not beside it.
    EXPECT_TRUE( contains( text, R"("fullyQualifiedName": "OutputVoltage/setup")"));
}

//
// Both levels spell their id "setup", so what tells them apart is what encloses
// them -- a group for the group's own pair, the run itself for the catalog's
// RUN_SETUP/RUN_TEARDOWN.
//
TEST_F( SarifSinkTest, ARunLevelHookIsQualifiedByNothingAndAGroupsByItsGroup)
{
    {
        core::SarifSink sink( mPath.string());

        sink.onRunStart( runInfo());
        sink.onPhaseStart( {}, "setup", "RUN_SETUP, bracketing the whole selection");
        sink.onGroupStart( "OutputVoltage", "Output rail tests");
        sink.onPhaseStart( "OutputVoltage", "setup", "SETUP, bracketing this group's tests");
        sink.onGroupEnd( "OutputVoltage");

        // After the last group has closed -- where RUN_TEARDOWN runs.
        sink.onPhaseStart( {}, "teardown", "RUN_TEARDOWN, bracketing the whole selection");
        sink.onRunEnd( true);
    }

    const auto text = readFile( mPath);

    EXPECT_TRUE( contains( text, R"("fullyQualifiedName": "setup")"));
    EXPECT_TRUE( contains( text, R"("fullyQualifiedName": "OutputVoltage/setup")"));

    // The run's teardown is not filed inside whichever group happened to be last.
    EXPECT_TRUE( contains( text, R"("fullyQualifiedName": "teardown")"));
    EXPECT_FALSE( contains( text, R"("fullyQualifiedName": "OutputVoltage/teardown")"));
}

TEST( CoreSarifEscape, JsonSyntaxCharactersAreEscaped)
{
    EXPECT_EQ( core::SarifSink::escape( "a\"b"),  "a\\\"b");
    EXPECT_EQ( core::SarifSink::escape( "a\\b"),  "a\\\\b");
    EXPECT_EQ( core::SarifSink::escape( "a\nb"),  "a\\nb");
    EXPECT_EQ( core::SarifSink::escape( "a\tb"),  "a\\tb");
}

TEST( CoreSarifEscape, RemainingControlBytesBecomeUnicodeEscapes)
{
    // A raw control byte makes the whole document unparseable, not merely ugly.
    EXPECT_EQ( core::SarifSink::escape( std::string( 1, '\x01')), "\\u0001");
}

TEST( CoreSarifEscape, ValidUtf8PassesThroughUntouched)
{
    // JSON is UTF-8 by default, so re-encoding could only break a sequence this
    // has no reason to decode.
    const std::string micro = "\xC2\xB5V";

    EXPECT_EQ( core::SarifSink::escape( micro), micro);
}

TEST( CoreSarifRuleId, VerifyUsesTheCriterionsOwnGroupAndId)
{
    core::JournalEvent event;
    event.Method       = core::Verb::Verify;
    event.Subject      = "FS_Supply_5V0";
    event.SubjectGroup = "FS_Supply_1";
    event.Group        = "OutputVoltage";   // the catalog test group, deliberately not used

    EXPECT_EQ( core::SarifSink::ruleIdFor( event), "FS_Supply_1/FS_Supply_5V0");
}

TEST( CoreSarifRuleId, OtherVerbsAreReportedUnderTheirVerbsRule)
{
    core::JournalEvent event;
    event.Method  = core::Verb::Connect;
    event.Subject = "DcP1";

    EXPECT_EQ( core::SarifSink::ruleIdFor( event), "Thorium/Connect");
}

//
// The other half of the criterion/ad-hoc split: a check with no criteria group
// is not indexed by its own prose, it reports under the shared Verify rule.
// What identifies it is the result -- its logical location and message -- not
// the rule, because an inline assertion has no identity to carry between runs.
//
TEST( CoreSarifRuleId, AnAdHocVerifyReportsUnderTheSharedVerifyRule)
{
    //
    // The record an ad-hoc check actually produces: no criteria group, no
    // subject either -- its one line of prose is the description, so that the
    // human report can give it the room a sentence needs (see core/criteria/verify.hpp's
    // three-argument Verify).
    //
    core::JournalEvent event;
    event.Method  = core::Verb::Verify;
    event.Detail  = "Supply voltage at Vout";
    event.Group   = "OutputVoltage";

    EXPECT_EQ( core::SarifSink::ruleIdFor( event), "Thorium/Verify");
}

//
// The distinction is the criteria group and nothing else -- two checks whose
// only difference is whether a table declared them must not land on the same
// rule, or promoting an ad-hoc check to a CRIT would change nothing.
//
TEST( CoreSarifRuleId, PromotingAnAdHocCheckToACriterionChangesItsRule)
{
    core::JournalEvent adHoc;
    adHoc.Method  = core::Verb::Verify;
    adHoc.Subject = "FS_Supply_5V0";

    auto declared = adHoc;
    declared.SubjectGroup = "FS_Supply_1";

    EXPECT_EQ( core::SarifSink::ruleIdFor( adHoc),    "Thorium/Verify");
    EXPECT_EQ( core::SarifSink::ruleIdFor( declared), "FS_Supply_1/FS_Supply_5V0");
}

//
// The machine log has no columns, so it does not have to leave an ad-hoc
// check's prose out of the place a consumer looks for "what is this result
// about". A nameless logical location would make one run's ad-hoc results
// indistinguishable from each other, which is the one thing they need to not
// be, now that they all share a rule.
//
TEST_F( SarifSinkTest, AnAdHocCheckIsNamedByItsProseNotLeftAnonymous)
{
    {
        core::SarifSink sink( mPath.string());

        core::JournalEvent event;
        event.Method = core::Verb::Verify;
        event.Detail = "Supply voltage at Vout";
        event.Value  = "12.03 V";
        event.Passed = true;

        sink.onEvent( event);
        sink.onRunEnd( true);
    }

    const auto text = readFile( mPath);

    EXPECT_NE( text.find( R"("name": "Supply voltage at Vout")"), std::string::npos) << text;

    // ...and said once in the message, not twice -- the prose is standing in as
    // the subject, so repeating it as a trailing description is noise.
    EXPECT_NE( text.find( "Verify Supply voltage at Vout = 12.03 V [PASS]\""), std::string::npos) << text;
}
