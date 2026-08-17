//
// hal::N6701A's own tests, split out of libs/hal/tests/test_source_instruments.cpp
// when this driver moved into its own directory. That file held N6701A's and
// Ac6834B's tests together behind one shared fixture; each driver now has its own
// copy of the fixture, trimmed to the instruments it actually names.
//
// Fixture and suite names are deliberately unchanged (SourceInstrumentFixture,
// SourceInstrument) even though a narrower name would read better here: TEST_F
// takes its suite name from the fixture, so renaming it would rename every test
// in this file and destroy the before/after comparison that makes the move
// provably behaviour-preserving. Worth doing later, on its own.
//
// One test did not come along: DcConnectAndDisconnectDoNotDisturbAnUnrelated-
// AlreadyConnectedPath, which needs an Ac6834B to supply the unrelated path and
// so genuinely spans two drivers. It stayed in libs/hal/tests/ -- see that
// file's own comment.
//
#include "hal/n6701a.hpp"
#include "hal/apply.hpp"

#include <gtest/gtest.h>

using namespace core::literals;
using namespace core::quantities;

namespace
{
    //
    // hal::N6701A<DirectWiring>'s Isolation doesn't satisfy
    // hal::SwitchableIsolation -- see that concept's own comment for why.
    // Checked the concept-wrapped way (see
    // core/tests/test_static_constraints.cpp's own comment for why a bare
    // static_assert(!requires{...}) is unreliable and this form isn't):
    // this is the compile-time half of the guarantee, since a runtime
    // test can't exercise "this doesn't compile" without breaking the
    // build.
    //
    template<typename Isolation>
    concept CanConnectN6701A = requires( hal::SwitchFabric & fabric, const hal::InstrumentWiring & iw, const hal::ConnectorWiring & cw, const hal::N6701AConfig<Isolation> & config)
    {
        connectDriver( fabric, iw, cw, config);
        disconnectDriver( fabric, iw, cw, config);
    };

    static_assert(  CanConnectN6701A<hal::RelayIsolated> );
    static_assert( !CanConnectN6701A<hal::DirectWiring> );

    //
    // The actual point of SwitchableIsolation being a concept rather than
    // a per-tag connectDriver/disconnectDriver overload: a brand new
    // relay-having tag, never seen anywhere in hal/n6701a.hpp, still gets
    // Connect/Disconnect for free -- nobody had to write a new overload
    // for it. If connectDriver/disconnectDriver had instead been written
    // out per tag (RelayIsolated's version, copy-pasted under a new
    // name), this tag would need its own copy before this static_assert
    // could pass.
    //
    struct HypotheticalContactorIsolation { static constexpr bool HasRelay = true; };

    static_assert( CanConnectN6701A<HypotheticalContactorIsolation> );

    struct SourceInstrumentFixture : ::testing::Test
    {
        hal::SwitchFabric      fabric;
        hal::InstrumentWiring  instrumentWiring;
        hal::ConnectorWiring   connectorWiring;

        // DcP1/DcP2: direct-wired, no isolation relay -- matches
        // instrument.inc's real assignment. Apply/Remove only; there is
        // no Connect/Disconnect to call on these at all (see
        // CanConnectN6701A above).
        hal::N6701ADirect      dcP1{ hal::InstrumentId::DcP1, 1 };
        hal::N6701ADirect      dcP2{ hal::InstrumentId::DcP2, 2 };

        // DcP3: has a real isolation relay -- matches instrument.inc's
        // real assignment. This is the one Connect/Disconnect tests below
        // exercise.
        hal::N6701ARelay       dcP3{ hal::InstrumentId::DcP3, 3 };

        ApplyEngine      apply{};
        RemoveEngine     remove{};
        ConnectEngine    connect{    fabric, instrumentWiring, connectorWiring };

        SourceInstrumentFixture()
        {
            // Only DcP3 gets a fixed channel -- DcP1/DcP2 have no relay,
            // so there is nothing for InstrumentWiring to record for them
            // (and nothing would ever look it up, since their config type
            // has no connectDriver to call find() from in the first
            // place).
            instrumentWiring.addWire( hal::InstrumentId::DcP3, { hal::SwitchDeviceKind::Matrix, "Matrix2", 24 });
        }
    };
} // namespace

TEST_F( SourceInstrumentFixture, DcApplyProgramsTheInstrumentWithoutTouchingTheFabric)
{
    apply( dcP1.dc().voltage( 24.0_V).currentLimit( 7.0_A));

    EXPECT_TRUE( dcP1.isEnabled());
    EXPECT_DOUBLE_EQ( dcP1.outputVoltage().value(), 24.0);
    ASSERT_TRUE( dcP1.currentLimit().has_value());
    EXPECT_DOUBLE_EQ( dcP1.currentLimit()->value(), 7.0);
}

TEST( SourceInstrument, DcApplyWithOnlyVoltageLeavesCurrentLimitUnset)
{
    hal::N6701ADirect dcP1{ hal::InstrumentId::DcP1, 1 };

    // Programs the instrument directly -- applyDriver needs no fabric/
    // wiring at all, so there's no separate "fixture-based" path to
    // exercise here.
    dcP1.applyOutput( 24.0_V, std::nullopt);

    EXPECT_DOUBLE_EQ( dcP1.outputVoltage().value(), 24.0);
    EXPECT_FALSE( dcP1.currentLimit().has_value());
}

