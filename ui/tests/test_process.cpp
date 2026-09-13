//
// Tests over ui::ChildProcess -- the half of the window that drives a real
// run_scripts and reads its stream.
//
// Console, not GUI: everything this class touches (wxProcess, wxTimer, the
// event loop) is wxBase, so this runs with no display, no window and no user.
// That is what makes it a test rather than a demonstration -- it is the piece
// that cannot be checked by clicking Run, because what it has to get right is
// what happens when a run does NOT finish.
//
#include <iostream>
#include <string>
#include <vector>

#include <wx/app.h>
#include <wx/evtloop.h>
#include <wx/filename.h>

#include "app/process.hpp"
#include "protocol/command.hpp"

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

    struct Outcome
    {
        std::vector<ui::RunEvent>  Events;
        std::string                Errors;
        int                        ExitCode{ -1 };
        bool                       Crashed{ false };
        bool                       Started{ true };
    };

    //
    // Runs one command to completion on a nested event loop and answers
    // everything the window would have been told.
    //
    auto drive( const std::vector<std::string> & argv) -> Outcome
    {
        Outcome              outcome;

        //
        // wxConsoleEventLoop explicitly, never wxEventLoop.
        //
        // This binary links wxcore (it shares ui::ChildProcess with the window,
        // and the build does not split the toolkit), which makes wxEventLoop
        // the *GUI* loop -- and on macOS that one calls [NSApplication run],
        // which never returns for a wxAppConsole that has no NSApplication to
        // run. The symptom is a test that hangs with no output at all, in a
        // stack that goes straight from wxEventLoopBase::Run into AppKit.
        //
        // Naming the console loop sidesteps all of that, and is portable:
        // wx/evtloop.h declares wxConsoleEventLoop on Unix and Windows alike.
        // Nothing under test cares which loop it runs on -- ChildProcess uses
        // only wxBase -- so the console one is simply the loop that works
        // without a display, which is the point of testing it here.
        //
        wxConsoleEventLoop   loop;

        //
        // Activated BEFORE the child is started, and that ordering is the whole
        // subtlety of this helper.
        //
        // wxExecute registers the child's completion with whatever event loop
        // is active at the moment of the call, and wxTimer::Start does the
        // same. Starting the process first and activating the loop afterwards
        // produces a run that never reports anything and a test that hangs
        // forever -- which is exactly what the first version of this did.
        //
        // The window cannot make that mistake: its GUI event loop is running
        // long before anybody presses Run. The test has to arrange for what the
        // application gets for free.
        //
        wxEventLoopActivator active( &loop);

        ui::ChildProcess child( ui::ChildProcess::Handlers{
            .OnEvent  = [ &outcome]( const ui::RunEvent & event) { outcome.Events.push_back( event); },
            .OnStderr = [ &outcome]( const std::string & text)   { outcome.Errors += text; },
            .OnEnded  = [ &outcome, &loop]( const int code, const bool crashed)
                        {
                            outcome.ExitCode = code;
                            outcome.Crashed  = crashed;

                            loop.Exit();
                        }
        });

        if( !child.start( argv))
        {
            outcome.Started = false;

            return outcome;
        }

        loop.Run();

        return outcome;
    }

} // namespace

class TestApp : public wxAppConsole
    {
        public:
            auto OnRun() -> int override
            {
                const std::string binary = THORIUM_UI_TEST_BINARY;

                // --- a real run, streamed --------------------------------
                {
                    ui::Suite suite;

                    suite.Binary = binary;

                    ui::RunRequest request;

                    //
                    // --skeleton so this touches no instrument and writes no
                    // log: the thing under test is the plumbing, and a test
                    // that left an RTF with a DUT serial in it would be
                    // claiming a run happened.
                    //
                    request.Extra = { "--skeleton=" + std::string( wxFileName::CreateTempFileName( "thorium-ui").utf8_string()) };

                    const auto outcome = drive( ui::buildRunCommand( suite, request));

                    check( outcome.Started,          "the process starts");
                    check( !outcome.Events.empty(),  "events arrive");
                    check( !outcome.Crashed,         "a complete run is not reported as crashed");

                    bool sawRunStart = false;
                    bool sawRunEnd   = false;

                    for( const auto & event : outcome.Events)
                    {
                        sawRunStart = sawRunStart || event.Which == ui::RunEvent::Kind::RunStart;
                        sawRunEnd   = sawRunEnd   || event.Which == ui::RunEvent::Kind::RunEnd;
                    }

                    check( sawRunStart, "the stream opens with a runStart");
                    check( sawRunEnd,   "the stream closes with a runEnd");

                    //
                    // The last event drained is the runEnd. This is the
                    // assertion behind ChildProcess::onEnded's final drain():
                    // wxEVT_END_PROCESS can arrive with the tail of the stream
                    // still in the pipe, and reporting the exit before reading
                    // it would show an operator a run that stopped several
                    // verdicts before it actually did.
                    //
                    check( !outcome.Events.empty() &&
                           outcome.Events.back().Which == ui::RunEvent::Kind::RunEnd,
                           "the pipe is drained before the exit is reported");
                }

                // --- a refused invocation --------------------------------
                {
                    //
                    // --events=- without --quiet, which run_scripts rejects.
                    // The run never starts, so there are no events at all and
                    // stderr is the only thing an operator could be shown --
                    // which is why ChildProcess carries it and MainFrame puts
                    // it in the results list.
                    //
                    const auto outcome = drive( { binary, "--events=-" });

                    check( outcome.Started,           "a refused invocation still starts a process");
                    check( outcome.ExitCode != 0,     "and exits non-zero");
                    check( outcome.Events.empty(),    "and produces no events");
                    check( !outcome.Errors.empty(),   "and says why on stderr");
                }

                // --- a binary that is not there ---------------------------
                {
                    const auto outcome = drive( { binary + "-does-not-exist", "--list-tests" });

                    //
                    // Asserted as the outcome a caller sees, not as the
                    // mechanism, because the mechanism differs by platform and
                    // both spellings are correct:
                    //
                    //   Unix    fork() succeeds, so wxExecute hands back a pid
                    //           and the failed exec surfaces as an exit of -1
                    //   Windows CreateProcess fails outright and wxExecute
                    //           answers 0, so start() returns false
                    //
                    // A test pinned to either one passes on one platform and
                    // fails on another while the program is behaving correctly
                    // on both. What every caller in MainFrame actually needs is
                    // this: a binary that is not there never looks like a run
                    // that produced nothing.
                    //
                    check( !outcome.Started || outcome.ExitCode != 0,
                           "a missing binary is reported, not silently empty");

                    check( outcome.Events.empty(), "and produces no events");
                }

                if( gFailures == 0)
                {
                    std::cout << "ui process: all checks passed\n";
                }

                return gFailures == 0 ? 0 : 1;
            }
};

wxIMPLEMENT_APP_CONSOLE( TestApp);
