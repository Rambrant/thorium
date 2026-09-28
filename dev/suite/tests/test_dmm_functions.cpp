#include "dev/suite/scripts.hpp"

//
// The DmmFunctions scripts, with their readings injected -- the same shape as
// test_dmm_self_check.cpp and for the same reasons; see that file on the keys
// and on why every test restores live readings afterwards.
//
// What these add over that file is the key collisions, which are the one thing
// about these scripts a reader could get wrong. A point-free reading keys as
// "<instrument>.<quantity>", so DC and AC volts are both "Dmm1.Voltage", DC
// and AC current both "Dmm1.Current", and 2-wire and 4-wire ohms both
// "Dmm1.Resistance". Each script reads one function, so within a test there is
// never a collision -- but it is why a test here injects by quantity and not
// by function name.
//
#include "suite/tests/verdict.hpp"

#include "core/quantities/quantity.hpp"
#include "hal/topology/active_instruments.hpp"
#include "hal/verbs/measure.hpp"

#include <gtest/gtest.h>

using core::quantities::Capacitance;
using core::quantities::Current;
using core::quantities::Frequency;
using core::quantities::Resistance;
using core::quantities::Voltage;

using namespace core::literals;

namespace
{
    struct DmmFunctionsFixture : ::testing::Test
    {
        protected:

            void TearDown() override
            {
                Measure.useLive();
            }
    };

    using Resolution = hal::keysight_edu34450a::EDU34450A::Resolution;
} // namespace

//
// -- One nominal reading each: every script passes on its own reference ---
//
// A table rather than one TEST per script, so that adding a function to the
// meter's list and forgetting its test is a visibly shorter table.
//
TEST_F( DmmFunctionsFixture, EachScriptPassesOnItsNominalReference)
{
    Measure.inject( "Dmm1.Voltage", 1.0_V);
    EXPECT_TRUE( verdictOf( dmmAcVoltage)) << "dmmAcVoltage";

    Measure.inject( "Dmm1.Current", 100.0_mA);
    EXPECT_TRUE( verdictOf( dmmDcCurrent)) << "dmmDcCurrent";

    Measure.inject( "Dmm1.Current", 100.0_mA);
    EXPECT_TRUE( verdictOf( dmmAcCurrent)) << "dmmAcCurrent";

    Measure.inject( "Dmm1.Resistance", 1.0_kOhm);
    EXPECT_TRUE( verdictOf( dmmResistance)) << "dmmResistance";

    Measure.inject( "Dmm1.Resistance", 1.0_kOhm);
    EXPECT_TRUE( verdictOf( dmmFourWireResistance)) << "dmmFourWireResistance";

    Measure.inject( "Dmm1.Frequency", 1000.0_Hz);
    EXPECT_TRUE( verdictOf( dmmFrequency)) << "dmmFrequency";

    Measure.inject( "Dmm1.Capacitance", 470.0_uF);
    EXPECT_TRUE( verdictOf( dmmCapacitance)) << "dmmCapacitance";
}

//
// -- And fails off it ------------------------------------------------------
//
// Each just outside its epsilon, in the direction a real fault would push it.
//
TEST_F( DmmFunctionsFixture, EachScriptFailsJustOutsideItsTolerance)
{
    Measure.inject( "Dmm1.Voltage", 0.97_V);
    EXPECT_FALSE( verdictOf( dmmAcVoltage)) << "dmmAcVoltage";

    Measure.inject( "Dmm1.Current", 103.0_mA);
    EXPECT_FALSE( verdictOf( dmmDcCurrent)) << "dmmDcCurrent";

    Measure.inject( "Dmm1.Current", 96.0_mA);
    EXPECT_FALSE( verdictOf( dmmAcCurrent)) << "dmmAcCurrent";

    Measure.inject( "Dmm1.Resistance", 1.002_kOhm);
    EXPECT_FALSE( verdictOf( dmmResistance)) << "dmmResistance";

    Measure.inject( "Dmm1.Frequency", 1002.0_Hz);
    EXPECT_FALSE( verdictOf( dmmFrequency)) << "dmmFrequency";

    Measure.inject( "Dmm1.Capacitance", 400.0_uF);
    EXPECT_FALSE( verdictOf( dmmCapacitance)) << "dmmCapacitance";
}

//
// The two resistance scripts read one resistor and differ only in their
// epsilon -- which is the whole point of having both, so it is asserted: a
// reading 1.3 Ohm high is inside what 2-wire allows for the leads, and outside
// what 4-wire allows once the leads are sensed out. That combination is what a
// broken sense lead looks like.
//
TEST_F( DmmFunctionsFixture, FourWireIsHeldTighterThanTwoWire)
{
    Measure.inject( "Dmm1.Resistance", Resistance{ 1001.3 });
    EXPECT_TRUE( verdictOf( dmmResistance));

    Measure.inject( "Dmm1.Resistance", Resistance{ 1001.3 });
    EXPECT_FALSE( verdictOf( dmmFourWireResistance));
}

//
// -- DC volts reads twice --------------------------------------------------
//
// Autoranged and on a fixed range, both against the 5 V cell -- so both
// readings have to be queued, and either one failing fails the test.
//
TEST_F( DmmFunctionsFixture, DcVoltageChecksBothTheAutorangedAndTheRangedReading)
{
    Measure.inject( "Dmm1.Voltage", { 5.01_V, 5.02_V });
    EXPECT_TRUE( verdictOf( dmmDcVoltage));

    Measure.inject( "Dmm1.Voltage", { 5.01_V, 5.20_V });
    EXPECT_FALSE( verdictOf( dmmDcVoltage)) << "a bad reading on the fixed range must fail";
}

//
// -- Resolution: two readings, and the meter put back ----------------------
//
TEST_F( DmmFunctionsFixture, ResolutionChecksBothSettings)
{
    Measure.inject( "Dmm1.Voltage", { 5.01_V, 5.01_V });
    EXPECT_TRUE( verdictOf( dmmResolution));

    // Fast is the last and noisiest, and a failure there fails the test.
    Measure.inject( "Dmm1.Voltage", { 5.01_V, 5.30_V });
    EXPECT_FALSE( verdictOf( dmmResolution));
}

//
// The script changes instrument state that outlives it, so it has to restore
// it -- or the next test in the run takes its readings at Fast without
// anybody having asked for that. Asserted from a setting that is not the
// driver's default, so a script that "restored" by resetting to Slow would
// not pass by accident.
//
TEST_F( DmmFunctionsFixture, ResolutionLeavesTheMeterAsItFoundIt)
{
    Dmm1.setResolution( Resolution::Fast);

    Measure.inject( "Dmm1.Voltage", { 5.01_V, 5.01_V });
    static_cast<void>( verdictOf( dmmResolution));

    EXPECT_EQ( Dmm1.resolution(), Resolution::Fast);

    Dmm1.setResolution( Resolution::Slow);   // the driver's default, for whichever test runs next
}
