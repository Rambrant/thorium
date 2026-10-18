#pragma once

#include <atomic>
#include <csignal>
#include <iostream>
#include <string>
#include <thread>

#ifndef _WIN32
#include <unistd.h>
#endif

//
// A way to ask a run to end early, from outside it.
//
// There was none. Ctrl-C was the default SIGINT, which kills the process where
// it stands: no teardown hook, no hal::RigSafingGuard, no log. That is the
// right answer for a process that has stopped answering and the wrong one for
// an operator who simply has had enough of an unbounded --until-failure soak.
//
// So there is one flag, set from two places and read from one:
//
//   - SIGINT / SIGTERM, for a person at a terminal. The first sets the flag; a
//     second restores the default action and re-raises, so a run that does not
//     reach a boundary (a hung instrument read) can still be ended by pressing
//     it again -- and then needs a --safe, exactly as it always did.
//   - a line reading "stop" on stdin, under --stop-on-stdin, for the console
//     that supervises this process. A signal is not the transport because
//     Windows has no way to send one to a child that has no console; a pipe is
//     the same on both. End of input is a stop too: the console holds the
//     other end for exactly as long as it is there to watch, so an end means
//     it is gone -- closed, crashed or killed -- and an unbounded run with
//     nobody watching is not one to leave energising a rig. It is recorded
//     as a different reason, so the log says which happened.
//
// It is read only between tests, groups and passes (see runSelectedTests,
// runOnePass and runPasses in main.cpp). Nothing here interrupts a script:
// the point of asking is that the run then ends the way a run ends -- the
// current test finishes, the teardown hooks run, the rig is safed and both
// logs are written -- rather than the way a killed process does.
//
namespace runner
{
    namespace detail
    {
        //
        // std::atomic<bool> is lock-free everywhere this builds, which is what
        // makes touching it from a signal handler permitted.
        //
        inline std::atomic<bool>  gStopRequested{ false };

        // Why, as a StopReason; the first request to arrive wins.
        inline std::atomic<int>   gStopReason{ 0 };

        // Whether a boundary check actually cut work short -- see stopNow().
        inline std::atomic<bool>  gStopHonoured{ false };

        extern "C" inline void onStopSignal( int signal)
        {
            if ( gStopRequested.exchange( true))
            {
                //
                // Second time: the operator is not waiting for a boundary.
                //
                std::signal( signal, SIG_DFL);
                std::raise( signal);

                return;
            }

#ifndef _WIN32
            static constexpr char kNotice[] =
                "\nStopping after the current test. Press Ctrl-C again to abort now "
                "(the rig is then left as it is: run --safe).\n";

            const auto ignored = ::write( STDERR_FILENO, kNotice, sizeof( kNotice) - 1);
            static_cast<void>( ignored);
#endif
        }
    } // namespace detail

    enum class StopReason : int
    {
        None = 0,
        Operator,       // Ctrl-C, SIGTERM, or a "stop" line
        ConsoleGone     // stdin ended: whoever was supervising this run is not there
    };

    inline auto requestStop( StopReason reason = StopReason::Operator) noexcept -> void
    {
        int expected = 0;
        detail::gStopReason.compare_exchange_strong( expected, static_cast<int>( reason));
        detail::gStopRequested.store( true);
    }

    [[nodiscard]]
    inline auto stopReason() noexcept -> StopReason
    {
        return static_cast<StopReason>( detail::gStopReason.load());
    }

    [[nodiscard]]
    inline auto stopRequested() noexcept -> bool
    {
        return detail::gStopRequested.load();
    }

    //
    // What the boundary checks call: whether to stop *here*, remembering that
    // this one did. Separate from stopRequested() because the two differ at the
    // end of a run: a request that arrives during the last test cuts nothing
    // short, and that run finished everything it was asked to and must not be
    // reported as stopped.
    //
    [[nodiscard]]
    inline auto stopNow() noexcept -> bool
    {
        if ( !stopRequested())
            return false;

        detail::gStopHonoured.store( true);

        return true;
    }

    [[nodiscard]]
    inline auto stopHonoured() noexcept -> bool
    {
        return detail::gStopHonoured.load();
    }

    inline auto installStopSignals() -> void
    {
        std::signal( SIGINT,  detail::onStopSignal);
        std::signal( SIGTERM, detail::onStopSignal);
    }

    //
    // Reads stdin for the word "stop", and for its end, on a thread that is
    // never joined: it is blocked in a read for the life of the process, and
    // process exit is what ends it. Anything else on a line is ignored, so a
    // future command does not break an older runner.
    //
    inline auto watchStdinForStop() -> void
    {
        std::thread( []
        {
            std::string  line;

            while ( std::getline( std::cin, line))
            {
                if ( line == "stop" || line == "stop\r")
                    requestStop();
            }

            requestStop( StopReason::ConsoleGone);
        }).detach();
    }
} // namespace runner
