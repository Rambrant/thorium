#include "dev/suite/scripts.hpp"

//
// The PicoIntoWfg scripts, with the Pico's readings injected and the bench
// detached -- so neither the generator nor the scope is reached. Same
// arrangement as test_wfg_into_scope.cpp, from the other end: the readings are
// Osc2's, Osc2.<measurement>.<quantity> with no channel, and a key read more
// than once in a script is injected as a sequence in read order.
//
// A detached run checks the script's logic -- what it reads, in what order, and
// the verdict each reading earns. Whether a range, an offset or a resolution is
// one the PicoScope accepts is the driver's to say against a unit, which a
// detached Setup never reaches.
//
#include "core/testing/verdict.hpp"

#include "core/quantities/quantity.hpp"
#include "core/session/bench.hpp"
#include "core/quantities/waveform.hpp"
#include "hal/topology/active_instruments.hpp"
#include "hal/verbs/acquire.hpp"
#include "hal/verbs/measure.hpp"
#include "hal/verbs/trace.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <numbers>
#include <vector>

using core::quantities::Time;
using core::quantities::Voltage;

using namespace core::literals;

namespace
{
    struct PicoIntoWfgFixture : ::testing::Test
    {
        protected:

            void SetUp() override
            {
                core::bench().detach();
            }

            void TearDown() override
            {
                Measure.useLive();

                core::bench().attach();
            }
    };

    auto injectHealthySine() -> void
    {
        Measure.inject( "Osc2.Vpp.Voltage",         2.02_V);
        Measure.inject( "Osc2.Frequency.Frequency", 1000.1_Hz);
        Measure.inject( "Osc2.Vaverage.Voltage",    0.004_V);
        Measure.inject( "Osc2.Vrms.Voltage",        0.712_V);
    }

    //
    // A 1 kHz sine of the given peak, 2000 samples over 2 ms -- two periods.
    //
    auto sineTrace( const double peak = 1.0) -> core::Waveform
    {
        std::vector<double> samples;

        for( int i = 0; i < 2000; ++i)
        {
            samples.push_back( peak * std::sin( 2.0 * std::numbers::pi * i / 1000.0));
        }

        return core::Waveform{ core::QuantityKind::Voltage,
                               core::Waveform::Timing{ Time{ -1.0e-3 }, Time{ 1.0e-6 } },
                               std::move( samples) };
    }
} // namespace

TEST_F( PicoIntoWfgFixture, AHealthySinePassesOnEitherChannel)
{
    injectHealthySine();
    EXPECT_TRUE( verdictOf( picoSineChA));

    injectHealthySine();
    EXPECT_TRUE( verdictOf( picoSineChB));
}

//
// A generator left on its fifty-ohm default doubles into the Pico's 1 MOhm, and
// the sine test must not pass it.
//
TEST_F( PicoIntoWfgFixture, ASineAtTwiceItsAmplitudeFails)
{
    Measure.inject( "Osc2.Vpp.Voltage",         4.03_V);
    Measure.inject( "Osc2.Frequency.Frequency", 1000.1_Hz);
    Measure.inject( "Osc2.Vaverage.Voltage",    0.004_V);
    Measure.inject( "Osc2.Vrms.Voltage",        1.42_V);

    EXPECT_FALSE( verdictOf( picoSineChA));
}

TEST_F( PicoIntoWfgFixture, FourDecadesOfFrequencyEachLandInTheirOwnWindow)
{
    Measure.inject( "Osc2.Frequency.Frequency", { 100.1_Hz, 1000.2_Hz, 10.01_kHz, 1.001_MHz });
    EXPECT_TRUE( verdictOf( picoFrequencySteps));

    Measure.inject( "Osc2.Frequency.Frequency", { 100.1_Hz, 1000.2_Hz, 10.01_kHz, 10.01_kHz });
    EXPECT_FALSE( verdictOf( picoFrequencySteps));
}

