#include <algorithm>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "cli.hpp"
#include "core/active_test_catalog.hpp"
#include "core/console_sink.hpp"
#include "core/criteria_variants.hpp"
#include "core/journal.hpp"
#include "core/rtf_sink.hpp"
#include "core/sarif_sink.hpp"
#include "hal/measure.hpp"
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
// That is the shape of the thing. For the flags themselves -- every spelling,
// what each takes, one line on what each does -- run `run_scripts --help`, or
// read the Options struct below, which is the single place they are declared and
// what --help is generated from. The sections that follow here deliberately do
// not restate that list: they explain the handful of choices whose *reasoning*
// does not fit on a help line.
//
// ---------------------------------------------------------------------------
// Choosing the tolerances
// ---------------------------------------------------------------------------
//   --criteria=NAME   run against that tolerance variant
//
// Every variant the deployment declares (THORIUM_KNOWN_CRITERIA_VARIANTS -- e.g.
// production, stress, aged) is compiled into this one binary, and this is what
// picks between them; without the flag, the variant the build was configured
// for applies (core::defaultCriteriaVariantName()). See suite/README.md for the
// mechanism and core/criteria_variants.hpp for the seam.
//
// An unrecognised name is fatal, and lists the ones that would have worked --
// same stance as an unknown flag, and for a stronger reason: a runner that
// quietly fell back to the default would apply the wrong tolerances to real
// hardware and produce a log that looks entirely normal.
//
// The choice is made before the journal opens and is frozen once it does, so
// the variant named in both logs' traceability header is provably the one every
// check in them was made against.
//
// ---------------------------------------------------------------------------
// Repeating a run
// ---------------------------------------------------------------------------
//   --repeat=N        run the selection N times over
//   --until-failure   stop as soon as a pass fails
//
// What repeats is the *selection*, not each script: --select=A,B --repeat=3
// runs A B A B A B, never A A A B B B. That is the unit a soak run cares
// about -- "the tests, again" -- and it is also the unit SETUP/TEARDOWN
// bracket, which they could not be if each script repeated on its own.
//
// The two combine as a bound and a stopping condition. --until-failure alone
// has no bound and runs until something fails, which is the point of it: how
// many passes a DUT survives is what the run is trying to find out, so
// requiring that number up front would be requiring the answer. With
// --repeat=N it stops at N passes or at the first failure, whichever comes
// first.
//
// A repeated run is one run, with one report and one exit status: the passes
// are marked in the log (see runTests below) but they are not separate runs,
// and any failing pass fails the whole thing.
//
// ---------------------------------------------------------------------------
// Bracketing a run
// ---------------------------------------------------------------------------
// A catalog may declare SETUP and TEARDOWN (see core/test_catalog.hpp) --
// typically powering the rig up and back down. They run once each, around
// everything, including every repeat pass. Neither is required; a catalog
// declaring neither behaves exactly as it did before they existed.
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
// ---------------------------------------------------------------------------
// Recording and replaying the readings
// ---------------------------------------------------------------------------
//   --record=PATH   write every reading the run took, in order
//   --replay=PATH   take every reading from that file instead of the rig
//
// A third artifact, and the only one that is an input as well as an output (see
// core/recording.hpp for the format). Where the two logs above describe a run --
// for a person, and for a server -- this is the readings themselves, so the same
// run can be played back afterwards with no rig attached: to reproduce a failure
// at a desk, to step through what a script actually saw, or to re-run a suite
// against readings captured from hardware that is no longer on the bench.
//
// Opt-in, unlike the logs, and for the opposite reason to theirs. A log is
// evidence that a run happened and every run should leave one; a recording is a
// tool for a particular investigation, is as long as the run is (a fifty-pass
// soak writes fifty passes' worth), and says nothing a report needs.
//
// Exclusive with each other: recording a replay would faithfully write out the
// values it had just been fed, handing back a file that looks like a fresh
// capture and is a copy of its input.
//
// Both are set up before the journal opens and before anything is measured, and
// both are fatal if they cannot be -- see main() below on why that beats
// discovering an unwritable path once the readings are already taken.
//
namespace
{
    //
    // Everything the command line can set. Held as one struct so parsing and
    // using it are separable -- notably so --list-tests and --safe can be
    // handled before any log file is created, since neither runs a test and
    // neither should leave a log claiming one happened.
    //
    // The annotations are what the parser and --help are generated from (see
    // cli.hpp): a member's Flag gives its spelling, its Doc gives its --help
    // line, and its *type* decides whether it takes a value and how that value
    // is read. A flag therefore exists in exactly one place. Adding one is
    // adding a member here; there is no parser branch and no help text to keep
    // in step with it.
    //
    struct Options
    {
        [[= cli::Flag{ "--select" }, = cli::Meta{ "ID[,ID...]" },
           = cli::Doc{ "run only the named test ids, in catalog order" }]]
        std::vector<std::string_view>  Selection;      // empty => run everything

