//
// hal/tests/test_instrument.cpp and hal/tests/test_source_instruments.cpp
// each exercise Measure and Apply/Connect/Disconnect/Remove in isolation --
// one instrument family at a time, and never both in the same test. A real
// script's actual shape is closer to: source a rail, connect it, measure it
// with a second instrument, then tear down -- so this file stitches those
// together, using the real hal engines (MeasureEngine/ApplyEngine/
// ConnectEngine/DisconnectEngine/RemoveEngine from hal/measure.hpp and
// hal/apply.hpp) end to end, exactly the call syntax a script would write,
// just against a local fixture rather than the rig's real global fabric/
// wiring/instruments (see hal/active_instruments.hpp) so tests don't share
// state with each other.
//
// This also demonstrates the two shapes Connect/Disconnect can take side by
// side: DcP1 is fixed-wired (hal::N6701A::dc() takes no point at all --
// see that header's own comment), Osc1 is routed (hal::DSO8064's Port
// takes at(...) on every Measure()) -- both physically reach the same VPC
// pin (Output5V), but only Osc1's path touches the mux; DcP1's connect()
// closes just its own fixed channel.
//
#include "hal/n6701a.hpp"
#include "hal/dso8064.hpp"
#include "hal/apply.hpp"
#include "hal/measure.hpp"

#include <gtest/gtest.h>

#include "core/at.hpp"

using namespace core::literals;
using namespace core::quantities;

using core::at;

namespace
{
    constexpr hal::VpcLocation kOutput5V{ hal::VpcRack::A, 1, 3 };
    constexpr hal::VpcLocation kClockOut{ hal::VpcRack::A, 1, 4 };

    // Output5V/ClockOut are only ever named on the Measure() side below --
    // DcP1 reaches the same physical Output5V pin too, but as a fixed wire,
    // not as an at(...) argument (see hal::N6701A's own comment).
    constexpr core::AdapterPointTag<kOutput5V, core::QuantityKind::Voltage> Output5V{ "Output5V", "5Vdc supply port" };
    constexpr core::AdapterPointTag<kClockOut, core::QuantityKind::Time>    ClockOut{ "ClockOut", "clock edge test point" };

    struct CallSyntaxFixture : ::testing::Test
    {
        hal::SwitchFabric      fabric;
        hal::InstrumentWiring  instrumentWiring;
        hal::ConnectorWiring   connectorWiring;

        hal::N6701A  dcP1{ hal::InstrumentId::DcP1, 1 };
        hal::DSO8064 osc1{ hal::InstrumentId::Osc1 };

        ApplyEngine      apply{};
        RemoveEngine     remove{};
        ConnectEngine    connect{    fabric, instrumentWiring, connectorWiring };
        DisconnectEngine disconnect{ fabric, instrumentWiring, connectorWiring };
        MeasureEngine    Measure{    fabric, instrumentWiring, connectorWiring };

        CallSyntaxFixture()
        {
            // Same shape as hal/wiring.inc's real entries -- DcP1's one
            // fixed channel on Matrix2/20, Osc1 routed through Matrix2/10
            // -- just declared locally so this fixture doesn't depend on
            // (or pollute) the rig's real global wiring tables.
            instrumentWiring.addWire( hal::InstrumentId::DcP1, { hal::SwitchDeviceKind::Matrix, "Matrix2", 20 });
            instrumentWiring.addWire( hal::InstrumentId::Osc1, { hal::SwitchDeviceKind::Matrix, "Matrix2", 10 });

            connectorWiring.addWire( kOutput5V, { hal::SwitchDeviceKind::Mux, "Mux1", 3 });
            connectorWiring.addWire( kClockOut, { hal::SwitchDeviceKind::Mux, "Mux1", 4 });
        }
    };
} // namespace

TEST_F( CallSyntaxFixture, SourceThenMeasureTheSamePhysicalRailTwoDifferentWays)
{
    // DcP1 drives Output5V's pin via its own fixed wire, no at(...)
    // involved; Osc1 reads the same physical pin back through the mux,
    // via at(Output5V) -- the two calls don't look symmetric any more,
    // and that asymmetry is the point: one of these instruments has a
    // routing decision to make, the other doesn't.
    apply(   dcP1.dc().voltage( 5.0_V).currentLimit( 1.0_A));
    connect( dcP1.dc());

    osc1.setSimulatedVpp( 3, 5.0_V);

    const auto reading = Measure( osc1.channel<3>().vpp(), at( Output5V));

    EXPECT_DOUBLE_EQ( reading.value(), 5.0);
    EXPECT_TRUE( dcP1.isEnabled());
    EXPECT_TRUE( fabric.isClosed( { hal::SwitchDeviceKind::Matrix, "Matrix2", 20 }));
}

