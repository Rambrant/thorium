//
// Tests over the protocol layer -- the half of framework/ui/ that has no toolkit in it.
//
// Black-box against real output, not against hand-written fixtures wherever
// that is possible: framework/ui/CMakeLists.txt points THORIUM_UI_TEST_BINARY at a built
// run_scripts, and the tests below drive it. A fixture asserting on a schema
// nobody produces would pass forever after the schema changed, which is the
// one failure this layer cannot afford -- it is the whole contract between two
// processes.
//
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "protocol/command.hpp"
#include "protocol/events.hpp"
#include "protocol/json.hpp"
#include "protocol/options.hpp"
#include "protocol/suite.hpp"

namespace
{
    int gFailures = 0;

    auto check( const bool condition, const std::string & what) -> void
    {
        if( !condition)
        {
            std::cerr << "FAILED: " << what << '\n';
            ++gFailures;
        }
    }

    //
    // Runs a command and returns its stdout. popen rather than anything
    // portable, because this is a test helper on a developer's machine -- the
    // real program uses wxProcess, which is where portability has to be
    // solved.
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
} // namespace

int main()
{
#ifndef THORIUM_UI_TEST_BINARY
#error "THORIUM_UI_TEST_BINARY must name a built run_scripts"
#endif

    const std::string binary = THORIUM_UI_TEST_BINARY;

    // --- The JSON reader ---------------------------------------------------
    {
        const auto document = ui::Json::parse( R"({"a":1,"b":"x\ny","c":[true,false],"d":null})");

        check( document != nullptr,                              "a well-formed object parses");
        check( document->at( "a")->number() == 1.0,              "a number reads back");
        check( document->textAt( "b") == "x\ny",                 "an escape is decoded");
        check( document->at( "c")->items().size() == 2,          "an array has its items");
        check( document->at( "c")->items()[ 0]->boolean(),       "a bool reads back");
        check( document->at( "d")->type() == ui::Json::Type::Null, "null is null");
        check( !document->has( "missing"),                       "an absent key is absent");
        check( document->textAt( "missing", "fb") == "fb",       "an absent key falls back");

        check( ui::Json::parse( R"({"a":1)")   == nullptr,       "an unterminated object fails");
        check( ui::Json::parse( R"({} {})")    == nullptr,       "trailing content fails");
        check( ui::Json::parse( R"("a)")       == nullptr,       "an unterminated string fails");
        check( ui::Json::parse( R"({"a":})")   == nullptr,       "a missing value fails");
        check( ui::Json::parse( R"("	")")->text() == "\t",  "a \\u00XX control byte decodes");
    }

    // --- The option model, from the binary itself --------------------------
    {
        const auto json  = capture( binary + " --describe-options");
        const auto model = ui::parseOptionModel( json);

        check( model.has_value(), "--describe-options parses");

        if( model)
        {
            bool sawSelect = false;
            bool sawNoLogs = false;
            bool sawRepeat = false;
            bool sawColour = false;
            bool sawHelp   = false;
            bool sawSafe   = false;

            for( const auto & option : *model)
            {
                check( !option.Spellings.empty(),  "every option has a spelling");
                check( !option.Help.empty(),       "every option has help: " + option.flag());
                check( option.Kind != ui::OptionKind::Unknown, "every kind is known: " + option.flag());

                if( option.flag() == "--select")
                {
                    sawSelect = true;
                    check( option.Kind == ui::OptionKind::List, "--select is a list");
                    check( !option.Repeatable,                  "--select is not repeatable");
                    check( option.Placeholder == "ID[,ID...]",  "--select has its placeholder");
                }

                if( option.flag() == "--no-logs")
                {
                    sawNoLogs = true;
                    check( option.Kind == ui::OptionKind::Switch, "--no-logs is a switch");
                    check( option.Clears,                        "--no-logs clears");
                }

                if( option.flag() == "--repeat")
                {
                    sawRepeat = true;
                    check( option.Kind == ui::OptionKind::Number, "--repeat is a number");
                    check( option.Positive,                       "--repeat is positive");
                    check( option.Noun == "passes",               "--repeat counts passes");
                }

                if( option.flag() == "--no-color")
                {
                    sawColour = true;
                    check( option.Spellings.size() == 2, "--no-color carries both spellings");
                }

                if( option.flag() == "--help")      { sawHelp = true;  check( option.Query,  "--help is a query"); }
                if( option.flag() == "--safe")      { sawSafe = true;  check( !option.Query, "--safe is NOT a query"); }
            }

            check( sawSelect && sawNoLogs && sawRepeat && sawColour && sawHelp && sawSafe,
                   "every flag the form relies on is described");
        }
    }

    // --- The catalog, from the binary itself --------------------------------
    {
        const auto tests = ui::parseTestList( capture( binary + " --list-tests"));

        check( !tests.empty(), "--list-tests yields tests");

        for( const auto & test : tests)
        {
            check( !test.Group.empty(), "every test has a group");
            check( !test.Id.empty(),    "every test has an id");
        }
    }

    // --- The event stream, from a real run ----------------------------------
    {
        const auto skeleton = ( std::filesystem::temp_directory_path() / "thorium-ui-test.tsv").string();
        const auto stream   = capture( binary + " --quiet --no-logs --events=- --skeleton=" + skeleton);

        ui::EventStream reader;

        const auto events = reader.consume( stream);

        check( !events.empty(),           "a run produces events");
        check( reader.residue().empty(),  "a complete run leaves no partial line");

        bool sawRunStart = false;
        bool sawRunEnd   = false;
        bool sawVerify   = false;

        std::string lastGroup;

        for( const auto & event : events)
        {
            check( event.Which != ui::RunEvent::Kind::Unknown, "every kind is known");

            switch( event.Which)
            {
                case ui::RunEvent::Kind::RunStart:
                    sawRunStart = true;
                    check( !event.Header.DutName.empty(),      "runStart names the DUT");
                    check( !event.Header.CriteriaVariant.empty(), "runStart names the variant");
                    check( !event.Header.BenchAttached,        "a --skeleton run reports no bench");
                    break;

                case ui::RunEvent::Kind::GroupStart:
                    lastGroup = event.Group;
                    check( !event.Group.empty(), "groupStart names its group");
                    break;

                case ui::RunEvent::Kind::TestEnd:
                    //
                    // The regression this exists for: onTestEnd is
                    // (group, test, passed), not (test, description, passed),
                    // and the first version of core::EventSink got it the other
                    // way round -- producing a stream in which every test was
                    // named after its group. It parsed perfectly.
                    //
                    check( event.Group == lastGroup,   "testEnd's group is the open group");
                    check( event.Passed.has_value(),   "testEnd carries a verdict");
                    break;

                case ui::RunEvent::Kind::Event:
                    check( !event.Verb.empty(), "an event names its verb");

                    if( event.Verb == "Verify")
                    {
                        sawVerify = true;
                        check( event.Passed.has_value(), "a Verify carries a verdict");
                    }
                    else if( event.Verb != "Note")
                    {
                        check( !event.Passed.has_value(), "a non-Verify carries no verdict: " + event.Verb);
                    }
                    break;

                case ui::RunEvent::Kind::RunEnd:
                    sawRunEnd = true;
                    check( event.Passed.has_value(), "runEnd carries the run's verdict");
                    break;

                default:
                    break;
            }
        }

        check( sawRunStart, "the stream opens with a runStart");
        check( sawRunEnd,   "the stream closes with a runEnd");
        check( sawVerify,   "the stream carries verdicts");

        std::filesystem::remove( skeleton);
    }

    // --- A chunked stream, which is what a pipe actually delivers ------------
    {
        const auto whole = std::string(
            R"({"kind":"groupStart","group":"A"})"  "\n"
            R"({"kind":"testStart","test":"T"})"    "\n"
            R"({"kind":"runEnd","allPassed":true})" "\n");

        ui::EventStream reader;

        std::vector<ui::RunEvent> events;

        //
        // One byte at a time -- the worst case a pipe can hand a reader, and
        // the one that finds an off-by-one in the partial-line handling.
        //
        for( const char c : whole)
        {
            for( auto & event : reader.consume( std::string_view( &c, 1)))
            {
                events.push_back( std::move( event));
            }
        }

        check( events.size() == 3,          "a byte-at-a-time stream yields whole events");
        check( reader.residue().empty(),    "nothing is left over");
        check( events.back().Passed == true, "the last event is the verdict");
    }

    // --- A run killed mid-line ----------------------------------------------
    {
        ui::EventStream reader;

        const auto events = reader.consume(
            R"({"kind":"testStart","test":"T"})" "\n" R"({"kind":"eve)");

        check( events.size() == 1,          "the complete line is delivered");
        check( !reader.residue().empty(),   "the fragment is held, not delivered");
    }

    // --- Windows line endings ------------------------------------------------
    {
        ui::EventStream reader;

        const auto events = reader.consume( R"({"kind":"runEnd","allPassed":false})" "\r\n");

        check( events.size() == 1,            "a \\r\\n line parses");
        check( events[ 0].Passed == false,    "and carries its verdict");
    }

    // --- Building a command --------------------------------------------------
    {
        ui::Suite suite;

        suite.Binary = "/opt/thorium/bin/run_scripts";

        ui::RunRequest request;

        request.Selection = { "SupplyRail", "AcDropout" };
        request.Settings  = {
            { .Flag = "--criteria",  .Value = "stress", .Present = true },
            { .Flag = "--no-logs",   .Value = "",       .Present = false },
            { .Flag = "--repeat",    .Value = "50",     .Present = true },
            { .Flag = "--until-failure", .Value = "",   .Present = true }
        };
        request.Extra = { "--address=Dmm1=lan:dev-dmm-3" };

        const auto argv = ui::buildRunCommand( suite, request);

        const auto has = [ &argv]( const std::string & flag)
        {
            return std::find( argv.begin(), argv.end(), flag) != argv.end();
        };

        check( argv.front() == suite.Binary.string(), "argv[0] is the binary");
        check( has( "--quiet"),                       "the stream flags are forced");
        check( has( "--no-color"),                    "colour is off");
        check( has( "--events=-"),                    "the stream goes to stdout");
        check( has( "--select=SupplyRail,AcDropout"), "the selection is one comma-separated flag");
        check( has( "--criteria=stress"),             "a value flag is flag=value");
        check( has( "--until-failure"),               "a switch is its flag alone");
        check( has( "--repeat=50"),                   "a number is flag=value");
        check( !has( "--no-logs"),                    "an untouched switch is absent");
        check( argv.back() == "--address=Dmm1=lan:dev-dmm-3", "a hand-typed flag comes last");

        //
        // An empty selection is an absent --select, not --select= with
        // everything in it. See RunRequest::Selection on why those are not the
        // same run.
        //
        const auto everything = ui::buildRunCommand( suite, ui::RunRequest{});

        check( std::none_of( everything.begin(), everything.end(),
                             []( const std::string & arg) { return arg.starts_with( "--select"); }),
               "no selection means no --select at all");

        const auto safe = ui::buildSafeCommand( suite);

        check( safe.size() == 2 && safe[ 1] == "--safe", "safing is the same binary with --safe");
    }

    // --- A manifest ----------------------------------------------------------
    {
        const auto suite = ui::parseManifest( R"({
            "criteriaVariants": ["production", "stress", "aged"],
            "defaultCriteriaVariant": "production",
            "masterCriteriaVariant": "production",
            "binary": "run_scripts",
            "tests": [ { "group": "G", "id": "T", "description": "d" } ]
        })", "/opt/thorium/bin/manifest.json", "/opt");

        check( suite.has_value(), "a manifest parses");

        if( suite)
        {
            check( suite->CriteriaVariants.size() == 3,     "every variant is listed");
            check( suite->MasterCriteria == "production",   "the master is reported");
            check( suite->Tests.size() == 1,                "the catalog snapshot is read");
            //
            // "thorium", not "bin". Every installed suite lives in <prefix>/bin,
            // so the immediate parent is the same word for all of them and
            // tells an operator nothing -- see Suite::label().
            //
            check( suite->label() == "thorium",             "the label is the prefix, not its bindir");

            //
            // Resolved against the manifest's directory, never taken as
            // absolute -- an install prefix has to survive being copied or
            // mounted somewhere else.
            //
            check( suite->Binary == std::filesystem::path( "/opt/thorium/bin/run_scripts"),
                   "the binary is resolved beside the manifest");
        }

        check( !ui::parseManifest( R"({"tests":[]})", "/x/manifest.json").has_value(),
               "a manifest naming no binary is refused");

        //
        // Two suites under one root must not come back with the same label,
        // which is the entire job of that function and what the first version
        // failed at -- it answered "bin" for both.
        //
        const auto json  = R"({"binary":"run_scripts","tests":[]})";
        const auto left  = ui::parseManifest( json, "/opt/thorium/dut-a/bin/manifest.json", "/opt/thorium");
        const auto right = ui::parseManifest( json, "/opt/thorium/dut-b/bin/manifest.json", "/opt/thorium");

        check( left && right,                          "two suites under one root parse");
        check( left->label() == "dut-a",               "the first is named for its own prefix");
        check( right->label() == "dut-b",              "and the second for its");
        check( left->label() != right->label(),        "two suites under one root are distinguishable");
    }

    if( gFailures == 0)
    {
        std::cout << "ui protocol: all checks passed\n";
    }

    return gFailures == 0 ? 0 : 1;
}
