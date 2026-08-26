#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <ranges>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

//
// The one platform-dependent file in this codebase, and it earns it: this
// file's entire job is to run another process and read what came back, which
// is not something the standard library exposes a portable exit status or
// quoting rule for. Note the contrast with framework/core, which deliberately reads
// the operator and host from environment variables rather than calling
// gethostname() -- core is portable framework code with a choice, and this is a
// test whose subject is a command line.
//
// Everything platform-specific is confined to this header block and to
// shellQuoted()/runBinary() below. The scenarios themselves are written once
// and say nothing about which shell is going to run them.
//
#ifndef _WIN32
    //
    // POSIX, for reading a child's exit status out of what std::system() hands
    // back (WIFEXITED/WEXITSTATUS). Windows has no equivalent because it needs
    // none -- see runBinary() on what std::system() returns there.
    //
    #include <sys/wait.h>
#endif

//
// Acceptance tests for the run_scripts binary: black-box, driven as a
// subprocess, the way a rig console or a CI job invokes it.
//
// ---------------------------------------------------------------------------
// What this covers that the other test binaries can't
// ---------------------------------------------------------------------------
// core_tests exercises the log sinks directly, with hand-built events, and
// rig_tests exercises the verbs that feed them. Between them that settles
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
// Why this lives in the suite rather than beside the runner
// ---------------------------------------------------------------------------
// The binary under test is framework -- framework/runner builds it, and every line
// of it is portable across deployments. What is asserted about it here is not:
// this suite's group and test names, this DUT's name in the report header, this
// rig's instruments in the log, this deployment's three criteria variants. A
// bench with one meter and one script satisfies none of it.
//
// So the tests belong to the deployment whose facts they encode, which is why
// they sit in suite/acceptance/ and are discovered from there by glob (see
// framework/runner/CMakeLists.txt). A second deployment writes its own, or sets
// THORIUM_ACCEPTANCE_TESTS=OFF and writes none -- see dev/README.md.
//
// The hook-ordering fixture these tests drive as a second runner is the other
// half of that split and went the other way: it names no instrument and no DUT
// point, and what it tests is the runner's own ordering, so it is framework and
// lives in framework/runner/tests/fixtures/.
//
// ---------------------------------------------------------------------------
// It is also meant to be read
// ---------------------------------------------------------------------------
// Every invocation is printed as it happens, tagged [  INVOKE  ], so running
// this binary -- or `ctest -V` -- is a worked document of how run_scripts is
// meant to be called:
//
//     ctest --test-dir <build-dir> -L acceptance -V
//     <build-dir>/framework/runner/acceptance_tests
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
// <build>/framework/runner/acceptance/<suite>.<test>/ and everything it produced stays
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
    // Both supplied by framework/runner/CMakeLists.txt: the built binary's real path (via a
    // $<TARGET_FILE:...> generator expression, so this works under any
    // generator or build layout) and where to keep artifacts.
    //
    constexpr std::string_view kRunScripts = THORIUM_RUN_SCRIPTS_EXE;
    constexpr std::string_view kOutputRoot = THORIUM_ACCEPTANCE_OUTPUT_DIR;

    //
    // The same runner over a *fixture* catalog whose hooks announce themselves
    // on stdout and can be made to fail on demand -- which the shipped suite's
    // real hooks cannot (see the AcceptanceHooks section below for why that
    // still matters now that the shipped catalog declares both). See
    // framework/runner/CMakeLists.txt and framework/runner/tests/fixtures/.
    //
    constexpr std::string_view kRunScriptsHooked = THORIUM_RUN_SCRIPTS_HOOKED_EXE;

    //
    // Read as bytes, then normalised to \n line endings.
    //
    // Both halves are deliberate. Binary, so what comes back is what is
    // actually on disk rather than whatever the platform's text mode decides --
    // and then the CRs come out in exactly one place, here, rather than at each
    // reader below.
    //
    // That second half is a Windows fix. The child's stdout and both log files
    // arrive with \r\n there, so a reader splitting on \n keeps a \r on the end
    // of every line: a marker pulled out with substr() carries an invisible
    // character into its comparison, and a substring assertion for a line's
    // last word fails against a file that plainly contains it. Normalising per
    // reader also works, but it is a list that silently falls behind the next
    // reader added -- and only some readers show the symptom, which is worse
    // than none of them doing so (see verdictOrder(), which survives on
    // Windows only because operator>> happens to treat \r as whitespace).
    //
    // Only \r\n is collapsed, not every \r: a lone one means something to
    // whoever wrote it, and none of the producers here emit one to begin with.
    //
    auto readFile( const std::filesystem::path & path) -> std::string
    {
        std::ifstream in( path, std::ios::in | std::ios::binary);
        std::ostringstream contents;

        contents << in.rdbuf();

        const auto raw = contents.str();

        std::string text;
        text.reserve( raw.size());

        for( std::size_t i = 0; i < raw.size(); ++i)
        {
            if( raw[ i] == '\r' && i + 1 < raw.size() && raw[ i + 1] == '\n')
            {
                continue;
            }

            text += raw[ i];
        }

        return text;
    }

    auto writeFile( const std::filesystem::path & path, const std::string & text) -> void
    {
        std::ofstream out( path, std::ios::out | std::ios::trunc);
        out << text;
    }

    //
    // Quoted for whichever shell std::system() hands the command to. None of
    // the arguments below actually need it -- they are all plain flags -- but a
    // quoting helper that only works for the arguments you happened to try is
    // the kind of thing that breaks the first time a path has a space in it,
    // and the build directory's path is not this file's to choose.
    //
    // The two shells share no quoting rule at all, which is why this cannot be
    // one implementation: cmd.exe does not treat ' as quoting and passes it
    // through as an ordinary character, so a POSIX-quoted `cd 'C:/dir'` asks it
    // for a directory whose name begins with a quote -- reported as an invalid
    // path, and the reason every scenario here failed on Windows before this
    // split existed.
    //
    auto shellQuoted( const std::string_view text) -> std::string
    {
    #ifdef _WIN32
        //
        // cmd.exe: double quotes, with an embedded one doubled. Backslashes
        // need no escaping inside them, which is what makes a native Windows
        // path safe to wrap this way.
        //
        std::string result = "\"";

        for( const char c : text)
        {
            result += ( c == '"') ? "\"\"" : std::string( 1, c);
        }

        return result + "\"";
    #else
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
    #endif
    }

    //
    // A path in the form the local shell expects -- separators included. CMake
    // hands this file its paths with forward slashes on every platform (see
    // THORIUM_RUN_SCRIPTS_EXE in framework/runner/CMakeLists.txt), and while cmd.exe
    // tolerates those in a quoted argument, it does not in every position, and
    // a command.txt written with them is not something a Windows user can
    // paste back.
    //
    auto nativePath( const std::string_view path) -> std::string
    {
        return std::filesystem::path( path).make_preferred().string();
    }

    //
    // Substring assertions that name the artifact to go and read on failure --
    // the whole reason the files are kept. Written as AssertionResult rather
    // than reaching for gmock's HasSubstr: this way the failure message can
    // carry the path, which HasSubstr's cannot.
    //
    //
    // How many times a marker appears -- for the assertions whose subject is
    // that something happened exactly once, which containsText cannot express.
    //
    auto countOccurrences( const std::string & text, const std::string_view needle) -> std::size_t
    {
        std::size_t count = 0;

        for ( auto at = text.find( needle); at != std::string::npos; at = text.find( needle, at + needle.size()))
        {
            ++count;
        }

        return count;
    }

    //
    // How many observations a recording holds: its rows, not its lines. The
    // file also carries a comment header (see core::writeSelectionHeader), and
    // a count of newlines would fold that in -- so a test asserting "seven
    // readings" would be asserting seven readings plus however many lines of
    // provenance the file happens to open with.
    //
    // '#' spelled out rather than taken from core::kCommentMarker, like every
    // other expectation in this file: these tests run the binary as a process
    // and read its artifacts back, so what they assert against is the format as
    // published, not the constant the program happens to build with.
    //
    auto recordedRows( const std::string & recording) -> std::size_t
    {
        std::size_t rows = 0;

        for ( const auto line : std::views::split( std::string_view( recording), '\n'))
        {
            const auto text = std::string_view( line);

            if ( !text.empty() && text.front() != '#')
            {
                ++rows;
            }
        }

        return rows;
    }

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
            // The hook fixture's runner (see framework/runner/CMakeLists.txt), with the
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
            // "setup", "script", "teardown". The machine log brackets each hook
            // (see TheMachineLogNamesBothLevelsOfBracketing below), but these
            // hooks post nothing of their own and the scripts are not in that
            // vocabulary at all, so stdout is the one stream carrying the whole
            // order.
            //
            [[nodiscard]]
            auto hookOrder() const -> std::vector<std::string>
            {
                constexpr std::string_view prefix = "HOOKFIXTURE ";

                std::vector<std::string> markers;
                std::istringstream       lines( mOut);

                //
                // No \r to strip here: mOut came through readFile(), which
                // normalises line endings for every reader at once.
                //
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
                std::string invocation = nativePath( exe);
                std::string readable   = std::filesystem::path( exe).filename().string();

                //
                // /d on Windows, so a build tree on a different drive than the
                // test binary's is actually entered rather than silently only
                // changing that drive's current directory -- which would leave
                // the run writing its artifacts wherever the test happened to
                // be, and every assertion below reading an empty file.
                //
            #ifdef _WIN32
                std::string command = "cd /d " + shellQuoted( nativePath( mDir.string())) + " && ";
            #else
                std::string command = "cd " + shellQuoted( mDir.string()) + " && ";
            #endif

                //
                // Set in the command rather than in this process: the variables
                // belong to the run being tested, and a scenario that leaked
                // one into the test binary's own environment would change every
                // scenario after it.
                //
                // The two shells spell this differently and there is no common
                // form -- `VAR=value prog` is a POSIX assignment-prefix, which
                // cmd.exe reads as a program named "VAR=value". Both spellings
                // scope the variable to the child, which is the property that
                // matters.
                //
                for( const auto & variable : environment)
                {
                    invocation = variable + " " + invocation;
                    readable   = variable + " " + readable;

                #ifdef _WIN32
                    command += "set " + shellQuoted( variable) + " && ";
                #else
                    command += variable + " ";
                #endif
                }

                command += shellQuoted( nativePath( exe));

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

                //
                // Windows' std::system() hands back the child's exit code
                // directly; POSIX packs it into a wait status that has to be
                // unpacked, and which says the child did not exit normally at
                // all if it was killed by a signal (-1 here, so an assertion on
                // a status can never mistake a crash for a verdict).
                //
            #ifdef _WIN32
                mStatus = raw;
            #else
                mStatus = ( raw != -1 && WIFEXITED( raw)) ? WEXITSTATUS( raw) : -1;
            #endif

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
    struct AcceptanceCriteria  : Acceptance {};
    struct AcceptanceHumanLog  : Acceptance {};
    struct AcceptanceMachineLog: Acceptance {};
    struct AcceptanceLogFiles  : Acceptance {};
    struct AcceptanceRepeat    : Acceptance {};
    struct AcceptanceHooks     : Acceptance {};
    struct AcceptanceRecording : Acceptance {};

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
    EXPECT_TRUE( containsText( outPath(), mOut, "Console|StatusRegister|"));
    EXPECT_TRUE( containsText( outPath(), mOut, "Transient|AcDropout|"));

    // Exactly one line per catalog test, nothing else on stdout.
    EXPECT_EQ( std::count( mOut.begin(), mOut.end(), '\n'), 4);

    EXPECT_FALSE( std::filesystem::exists( mDir / "logs"));
}

