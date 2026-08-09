#include <algorithm>
#include <exception>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/active_test_catalog.hpp"
#include "core/console_sink.hpp"
#include "core/journal.hpp"
#include "core/rtf_sink.hpp"
#include "core/sarif_sink.hpp"
#include "hal/safing.hpp"

//
// Runner for the test-script catalog (core/active_test_catalog.hpp). Four
// modes, matching what tools/run-tests.sh expects:
//
//   run_scripts                    run every test in the catalog
//   run_scripts --list-tests       print "group|id|description", one per
//                                  test, then exit -- nothing is run
//   run_scripts --select=a,b,c     run only the named test ids (from any
//                                  group), in catalog order
//   run_scripts --safe             drop the rig to a known idle state and
//                                  exit -- no test is run, nothing is
//                                  measured, nothing is reported
//
// ids are only ever compared for a match against what's already in the
// catalog -- never parsed into anything -- so a typo in --select just
// means that test doesn't run, not a crash or an unintended one.
//
// --safe exists for a caller outside this process. The rig console
// supervises a run as a child process, and on an abnormal exit it cannot
// reach into the dead process's hal::fabric to open anything -- so it
// re-invokes this same binary with --safe instead, which is why the mode
// lives here rather than in a separate tool. Using the suite binary itself
// matters: it was built against this rig's exact instrument list (see
// rig/instrument.inc), so it needs no independent description of what
// instruments exist to safe, and cannot disagree with the run it is
// cleaning up after.
//
// Deliberately exclusive with the other three, and checked before them:
// --safe is what you pass when something has already gone wrong, so
// combining it with a run would mean choosing whether to safe before or
// after testing -- neither of which is what the flag means.
//
// A normal run (no flag, or --select) safes the rig too, on every way out
// of runTests() below -- completing, or a script throwing -- via
// hal::RigSafingGuard (see hal/safing.hpp). That's a different situation
// from --safe's: this process is still alive and its stack is still
// intact, so there's a scope for a guard to run at the end of, rather
// than a dead process a supervisor has to clean up after from the
// outside.
//
// ---------------------------------------------------------------------------
// Logging
// ---------------------------------------------------------------------------
// A run produces two logs, from one event stream (see core/journal.hpp):
//
//   --sarif=PATH   machine-readable, SARIF 2.1.0, every verb the run executed
//                  (Measure, Apply, Remove, Connect, Disconnect, Verify, and
//                  the safing pass) -- core/sarif_sink.hpp
//   --rtf=PATH     human-readable, colour-coded, Measure and Verify only, with
//                  each test's name and verdict marked out -- core/rtf_sink.hpp
//
// Both carry the same traceability header: framework version, criteria variant,
// DUT and rig name, operator, host, start time, command line (see
// core::RunInfo). The RTF file is a valid, openable document after every logged
// event rather than only at the end, so it can be read while the run is still
// going (see core::RtfSink on how) -- and the same human content is written
// live to the terminal by core::ConsoleSink, which is what an operator at the
// bench actually watches.
//
// Both files are written by default, under --log-dir, named for the run's start
// time. That is deliberate rather than opt-in: a test run whose record depends
// on somebody having remembered a flag is a test run with no record.
//
namespace
{
    //
    // Everything the command line can set. Held as one struct so parsing and
    // using it are separable -- notably so --list-tests and --safe can be
    // handled before any log file is created, since neither runs a test and
    // neither should leave a log claiming one happened.
    //
    struct Options
    {
        std::vector<std::string_view>  Selection;      // empty => run everything
        bool                           ListOnly{ false };
        bool                           SafeOnly{ false };

        std::string                    LogDir{ "logs" };
        std::optional<std::string>     SarifPath;      // unset => derived from LogDir
        std::optional<std::string>     RtfPath;
        bool                           WriteLogs{ true };

        bool                           Colour{ true };
        bool                           Quiet{ false };  // no live console view

        std::string                    DutSerial;
        std::string                    OperatorName;
    };

    auto splitCommaList( std::string_view csv) -> std::vector<std::string_view>
    {
        std::vector<std::string_view> parts;
        std::size_t                   start = 0;

        while ( start <= csv.size())
        {
            auto comma = csv.find( ',', start);
            auto end   = (comma == std::string_view::npos) ? csv.size() : comma;

            if ( end > start)
                parts.push_back( csv.substr( start, end - start));

            if ( comma == std::string_view::npos)
                break;

            start = comma + 1;
        }

        return parts;
    }

    auto isSelected( std::string_view id, const std::vector<std::string_view> & selection) -> bool
    {
        return selection.empty() || std::find( selection.begin(), selection.end(), id) != selection.end();
    }

    void listTests()
    {
        for ( const auto & group : core::catalog::Catalog)
            for ( const auto & test : group.tests)
                std::cout << group.name << '|' << test.id << '|' << test.description << '\n';
    }

    auto valueOf( const std::string_view arg, const std::string_view prefix) -> std::string_view
    {
        return arg.substr( prefix.size());
    }

