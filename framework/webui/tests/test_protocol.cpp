//
// Tests over the protocol layer -- the half of framework/webui/ that talks
// to run_scripts and to nothing else (no httplib, no process spawning of its
// own).
//
// Black-box against real output, not against hand-written fixtures wherever
// that is possible: framework/webui/CMakeLists.txt points
// THORIUM_WEBUI_TEST_BINARY at this same build's own run_scripts
// ($<TARGET_FILE:run_scripts>), and the tests below drive it. A fixture
// asserting on a schema nobody produces would pass forever after the schema
// changed, which is the one failure this layer cannot afford -- it is the
// whole contract between two processes.
//
// GoogleTest, like every other test target in this repository, and compiled
// from the same third_party/googletest-1.18.0 -- vendored as source rather
// than taken from a package manager so it is built by this project's own
// compiler (see cmake/FetchGTest.cmake).
//
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "protocol/command.hpp"
#include "protocol/events.hpp"
#include "protocol/json.hpp"
#include "protocol/options.hpp"
#include "protocol/suite.hpp"

namespace
{
    const std::string kBinary = THORIUM_WEBUI_TEST_BINARY;

    //
    // Runs a command and returns its stdout. popen rather than argv-based
    // spawning, because this is a fixed, developer-written command string on
    // a test machine, not operator text from a POST body -- the real program
    // (child_stream.cpp) spawns argv directly and never through a shell,
    // specifically because it cannot make that same assumption about its
    // input. See child_stream.hpp's class comment.
    //
    auto capture( const std::string & command) -> std::string
    {
        std::string  output;
        auto *       pipe = popen( command.c_str(), "r");

        if( !pipe)
        {
            return output;
        }

        char buffer[ 4096];

        while( std::fgets( buffer, sizeof( buffer), pipe))
        {
            output += buffer;
        }

        pclose( pipe);

        return output;
    }

    auto contains( const std::vector<std::string> & argv, const std::string & flag) -> bool
    {
        return std::find( argv.begin(), argv.end(), flag) != argv.end();
    }
} // namespace

// ---------------------------------------------------------------------------
// The JSON reader
// ---------------------------------------------------------------------------

TEST( Json, ReadsAWellFormedObject)
{
    const auto document = webui::Json::parse( R"({"a":1,"b":"x\ny","c":[true,false],"d":null})");

    ASSERT_NE( document, nullptr);

    EXPECT_EQ( document->at( "a")->number(), 1.0);
    EXPECT_EQ( document->textAt( "b"), "x\ny");
    EXPECT_EQ( document->at( "c")->items().size(), 2u);
    EXPECT_TRUE( document->at( "c")->items()[ 0]->boolean());
    EXPECT_EQ( document->at( "d")->type(), webui::Json::Type::Null);
}

TEST( Json, AnAbsentKeyFallsBackRatherThanFailing)
{
    const auto document = webui::Json::parse( R"({"a":1})");

    ASSERT_NE( document, nullptr);

    EXPECT_FALSE( document->has( "missing"));
    EXPECT_EQ( document->textAt( "missing", "fb"), "fb");
}

TEST( Json, DecodesTheControlByteEscapeTheProducerEmits)
{
    //
    // core::jsonEscape spells a control byte \u00XX and passes everything above
    // ASCII through untouched, so this is the only \u form that has to work.
    //
    const auto document = webui::Json::parse( R"("	")");

    ASSERT_NE( document, nullptr);
    EXPECT_EQ( document->text(), "\t");
}

