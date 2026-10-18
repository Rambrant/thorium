#pragma once

#include <atomic>
#include <exception>

namespace core
{
    //
    // A request, from outside a run, that it end early.
    //
    // This is only the flag and what reads it. What *sets* it is the process's
    // business and lives with the process: framework/runner/src/stop_sources.hpp
    // turns a Ctrl-C, a "stop" line or the end of stdin into a call to
    // requestStop(). It is in core, not the runner, because the things that read
    // it are not all the runner's: Delay (core/verbs/delay.hpp) is a verb, and a
    // verb cannot reach into the runner.
    //
    // ---------------------------------------------------------------------
    // Asked, never forced
    // ---------------------------------------------------------------------
    //
    // Nothing interrupts a script. The runner looks at the flag between tests,
    // groups and passes (stopNow() below), and a verb that is going to take a
    // while looks at it too -- that is the second place, and the only one inside
    // a script. In both the point is the same: the run then ends the way a run
    // ends, with the teardown hooks run, the rig safed and both logs written,
    // rather than the way a killed process does.
    //
    // ---------------------------------------------------------------------
    // Armed, which is the part that is easy to get wrong
    // ---------------------------------------------------------------------
    //
    // A verb may abandon a *test script* on a stop, by throwing StopRequested
    // out of it. It must never do the same in a teardown hook, in RUN_SETUP, or
    // in the rig safing -- a hook is exactly the code that is supposed to run
    // after a stop, and a throw out of one during the unwinding of another is
    // std::terminate, with both logs unwritten.
    //
    // So a verb abandons only when the run says it is inside a script: the
    // runner holds a StopArmed for the duration of one test script and for
    // nothing else (framework/runner/src/main.cpp, runSelectedTests). Outside
    // one, a Delay waits its full time -- a hook that settles a supply before
    // switching it off wants its settle, stop or no stop.
    //
    enum class StopReason : int
    {
        None = 0,
        Operator,       // Ctrl-C, SIGTERM, or a "stop" line
        ConsoleGone     // stdin ended: whoever was supervising this run is not there
    };

    namespace detail
    {
        //
        // std::atomic<int> and <bool> are lock-free everywhere this builds, which
        // is what makes setting them from a signal handler permitted.
        //
        inline std::atomic<bool>  gStopRequested{ false };

        // Why, as a StopReason; the first request to arrive wins.
        inline std::atomic<int>   gStopReason{ 0 };

        // Whether a check actually cut work short -- see stopNow().
        inline std::atomic<bool>  gStopHonoured{ false };

        // How many StopArmed scopes are open.
        inline std::atomic<int>   gStopArmed{ 0 };
    } // namespace detail

    inline auto requestStop( StopReason reason = StopReason::Operator) noexcept -> void
    {
        int expected = 0;
        detail::gStopReason.compare_exchange_strong( expected, static_cast<int>( reason));
        detail::gStopRequested.store( true);
    }

    [[nodiscard]]
    inline auto stopRequested() noexcept -> bool
    {
        return detail::gStopRequested.load();
    }

    [[nodiscard]]
    inline auto stopReason() noexcept -> StopReason
    {
        return static_cast<StopReason>( detail::gStopReason.load());
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
        if( !stopRequested())
            return false;

        detail::gStopHonoured.store( true);

        return true;
    }

    [[nodiscard]]
    inline auto stopHonoured() noexcept -> bool
    {
        return detail::gStopHonoured.load();
    }

    //
    // Held by the runner for one test script, so that a verb inside it may end
    // the script on a stop. Nests, and is what stopArmed() counts.
    //
    class StopArmed
    {
        public:
            StopArmed()  noexcept { ++detail::gStopArmed; }
            ~StopArmed()          { --detail::gStopArmed; }

            StopArmed( const StopArmed &) = delete;
            auto operator=( const StopArmed &) -> StopArmed & = delete;
    };

    [[nodiscard]]
    inline auto stopArmed() noexcept -> bool
    {
        return detail::gStopArmed.load() > 0;
    }

    //
    // Thrown by a verb to abandon the test script it is in, because a stop was
    // requested while the script was waiting. The runner catches it where it
    // calls the script (runSelectedTests) and nowhere else: a script that
    // catches it has decided the run should not end, which is not its call.
    //
    class StopRequested : public std::exception
    {
        public:
            [[nodiscard]]
            auto what() const noexcept -> const char * override { return "a stop was requested"; }
    };

    //
    // Forgets that a stop was ever asked for. For tests, which share a process
    // and a flag; a run never un-asks.
    //
    inline auto clearStopRequest() noexcept -> void
    {
        detail::gStopRequested.store( false);
        detail::gStopReason.store( 0);
        detail::gStopHonoured.store( false);
    }
} // namespace core
