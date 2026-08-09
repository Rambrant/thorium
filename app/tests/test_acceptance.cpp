#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

//
// POSIX, for reading a child's exit status out of what std::system() hands back
// (WIFEXITED/WEXITSTATUS). The one platform header in this codebase, and it
// earns its place: this file's entire job is to run another process, which is
// not something the standard library exposes an exit status for. Note the
// contrast with libs/core, which deliberately reads the operator and host from
// environment variables rather than calling gethostname() -- core is portable
// framework code with a choice, and this is a test whose subject is a command.
//
#include <sys/wait.h>

//
// Acceptance tests for the run_scripts binary: black-box, driven as a
// subprocess, the way a rig console or a CI job invokes it.
//
// ---------------------------------------------------------------------------
// What this covers that the other test binaries can't
// ---------------------------------------------------------------------------
// core_tests exercises the log sinks directly, with hand-built events, and
// hal_tests exercises the verbs that feed them. Between them that settles
// whether each piece is correct -- and says nothing about whether the *binary*
// wires them together: that the flags parse, that log files land where they
// were asked to, that --safe leaves no log behind, that the exit status agrees
// with what the log says happened. Those are properties of a command line, and
// the only honest way to test a command line is to run it.
//
// So the assertions here are deliberately shallow about *format* -- "the SARIF
// log carries this run's DUT serial", not "SARIF is well-formed", which
// core_tests already owns -- and deliberately strict about wiring.
//
// ---------------------------------------------------------------------------
// It is also meant to be read
// ---------------------------------------------------------------------------
// Every invocation is printed as it happens, tagged [  INVOKE  ], so running
// this binary -- or `ctest -V` -- is a worked document of how run_scripts is
// meant to be called:
//
//     ctest --test-dir <build-dir> -L acceptance -V
//     <build-dir>/app/acceptance_tests
//
// That is why the argument lists in each scenario below are written out
// literally rather than built up from shared constants or helper wrappers: the
// value of a line like `run_scripts --quiet --log-dir=artifacts/run-1` is that
// somebody can read it and then type it, which a scenario assembling its flags
// from variables would lose.
//
// ---------------------------------------------------------------------------
// Artifacts are kept, on purpose
// ---------------------------------------------------------------------------
// Every scenario runs in its own directory under
// <build>/app/acceptance/<suite>.<test>/ and everything it produced stays
// there after the run:
//
//     command.txt   the exact invocation, so the directory explains itself
//     stdout.txt    the live console view the run printed
//     stderr.txt
//     status.txt    the exit status
//     logs/         whatever run_scripts wrote (or an explicit path)
//
// That is the point rather than a side effect: when an assertion fails, the
// message names the file to go and read, and a passing run still leaves real
// specimens of both log formats to look at. Each directory is cleared at the
// start of its scenario, so nothing in there is ever a leftover from a previous
// build being mistaken for this one's output.
//
// ---------------------------------------------------------------------------
// One thing to know before reading the assertions
// ---------------------------------------------------------------------------
// A bare run of this suite FAILS today, and that is not what these tests are
// failing on. Nothing seeds the simulated instruments in the app path, so every
// rail reads 0 V against criteria expecting 5 V / 3.3 V / 12 V (see
// suite/scripts/*.cpp and dut/criteria_production.inc). The two FuseRegister
// criteria pass regardless -- their value is a hard-coded 0xF5 stand-in -- so a
// run produces both passing and failing checks, which is convenient: it
// exercises both renderings end to end.
//
// So nothing here asserts "a run passes". Where the verdict matters, the
// assertion is that the exit status agrees with the log's own allPassed, which
// is the actual contract and stays true whichever way the readings go once real
// hardware or seeded simulation data is behind them.
//
namespace
{
    //
    // Both supplied by app/CMakeLists.txt: the built binary's real path (via a
    // $<TARGET_FILE:...> generator expression, so this works under any
    // generator or build layout) and where to keep artifacts.
    //
    constexpr std::string_view kRunScripts = THORIUM_RUN_SCRIPTS_EXE;
    constexpr std::string_view kOutputRoot = THORIUM_ACCEPTANCE_OUTPUT_DIR;

    //
    // The same runner over a catalog that declares SETUP/TEARDOWN, which the
    // shipped suite deliberately does not -- see app/CMakeLists.txt and
    // app/tests/fixtures/.
    //
    constexpr std::string_view kRunScriptsHooked = THORIUM_RUN_SCRIPTS_HOOKED_EXE;

    auto readFile( const std::filesystem::path & path) -> std::string
    {
        std::ifstream in( path, std::ios::in | std::ios::binary);
        std::ostringstream contents;

        contents << in.rdbuf();

        return contents.str();
    }

