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

TEST( HalInstrument, PowerSupplySourcesRatherThanMeasures)
{
    hal::PowerSupply supply{ hal::InstrumentId::PowerSupply1 };

    EXPECT_FALSE( supply.isEnabled());

    supply.setOutput( 12.0_V);
    supply.enable();

    EXPECT_TRUE( supply.isEnabled());
    EXPECT_DOUBLE_EQ( supply.output().value(), 12.0);
}

TEST( HalInstrument, ToStringNamesEachInstrument)
{
    EXPECT_EQ( to_string( hal::InstrumentId::Dmm1), "Dmm1");
    EXPECT_EQ( to_string( hal::InstrumentId::Dmm2), "Dmm2");
}