TEST_F( CallSyntaxFixture, MeasureConnectsThenDisconnectsAroundEachReading)
{
    osc1.setSimulatedVpp( 3, 3.3_V);

    // Osc1 isn't connected before this call...
    EXPECT_FALSE( fabric.isClosed( { hal::SwitchDeviceKind::Matrix, "Matrix2", 10 }));

    const auto reading = Measure( osc1.channel<3>().vpp(), at( Output5V));

    // ...and Measure() has already disconnected it again by the time it
    // returns -- the live-by-default connect/measure/disconnect shape
    // core/tests/test_measure.cpp documents at the core::MeasureEngine
    // level, exercised here through the real hal engine instead of a mock.
    EXPECT_DOUBLE_EQ( reading.value(), 3.3);
    EXPECT_FALSE( fabric.isClosed( { hal::SwitchDeviceKind::Matrix, "Matrix2", 10 }));
    EXPECT_FALSE( fabric.isClosed( { hal::SwitchDeviceKind::Mux,    "Mux1",    3 }));
}

TEST_F( CallSyntaxFixture, MeasureAcceptsTheChainedThresholdBuilderDirectlyAtTheCallSite)
{
    // The full DSO8064 syntax in one line: channel-first, then the timing
    // family's chained threshold builder, then straight into Measure/at() --
    // exactly what a script would write, no intermediate named variable.
    osc1.setSimulatedRiseTime( 3, Time{ 9e-9});

    const auto reading = Measure( osc1.channel<3>().riseTime().lowThreshold( 0.2).highThreshold( 0.8), at( ClockOut));

    EXPECT_DOUBLE_EQ( reading.value(), 9e-9);
}

TEST_F( CallSyntaxFixture, DisconnectAndRemoveTearDownIndependentlyOfMeasure)
{
    apply(   dcP1.dc().voltage( 5.0_V));
    connect( dcP1.dc());

    osc1.setSimulatedVpp( 3, 5.0_V);
    (void)Measure( osc1.channel<3>().vpp(), at( Output5V));

    // Measure's own connect/disconnect around the reading is already done
    // by this point -- disconnect()/remove() here are only tearing down
    // dcP1's still-open source path, which was never touched by Measure()
    // in the first place (see the fixture comment on why dcP1 and osc1
    // don't share any fabric channel any more).
    disconnect( dcP1.dc());
    remove(     dcP1.dc());

    EXPECT_FALSE( dcP1.isEnabled());
    EXPECT_FALSE( fabric.isClosed( { hal::SwitchDeviceKind::Matrix, "Matrix2", 20 }));
}

TEST_F( CallSyntaxFixture, DcSourceAndScopeUseCompletelyDisjointFabricChannelsNow)
{
    // Before DcP1 became fixed-wired, DcP1 and Osc1 shared Mux1/3 (the
    // same physical rail), so connecting/disconnecting one had to leave
    // the other's use of that shared mux channel alone -- see
    // hal::SwitchFabric's use-count-based connect()/disconnect(). Now that
    // DcP1's own connect() never touches the mux at all, there's no
    // overlap left to protect: DcP1's path is Matrix2/20 alone; Osc1's
    // Measure() path is Matrix2/10 plus Mux1/3. Disjoint sets, not shared
    // ones, even though both ultimately land on the same physical pin.
    connect( dcP1.dc());

    osc1.setSimulatedVpp( 3, 5.0_V);
    (void)Measure( osc1.channel<3>().vpp(), at( Output5V));

    // dcP1's own connect is still up, and Measure()'s internal connect/
    // disconnect around the reading never touched Matrix2/20 at all.
    EXPECT_TRUE(  fabric.isClosed( { hal::SwitchDeviceKind::Matrix, "Matrix2", 20 }));
    EXPECT_FALSE( fabric.isClosed( { hal::SwitchDeviceKind::Matrix, "Matrix2", 10 }));
    EXPECT_FALSE( fabric.isClosed( { hal::SwitchDeviceKind::Mux,    "Mux1",    3 }));

    disconnect( dcP1.dc());

    EXPECT_FALSE( fabric.isClosed( { hal::SwitchDeviceKind::Matrix, "Matrix2", 20 }));
}