    auto writeFile( const std::filesystem::path & path, const std::string & text) -> void
    {
        std::ofstream out( path, std::ios::out | std::ios::trunc);
        out << text;
    }

    //
    // Single-quoted for the shell, with embedded quotes handled. None of the
    // arguments below actually need it -- they are all plain flags -- but a
    // quoting helper that only works for the arguments you happened to try is
    // the kind of thing that breaks the first time a path has a space in it,
    // and the build directory's path is not this file's to choose.
    //
    auto shellQuoted( const std::string_view text) -> std::string
    {
        std::string result = "'";

        for( const char c : text)
        {
            if( c == '\'')
            {
                result += "'\\''";
            }
            else
            {
                result += c;
            }
        }

        return result + "'";
    }

    //
    // Substring assertions that name the artifact to go and read on failure --
    // the whole reason the files are kept. Written as AssertionResult rather
    // than reaching for gmock's HasSubstr: this way the failure message can
    // carry the path, which HasSubstr's cannot.
    //
    auto containsText( const std::filesystem::path & artifact, const std::string & text, const std::string_view needle) -> ::testing::AssertionResult
    {
        if( text.find( needle) != std::string::npos)
        {
            return ::testing::AssertionSuccess();
        }

        return ::testing::AssertionFailure()
            << "expected to find \"" << needle << "\"\n  in: " << artifact.string();
    }

    auto omitsText( const std::filesystem::path & artifact, const std::string & text, const std::string_view needle) -> ::testing::AssertionResult
    {
        if( text.find( needle) == std::string::npos)
        {
            return ::testing::AssertionSuccess();
        }

        return ::testing::AssertionFailure()
            << "unexpectedly found \"" << needle << "\"\n  in: " << artifact.string();
    }

    //
    // A cheap not-truncated check, not a parser: whether the machine log is
    // *valid* SARIF is core_tests' question (see test_sarif_sink.cpp, which
    // owns the format), and duplicating that here would be duplicating the
    // authority as well as the code. What this catches is the failure this
    // level can actually have -- a log that stopped halfway because the run
    // died before closing it out.
    //
    auto looksComplete( const std::string & json) -> bool
    {
        int  depth    = 0;
        bool inString = false;

        for( std::size_t i = 0; i < json.size(); ++i)
        {
            if( inString)
            {
                if( json[ i] == '\\')        ++i;
                else if( json[ i] == '"')    inString = false;

                continue;
            }

            switch( json[ i])
            {
                case '"':           inString = true; break;
                case '{': case '[': ++depth; break;
                case '}': case ']': --depth; break;
                default: break;
            }
        }

        return !json.empty() && depth == 0 && !inString;
    }

    class Acceptance : public ::testing::Test
    {
        protected:
            auto SetUp() -> void override
            {
                const auto * info = ::testing::UnitTest::GetInstance()->current_test_info();

                mDir = std::filesystem::path( kOutputRoot) /
                       ( std::string( info->test_suite_name()) + "." + info->name());

                // Cleared, not merely created: a stale artifact from a previous
                // build read as this run's output is worse than no artifact.
                std::filesystem::remove_all( mDir);
                std::filesystem::create_directories( mDir);

                ASSERT_TRUE( std::filesystem::exists( kRunScripts))
                    << "run_scripts not built: " << kRunScripts;
            }

            //
            // Runs run_scripts in this scenario's directory, capturing stdout,
            // stderr and the exit status to disk. Returns the exit status.
            //
            // Via the shell rather than fork/exec: the redirections are the
            // point (they are what leaves the transcripts behind for
            // inspection), and a test that runs a command line should run it
            // the way a caller would type it.
            //
            auto run( const std::vector<std::string> & args) -> int
            {
                return runBinary( kRunScripts, args, {});
            }

            //
            // The hook fixture's runner (see app/CMakeLists.txt), with the
            // environment variables its scripts and hooks read to decide
            // whether to fail or throw. Same main.cpp as run() drives -- only
            // the catalog behind it differs.
            //
            auto runHooked( const std::vector<std::string> & args,
                            const std::vector<std::string> & environment = {}) -> int
            {
                return runBinary( kRunScriptsHooked, args, environment);
            }

            //
            // The markers the hook fixture wrote, in the order it wrote them --
            // "setup", "script", "teardown". A hook posts no journal event, so
            // stdout is the only place its ordering is visible at all.
            //
            [[nodiscard]]
            auto hookOrder() const -> std::vector<std::string>
            {
                constexpr std::string_view prefix = "HOOKFIXTURE ";

                std::vector<std::string> markers;
                std::istringstream       lines( mOut);

                for( std::string line; std::getline( lines, line); )
                {
                    if( const auto at = line.find( prefix); at != std::string::npos)
                    {
                        markers.push_back( line.substr( at + prefix.size()));
                    }
                }

                return markers;
            }