TEST( Json, MalformedInputIsRefusedRatherThanGuessedAt)
{
    EXPECT_EQ( webui::Json::parse( R"({"a":1)"), nullptr) << "unterminated object";
    EXPECT_EQ( webui::Json::parse( R"({} {})"), nullptr) << "trailing content";
    EXPECT_EQ( webui::Json::parse( R"("a)"),    nullptr) << "unterminated string";
    EXPECT_EQ( webui::Json::parse( R"({"a":})"), nullptr) << "missing value";
}

// ---------------------------------------------------------------------------
// The option model, read back from the binary itself
// ---------------------------------------------------------------------------

class OptionModel : public ::testing::Test
{
    protected:
        void SetUp() override
        {
            auto parsed = webui::parseOptionModel( capture( kBinary + " --describe-options"));

            ASSERT_TRUE( parsed.has_value()) << "--describe-options did not parse";

            Model = std::move( *parsed);
        }

        auto find( const std::string & flag) const -> const webui::OptionInfo *
        {
            for( const auto & option : Model)
            {
                if( option.flag() == flag)
                {
                    return &option;
                }
            }

            return nullptr;
        }

        std::vector<webui::OptionInfo> Model;
};

TEST_F( OptionModel, EveryOptionIsUsable)
{
    ASSERT_FALSE( Model.empty());

    for( const auto & option : Model)
    {
        EXPECT_FALSE( option.Spellings.empty());
        EXPECT_FALSE( option.Help.empty())                     << option.flag() << " has no help";
        EXPECT_NE( option.Kind, webui::OptionKind::Unknown)       << option.flag() << " has an unknown kind";
    }
}

TEST_F( OptionModel, AListFlagIsDescribedAsOne)
{
    const auto * select = find( "--select");

    ASSERT_NE( select, nullptr);

    EXPECT_EQ( select->Kind, webui::OptionKind::List);
    EXPECT_FALSE( select->Repeatable) << "a test id cannot contain a comma, so --select is not repeatable";
    EXPECT_EQ( select->Placeholder, "ID[,ID...]");
}

TEST_F( OptionModel, AClearingSwitchSaysSo)
{
    const auto * noLogs = find( "--no-logs");

    ASSERT_NE( noLogs, nullptr);

    EXPECT_EQ( noLogs->Kind, webui::OptionKind::Switch);
    EXPECT_TRUE( noLogs->Clears) << "the form starts a clearing flag's box ticked";
}

TEST_F( OptionModel, APositiveNumberCarriesItsNoun)
{
    const auto * repeat = find( "--repeat");

    ASSERT_NE( repeat, nullptr);

    EXPECT_EQ( repeat->Kind, webui::OptionKind::Number);
    EXPECT_TRUE( repeat->Positive);
    EXPECT_EQ( repeat->Noun, "passes");
}

TEST_F( OptionModel, EverySpellingOfAFlagIsReported)
{
    const auto * colour = find( "--no-color");

    ASSERT_NE( colour, nullptr);
    EXPECT_EQ( colour->Spellings.size(), 2u) << "--no-color and --no-colour are one member";
}

TEST_F( OptionModel, AQueryFlagIsMarkedAndASafingFlagIsNot)
{
    //
    // The distinction cli::Query draws, and the reason the generated dialog can
    // leave --help out without carrying a list of names to skip: a query
    // describes the binary, --safe does something to the rig and very much
    // wants a button.
    //
    const auto * help = find( "--help");
    const auto * safe = find( "--safe");

    ASSERT_NE( help, nullptr);
    ASSERT_NE( safe, nullptr);

    EXPECT_TRUE( help->Query);
    EXPECT_FALSE( safe->Query);
}

// ---------------------------------------------------------------------------
// The catalog
// ---------------------------------------------------------------------------

TEST( Catalog, ListTestsYieldsUsableEntries)
{
    const auto tests = webui::parseTestList( capture( kBinary + " --list-tests"));

    ASSERT_FALSE( tests.empty());

    for( const auto & test : tests)
    {
        EXPECT_FALSE( test.Group.empty());
        EXPECT_FALSE( test.Id.empty());
    }
}

// ---------------------------------------------------------------------------
// The event stream, from a real run
// ---------------------------------------------------------------------------

class EventStream : public ::testing::Test
{
    protected:
        void SetUp() override
        {
            const auto skeleton = ( std::filesystem::temp_directory_path() / "thorium-ui-test.tsv").string();

            webui::EventStream reader;

            Events   = reader.consume( capture( kBinary + " --quiet --no-logs --events=- --skeleton=" + skeleton));
            Residue  = reader.residue();

            std::filesystem::remove( skeleton);
        }

        std::vector<webui::RunEvent>  Events;
        std::string                Residue;
};

TEST_F( EventStream, ARunProducesAWholeStream)
{
    ASSERT_FALSE( Events.empty());

    EXPECT_TRUE( Residue.empty()) << "a complete run leaves no partial line";
    EXPECT_EQ( Events.front().Which, webui::RunEvent::Kind::RunStart);
    EXPECT_EQ( Events.back().Which,  webui::RunEvent::Kind::RunEnd);
    EXPECT_TRUE( Events.back().Passed.has_value()) << "runEnd carries the run's verdict";
}

TEST_F( EventStream, EveryKindIsOneThisBuildKnows)
{
    for( const auto & event : Events)
    {
        EXPECT_NE( event.Which, webui::RunEvent::Kind::Unknown);
    }
}

TEST_F( EventStream, RunStartCarriesTheTraceabilityHeader)
{
    ASSERT_FALSE( Events.empty());

    const auto & header = Events.front().Header;

    EXPECT_FALSE( header.DutName.empty());
    EXPECT_FALSE( header.CriteriaVariant.empty());

    //
    // The single most important fact about a --skeleton run, and the reason
    // core::RunInfo states it rather than leaving it to be derived from the
    // command line: this is the difference between a green report about a DUT
    // and a green report about a file.
    //
    EXPECT_FALSE( header.BenchAttached) << "a --skeleton run touches no instrument";
}

TEST_F( EventStream, TestEndNamesItsGroupAndNotItsDescription)
{
    //
    // The regression this exists for: IJournalSink::onTestEnd is
    // (group, test, passed), not (test, description, passed) the way
    // onTestStart reads, and the first version of core::EventSink got it the
    // other way round -- producing a stream in which every test was named after
    // its group. It parsed perfectly.
    //
    std::string  openGroup;
    std::size_t  checked = 0;

    for( const auto & event : Events)
    {
        if( event.Which == webui::RunEvent::Kind::GroupStart)
        {
            openGroup = event.Group;
        }

        if( event.Which == webui::RunEvent::Kind::TestEnd)
        {
            EXPECT_EQ( event.Group, openGroup);
            EXPECT_TRUE( event.Passed.has_value());

            ++checked;
        }
    }

    EXPECT_GT( checked, 0u) << "the run ended no tests, so nothing was checked";
}

TEST_F( EventStream, AVerdictIsCarriedOnlyByAVerify)
{
    std::size_t verifies = 0;

    for( const auto & event : Events)
    {
        if( event.Which != webui::RunEvent::Kind::Event)
        {
            continue;
        }

        EXPECT_FALSE( event.Verb.empty());

        if( event.Verb == "Verify")
        {
            EXPECT_TRUE( event.Passed.has_value());

            ++verifies;
        }
        else if( event.Verb != "Note")
        {
            //
            // An unset Passed is not "false" -- it is an event with no pass/fail
            // notion at all, which is what stops an Apply being drawn as a
            // check that succeeded.
            //
            EXPECT_FALSE( event.Passed.has_value()) << event.Verb << " carried a verdict";
        }
    }

    EXPECT_GT( verifies, 0u);
}

// ---------------------------------------------------------------------------
// What a pipe actually delivers
// ---------------------------------------------------------------------------

TEST( EventStreaming, AByteAtATimeStreamYieldsWholeEvents)
{
    const auto whole = std::string(
        R"({"kind":"groupStart","group":"A"})"  "\n"
        R"({"kind":"testStart","test":"T"})"    "\n"
        R"({"kind":"runEnd","allPassed":true})" "\n");

    webui::EventStream            reader;
    std::vector<webui::RunEvent>  events;

    //
    // The worst case a pipe can hand a reader, and the one that finds an
    // off-by-one in the partial-line handling.
    //
    for( const char c : whole)
    {
        for( auto & event : reader.consume( std::string_view( &c, 1)))
        {
            events.push_back( std::move( event));
        }
    }

    ASSERT_EQ( events.size(), 3u);

    EXPECT_TRUE( reader.residue().empty());
    EXPECT_EQ( events.back().Passed, true);
}

TEST( EventStreaming, ARunKilledMidLineDeliversWhatWasCompleteAndHoldsTheRest)
{
    webui::EventStream reader;

    const auto events = reader.consume(
        R"({"kind":"testStart","test":"T"})" "\n" R"({"kind":"eve)");

    EXPECT_EQ( events.size(), 1u);
    EXPECT_FALSE( reader.residue().empty()) << "the fragment is held, not delivered";
}

TEST( EventStreaming, AWindowsLineEndingParses)
{
    webui::EventStream reader;

    const auto events = reader.consume( R"({"kind":"runEnd","allPassed":false})" "\r\n");

    ASSERT_EQ( events.size(), 1u);
    EXPECT_EQ( events[ 0].Passed, false);
}

// ---------------------------------------------------------------------------
// Building a command line
// ---------------------------------------------------------------------------

class RunCommand : public ::testing::Test
{
    protected:
        void SetUp() override { Suite.Binary = "/opt/thorium/bin/run_scripts"; }

        webui::Suite Suite;
};

TEST_F( RunCommand, TheStreamFlagsAreForcedAndTheBinaryComesFirst)
{
    const auto argv = webui::buildRunCommand( Suite, webui::RunRequest{});

    ASSERT_FALSE( argv.empty());

    EXPECT_EQ( argv.front(), Suite.Binary.string());
    EXPECT_TRUE( contains( argv, "--quiet"));
    EXPECT_TRUE( contains( argv, "--no-color"));
    EXPECT_TRUE( contains( argv, "--events=-"));
}

TEST_F( RunCommand, AnEmptySelectionIsAnAbsentFlagRatherThanEveryId)
{
    //
    // Not the same run the day a test is added to the catalog: an operator who
    // ticked "all" means all, and a window that had frozen today's list into a
    // --select would silently keep running yesterday's suite.
    //
    const auto argv = webui::buildRunCommand( Suite, webui::RunRequest{});

    EXPECT_TRUE( std::none_of( argv.begin(), argv.end(),
                               []( const std::string & arg) { return arg.starts_with( "--select"); }));
}

TEST_F( RunCommand, SettingsBecomeFlagsAndUntouchedControlsDoNot)
{
    webui::RunRequest request;

    request.Selection = { "SupplyRail", "AcDropout" };
    request.Settings  = {
        { .Flag = "--criteria",      .Value = "stress", .Present = true },
        { .Flag = "--no-logs",       .Value = "",       .Present = false },
        { .Flag = "--repeat",        .Value = "50",     .Present = true },
        { .Flag = "--until-failure", .Value = "",       .Present = true }
    };
    request.Extra = { "--address=Dmm1=lan:dev-dmm-3" };

    const auto argv = webui::buildRunCommand( Suite, request);

    EXPECT_TRUE( contains( argv, "--select=SupplyRail,AcDropout")) << "one comma-separated flag";
    EXPECT_TRUE( contains( argv, "--criteria=stress"));
    EXPECT_TRUE( contains( argv, "--repeat=50"));
    EXPECT_TRUE( contains( argv, "--until-failure")) << "a switch is its flag alone";
    EXPECT_FALSE( contains( argv, "--no-logs"))      << "an untouched switch contributes nothing";
    EXPECT_EQ( argv.back(), "--address=Dmm1=lan:dev-dmm-3") << "a hand-typed flag comes last";
}

TEST_F( RunCommand, SafingIsTheSameBinaryWithOneFlag)
{
    const auto safe = webui::buildSafeCommand( Suite);

    ASSERT_EQ( safe.size(), 2u);
    EXPECT_EQ( safe[ 1], "--safe");
}

// ---------------------------------------------------------------------------
// The manifest
// ---------------------------------------------------------------------------

TEST( Manifest, IsReadIntoASuite)
{
    const auto suite = webui::parseManifest( R"({
        "criteriaVariants": ["production", "stress", "aged"],
        "defaultCriteriaVariant": "production",
        "masterCriteriaVariant": "production",
        "binary": "run_scripts",
        "tests": [ { "group": "G", "id": "T", "description": "d" } ]
    })", "/opt/thorium/bin/manifest.json", "/opt");

    ASSERT_TRUE( suite.has_value());

    EXPECT_EQ( suite->CriteriaVariants.size(), 3u);
    EXPECT_EQ( suite->MasterCriteria, "production");
    EXPECT_EQ( suite->Tests.size(), 1u);

    //
    // Resolved against the manifest's own directory, never taken as absolute --
    // an install prefix has to survive being copied or mounted elsewhere.
    //
    EXPECT_EQ( suite->Binary, std::filesystem::path( "/opt/thorium/bin/run_scripts"));
}

