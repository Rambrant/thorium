#pragma once

#include <string_view>

#include "core/quantities/quantity.hpp"

namespace core
{
    //
    // Pause the script for a fixed time.
    //
    //     Apply( DcP1.output( 5_V));
    //     Delay( 250_ms, "supply settles");
    //     const auto volts = Measure( Output5V);
    //
    // A *delay*, not a wait. It waits for nothing: when its time is up it
    // returns, whatever has or has not happened. Waiting for something -- a
    // capture to land, a reading to settle -- is a different verb with a
    // different answer to "and if it never does", and Await
    // (core/verbs/acquire.hpp) is the one that exists. This is deliberately the
    // weaker tool, and the name is there so that nobody reaches for it
    // believing it is the stronger: core/verbs/acquire.hpp quotes the
    // instrument manual on why fixed delays "often use excessive time or the
    // time may not be long enough". Use Delay where the physics has a time
    // constant and the instrument cannot tell you it is done.
    //
    // The reason is for the log, and is worth writing: "supply settles" says
    // why a test is forty seconds longer than its checks explain.
    //
    // ---------------------------------------------------------------------
    // What it does with no bench
    // ---------------------------------------------------------------------
    //
    // Nothing, and says so. A delay is there for physics -- a supply ramping, a
    // relay bouncing -- and a run with the bench detached (--replay, --inject,
    // --skeleton: see core/session/bench.hpp) has none, so waiting would make
    // every replay of a long test as long as the original for no reason. It is
    // an instruction like Apply and Connect and follows the same rule: it
    // reaches the bench when there is one and the log says which.
    //
    // ---------------------------------------------------------------------
    // And on a stop
    // ---------------------------------------------------------------------
    //
    // It is the place a long test can be ended. The wait is taken in short
    // slices, and when a stop has been requested (core/control/stop_request.hpp)
    // and the script is one the runner will let a verb abandon, it throws
    // StopRequested, which the runner catches where it calls the script. Without
    // that, a stop during a forty-second settle waits out the forty seconds.
    //
    // Only inside a test script. In a teardown hook or RUN_SETUP the wait is
    // taken in full, stop or no stop -- see core::StopArmed for why a throw there
    // is not an option.
    //
    // Posted when it *starts*, not when it ends: a watcher of a long run is
    // exactly who wants to know what it is doing for the next forty seconds. If
    // it is cut short a second record says so and how far it got.
    //
    // ---------------------------------------------------------------------
    // Why an engine
    // ---------------------------------------------------------------------
    //
    // For the reason Verify is one (core/verbs/verify.hpp): the verbs share a
    // shape, a stateless engine with a const call operator and one instance a
    // script names, and a verb that was a free function instead would be the one
    // a reader has to remember is different. Generic and stateless, so it lives
    // in core and its single instance is defined in core/src/verbs/delay.cpp
    // rather than being constructed by the hal layer like the engines that need
    // a rig behind them.
    //
    class DelayEngine
    {
        public:
            auto operator()( quantities::Time duration, std::string_view reason = {}) const -> void;
    };

    extern const DelayEngine Delay;
} // namespace core