            auto runBinary( const std::string_view           exe,
                            const std::vector<std::string> & args,
                            const std::vector<std::string> & environment) -> int
            {
                //
                // Two spellings of the same invocation, because they are for
                // different readers:
                //
                //   invocation -- the binary's full path, written to
                //                 command.txt, so the artifact directory holds
                //                 something that can actually be re-run
                //   readable   -- just "run_scripts ...", printed below, where
                //                 an absolute build path is noise
                //
                std::string invocation( exe);
                std::string readable = std::filesystem::path( exe).filename().string();
                std::string command  = "cd " + shellQuoted( mDir.string()) + " && ";

                //
                // Prefixed onto the command rather than set in this process:
                // the variables belong to the run being tested, and a scenario
                // that leaked one into the test binary's own environment would
                // change every scenario after it.
                //
                for( const auto & variable : environment)
                {
                    invocation = variable + " " + invocation;
                    readable   = variable + " " + readable;
                    command   += variable + " ";
                }

                command += shellQuoted( exe);

                for( const auto & arg : args)
                {
                    invocation += " " + arg;
                    readable   += " " + arg;
                    command    += " " + shellQuoted( arg);
                }

                command += " > stdout.txt 2> stderr.txt";

                //
                // Printed, not merely recorded: it makes running this binary --
                // or `ctest -V` -- a worked document of how run_scripts is meant
                // to be called, which is half of what an acceptance test is for.
                // A scenario that invokes it twice (see
                // AcceptanceArguments.ColourIsOnByDefaultAndOffOnRequest) prints
                // both lines, in order.
                //
                // Tagged to the same 12-column width GoogleTest uses for
                // [ RUN      ] / [       OK ], so the invocation reads as part
                // of the same output rather than as something that escaped into
                // it.
                //
                std::cout << "[  INVOKE  ] " << readable << std::endl;

                // Recorded first, so the directory explains itself even if the
                // invocation goes on to hang or crash.
                writeFile( mDir / "command.txt", invocation + "\n");

                const int raw = std::system( command.c_str());

                mStatus = ( raw != -1 && WIFEXITED( raw)) ? WEXITSTATUS( raw) : -1;

                writeFile( mDir / "status.txt", std::to_string( mStatus) + "\n");

                mOut = readFile( mDir / "stdout.txt");
                mErr = readFile( mDir / "stderr.txt");

                return mStatus;
            }

            //
            // The single file with this extension anywhere under the scenario
            // directory, or an empty path. Recursive, so it finds both the
            // default logs/ subdirectory and an explicit nested --sarif= path
            // without the caller having to say which it expects.
            //
            [[nodiscard]]
            auto findArtifact( const std::string_view extension) const -> std::filesystem::path
            {
                for( const auto & entry : std::filesystem::recursive_directory_iterator( mDir))
                {
                    if( entry.is_regular_file() && entry.path().extension() == extension)
                    {
                        return entry.path();
                    }
                }

                return {};
            }

            [[nodiscard]]
            auto countArtifacts( const std::string_view extension) const -> std::size_t
            {
                std::size_t found = 0;

                for( const auto & entry : std::filesystem::recursive_directory_iterator( mDir))
                {
                    found += ( entry.is_regular_file() && entry.path().extension() == extension) ? 1 : 0;
                }

                return found;
            }

            [[nodiscard]] auto outPath() const -> std::filesystem::path { return mDir / "stdout.txt"; }
            [[nodiscard]] auto errPath() const -> std::filesystem::path { return mDir / "stderr.txt"; }

            std::filesystem::path  mDir;
            std::string            mOut;
            std::string            mErr;
            int                    mStatus{ -1 };
    };

    //
    // One fixture alias per concern, so each group of scenarios gets its own
    // ctest suite name to filter on (`ctest -R AcceptanceMachineLog`) and its
    // own artifact directories.
    //
    struct AcceptanceCatalog   : Acceptance {};
    struct AcceptanceSafing    : Acceptance {};
    struct AcceptanceArguments : Acceptance {};
    struct AcceptanceSelection : Acceptance {};
    struct AcceptanceHumanLog  : Acceptance {};
    struct AcceptanceMachineLog: Acceptance {};
    struct AcceptanceLogFiles  : Acceptance {};
    struct AcceptanceRepeat    : Acceptance {};
    struct AcceptanceHooks     : Acceptance {};