        [[= cli::Flag{ "--list-tests" },
           = cli::Doc{ "print \"group|id|description\" per test and exit" }]]
        bool                           ListOnly{ false };

        [[= cli::Flag{ "--safe" },
           = cli::Doc{ "drop the rig to a known idle state and exit" }]]
        bool                           SafeOnly{ false };

        [[= cli::Flag{ "--help" }, = cli::Doc{ "print this list and exit" }]]
        bool                           ShowHelp{ false };

        //
        // Which tolerance variant to apply. Unset means the one this build was
        // configured for -- deliberately not resolved to a name here, so that
        // "the caller said nothing" and "the caller happened to name the
        // default" stay distinguishable right up to the point of use.
        //
        [[= cli::Flag{ "--criteria" }, = cli::Meta{ "NAME" },
           = cli::Doc{ "run against that tolerance variant" }]]
        std::optional<std::string_view>  CriteriaVariant;

        //
        // How many times to run the selection. Unset is not the same as 1: on
        // its own it means once, but with UntilFailure it means "keep going",
        // which is what lets --until-failure be useful without having to guess
        // a pass count up front. See passCount() below.
        //
        [[= cli::Flag{ "--repeat" }, = cli::Meta{ "N" }, = cli::Positive{ "passes" },
           = cli::Doc{ "run the selection N times over" }]]
        std::optional<std::uint64_t>   Repeat;

        [[= cli::Flag{ "--until-failure" },
           = cli::Doc{ "stop as soon as a pass fails" }]]
        bool                           UntilFailure{ false };

        //
        // The replayable value stream (core/recording.hpp), separate from the
        // two report logs below: those describe a run for a person and for a
        // server, this one is the readings themselves, in order, so the same
        // run can be played back with no rig attached.
        //
        [[= cli::Flag{ "--record" }, = cli::Meta{ "PATH" },
           = cli::Doc{ "write the reading stream to PATH" }]]
        std::optional<std::string>     RecordPath;

        [[= cli::Flag{ "--replay" }, = cli::Meta{ "PATH" },
           = cli::Doc{ "read readings from PATH instead of the rig" }]]
        std::optional<std::string>     ReplayPath;

        [[= cli::Flag{ "--log-dir" }, = cli::Meta{ "DIR" },
           = cli::Doc{ "where both run logs are written (default: logs)" }]]
        std::string                    LogDir{ "logs" };

        [[= cli::Flag{ "--sarif" }, = cli::Meta{ "PATH" },
           = cli::Doc{ "SARIF log path (default: derived from --log-dir)" }]]
        std::optional<std::string>     SarifPath;      // unset => derived from LogDir

        [[= cli::Flag{ "--rtf" }, = cli::Meta{ "PATH" },
           = cli::Doc{ "RTF log path (default: derived from --log-dir)" }]]
        std::optional<std::string>     RtfPath;

        [[= cli::Flag{ "--no-logs" }, = cli::Clears{},
           = cli::Doc{ "write no run log at all" }]]
        bool                           WriteLogs{ true };

        [[= cli::Flag{ "--no-color" }, = cli::Flag{ "--no-colour" }, = cli::Clears{},
           = cli::Doc{ "no ANSI colour in the console view" }]]
        bool                           Colour{ true };

        [[= cli::Flag{ "--quiet" }, = cli::Doc{ "no live console view" }]]
        bool                           Quiet{ false };  // no live console view

        [[= cli::Flag{ "--dut-serial" }, = cli::Meta{ "SERIAL" },
           = cli::Doc{ "DUT serial, recorded in both logs' header" }]]
        std::string                    DutSerial;

        [[= cli::Flag{ "--operator" }, = cli::Meta{ "NAME" },
           = cli::Doc{ "operator name, recorded in both logs' header" }]]
        std::string                    OperatorName;
    };

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