TEST( Manifest, NamingNoBinaryIsRefused)
{
    EXPECT_FALSE( webui::parseManifest( R"({"tests":[]})", "/x/manifest.json").has_value());
}

TEST( Manifest, TwoSuitesUnderOneRootAreDistinguishable)
{
    //
    // The entire job of Suite::label(), and what the first version failed at --
    // it answered "bin" for both, because an install puts every suite in
    // <prefix>/bin.
    //
    const auto json  = R"({"binary":"run_scripts","tests":[]})";
    const auto left  = webui::parseManifest( json, "/opt/thorium/dut-a/bin/manifest.json", "/opt/thorium");
    const auto right = webui::parseManifest( json, "/opt/thorium/dut-b/bin/manifest.json", "/opt/thorium");

    ASSERT_TRUE( left.has_value());
    ASSERT_TRUE( right.has_value());

    EXPECT_EQ( left->label(),  "dut-a");
    EXPECT_EQ( right->label(), "dut-b");
}

TEST( Manifest, ASingleInstallIsNamedForItsPrefixRatherThanItsBindir)
{
    const auto suite = webui::parseManifest( R"({"binary":"run_scripts","tests":[]})",
                                          "/opt/thorium/bin/manifest.json", "/opt");

    ASSERT_TRUE( suite.has_value());
    EXPECT_EQ( suite->label(), "thorium");
}