    //
    // The test ids the console reported a verdict for, in the order it
    // reported them -- which is the only way to see from outside whether
    // --repeat repeated the selection or repeated each script. Read off the
    // "RESULT <id>" lines core::ConsoleSink writes per test.
    //
    [[nodiscard]]
    auto verdictOrder( const std::string & console) -> std::vector<std::string>
    {
        std::vector<std::string> ids;
        std::istringstream       lines( console);

        for( std::string line; std::getline( lines, line); )
        {
            const auto marker = line.find( "RESULT");

            if( marker == std::string::npos)
            {
                continue;
            }

            std::istringstream rest( line.substr( marker + std::string_view( "RESULT").size()));

            if( std::string id; rest >> id)
            {
                ids.push_back( id);
            }
        }

        return ids;
    }
} // namespace

// ---------------------------------------------------------------------------
// Catalog query: run_scripts --list-tests
// ---------------------------------------------------------------------------

//
// The query a supervising console uses to build its picker (see
// tools/run-tests.sh, which parses exactly this). Machine-readable, and
// deliberately log-free: nothing ran, so there is nothing to record.
//
TEST_F( AcceptanceCatalog, ListTestsPrintsOneLinePerTestAndWritesNoLog)
{
    EXPECT_EQ( run( { "--list-tests" }), 0);

    EXPECT_TRUE( containsText( outPath(), mOut, "OutputVoltage|SupplyRail|Verify supply rail"));
    EXPECT_TRUE( containsText( outPath(), mOut, "OutputVoltage|FuseRegister|"));

    // Exactly one line per catalog test, nothing else on stdout.
    EXPECT_EQ( std::count( mOut.begin(), mOut.end(), '\n'), 2);

    EXPECT_FALSE( std::filesystem::exists( mDir / "logs"));
}

// ---------------------------------------------------------------------------
// Safing: run_scripts --safe
// ---------------------------------------------------------------------------

//
// What the rig console re-invokes after an abnormal child exit (see
// app/src/main.cpp and hal/safing.hpp). Exits 0 unconditionally, and writes no
// log: this process is cleaning up after a *different* run, and the log it
// would append to belongs to that dead run.
//
TEST_F( AcceptanceSafing, SafeExitsCleanlyAndWritesNoLog)
{
    EXPECT_EQ( run( { "--safe" }), 0);

    EXPECT_TRUE( containsText( outPath(), mOut, "Rig safed"));
    EXPECT_FALSE( std::filesystem::exists( mDir / "logs"));
}

// ---------------------------------------------------------------------------
// Argument handling
// ---------------------------------------------------------------------------

//
// A mistyped flag has to be a hard failure -- silently ignored, it means a run
// that didn't do what was asked.
//
TEST_F( AcceptanceArguments, UnknownFlagIsAHardFailureThatTouchesNothing)
{
    EXPECT_EQ( run( { "--no-such-flag" }), 1);

    EXPECT_TRUE( containsText( errPath(), mErr, "Unknown argument: --no-such-flag"));
    EXPECT_FALSE( std::filesystem::exists( mDir / "logs"));
}

//
// Colour is the caller's decision, not something detected from whether stdout
// is a terminal (see core::ConsoleSink). Both halves of this run redirected to
// a file, so a detection-based implementation would fail the first one.
//
TEST_F( AcceptanceArguments, ColourIsOnByDefaultAndOffOnRequest)
{
    run( { "--no-logs" });
    EXPECT_TRUE( containsText( outPath(), mOut, "\033[")) << "expected ANSI escapes even when not a terminal";

    run( { "--no-logs", "--no-color" });
    EXPECT_TRUE( omitsText( outPath(), mOut, "\033["));
}

//
// --quiet drops the live console view; the logs are unaffected, since they are
// the record and the console is a convenience on top of it. --no-logs is the
// inverse: watch it happen, record nothing.
//
TEST_F( AcceptanceArguments, QuietSuppressesTheConsoleButNotTheLogs)
{
    run( { "--quiet" });

    EXPECT_TRUE( mOut.empty()) << "expected no stdout, got:\n" << mOut;
    EXPECT_FALSE( findArtifact( ".sarif").empty());
    EXPECT_FALSE( findArtifact( ".rtf").empty());
}

TEST_F( AcceptanceArguments, NoLogsSuppressesTheLogsButNotTheConsole)
{
    run( { "--no-logs", "--no-color" });

    EXPECT_TRUE( containsText( outPath(), mOut, "OutputVoltage"));
    EXPECT_FALSE( std::filesystem::exists( mDir / "logs"));
}

// ---------------------------------------------------------------------------
// Test selection
// ---------------------------------------------------------------------------