// ---------------------------------------------------------------------------
// Safing: run_scripts --safe
// ---------------------------------------------------------------------------

//
// What the rig console re-invokes after an abnormal child exit (see
// framework/runner/src/main.cpp and hal/safing.hpp). Exits 0 unconditionally, and writes no
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
// --help is generated from the Options struct's annotations (see framework/runner/src/cli.hpp),
// which is the whole reason it is worth an acceptance test: the risk is not that
// the text is badly worded, it is that a flag exists and no line describes it.
//
// So this asserts on coverage rather than on layout -- every flag the parser
// accepts has to appear, including both spellings of the aliased one -- and it
// asserts --help touches nothing, since a caller asking what the flags are has
// not asked for a rig to be driven or a log to be written.
//
TEST_F( AcceptanceArguments, HelpListsEveryFlagAndTouchesNothing)
{
    EXPECT_EQ( run( { "--help" }), 0);

    for ( const auto flag : { "--select=", "--list-tests", "--safe", "--help",
                              "--criteria=", "--repeat=", "--until-failure",
                              "--record=", "--replay=", "--log-dir=", "--sarif=",
                              "--rtf=", "--no-logs", "--no-color", "--no-colour",
                              "--quiet", "--dut-serial=", "--operator=" })
        EXPECT_TRUE( containsText( outPath(), mOut, flag)) << flag;

    EXPECT_FALSE( std::filesystem::exists( mDir / "logs"));
}

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
// A typo in --select is refused -- not a crash, and not a silently-empty pass.
// (tools/run-tests.sh validates ids before it gets here, but a caller typing
// the flag by hand has no such help.)
//
TEST_F( AcceptanceSelection, SelectingNothingIsReportedRatherThanPassingVacuously)
{
    EXPECT_EQ( run( { "--select=NoSuchTest" }), 1);

    EXPECT_TRUE( containsText( errPath(), mErr, "No catalog test named"));
    EXPECT_TRUE( containsText( errPath(), mErr, "NoSuchTest"));
}

//
// The half that used to slip through, and the reason the check is per-id rather
// than "did anything match": a list with one good id and one typo ran the good
// one and exited zero, so the report looked like a complete answer to what the
// caller asked for and was not.
//
// Asserted on the exit status *and* on nothing having run, because either one
// alone would be satisfied by the wrong fix -- reporting the typo while still
// running SupplyRail would pass an exit-status check, and refusing silently
// would pass a no-output check.
//
TEST_F( AcceptanceSelection, OneBadIdInAnOtherwiseValidSelectionRunsNothingAtAll)
{
    EXPECT_EQ( run( { "--select=SupplyRail,NoSuchTest" }), 1);

    EXPECT_TRUE( containsText( errPath(), mErr, "NoSuchTest"));

    // The valid half is named nowhere: it did not run, and it is not the
    // problem being reported either.
    EXPECT_TRUE( omitsText( errPath(), mErr, "SupplyRail"));
}

//
// Every unmatched id, not just the first -- a mistyped --select is usually a
// mistyped list, and one typo per run is a poor way to discover there are two.
//
TEST_F( AcceptanceSelection, EveryUnmatchedIdIsNamedAtOnce)
{
    EXPECT_EQ( run( { "--select=NoSuchTest,AlsoMissing" }), 1);

    EXPECT_TRUE( containsText( errPath(), mErr, "NoSuchTest"));
    EXPECT_TRUE( containsText( errPath(), mErr, "AlsoMissing"));
}

// ---------------------------------------------------------------------------
// Tolerance variants: run_scripts --criteria=
// ---------------------------------------------------------------------------

//
// Every variant the deployment declares is compiled into this one binary, and
// --criteria= is what picks between them (see suite/README.md). This is the
// only place that can show it end to end: unit tests can select a variant and
// watch a verdict change, but only a real invocation shows the flag reaching
// the tolerances, the console and both log files as one consistent story.
//
// Asserted through the *rendered tolerance* rather than the criterion's prose:
// "+/-0.15 V" is core::describeCriterion reading the predicate that was
// actually evaluated, whereas the description beside it is hand-written text
// that could say anything. If those two ever disagree, this catches the one
// that matters.
//
TEST_F( AcceptanceCriteria, WithoutTheFlagTheBuildsDefaultVariantApplies)
{
    run( { "--no-logs", "--no-color" });

    EXPECT_TRUE( containsText( outPath(), mOut, "Criteria          production"));
    EXPECT_TRUE( containsText( outPath(), mOut, "= 5 V +/-0.05 V"));
}

