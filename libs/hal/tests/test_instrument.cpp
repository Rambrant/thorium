#include "hal/instrument.hpp"
#include "hal/l4411a.hpp"
#include "hal/dso8064.hpp"

#include <gtest/gtest.h>

using namespace core::literals;
using namespace core::quantities;

namespace
{
    using namespace core::literals;
    using namespace core::quantities;

    //
    // ValidDso8064Channel's compile-time bound, checked the concept-wrapped
    // way -- see core/tests/test_static_constraints.cpp's own comment for
    // why a bare static_assert(!requires{...}) is unreliable and this
    // routing-through-a-concept form isn't.
    //
    template<unsigned N>
    concept CanChannel = requires( hal::DSO8064 & osc) { osc.template channel<N>(); };

    static_assert(  CanChannel<1> );
    static_assert(  CanChannel<4> );
    static_assert( !CanChannel<0> );
    static_assert( !CanChannel<5> );
} // namespace

TEST( HalInstrument, DSO8064VppPortReturnsSimulatedReading)
{
    hal::DSO8064 osc1{ hal::InstrumentId::Osc1 };
    osc1.setSimulatedVpp( 1, 3.3_V);

    auto port = osc1.channel<1>().vpp();

    EXPECT_DOUBLE_EQ( port.rawMeasure().value(), 3.3);
    EXPECT_EQ( port.instrumentId(), hal::InstrumentId::Osc1);
}

TEST( HalInstrument, DSO8064PortOutlivesTheTemporaryChannelViewThatCreatedIt)
{
    // The dangling-reference trap an earlier version of this file had:
    // channel<1>() returns a temporary DSO8064Channel<1>, gone by the next
    // statement -- Port must bind to the real, long-lived DSO8064&, not to
    // that temporary, or this reads garbage (or crashes) instead of 3.3.
    // See hal/dso8064.hpp's own comment on DSO8064Channel for why.
    hal::DSO8064 osc1{ hal::InstrumentId::Osc1 };
    osc1.setSimulatedVpp( 1, 3.3_V);

    auto port = osc1.channel<1>().vpp();  // the channel view temporary is gone after this line

    EXPECT_DOUBLE_EQ( port.rawMeasure().value(), 3.3);  // still valid here
}

TEST( HalInstrument, DSO8064ExposesTheWholeAmplitudeFamily)
{
    hal::DSO8064 osc1{ hal::InstrumentId::Osc1 };
    osc1.setSimulatedVpp( 1, 3.3_V);
    osc1.setSimulatedVmax( 1, 1.9_V);
    osc1.setSimulatedVmin( 1, -1.4_V);
    osc1.setSimulatedVrms( 1, 1.2_V);
    osc1.setSimulatedVaverage( 1, 0.1_V);

    auto ch1 = osc1.channel<1>();

    EXPECT_DOUBLE_EQ( ch1.vpp().rawMeasure().value(),      3.3);
    EXPECT_DOUBLE_EQ( ch1.vmax().rawMeasure().value(),     1.9);
    EXPECT_DOUBLE_EQ( ch1.vmin().rawMeasure().value(),    -1.4);
    EXPECT_DOUBLE_EQ( ch1.vrms().rawMeasure().value(),     1.2);
    EXPECT_DOUBLE_EQ( ch1.vaverage().rawMeasure().value(), 0.1);
}

TEST( HalInstrument, DSO8064ChannelsAreIndependentlyAddressedSimulatedData)
{
    hal::DSO8064 osc1{ hal::InstrumentId::Osc1 };
    osc1.setSimulatedVpp( 1, 3.3_V);
    osc1.setSimulatedVpp( 3, 5.0_V);

    EXPECT_DOUBLE_EQ( osc1.channel<1>().vpp().rawMeasure().value(), 3.3);
    EXPECT_DOUBLE_EQ( osc1.channel<3>().vpp().rawMeasure().value(), 5.0);
}

