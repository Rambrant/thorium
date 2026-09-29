#include "dev/suite/scripts.hpp"

//
// The WfgIntoScope scripts, with the scope's readings injected and the bench
// detached -- so neither the generator nor the scope is reached. Both are Usb
// rows on this desk, and an attached Apply or Setup would go looking for them
// through VISA from a unit test.
//
// What a detached run can check is the part of each script that is logic: that
// it reads what it should, in the order it should, and turns each reading into
// the verdict it should. What it cannot is whether the settings are inside the
// 33522B's limits -- the driver checks those in applyWaveform(), which a
// detached Apply never calls (see the script file's own comment).
//
// Keys are the scope's -- Osc1.<measurement>.<quantity>, no channel -- so a key
// read more than once in a script is injected as a sequence in read order.
//
#include "suite/tests/verdict.hpp"

#include "core/quantities/quantity.hpp"
#include "core/session/bench.hpp"
#include "hal/topology/active_instruments.hpp"
#include "hal/verbs/measure.hpp"

#include <gtest/gtest.h>

using core::quantities::Frequency;
using core::quantities::Time;
using core::quantities::Voltage;

using namespace core::literals;

namespace
{
    struct WfgIntoScopeFixture : ::testing::Test
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
        Measure.inject( "Osc1.Vpp.Voltage",         2.02_V);
        Measure.inject( "Osc1.Frequency.Frequency", 1000.1_Hz);
        Measure.inject( "Osc1.Vaverage.Voltage",    0.004_V);
        Measure.inject( "Osc1.Vrms.Voltage",        0.712_V);
    }
} // namespace

//
// -- Sine, per channel ---------------------------------------------------------
//

TEST_F( WfgIntoScopeFixture, AHealthySinePassesOnEitherChannel)
{
    injectHealthySine();
    EXPECT_TRUE( verdictOf( wfgSineCh1));

    injectHealthySine();
    EXPECT_TRUE( verdictOf( wfgSineCh2));
}

//
// The termination mistake seen from the sine script: a generator left on its
// fifty-ohm default doubles into the scope, and the sine test must not pass it.
//
TEST_F( WfgIntoScopeFixture, ASineAtTwiceItsAmplitudeFails)
{
    Measure.inject( "Osc1.Vpp.Voltage",         4.03_V);
    Measure.inject( "Osc1.Frequency.Frequency", 1000.1_Hz);
    Measure.inject( "Osc1.Vaverage.Voltage",    0.004_V);
    Measure.inject( "Osc1.Vrms.Voltage",        1.42_V);

    EXPECT_FALSE( verdictOf( wfgSineCh1));
}

//
// -- Frequency -------------------------------------------------------------------
//

TEST_F( WfgIntoScopeFixture, FourDecadesOfFrequencyEachLandInTheirOwnWindow)
{
    Measure.inject( "Osc1.Frequency.Frequency", { 100.1_Hz, 1000.2_Hz, 10.01_kHz, 1.001_MHz });
    EXPECT_TRUE( verdictOf( wfgFrequencySteps));

    // One decade off at the top -- a generator stuck on the previous setting.
    Measure.inject( "Osc1.Frequency.Frequency", { 100.1_Hz, 1000.2_Hz, 10.01_kHz, 10.01_kHz });
    EXPECT_FALSE( verdictOf( wfgFrequencySteps));
}

//
// -- Amplitude and offset --------------------------------------------------------
//

TEST_F( WfgIntoScopeFixture, OffsetAndAmplitudeMustBothBeRight)
{
    Measure.inject( "Osc1.Vmax.Voltage",     3.02_V);
    Measure.inject( "Osc1.Vmin.Voltage",     Voltage{ -0.98 });
    Measure.inject( "Osc1.Vaverage.Voltage", 1.01_V);
    EXPECT_TRUE( verdictOf( wfgAmplitudeAndOffset));

    //
    // The amplitude right and the offset missing: the same 4 Vpp, centred on
    // zero. Max and min both fail, and the mean does -- three failures from one
    // setting that did not arrive.
    //
    Measure.inject( "Osc1.Vmax.Voltage",     2.01_V);
    Measure.inject( "Osc1.Vmin.Voltage",     Voltage{ -2.01 });
    Measure.inject( "Osc1.Vaverage.Voltage", 0.0_V);
    EXPECT_FALSE( verdictOf( wfgAmplitudeAndOffset));
}

//
// -- Square, ramp, triangle, pulse ---------------------------------------------
//

TEST_F( WfgIntoScopeFixture, ASquareIsCheckedOnItsDutyCycle)
{
    Measure.inject( "Osc1.Vtop.Voltage",        1.0_V);
    Measure.inject( "Osc1.Vbase.Voltage",       Voltage{ -1.0 });
    Measure.inject( "Osc1.PositiveWidth.Time",  250.4_us);
    Measure.inject( "Osc1.NegativeWidth.Time",  749.6_us);
    EXPECT_TRUE( verdictOf( wfgSquareDutyCycle));

    // 50%: the duty cycle never reached FUNC:SQU:DCYC.
    Measure.inject( "Osc1.Vtop.Voltage",        1.0_V);
    Measure.inject( "Osc1.Vbase.Voltage",       Voltage{ -1.0 });
    Measure.inject( "Osc1.PositiveWidth.Time",  500.0_us);
    Measure.inject( "Osc1.NegativeWidth.Time",  500.0_us);
    EXPECT_FALSE( verdictOf( wfgSquareDutyCycle));
}

