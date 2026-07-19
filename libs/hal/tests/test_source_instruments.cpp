#include "hal/n6701a.hpp"
#include "hal/ac6677a.hpp"
#include "hal/apply.hpp"

#include <gtest/gtest.h>

using namespace core::literals;
using namespace core::quantities;

namespace
{
    struct SourceInstrumentFixture : ::testing::Test
    {
        hal::SwitchFabric      fabric;
        hal::InstrumentWiring  instrumentWiring;
        hal::ConnectorWiring   connectorWiring;

        hal::N6701A            dcP1{ hal::InstrumentId::DcP1, 1 };
        hal::N6701A            dcP2{ hal::InstrumentId::DcP2, 2 };
        hal::Ac6677A           acP1{ hal::InstrumentId::AcP1 };

        ApplyEngine      apply{};
        RemoveEngine     remove{};
        ConnectEngine    connect{    fabric, instrumentWiring, connectorWiring };
        DisconnectEngine disconnect{ fabric, instrumentWiring, connectorWiring };

        SourceInstrumentFixture()
        {
            // DcP1/DcP2: one fixed channel apiece -- a real DC rail is
            // hard-cabled straight to one VPC pin (see hal::N6701A's own
            // comment for why), not routed through a mux.
            instrumentWiring.addWire( hal::InstrumentId::DcP1, { hal::SwitchDeviceKind::Matrix, "Matrix2", 20 });
            instrumentWiring.addWire( hal::InstrumentId::DcP2, { hal::SwitchDeviceKind::Matrix, "Matrix2", 21 });

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

    // Apply doesn't touch the fabric at all -- programming the instrument
    // and closing its relay are separate calls (Connect, below).
    EXPECT_FALSE( fabric.isClosed( { hal::SwitchDeviceKind::Matrix, "Matrix2", 20 }));
}

TEST( SourceInstrument, DcApplyWithOnlyVoltageLeavesCurrentLimitUnset)
{
    hal::N6701A dcP1{ hal::InstrumentId::DcP1, 1 };

    // Programs the instrument directly -- applyDriver needs no fabric/
    // wiring at all any more, so there's no separate "fixture-based" path
    // to exercise here the way there used to be.
    dcP1.applyOutput( 24.0_V, std::nullopt);

    EXPECT_DOUBLE_EQ( dcP1.outputVoltage().value(), 24.0);
    EXPECT_FALSE( dcP1.currentLimit().has_value());
}

TEST_F( SourceInstrumentFixture, DcConnectClosesExactlyTheOneFixedChannel)
{
    connect( dcP1.dc());

    EXPECT_TRUE( fabric.isClosed( { hal::SwitchDeviceKind::Matrix, "Matrix2", 20 }));
}

TEST_F( SourceInstrumentFixture, DcConnectAndDisconnectDoNotDisturbAnUnrelatedAlreadyConnectedPath)
{
    // dcP1 and dcP2 are two independent fixed channels. Connecting/
    // disconnecting one must leave the other's relay alone -- the whole
    // reason connect()/disconnect() are additive rather than route()'s
    // "make this the one live path" behaviour.
    connect( dcP1.dc());
    connect( dcP2.dc());

    disconnect( dcP1.dc());

    EXPECT_FALSE( fabric.isClosed( { hal::SwitchDeviceKind::Matrix, "Matrix2", 20 }));
    EXPECT_TRUE(  fabric.isClosed( { hal::SwitchDeviceKind::Matrix, "Matrix2", 21 }));
}

TEST_F( SourceInstrumentFixture, DcApplyCanBeCalledBeforeConnectIsEverMade)
{
    // The point of splitting Apply out from Connect: programming the
    // supply doesn't require the DUT to be wired up yet.
    apply( dcP1.dc().voltage( 24.0_V));
    EXPECT_TRUE( dcP1.isEnabled());
    EXPECT_FALSE( fabric.isClosed( { hal::SwitchDeviceKind::Matrix, "Matrix2", 20 }));

    connect( dcP1.dc());
    EXPECT_TRUE( fabric.isClosed( { hal::SwitchDeviceKind::Matrix, "Matrix2", 20 }));
}

TEST_F( SourceInstrumentFixture, DcRemoveDisablesTheInstrumentWithoutTouchingTheFabric)
{
    apply( dcP1.dc().voltage( 24.0_V));
    connect( dcP1.dc());
    ASSERT_TRUE( dcP1.isEnabled());

    remove( dcP1.dc());

    EXPECT_FALSE( dcP1.isEnabled());
    // Remove doesn't disconnect -- that's Disconnect's job, called on its
    // own schedule (e.g. immediately, for a safety interlock, without
    // waiting on some other Remove-driven ramp-down).
    EXPECT_TRUE( fabric.isClosed( { hal::SwitchDeviceKind::Matrix, "Matrix2", 20 }));
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
    // DcP1 and DcP2 are two separate hal::N6701A instances -- two channels
    // of the same physical mainframe, but with no shared state at this
    // layer, the same way Dmm1/Dmm2 don't share state today.
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