    //
    // Returns std::nullopt on an unrecognised argument, having already reported
    // it -- the same "unknown argument is a hard failure" behaviour this runner
    // has always had, kept because a mistyped flag silently ignored is a run
    // that didn't do what was asked.
    //
    auto parseOptions( const int argc, char ** argv) -> std::optional<Options>
    {
        Options options;

        for ( int i = 1; i < argc; ++i)
        {
            const std::string_view arg = argv[ i];

            if ( arg == "--list-tests")
                options.ListOnly = true;
            else if ( arg == "--safe")
                options.SafeOnly = true;
            else if ( arg == "--no-color" || arg == "--no-colour")
                options.Colour = false;
            else if ( arg == "--quiet")
                options.Quiet = true;
            else if ( arg == "--no-logs")
                options.WriteLogs = false;
            else if ( arg.starts_with( "--select="))
                options.Selection = splitCommaList( valueOf( arg, "--select="));
            else if ( arg.starts_with( "--log-dir="))
                options.LogDir = std::string( valueOf( arg, "--log-dir="));
            else if ( arg.starts_with( "--sarif="))
                options.SarifPath = std::string( valueOf( arg, "--sarif="));
            else if ( arg.starts_with( "--rtf="))
                options.RtfPath = std::string( valueOf( arg, "--rtf="));
            else if ( arg.starts_with( "--dut-serial="))
                options.DutSerial = std::string( valueOf( arg, "--dut-serial="));
            else if ( arg.starts_with( "--operator="))
                options.OperatorName = std::string( valueOf( arg, "--operator="));
            else
            {
                std::cerr << "Unknown argument: " << arg << '\n';
                return std::nullopt;
            }
        }

        return options;
    }

    auto commandLineOf( const int argc, char ** argv) -> std::string
    {
        std::string line;

        for ( int i = 0; i < argc; ++i)
        {
            if ( i > 0)
                line += ' ';

            line += argv[ i];
        }

        return line;
    }

    //
    // Makes sure path's directory exists before a sink tries to open it.
    // Applied to explicitly-given --sarif=/--rtf= paths as well as to the
    // derived ones under --log-dir: "the directory I named doesn't exist yet"
    // is not a reason to refuse to log, and creating it is what the caller
    // meant either way.
    //
    auto ensureParentDirectory( const std::filesystem::path & path) -> void
    {
        if ( const auto parent = path.parent_path(); !parent.empty())
            std::filesystem::create_directories( parent);
    }

    //
    // A filename-safe stamp derived from the run's own start time, so the two
    // logs for one run share a name and sort chronologically in a directory of
    // them. Built from the RunInfo's ISO timestamp rather than by asking the
    // clock again -- the log's contents and its filename must agree about when
    // the run started.
    //
    auto fileStamp( std::string_view isoUtc) -> std::string
    {
        std::string stamp;
        stamp.reserve( isoUtc.size());

        for ( const char c : isoUtc)
        {
            if ( c == ':' || c == '-' || c == '.')
                continue;

            if ( c == 'Z')
                break;

            stamp += ( c == 'T') ? '-' : c;
        }

        return stamp;
    }

    // Whether any of this group's tests are in the selection at all -- see
    // runTests below on why a group has to be asked before it is opened.
    auto anySelected( const core::TestGroup & group, const std::vector<std::string_view> & selection) -> bool
    {
        for ( const auto & test : group.tests)
            if ( isSelected( test.id, selection))
                return true;

        return false;
    }

    //
    // Runs the selected tests, bracketing each group and each test with the
    // journal's own boundaries. Those calls are what make the logs
    // catalog-aware: everything a script posts inside them is attributed to
    // that group and test, so neither the script nor Measure nor Verify has to
    // know its own test's name (they can't -- a script takes no parameters at
    // all, see core/test_catalog.hpp on why), and the human log can state each
    // group once with its description and nest its tests under it.
    //
    // The group is opened only if something in it is actually going to run.
    // This is the one place that can know: the selection lives here, and a
    // --select naming one test would otherwise produce a log full of headings
    // for groups that contributed nothing.
    //
    auto runTests( const std::vector<std::string_view> & selection) -> bool
    {
        bool allPassed = true;
        bool ranAny    = false;

        for ( const auto & group : core::catalog::Catalog)
        {
            if ( !anySelected( group, selection))
                continue;

            core::journal().beginGroup( group.name, group.description);

            for ( const auto & test : group.tests)
            {
                if ( !isSelected( test.id, selection))
                    continue;

                ranAny = true;

                core::journal().beginTest( test.id, test.description);

                //
                // endTest is reached on the normal path only. A script that
                // throws unwinds straight past it to main's handler, which ends
                // the run as a failure -- deliberately not wrapped in a
                // try/catch here that would swallow it into a per-test failure
                // and carry on: an exception out of a script means the rig is in
                // an unknown state, which is precisely what hal::RigSafingGuard
                // and the --safe path exist for.
                //
                const bool passed = test.script();

                allPassed &= passed;

                core::journal().endTest( passed);
            }

            core::journal().endGroup();
        }

        if ( !ranAny)
        {
            std::cerr << "No catalog test matched --select; nothing ran.\n";
            return false;
        }

        return allPassed;
    }
} // namespace