TEST_F( WfgIntoScopeFixture, ARampAndATriangleAreToldApartByTheirEdges)
{
    // Sawtooth rise, sawtooth reset, then the triangle's two sides.
    Measure.inject( "Osc1.RiseTime.Time", { 0.80_ms, 0.40_ms });
    Measure.inject( "Osc1.FallTime.Time", { Time{ 2.0e-6 }, 0.40_ms });
    EXPECT_TRUE( verdictOf( wfgRampAndTriangle));

    //
    // Symmetry not applied: the "sawtooth" came out as a 50% ramp, a triangle
    // by another name, and its reset is as slow as its rise.
    //
    Measure.inject( "Osc1.RiseTime.Time", { 0.40_ms, 0.40_ms });
    Measure.inject( "Osc1.FallTime.Time", { 0.40_ms, 0.40_ms });
    EXPECT_FALSE( verdictOf( wfgRampAndTriangle));
}

TEST_F( WfgIntoScopeFixture, APulseIsCheckedOnItsWidth)
{
    Measure.inject( "Osc1.PositiveWidth.Time", 100.5_us);
    EXPECT_TRUE( verdictOf( wfgPulse));

    // The pulse's duty cycle went to the square's command and was ignored.
    Measure.inject( "Osc1.PositiveWidth.Time", 500.0_us);
    EXPECT_FALSE( verdictOf( wfgPulse));
}

//
// -- DC and noise ----------------------------------------------------------------
//

TEST_F( WfgIntoScopeFixture, DcLevelsEitherSideOfZero)
{
    Measure.inject( "Osc1.Vaverage.Voltage", { 1.51_V, Voltage{ -1.99 } });
    EXPECT_TRUE( verdictOf( wfgDcLevels));

    // The negative level refused, the output left at the positive one.
    Measure.inject( "Osc1.Vaverage.Voltage", { 1.51_V, 1.51_V });
    EXPECT_FALSE( verdictOf( wfgDcLevels));
}

TEST_F( WfgIntoScopeFixture, NoiseIsABandNotAValue)
{
    Measure.inject( "Osc1.Vrms.Voltage",     0.16_V);
    Measure.inject( "Osc1.Vaverage.Voltage", 0.003_V);
    EXPECT_TRUE( verdictOf( wfgNoise));

    // No noise at all -- the output never came on.
    Measure.inject( "Osc1.Vrms.Voltage",     0.004_V);
    Measure.inject( "Osc1.Vaverage.Voltage", 0.0_V);
    EXPECT_FALSE( verdictOf( wfgNoise));
}

//
// -- Termination, off, and the two channels ----------------------------------------
//

TEST_F( WfgIntoScopeFixture, TheTerminationMustDoubleAndThenNot)
{
    Measure.inject( "Osc1.Vpp.Voltage", { 2.01_V, 1.01_V });
    EXPECT_TRUE( verdictOf( wfgTermination));

    //
    // Both readings 1 Vpp: OUTPut:LOAD never reached the instrument, so the
    // "fifty ohm" half of the pair is really high impedance too. Only the pair
    // can see this -- the second reading alone would have passed.
    //
    Measure.inject( "Osc1.Vpp.Voltage", { 1.01_V, 1.01_V });
    EXPECT_FALSE( verdictOf( wfgTermination));
}

TEST_F( WfgIntoScopeFixture, RemoveMustLeaveOnlyTheScopesNoise)
{
    Measure.inject( "Osc1.Vpp.Voltage", { 2.02_V, 0.02_V });
    EXPECT_TRUE( verdictOf( wfgOutputOff));

    Measure.inject( "Osc1.Vpp.Voltage", { 2.02_V, 2.02_V });
    EXPECT_FALSE( verdictOf( wfgOutputOff));
}

TEST_F( WfgIntoScopeFixture, TheTwoChannelsMustCarryTheirOwnSignals)
{
    // Channel 1 read first, then channel 2 -- one key, in that order.
    Measure.inject( "Osc1.Frequency.Frequency", { 1000.1_Hz, 5.001_kHz });
    EXPECT_TRUE( verdictOf( wfgChannelsIndependent));

    // Channel 2 carrying channel 1's signal: a SOURce addressed wrongly.
    Measure.inject( "Osc1.Frequency.Frequency", { 1000.1_Hz, 1000.1_Hz });
    EXPECT_FALSE( verdictOf( wfgChannelsIndependent));
}

//
// And the fixture's reason: detached, a script that Applies and Removes leaves
// the generator's own state exactly as it was -- nothing enabled, nothing
// sent. If this fails, these tests were driving the generator on the desk.
//
TEST_F( WfgIntoScopeFixture, ADetachedRunLeavesTheGeneratorUntouched)
{
    Measure.inject( "Osc1.Frequency.Frequency", { 1000.1_Hz, 5.001_kHz });
    static_cast<void>( verdictOf( wfgChannelsIndependent));

    EXPECT_FALSE( Wfg1.isEnabled( 1));
    EXPECT_FALSE( Wfg1.isEnabled( 2));
}
