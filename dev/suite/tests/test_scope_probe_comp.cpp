#include "dev/suite/scripts.hpp"

//
// The ScopeProbeComp scripts, with readings, the capture and the traces
// injected, and the bench detached.
//
// Detached for the reason test_supply_outputs.cpp is: every one of these
// scripts Setups the scope first, and on this desk Osc1 is a Usb row -- an
// attached Setup would go looking for it through VISA from a unit test.
//
// The keys, which are the thing to get right here (see the script file's own
// comment): readings are "Osc1.<measurement>.<quantity>" and carry no channel,
// the capture is "Osc1.Acquisition", and a trace is "Osc1.Channel<N>". A key a
// script reads more than once -- Osc1.Vpp.Voltage, four times in the
// acquisition script -- is injected as a sequence in the order it is read.
//
#include "suite/tests/verdict.hpp"

#include "core/quantities/quantity.hpp"
#include "core/quantities/waveform.hpp"
#include "core/session/bench.hpp"
#include "hal/verbs/acquire.hpp"
#include "hal/verbs/measure.hpp"
#include "hal/verbs/trace.hpp"

#include <gtest/gtest.h>

#include <vector>

using core::quantities::Frequency;
using core::quantities::Time;
using core::quantities::Voltage;

using namespace core::literals;

namespace
{
    struct ScopeProbeCompFixture : ::testing::Test
    {
        protected:

            void SetUp() override
            {
                core::bench().detach();
            }

            //
            // useLive() clears the whole shared bank -- Measure's readings,
            // Await's flags and Fetch's traces alike (see core::ReadEngine).
            //
            void TearDown() override
            {
                Measure.useLive();

                core::bench().attach();
            }
    };

    //
    // A healthy comp square's eight levels, in the order amplitude<N>() reads
    // them -- each its own key, so each injected once.
    //
    auto injectHealthyLevels() -> void
    {
        Measure.inject( "Osc1.Vtop.Voltage",       2.50_V);
        Measure.inject( "Osc1.Vbase.Voltage",      0.01_V);
        Measure.inject( "Osc1.Vamplitude.Voltage", 2.49_V);
        Measure.inject( "Osc1.Vpp.Voltage",        2.62_V);
        Measure.inject( "Osc1.Vmax.Voltage",       2.60_V);
        Measure.inject( "Osc1.Vmin.Voltage",       Voltage{ -0.02 });
        Measure.inject( "Osc1.Vaverage.Voltage",   1.26_V);
        Measure.inject( "Osc1.Vrms.Voltage",       1.77_V);
    }

    auto injectHealthyTiming() -> void
    {
        Measure.inject( "Osc1.Frequency.Frequency",   1000.2_Hz);
        Measure.inject( "Osc1.Period.Time",           Time{ 0.99998e-3 });
        Measure.inject( "Osc1.PositiveWidth.Time",    Time{ 0.501e-3 });
        Measure.inject( "Osc1.NegativeWidth.Time",    Time{ 0.499e-3 });
        Measure.inject( "Osc1.RiseTime.Time",         Time{ 0.8e-6 });
        Measure.inject( "Osc1.FallTime.Time",         Time{ 0.9e-6 });
    }

    //
    // Two periods of the comp square as a scope would transfer them: volts,
    // 1 us apart, a little overshoot on each rising edge.
    //
    auto compTrace( const double top = 2.5) -> core::Waveform
    {
        std::vector<double> samples;

        for( int i = 0; i < 2000; ++i)
        {
            const bool high = ( i / 500) % 2 == 0;

            samples.push_back( high ? ( i % 500 == 0 ? top + 0.08 : top) : 0.0);
        }

        return core::Waveform{ core::QuantityKind::Voltage,
                               core::Waveform::Timing{ Time{ -1.0e-3 }, Time{ 1.0e-6 } },
                               std::move( samples) };
    }
} // namespace

//
// -- Amplitude ---------------------------------------------------------------
//

