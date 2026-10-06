#include "dev/suite/scripts.hpp"

//
// The ScopeGenerator scripts, with the scope's readings injected and the bench
// detached -- so neither face of box Scope1 is reached. Same arrangement as
// test_wfg_into_scope.cpp, and the same keys: Osc1.<measurement>.<quantity>, a
// key read more than once injected as a sequence in read order.
//
// What a detached run checks is the script's logic -- what it reads, in what
// order, and the verdict each reading earns -- not the generator's limits,
// which WGEN::applyGenerator checks and a detached Apply never reaches.
//
#include "suite/tests/verdict.hpp"

#include "core/quantities/quantity.hpp"
#include "core/session/bench.hpp"
#include "hal/topology/active_instruments.hpp"
#include "hal/verbs/measure.hpp"

#include <gtest/gtest.h>

using core::quantities::Time;
using core::quantities::Voltage;

using namespace core::literals;

namespace
{
    struct ScopeGeneratorFixture : ::testing::Test
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
} // namespace

TEST_F( ScopeGeneratorFixture, AHealthySinePasses)
{
    Measure.inject( "Osc1.Vpp.Voltage",         2.02_V);
    Measure.inject( "Osc1.Frequency.Frequency", 1000.1_Hz);
    Measure.inject( "Osc1.Vaverage.Voltage",    0.004_V);
    Measure.inject( "Osc1.Vrms.Voltage",        0.712_V);
    EXPECT_TRUE( verdictOf( wgenSine));
}

TEST_F( ScopeGeneratorFixture, ASineAtTwiceItsAmplitudeFails)
{
    Measure.inject( "Osc1.Vpp.Voltage",         4.03_V);
    Measure.inject( "Osc1.Frequency.Frequency", 1000.1_Hz);
    Measure.inject( "Osc1.Vaverage.Voltage",    0.004_V);
    Measure.inject( "Osc1.Vrms.Voltage",        1.42_V);
    EXPECT_FALSE( verdictOf( wgenSine));
}

TEST_F( ScopeGeneratorFixture, ASquareIsCheckedOnItsDutyCycle)
{
    Measure.inject( "Osc1.Vtop.Voltage",        1.0_V);
    Measure.inject( "Osc1.Vbase.Voltage",       Voltage{ -1.0 });
    Measure.inject( "Osc1.PositiveWidth.Time",  250.4_us);
    Measure.inject( "Osc1.NegativeWidth.Time",  749.6_us);
    EXPECT_TRUE( verdictOf( wgenSquareDutyCycle));

    // 50%: the duty cycle never reached :WGEN:FUNCtion:SQUare:DCYCle.
    Measure.inject( "Osc1.Vtop.Voltage",        1.0_V);
    Measure.inject( "Osc1.Vbase.Voltage",       Voltage{ -1.0 });
    Measure.inject( "Osc1.PositiveWidth.Time",  500.0_us);
    Measure.inject( "Osc1.NegativeWidth.Time",  500.0_us);
    EXPECT_FALSE( verdictOf( wgenSquareDutyCycle));
}

TEST_F( ScopeGeneratorFixture, ARampIsASawtoothOnlyWithItsSymmetry)
{
    Measure.inject( "Osc1.RiseTime.Time", 0.80_ms);
    Measure.inject( "Osc1.FallTime.Time", Time{ 2.0e-6 });
    EXPECT_TRUE( verdictOf( wgenRampSymmetry));

    // A 50% ramp: a triangle, with as slow a reset as its rise.
    Measure.inject( "Osc1.RiseTime.Time", 0.40_ms);
    Measure.inject( "Osc1.FallTime.Time", 0.40_ms);
    EXPECT_FALSE( verdictOf( wgenRampSymmetry));
}

TEST_F( ScopeGeneratorFixture, APulseIsCheckedOnItsWidth)
{
    Measure.inject( "Osc1.PositiveWidth.Time", 100.5_us);
    EXPECT_TRUE( verdictOf( wgenPulseWidth));

    Measure.inject( "Osc1.PositiveWidth.Time", 500.0_us);
    EXPECT_FALSE( verdictOf( wgenPulseWidth));
}

TEST_F( ScopeGeneratorFixture, DcLevelsEitherSideOfZero)
{
    Measure.inject( "Osc1.Vaverage.Voltage", { 1.51_V, Voltage{ -1.99 } });
    EXPECT_TRUE( verdictOf( wgenDcLevels));

    Measure.inject( "Osc1.Vaverage.Voltage", { 1.51_V, 1.51_V });
    EXPECT_FALSE( verdictOf( wgenDcLevels));
}

TEST_F( ScopeGeneratorFixture, TheTerminationMustDoubleAndThenNot)
{
    Measure.inject( "Osc1.Vpp.Voltage", { 2.01_V, 1.01_V });
    EXPECT_TRUE( verdictOf( wgenTermination));

    // OUTPut:LOAD never arrived: only the pair can see it.
    Measure.inject( "Osc1.Vpp.Voltage", { 1.01_V, 1.01_V });
    EXPECT_FALSE( verdictOf( wgenTermination));
}

TEST_F( ScopeGeneratorFixture, RemoveMustLeaveOnlyTheScopesNoise)
{
    Measure.inject( "Osc1.Vpp.Voltage", { 2.02_V, 0.02_V });
    EXPECT_TRUE( verdictOf( wgenOutputOff));

    Measure.inject( "Osc1.Vpp.Voltage", { 2.02_V, 2.02_V });
    EXPECT_FALSE( verdictOf( wgenOutputOff));
}

//
// Detached, a script that Applies and Removes leaves the generator exactly as
// it was. If this fails, these tests were driving the generator on the desk.
//
TEST_F( ScopeGeneratorFixture, ADetachedRunLeavesTheGeneratorUntouched)
{
    Measure.inject( "Osc1.Vpp.Voltage", { 2.02_V, 0.02_V });
    static_cast<void>( verdictOf( wgenOutputOff));

    EXPECT_FALSE( Wfg2.isEnabled());
}