TEST( HalInstrument, DSO8064ExposesTheTimingFamily)
{
    hal::DSO8064 osc1{ hal::InstrumentId::Osc1 };
    osc1.setSimulatedFrequency( 3, 1.0_kHz);
    osc1.setSimulatedPeriod( 3, 1.0_ms);
    osc1.setSimulatedRiseTime( 3, Time{ 12e-9});
    osc1.setSimulatedFallTime( 3, Time{ 14e-9});

    auto ch3 = osc1.channel<3>();

    EXPECT_DOUBLE_EQ( ch3.frequency().rawMeasure().value(), 1000.0);
    EXPECT_DOUBLE_EQ( ch3.period().rawMeasure().value(),    0.001);
    EXPECT_DOUBLE_EQ( ch3.riseTime().rawMeasure().value(),  12e-9);
    EXPECT_DOUBLE_EQ( ch3.fallTime().rawMeasure().value(),  14e-9);
}

TEST( HalInstrument, DSO8064RiseTimeDefaultsToTenNinetyThresholds)
{
    hal::DSO8064 osc1{ hal::InstrumentId::Osc1 };

    auto port = osc1.channel<1>().riseTime();

    ASSERT_TRUE( port.setup().LowThreshold.has_value());
    ASSERT_TRUE( port.setup().HighThreshold.has_value());
    EXPECT_DOUBLE_EQ( *port.setup().LowThreshold,  0.1);
    EXPECT_DOUBLE_EQ( *port.setup().HighThreshold, 0.9);
}

TEST( HalInstrument, DSO8064RiseTimeThresholdsAreOverridable)
{
    hal::DSO8064 osc1{ hal::InstrumentId::Osc1 };

    auto port = osc1.channel<1>().riseTime().lowThreshold( 0.2).highThreshold( 0.8);

    EXPECT_DOUBLE_EQ( *port.setup().LowThreshold,  0.2);
    EXPECT_DOUBLE_EQ( *port.setup().HighThreshold, 0.8);
}

TEST( HalInstrument, DSO8064DefaultsToVppMode)
{
    hal::DSO8064 osc1{ hal::InstrumentId::Osc1 };

    EXPECT_EQ( osc1.mode(), hal::DSO8064::Mode::Vpp);
}

TEST( HalInstrument, DSO8064ModeIsSharedAcrossPortHandlesHeldPastAModeSwitch)
{
    // Documents the same known, accepted sharp edge as hal::L4411A's own
    // test: a port handle obtained before a mode switch still reads
    // whichever mode is current when rawMeasure() is eventually called, not
    // the mode active when the handle was created.
    hal::DSO8064 osc1{ hal::InstrumentId::Osc1 };
    osc1.setSimulatedVpp( 1, 3.3_V);
    osc1.setSimulatedVrms( 1, 1.2_V);

    auto vppPort = osc1.channel<1>().vpp();
    (void)osc1.channel<1>().vrms();

    EXPECT_DOUBLE_EQ( vppPort.rawMeasure().value(), 1.2);
}

TEST( HalInstrument, DSO8064ChannelIsSharedAcrossPortHandlesHeldPastAChannelSwitch)
{
    // Same sharp edge, other axis: channel is instrument-level state too
    // (see hal/dso8064.hpp's own comment on DSO8064Channel for why), so a
    // port handle obtained on channel 1 still reads whichever channel is
    // current at rawMeasure() time if channel 3 gets selected afterward.
    hal::DSO8064 osc1{ hal::InstrumentId::Osc1 };
    osc1.setSimulatedVpp( 1, 3.3_V);
    osc1.setSimulatedVpp( 3, 5.0_V);

    auto ch1Port = osc1.channel<1>().vpp();
    (void)osc1.channel<3>().vpp();

    EXPECT_DOUBLE_EQ( ch1Port.rawMeasure().value(), 5.0);
}

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

TEST( HalInstrument, L4411AFourWireResistanceRequiresTheSensePathButTwoWireDoesNot)
{
    hal::L4411A dmm1{ hal::InstrumentId::Dmm1 };

    EXPECT_FALSE( dmm1.resistance().setup().RequiresSensePath);
    EXPECT_TRUE(  dmm1.fourWireResistance().setup().RequiresSensePath);
}

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
