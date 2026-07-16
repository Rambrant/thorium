#include "hal/instrument.hpp"

#include <gtest/gtest.h>

using namespace core::literals;
using namespace core::quantities;

TEST( HalInstrument, OscilloscopeVoltagePortReturnsSimulatedReading)
{
    hal::Oscilloscope osc1{ hal::InstrumentId::Osc1 };
    osc1.setSimulatedVoltage( 3.3_V);

    auto port = osc1.voltage();

    EXPECT_DOUBLE_EQ( port.rawMeasure().value(), 3.3);
    EXPECT_EQ( port.instrumentId(), hal::InstrumentId::Osc1);
}

TEST( HalInstrument, DmmExposesBothVoltageAndCurrentPorts)
{
    hal::Dmm dmm1{ hal::InstrumentId::Dmm1 };
    dmm1.setSimulatedVoltage( 5.02_V);
    dmm1.setSimulatedCurrent( 0.5_A);

    EXPECT_DOUBLE_EQ( dmm1.voltage().rawMeasure().value(), 5.02);
    EXPECT_DOUBLE_EQ( dmm1.current().rawMeasure().value(), 0.5);
}

TEST( HalInstrument, TwoDmmsAreDistinguishableByInstrumentId)
{
    hal::Dmm dmm1{ hal::InstrumentId::Dmm1 };
    hal::Dmm dmm2{ hal::InstrumentId::Dmm2 };

    EXPECT_EQ( dmm1.voltage().instrumentId(), hal::InstrumentId::Dmm1);
    EXPECT_EQ( dmm2.voltage().instrumentId(), hal::InstrumentId::Dmm2);
}

TEST( HalInstrument, DmmAcVoltagePortReadsTheAcSimulatedReading)
{
    hal::Dmm dmm1{ hal::InstrumentId::Dmm1 };
    dmm1.setSimulatedVoltage( 5.0_V);
    dmm1.setSimulatedAcVoltage( 230.0_V);

    EXPECT_DOUBLE_EQ( dmm1.acVoltage().rawMeasure().value(), 230.0);
}

TEST( HalInstrument, DmmAcCurrentPortReadsTheAcSimulatedReading)
{
    hal::Dmm dmm1{ hal::InstrumentId::Dmm1 };
    dmm1.setSimulatedCurrent( 0.5_A);
    dmm1.setSimulatedAcCurrent( 1.2_A);

    EXPECT_DOUBLE_EQ( dmm1.acCurrent().rawMeasure().value(), 1.2);
}

TEST( HalInstrument, DmmVoltageAfterAcVoltageSwitchesBackToDcMode)
{
    hal::Dmm dmm1{ hal::InstrumentId::Dmm1 };
    dmm1.setSimulatedVoltage( 5.0_V);
    dmm1.setSimulatedAcVoltage( 230.0_V);

    (void)dmm1.acVoltage();

    EXPECT_DOUBLE_EQ( dmm1.voltage().rawMeasure().value(), 5.0);
}

TEST( HalInstrument, DmmModeIsSharedAcrossPortHandlesHeldPastAModeSwitch)
{
    // Documents the known, accepted sharp edge from Dmm's own comment: a
    // port handle obtained before a mode switch still reads whichever mode
    // is current when rawMeasure() is eventually called, not the mode active
    // when the handle was created.
    hal::Dmm dmm1{ hal::InstrumentId::Dmm1 };
    dmm1.setSimulatedVoltage( 5.0_V);
    dmm1.setSimulatedAcVoltage( 230.0_V);

    auto dcPort = dmm1.voltage();
    (void)dmm1.acVoltage();

    EXPECT_DOUBLE_EQ( dcPort.rawMeasure().value(), 230.0);
}

TEST( HalInstrument, PortRangeNplcAndFrequencyChainWithoutAffectingTheReading)
{
    hal::Dmm dmm1{ hal::InstrumentId::Dmm1 };
    dmm1.setSimulatedVoltage( 5.02_V);

    auto port = dmm1.voltage().range( 20.0_V).nplc( 10).frequency( 50.0_Hz);

    EXPECT_DOUBLE_EQ( port.rawMeasure().value(), 5.02);
    EXPECT_EQ( port.setup().Range,     std::optional{ 20.0_V});
    EXPECT_EQ( port.setup().Nplc,      std::optional{ 10});
    EXPECT_EQ( port.setup().Frequency, std::optional{ 50.0_Hz});
}

TEST( HalInstrument, ToStringNamesEachInstrument)
{
    EXPECT_EQ( to_string( hal::InstrumentId::Dmm1), "Dmm1");
    EXPECT_EQ( to_string( hal::InstrumentId::Dmm2), "Dmm2");
    EXPECT_EQ( to_string( hal::InstrumentId::Osc1), "Osc1");
    EXPECT_EQ( to_string( hal::InstrumentId::DcP1), "DcP1");
    EXPECT_EQ( to_string( hal::InstrumentId::DcP2), "DcP2");
    EXPECT_EQ( to_string( hal::InstrumentId::DcP3), "DcP3");
    EXPECT_EQ( to_string( hal::InstrumentId::DcP4), "DcP4");
    EXPECT_EQ( to_string( hal::InstrumentId::AcP1), "AcP1");
}
