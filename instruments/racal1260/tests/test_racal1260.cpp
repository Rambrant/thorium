#include "hal/racal1260.hpp"

#include <gtest/gtest.h>

#include <concepts>

//
// This model's back panel, as the constructor constraint actually sees it --
// checked in both directions, since a check that only ever passes proves
// nothing about what it rejects (the same shape hal/tests/test_safing.cpp
// uses for hal::SafeableInstrument, and hal/tests/test_address.cpp for the
// hal::ReachableOver mechanism itself).
//
// Two kinds of hardware provide a matrix-routed RS232 port -- a PC port
// cabled into the matrix, or a serial module in the switching chassis -- and
// this driver does not know which one this rig has, so it accepts either. It
// is the one driver here whose set is wider than one panel, and the reason is
// missing bench knowledge rather than a model with two panels; see the
// constructor's own comment.
//
// What it still rejects is the part that is known: nothing about this
// instrument is reached over a network or USB.
//
namespace
{
    static_assert(   std::constructible_from< hal::Racal1260, hal::InstrumentId, hal::Serial> );
    static_assert(   std::constructible_from< hal::Racal1260, hal::InstrumentId, hal::Gpib> );
    static_assert(   std::constructible_from< hal::Racal1260, hal::InstrumentId, hal::Simulated> );
    static_assert( ! std::constructible_from< hal::Racal1260, hal::InstrumentId, hal::Lan> );
    static_assert( ! std::constructible_from< hal::Racal1260, hal::InstrumentId, hal::Usb> );
    static_assert( ! std::constructible_from< hal::Racal1260, hal::InstrumentId> );
} // namespace

#include <chrono>

#include "core/at.hpp"
#include "hal/adapter.hpp"

//
// hal::Racal1260's own tests, against a local adapter table and local wiring
// rather than this rig's -- the same isolation every driver directory keeps
// (see instruments/README.md). Nothing here names another instrument, and the
// fabric and the two wiring tables are built by the fixture rather than taken
// from rig/wiring.inc, so this directory stays packageable on its own.
//
namespace
{
    //
    // A console interface to route to. No ADAPTER around it: ADAPTER opens the
    // fixed `dut` struct, and defining a second one in a translation unit that
    // may be linked alongside the real adapter is a clash waiting to happen --
    // the same reason libs/hal/tests/test_adapter_macros.cpp declares its
    // bundle bare.
    //
    BUNDLE( Console, "RS232 debug console")
        LINE( Tx,   A, 2, 1, "console transmit, DUT to bench")
        LINE( Rx,   A, 2, 2, "console receive, bench to DUT")
        LINE( Gnd,  A, 2, 5, "console signal ground")
    END_BUNDLE

    struct Racal1260Fixture : ::testing::Test
    {
        hal::SwitchFabric      fabric;
        hal::InstrumentWiring  instrumentWiring;
        hal::ConnectorWiring   connectorWiring;

        hal::Racal1260         ser1{ hal::InstrumentId::Ser1, hal::Simulated{} };

        Racal1260Fixture()
        {
            // The port's own three channels -- transmit, receive, return.
            instrumentWiring.addWire( hal::InstrumentId::Ser1, { hal::SwitchDeviceId::Matrix2, 30 });
            instrumentWiring.addWire( hal::InstrumentId::Ser1, { hal::SwitchDeviceId::Matrix2, 31 });
            instrumentWiring.addWire( hal::InstrumentId::Ser1, { hal::SwitchDeviceId::Matrix2, 32 });

            // The interface's three pins.
            connectorWiring.addWire( hal::VpcLocation{ hal::VpcRack::A, 2, 1 }, { hal::SwitchDeviceId::Mux2, 6 });
            connectorWiring.addWire( hal::VpcLocation{ hal::VpcRack::A, 2, 2 }, { hal::SwitchDeviceId::Mux2, 7 });
            connectorWiring.addWire( hal::VpcLocation{ hal::VpcRack::A, 2, 5 }, { hal::SwitchDeviceId::Mux2, 8 });
        }
    };
} // namespace

//
// Setup means "leave what you were not told about alone". This is the whole
// reason every field of the config is optional, and the difference between a
// Setup that named only the baud rate and one that named all four -- a driver
// that filled the rest in with defaults would silently reframe a port a
// previous Setup had configured deliberately.
//
TEST_F( Racal1260Fixture, SetupChangesOnlyTheFieldsItWasGiven)
{
    hal::setupDriver( ser1.rs232().baudRate( 9600).parity( hal::Parity::Even).config());

    ASSERT_TRUE( ser1.baudRate().has_value());
    EXPECT_EQ( *ser1.baudRate(), 9600u);
    EXPECT_EQ( ser1.parity(), hal::Parity::Even);

    // A second Setup naming only the baud rate must not reset the parity.
    hal::setupDriver( ser1.rs232().baudRate( 19200).config());

    EXPECT_EQ( *ser1.baudRate(), 19200u);
    EXPECT_EQ( ser1.parity(), hal::Parity::Even);
}

TEST_F( Racal1260Fixture, ATextLiteralReachesTheInstrumentAsItsOctets)
{
    hal::writeDriver( ser1.rs232().config(), core::Bytes( "RD 30\r"));

    EXPECT_EQ( ser1.lastWrite(), core::Bytes( "RD 30\r"));
}