TEST_F( ScopeProbeCompFixture, AmplitudePassesOnAHealthySquareOnEitherChannel)
{
    injectHealthyLevels();
    EXPECT_TRUE( verdictOf( scopeAmplitudeCh1));

    injectHealthyLevels();
    EXPECT_TRUE( verdictOf( scopeAmplitudeCh2));
}

//
// The overshoot a badly compensated probe adds shows in max but not in top --
// which is the distinction the amplitude script keeps both readings for.
//
TEST_F( ScopeProbeCompFixture, AnOvershootBeyondToleranceFailsOnMaxAloneNotOnTop)
{
    Measure.inject( "Osc1.Vtop.Voltage",       2.50_V);
    Measure.inject( "Osc1.Vbase.Voltage",      0.01_V);
    Measure.inject( "Osc1.Vamplitude.Voltage", 2.49_V);
    Measure.inject( "Osc1.Vpp.Voltage",        2.62_V);
    Measure.inject( "Osc1.Vmax.Voltage",       3.10_V);    // 0.6 V of overshoot
    Measure.inject( "Osc1.Vmin.Voltage",       Voltage{ -0.02 });
    Measure.inject( "Osc1.Vaverage.Voltage",   1.26_V);
    Measure.inject( "Osc1.Vrms.Voltage",       1.77_V);

    EXPECT_FALSE( verdictOf( scopeAmplitudeCh1));
}

TEST_F( ScopeProbeCompFixture, AmplitudeFailsWhenTheProbeAttenuationIsWrong)
{
    //
    // The classic: a 1:1 lead with the channel set for a 10:1 probe reads ten
    // times high, everywhere at once.
    //
    Measure.inject( "Osc1.Vtop.Voltage",       25.0_V);
    Measure.inject( "Osc1.Vbase.Voltage",      0.1_V);
    Measure.inject( "Osc1.Vamplitude.Voltage", 24.9_V);
    Measure.inject( "Osc1.Vpp.Voltage",        26.2_V);
    Measure.inject( "Osc1.Vmax.Voltage",       26.0_V);
    Measure.inject( "Osc1.Vmin.Voltage",       Voltage{ -0.2 });
    Measure.inject( "Osc1.Vaverage.Voltage",   12.6_V);
    Measure.inject( "Osc1.Vrms.Voltage",       17.7_V);

    EXPECT_FALSE( verdictOf( scopeAmplitudeCh1));
}

//
// -- Timing ------------------------------------------------------------------
//

TEST_F( ScopeProbeCompFixture, TimingPassesOnAHealthySquareOnEitherChannel)
{
    injectHealthyTiming();
    EXPECT_TRUE( verdictOf( scopeTimingCh1));

    injectHealthyTiming();
    EXPECT_TRUE( verdictOf( scopeTimingCh2));
}

TEST_F( ScopeProbeCompFixture, AnAsymmetricSquareFailsOnItsWidths)
{
    Measure.inject( "Osc1.Frequency.Frequency", 1000.0_Hz);
    Measure.inject( "Osc1.Period.Time",         1.0_ms);
    Measure.inject( "Osc1.PositiveWidth.Time",  Time{ 0.6e-3 });   // 60% duty
    Measure.inject( "Osc1.NegativeWidth.Time",  Time{ 0.4e-3 });
    Measure.inject( "Osc1.RiseTime.Time",       Time{ 0.8e-6 });
    Measure.inject( "Osc1.FallTime.Time",       Time{ 0.9e-6 });

    EXPECT_FALSE( verdictOf( scopeTimingCh1));
}

TEST_F( ScopeProbeCompFixture, ASlowEdgeFails)
{
    Measure.inject( "Osc1.Frequency.Frequency", 1000.0_Hz);
    Measure.inject( "Osc1.Period.Time",         1.0_ms);
    Measure.inject( "Osc1.PositiveWidth.Time",  0.5_ms);
    Measure.inject( "Osc1.NegativeWidth.Time",  0.5_ms);
    Measure.inject( "Osc1.RiseTime.Time",       Time{ 12.0e-6 });  // a probe on the wrong input capacitance
    Measure.inject( "Osc1.FallTime.Time",       Time{ 0.9e-6 });

    EXPECT_FALSE( verdictOf( scopeTimingCh1));
}