    //
    // Returns std::nullopt on an unrecognised argument, having already reported
    // it -- the same "unknown argument is a hard failure" behaviour this runner
    // has always had, kept because a mistyped flag silently ignored is a run
    // that didn't do what was asked.
    //
    // The per-flag half of that -- which spellings exist, which take a value,
    // how each value is read -- is generated from the Options annotations by
    // cli::parse. What stays here is everything that is not a property of one
    // flag on its own: the two checks below. Neither could be an annotation
    // without inventing a way to write "this flag and that flag together" down,
    // and both are short enough that spelling them out is clearer than the
    // machinery that would be needed to declare them.
    //
    auto parseOptions( const int argc, char ** argv) -> std::optional<Options>
    {
        auto parsed = cli::parse<Options>( argc, argv, std::cerr);

        if ( !parsed)
            return std::nullopt;

        const auto & options = *parsed;

        //
        // Rejected rather than allowed to mean something surprising. Recording
        // a replay would faithfully write out the values it was just fed, so
        // the run would succeed and produce a file that looks like a fresh
        // capture and is a copy of the input -- which is exactly the kind of
        // quietly-wrong artifact a bench run must not hand back.
        //
        if ( options.RecordPath && options.ReplayPath)
        {
            std::cerr << "--record= and --replay= are exclusive: recording a replay would just copy its input.\n";
            return std::nullopt;
        }

        //
        // Applied here rather than at the point of use, for the same reason the
        // checks above are rejected here: this is the last moment at which
        // nothing has happened yet. core::selectCriteriaVariant validates and
        // applies in one step -- it owns the list of legal names (generated
        // from THORIUM_KNOWN_CRITERIA_VARIANTS, see core/criteria_variants.hpp),
        // so there is no second copy of it here to fall out of step with it.
        //
        // Not applying anything when the flag is absent is what leaves the
        // build's own default in force.
        //
        if ( options.CriteriaVariant && !core::selectCriteriaVariant( *options.CriteriaVariant))
        {
            std::cerr << "Unknown criteria variant: " << *options.CriteriaVariant << "\nKnown variants:";

            for ( const auto & name : core::criteriaVariantNames())
                std::cerr << ' ' << name;

            std::cerr << " (default: " << core::defaultCriteriaVariantName() << ")\n";

            return std::nullopt;
        }

        return parsed;
    }