//
// An offset applied with the wrong sign puts the trace 2 V from where it
// belongs: same amplitude, centred on -1 V.
//
TEST_F( PicoIntoWfgFixture, OffsetAndAmplitudeMustBothBeRight)
{
    Measure.inject( "Osc2.Vmax.Voltage",     3.02_V);
    Measure.inject( "Osc2.Vmin.Voltage",     Voltage{ -0.98 });
    Measure.inject( "Osc2.Vaverage.Voltage", 1.01_V);
    EXPECT_TRUE( verdictOf( picoAmplitudeAndOffset));

    Measure.inject( "Osc2.Vmax.Voltage",     1.02_V);
    Measure.inject( "Osc2.Vmin.Voltage",     Voltage{ -2.98 });
    Measure.inject( "Osc2.Vaverage.Voltage", Voltage{ -0.99 });
    EXPECT_FALSE( verdictOf( picoAmplitudeAndOffset));
}

//
// Every range and every resolution has to read the same sine; one that does not
// -- a scaling that only holds on the range it was tuned on -- fails.
//
TEST_F( PicoIntoWfgFixture, EveryRangeReadsTheSameSine)
{
    Measure.inject( "Osc2.Vpp.Voltage", { 2.01_V, 2.02_V, 2.00_V });
    EXPECT_TRUE( verdictOf( picoRanges));

    Measure.inject( "Osc2.Vpp.Voltage", { 2.01_V, 2.02_V, 2.60_V });
    EXPECT_FALSE( verdictOf( picoRanges));
}

TEST_F( PicoIntoWfgFixture, EveryResolutionReadsTheSameSine)
{
    Measure.inject( "Osc2.Vpp.Voltage",         { 2.02_V, 2.01_V, 2.01_V });
    Measure.inject( "Osc2.Frequency.Frequency", { 1000.1_Hz, 1000.0_Hz, 1000.0_Hz });
    EXPECT_TRUE( verdictOf( picoResolutions));

    Measure.inject( "Osc2.Vpp.Voltage",         { 2.02_V, 2.01_V, 1.20_V });
    Measure.inject( "Osc2.Frequency.Frequency", { 1000.1_Hz, 1000.0_Hz, 1000.0_Hz });
    EXPECT_FALSE( verdictOf( picoResolutions));
}

TEST_F( PicoIntoWfgFixture, ASquareIsCheckedOnItsDutyCycle)
{
    Measure.inject( "Osc2.Vtop.Voltage",        1.0_V);
    Measure.inject( "Osc2.Vbase.Voltage",       Voltage{ -1.0 });
    Measure.inject( "Osc2.PositiveWidth.Time",  250.4_us);
    Measure.inject( "Osc2.NegativeWidth.Time",  749.6_us);
    EXPECT_TRUE( verdictOf( picoSquareDutyCycle));

    Measure.inject( "Osc2.Vtop.Voltage",        1.0_V);
    Measure.inject( "Osc2.Vbase.Voltage",       Voltage{ -1.0 });
    Measure.inject( "Osc2.PositiveWidth.Time",  500.0_us);
    Measure.inject( "Osc2.NegativeWidth.Time",  500.0_us);
    EXPECT_FALSE( verdictOf( picoSquareDutyCycle));
}

TEST_F( PicoIntoWfgFixture, ARampAndATriangleAreToldApartByTheirEdges)
{
    Measure.inject( "Osc2.RiseTime.Time", { 0.80_ms, 0.40_ms });
    Measure.inject( "Osc2.FallTime.Time", { Time{ 2.0e-6 }, 0.40_ms });
    EXPECT_TRUE( verdictOf( picoRampAndTriangle));

    Measure.inject( "Osc2.RiseTime.Time", { 0.40_ms, 0.40_ms });
    Measure.inject( "Osc2.FallTime.Time", { 0.40_ms, 0.40_ms });
    EXPECT_FALSE( verdictOf( picoRampAndTriangle));
}

