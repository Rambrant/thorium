#include "hal/racal1260.hpp"

#include <gtest/gtest.h>

#include <concepts>

//
// This model's back panel, as the constructor constraint actually sees it --
// checked in both directions, since a check that only ever passes proves
// nothing about what it rejects (the same shape rig/tests/test_safing.cpp
// uses for hal::SafeableInstrument, and hal/tests/driver/test_address.cpp for the
// hal::ReachableOver mechanism itself).
//
// Two kinds of hardware provide a switched RS232 port -- a PC port cabled
// into the fabric, or a serial module in the switching chassis -- and
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
    static_assert(   std::constructible_from< hal::racal1260::Racal1260, hal::InstrumentId, hal::Serial> );
    static_assert(   std::constructible_from< hal::racal1260::Racal1260, hal::InstrumentId, hal::Gpib> );
    static_assert(   std::constructible_from< hal::racal1260::Racal1260, hal::InstrumentId, hal::Simulated> );
    static_assert( ! std::constructible_from< hal::racal1260::Racal1260, hal::InstrumentId, hal::Lan> );
    static_assert( ! std::constructible_from< hal::racal1260::Racal1260, hal::InstrumentId, hal::Usb> );
    static_assert( ! std::constructible_from< hal::racal1260::Racal1260, hal::InstrumentId> );
} // namespace

#include <chrono>

#include "core/meta.hpp"
#include "core/verbs/at.hpp"
#include "hal/topology/adapter.hpp"

