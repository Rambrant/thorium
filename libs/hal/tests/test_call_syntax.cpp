//
// hal/tests/test_instrument.cpp and hal/tests/test_source_instruments.cpp
// each exercise Measure and Apply/Connect/Disconnect/Remove in isolation --
// one instrument family at a time, and never both in the same test. A real
// script's actual shape is closer to: source a rail, connect it, measure it
// with a second instrument, then tear down -- so this file stitches those
// together, using the real hal engines (MeasureEngine/ApplyEngine/
// ConnectEngine/DisconnectEngine/RemoveEngine from hal/measure.hpp and
// hal/source.hpp and hal/route.hpp) end to end, exactly the call syntax a
// script would write,
// just against a local fixture rather than the rig's real global fabric/
// wiring/instruments (see rig/active_instruments.hpp) so tests don't share
// state with each other.
//
// This also demonstrates the two shapes Connect/Disconnect can take side by
// side: DcP3 is fixed-wired (hal::N6701A::dc() takes no point at all --
// see that header's own comment; DcP3 specifically has a real isolation
// relay -- hal::N6701ARelay -- unlike DcP1/DcP2, which have none at all),
// Osc1 is routed (hal::DSO8064's Port takes at(...) on every Measure()) --
// both physically reach the same VPC pin (Output5V), but only Osc1's path
// touches the mux; DcP3's connect() closes just its own fixed channel.
//
#include "hal/n6701a.hpp"
#include "hal/dso8064.hpp"
#include "hal/l4411a.hpp"
#include "hal/measure.hpp"
#include "hal/route.hpp"
#include "hal/source.hpp"

#include <gtest/gtest.h>

#include "core/at.hpp"

using namespace core::literals;
using namespace core::quantities;

using core::at;

namespace
{
    constexpr hal::VpcLocation kOutput5V{ hal::VpcRack::A, 1, 3 };
    constexpr hal::VpcLocation kClockOut{ hal::VpcRack::A, 1, 4 };
    constexpr hal::VpcLocation kResistancePoint{ hal::VpcRack::A, 1, 6 };

    // Output5V/ClockOut are only ever named on the Measure() side below --
    // DcP3 reaches the same physical Output5V pin too, but as a fixed wire,
    // not as an at(...) argument (see hal::N6701A's own comment).
    constexpr core::AdapterPointTag<kOutput5V>        Output5V{ "Output5V", "5Vdc supply port" };
    constexpr core::AdapterPointTag<kClockOut>        ClockOut{ "ClockOut", "clock edge test point" };
    constexpr core::AdapterPointTag<kResistancePoint> ResistancePoint{ "ResistancePoint", "4-wire Kelvin test point" };

    struct CallSyntaxFixture : ::testing::Test
    {
        hal::SwitchFabric      fabric;
        hal::InstrumentWiring  instrumentWiring;
        hal::ConnectorWiring   connectorWiring;

        hal::N6701ARelay  dcP3{ hal::InstrumentId::DcP3, hal::Simulated{}, 3 };
        hal::DSO8064 osc1{ hal::InstrumentId::Osc1, hal::Simulated{} };
        hal::L4411A  dmm1{ hal::InstrumentId::Dmm1, hal::Simulated{} };

        ApplyEngine      apply{};
        RemoveEngine     remove{};
        ConnectEngine    connect{    fabric, instrumentWiring, connectorWiring };
        DisconnectEngine disconnect{ fabric, instrumentWiring, connectorWiring };
        MeasureEngine    Measure{    fabric, instrumentWiring, connectorWiring };

        CallSyntaxFixture()
        {
            // Same shape as the rig's real wiring.inc entries -- DcP3's one
            // fixed channel on Matrix2/24, Osc1 routed through Matrix2/10
            // -- just declared locally so this fixture doesn't depend on
            // (or pollute) the rig's real global wiring tables.
            instrumentWiring.addWire( hal::InstrumentId::DcP3, { hal::SwitchDeviceId::Matrix2, 24 });
            instrumentWiring.addWire( hal::InstrumentId::Osc1, { hal::SwitchDeviceId::Matrix2, 10 });
            instrumentWiring.addWire( hal::InstrumentId::Dmm1, { hal::SwitchDeviceId::Matrix2, 14 });

            connectorWiring.addWire( kOutput5V, { hal::SwitchDeviceId::Mux1, 3 });
            connectorWiring.addWire( kClockOut, { hal::SwitchDeviceId::Mux1, 4 });
            connectorWiring.addWire( kResistancePoint, { hal::SwitchDeviceId::Mux1, 6 });

            // Dmm1's sense terminals and ResistancePoint's own sense mux
            // channel -- only ever touched by a 4-wire reading (see
            // core::MeasureEngine's own comment on the sense path);
            // a plain 2-wire resistance() call never looks these up.
            instrumentWiring.addWire( hal::InstrumentId::Dmm1, { hal::SwitchDeviceId::Matrix2, 15 }, hal::WireRole::Sense);
            connectorWiring.addWire( kResistancePoint, { hal::SwitchDeviceId::Mux1, 7 }, hal::WireRole::Sense);
        }
    };
} // namespace

