#include "core/rtf_sink.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace
{
    //
    // Reads the file back *while the sink still owns it* -- which is the whole
    // point of several tests below: the RTF log has to be a complete, openable
    // document mid-run, not only after the sink is destroyed.
    //
    auto readFile( const std::filesystem::path & path) -> std::string
    {
        std::ifstream in( path, std::ios::in | std::ios::binary);
        std::ostringstream contents;

        contents << in.rdbuf();

        return contents.str();
    }

    auto isCompleteRtfDocument( const std::string & text) -> bool
    {
        if( !text.starts_with( "{\\rtf1") || text.empty() || text.back() != '}')
        {
            return false;
        }

        //
        // Balanced braces, counting RTF's own escapes for a literal brace as
        // ordinary characters -- an unbalanced document is exactly what the
        // seek-back-over-the-trailer scheme exists to avoid producing.
        //
        int depth = 0;

        for( std::size_t i = 0; i < text.size(); ++i)
        {
            if( text[ i] == '\\' && i + 1 < text.size())
            {
                ++i;   // whatever follows a backslash is escaped, brace included
                continue;
            }

            if( text[ i] == '{')
            {
                ++depth;
            }
            else if( text[ i] == '}')
            {
                --depth;

                if( depth < 0)
                {
                    return false;
                }
            }
        }

        return depth == 0;
    }

    class RtfSinkTest : public ::testing::Test
    {
        protected:
            auto SetUp() -> void override
            {
                mPath = std::filesystem::temp_directory_path() /
                        ( "thorium-rtf-test-" + std::string( ::testing::UnitTest::GetInstance()->current_test_info()->name()) + ".rtf");

                std::filesystem::remove( mPath);
            }

            auto TearDown() -> void override
            {
                std::filesystem::remove( mPath);
            }

            [[nodiscard]]
            auto runInfo() const -> core::RunInfo
            {
                core::RunInfo info;
                info.FrameworkName    = "Thorium";
                info.FrameworkVersion = "0.1.0";
                info.DutName          = "DeviceX_StdAdapter";
                info.CriteriaVariant  = "production";
                info.StartedUtc       = "2026-07-30T09:14:02.371Z";
                return info;
            }

            [[nodiscard]]
            static auto measureEvent() -> core::JournalEvent
            {
                core::JournalEvent event;
                event.Method     = core::Verb::Measure;
                event.Subject    = "Output5V";
                event.Instrument = "Dmm1";
                event.Value      = "5.021 V";
                return event;
            }

            std::filesystem::path mPath;
    };
} // namespace

//
// The reason this class owns an ofstream and rewrites its trailer: an operator
// (or a report tool) must be able to open the log while the run is still going.
//
TEST_F( RtfSinkTest, FileIsACompleteDocumentAfterEveryEvent)
{
    core::RtfSink sink( mPath.string());

    // Valid before anything is logged at all -- a run that dies during setup
    // still leaves something openable.
    EXPECT_TRUE( isCompleteRtfDocument( readFile( mPath)));

    sink.onRunStart( runInfo());
    EXPECT_TRUE( isCompleteRtfDocument( readFile( mPath)));

    sink.onTestStart( "OutputVoltage", "SupplyRail", "Verify supply rails");
    EXPECT_TRUE( isCompleteRtfDocument( readFile( mPath)));

    sink.onEvent( measureEvent());
    EXPECT_TRUE( isCompleteRtfDocument( readFile( mPath)));

    sink.onTestEnd( "OutputVoltage", "SupplyRail", false);
    EXPECT_TRUE( isCompleteRtfDocument( readFile( mPath)));
}

//
// The trailer rewrite must not leave a stray brace behind once the run closes
// out -- the finished document has exactly one closing brace for its outer group.
//
TEST_F( RtfSinkTest, TrailerIsWrittenOnceWhenTheRunEnds)
{
    {
        core::RtfSink sink( mPath.string());

        sink.onRunStart( runInfo());
        sink.onEvent( measureEvent());
        sink.onRunEnd( false);
    }

    const auto text = readFile( mPath);

    EXPECT_TRUE( isCompleteRtfDocument( text));
    EXPECT_FALSE( text.ends_with( "}}"));
}

//
// A run whose last event never reached onRunEnd (an exception past the runner)
// still has to leave a finished document -- see ~RtfSink.
//
TEST_F( RtfSinkTest, DocumentIsFinalisedEvenWithoutOnRunEnd)
{
    {
        core::RtfSink sink( mPath.string());
        sink.onRunStart( runInfo());
        sink.onEvent( measureEvent());
    }

    EXPECT_TRUE( isCompleteRtfDocument( readFile( mPath)));
}

TEST_F( RtfSinkTest, HeaderCarriesAFontAndColourTable)
{
    core::RtfSink sink( mPath.string());

    const auto text = readFile( mPath);

    EXPECT_NE( text.find( "\\fonttbl"), std::string::npos);
    EXPECT_NE( text.find( "\\colortbl"), std::string::npos);
}

//
// Colour coding is the point of choosing RTF over plain text, so a pass and a
// fail must not come out in the same colour.
//
TEST_F( RtfSinkTest, PassAndFailAreWrittenInDifferentColours)
{
    core::RtfSink sink( mPath.string());

    core::JournalEvent passing;
    passing.Method  = core::Verb::Verify;
    passing.Subject = "FS_Supply_5V0";
    passing.Value   = "5.021 V";
    passing.Passed  = true;

    auto failing = passing;
    failing.Subject = "FS_Supply_3V3";
    failing.Passed  = false;

    sink.onEvent( passing);
    sink.onEvent( failing);

    const auto text = readFile( mPath);

    const auto passLine = text.find( "FS_Supply_5V0");
    const auto failLine = text.find( "FS_Supply_3V3");

    ASSERT_NE( passLine, std::string::npos);
    ASSERT_NE( failLine, std::string::npos);

    // \cf2 is green (passed), \cf3 red (failed) -- see the colour table in
    // rtf_sink.cpp. Each marker appears on its own line's run.
    EXPECT_NE( text.rfind( "\\cf2", passLine), std::string::npos);
    EXPECT_NE( text.rfind( "\\cf3", failLine), std::string::npos);
}

//
// A criterion description containing a brace or a backslash must not be able to
// corrupt the document -- these are RTF's own syntax.
//
TEST( CoreRtfEscape, RtfSyntaxCharactersAreEscaped)
{
    EXPECT_EQ( core::RtfSink::escape( "a{b}c"),  "a\\{b\\}c");
    EXPECT_EQ( core::RtfSink::escape( "a\\b"),   "a\\\\b");
    EXPECT_EQ( core::RtfSink::escape( "plain"),  "plain");
}

TEST( CoreRtfEscape, NonAsciiBytesAreGivenInAnsiHexForm)
{
    // \ansi is declared in the preamble, so a byte above ASCII needs \'hh form.
    EXPECT_EQ( core::RtfSink::escape( "\xB5V"), "\\'b5V");
}

TEST( CoreRtfEscape, EmbeddedNewlinesStayVisible)
{
    // A raw newline is whitespace to an RTF reader and would silently vanish.
    EXPECT_EQ( core::RtfSink::escape( "a\nb"), "a\\line b");
}

TEST( CoreRtfSink, UnwritablePathIsAHardFailure)
{
    //
    // A run asked to produce a log and silently not producing one is the failure
    // mode the throw exists to prevent.
    //
    EXPECT_THROW( core::RtfSink( "/definitely/not/a/directory/thorium.rtf"), std::runtime_error);
}
