//
// hal::Ac6677A's own tests, split out of libs/hal/tests/test_source_instruments.cpp
// when this driver moved into its own directory -- the N6701A half went to
// instruments/n6701a/tests/ at the same time, and that file's own comment
// explains the shared fixture the two used to hold in common.
//
// Fixture and suite names are unchanged for the same reason given there: TEST_F
// takes its suite name from the fixture, so renaming would rename every test
// here and cost the before/after comparison that makes the move provably
// behaviour-preserving.
//
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

        hal::Ac6677A           acP1{ hal::InstrumentId::AcP1 };

        ApplyEngine      apply{};
        RemoveEngine     remove{};
        ConnectEngine    connect{    fabric, instrumentWiring, connectorWiring };
        DisconnectEngine disconnect{ fabric, instrumentWiring, connectorWiring };

        SourceInstrumentFixture()
        {
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
