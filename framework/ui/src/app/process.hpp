#pragma once

#include <functional>
#include <string>
#include <vector>

#include <wx/event.h>
#include <wx/process.h>
#include <wx/timer.h>

#include "protocol/events.hpp"

namespace ui
{
    //
    // A run_scripts invocation, driven asynchronously.
    //
    // Everything this class exists to get right is a consequence of one fact:
    // the process being driven is attached to a rig. So --
    //
    //   - it never blocks the window. An operator must be able to press "Safe
    //     the rig" while a run is in progress, which means the run cannot be a
    //     modal wait.
    //   - it distinguishes a finished run from a dead one. A run that exits
    //     non-zero failed a check; a run that exits without a runEnd crashed,
    //     and the rig's state is then unknown. Those are different situations
    //     with different consequences and this is where they are told apart.
    //   - it owns nothing about the run's meaning. It hands out ui::RunEvent
    //     and an exit code; what a verdict implies is the window's business.
    //
    class ChildProcess : public wxEvtHandler
    {
        public:
            //
            // Called on the GUI thread as output arrives and when the process
            // ends. OnEnded's Crashed flag is the "no runEnd" case above.
            //
            //
            // OnEvent and OnOutput are both optional and both fed from the
            // child's stdout: OnEvent gets it parsed as the event stream,
            // OnOutput gets it raw. Which handlers a caller sets is what
            // decides how the child is read, and there is no mode flag.
            //
            // Two handlers rather than two classes because the three things
            // this program runs -- a test run, --list-tests, --describe-options
            // -- differ only in how their stdout is read. Everything that is
            // actually hard here (not blocking the window, draining the pipe
            // before reporting the exit, telling a finished run from a dead
            // one) is identical for all three, and a second launcher would be a
            // second place to get it wrong.
            //
            struct Handlers
            {
                std::function<void( const RunEvent &)>  OnEvent;
                std::function<void( const std::string &)>  OnOutput;
                std::function<void( const std::string &)>  OnStderr;
                std::function<void( int exitCode, bool crashed)>  OnEnded;
            };

            //
            // A handler must not destroy this object.
            //
            // Every one of them is owned by the ChildProcess that calls it, so
            // a handler that drops the owning pointer is destroying the lambda
            // mid-call, and the wxEvtHandler underneath it mid-dispatch. In
            // practice that does not crash -- it silently does nothing -- which
            // is why this is written down rather than left to be discovered.
            // A caller that needs to start the next process from a handler
            // defers it with wxEvtHandler::CallAfter (see MainFrame).
            //

            explicit ChildProcess( Handlers handlers);

            ~ChildProcess() override;

            //
            // Starts argv[0] with argv[1..]. False if the process could not be
            // launched at all -- a missing binary, a prefix that went away --
            // which is reported to the operator rather than silently leaving
            // the window looking like a run that produces nothing.
            //
            // A false here is not the only way that failure arrives, and a
            // caller must handle both. On Windows CreateProcess fails and this
            // answers false; on Unix fork() succeeds first, so this answers
            // true and the failed exec turns up in OnEnded as an exit of -1.
            // Every caller in MainFrame therefore reports the bad news from two
            // places, which looks redundant until this comment is read.
            //
            auto start( const std::vector<std::string> & argv) -> bool;

            [[nodiscard]]
            auto running() const -> bool { return mPid != 0; }

            //
            // Asks the run to stop, the polite way first.
            //
            // wxSIGTERM rather than wxSIGKILL, and the difference is the point:
            // a terminated run still unwinds, so hal::RigSafingGuard's
            // destructor still runs and the rig still comes down to idle (see
            // main.cpp's safeOnExit). A killed one does not, and leaves a
            // powered rig behind. The window follows up with --safe regardless,
            // because "the guard probably ran" is not something to be relying
            // on where an output rail is concerned.
            //
            auto requestStop() -> void;

        private:
            //
            // The output pump. A timer rather than a thread: wxProcess's
            // streams are only safe to touch from the GUI thread, and the
            // volume here is a few hundred lines over a run, not a stream that
            // needs its own core.
            //
            auto onTick( wxTimerEvent &) -> void;
            auto onEnded( wxProcessEvent & event) -> void;

            auto drain() -> void;

            Handlers      mHandlers;
            wxProcess *   mProcess{ nullptr };
            long          mPid{ 0 };
            wxTimer       mTimer;
            EventStream   mStream;

            //
            // Set when a runEnd arrives. Its absence at exit is what "crashed"
            // means -- see the class comment and framework/ui/README.md §4.
            //
            // Only meaningful for a caller that set OnEvent, which is why
            // onEnded narrows it before reporting rather than passing it on
            // raw: a --list-tests or a --safe has no runEnd to miss, and a
            // handler told "crashed" for every one of those will say so.
            //
            bool          mSawRunEnd{ false };
    };
} // namespace ui