int main( int argc, char ** argv)
{
    const auto parsed = parseOptions( argc, argv);

    if ( !parsed)
        return 1;

    const auto & options = *parsed;

    //
    // Checked before --list-tests, and before any script runs -- see this
    // file's own comment on why --safe is exclusive with the other modes.
    // Always exits 0: safing is unconditional and has nothing to report a
    // failure about, and a console invoking this after a child crash has
    // no useful way to act on a non-zero exit here anyway.
    //
    // No logs, and no journal sinks: this mode reports nothing (see the mode
    // list above), so the Safe event hal::safeRig() posts has nowhere to go and
    // is discarded. That is correct rather than a gap -- this process is
    // cleaning up after a *different* run, and the log it would append to
    // belongs to that dead run, not to this one.
    //
    if ( options.SafeOnly)
    {
        hal::safeRig();
        std::cout << "Rig safed: all outputs off, all relays open.\n";
        return 0;
    }

    if ( options.ListOnly)
    {
        listTests();
        return 0;
    }

    //
    // --- Traceability header, and the sinks that carry it ---
    // Assembled before anything is measured, so the logs describe the run that
    // is about to happen rather than being reconstructed afterwards.
    //
    auto runInfo = core::defaultRunInfo();

    runInfo.CommandLine = commandLineOf( argc, argv);
    runInfo.DutSerial   = options.DutSerial;

    if ( !options.OperatorName.empty())
        runInfo.Operator = options.OperatorName;   // explicit beats the environment's guess

    //
    // Sinks are locals whose scope encloses the entire run *and* the
    // journal().end() call below: each file sink's destructor is its last-resort
    // close (see core::SarifSink's and core::RtfSink's own comments), so their
    // lifetime has to outlive the last event either could receive.
    //
    std::optional<core::ConsoleSink>  console;
    std::optional<core::SarifSink>    sarif;
    std::optional<core::RtfSink>      rtf;

    if ( !options.Quiet)
    {
        console.emplace( std::cout, options.Colour);
        core::journal().add( *console);
    }

    if ( options.WriteLogs)
    {
        try
        {
            const auto stamp = fileStamp( runInfo.StartedUtc);
            const auto dir   = std::filesystem::path( options.LogDir);

            const auto sarifPath = std::filesystem::path( options.SarifPath.value_or( ( dir / ( "thorium-" + stamp + ".sarif")).string()));
            const auto rtfPath   = std::filesystem::path( options.RtfPath.value_or(   ( dir / ( "thorium-" + stamp + ".rtf")).string()));

            ensureParentDirectory( sarifPath);
            ensureParentDirectory( rtfPath);

            sarif.emplace( sarifPath.string());
            rtf.emplace(   rtfPath.string());

            core::journal().add( *sarif);
            core::journal().add( *rtf);
        }
        catch ( const std::exception & e)
        {
            //
            // Fatal, not a warning: the caller asked for a logged run, and a
            // run that quietly produced no record is worse than one that
            // refused to start -- the rig hasn't been touched yet at this
            // point, so refusing costs nothing.
            //
            std::cerr << "Could not open run logs: " << e.what() << '\n';
            return 1;
        }
    }

    core::journal().begin( runInfo);

    bool allPassed = false;

    {
        // Guard's scope wraps the run and nothing else -- --list-tests and
        // --safe above have already returned, and neither one touches an
        // instrument or the fabric, so there is nothing for this run to
        // safe on their behalf.
        //
        // Inside journal().begin()/end() rather than outside, so the Safe event
        // this guard's destructor produces (see hal/src/safing.cpp) lands in
        // the machine log for the run it belongs to.
        hal::RigSafingGuard safeOnExit;

        try
        {
            allPassed = runTests( options.Selection);
        }
        catch ( const std::exception & e)
        {
            // Caught here, not left to escape main(): whether the stack
            // unwinds for an exception nobody catches is unspecified, and
            // safeOnExit's destructor running is the entire point of this
            // block. Reported and turned into a failing exit instead --
            // same non-zero result a caller already gets from allPassed
            // being false.
            //
            // Recorded as a journal Note as well as printed, so the machine log
            // says *why* a run stopped where it did rather than simply ending
            // mid-test. Deliberately not rethrown or returned from here: the
            // run still has to be closed out below, which is what gets both
            // logs written.
            std::cerr << "Uncaught exception during test run: " << e.what() << '\n';

            core::journal().post( core::JournalRecord{
                .Method  = core::Verb::Note,
                .Subject = "run",
                .Detail  = std::string( "uncaught exception during test run: ") + e.what()
            });

            allPassed = false;
        }
    }

    core::journal().end( allPassed);

    return allPassed ? 0 : 1;
}