TEST_F( PicoIntoWfgFixture, DcLevelsEitherSideOfZero)
{
    Measure.inject( "Osc2.Vaverage.Voltage", { 1.51_V, Voltage{ -2.01 } });
    EXPECT_TRUE( verdictOf( picoDcLevels));

    Measure.inject( "Osc2.Vaverage.Voltage", { 1.51_V, 2.01_V });
    EXPECT_FALSE( verdictOf( picoDcLevels));
}

//
// AC coupling has to take the offset out; a record that still averages 1 V
// after the coupling changed means the coupling never reached the channel.
//
TEST_F( PicoIntoWfgFixture, AcCouplingTakesTheOffsetOut)
{
    Measure.inject( "Osc2.Vaverage.Voltage", { 1.01_V, 0.003_V });
    EXPECT_TRUE( verdictOf( picoCoupling));

    Measure.inject( "Osc2.Vaverage.Voltage", { 1.01_V, 1.01_V });
    EXPECT_FALSE( verdictOf( picoCoupling));
}

TEST_F( PicoIntoWfgFixture, TheBandwidthLimitLeavesAKilohertzSineAlone)
{
    Measure.inject( "Osc2.Vpp.Voltage", { 2.01_V, 2.01_V });
    EXPECT_TRUE( verdictOf( picoBandwidthLimit));
}

TEST_F( PicoIntoWfgFixture, BothTriggerSlopesGiveTheFrequency)
{
    Measure.inject( "Osc2.Frequency.Frequency", { 1000.1_Hz, 999.9_Hz });
    EXPECT_TRUE( verdictOf( picoTriggerSlopes));

    Measure.inject( "Osc2.Frequency.Frequency", { 1000.1_Hz, 499.9_Hz });
    EXPECT_FALSE( verdictOf( picoTriggerSlopes));
}

//
// -- Capture -------------------------------------------------------------------
//

TEST_F( PicoIntoWfgFixture, ACompletedCaptureWithBothTracesPasses)
{
    Await.inject( "Osc2.Acquisition", true);
    Fetch.inject( "Osc2.Channel1", sineTrace());
    Fetch.inject( "Osc2.Channel2", sineTrace());

    EXPECT_TRUE( verdictOf( picoCapture));
}

//
// A capture that never completed fails and fetches nothing: no trace is
// injected, so a script that fetched anyway would throw rather than pass.
//
TEST_F( PicoIntoWfgFixture, ACaptureThatNeverCompletedFailsAndFetchesNothing)
{
    Await.inject( "Osc2.Acquisition", false);

    bool verdict = true;

    EXPECT_NO_THROW( verdict = verdictOf( picoCapture));
    EXPECT_FALSE( verdict);
}

//
// A trace scaled wrong -- a count-to-volts conversion off by the range -- fails
// even though the capture completed.
//
TEST_F( PicoIntoWfgFixture, ATraceScaledWrongFailsEvenThoughTheCaptureCompleted)
{
    Await.inject( "Osc2.Acquisition", true);
    Fetch.inject( "Osc2.Channel1", sineTrace());
    Fetch.inject( "Osc2.Channel2", sineTrace( 2.5));

    EXPECT_FALSE( verdictOf( picoCapture));
}

TEST_F( PicoIntoWfgFixture, TwoChannelsAreToldApartByTheirFrequencies)
{
    Measure.inject( "Osc2.Frequency.Frequency", { 1000.1_Hz, 5000.3_Hz });
    EXPECT_TRUE( verdictOf( picoChannelsIndependent));

    // Both channels reading channel A's signal.
    Measure.inject( "Osc2.Frequency.Frequency", { 1000.1_Hz, 1000.1_Hz });
    EXPECT_FALSE( verdictOf( picoChannelsIndependent));
}

TEST_F( PicoIntoWfgFixture, RemoveMustLeaveOnlyTheScopesNoise)
{
    Measure.inject( "Osc2.Vpp.Voltage", { 2.01_V, 0.03_V });
    EXPECT_TRUE( verdictOf( picoOutputOff));

    Measure.inject( "Osc2.Vpp.Voltage", { 2.01_V, 2.01_V });
    EXPECT_FALSE( verdictOf( picoOutputOff));
}