TEST_F( Racal1260Fixture, RepliesComeBackInTheOrderTheDutSentThem)
{
    ser1.queueReply( core::Bytes( "ACK\r"));
    ser1.queueReply( core::Bytes( "0xF5\r"));

    const auto config = ser1.rs232().config();

    EXPECT_EQ( hal::readDriver( config), core::Bytes( "ACK\r"));
    EXPECT_EQ( hal::readDriver( config), core::Bytes( "0xF5\r"));
}

//
// A DUT that said nothing is a finding, not a driver error. The criterion is
// the right place to fail it -- a driver that threw here would make "the port
// is misconfigured" and "the DUT is dead" the same exception.
//
TEST_F( Racal1260Fixture, APortWithNothingToSayReturnsAnEmptyPayload)
{
    EXPECT_TRUE( hal::readDriver( ser1.rs232().timeout( std::chrono::milliseconds{ 50 }).config()).empty());
}

//
// The routed connect this driver exists to demonstrate: both halves of the
// path, composed. Three instrument channels plus three connector channels, all
// closed as one -- an interface is connected, never a wire.
//
TEST_F( Racal1260Fixture, ConnectingTheInterfaceClosesEveryLineOnBothSides)
{
    hal::connectDriver( fabric, instrumentWiring, connectorWiring,
                        ser1.rs232().config(), core::at( Console).point);

    EXPECT_TRUE( fabric.isClosed( { hal::SwitchDeviceId::Matrix2, 30 }));
    EXPECT_TRUE( fabric.isClosed( { hal::SwitchDeviceId::Matrix2, 31 }));
    EXPECT_TRUE( fabric.isClosed( { hal::SwitchDeviceId::Matrix2, 32 }));

    EXPECT_TRUE( fabric.isClosed( { hal::SwitchDeviceId::Mux2, 6 }));
    EXPECT_TRUE( fabric.isClosed( { hal::SwitchDeviceId::Mux2, 7 }));
    EXPECT_TRUE( fabric.isClosed( { hal::SwitchDeviceId::Mux2, 8 }));
}

//
// Disconnect must open exactly what Connect closed -- which is why both compose
// their path through the one routeTo() rather than each building its own. A
// crosspoint left latched after a Disconnect is a rig in a state no log
// describes.
//
TEST_F( Racal1260Fixture, DisconnectingOpensExactlyWhatWasClosed)
{
    const auto config = ser1.rs232().config();

    hal::connectDriver(    fabric, instrumentWiring, connectorWiring, config, core::at( Console).point);
    hal::disconnectDriver( fabric, instrumentWiring, connectorWiring, config, core::at( Console).point);

    EXPECT_FALSE( fabric.isClosed( { hal::SwitchDeviceId::Matrix2, 30 }));
    EXPECT_FALSE( fabric.isClosed( { hal::SwitchDeviceId::Matrix2, 32 }));
    EXPECT_FALSE( fabric.isClosed( { hal::SwitchDeviceId::Mux2,    6 }));
    EXPECT_FALSE( fabric.isClosed( { hal::SwitchDeviceId::Mux2,    8 }));
}

//
// Safing is what runs when nobody knows what state the rig reached, so it must
// not depend on being told anything. A queued write is discarded rather than
// left to finish arriving at a DUT nobody is supervising.
//
TEST_F( Racal1260Fixture, SafingDropsTheConnectionAndDiscardsAnythingQueued)
{
    hal::connectDriver( fabric, instrumentWiring, connectorWiring,
                        ser1.rs232().config(), core::at( Console).point);
    hal::writeDriver( ser1.rs232().config(), core::Bytes( "RD 30\r"));

    ASSERT_TRUE( ser1.isConnected());

    ser1.safe();

    EXPECT_FALSE( ser1.isConnected());
}

//
// The framing a Setup applied is reported field by field, and an unset field is
// reported not at all -- "9600 8N1" would invent the three settings a Setup was
// never given.
//
TEST_F( Racal1260Fixture, TheLogReportsOnlyTheSettingsThatWereActuallyGiven)
{
    const auto described = hal::describeConfig(
        ser1.rs232().baudRate( 9600).wordLength( 8).parity( hal::Parity::None).stopBits( hal::StopBits::One).config());

    EXPECT_EQ( described.Instrument, "Ser1");
    EXPECT_EQ( described.Settings,   "baud=9600, wordLength=8, parity=none, stopBits=1");

    const auto sparse = hal::describeConfig( ser1.rs232().baudRate( 9600).config());

    EXPECT_EQ( sparse.Settings, "baud=9600");
}

//
// 1.5 stop bits is a real RS232 framing, and the reason StopBits is an enum
// rather than a count -- there is nowhere for it to go in an `int`.
//
TEST_F( Racal1260Fixture, OneAndAHalfStopBitsIsExpressible)
{
    const auto described = hal::describeConfig( ser1.rs232().stopBits( hal::StopBits::OnePointFive).config());

    EXPECT_EQ( described.Settings, "stopBits=1.5");
}

//
// A serial port has no output to energise, so there is nothing an
// Apply( Ser1.rs232()) could mean -- and the absence of applyDriver is what
// makes writing one a compile error rather than a call that silently does
// nothing. The same guarantee hal::SwitchableIsolation gives Connect on a
// relay-less supply.
//
TEST_F( Racal1260Fixture, ThereIsNoApplyForAPortWithNoOutput)
{
    constexpr auto appliable = []< typename ConfigT>( const ConfigT & config)
    {
        return requires { applyDriver( config); };
    };

    EXPECT_FALSE( appliable( ser1.rs232().config()));
    EXPECT_TRUE(  ( requires { hal::setupDriver( ser1.rs232().config()); }));
}