TEST_F( AcceptanceCriteria, TheFlagChangesTheToleranceThatIsActuallyApplied)
{
    run( { "--no-logs", "--no-color", "--criteria=aged" });

    EXPECT_TRUE( containsText( outPath(), mOut, "Criteria          aged"));
    EXPECT_TRUE( containsText( outPath(), mOut, "= 5 V +/-0.15 V"));

    //
    // The 5V rail specifically, not "+/-0.05 V" anywhere: fuse_register_script
    // also makes an ad-hoc EQ( 12.0_V).epsilon( 0.05_V) check that no variant
    // has any say over, so the broader assertion would fail on a criterion that
    // is behaving exactly as intended.
    //
    EXPECT_TRUE( omitsText( outPath(), mOut, "= 5 V +/-0.05 V"))
        << "production's 5V tolerance must not be applied in an aged run";
}

//
// One binary, three tolerance tables: the same criterion id renders a different
// limit per run, with no rebuild between them. This is the property the whole
// mechanism exists for, and running the binary is the only way to state it.
//
TEST_F( AcceptanceCriteria, TheSameBinaryServesEveryVariant)
{
    run( { "--no-logs", "--no-color", "--criteria=production" });
    EXPECT_TRUE( containsText( outPath(), mOut, "= 5 V +/-0.05 V"));

    run( { "--no-logs", "--no-color", "--criteria=stress" });
    EXPECT_TRUE( containsText( outPath(), mOut, "= 5 V +/-0.1 V"));

    run( { "--no-logs", "--no-color", "--criteria=aged" });
    EXPECT_TRUE( containsText( outPath(), mOut, "= 5 V +/-0.15 V"));
}