// ---------------------------------------------------------------------------
// Where the logs went
// ---------------------------------------------------------------------------

TEST( LogFiles, ALoggedRunNamesBothLogsAndTheyExist)
{
    //
    // What the console's "Save log" rests on: run_scripts derives the file
    // names from --log-dir and the start time, and this is the only place a
    // watcher learns them. A replay of a skeleton rather than a real run,
    // because a skeleton run writes no logs and a test machine has no rig.
    //
    const auto dir    = std::filesystem::temp_directory_path() / "thorium-ui-logs-test";
    const auto replay = ( dir / "readings.tsv").string();

    std::filesystem::remove_all( dir);
    std::filesystem::create_directories( dir);

    capture( kBinary + " --quiet --no-logs --skeleton=" + replay);

    webui::EventStream reader;
    const auto events = reader.consume( capture( kBinary + " --quiet --events=- --replay=" + replay +
                                                  " --log-dir=" + ( dir / "logs").string()));

    ASSERT_FALSE( events.empty());
    ASSERT_EQ( events.front().Which, webui::RunEvent::Kind::RunStart);

    const auto & start = events.front();

    EXPECT_TRUE( std::filesystem::path( start.SarifLog).is_absolute());
    EXPECT_TRUE( std::filesystem::path( start.RtfLog).is_absolute());
    EXPECT_TRUE( std::filesystem::exists( start.SarifLog)) << start.SarifLog;
    EXPECT_TRUE( std::filesystem::exists( start.RtfLog))   << start.RtfLog;

    std::filesystem::remove_all( dir);
}

TEST_F( EventStream, AnUnloggedRunNamesNoLogs)
{
    ASSERT_FALSE( Events.empty());

    EXPECT_TRUE( Events.front().SarifLog.empty());
    EXPECT_TRUE( Events.front().RtfLog.empty());
}
