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
//
// GoogleTest hosted inside a wxAppConsole, which is not the usual shape and is
// forced by what is under test. ui::ChildProcess needs a live wxApp -- it is a
// wxEvtHandler and it starts wxTimers -- so the app has to be initialised
// before any test body runs. wxIMPLEMENT_APP_NO_MAIN gives us the app without
// its main(), and main() below hands control to wxEntry, which runs OnInit and
// then OnRun; OnRun is where RUN_ALL_TESTS lives.
//
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <wx/app.h>
#include <wx/evtloop.h>
#include <wx/filename.h>

#include "app/process.hpp"
#include "protocol/command.hpp"

namespace
{
    const std::string kBinary = THORIUM_UI_TEST_BINARY;

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

// ---------------------------------------------------------------------------
// A real run, streamed
// ---------------------------------------------------------------------------

TEST( ChildProcess, StreamsARunAndDoesNotCallItACrash)
{
    ui::Suite suite;

    suite.Binary = kBinary;

    ui::RunRequest request;

    //
    // --skeleton so this touches no instrument and writes no log: the thing
    // under test is the plumbing, and a test that left an RTF with a DUT serial
    // in it would be claiming a run happened.
    //
    request.Extra = { "--skeleton=" + std::string( wxFileName::CreateTempFileName( "thorium-ui").utf8_string()) };

    const auto outcome = drive( ui::buildRunCommand( suite, request));

    ASSERT_TRUE( outcome.Started);
    ASSERT_FALSE( outcome.Events.empty());

    EXPECT_FALSE( outcome.Crashed) << "a run that reported a result is not a crash";
    EXPECT_EQ( outcome.Events.front().Which, ui::RunEvent::Kind::RunStart);

    //
    // The assertion behind ChildProcess::onEnded's final drain():
    // wxEVT_END_PROCESS can arrive with the tail of the stream still in the
    // pipe, and reporting the exit before reading it would show an operator a
    // run that stopped several verdicts before it actually did.
    //
    EXPECT_EQ( outcome.Events.back().Which, ui::RunEvent::Kind::RunEnd)
        << "the pipe is drained before the exit is reported";
}

// ---------------------------------------------------------------------------
// The ways a run does not happen
// ---------------------------------------------------------------------------

TEST( ChildProcess, ARefusedInvocationSaysWhyOnStderrAndStreamsNothing)
{
    //
    // --events=- without --quiet, which run_scripts rejects. The run never
    // starts, so there are no events at all and stderr is the only thing an
    // operator could be shown -- which is why ChildProcess carries it and
    // MainFrame puts it in the results list.
    //
    const auto outcome = drive( { kBinary, "--events=-" });

    EXPECT_TRUE( outcome.Started)        << "the process starts, then refuses";
    EXPECT_NE( outcome.ExitCode, 0);
    EXPECT_TRUE( outcome.Events.empty());
    EXPECT_FALSE( outcome.Errors.empty());
}

TEST( ChildProcess, AMissingBinaryIsReportedRatherThanLookingLikeAnEmptyRun)
{
    const auto outcome = drive( { kBinary + "-does-not-exist", "--list-tests" });

    //
    // Asserted as the outcome a caller sees, not as the mechanism, because the
    // mechanism differs by platform and both spellings are correct:
    //
    //   Unix    fork() succeeds, so wxExecute hands back a pid and the failed
    //           exec surfaces as an exit of -1
    //   Windows CreateProcess fails outright and wxExecute answers 0, so
    //           start() returns false
    //
    // A test pinned to either one passes on one platform and fails on another
    // while the program is behaving correctly on both.
    //
    EXPECT_TRUE( !outcome.Started || outcome.ExitCode != 0);
    EXPECT_TRUE( outcome.Events.empty());
}

// ---------------------------------------------------------------------------
// The app host
// ---------------------------------------------------------------------------

//
// wxAppConsole rather than wxApp: everything ui::ChildProcess touches
// (wxProcess, wxTimer, the event loop) is wxBase, so this runs on a build
// machine with no display.
//
class TestApp : public wxAppConsole
{
    public:
        auto OnRun() -> int override { return RUN_ALL_TESTS(); }
};

wxIMPLEMENT_APP_NO_MAIN( TestApp);

int main( int argc, char ** argv)
{
    //
    // InitGoogleTest first so it can strip its own --gtest_* arguments before
    // wx sees them -- gtest_discover_tests runs this binary with
    // --gtest_list_tests, and wxAppConsole would otherwise reject the flag as
    // unknown and exit before any test was listed.
    //
    ::testing::InitGoogleTest( &argc, argv);

    return wxEntry( argc, argv);
}