    //
    // Generated from the Options annotations, so this cannot drift from the
    // flags the parser actually accepts -- adding a member with a Flag and a Doc
    // is what makes it appear here. The body is assembled at compile time and
    // baked into the binary as one string; nothing is walked at runtime.
    //
    void printUsage()
    {
        std::cout << "usage: run_scripts [options]\n\n"
                  << cli::usageText<Options>()
                  << "\nWith no options, every test in the catalog is run.\n";
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
    // One pass over the selection: every selected test, once, in catalog order.
    //
    // Bracketed with the journal's own group/test boundaries. Those calls are
    // what make the logs catalog-aware: everything a script posts inside them
    // is attributed to that group and test, so neither the script nor Measure
    // nor Verify has to know its own test's name (they can't -- a script takes
    // no parameters at all, see core/test_catalog.hpp on why), and the human
    // log can state each group once with its description and nest its tests
    // under it.
    //
    // The group is opened only if something in it is actually going to run.
    // This is the one place that can know: the selection lives here, and a
    // --select naming one test would otherwise produce a log full of headings
    // for groups that contributed nothing.
    //
    auto runOnePass( const std::vector<std::string_view> & selection) -> bool
    {
        bool allPassed = true;

        for ( const auto & group : core::catalog::Catalog)
        {
            if ( !anySelected( group, selection))
                continue;

            core::journal().beginGroup( group.name, group.description);

            for ( const auto & test : group.tests)
            {
                if ( !isSelected( test.id, selection))
                    continue;

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

        return allPassed;
    }

    // Whether the selection names anything this catalog actually has.
    auto anythingSelected( const std::vector<std::string_view> & selection) -> bool
    {
        for ( const auto & group : core::catalog::Catalog)
            if ( anySelected( group, selection))
                return true;

        return false;
    }

    //
    // How many passes to make over the selection. Unbounded is a real answer,
    // not a fallback: --until-failure with no --repeat means "keep running this
    // until something breaks", which is a soak run whose length is decided by
    // the DUT rather than by the caller.
    //
    auto passCount( const Options & options) -> std::uint64_t
    {
        if ( options.Repeat)
            return *options.Repeat;

        return options.UntilFailure ? std::numeric_limits<std::uint64_t>::max() : 1;
    }

    //
    // Calls a hook if the catalog declared one. An absent hook succeeds --
    // "there was nothing to do" is not a failure.
    //
    // Taking the hook as a parameter rather than testing core::catalog::Setup
    // against nullptr at the call site is what keeps this compiling under
    // -Werror both ways round: those constants are compile-time known, so a
    // catalog that *does* declare a hook makes the null test provably useless
    // and -Waddress rejects it, while a catalog that declares none needs
    // exactly that test. A parameter is opaque to the warning and correct for
    // both.
    //
    [[nodiscard]]
    auto runHook( const core::RunHook hook) -> bool
    {
        return hook == nullptr || hook();
    }

    //
    // Runs TEARDOWN on the way out of the run, whichever way that is -- the
    // selection finishing, --until-failure stopping early, or a script throwing
    // straight past everything. A destructor for the same reason
    // hal::RigSafingGuard is one: the alternative is a call at each of those
    // exits and a list to keep in step with the next one added.
    //
    // Ordered *inside* main's hal::RigSafingGuard, so a run ends by doing what
    // the suite asked for and only then the unconditional safing -- a teardown
    // that expects the fabric still wired up gets it.
    //
    // Nothing escapes here. A destructor that throws while the stack is already
    // unwinding from a failing script terminates the process, which would lose
    // both logs and tell a rig console nothing about what went wrong.
    //
    class TeardownGuard
    {
        public:
            TeardownGuard( const core::RunHook hook, bool & allPassed) : mHook( hook), mAllPassed( allPassed) {}

            ~TeardownGuard()
            {
                if ( !mHook)
                    return;

                try
                {
                    if ( !mHook())
                        fail( "TEARDOWN reported failure");
                }
                catch ( const std::exception & e)
                {
                    fail( std::string( "TEARDOWN threw: ") + e.what());
                }
                catch ( ...)
                {
                    fail( "TEARDOWN threw an unknown exception");
                }
            }

        private:
            //
            // A failing teardown fails the run. The scripts may well all have
            // passed, but a rig that did not shut down the way the suite says
            // it should is not a run anybody should read as clean.
            //
            auto fail( const std::string & what) -> void
            {
                std::cerr << what << '\n';

                core::journal().post( core::JournalRecord{
                    .Method  = core::Verb::Note,
                    .Subject = "run",
                    .Detail  = what
                });

                mAllPassed = false;
            }

            core::RunHook  mHook;
            bool &         mAllPassed;
    };

    //
    // The whole run: SETUP, then the selection as many times as asked, then
    // TEARDOWN.
    //
    // The hooks bracket the *selection*, not each pass over it -- so
    // --repeat=50 powers the rig on once, runs the scripts fifty times, and
    // powers it off once. See core::RunHook.
    //
    // Defined below runTests, next to the loop it owns.
    auto runPasses( const Options & options, bool & allPassed) -> void;

    auto runTests( const Options & options) -> bool
    {
        //
        // Checked before SETUP: a selection matching nothing is a caller error,
        // and powering a rig up to then run no test at all is not a helpful way
        // to report it.
        //
        if ( !anythingSelected( options.Selection))
        {
            std::cerr << "No catalog test matched --select; nothing ran.\n";
            return false;
        }

        bool allPassed = true;

        //
        // The guard lives in a scope of its own, and the verdict is returned
        // only after that scope closes. A `return allPassed` with the guard
        // still alive would copy the value out *before* the destructor ran, so
        // a teardown reporting failure could never affect the exit status --
        // which is precisely what it is meant to do.
        //
        {
            //
            // Constructed before SETUP runs, so a setup that fails half way
            // through -- supplies up, fabric not -- still gets its teardown.
            //
            const TeardownGuard teardown{ core::catalog::Teardown, allPassed };

            if ( !runHook( core::catalog::Setup))
            {
                std::cerr << "SETUP reported failure; no test was run.\n";

                core::journal().post( core::JournalRecord{
                    .Method  = core::Verb::Note,
                    .Subject = "run",
                    .Detail  = "SETUP reported failure; no test was run"
                });

                allPassed = false;
            }
            else
            {
                runPasses( options, allPassed);
            }
        }

        return allPassed;
    }

    //
    // The passes themselves, split out so runTests above can keep the guard's
    // scope and the verdict's lifetime obvious rather than burying them in a
    // loop.
    //
    auto runPasses( const Options & options, bool & allPassed) -> void
    {
        const auto passes = passCount( options);

        for ( std::uint64_t pass = 0; pass < passes; ++pass)
        {
            //
            // Recorded only when there is more than one, so an ordinary single
            // run's log is unchanged. Without it a repeated run's log is the
            // same group and test headings over and over with nothing saying
            // which time round it is.
            //
            if ( passes > 1)
            {
                core::journal().post( core::JournalRecord{
                    .Method  = core::Verb::Note,
                    .Subject = "run",
                    .Detail  = options.Repeat
                        ? "pass " + std::to_string( pass + 1) + " of " + std::to_string( passes)
                        : "pass " + std::to_string( pass + 1)
                });
            }

            const bool passed = runOnePass( options.Selection);

            allPassed &= passed;

            if ( !passed && options.UntilFailure)
            {
                std::cerr << "Stopping after failing pass " << ( pass + 1) << " (--until-failure).\n";

                core::journal().post( core::JournalRecord{
                    .Method  = core::Verb::Note,
                    .Subject = "run",
                    .Detail  = "stopped after failing pass " + std::to_string( pass + 1) + " (--until-failure)"
                });

                break;
            }
        }
    }
} // namespace

int main( int argc, char ** argv)
{
    const auto parsed = parseOptions( argc, argv);

    if ( !parsed)
        return 1;

    const auto & options = *parsed;

    //
    // Before every other mode, including --safe: --help is the one invocation
    // that must not touch the rig, and a caller who asked what the flags are has
    // not asked for anything to happen.
    //
    if ( options.ShowHelp)
    {
        printUsage();
        return 0;
    }

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

    //
    // --- The replayable value stream ---
    // Both set up here, before the journal opens and before anything is
    // measured, and both fatal if they cannot be: a caller that asked to record
    // a run and silently got no recording, or asked to replay one and silently
    // got live hardware instead, has been told the run did something it didn't.
    // Nothing has touched the rig at this point, so refusing costs nothing --
    // the same reasoning the log files above are opened on.
    //
    // The record file is opened now and written at the end, rather than opened
    // at the end: discovering an unwritable path after a fifty-pass soak run
    // would mean discovering it exactly when the data is most expensive to have
    // lost.
    //
    std::ofstream recording;

    if ( options.RecordPath)
    {
        try
        {
            const auto path = std::filesystem::path( *options.RecordPath);

            ensureParentDirectory( path);

            recording.open( path, std::ios::out | std::ios::trunc);

            if ( !recording)
            {
                throw std::runtime_error( "could not open '" + path.string() + "' for writing");
            }
        }
        catch ( const std::exception & e)
        {
            std::cerr << "Could not open the recording: " << e.what() << '\n';
            return 1;
        }

        Measure.startRecording();
    }

    if ( options.ReplayPath)
    {
        try
        {
            //
            // Every reading now comes from the file, and nothing reaches an
            // instrument or the fabric. The rig is still safed on the way out
            // regardless -- a replay run has nothing to safe, and safing
            // something already idle is what hal::safeRig() is built for.
            //
            Measure.load( *options.ReplayPath);
        }
        catch ( const std::exception & e)
        {
            std::cerr << "Could not load the recording to replay: " << e.what() << '\n';
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
            allPassed = runTests( options);
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

    //
    // Written after the run block above has closed, so the file holds every
    // reading the run took -- including any a TEARDOWN made on its way out.
    //
    // Deliberately outside the try/catch: a run that ended by throwing is the
    // one whose readings are most worth having, so the recording is written
    // for a failed run exactly as for a passing one.
    //
    // A recording that could not be written fails the run. The scripts may
    // well all have passed, but the caller asked for an artifact and hasn't
    // got one -- the same stance --sarif=/--rtf= take on a log that could not
    // be opened.
    //
    if ( options.RecordPath)
    {
        Measure.stopRecording();
        Measure.dump( recording);

        recording.flush();

        if ( !recording)
        {
            std::cerr << "Could not write the recording to " << *options.RecordPath << '\n';

            core::journal().post( core::JournalRecord{
                .Method  = core::Verb::Note,
                .Subject = "run",
                .Detail  = "could not write the recording to " + *options.RecordPath
            });

            allPassed = false;
        }
    }

    core::journal().end( allPassed);

    return allPassed ? 0 : 1;
}
