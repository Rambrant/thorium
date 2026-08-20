//
// hal::DSO8064's own tests, moved here verbatim from libs/hal/tests/
// test_instrument.cpp when this driver moved out of libs/hal into its own
// directory -- the same lift hal::L4411A's tests got, and for the same reason.
// Test names are unchanged (still the HalInstrument suite) so the move is
// provably behaviour-preserving: the same named tests, in a different binary.
//
// hal::InstrumentId and to_string() over it stay behind in the original file:
// they are hal's own generic mechanism and exist without reference to any
// particular instrument.
//
#include "hal/dso8064.hpp"

#include <gtest/gtest.h>

#include <concepts>

//
// This model's back panel, as the constructor constraint actually sees it --
// checked in both directions, since a check that only ever passes proves
// nothing about what it rejects (the same shape hal/tests/test_safing.cpp
// uses for hal::SafeableInstrument, and hal/tests/test_address.cpp for the
// hal::ReachableOver mechanism itself).
//
// All three remote interfaces are on this scope's back panel; a PC serial
// port is not one of them.
//
namespace
{
    static_assert(   std::constructible_from< hal::DSO8064, hal::InstrumentId, hal::Gpib> );
    static_assert(   std::constructible_from< hal::DSO8064, hal::InstrumentId, hal::Lan> );
    static_assert(   std::constructible_from< hal::DSO8064, hal::InstrumentId, hal::Usb> );
    static_assert(   std::constructible_from< hal::DSO8064, hal::InstrumentId, hal::Simulated> );
    static_assert( ! std::constructible_from< hal::DSO8064, hal::InstrumentId, hal::Serial> );
    static_assert( ! std::constructible_from< hal::DSO8064, hal::InstrumentId> );
} // namespace

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
    hal::DSO8064 osc1{ hal::InstrumentId::Osc1, hal::Simulated{} };
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
    hal::DSO8064 osc1{ hal::InstrumentId::Osc1, hal::Simulated{} };
    osc1.setSimulatedVpp( 1, 3.3_V);

    auto port = osc1.channel<1>().vpp();  // the channel view temporary is gone after this line

    EXPECT_DOUBLE_EQ( port.rawMeasure().value(), 3.3);  // still valid here
}

TEST( HalInstrument, DSO8064ExposesTheWholeAmplitudeFamily)
{
    hal::DSO8064 osc1{ hal::InstrumentId::Osc1, hal::Simulated{} };
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
    hal::DSO8064 osc1{ hal::InstrumentId::Osc1, hal::Simulated{} };
    osc1.setSimulatedVpp( 1, 3.3_V);
    osc1.setSimulatedVpp( 3, 5.0_V);

    EXPECT_DOUBLE_EQ( osc1.channel<1>().vpp().rawMeasure().value(), 3.3);
    EXPECT_DOUBLE_EQ( osc1.channel<3>().vpp().rawMeasure().value(), 5.0);
}

TEST( HalInstrument, DSO8064ExposesTheTimingFamily)
{
    hal::DSO8064 osc1{ hal::InstrumentId::Osc1, hal::Simulated{} };
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
    hal::DSO8064 osc1{ hal::InstrumentId::Osc1, hal::Simulated{} };

    auto port = osc1.channel<1>().riseTime();

    ASSERT_TRUE( port.setup().LowThreshold.has_value());
    ASSERT_TRUE( port.setup().HighThreshold.has_value());
    EXPECT_DOUBLE_EQ( *port.setup().LowThreshold,  0.1);
    EXPECT_DOUBLE_EQ( *port.setup().HighThreshold, 0.9);
}

TEST( HalInstrument, DSO8064RiseTimeThresholdsAreOverridable)
{
    hal::DSO8064 osc1{ hal::InstrumentId::Osc1, hal::Simulated{} };

    auto port = osc1.channel<1>().riseTime().lowThreshold( 0.2).highThreshold( 0.8);

    EXPECT_DOUBLE_EQ( *port.setup().LowThreshold,  0.2);
    EXPECT_DOUBLE_EQ( *port.setup().HighThreshold, 0.8);
}

TEST( HalInstrument, DSO8064DefaultsToVppMode)
{
    hal::DSO8064 osc1{ hal::InstrumentId::Osc1, hal::Simulated{} };

    EXPECT_EQ( osc1.mode(), hal::DSO8064::Mode::Vpp);
}

TEST( HalInstrument, DSO8064ModeIsSharedAcrossPortHandlesHeldPastAModeSwitch)
{
    // Documents the same known, accepted sharp edge as hal::L4411A's own
    // test: a port handle obtained before a mode switch still reads
    // whichever mode is current when rawMeasure() is eventually called, not
    // the mode active when the handle was created.
    hal::DSO8064 osc1{ hal::InstrumentId::Osc1, hal::Simulated{} };
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
    hal::DSO8064 osc1{ hal::InstrumentId::Osc1, hal::Simulated{} };
    osc1.setSimulatedVpp( 1, 3.3_V);
    osc1.setSimulatedVpp( 3, 5.0_V);

    auto ch1Port = osc1.channel<1>().vpp();
    (void)osc1.channel<3>().vpp();

    EXPECT_DOUBLE_EQ( ch1Port.rawMeasure().value(), 5.0);
}