TEST_F( AcceptanceSelection, SelectRunsOnlyTheNamedTest)
{
    run( { "--select=SupplyRail", "--no-color" });

    EXPECT_TRUE( containsText( outPath(), mOut, "\tSupplyRail"));
    EXPECT_TRUE( omitsText( outPath(), mOut, "FuseRegister"));

    const auto sarif = findArtifact( ".sarif");

    ASSERT_FALSE( sarif.empty());

    const auto log = readFile( sarif);

    EXPECT_TRUE( containsText( sarif, log, "select=SupplyRail"));
    EXPECT_TRUE( omitsText( sarif, log, "FuseRegister"));
}

//
// A typo in --select means that test doesn't run -- not a crash, and not a
// silently-empty pass. (tools/run-tests.sh validates ids before it gets here,
// but a caller typing the flag by hand has no such help.)
//
TEST_F( AcceptanceSelection, SelectingNothingIsReportedRatherThanPassingVacuously)
{
    EXPECT_EQ( run( { "--select=NoSuchTest" }), 1);

    EXPECT_TRUE( containsText( errPath(), mErr, "No catalog test matched"));
}

// ---------------------------------------------------------------------------
// The human-readable stream
// ---------------------------------------------------------------------------

TEST_F( AcceptanceHumanLog, ConsoleCarriesTheHeaderTestNamesAndVerdicts)
{
    run( { "--dut-serial=SN-000123", "--operator=acceptance", "--no-color" });

    //
    // Traceability header. The title names the DUT and the local time; the rows
    // under it are the report's fixed schedule -- serial, operator, criteria,
    // framework, content revision, UTC instant, command line.
    //
    EXPECT_TRUE( containsText( outPath(), mOut, "DeviceX -- "));
    EXPECT_TRUE( containsText( outPath(), mOut, "DUT serial        SN-000123"));
    EXPECT_TRUE( containsText( outPath(), mOut, "Operator          acceptance"));
    EXPECT_TRUE( containsText( outPath(), mOut, "Criteria"));
    EXPECT_TRUE( containsText( outPath(), mOut, "Framework         Thorium"));
    EXPECT_TRUE( containsText( outPath(), mOut, "Started (UTC)"));
    EXPECT_TRUE( containsText( outPath(), mOut, "Command line"));

    //
    // The content revision -- whichever shape it takes. One row when suite/,
    // dut/ and rig/ share a git revision (this repo), three when they diverge;
    // asserting on the label rather than the value keeps this from depending on
    // what the working tree happens to be at.
    //
    EXPECT_TRUE( containsText( outPath(), mOut, "Suite/DUT/rig") ||
                 containsText( outPath(), mOut, "Suite version"));

    // The test name marked out on its own, and its verdict restated after its
    // checks -- what a reader scanning a multi-test log is looking for.
    // The group states itself once, unindented; its tests nest under it.
    EXPECT_TRUE( containsText( outPath(), mOut, "OutputVoltage Tests validating DUT output voltage rails"));
    EXPECT_TRUE( containsText( outPath(), mOut, "\tSupplyRail Verify supply rail voltages"));
    EXPECT_TRUE( containsText( outPath(), mOut, "RESULT"));

    //
    // Each check states what was required next to what was measured -- the
    // criterion's own predicate, rendered (see core/predicate_text.hpp), not
    // just the prose from its CRIT entry.
    //
    EXPECT_TRUE( containsText( outPath(), mOut, "FS_Supply_1::FS_Supply_5V0"));
    EXPECT_TRUE( containsText( outPath(), mOut, "= 5 V +/-0.05 V"));
    EXPECT_TRUE( containsText( outPath(), mOut, "(value & 0xF) == 0x5"));

    // Measurements and both check outcomes.
    EXPECT_TRUE( containsText( outPath(), mOut, "measure Output5V"));
    EXPECT_TRUE( containsText( outPath(), mOut, "[PASS]"));
    EXPECT_TRUE( containsText( outPath(), mOut, "[FAIL]"));

    // Measure and Verify only -- the sourcing/routing verbs and the safing pass
    // go to the machine log (see core/report.hpp).
    EXPECT_TRUE( omitsText( outPath(), mOut, "Safe"));
}

TEST_F( AcceptanceHumanLog, RtfFileIsAColourCodedCompleteDocument)
{
    run( { "--quiet" });

    const auto rtf = findArtifact( ".rtf");

    ASSERT_FALSE( rtf.empty());

    const auto document = readFile( rtf);

    // A finished RTF document. Per-*event* validity -- the property that makes
    // the file readable mid-run -- can't be observed from out here against a
    // run this fast; core_tests' RtfSinkTest asserts it directly instead.
    EXPECT_TRUE( document.starts_with( "{\\rtf1")) << rtf.string();
    EXPECT_TRUE( document.ends_with( "}")) << rtf.string();

    EXPECT_TRUE( containsText( rtf, document, "colortbl"));
    EXPECT_TRUE( containsText( rtf, document, "\\cf2"));   // green -- a passing check
    EXPECT_TRUE( containsText( rtf, document, "\\cf3"));   // red   -- a failing one
    EXPECT_TRUE( containsText( rtf, document, "OutputVoltage"));
    EXPECT_TRUE( containsText( rtf, document, "SupplyRail"));
}