//
// Both logs have to name the variant that was applied, not the one the build
// defaults to -- a machine log keyed on the wrong tolerance table would make
// two incomparable runs look comparable.
//
TEST_F( AcceptanceCriteria, BothLogsRecordTheVariantThatWasApplied)
{
    run( { "--quiet", "--criteria=stress" });

    const auto sarif = findArtifact( ".sarif");
    const auto rtf   = findArtifact( ".rtf");

    ASSERT_FALSE( sarif.empty());
    ASSERT_FALSE( rtf.empty());

    const auto machineLog = readFile( sarif);

    EXPECT_TRUE( containsText( sarif, machineLog, R"("criteriaVariant": "stress")"));

    // The run's own identity, which a server uses to group results -- see
    // core::SarifSink::automationDetails.
    EXPECT_TRUE( containsText( sarif, machineLog, R"("id": "thorium/DeviceX/stress/)"));

    EXPECT_TRUE( containsText( rtf, readFile( rtf), "stress"));
}

//
// Fatal, and it says what would have worked. A runner that quietly fell back to
// the default here would apply the wrong tolerances to real hardware and hand
// back a log that looks entirely normal -- worse than the unknown-flag case,
// which at least fails obviously.
//
TEST_F( AcceptanceCriteria, AnUnknownVariantIsFatalAndListsTheKnownOnes)
{
    EXPECT_EQ( run( { "--criteria=no-such-variant" }), 1);

    EXPECT_TRUE( containsText( errPath(), mErr, "Unknown criteria variant: no-such-variant"));
    EXPECT_TRUE( containsText( errPath(), mErr, "production"));
    EXPECT_TRUE( containsText( errPath(), mErr, "stress"));
    EXPECT_TRUE( containsText( errPath(), mErr, "aged"));

    EXPECT_FALSE( std::filesystem::exists( mDir / "logs")) << "a rejected run must not leave a log behind";
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

    //
    // The run's own bracket, at the level of the groups it brackets -- and its
    // readings under it, where they used to sit between the header and the
    // first group belonging to nothing.
    //
    // Headed by the prose its RUN_SETUP line described it with, the way a group
    // and a test are headed by theirs. Every hook in a catalog is called
    // "setup" or "teardown", so that description is what a reader has to tell
    // one from another.
    //
    EXPECT_TRUE( containsText( outPath(), mOut, "\nsetup Bring the AC input"));
    EXPECT_TRUE( containsText( outPath(), mOut, "\nteardown Take the supplies back down"));

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

    // The same headings the console showed -- the two are the same log, and an
    // operator who saw a line on screen that is not in the file has been misled.
    EXPECT_TRUE( containsText( rtf, document, "Bring the AC input"));
    EXPECT_TRUE( containsText( rtf, document, "Take the supplies back down"));
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

//
// The catalog's own shape, in the machine log. Group and test already ride
// along on every result as properties, which answers "which test was this?" but
// not "what did the run consist of?" -- and the titles a GROUP/TEST entry gives
// are otherwise nowhere in the file at all.
//
TEST_F( AcceptanceMachineLog, SarifNamesTheCatalogTheRunWalked)
{
    run( { "--quiet" });

    const auto sarif = findArtifact( ".sarif");

    ASSERT_FALSE( sarif.empty());

    const auto log = readFile( sarif);

    EXPECT_TRUE( looksComplete( log)) << "truncated machine log: " << sarif.string();

    EXPECT_TRUE( containsText( sarif, log, R"("ruleId": "Thorium/Group")"));
    EXPECT_TRUE( containsText( sarif, log, R"("ruleId": "Thorium/Test")"));

    // Each with the title its catalog entry gives it.
    EXPECT_TRUE( containsText( sarif, log, "Tests validating DUT output voltage rails"));
    EXPECT_TRUE( containsText( sarif, log, "Verify supply rail voltages via matrix"));

    // A test is qualified by its group, so "which group did this belong to" is
    // answerable from the location alone.
    EXPECT_TRUE( containsText( sarif, log, R"("fullyQualifiedName": "OutputVoltage/SupplyRail")"));

    //
    // And the run's own bracket -- the shipped catalog declares
    // RUN_SETUP/RUN_TEARDOWN and no group-level pair, so both hooks appear
    // qualified by nothing.
    //
    EXPECT_TRUE( containsText( sarif, log, R"("ruleId": "Thorium/Phase")"));
    EXPECT_TRUE( containsText( sarif, log, R"("fullyQualifiedName": "setup")"));
    EXPECT_TRUE( containsText( sarif, log, R"("fullyQualifiedName": "teardown")"));

    // The readings RUN_SETUP took, attributed to it rather than left looking
    // like the first test's.
    EXPECT_TRUE( containsText( sarif, log, R"("phase": "setup")"));
}

//
// No script in the shipped suite, and neither of its hooks, moves a relay on a
// live path.
//
// This is the check that was missing, and it is worth being clear about what it
// is for. The interlock does not refuse hot switching -- that is deliberate, and
// core/source.hpp gives the counterexample (a safety interlock dropping a
// connection must not first wait out a supply's ramp-down). So a mis-ordered
// Remove/Disconnect in a script breaks nothing, fails nothing, and exits zero.
// Every other guarantee in this repo about that ordering was a comment.
//
// The rule is therefore asserted where it actually belongs -- about *this
// suite's own scripts*, not about the framework. A rig that genuinely wants a
// hot switch somewhere writes it, gets the note in its log, and does not have
// this test. What it buys here is that swapping those two lines in any script
// under suite/scripts/ turns from a silent relay-wear regression into a red
// build naming the instrument.
//
// Note honestly what it does not cover: the paths a run does not take. A hot
// switch inside a branch that this DUT's readings never reach (acDropoutScript
// has one such branch -- see its `if( captured)`) would not appear here. This
// is a check on the run, which is the only thing a log can be a check on.
//
TEST_F( AcceptanceMachineLog, NoShippedScriptOrHookMovesARelayUnderLoad)
{
    // The whole catalog, no --select: a guard that covered some scripts would
    // read as though it covered all of them.
    run( { "--quiet" });

    const auto sarif = findArtifact( ".sarif");

    ASSERT_FALSE( sarif.empty());

    const auto log = readFile( sarif);

    //
    // Named rather than merely counted. omitsText would report only that the
    // phrase was present and in which file, and the first thing anyone seeing
    // this fail needs is which instrument -- the notice carries it (see
    // core::hotSwitchDetail), so the assertion should hand it over instead of
    // sending a reader into a machine log to find it.
    //
    std::vector<std::string> notices;

    for ( auto at = log.find( "hot switching"); at != std::string::npos; at = log.find( "hot switching", at + 1))
    {
        // To the end of the JSON string the notice sits in, which is exactly
        // one Detail -- "hot switching -- relay opened while the output was
        // energised (AcP1)".
        const auto end = log.find( '"', at);

        notices.push_back( log.substr( at, end == std::string::npos ? std::string::npos : end - at));
    }

    EXPECT_TRUE( notices.empty())
        << "a shipped script or hook moved a relay on a live path -- put Remove before\n"
           "Disconnect (or Connect before Apply); see core/source.hpp:\n  "
        << [&notices]
           {
               std::string joined;

               for ( const auto & notice : notices)
                   joined += notice + "\n  ";

               return joined;
           }()
        << "\n  in: " << sarif.string();
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
    // The stamp is second-resolution (see fileStamp in framework/runner/src/main.cpp), so
    // two runs inside the same second would legitimately collide. Sleeping is
    // the honest way to test the property that actually matters -- that a
    // *later* run doesn't clobber an earlier one -- rather than asserting a
    // uniqueness guarantee the naming scheme doesn't make.
    //
    //
    // Slept in-process rather than shelled out to sleep(1), which Windows'
    // cmd.exe has no equivalent of -- and which was never worth a subprocess
    // here anyway.
    //
    std::this_thread::sleep_for( std::chrono::seconds( 1));

    run( { "--quiet", "--select=SupplyRail" });

    EXPECT_EQ( countArtifacts( ".sarif"), 2u);
    EXPECT_EQ( countArtifacts( ".rtf"),   2u);
}

// ---------------------------------------------------------------------------
// Repeating a run: --repeat / --until-failure
// ---------------------------------------------------------------------------

//
// The property the flag is named for, and the one worth pinning from outside:
// the *selection* is what repeats. Four tests over two passes must come back
// A B C D A B C D, not A A B B C C D D -- which is what makes a pass a
// meaningful unit for the hooks to bracket and for --until-failure to
// stop at.
//
TEST_F( AcceptanceRepeat, RepeatRunsTheWholeSelectionEachPassRatherThanEachScript)
{
    run( { "--repeat=2", "--no-color" });

    EXPECT_EQ( verdictOrder( mOut),
        ( std::vector<std::string>{ "FuseRegister", "SupplyRail", "AcDropout", "StatusRegister",
                                    "FuseRegister", "SupplyRail", "AcDropout", "StatusRegister" }));
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

TEST_F( AcceptanceHooks, SetupRunsBeforeTheScriptsAndTeardownAfterThem)
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
TEST_F( AcceptanceHooks, TheHooksBracketEveryRepeatPassRatherThanEachOne)
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
TEST_F( AcceptanceHooks, AFailingSetupStopsTheRunButStillTearsDown)
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
TEST_F( AcceptanceHooks, TeardownStillRunsWhenAScriptThrows)
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
TEST_F( AcceptanceHooks, AFailingTeardownFailsAnOtherwisePassingRun)
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
TEST_F( AcceptanceHooks, StoppingEarlyOnFailureStillTearsDown)
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
TEST_F( AcceptanceHooks, ASelectionMatchingNothingNeverReachesTheHooks)
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
TEST_F( AcceptanceHooks, AGroupWhoseTestsAreAllDeselectedIsNeverBracketed)
{
    EXPECT_EQ( runHooked( { "--select=OtherFixtureTest", "--no-color" }), 0);

    EXPECT_EQ( hookOrder(), ( std::vector<std::string>{ "setup", "other-script", "teardown" }));
}

//
// The other direction: selecting the hooked group's one test brackets that
// group and reaches nothing else. Note that the run-level pair still runs --
// it brackets the selection, whatever the selection turned out to be.
//
TEST_F( AcceptanceHooks, SelectingATestBracketsItsOwnGroupAndNoOther)
{
    EXPECT_EQ( runHooked( { "--select=FixtureTest", "--no-color" }), 0);

    EXPECT_EQ( hookOrder(), ( std::vector<std::string>{
        "setup", "group-setup", "script", "group-teardown", "teardown" }));
}

//
// The same two levels, on the page. A group's own SETUP is indented with that
// group's tests; the catalog's RUN_SETUP is unindented with the groups.
//
TEST_F( AcceptanceHooks, TheHumanLogHeadsEachHookAtTheLevelItBrackets)
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
TEST_F( AcceptanceHooks, TheMachineLogNamesBothLevelsOfBracketing)
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
TEST_F( AcceptanceHooks, AFailingGroupSetupSkipsItsOwnGroupOnly)
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
TEST_F( AcceptanceHooks, AFailingGroupTeardownFailsAnOtherwisePassingRun)
{
    EXPECT_EQ( runHooked( { "--no-color" }, { "THORIUM_FIXTURE_GROUP_TEARDOWN_FAILS=1" }), 1);

    EXPECT_EQ( hookOrder(), ( std::vector<std::string>{
        "setup", "group-setup", "script", "group-teardown", "other-script", "teardown" }));

    EXPECT_TRUE( containsText( errPath(), mErr,
        "TEARDOWN for group 'OutputVoltage' reported failure"));
}

//
// The shipped suite's own hooks, on the real binary rather than the fixture.
// rigPowerOn/rigPowerOff say nothing on stdout, so the machine log is the only
// place they are visible -- and what they put there is the thing worth pinning:
// a power-up and a power-down that are each other's inverse, bracketing the
// scripts, with hal::safeRig() behind them.
//
// Ordering asserted by offset rather than by containsText alone: that the
// events are present says nothing, since safing at the end leaves the rig in
// the same state either way. The sequence is the entire reason these are hooks
// rather than something left to safing.
//
TEST_F( AcceptanceHooks, TheShippedHooksBracketTheRunWithAnOrderedPowerCycle)
{
    EXPECT_EQ( run( { "--quiet" }), 1);

    const auto sarif = findArtifact( ".sarif");

    ASSERT_FALSE( sarif.empty());

    const auto log = readFile( sarif);

    const auto positionOf = [&log]( const std::string_view needle)
    {
        return log.find( needle);
    };

    const auto closeAc1    = positionOf( "Connect AcP1");
    const auto applyAc1    = positionOf( "Apply AcP1");
    const auto applyDc1    = positionOf( "Apply DcP1");
    const auto firstVerify = positionOf( "Verify FS_Fuse_01");
    const auto lastVerify  = log.rfind( "Verify ");

    //
    // The power-down half is anchored rather than taken as the first
    // occurrence of each event, and that is not defensive coding -- it is what
    // the test means. A source is allowed to come down and go back up mid-run,
    // and one does: acDropoutScript drops the primary AC to measure what the
    // DUT's output rails do about it (see
    // suite/scripts/ac_dropout_script.cpp), and the Transient group's own
    // TEARDOWN puts it back (suite/scripts/transient_bracket.cpp).
    // First-occurrence lookups would find *that* Remove and assert the
    // run-level teardown's ordering against an event belonging to a group.
    //
    // DcP1 is the anchor because nothing but the teardown ever removes it, so
    // its last occurrence is unambiguously the start of the power-down. Every
    // later event is then searched for from there.
    //
    const auto removeDc1   = log.rfind( "Remove DcP1");
    const auto removeAc1   = log.find( "Remove AcP1", removeDc1);
    const auto openAc1     = log.find( "Disconnect AcP1", removeAc1);
    const auto safed       = positionOf( "Safe rig");

    ASSERT_NE( closeAc1,  std::string::npos) << "no power-up in " << sarif;
    ASSERT_NE( removeDc1, std::string::npos) << "no power-down in " << sarif;
    ASSERT_NE( safed,     std::string::npos) << "no safing record in " << sarif;

    // Up: relay closed dead, then energised, primary before the alternates,
    // and all of it before the first script's first check.
    EXPECT_LT( closeAc1,    applyAc1)    << "AcP1 was energised before its relay closed -- hot switching";
    EXPECT_LT( applyAc1,    applyDc1)    << "an alternate rail came up before the primary";
    EXPECT_LT( applyDc1,    firstVerify) << "a script ran before the rig was powered";

    // Down: the exact inverse, after the last check and before safing.
    EXPECT_LT( lastVerify,  removeDc1)   << "the teardown ran before the scripts finished";
    EXPECT_LT( removeDc1,   removeAc1)   << "the primary AC source went down before a DC rail";
    EXPECT_LT( removeAc1,   openAc1)     << "a relay was opened before its source was off -- hot switching";
    EXPECT_LT( openAc1,     safed)       << "safing preceded the teardown it is meant to back up";
}

//
// The question a script author is bound to ask when writing a power-up hook:
// if RUN_SETUP fails half way through, who powers the rig back down? Not the hook
// -- calling the teardown from inside the setup's own failure path would run it
// twice, since main.cpp's TeardownGuard is constructed *before* RUN_SETUP
// so that a setup which energised three rails and then failed on the fourth
// still gets torn down.
//
// Asserted on the shipped hooks rather than the fixture, because the fixture's
// version of this (AFailingSetupStopsTheRunButStillTearsDown, above) only shows
// that *a* teardown ran. What matters here is that the real one ran, in full:
// every source this rig's setup could have energised is removed, in order,
// even though no test ever started.
//
// A replay file is what makes the setup fail on demand -- the rig itself has no
// way to be told to come up wrong, and this is exactly the case --replay exists
// for. All six setup readings are present because the hook checks each one
// before returning; only the first is out of tolerance. AcP1 contributes three
// of them, one per phase, keyed with the phase (see core::Port::qualifiedBy).
//
TEST_F( AcceptanceHooks, AFailedPowerUpIsStillPoweredBackDown)
{
    writeFile( mDir / "bad-setup.tsv",
        "0\t0\t<run>\tAcP1.A.Voltage\tAcP1\tVoltage\t100.0\n"    // outside 115 V +/-2 V
        "1\t0\t<run>\tAcP1.B.Voltage\tAcP1\tVoltage\t115.0\n"
        "2\t0\t<run>\tAcP1.C.Voltage\tAcP1\tVoltage\t115.0\n"
        "3\t0\t<run>\tDcP1.Voltage\tDcP1\tVoltage\t28.0\n"
        "4\t0\t<run>\tDcP2.Voltage\tDcP2\tVoltage\t28.0\n"
        "5\t0\t<run>\tDcP3.Voltage\tDcP3\tVoltage\t24.0\n");

    EXPECT_EQ( run( { "--replay=bad-setup.tsv", "--quiet" }), 1);

    EXPECT_TRUE( containsText( errPath(), mErr, "RUN_SETUP reported failure; no test was run"));

    const auto sarif = findArtifact( ".sarif");

    ASSERT_FALSE( sarif.empty());

    const auto log = readFile( sarif);

    // No test ran...
    EXPECT_FALSE( containsText( sarif, log, "Verify FS_Supply_5V0"));

    // ...and the rig was still taken down, every source and both relay paths.
    EXPECT_TRUE( containsText( sarif, log, "Remove DcP1"));
    EXPECT_TRUE( containsText( sarif, log, "Remove DcP2"));
    EXPECT_TRUE( containsText( sarif, log, "Remove DcP3"));
    EXPECT_TRUE( containsText( sarif, log, "Remove AcP1"));
    EXPECT_TRUE( containsText( sarif, log, "Disconnect DcP3"));
    EXPECT_TRUE( containsText( sarif, log, "Disconnect AcP1"));

    // Once, not twice -- the count is the assertion that the hook does not also
    // call the teardown itself.
    EXPECT_EQ( countOccurrences( log, "Remove AcP1"), 1u);
}

// ---------------------------------------------------------------------------
// The replayable value stream: --record / --replay
// ---------------------------------------------------------------------------
//
// A third artifact, and unlike the two logs it is an input as well as an
// output: --record writes the readings a run took, --replay feeds them back to
// a later run with no rig attached. See core/recording.hpp for the format.
//

TEST_F( AcceptanceRecording, RecordWritesEveryReadingTheRunTook)
{
    EXPECT_EQ( run( { "--record=readings.tsv", "--quiet", "--no-logs" }), 1);

    const auto tsv = readFile( mDir / "readings.tsv");

    EXPECT_TRUE( containsText( mDir / "readings.tsv", tsv, "Vout\tDmm2\tVoltage"));
    EXPECT_TRUE( containsText( mDir / "readings.tsv", tsv, "Output5V\tDmm1\tVoltage"));
    EXPECT_TRUE( containsText( mDir / "readings.tsv", tsv, "Output3V3\tDmm1\tVoltage"));

    // "Every reading the run took" includes the ones RUN_SETUP took before
    // the first script -- rigPowerOn() reads each source back to decide whether
    // the rig came up (see suite/scripts/rig_power_on.cpp). A hook's readings
    // are readings: its verdict gates the run, so a replay that could not
    // reproduce them could not reproduce the run.
    //
    // Keyed by instrument rather than by point, because these are instrument
    // readbacks with no route -- see core::MeasureEngine's point-free overload.
    EXPECT_TRUE( containsText( mDir / "readings.tsv", tsv, "AcP1.A.Voltage\tAcP1\tVoltage"));
    EXPECT_TRUE( containsText( mDir / "readings.tsv", tsv, "DcP3.Voltage\tDcP3\tVoltage"));

    //
    // And the console reply, in the same file and the same stream. A payload
    // row carries "<bytes>" where a quantity row carries its unit's name, and
    // its value is unspaced hex rather than the text it may be -- see
    // core::kPayloadKind on why the token cannot collide with a unit, and
    // core/recording.hpp on why the value is not written as text.
    //
    EXPECT_TRUE( containsText( mDir / "readings.tsv", tsv, "Ser1.Data\tSer1\t<bytes>"));

    //
    // And the third kind of observation: whether the single-shot capture
    // acDropoutScript arms actually completed. A flag row carries "<flag>"
    // where a quantity row carries its unit and a payload row carries
    // "<bytes>", and its value is 1 or 0 -- see core::kFlagKind, and
    // core/acquire.hpp on why an Await is recorded at all.
    //
    EXPECT_TRUE( containsText( mDir / "readings.tsv", tsv, "Osc1.Acquisition\tOsc1\t<flag>"));

    //
    // The scope's two readings at one pin, keyed apart by which measurement
    // each is -- "Output5V.Vbase" and not a second "Output5V" (see
    // core::Port::qualifiedBy). Without the qualifier these would be one
    // recording slot, and a replay would hand the baseline reading to
    // whichever of the two asked first.
    //
    EXPECT_TRUE( containsText( mDir / "readings.tsv", tsv, "Output5V.Vbase\tOsc1\tVoltage"));

    EXPECT_EQ( recordedRows( tsv), 13u);   // six from setup, seven from the scripts
}

//
// The point of the whole thing: with a recording behind it, a run's verdict
// comes from the file rather than from the rig. These values are inside every
// criterion the three scripts check, so this run PASSES -- where the identical
// invocation against the (unseeded, zero-reading) simulated rig fails. Nothing
// else in this file can assert a passing run.
//
// It is also the one place the two halves of the session seam are exercised
// together from outside: one file arms both the measurements and the console
// reply, and the run passes only if both were fed from it. A replay that
// covered the readings and let the serial read go live would fail here, which
// is exactly the half-scripted run core::SessionBank exists to make impossible.
//
TEST_F( AcceptanceRecording, AReplayedRunTakesItsReadingsFromTheFileNotTheRig)
{
    // The six setup readings come first and have to be here too: rigPowerOn()
    // runs before the scripts and checks each one, so a file without them
    // replays a run whose rig never came up. Three of the six are AcP1's
    // phases, keyed individually. They carry "<run>" in the test column rather
    // than a test id, because RUN_SETUP belongs to the run -- kRunScope,
    // and AReplayCanBeNarrowedToOneTest below for what the column is for.
    //
    // The last row is the console reply: kind "<bytes>", value unspaced hex.
    // 41 43 4B is "ACK", 0D the terminator, 08 a status byte with READY (bit 3)
    // set and FAULT (bit 7) clear -- what FS_Console_1 requires.
    writeFile( mDir / "passing.tsv",
        "0\t0\t<run>\tAcP1.A.Voltage\tAcP1\tVoltage\t115.0\n"
        "1\t0\t<run>\tAcP1.B.Voltage\tAcP1\tVoltage\t115.0\n"
        "2\t0\t<run>\tAcP1.C.Voltage\tAcP1\tVoltage\t115.0\n"
        "3\t0\t<run>\tDcP1.Voltage\tDcP1\tVoltage\t28.0\n"
        "4\t0\t<run>\tDcP2.Voltage\tDcP2\tVoltage\t28.0\n"
        "5\t0\t<run>\tDcP3.Voltage\tDcP3\tVoltage\t24.0\n"
        "6\t0\tFuseRegister\tVout\tDmm2\tVoltage\t12.0\n"
        "7\t0\tSupplyRail\tOutput5V\tDmm1\tVoltage\t5.0\n"
        "8\t0\tSupplyRail\tOutput3V3\tDmm1\tVoltage\t3.3\n"
        //
        // acDropoutScript's three: the rail's settled level before the input
        // is dropped, whether the capture completed, and how low the rail
        // went. The middle row is the third kind of value this format holds
        // -- "<flag>", written 1 or 0 (see core::kFlagKind).
        //
        // 4.95 V against a 5.0 V baseline is a 50 mV dip, inside every
        // variant's limit. A 0 in the flag row would fail the run outright,
        // which is what makes this row worth being in the file rather than
        // assumed.
        //
        "9\t0\tAcDropout\tOutput5V.Vbase\tOsc1\tVoltage\t5.0\n"
        "10\t0\tAcDropout\tOsc1.Acquisition\tOsc1\t<flag>\t1\n"
        "11\t0\tAcDropout\tOutput5V.Vmin\tOsc1\tVoltage\t4.95\n"
        "12\t0\tStatusRegister\tSer1.Data\tSer1\t<bytes>\t41434B0D08\n");

    EXPECT_EQ( run( { "--replay=passing.tsv", "--quiet" }), 0);

    const auto sarif = findArtifact( ".sarif");

    ASSERT_FALSE( sarif.empty());

    const auto log = readFile( sarif);

    EXPECT_TRUE( containsText( sarif, log, "12 V"));
    EXPECT_TRUE( containsText( sarif, log, "5 V"));
    EXPECT_TRUE( containsText( sarif, log, "3.3 V"));

    // The replayed payload, rendered the way a text payload is written down.
    EXPECT_TRUE( containsText( sarif, log, "ACK"));
}

//
// Record then replay: the second run must read back exactly what the first one
// measured, which is the property that makes a recording worth keeping.
//
TEST_F( AcceptanceRecording, ARecordingReplaysToTheSameVerdict)
{
    const int recorded = run( { "--record=readings.tsv", "--quiet", "--no-logs" });
    const int replayed = run( { "--replay=readings.tsv", "--quiet", "--no-logs" });

    EXPECT_EQ( recorded, replayed);
}

//
// Every recording this program writes opens by saying which tests the run was
// asked for. A recording is as long as the run is, so "whole run or narrow
// capture?" is the first question about a file that arrived from a bench, and
// the rows do not answer it without reading all of them.
//
TEST_F( AcceptanceRecording, ARecordingSaysWhichTestsItsRunWasAskedFor)
{
    run( { "--select=SupplyRail,StatusRegister", "--record=narrow.tsv", "--quiet", "--no-logs" });
    run( { "--record=whole.tsv", "--quiet", "--no-logs" });

    EXPECT_TRUE( containsText( mDir / "narrow.tsv", readFile( mDir / "narrow.tsv"),
                               "# select=SupplyRail,StatusRegister"));

    // A run given no --select says so rather than leaving the line off -- see
    // core::kEverySelection.
    EXPECT_TRUE( containsText( mDir / "whole.tsv", readFile( mDir / "whole.tsv"), "# select=<all>"));
}

//
// And the header does not stop the file being a valid replay input: it is a
// comment, which the reader skips like any other.
//
TEST_F( AcceptanceRecording, AHeaderedRecordingStillReplays)
{
    const int recorded = run( { "--record=readings.tsv", "--quiet", "--no-logs" });
    const int replayed = run( { "--replay=readings.tsv", "--quiet", "--no-logs" });

    EXPECT_TRUE( containsText( mDir / "readings.tsv", readFile( mDir / "readings.tsv"), "# select="));
    EXPECT_EQ( recorded, replayed);
}

//
// What the test column buys at the command line: a whole run's recording, taken
// once on the bench, and one script debugged out of it at a desk. Without it a
// replayed test dequeues from the front of each point's queue and takes whatever
// the first test to touch that point recorded (see
// core::ScriptedSession::loadFromFile).
//
// The recording is captured by an ordinary run, not hand-authored, because the
// property being asserted is that the two halves agree about what a test id is.
//
TEST_F( AcceptanceRecording, AReplayCanBeNarrowedToOneTest)
{
    run( { "--record=readings.tsv", "--quiet", "--no-logs" });

    // One test out of the four the recording holds. RUN_SETUP runs and still
    // finds its readings -- they are recorded under "<run>" (see
    // core::kRunScope), which no selection filters out.
    run( { "--replay=readings.tsv", "--select=SupplyRail", "--no-color" });

    const auto sarif = findArtifact( ".sarif");

    ASSERT_FALSE( sarif.empty());

    const auto log = readFile( sarif);

    EXPECT_TRUE(  containsText( sarif, log, "SupplyRail"));
    EXPECT_FALSE( containsText( sarif, log, "FuseRegister"));
    EXPECT_FALSE( containsText( sarif, log, "AcDropout"));
    EXPECT_FALSE( containsText( sarif, log, "StatusRegister"));
}

//
// The recording says which tests it covers, so asking it for one it does not
// have is refused up front and names what it does have. The alternative is a
// first Measure failing about a point name, which reports the symptom and
// leaves the cause -- wrong recording for this selection -- to be guessed.
//
TEST_F( AcceptanceRecording, ReplayingATestTheRecordingDoesNotCoverIsFatalAndSaysWhatItHas)
{
    run( { "--select=SupplyRail", "--record=rail-only.tsv", "--quiet", "--no-logs" });

    EXPECT_EQ( run( { "--replay=rail-only.tsv", "--select=AcDropout", "--quiet" }), 1);

    EXPECT_TRUE( containsText( errPath(), mErr, "no readings for the selected test"));
    EXPECT_TRUE( containsText( errPath(), mErr, "SupplyRail"));
}

//
// And the recording of one test replays that test, which is the workflow the
// two tests above bracket: capture narrow on the bench, replay it at the desk.
//
TEST_F( AcceptanceRecording, ARecordingOfOneTestReplaysThatTest)
{
    const int recorded = run( { "--select=SupplyRail", "--record=rail-only.tsv", "--quiet", "--no-logs" });
    const int replayed = run( { "--replay=rail-only.tsv", "--select=SupplyRail", "--quiet", "--no-logs" });

    EXPECT_EQ( recorded, replayed);
}

//
// A repeated run records every pass, so replaying it needs the same pass count
// -- the file is a value stream, not a per-test snapshot.
//
TEST_F( AcceptanceRecording, EachRepeatPassIsRecorded)
{
    EXPECT_EQ( run( { "--repeat=3", "--record=readings.tsv", "--quiet", "--no-logs" }), 1);

    const auto tsv = readFile( mDir / "readings.tsv");

    // Seven observations per pass -- three voltages from the meters, the
    // console reply, and the scope's baseline, capture flag and minimum --
    // over three passes, plus the six the setup took, once, because the hooks
    // bracket the whole selection rather than each pass (see AcceptanceHooks
    // above). Six rather than four since rigPowerOn() reads AcP1 once per phase.
    EXPECT_EQ( recordedRows( tsv), 27u);
}

//
// Recording a replay would faithfully write out what it was just fed, handing
// back something that looks like a fresh capture and is a copy of the input.
//
TEST_F( AcceptanceRecording, RecordAndReplayAreExclusive)
{
    EXPECT_EQ( run( { "--record=out.tsv", "--replay=in.tsv" }), 1);

    EXPECT_TRUE( containsText( errPath(), mErr, "exclusive"));
    EXPECT_FALSE( std::filesystem::exists( mDir / "out.tsv"));
}

//
// Both failures are fatal before the rig is touched: a caller who asked to
// record a run and silently got none, or asked to replay one and silently got
// live hardware, has been told the run did something it did not.
//
TEST_F( AcceptanceRecording, AReplayFileThatCannotBeReadIsFatalAndRunsNothing)
{
    EXPECT_EQ( run( { "--replay=no-such-file.tsv", "--no-logs" }), 1);

    EXPECT_TRUE( containsText( errPath(), mErr, "Could not load the recording"));
    EXPECT_TRUE( omitsText( outPath(), mOut, "RESULT"));
}

//
// The unwritable path is built rather than hard-coded, because "a path no
// platform can write to" has no portable spelling: an absolute one like
// /nonexistent/readings.tsv is refused on POSIX but resolves to the current
// drive's root on Windows, where creating it may well succeed. A directory
// component that is an existing *file* cannot be created anywhere.
//
TEST_F( AcceptanceRecording, ARecordingPathThatCannotBeWrittenIsFatalAndRunsNothing)
{
    writeFile( mDir / "blocker", "not a directory\n");

    EXPECT_EQ( run( { "--record=blocker/readings.tsv", "--no-logs" }), 1);

    EXPECT_TRUE( containsText( errPath(), mErr, "Could not open the recording"));
    EXPECT_TRUE( omitsText( outPath(), mOut, "RESULT"));
}

// ---------------------------------------------------------------------------
// Authoring a replay: run_scripts --skeleton
// ---------------------------------------------------------------------------

namespace
{
    struct AcceptanceSkeleton : Acceptance {};
} // namespace

//
// What the mode is for. A script's session keys are produced by
// core::Port::qualifiedBy, by each engine's "<instrument>.<what>" rule and by
// the DUT adapter -- there is nowhere a person can read the complete list, so
// authoring a replay file by hand means reconstructing it from every script.
// This writes it out, in the order the scripts ask for it.
//
TEST_F( AcceptanceSkeleton, TheSkeletonListsEveryReadingTheScriptsAskFor)
{
    EXPECT_EQ( run( { "--skeleton=skeleton.tsv" }), 0);

    const auto tsv = readFile( mDir / "skeleton.tsv");

    // The whole span of the run: the RUN_SETUP hook's readbacks, then each seam.
    EXPECT_TRUE( containsText( mDir / "skeleton.tsv", tsv, "AcP1.A.Voltage\tAcP1\tVoltage"));
    EXPECT_TRUE( containsText( mDir / "skeleton.tsv", tsv, "Output5V\tDmm1\tVoltage"));
    EXPECT_TRUE( containsText( mDir / "skeleton.tsv", tsv, "Output5V.Vbase\tOsc1\tVoltage"));
    EXPECT_TRUE( containsText( mDir / "skeleton.tsv", tsv, "Osc1.Acquisition\tOsc1\t<flag>"));
    EXPECT_TRUE( containsText( mDir / "skeleton.tsv", tsv, "Ser1.Data\tSer1\t<bytes>"));
}

//
// The reason the mode overrides the RUN_SETUP verdict. rigPowerOn() reads
// back and concludes the rig is dead when they answer zero -- and stopping
// there would write a skeleton holding RUN_SETUP's six readings and none of
// ones the tests take, which is the opposite of what was asked for.
//
TEST_F( AcceptanceSkeleton, AFailingSetupDoesNotTruncateTheSkeleton)
{
    EXPECT_EQ( run( { "--skeleton=skeleton.tsv" }), 0);

    const auto tsv = readFile( mDir / "skeleton.tsv");

    // Thirteen reads: six from the hook, seven from the three scripts.
    EXPECT_EQ( std::ranges::count( tsv, '\n') - std::ranges::count( tsv, '#'), 13);
}

//
// It is a recording, not something shaped like one -- so the file it writes can
// be edited and handed straight back to --replay.
//
TEST_F( AcceptanceSkeleton, TheSkeletonIsAValidReplayFile)
{
    EXPECT_EQ( run( { "--skeleton=skeleton.tsv" }), 0);

    // No throw, no "could not load" -- the placeholders drive a real run, which
    // then fails its checks, because zero volts is not a working DUT.
    EXPECT_EQ( run( { "--replay=skeleton.tsv", "--quiet", "--no-logs" }), 1);

    EXPECT_TRUE( omitsText( errPath(), mErr, "Could not load"));
}

//
// A skeleton run tested nothing, so it leaves no evidence that it did. Same
// stance --safe takes: a mode that reports nothing gets no log to report it in.
//
TEST_F( AcceptanceSkeleton, ASkeletonRunWritesNoLogs)
{
    EXPECT_EQ( run( { "--skeleton=skeleton.tsv" }), 0);

    EXPECT_TRUE( omitsText( outPath(), mOut, "RESULT"));

    EXPECT_FALSE( std::filesystem::exists( mDir / "logs"));
}

//
// And its exit status is about the file, not about the DUT. Returning the
// verdicts of a run that read zeroes would tell a CI job something false about
// hardware that was never connected.
//
TEST_F( AcceptanceSkeleton, TheExitStatusReportsTheFileNotTheVerdicts)
{
    EXPECT_EQ( run( { "--skeleton=skeleton.tsv" }), 0);

    EXPECT_TRUE( containsText( outPath(), mOut, "placeholder"));
}

TEST_F( AcceptanceSkeleton, SkeletonIsExclusiveWithRecordAndReplay)
{
    EXPECT_EQ( run( { "--skeleton=out.tsv", "--record=rec.tsv" }), 1);
    EXPECT_TRUE( containsText( errPath(), mErr, "exclusive"));

    EXPECT_EQ( run( { "--skeleton=out.tsv", "--replay=in.tsv" }), 1);
    EXPECT_TRUE( containsText( errPath(), mErr, "exclusive"));

    EXPECT_FALSE( std::filesystem::exists( mDir / "out.tsv"));
}

// ---------------------------------------------------------------------------
// Described readings: run_scripts --inject
// ---------------------------------------------------------------------------

namespace
{
    struct AcceptanceInject : Acceptance {};

    //
    // A DUT that behaves, written out. The keys are what --skeleton lists; the
    // values are what a working unit would answer.
    //
    constexpr std::string_view kHealthyDut =
        "# authored, not captured\n"
        "AcP1.A.Voltage   = 115 V\n"
        "AcP1.B.Voltage   = 115 V\n"
        "AcP1.C.Voltage   = 115 V\n"
        "DcP1.Voltage     = 28 V\n"
        "DcP2.Voltage     = 28 V\n"
        "DcP3.Voltage     = 24 V\n"
        "Vout             = 12 V\n"
        "Output5V         = 5.01 V\n"
        "Output3V3        = 3.29 V\n"
        "Output5V.Vbase   = 5 V\n"
        "Osc1.Acquisition = true\n"
        "Output5V.Vmin    = 4.85 V\n"
        "Ser1.Data        = <41 43 4B 0D 08>\n";
} // namespace

TEST_F( AcceptanceInject, AnAuthoredFileDrivesTheWholeSuiteWithNoRig)
{
    writeFile( mDir / "healthy.stim", std::string( kHealthyDut));

    EXPECT_EQ( run( { "--inject=healthy.stim", "--quiet", "--no-logs" }), 0);
}

//
// The reason this file exists beside the recording format. Every value above is
// sticky, so the same thirteen lines answer a run of any length -- where a
// recording holds one value per read and needs fifty passes' worth of rows to
// survive --repeat=50.
//
TEST_F( AcceptanceInject, AStickyValueAnswersEveryPassOfARepeatedRun)
{
    writeFile( mDir / "healthy.stim", std::string( kHealthyDut));

    EXPECT_EQ( run( { "--inject=healthy.stim", "--repeat=3", "--quiet", "--no-logs" }), 0);
}

//
// And a list is the other half of it: it is how an authored file describes a
// DUT that misbehaves on one pass and not the others.
//
TEST_F( AcceptanceInject, AListAnswersOnePassEach)
{
    auto text = std::string( kHealthyDut);

    text.replace( text.find( "Output5V.Vmin    = 4.85 V"),
                  std::string( "Output5V.Vmin    = 4.85 V").size(),
                  "Output5V.Vmin    = 4.85 V, 4.70 V, 4.90 V");

    writeFile( mDir / "dips.stim", text);

    // The middle pass dips 0.3 V below a 5 V baseline, past the 0.2 V limit.
    EXPECT_EQ( run( { "--inject=dips.stim", "--repeat=3", "--no-logs" }), 1);

    EXPECT_TRUE( containsText( outPath(), mOut, "0.3 V"));
}

//
// Layering is why both flags exist: re-run a captured failure with one reading
// changed, and find out whether that reading was the cause.
//
TEST_F( AcceptanceInject, InjectLayersOverAReplayedRecording)
{
    //
    // Which is why both flags exist rather than one: --replay lays down a
    // captured run and --inject changes named readings within it, so a
    // recorded failure can be re-run with one value moved to find out whether
    // that value was the cause.
    //
    // The recording here is a skeleton -- every reading zero -- because that is
    // the one a test can produce without a rig. Replayed alone it fails at
    // RUN_SETUP; with the authored values layered on top it passes, which is the
    // override being asserted.
    //
    writeFile( mDir / "healthy.stim", std::string( kHealthyDut));

    EXPECT_EQ( run( { "--skeleton=zeros.tsv" }), 0);

    EXPECT_EQ( run( { "--replay=zeros.tsv", "--quiet", "--no-logs" }), 1);
    EXPECT_EQ( run( { "--replay=zeros.tsv", "--inject=healthy.stim", "--quiet", "--no-logs" }), 0);
}

TEST_F( AcceptanceInject, AStimulusFileThatCannotBeReadIsFatalAndRunsNothing)
{
    EXPECT_EQ( run( { "--inject=no-such-file.stim", "--no-logs" }), 1);

    EXPECT_TRUE( containsText( errPath(), mErr, "Could not read the stimulus file"));
    EXPECT_TRUE( omitsText( outPath(), mOut, "RESULT"));
}

//
// A typo is reported where it is, before anything is measured -- naming the
// line, its number and what is wrong with it, rather than surfacing three
// scripts later as a point nobody programmed.
//
TEST_F( AcceptanceInject, ABadLineNamesItselfAndRunsNothing)
{
    writeFile( mDir / "bad.stim", "Output5V = 5.01 V\nOutput3V3 = 3.3 Volts\n");

    EXPECT_EQ( run( { "--inject=bad.stim", "--no-logs" }), 1);

    EXPECT_TRUE( containsText( errPath(), mErr, "line 2"));
    EXPECT_TRUE( containsText( errPath(), mErr, "Volts"));
    EXPECT_TRUE( omitsText( outPath(), mOut, "RESULT"));
}

TEST_F( AcceptanceInject, InjectIsExclusiveWithTheModesThatWriteAFile)
{
    writeFile( mDir / "healthy.stim", std::string( kHealthyDut));

    EXPECT_EQ( run( { "--inject=healthy.stim", "--record=rec.tsv" }), 1);
    EXPECT_TRUE( containsText( errPath(), mErr, "exclusive"));

    EXPECT_EQ( run( { "--inject=healthy.stim", "--skeleton=sk.tsv" }), 1);
    EXPECT_TRUE( containsText( errPath(), mErr, "exclusive"));

    EXPECT_FALSE( std::filesystem::exists( mDir / "rec.tsv"));
    EXPECT_FALSE( std::filesystem::exists( mDir / "sk.tsv"));
}

// ---------------------------------------------------------------------------
// Whether a run reaches a bench at all
// ---------------------------------------------------------------------------

namespace
{
    struct AcceptanceBench : Acceptance {};
} // namespace

//
// The hole this closes, seen from outside. --replay took every reading from the
// file and then went on energising rails and closing relays for real, which was
// survivable only because every driver here is still simulated. Now the machine
// log says, per instruction, that it went nowhere -- and a run that says that is
// a run in which no driver was called (see framework/core/tests/test_bench.cpp, which
// asserts the driver side of the same claim against a counting mock).
//
TEST_F( AcceptanceBench, AReplayedRunInstructsNothing)
{
    EXPECT_EQ( run( { "--skeleton=zeros.tsv" }), 0);
    EXPECT_EQ( run( { "--replay=zeros.tsv", "--quiet" }), 1);

    const auto sarifPath = findArtifact( ".sarif");

    ASSERT_FALSE( sarifPath.empty());

    const auto sarif = readFile( sarifPath);

    EXPECT_TRUE( containsText( sarifPath, sarif, "not performed -- no bench attached"));
}

TEST_F( AcceptanceBench, AnInjectedRunInstructsNothing)
{
    writeFile( mDir / "healthy.stim", std::string( kHealthyDut));

    EXPECT_EQ( run( { "--inject=healthy.stim", "--quiet" }), 0);

    const auto sarifPath = findArtifact( ".sarif");

    ASSERT_FALSE( sarifPath.empty());

    const auto sarif = readFile( sarifPath);

    //
    // Every one of them, not merely one somewhere: a mode that silenced the
    // supplies and still drove the scope would be the worst of both.
    //
    for( const auto * verb : { "Apply", "Connect", "Setup", "Write", "Arm", "Remove", "Disconnect" })
    {
        EXPECT_TRUE( containsText( sarifPath, sarif, std::string( verb) + " ")) << verb;
    }

    EXPECT_TRUE( omitsText( sarifPath, sarif, "\"text\": \"Apply AcP1 = 3-phase, phaseVoltage=115 V, frequency=400 Hz, currentLimit=2 A\""));
}

//
// A live run is unchanged -- the default is attached, and this is what proves
// the marker is not simply always on.
//
TEST_F( AcceptanceBench, AnOrdinaryRunStillInstructsTheRig)
{
    EXPECT_EQ( run( { "--quiet" }), 1);

    const auto sarifPath = findArtifact( ".sarif");

    ASSERT_FALSE( sarifPath.empty());

    const auto sarif = readFile( sarifPath);

    EXPECT_TRUE( containsText( sarifPath, sarif, "Apply AcP1"));
    EXPECT_TRUE( omitsText(    sarifPath, sarif, "no bench attached"));
}

//
// Spelled out in the traceability header, not left to be worked out from the
// command line two rows below it. A detached run's checks can all pass, and
// what they passed about is a file -- so the person holding the report is told,
// rather than being expected to notice a flag.
//
TEST_F( AcceptanceBench, TheReportHeaderSaysWhetherARigWasThere)
{
    EXPECT_EQ( run( { "--skeleton=zeros.tsv" }), 0);
    EXPECT_EQ( run( { "--replay=zeros.tsv" }), 1);

    EXPECT_TRUE( containsText( outPath(), mOut, "Bench"));
    EXPECT_TRUE( containsText( outPath(), mOut, "DETACHED -- no instrument was touched"));

    const auto rtfPath = findArtifact( ".rtf");

    ASSERT_FALSE( rtfPath.empty());

    EXPECT_TRUE( containsText( rtfPath, readFile( rtfPath), "DETACHED -- no instrument was touched"));
}

TEST_F( AcceptanceBench, AnOrdinaryRunsHeaderSaysTheBenchWasThere)
{
    EXPECT_EQ( run( {} ), 1);

    EXPECT_TRUE( containsText( outPath(), mOut, "Bench             attached"));
    EXPECT_TRUE( omitsText(    outPath(), mOut, "DETACHED"));
}

//
// The one call in a detached run that most looks like it should happen anyway,
// and most must not. Safing an unattached bench would be the single instruction
// that did reach real hardware -- opening the relays of whatever rig the runner
// happened to be pointed at, on behalf of a run that never touched it.
//
TEST_F( AcceptanceBench, ADetachedRunDoesNotSafeTheRigOnTheWayOut)
{
    EXPECT_EQ( run( { "--skeleton=zeros.tsv" }), 0);
    EXPECT_EQ( run( { "--replay=zeros.tsv", "--quiet" }), 1);

    const auto sarifPath = findArtifact( ".sarif");

    ASSERT_FALSE( sarifPath.empty());

    const auto sarif = readFile( sarifPath);

    // The Safe event is still posted -- what happened is still in the log --
    // and says it did nothing.
    EXPECT_TRUE( containsText( sarifPath, sarif, "Safe rig -- not performed -- no bench attached"));
    EXPECT_TRUE( omitsText(    sarifPath, sarif, "all instrument outputs off and zeroed"));
}
