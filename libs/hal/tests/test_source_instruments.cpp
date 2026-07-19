#include "hal/n6701a.hpp"
#include "hal/ac6677a.hpp"
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

        hal::Ac6677A           acP1{ hal::InstrumentId::AcP1 };

        ApplyEngine      apply{};
        RemoveEngine     remove{};
        ConnectEngine    connect{    fabric, instrumentWiring, connectorWiring };
        DisconnectEngine disconnect{ fabric, instrumentWiring, connectorWiring };

        SourceInstrumentFixture()
        {
            // Only DcP3 gets a fixed channel -- DcP1/DcP2 have no relay,
            // so there is nothing for InstrumentWiring to record for them
            // (and nothing would ever look it up, since their config type
            // has no connectDriver to call find() from in the first
            // place).
            instrumentWiring.addWire( hal::InstrumentId::DcP3, { hal::SwitchDeviceKind::Matrix, "Matrix2", 24 });

            // AcP1: four fixed channels -- phases A/B/C plus the neutral/
            // ground return (see hal::Ac6677A's own comment on why the
            // return is included), all under the same InstrumentId so
            // hal::InstrumentWiring::findAll() returns all four together.
            instrumentWiring.addWire( hal::InstrumentId::AcP1, { hal::SwitchDeviceKind::Matrix, "Matrix2", 22 });
            instrumentWiring.addWire( hal::InstrumentId::AcP1, { hal::SwitchDeviceKind::Matrix, "Matrix2", 23 });
            instrumentWiring.addWire( hal::InstrumentId::AcP1, { hal::SwitchDeviceKind::Matrix, "Matrix2", 26 });
            instrumentWiring.addWire( hal::InstrumentId::AcP1, { hal::SwitchDeviceKind::Matrix, "Matrix2", 27 });
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

TEST_F( SourceInstrumentFixture, DcConnectAndDisconnectDoNotDisturbAnUnrelatedAlreadyConnectedPath)
{
    // acP1's phase A and dcP3's relay are two independent fixed channels.
    // Connecting/disconnecting one must leave the other's relay alone --
    // the whole reason connect()/disconnect() are additive rather than
    // route()'s "make this the one live path" behaviour.
    connect( dcP3.dc());
    connect( acP1.threePhaseWye());

    disconnect( dcP3.dc());

    EXPECT_FALSE( fabric.isClosed( { hal::SwitchDeviceKind::Matrix, "Matrix2", 24 }));
    EXPECT_TRUE(  fabric.isClosed( { hal::SwitchDeviceKind::Matrix, "Matrix2", 22 }));
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

TEST_F( SourceInstrumentFixture, AcApplyProgramsTheInstrumentWithoutTouchingTheFabric)
{
    apply( acP1.threePhaseWye().phaseVoltage( 115.0_V).frequency( 400.0_Hz).currentLimit( 3.0_A));

    EXPECT_TRUE( acP1.isEnabled());
    EXPECT_FALSE( fabric.isClosed( { hal::SwitchDeviceKind::Matrix, "Matrix2", 22 }));
}

TEST_F( SourceInstrumentFixture, AcConnectClosesAllFourFixedChannelsPhasesAndGround)
{
    connect( acP1.threePhaseWye());

    EXPECT_TRUE( fabric.isClosed( { hal::SwitchDeviceKind::Matrix, "Matrix2", 22 })); // phase A
    EXPECT_TRUE( fabric.isClosed( { hal::SwitchDeviceKind::Matrix, "Matrix2", 23 })); // phase B
    EXPECT_TRUE( fabric.isClosed( { hal::SwitchDeviceKind::Matrix, "Matrix2", 26 })); // phase C
    EXPECT_TRUE( fabric.isClosed( { hal::SwitchDeviceKind::Matrix, "Matrix2", 27 })); // ground/neutral
}

TEST_F( SourceInstrumentFixture, AcDisconnectOpensAllFourFixedChannelsTogether)
{
    connect( acP1.threePhaseWye());
    disconnect( acP1.threePhaseWye());

    EXPECT_FALSE( fabric.isClosed( { hal::SwitchDeviceKind::Matrix, "Matrix2", 22 }));
    EXPECT_FALSE( fabric.isClosed( { hal::SwitchDeviceKind::Matrix, "Matrix2", 23 }));
    EXPECT_FALSE( fabric.isClosed( { hal::SwitchDeviceKind::Matrix, "Matrix2", 26 }));
    EXPECT_FALSE( fabric.isClosed( { hal::SwitchDeviceKind::Matrix, "Matrix2", 27 }));
}

TEST_F( SourceInstrumentFixture, AcApplySetsPhaseVoltageFrequencyAndCurrentLimit)
{
    apply( acP1.threePhaseWye().phaseVoltage( 115.0_V).frequency( 400.0_Hz).currentLimit( 3.0_A));

    EXPECT_DOUBLE_EQ( acP1.phaseVoltage().value(), 115.0);
    ASSERT_TRUE( acP1.frequency().has_value());
    EXPECT_DOUBLE_EQ( acP1.frequency()->value(), 400.0);
    ASSERT_TRUE( acP1.currentLimit().has_value());
    EXPECT_DOUBLE_EQ( acP1.currentLimit()->value(), 3.0);
}

TEST_F( SourceInstrumentFixture, AcRemoveDisablesTheInstrumentWithoutRequiringAnySetupCalls)
{
    apply( acP1.threePhaseWye().phaseVoltage( 115.0_V));
    ASSERT_TRUE( acP1.isEnabled());

    remove( acP1.threePhaseWye());

    EXPECT_FALSE( acP1.isEnabled());
}

TEST_F( SourceInstrumentFixture, AcBuilderChainReturnsUpdatedCopiesWithoutMutatingTheOriginal)
{
    const auto base      = acP1.threePhaseWye();
    const auto withVolts = base.phaseVoltage( 115.0_V);

    EXPECT_FALSE( base.config().PhaseVoltage.has_value());
    ASSERT_TRUE( withVolts.config().PhaseVoltage.has_value());
    EXPECT_DOUBLE_EQ( withVolts.config().PhaseVoltage->value(), 115.0);
}