//
// hal::racal1260::Racal1260's own tests, against a local adapter table and local wiring
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
    // the same reason framework/hal/tests/topology/test_adapter_macros.cpp declares its
    // bundle bare.
    //
    BUNDLE( Console, "RS232 debug console")
        LINE( Tx,   A, 2, 1, "console transmit, DUT to bench")
        LINE( Rx,   A, 2, 2, "console receive, bench to DUT")
        LINE( Gnd,  A, 2, 5, "console signal ground")
    END_BUNDLE

    //
    // Whatever the linking deployment's first instrument is called. A driver's
    // tests have no business knowing that this repo's bench rig calls this
    // port Ser1 -- see the top-level CMakeLists.txt on the packageability
    // defect that hard-coded ids in a driver's tests cause.
    //
    [[nodiscard]]
    auto anyId() -> hal::InstrumentId
    {
        return core::meta::values<hal::InstrumentId>[ 0];
    }

    //
    // And whatever its switching hardware is called. The wiring below used to
    // name this bench's Spst1 for the port's channels and its Spdt1 for the
    // connector's, and the two cards were never the point: what the routed
    // Connect has to compose is one path out of both tables. So both halves sit
    // on the first declared device here, in two channel ranges that cannot
    // collide, and the assertions read the same either way.
    //
    // Unlike the instrument list this one is allowed to be empty -- a
    // deployment may declare no switching hardware at all (dev/rig/devices.inc
    // is exactly that) -- so the three tests that route skip there rather than
    // assert. The rest of this file configures, writes to and safes a port,
    // which is the same fact on a desk as on a rack.
    //
    constexpr auto switchDeviceIds = core::meta::values<hal::SwitchDeviceId>;

    struct Racal1260Fixture : ::testing::Test
    {
        hal::SwitchFabric      fabric;
        hal::InstrumentWiring  instrumentWiring;
        hal::ConnectorWiring   connectorWiring;

        hal::racal1260::Racal1260         ser1{ anyId(), hal::Simulated{} };

        Racal1260Fixture()
        {
            if constexpr( ! switchDeviceIds.empty())
            {
                // The port's own three channels -- transmit, receive, return.
                instrumentWiring.addWire( anyId(), { switchDeviceIds[ 0], 16 });
                instrumentWiring.addWire( anyId(), { switchDeviceIds[ 0], 17 });
                instrumentWiring.addWire( anyId(), { switchDeviceIds[ 0], 18 });

                // The interface's three pins.
                connectorWiring.addWire( hal::VpcLocation{ hal::VpcRack::A, 2, 1 }, { switchDeviceIds[ 0], 0 });
                connectorWiring.addWire( hal::VpcLocation{ hal::VpcRack::A, 2, 2 }, { switchDeviceIds[ 0], 1 });
                connectorWiring.addWire( hal::VpcLocation{ hal::VpcRack::A, 2, 5 }, { switchDeviceIds[ 0], 2 });
            }
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
    setupDriver( ser1.rs232().baudRate( 9600).parity( hal::racal1260::Parity::Even).config());

    ASSERT_TRUE( ser1.baudRate().has_value());
    EXPECT_EQ( *ser1.baudRate(), 9600u);
    EXPECT_EQ( ser1.parity(), hal::racal1260::Parity::Even);

    // A second Setup naming only the baud rate must not reset the parity.
    setupDriver( ser1.rs232().baudRate( 19200).config());

    EXPECT_EQ( *ser1.baudRate(), 19200u);
    EXPECT_EQ( ser1.parity(), hal::racal1260::Parity::Even);
}

TEST_F( Racal1260Fixture, ATextLiteralReachesTheInstrumentAsItsOctets)
{
    writeDriver( ser1.rs232().config(), core::Bytes( "RD 30\r"));

    EXPECT_EQ( ser1.lastWrite(), core::Bytes( "RD 30\r"));
}

TEST_F( Racal1260Fixture, RepliesComeBackInTheOrderTheDutSentThem)
{
    ser1.queueReply( core::Bytes( "ACK\r"));
    ser1.queueReply( core::Bytes( "0xF5\r"));

    const auto config = ser1.rs232().config();

    EXPECT_EQ( readDriver( config), core::Bytes( "ACK\r"));
    EXPECT_EQ( readDriver( config), core::Bytes( "0xF5\r"));
}

//
// A DUT that said nothing is a finding, not a driver error. The criterion is
// the right place to fail it -- a driver that threw here would make "the port
// is misconfigured" and "the DUT is dead" the same exception.
//
TEST_F( Racal1260Fixture, APortWithNothingToSayReturnsAnEmptyPayload)
{
    EXPECT_TRUE( readDriver( ser1.rs232().timeout( std::chrono::milliseconds{ 50 }).config()).empty());
}

//
// The routed connect this driver exists to demonstrate: both halves of the
// path, composed. Three instrument channels plus three connector channels, all
// closed as one -- an interface is connected, never a wire.
//
TEST_F( Racal1260Fixture, ConnectingTheInterfaceClosesEveryLineOnBothSides)
{
    if( switchDeviceIds.empty())
    {
        GTEST_SKIP() << "this deployment declares no switching hardware -- nothing to route over";
    }

    connectDriver( fabric, instrumentWiring, connectorWiring,
                        ser1.rs232().config(), core::at( Console).point);

    EXPECT_TRUE( fabric.isClosed( { switchDeviceIds[ 0], 16 }));
    EXPECT_TRUE( fabric.isClosed( { switchDeviceIds[ 0], 17 }));
    EXPECT_TRUE( fabric.isClosed( { switchDeviceIds[ 0], 18 }));

    EXPECT_TRUE( fabric.isClosed( { switchDeviceIds[ 0], 0 }));
    EXPECT_TRUE( fabric.isClosed( { switchDeviceIds[ 0], 1 }));
    EXPECT_TRUE( fabric.isClosed( { switchDeviceIds[ 0], 2 }));
}

//
// Disconnect must open exactly what Connect closed -- which is why both compose
// their path through the one routeTo() rather than each building its own. A
// relay left latched after a Disconnect is a rig in a state no log
// describes.
//
TEST_F( Racal1260Fixture, DisconnectingOpensExactlyWhatWasClosed)
{
    if( switchDeviceIds.empty())
    {
        GTEST_SKIP() << "this deployment declares no switching hardware -- nothing to route over";
    }

    const auto config = ser1.rs232().config();

    connectDriver(    fabric, instrumentWiring, connectorWiring, config, core::at( Console).point);
    disconnectDriver( fabric, instrumentWiring, connectorWiring, config, core::at( Console).point);

    EXPECT_FALSE( fabric.isClosed( { switchDeviceIds[ 0], 16 }));
    EXPECT_FALSE( fabric.isClosed( { switchDeviceIds[ 0], 18 }));
    EXPECT_FALSE( fabric.isClosed( { switchDeviceIds[ 0], 0 }));
    EXPECT_FALSE( fabric.isClosed( { switchDeviceIds[ 0], 2 }));
}

//
// Safing is what runs when nobody knows what state the rig reached, so it must
// not depend on being told anything. A queued write is discarded rather than
// left to finish arriving at a DUT nobody is supervising.
//
TEST_F( Racal1260Fixture, SafingDropsTheConnectionAndDiscardsAnythingQueued)
{
    if( switchDeviceIds.empty())
    {
        GTEST_SKIP() << "this deployment declares no switching hardware -- nothing to route over";
    }

    connectDriver( fabric, instrumentWiring, connectorWiring,
                        ser1.rs232().config(), core::at( Console).point);
    writeDriver( ser1.rs232().config(), core::Bytes( "RD 30\r"));

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
    const auto described = describeConfig(
        ser1.rs232().baudRate( 9600).wordLength( 8).parity( hal::racal1260::Parity::None).stopBits( hal::racal1260::StopBits::One).config());

    EXPECT_EQ( described.Instrument, core::meta::to_string( anyId()));
    EXPECT_EQ( described.Settings,   "baud=9600, wordLength=8, parity=none, stopBits=1");

    const auto sparse = describeConfig( ser1.rs232().baudRate( 9600).config());

    EXPECT_EQ( sparse.Settings, "baud=9600");
}

//
// 1.5 stop bits is a real RS232 framing, and the reason StopBits is an enum
// rather than a count -- there is nowhere for it to go in an `int`.
//
TEST_F( Racal1260Fixture, OneAndAHalfStopBitsIsExpressible)
{
    const auto described = describeConfig( ser1.rs232().stopBits( hal::racal1260::StopBits::OnePointFive).config());

    EXPECT_EQ( described.Settings, "stopBits=1.5");
}

//
// A serial port has no output to energise, so there is nothing an
// Apply( Ser1.rs232()) could mean -- and the absence of applyDriver is what
// makes writing one a compile error rather than a call that silently does
// nothing. The same guarantee hal::keysight_edu36311a::SwitchableIsolation gives Connect on a
// relay-less supply.
//
TEST_F( Racal1260Fixture, ThereIsNoApplyForAPortWithNoOutput)
{
    constexpr auto appliable = []< typename ConfigT>( const ConfigT & config)
    {
        return requires { applyDriver( config); };
    };

    EXPECT_FALSE( appliable( ser1.rs232().config()));
    EXPECT_TRUE(  ( requires { setupDriver( ser1.rs232().config()); }));
}
