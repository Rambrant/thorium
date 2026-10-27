#include "dev/suite/scripts.hpp"

//
// The PicoGenerator scripts, with the scope's readings injected and the bench
// detached -- so neither face of box Pico1 is reached. Same arrangement as
// test_scope_generator.cpp, and the same keys with the Pico's row: Osc2.
// <measurement>.<quantity>, a key read more than once injected as a sequence
// in read order.
//
// What a detached run checks is the script's logic -- what it reads, in what
// order, and the verdict each reading earns -- not the generator's limits,
// which the driver checks in applyGenerator() and a detached Apply never
// reaches.
//
#include "core/testing/verdict.hpp"

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
    struct PicoGeneratorFixture : ::testing::Test
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

TEST_F( PicoGeneratorFixture, AHealthySinePasses)
{
    Measure.inject( "Osc2.Vpp.Voltage",         2.02_V);
    Measure.inject( "Osc2.Frequency.Frequency", 1000.1_Hz);
    Measure.inject( "Osc2.Vaverage.Voltage",    0.004_V);
    Measure.inject( "Osc2.Vrms.Voltage",        0.712_V);
    EXPECT_TRUE( verdictOf( pgenSine));
}

TEST_F( PicoGeneratorFixture, ASineAtHalfItsAmplitudeFails)
{
    Measure.inject( "Osc2.Vpp.Voltage",         1.0_V);
    Measure.inject( "Osc2.Frequency.Frequency", 1000.1_Hz);
    Measure.inject( "Osc2.Vaverage.Voltage",    0.004_V);
    Measure.inject( "Osc2.Vrms.Voltage",        0.35_V);
    EXPECT_FALSE( verdictOf( pgenSine));
}

TEST_F( PicoGeneratorFixture, FourDecadesOfFrequencyEachLandInTheirOwnWindow)
{
    Measure.inject( "Osc2.Frequency.Frequency", { 100.1_Hz, 1000.2_Hz, 10.01_kHz, 1.001_MHz });
    EXPECT_TRUE( verdictOf( pgenFrequencySteps));

    // The frequency that never changed from the first step.
    Measure.inject( "Osc2.Frequency.Frequency", { 100.1_Hz, 100.1_Hz, 100.1_Hz, 100.1_Hz });
    EXPECT_FALSE( verdictOf( pgenFrequencySteps));
}

TEST_F( PicoGeneratorFixture, ASquareIsHalfHighAndHalfLow)
{
    Measure.inject( "Osc2.Vtop.Voltage",        1.0_V);
    Measure.inject( "Osc2.Vbase.Voltage",       Voltage{ -1.0 });
    Measure.inject( "Osc2.PositiveWidth.Time",  500.4_us);
    Measure.inject( "Osc2.NegativeWidth.Time",  499.6_us);
    EXPECT_TRUE( verdictOf( pgenSquare));

    Measure.inject( "Osc2.Vtop.Voltage",        1.0_V);
    Measure.inject( "Osc2.Vbase.Voltage",       Voltage{ -1.0 });
    Measure.inject( "Osc2.PositiveWidth.Time",  250.0_us);
    Measure.inject( "Osc2.NegativeWidth.Time",  750.0_us);
    EXPECT_FALSE( verdictOf( pgenSquare));
}

//
// Rising sawtooth, falling sawtooth, triangle: six edge readings, and each shape
// has to be told from the others by them.
//
TEST_F( PicoGeneratorFixture, ThreeSlopedShapesAreToldApartByTheirEdges)
{
    Measure.inject( "Osc2.RiseTime.Time", { 0.80_ms, Time{ 2.0e-6 }, 0.40_ms });
    Measure.inject( "Osc2.FallTime.Time", { Time{ 2.0e-6 }, 0.80_ms, 0.40_ms });
    EXPECT_TRUE( verdictOf( pgenRamps));

    // Both ramps came out as triangles: the shape never reached the SDK.
    Measure.inject( "Osc2.RiseTime.Time", { 0.40_ms, 0.40_ms, 0.40_ms });
    Measure.inject( "Osc2.FallTime.Time", { 0.40_ms, 0.40_ms, 0.40_ms });
    EXPECT_FALSE( verdictOf( pgenRamps));
}

TEST_F( PicoGeneratorFixture, OffsetAndAmplitudeMustBothBeRight)
{
    Measure.inject( "Osc2.Vmax.Voltage",     1.51_V);
    Measure.inject( "Osc2.Vmin.Voltage",     Voltage{ -0.49 });
    Measure.inject( "Osc2.Vaverage.Voltage", 0.51_V);
    EXPECT_TRUE( verdictOf( pgenAmplitudeAndOffset));

    // The offset applied with the wrong sign.
    Measure.inject( "Osc2.Vmax.Voltage",     0.51_V);
    Measure.inject( "Osc2.Vmin.Voltage",     Voltage{ -1.49 });
    Measure.inject( "Osc2.Vaverage.Voltage", Voltage{ -0.49 });
    EXPECT_FALSE( verdictOf( pgenAmplitudeAndOffset));
}

TEST_F( PicoGeneratorFixture, DcLevelsEitherSideOfZero)
{
    Measure.inject( "Osc2.Vaverage.Voltage", { 1.51_V, Voltage{ -1.51 } });
    EXPECT_TRUE( verdictOf( pgenDcLevels));

    Measure.inject( "Osc2.Vaverage.Voltage", { 1.51_V, 1.51_V });
    EXPECT_FALSE( verdictOf( pgenDcLevels));
}

TEST_F( PicoGeneratorFixture, NoiseIsABandNotAValue)
{
    Measure.inject( "Osc2.Vrms.Voltage",     0.16_V);
    Measure.inject( "Osc2.Vaverage.Voltage", 0.003_V);
    EXPECT_TRUE( verdictOf( pgenNoise));

    Measure.inject( "Osc2.Vrms.Voltage",     0.003_V);
    Measure.inject( "Osc2.Vaverage.Voltage", 0.003_V);
    EXPECT_FALSE( verdictOf( pgenNoise));
}

TEST_F( PicoGeneratorFixture, RemoveMustLeaveOnlyTheScopesNoise)
{
    Measure.inject( "Osc2.Vpp.Voltage", { 2.01_V, 0.03_V });
    EXPECT_TRUE( verdictOf( pgenOutputOff));

    Measure.inject( "Osc2.Vpp.Voltage", { 2.01_V, 2.01_V });
    EXPECT_FALSE( verdictOf( pgenOutputOff));
}
