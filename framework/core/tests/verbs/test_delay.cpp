#include "core/verbs/delay.hpp"

#include <chrono>
#include <string>
#include <thread>

#include <gtest/gtest.h>

#include "core/control/stop_request.hpp"
#include "core/session/bench.hpp"
#include "core/testing/capturing_sink.hpp"

using namespace core::literals;
using Clock = std::chrono::steady_clock;

//
// Delay is time, so these tests are partly about time -- but about *bounds*, not
// durations: "took at least what was asked" and "returned long before what was
// asked", with a wide gap between the two, because a loaded machine makes a
// test that asserts "about 60 ms" a flaky one.
//
// Every test leaves the process's bench and stop flag as it found them: both are
// shared state, and a stop left requested would end the next test's delay.
//
namespace
{
    class Delay : public testing::Test
    {
        protected:
            auto SetUp() -> void override
            {
                core::journal().clearSinks();
                core::journal().add( Sink);
                core::clearStopRequest();
                core::bench().attach();
            }

            auto TearDown() -> void override
            {
                core::clearStopRequest();
                core::bench().attach();
                core::journal().clearSinks();
            }

            auto onlyEvent() const -> const core::JournalEvent &
            {
                EXPECT_FALSE( Sink.Events.empty());
                return Sink.Events.front();
            }

            core::CapturingSink  Sink;
    };

    auto since( const Clock::time_point start) -> std::chrono::milliseconds
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>( Clock::now() - start);
    }
}

TEST_F( Delay, WaitsForAtLeastTheTimeAsked)
{
    const auto start = Clock::now();

    core::Delay( 60_ms, "supply settles");

    EXPECT_GE( since( start), std::chrono::milliseconds( 60));
}

TEST_F( Delay, IsLoggedWhenItStartsWithItsReasonAndDuration)
{
    core::Delay( 250_ms, "supply settles");

    ASSERT_GE( Sink.Events.size(), 1u);

    const auto & event = onlyEvent();

    EXPECT_EQ( event.Method, core::Verb::Delay);
    EXPECT_EQ( event.Subject, "supply settles");
    EXPECT_EQ( event.Detail, "waiting");
    EXPECT_EQ( event.Value, "250 ms");
    EXPECT_EQ( event.Unit, "s");
    ASSERT_TRUE( event.Numeric.has_value());
    EXPECT_DOUBLE_EQ( *event.Numeric, 0.25);
    EXPECT_FALSE( event.Passed.has_value()) << "not a check: it has no pass/fail notion";
}

TEST_F( Delay, WithoutAReasonItIsStillNamed)
{
    core::Delay( 1_ms);

    EXPECT_EQ( onlyEvent().Subject, "delay");
}

TEST_F( Delay, LongerDelaysAreWrittenInSeconds)
{
    core::bench().detach();   // so the test does not actually wait

    core::Delay( 1.5_s);
    core::Delay( 30_s);

    ASSERT_EQ( Sink.Events.size(), 2u);
    EXPECT_EQ( Sink.Events[ 0].Value, "1.5 s");
    EXPECT_EQ( Sink.Events[ 1].Value, "30 s");
}

TEST_F( Delay, WithNoBenchItDoesNotWaitAndSaysSo)
{
    core::bench().detach();

    const auto start = Clock::now();

    core::Delay( 5_s, "supply settles");

    EXPECT_LT( since( start), std::chrono::milliseconds( 500)) << "a replay has no physics to wait for";

    ASSERT_EQ( Sink.Events.size(), 1u);
    EXPECT_EQ( onlyEvent().Method, core::Verb::Delay);
    EXPECT_EQ( onlyEvent().Detail, "skipped: no bench attached");
    EXPECT_EQ( onlyEvent().Value, "5 s") << "the log still says how long it would have been";
}

TEST_F( Delay, ANegativeTimeIsNoTimeAtAll)
{
    const auto start = Clock::now();

    core::Delay( core::quantities::Time{ -1.0 });

    EXPECT_LT( since( start), std::chrono::milliseconds( 500));
}

// ---------------------------------------------------------------------------
// A stop request
// ---------------------------------------------------------------------------

TEST_F( Delay, AStopEndsItEarlyWhenTheScriptIsOneAVerbMayAbandon)
{
    const core::StopArmed armed;

    std::thread stopper( []
    {
        std::this_thread::sleep_for( std::chrono::milliseconds( 60));
        core::requestStop();
    });

    const auto start = Clock::now();

    EXPECT_THROW( core::Delay( 10_s, "a long settle"), core::StopRequested);

    stopper.join();

    EXPECT_LT( since( start), std::chrono::milliseconds( 2000)) << "it did not wait out the ten seconds";

    ASSERT_EQ( Sink.Events.size(), 2u);
    EXPECT_EQ( Sink.Events[ 0].Detail, "waiting");
    EXPECT_NE( Sink.Events[ 1].Detail.find( "interrupted by a stop request after"), std::string::npos);
    EXPECT_EQ( Sink.Events[ 1].Subject, "a long settle");
}

TEST_F( Delay, AStopAlreadyRequestedEndsItAtOnce)
{
    const core::StopArmed armed;

    core::requestStop();

    const auto start = Clock::now();

    EXPECT_THROW( core::Delay( 10_s), core::StopRequested);
    EXPECT_LT( since( start), std::chrono::milliseconds( 500));
}

TEST_F( Delay, OutsideATestScriptAStopDoesNotCutItShort)
{
    //
    // A teardown hook runs from a destructor, possibly while another exception
    // is unwinding: a throw out of it is std::terminate. So a hook's settle is
    // taken in full.
    //
    core::requestStop();

    const auto start = Clock::now();

    EXPECT_NO_THROW( core::Delay( 60_ms, "teardown settle"));
    EXPECT_GE( since( start), std::chrono::milliseconds( 60));
}

TEST_F( Delay, TheScriptIsOnlyArmedForAsLongAsTheScopeLasts)
{
    EXPECT_FALSE( core::stopArmed());

    {
        const core::StopArmed outer;
        EXPECT_TRUE( core::stopArmed());

        {
            const core::StopArmed inner;
            EXPECT_TRUE( core::stopArmed());
        }

        EXPECT_TRUE( core::stopArmed()) << "scopes nest";
    }

    EXPECT_FALSE( core::stopArmed());
}

TEST_F( Delay, ADetachedBenchStillSkipsRatherThanThrowingOnAStop)
{
    //
    // Nothing is waited for, so there is nothing to interrupt: a replay with a
    // stop already requested must reach the runner's own between-tests check,
    // not die inside a verb that did no waiting.
    //
    const core::StopArmed armed;

    core::bench().detach();
    core::requestStop();

    EXPECT_NO_THROW( core::Delay( 10_s));
}