//
// -- Capture -----------------------------------------------------------------
//

TEST_F( ScopeProbeCompFixture, ACompletedCaptureWithBothTracesPasses)
{
    Await.inject( "Osc1.Acquisition", true);
    Fetch.inject( "Osc1.Channel1", compTrace());
    Fetch.inject( "Osc1.Channel2", compTrace());

    EXPECT_TRUE( verdictOf( scopeCapture));
}

//
// A capture that never completed fails -- and nothing is fetched after it,
// which is the ordering the script exists to keep. No trace is injected here,
// so a script that fetched anyway would throw rather than quietly pass.
//
TEST_F( ScopeProbeCompFixture, ACaptureThatNeverCompletedFailsAndFetchesNothing)
{
    Await.inject( "Osc1.Acquisition", false);

    bool verdict = true;

    EXPECT_NO_THROW( verdict = verdictOf( scopeCapture));
    EXPECT_FALSE( verdict);
}

//
// The trace has to agree with the instrument's own levels. A transfer whose
// scaling is off by the probe factor is a trace ten times too large -- and a
// completed capture does not save it.
//
TEST_F( ScopeProbeCompFixture, ATraceScaledWrongFailsEvenThoughTheCaptureCompleted)
{
    Await.inject( "Osc1.Acquisition", true);
    Fetch.inject( "Osc1.Channel1", compTrace());
    Fetch.inject( "Osc1.Channel2", compTrace( 25.0));

    EXPECT_FALSE( verdictOf( scopeCapture));
}

TEST_F( ScopeProbeCompFixture, AnEmptyTraceFailsRatherThanThrowing)
{
    Await.inject( "Osc1.Acquisition", true);
    Fetch.inject( "Osc1.Channel1", compTrace());
    Fetch.inject( "Osc1.Channel2", core::Waveform{});

    bool verdict = true;

    EXPECT_NO_THROW( verdict = verdictOf( scopeCapture));
    EXPECT_FALSE( verdict);
}

//
// -- Acquisition types, coupling and bandwidth ---------------------------------
//

TEST_F( ScopeProbeCompFixture, EveryAcquisitionTypeHasToReadTheSquare)
{
    // Normal, high resolution, peak detect, averaged -- in that order.
    Measure.inject( "Osc1.Vpp.Voltage", { 2.62_V, 2.58_V, 2.70_V, 2.55_V });
    EXPECT_TRUE( verdictOf( scopeAcquisitionTypes));

    // Peak detect is the one that keeps spikes, and a spike beyond the window fails.
    Measure.inject( "Osc1.Vpp.Voltage", { 2.62_V, 2.58_V, 3.20_V, 2.55_V });
    EXPECT_FALSE( verdictOf( scopeAcquisitionTypes));
}

TEST_F( ScopeProbeCompFixture, AcCouplingMustCentreTheSquareAndTheLimitMustLeaveItAlone)
{
    Measure.inject( "Osc1.Vaverage.Voltage",   0.02_V);
    Measure.inject( "Osc1.Vamplitude.Voltage", 2.48_V);
    EXPECT_TRUE( verdictOf( scopeCouplingAndBandwidth));

    //
    // An average still at Vtop/2 is a coupling that never switched.
    //
    Measure.inject( "Osc1.Vaverage.Voltage",   1.25_V);
    Measure.inject( "Osc1.Vamplitude.Voltage", 2.48_V);
    EXPECT_FALSE( verdictOf( scopeCouplingAndBandwidth));
}

//
// And the fixture's reason, as behaviour: with the bench detached, a script
// that Setups the scope four times and measures eight readings runs to the end
// on injected values. Osc1 is a Usb row, so an attached Setup would have opened
// a VISA session -- and on a machine without VISA, thrown out of the script.
//
TEST_F( ScopeProbeCompFixture, ADetachedRunNeverReachesTheScope)
{
    injectHealthyLevels();

    EXPECT_NO_THROW( static_cast<void>( verdictOf( scopeAmplitudeCh2)));
}