TEST_F( SourceInstrumentFixture, DirectWiredSuppliesHaveNoRelayForConnectToClose)
{
    // DcP1/DcP2 have no fixed channel wired at all (see the fixture's own
    // comment) -- the real guarantee here is compile-time (CanConnectN6701A
    // above): there simply is no Connect(dcP1.dc()) call that compiles for
    // a DirectWiring instrument. Apply still works fine on its own.
    apply( dcP1.dc().voltage( 24.0_V));

    EXPECT_TRUE( dcP1.isEnabled());
}

TEST_F( SourceInstrumentFixture, DcConnectClosesExactlyTheOneFixedChannel)
{
    connect( dcP3.dc());

    EXPECT_TRUE( fabric.isClosed( { hal::SwitchDeviceKind::Matrix, "Matrix2", 24 }));
}

TEST( SourceInstrument, DcConnectClosesRemoteSenseLeadsTogetherWithForceWhenTheyAreWired)
{
    // Not every DC rail has remote-sense leads wired at all -- most of
    // this rig's don't (see the shared fixture above, which never adds a
    // Sense entry for DcP3). When one does, hal::N6701A's connectDriver
    // uses findAll() (see that header's own comment), which doesn't
    // filter by role -- so a WIRE_INSTRUMENT_SENSE entry closes/opens
    // together with the force channel automatically, no driver change
    // needed to support it.
    hal::SwitchFabric      fabric;
    hal::InstrumentWiring  instrumentWiring;
    hal::ConnectorWiring   connectorWiring;
    hal::N6701ARelay       dcP3{ hal::InstrumentId::DcP3, 3 };

    instrumentWiring.addWire( hal::InstrumentId::DcP3, { hal::SwitchDeviceKind::Matrix, "Matrix2", 24 });
    instrumentWiring.addWire( hal::InstrumentId::DcP3, { hal::SwitchDeviceKind::Matrix, "Matrix2", 25 }, hal::WireRole::Sense);

    ConnectEngine    connect{    fabric, instrumentWiring, connectorWiring };
    DisconnectEngine disconnect{ fabric, instrumentWiring, connectorWiring };

    connect( dcP3.dc());

    EXPECT_TRUE( fabric.isClosed( { hal::SwitchDeviceKind::Matrix, "Matrix2", 24 })); // force
    EXPECT_TRUE( fabric.isClosed( { hal::SwitchDeviceKind::Matrix, "Matrix2", 25 })); // sense

    disconnect( dcP3.dc());

    EXPECT_FALSE( fabric.isClosed( { hal::SwitchDeviceKind::Matrix, "Matrix2", 24 }));
    EXPECT_FALSE( fabric.isClosed( { hal::SwitchDeviceKind::Matrix, "Matrix2", 25 }));
}

TEST_F( SourceInstrumentFixture, DcApplyCanBeCalledBeforeConnectIsEverMade)
{
    // The point of splitting Apply out from Connect: programming the
    // supply doesn't require the DUT to be wired up yet.
    apply( dcP3.dc().voltage( 24.0_V));
    EXPECT_TRUE( dcP3.isEnabled());
    EXPECT_FALSE( fabric.isClosed( { hal::SwitchDeviceKind::Matrix, "Matrix2", 24 }));

    connect( dcP3.dc());
    EXPECT_TRUE( fabric.isClosed( { hal::SwitchDeviceKind::Matrix, "Matrix2", 24 }));
}

TEST_F( SourceInstrumentFixture, DcRemoveDisablesTheInstrumentWithoutTouchingTheFabric)
{
    apply( dcP3.dc().voltage( 24.0_V));
    connect( dcP3.dc());
    ASSERT_TRUE( dcP3.isEnabled());

    remove( dcP3.dc());

    EXPECT_FALSE( dcP3.isEnabled());
    // Remove doesn't disconnect -- that's Disconnect's job, called on its
    // own schedule (e.g. immediately, for a safety interlock, without
    // waiting on some other Remove-driven ramp-down).
    EXPECT_TRUE( fabric.isClosed( { hal::SwitchDeviceKind::Matrix, "Matrix2", 24 }));
}

TEST_F( SourceInstrumentFixture, DcBuilderChainReturnsUpdatedCopiesWithoutMutatingTheOriginal)
{
    const auto base     = dcP1.dc();
    const auto withVolt = base.voltage( 24.0_V);

    EXPECT_FALSE( base.config().Voltage.has_value());
    ASSERT_TRUE( withVolt.config().Voltage.has_value());
    EXPECT_DOUBLE_EQ( withVolt.config().Voltage->value(), 24.0);
}

TEST_F( SourceInstrumentFixture, TwoN6701AChannelsAreProgrammedIndependently)
{
    // DcP1 and DcP2 are two separate hal::N6701ADirect instances -- two
    // channels of the same physical mainframe, but with no shared state at
    // this layer, the same way Dmm1/Dmm2 don't share state today.
    apply( dcP1.dc().voltage( 24.0_V));
    apply( dcP2.dc().voltage( 5.0_V));

    EXPECT_DOUBLE_EQ( dcP1.outputVoltage().value(), 24.0);
    EXPECT_DOUBLE_EQ( dcP2.outputVoltage().value(), 5.0);
    EXPECT_EQ( dcP1.channel(), 1);
    EXPECT_EQ( dcP2.channel(), 2);

    remove( dcP1.dc());

    EXPECT_FALSE( dcP1.isEnabled());
    EXPECT_TRUE( dcP2.isEnabled());
}
