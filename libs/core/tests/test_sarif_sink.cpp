#include "core/sarif_sink.hpp"

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
                info.DutName          = "DeviceX_StdAdapter";
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
    EXPECT_TRUE( contains( text, "\"dutName\": \"DeviceX_StdAdapter\""));
    EXPECT_TRUE( contains( text, "\"dutSerial\": \"SN-000123\""));
    EXPECT_TRUE( contains( text, "\"rigName\": \"bench-7\""));
    EXPECT_TRUE( contains( text, "\"criteriaVariant\": \"stress\""));

    // automationDetails: what makes two runs comparable.
    EXPECT_TRUE( contains( text, "thorium/DeviceX_StdAdapter/stress/"));
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
