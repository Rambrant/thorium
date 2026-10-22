#pragma once
//
// The harness for black-box tests of a built run_scripts: run it as a
// subprocess, keep every artifact it produced, and assert on what came out.
//
// Shared rather than owned by one test file because two kinds of test want it
// and neither can borrow it from the other. The framework's own acceptance
// target (framework/runner/tests/acceptance/) drives run_scripts_hooked, the
// runner over a fixture catalog, to pin the runner's own behaviour; a
// deployment's acceptance directory drives that deployment's run_scripts, to pin
// what it says about its own suite, DUT and rig. Both need the same
// subprocess, quoting, artifact and log-reading machinery, and a DUT
// repository that has pinned a framework version gets exactly the harness that
// version's own tests were written with.
//
// What a target defines, as compile definitions (all optional -- a missing one is
// an empty path, and the fixture only insists on the binaries it was given):
//
//     THORIUM_RUN_SCRIPTS_EXE         the deployment's run_scripts
//     THORIUM_RUN_SCRIPTS_HOOKED_EXE  the runner over the hook-ordering fixture
//     THORIUM_ACCEPTANCE_OUTPUT_DIR   where each scenario keeps its artifacts
//
// A test file brings the names into scope with
//
//     using namespace thorium_acceptance;
//
// and derives its fixtures from thorium_acceptance::Acceptance. See
// acceptance/README.md for what the artifacts are and why they are kept, and
// acceptance/test_acceptance.cpp for the header comment on why this is the one
// platform-dependent code in the tree.
//
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

#ifndef _WIN32
    //
    // POSIX, for reading a child's exit status out of what std::system() hands
    // back (WIFEXITED/WEXITSTATUS). Windows has no equivalent because it needs
    // none -- see runBinary() on what std::system() returns there.
    //
    #include <sys/wait.h>
#endif

namespace thorium_acceptance
{
    //
    // Both supplied by framework/runner/CMakeLists.txt: the built binary's real path (via a
    // $<TARGET_FILE:...> generator expression, so this works under any
    // generator or build layout) and where to keep artifacts.
    //
    #ifdef THORIUM_RUN_SCRIPTS_EXE
    inline constexpr std::string_view kRunScripts = THORIUM_RUN_SCRIPTS_EXE;
#else
    inline constexpr std::string_view kRunScripts = "";
#endif
    inline constexpr std::string_view kOutputRoot = THORIUM_ACCEPTANCE_OUTPUT_DIR;

    //
    // The same runner over a *fixture* catalog whose hooks announce themselves
    // on stdout and can be made to fail on demand -- which the shipped suite's
    // real hooks cannot (see the AcceptanceHooks section below for why that
    // still matters now that the shipped catalog declares both). See
    // framework/runner/CMakeLists.txt and framework/runner/tests/fixtures/.
    //
    #ifdef THORIUM_RUN_SCRIPTS_HOOKED_EXE
    inline constexpr std::string_view kRunScriptsHooked = THORIUM_RUN_SCRIPTS_HOOKED_EXE;
#else
    inline constexpr std::string_view kRunScriptsHooked = "";
#endif

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
    inline auto readFile( const std::filesystem::path & path) -> std::string
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

    inline auto writeFile( const std::filesystem::path & path, const std::string & text) -> void
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
    inline auto shellQuoted( const std::string_view text) -> std::string
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
    inline auto nativePath( const std::string_view path) -> std::string
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
    inline auto countOccurrences( const std::string & text, const std::string_view needle) -> std::size_t
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
    inline auto recordedRows( const std::string & recording) -> std::size_t
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

    inline auto containsText( const std::filesystem::path & artifact, const std::string & text, const std::string_view needle) -> ::testing::AssertionResult
    {
        if( text.find( needle) != std::string::npos)
        {
            return ::testing::AssertionSuccess();
        }

        return ::testing::AssertionFailure()
            << "expected to find \"" << needle << "\"\n  in: " << artifact.string();
    }

    inline auto omitsText( const std::filesystem::path & artifact, const std::string & text, const std::string_view needle) -> ::testing::AssertionResult
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
    inline auto looksComplete( const std::string & json) -> bool
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

                //
                // Only the binaries this target was given: the framework's own
                // acceptance target drives run_scripts_hooked and has no
                // run_scripts, a deployment's has both.
                //
                if( !kRunScripts.empty())
                {
                    ASSERT_TRUE( std::filesystem::exists( kRunScripts))
                        << "run_scripts not built: " << kRunScripts;
                }
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
    // The test ids the console reported a verdict for, in the order it
    // reported them -- which is the only way to see from outside whether
    // --repeat repeated the selection or repeated each script. Read off the
    // "RESULT <id>" lines core::ConsoleSink writes per test.
    //
    [[nodiscard]]
    inline auto verdictOrder( const std::string & console) -> std::vector<std::string>
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
} // namespace thorium_acceptance