// ---------------------------------------------------------------------------
// The machine-readable stream
// ---------------------------------------------------------------------------

TEST_F( AcceptanceMachineLog, SarifCarriesThisRunsTraceabilityFacts)
{
    run( { "--quiet", "--dut-serial=SN-000123", "--operator=acceptance" });

    const auto sarif = findArtifact( ".sarif");

    ASSERT_FALSE( sarif.empty());

    const auto log = readFile( sarif);

    EXPECT_TRUE( looksComplete( log)) << "truncated machine log: " << sarif.string();

    EXPECT_TRUE( containsText( sarif, log, "sarif-schema-2.1.0"));
    EXPECT_TRUE( containsText( sarif, log, R"("name": "Thorium")"));
    EXPECT_TRUE( containsText( sarif, log, R"("dutSerial": "SN-000123")"));
    EXPECT_TRUE( containsText( sarif, log, R"("operator": "acceptance")"));
    EXPECT_TRUE( containsText( sarif, log, R"("criteriaVariant")"));
    EXPECT_TRUE( containsText( sarif, log, R"("commandLine")"));

    // What makes two runs comparable: same DUT, same tolerance table.
    EXPECT_TRUE( containsText( sarif, log, R"("id": "thorium/DeviceX/)"));
}

TEST_F( AcceptanceMachineLog, SarifCarriesEveryVerbAndKeysCriteriaByGroupAndId)
{
    run( { "--quiet" });

    const auto sarif = findArtifact( ".sarif");

    ASSERT_FALSE( sarif.empty());

    const auto log = readFile( sarif);

    // Criteria become rules keyed by their own CRITERIA group and CRIT id --
    // the pair a test spec traces to.
    EXPECT_TRUE( containsText( sarif, log, "FS_Supply_1/FS_Supply_5V0"));
    EXPECT_TRUE( containsText( sarif, log, R"("kind": "pass")"));
    EXPECT_TRUE( containsText( sarif, log, R"("kind": "fail")"));
    EXPECT_TRUE( containsText( sarif, log, R"("level": "error")"));

    // Every verb, unlike the human stream -- a routing step omitted for brevity
    // is exactly the step that explains a failed reading.
    EXPECT_TRUE( containsText( sarif, log, "Thorium/Measure"));
    EXPECT_TRUE( containsText( sarif, log, "Thorium/Safe"));

    // The bare number alongside the formatted value, so a consumer can compare
    // against a limit without re-parsing "5.021 V".
    EXPECT_TRUE( containsText( sarif, log, R"("numericValue")"));

    // And the tolerance each check enforced, spelled out.
    EXPECT_TRUE( containsText( sarif, log, R"("criterion")"));
}

// ---------------------------------------------------------------------------
// Where the logs land
// ---------------------------------------------------------------------------

TEST_F( AcceptanceLogFiles, BothLogsShareOneRunsNameUnderTheDefaultDirectory)
{
    run( { "--quiet" });

    const auto sarif = findArtifact( ".sarif");
    const auto rtf   = findArtifact( ".rtf");

    ASSERT_FALSE( sarif.empty());
    ASSERT_FALSE( rtf.empty());

    EXPECT_EQ( sarif.parent_path().filename(), "logs");
    EXPECT_EQ( rtf.parent_path().filename(),   "logs");

    // Named from the run's start time, so the two pair up in a directory of
    // them and sort chronologically.
    EXPECT_EQ( sarif.stem(), rtf.stem());
}

//
// The exit status has to agree with what the log says happened -- see this
// file's own comment on why the contract is stated this way rather than as "a
// run passes".
//
TEST_F( AcceptanceLogFiles, ExitStatusAgreesWithTheLogsOwnVerdict)
{
    const int status = run( { "--quiet" });

    const auto sarif = findArtifact( ".sarif");

    ASSERT_FALSE( sarif.empty());

    const bool allPassed = readFile( sarif).find( R"("allPassed": true)") != std::string::npos;

    EXPECT_EQ( status, allPassed ? 0 : 1);
}

//
// "The directory I named doesn't exist yet" is not a reason to refuse to log.
//
TEST_F( AcceptanceLogFiles, ExplicitPathsAreCreatedIncludingMissingDirectories)
{
    run( { "--quiet", "--sarif=reports/nested/run.sarif", "--rtf=reports/nested/run.rtf" });

    EXPECT_TRUE( std::filesystem::exists( mDir / "reports/nested/run.sarif"));
    EXPECT_TRUE( std::filesystem::exists( mDir / "reports/nested/run.rtf"));

    // And nothing under the default directory, since neither log defaulted.
    EXPECT_FALSE( std::filesystem::exists( mDir / "logs"));
}

TEST_F( AcceptanceLogFiles, LogDirMovesBothDefaultNamedLogsTogether)
{
    run( { "--quiet", "--log-dir=artifacts/run-1" });

    EXPECT_EQ( findArtifact( ".sarif").parent_path(), mDir / "artifacts/run-1");
    EXPECT_EQ( findArtifact( ".rtf").parent_path(),   mDir / "artifacts/run-1");
}

//
// A log named after its run's start time is only useful if the name is actually
// distinct -- two runs into one directory must not overwrite each other.
//
TEST_F( AcceptanceLogFiles, TwoRunsIntoOneDirectoryCoexist)
{
    run( { "--quiet", "--select=SupplyRail" });

    //
    // The stamp is second-resolution (see fileStamp in app/src/main.cpp), so
    // two runs inside the same second would legitimately collide. Sleeping is
    // the honest way to test the property that actually matters -- that a
    // *later* run doesn't clobber an earlier one -- rather than asserting a
    // uniqueness guarantee the naming scheme doesn't make.
    //
    std::system( "sleep 1");

    run( { "--quiet", "--select=SupplyRail" });

    EXPECT_EQ( countArtifacts( ".sarif"), 2u);
    EXPECT_EQ( countArtifacts( ".rtf"),   2u);
}

// ---------------------------------------------------------------------------
// Repeating a run: --repeat / --until-failure
// ---------------------------------------------------------------------------

//
// The property the flag is named for, and the one worth pinning from outside:
// the *selection* is what repeats. Two tests over two passes must come back
// A B A B, not A A B B -- which is what makes a pass a meaningful unit for
// SETUP/TEARDOWN to bracket and for --until-failure to stop at.
//
TEST_F( AcceptanceRepeat, RepeatRunsTheWholeSelectionEachPassRatherThanEachScript)
{
    run( { "--repeat=2", "--no-color" });

    EXPECT_EQ( verdictOrder( mOut),
        ( std::vector<std::string>{ "FuseRegister", "SupplyRail", "FuseRegister", "SupplyRail" }));
}

TEST_F( AcceptanceRepeat, RepeatAppliesToASelectionToo)
{
    run( { "--select=SupplyRail", "--repeat=3", "--no-color" });

    EXPECT_EQ( verdictOrder( mOut),
        ( std::vector<std::string>{ "SupplyRail", "SupplyRail", "SupplyRail" }));
}

//
// Passes are marked in the machine log -- without it a repeated run's log is
// the same headings over and over with nothing saying which time round it is.
//
TEST_F( AcceptanceRepeat, EachPassIsMarkedInTheMachineLog)
{
    run( { "--repeat=3", "--quiet" });

    const auto sarif = findArtifact( ".sarif");

    ASSERT_FALSE( sarif.empty());

    const auto log = readFile( sarif);

    EXPECT_TRUE( containsText( sarif, log, "pass 1 of 3"));
    EXPECT_TRUE( containsText( sarif, log, "pass 2 of 3"));
    EXPECT_TRUE( containsText( sarif, log, "pass 3 of 3"));
}

//
// An ordinary single run is unchanged by any of this -- no pass markers, no
// extra events.
//
TEST_F( AcceptanceRepeat, ASingleRunIsNotMarkedWithPasses)
{
    run( { "--quiet" });

    const auto sarif = findArtifact( ".sarif");

    ASSERT_FALSE( sarif.empty());

    EXPECT_TRUE( omitsText( sarif, readFile( sarif), "pass 1"));
}

//
// --until-failure caps a --repeat. Every script fails on a rig with no
// hardware attached (the instruments read zero), so the first pass is the
// failing one and passes two onwards must never run.
//
TEST_F( AcceptanceRepeat, UntilFailureStopsAtTheFirstFailingPass)
{
    EXPECT_EQ( run( { "--repeat=4", "--until-failure", "--quiet" }), 1);

    EXPECT_TRUE( containsText( errPath(), mErr, "Stopping after failing pass 1"));

    const auto sarif = findArtifact( ".sarif");

    ASSERT_FALSE( sarif.empty());

    const auto log = readFile( sarif);

    EXPECT_TRUE( containsText( sarif, log, "pass 1 of 4"));
    EXPECT_TRUE( omitsText(    sarif, log, "pass 2 of 4"));
}

//
// A pass count that isn't one is a caller error, not something to reinterpret
// -- the same treatment an unknown flag gets, and for the same reason.
//
TEST_F( AcceptanceRepeat, ARepeatCountThatIsNotAPositiveNumberIsRejectedBeforeAnythingRuns)
{
    for( const auto bad : { "--repeat=0", "--repeat=-1", "--repeat=ten", "--repeat=3x" })
    {
        EXPECT_EQ( run( { bad }), 1) << bad;
        EXPECT_TRUE( containsText( errPath(), mErr, "--repeat=")) << bad;
    }

    EXPECT_EQ( countArtifacts( ".sarif"), 0u);
    EXPECT_EQ( countArtifacts( ".rtf"),   0u);
}

// ---------------------------------------------------------------------------
// Bracketing a run: SETUP / TEARDOWN
// ---------------------------------------------------------------------------
//
// Driven against run_scripts_hooked -- the same main.cpp over a catalog that
// declares both hooks (see app/CMakeLists.txt). The shipped suite declares
// neither, which is itself covered: every other scenario in this file runs a
// catalog with no hooks and none of them see one fire.
//

TEST_F( AcceptanceHooks, SetupRunsBeforeTheScriptsAndTeardownAfterThem)
{
    EXPECT_EQ( runHooked( { "--no-color" }), 0);

    EXPECT_EQ( hookOrder(), ( std::vector<std::string>{ "setup", "script", "teardown" }));
}

//
// The property that makes a pass a meaningful unit: the hooks bracket the
// selection *once*, not once per repetition. A rig is powered up, the scripts
// run three times, and it is powered down -- not powered up and down three
// times over.
//
TEST_F( AcceptanceHooks, TheHooksBracketEveryRepeatPassRatherThanEachOne)
{
    EXPECT_EQ( runHooked( { "--repeat=3", "--no-color" }), 0);

    EXPECT_EQ( hookOrder(),
        ( std::vector<std::string>{ "setup", "script", "script", "script", "teardown" }));
}

//
// A setup that fails means no test runs at all -- but teardown still does,
// since a setup that got half way is exactly the case with something to undo.
//
TEST_F( AcceptanceHooks, AFailingSetupStopsTheRunButStillTearsDown)
{
    EXPECT_EQ( runHooked( { "--no-color" }, { "THORIUM_FIXTURE_SETUP_FAILS=1" }), 1);

    EXPECT_EQ( hookOrder(), ( std::vector<std::string>{ "setup", "teardown" }));

    EXPECT_TRUE( containsText( errPath(), mErr, "SETUP reported failure"));
}

//
// Teardown is a guard destructor, so it runs on the way out of a script that
// threw just as it does on the ordinary path -- the case it most needs to
// cover, since that is when the rig is in an unknown state.
//
TEST_F( AcceptanceHooks, TeardownStillRunsWhenAScriptThrows)
{
    EXPECT_EQ( runHooked( { "--no-color" }, { "THORIUM_FIXTURE_SCRIPT_THROWS=1" }), 1);

    EXPECT_EQ( hookOrder(), ( std::vector<std::string>{ "setup", "script", "teardown" }));

    EXPECT_TRUE( containsText( errPath(), mErr, "Uncaught exception during test run"));
}

//
// A rig that did not shut down the way the suite says it should is not a clean
// run, however well the scripts themselves went.
//
TEST_F( AcceptanceHooks, AFailingTeardownFailsAnOtherwisePassingRun)
{
    EXPECT_EQ( runHooked( { "--no-color" }, { "THORIUM_FIXTURE_TEARDOWN_FAILS=1" }), 1);

    EXPECT_EQ( hookOrder(), ( std::vector<std::string>{ "setup", "script", "teardown" }));

    EXPECT_TRUE( containsText( errPath(), mErr, "TEARDOWN reported failure"));
}

//
// --until-failure stops the passes; it must not skip the teardown on the way
// out.
//
TEST_F( AcceptanceHooks, StoppingEarlyOnFailureStillTearsDown)
{
    EXPECT_EQ( runHooked( { "--repeat=5", "--until-failure", "--no-color" },
                          { "THORIUM_FIXTURE_SCRIPT_FAILS=1" }), 1);

    EXPECT_EQ( hookOrder(), ( std::vector<std::string>{ "setup", "script", "teardown" }));
}

//
// A selection matching nothing is reported without powering anything up: there
// is no run to bracket.
//
TEST_F( AcceptanceHooks, ASelectionMatchingNothingNeverReachesTheHooks)
{
    EXPECT_EQ( runHooked( { "--select=NoSuchTest" }), 1);

    EXPECT_TRUE( hookOrder().empty());
    EXPECT_TRUE( containsText( errPath(), mErr, "No catalog test matched"));
}