TEST_F( CallSyntaxFixture, SourceThenMeasureTheSamePhysicalRailTwoDifferentWays)
{
    // DcP3 drives Output5V's pin via its own fixed wire, no at(...)
    // involved; Osc1 reads the same physical pin back through the mux,
    // via at(Output5V) -- the two calls don't look symmetric any more,
    // and that asymmetry is the point: one of these instruments has a
    // routing decision to make, the other doesn't.
    apply(   dcP3.dc().voltage( 5.0_V).currentLimit( 1.0_A));
    connect( dcP3.dc());

    osc1.setSimulatedVpp( 3, 5.0_V);

    const auto reading = Measure( osc1.channel<3>().vpp(), at( Output5V));

    EXPECT_DOUBLE_EQ( reading.value(), 5.0);
    EXPECT_TRUE( dcP3.isEnabled());
    EXPECT_TRUE( fabric.isClosed( { hal::SwitchDeviceId::Matrix2, 24 }));
}

TEST_F( CallSyntaxFixture, MeasureConnectsThenDisconnectsAroundEachReading)
{
    osc1.setSimulatedVpp( 3, 3.3_V);

    // Osc1 isn't connected before this call...
    EXPECT_FALSE( fabric.isClosed( { hal::SwitchDeviceId::Matrix2, 10 }));

    const auto reading = Measure( osc1.channel<3>().vpp(), at( Output5V));

    // ...and Measure() has already disconnected it again by the time it
    // returns -- the live-by-default connect/measure/disconnect shape
    // core/tests/test_measure.cpp documents at the core::MeasureEngine
    // level, exercised here through the real hal engine instead of a mock.
    EXPECT_DOUBLE_EQ( reading.value(), 3.3);
    EXPECT_FALSE( fabric.isClosed( { hal::SwitchDeviceId::Matrix2, 10 }));
    EXPECT_FALSE( fabric.isClosed( { hal::SwitchDeviceId::Mux1,    3 }));
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
    apply(   dcP3.dc().voltage( 5.0_V));
    connect( dcP3.dc());

    osc1.setSimulatedVpp( 3, 5.0_V);
    (void)Measure( osc1.channel<3>().vpp(), at( Output5V));

    // Measure's own connect/disconnect around the reading is already done
    // by this point -- disconnect()/remove() here are only tearing down
    // dcP3's still-open source path, which was never touched by Measure()
    // in the first place (see the fixture comment on why dcP3 and osc1
    // don't share any fabric channel any more).
    disconnect( dcP3.dc());
    remove(     dcP3.dc());

    EXPECT_FALSE( dcP3.isEnabled());
    EXPECT_FALSE( fabric.isClosed( { hal::SwitchDeviceId::Matrix2, 24 }));
}

TEST_F( CallSyntaxFixture, DcSourceAndScopeUseCompletelyDisjointFabricChannelsNow)
{
    // Before DcP3 became fixed-wired, DcP3 and Osc1 shared Mux1/3 (the
    // same physical rail), so connecting/disconnecting one had to leave
    // the other's use of that shared mux channel alone -- see
    // hal::SwitchFabric's use-count-based connect()/disconnect(). Now that
    // DcP3's own connect() never touches the mux at all, there's no
    // overlap left to protect: DcP3's path is Matrix2/24 alone; Osc1's
    // Measure() path is Matrix2/10 plus Mux1/3. Disjoint sets, not shared
    // ones, even though both ultimately land on the same physical pin.
    connect( dcP3.dc());

    osc1.setSimulatedVpp( 3, 5.0_V);
    (void)Measure( osc1.channel<3>().vpp(), at( Output5V));

    // dcP3's own connect is still up, and Measure()'s internal connect/
    // disconnect around the reading never touched Matrix2/24 at all.
    EXPECT_TRUE(  fabric.isClosed( { hal::SwitchDeviceId::Matrix2, 24 }));
    EXPECT_FALSE( fabric.isClosed( { hal::SwitchDeviceId::Matrix2, 10 }));
    EXPECT_FALSE( fabric.isClosed( { hal::SwitchDeviceId::Mux1,    3 }));

    disconnect( dcP3.dc());

    EXPECT_FALSE( fabric.isClosed( { hal::SwitchDeviceId::Matrix2, 24 }));
}

