//
// hal::L4411A's own tests, moved here verbatim from libs/hal/tests/
// test_instrument.cpp when this driver moved out of libs/hal into its own
// directory. Test names are deliberately unchanged (still the HalInstrument
// suite, still L4411A-prefixed) so the move is provably behaviour-preserving:
// the same named tests run, in a different binary. Renaming the suite to
// L4411A and dropping the now-redundant prefix is a reasonable follow-up, but
// it is a separate change from the move, and doing both at once would have
// destroyed the before/after comparison that makes the move safe.
//
// What is left behind in libs/hal/tests/test_instrument.cpp is the part that
// is genuinely about hal's generic mechanism rather than about any driver --
// hal::InstrumentId and to_string() over it.
//
#include "hal/l4411a.hpp"

#include <gtest/gtest.h>

using namespace core::literals;
using namespace core::quantities;

TEST( HalInstrument, L4411AExposesBothVoltageAndCurrentPorts)
{
    hal::L4411A dmm1{ hal::InstrumentId::Dmm1 };
    dmm1.setSimulatedVoltage( 5.02_V);
    dmm1.setSimulatedCurrent( 0.5_A);

    EXPECT_DOUBLE_EQ( dmm1.voltage().rawMeasure().value(), 5.02);
    EXPECT_DOUBLE_EQ( dmm1.current().rawMeasure().value(), 0.5);
}

TEST( HalInstrument, TwoL4411AsAreDistinguishableByInstrumentId)
{
    hal::L4411A dmm1{ hal::InstrumentId::Dmm1 };
    hal::L4411A dmm2{ hal::InstrumentId::Dmm2 };

    EXPECT_EQ( dmm1.voltage().instrumentId(), hal::InstrumentId::Dmm1);
    EXPECT_EQ( dmm2.voltage().instrumentId(), hal::InstrumentId::Dmm2);
}

TEST( HalInstrument, L4411AAcVoltagePortReadsTheAcSimulatedReading)
{
    hal::L4411A dmm1{ hal::InstrumentId::Dmm1 };
    dmm1.setSimulatedVoltage( 5.0_V);
    dmm1.setSimulatedAcVoltage( 230.0_V);

    EXPECT_DOUBLE_EQ( dmm1.acVoltage().rawMeasure().value(), 230.0);
}

TEST( HalInstrument, L4411AAcCurrentPortReadsTheAcSimulatedReading)
{
    hal::L4411A dmm1{ hal::InstrumentId::Dmm1 };
    dmm1.setSimulatedCurrent( 0.5_A);
    dmm1.setSimulatedAcCurrent( 1.2_A);

    EXPECT_DOUBLE_EQ( dmm1.acCurrent().rawMeasure().value(), 1.2);
}

TEST( HalInstrument, L4411AVoltageAfterAcVoltageSwitchesBackToDcMode)
{
    hal::L4411A dmm1{ hal::InstrumentId::Dmm1 };
    dmm1.setSimulatedVoltage( 5.0_V);
    dmm1.setSimulatedAcVoltage( 230.0_V);

    (void)dmm1.acVoltage();

    EXPECT_DOUBLE_EQ( dmm1.voltage().rawMeasure().value(), 5.0);
}

TEST( HalInstrument, L4411AModeIsSharedAcrossPortHandlesHeldPastAModeSwitch)
{
    // Documents the known, accepted sharp edge from L4411A's own comment: a
    // port handle obtained before a mode switch still reads whichever mode
    // is current when rawMeasure() is eventually called, not the mode active
    // when the handle was created.
    hal::L4411A dmm1{ hal::InstrumentId::Dmm1 };
    dmm1.setSimulatedVoltage( 5.0_V);
    dmm1.setSimulatedAcVoltage( 230.0_V);

    auto dcPort = dmm1.voltage();
    (void)dmm1.acVoltage();

    EXPECT_DOUBLE_EQ( dcPort.rawMeasure().value(), 230.0);
}

TEST( HalInstrument, L4411AResistancePortReadsTheTwoWireSimulatedReading)
{
    hal::L4411A dmm1{ hal::InstrumentId::Dmm1 };
    dmm1.setSimulatedResistance( 100.0_Ohm);
    dmm1.setSimulatedFourWireResistance( 99.5_Ohm);

    EXPECT_DOUBLE_EQ( dmm1.resistance().rawMeasure().value(), 100.0);
}

TEST( HalInstrument, L4411AFourWireResistancePortReadsTheFourWireSimulatedReading)
{
    hal::L4411A dmm1{ hal::InstrumentId::Dmm1 };
    dmm1.setSimulatedResistance( 100.0_Ohm);
    dmm1.setSimulatedFourWireResistance( 99.5_Ohm);

    EXPECT_DOUBLE_EQ( dmm1.fourWireResistance().rawMeasure().value(), 99.5);
}

TEST( HalInstrument, L4411AResistanceAfterFourWireResistanceSwitchesBackToTwoWireMode)
{
    hal::L4411A dmm1{ hal::InstrumentId::Dmm1 };
    dmm1.setSimulatedResistance( 100.0_Ohm);
    dmm1.setSimulatedFourWireResistance( 99.5_Ohm);

    (void)dmm1.fourWireResistance();

    EXPECT_DOUBLE_EQ( dmm1.resistance().rawMeasure().value(), 100.0);
}

//
// The sense requirement is in the port's *type*, not in its runtime setup, so
// this is a static_assert rather than an EXPECT -- a two-wire reading and a
// four-wire one are now different types, and core::MeasureEngine branches on
// that with if constexpr.
//
TEST( HalInstrument, L4411AFourWireResistanceRequiresTheSensePathButTwoWireDoesNot)
{
    hal::L4411A dmm1{ hal::InstrumentId::Dmm1 };

    static_assert( decltype( dmm1.resistance())::SenseUse         == core::SensePath::NotUsed);
    static_assert( decltype( dmm1.fourWireResistance())::SenseUse == core::SensePath::Required);

    // The chained setup builders preserve it -- .nplc() on a four-wire port
    // must not quietly hand back a two-wire one.
    static_assert( decltype( dmm1.fourWireResistance().nplc( 10))::SenseUse == core::SensePath::Required);

    (void) dmm1;
}

//
// Generic core::Port machinery rather than anything L4411A-specific, but it
// needs a real driver to exercise it through and this is the one it was
// written against -- so it travels with the driver rather than staying behind
// in a libs/hal test that would then have had to keep including a driver
// header for this one case alone. core/tests/test_port.cpp covers the builder
// chain itself without any instrument at all.
//
TEST( HalInstrument, PortRangeNplcAndFrequencyChainWithoutAffectingTheReading)
{
    hal::L4411A dmm1{ hal::InstrumentId::Dmm1 };
    dmm1.setSimulatedVoltage( 5.02_V);

    auto port = dmm1.voltage().range( 20.0_V).nplc( 10).frequency( 50.0_Hz);

    EXPECT_DOUBLE_EQ( port.rawMeasure().value(), 5.02);
    EXPECT_EQ( port.setup().Range,     std::optional{ 20.0_V});
    EXPECT_EQ( port.setup().Nplc,      std::optional{ 10});
    EXPECT_EQ( port.setup().Frequency, std::optional{ 50.0_Hz});
}