TEST_F( CallSyntaxFixture, TwoWireResistanceNeverTouchesTheSenseChannels)
{
    dmm1.setSimulatedResistance( 100.0_Ohm);

    const auto reading = Measure( dmm1.resistance(), at( ResistancePoint));

    EXPECT_DOUBLE_EQ( reading.value(), 100.0);
    // Sense channels were never closed at all -- not even briefly -- since
    // a plain 2-wire reading's Port is typed core::SensePath::NotUsed.
    EXPECT_FALSE( fabric.isClosed( { hal::SwitchDeviceId::Matrix2, 15 }));
    EXPECT_FALSE( fabric.isClosed( { hal::SwitchDeviceId::Mux1,    7 }));
}

TEST_F( CallSyntaxFixture, FourWireResistanceRoutesForceAndSenseTogetherThenReleasesBoth)
{
    dmm1.setSimulatedFourWireResistance( 99.5_Ohm);

    const auto reading = Measure( dmm1.fourWireResistance(), at( ResistancePoint));

    EXPECT_DOUBLE_EQ( reading.value(), 99.5);

    // All four channels -- force (instrument + connector) and sense
    // (instrument + connector) -- were closed together for the reading,
    // then released together, exactly like the force-only case but with
    // two more elements in the same Path.
    EXPECT_FALSE( fabric.isClosed( { hal::SwitchDeviceId::Matrix2, 14 })); // force, instrument side
    EXPECT_FALSE( fabric.isClosed( { hal::SwitchDeviceId::Mux1,    6 }));  // force, connector side
    EXPECT_FALSE( fabric.isClosed( { hal::SwitchDeviceId::Matrix2, 15 })); // sense, instrument side
    EXPECT_FALSE( fabric.isClosed( { hal::SwitchDeviceId::Mux1,    7 }));  // sense, connector side
}

//
// ---------------------------------------------------------------------------
// A cabled rail is still measurable at its pin
// ---------------------------------------------------------------------------
// The property core::PointKind was cut back to (see its own comment): a
// SOURCE_POINT records that a supply is cabled onto a pin, and restricts
// nothing at the call site.
//
// Worth asserting rather than leaving implicit, because the first version of
// this did the opposite -- it made Measure( ..., at( aRail)) fail to compile,
// which banned exactly the check suite/scripts/rig_power_on.cpp names as the
// right way to ask what arrives at the DUT's pin (as against the supply's own
// readback, which cannot see cable drop or a bad contact). A test that pins
// the behaviour down is what stops that being "tightened" back.
//
// Expressed as a concept with its own bound parameters rather than a bare
// `requires` against a concrete call -- see the IMPORTANT note at the top of
// core/tests/test_static_constraints.cpp for why only this form is reliably
// soft-failed.
//
namespace
{
    constexpr hal::VpcLocation kBackupSupply{ hal::VpcRack::A, 1, 5 };

    // Same pin, both kinds -- so what follows isolates the kind and nothing
    // else. The Signal one is what POINT builds, the Source one SOURCE_POINT.
    constexpr core::AdapterPointTag<kBackupSupply>                            RoutablePin{ "RoutablePin", "as a plain POINT" };
    constexpr core::AdapterPointTag<kBackupSupply, core::PointKind::Source>   BackupSupply{ "BackupSupply", "28Vdc backup supply" };

    template<typename MeasureT, typename PortT, typename PointT>
    concept CanMeasureAt = requires( MeasureT measure, PortT port, PointT point)
    {
        measure( port, core::at( point));
    };
} // namespace

TEST_F( CallSyntaxFixture, ASourcePointIsMeasurableLikeAnyOther)
{
    using PortT = decltype( dmm1.voltage());

    static_assert( CanMeasureAt<MeasureEngine, PortT, decltype( RoutablePin)>);

    static_assert( CanMeasureAt<MeasureEngine, PortT, decltype( BackupSupply)>,
                   "Measure( ..., at( sourcePoint)) must compile -- a rail is cabled so the fabric "
                   "never carries its load current, which says nothing against a high-impedance tap "
                   "onto the same pin (see core::PointKind)");

    SUCCEED();
}

TEST_F( CallSyntaxFixture, ReadingACabledRailAtItsPinGoesThroughTheFabricNotTheSupply)
{
    // The measurement the assertion above exists to keep possible, actually
    // taken: a DMM at the rail's pin, routed like any other reading, which is
    // a different question from DcP3.measuredVoltage() -- that one never
    // leaves the instrument (see core::MeasureEngine's point-free overload).
    connectorWiring.addWire( kBackupSupply, { hal::SwitchDeviceId::Mux1, 6 });

    const auto reading = Measure( dmm1.voltage(), at( BackupSupply));

    EXPECT_EQ( reading, 0.0_V);   // the L4411A stub reads zero; that it read at all is the point

    // Both sides of the composed route released again -- connected just long
    // enough to take the reading, see core::MeasureEngine.
    EXPECT_FALSE( fabric.isClosed( { hal::SwitchDeviceId::Matrix2, 14 }));  // Dmm1's own channel
    EXPECT_FALSE( fabric.isClosed( { hal::SwitchDeviceId::Mux1,    6 }));   // the rail's tap
}
