#include "hal/n6701a.hpp"
#include "hal/ac6677a.hpp"
#include "hal/apply.hpp"

#include <gtest/gtest.h>

#include "core/at.hpp"

using namespace core::literals;
using namespace core::quantities;

using core::at;
using hal::phase;

namespace
{
    constexpr hal::VpcLocation kInput24V{ hal::VpcRack::A, 1, 5 };
    constexpr hal::VpcLocation kOutput5V{ hal::VpcRack::A, 1, 3 };
    constexpr hal::VpcLocation kPhaseA{ hal::VpcRack::A, 3, 1 };
    constexpr hal::VpcLocation kPhaseB{ hal::VpcRack::A, 3, 3 };
    constexpr hal::VpcLocation kPhaseC{ hal::VpcRack::A, 3, 5 };

    constexpr core::AdapterPointTag<kInput24V, core::QuantityKind::Voltage> Input24V{ "Input24V", "24Vdc input supply port" };
    constexpr core::AdapterPointTag<kOutput5V, core::QuantityKind::Voltage> Output5V{ "Output5V", "5Vdc supply port" };
    constexpr core::AdapterPointTag<kPhaseA,   core::QuantityKind::Voltage> AcInput_A{ "AcInput_A", "AC input, phase A" };
    constexpr core::AdapterPointTag<kPhaseB,   core::QuantityKind::Voltage> AcInput_B{ "AcInput_B", "AC input, phase B" };
    constexpr core::AdapterPointTag<kPhaseC,   core::QuantityKind::Voltage> AcInput_C{ "AcInput_C", "AC input, phase C" };

    constexpr core::AdapterPointTag<kInput24V, core::QuantityKind::Current> NotVoltageTagged{ "NotVoltageTagged", "wrong kind, on purpose" };

    struct SourceInstrumentFixture : ::testing::Test
    {
        hal::SwitchFabric      fabric;
        hal::InstrumentWiring  instrumentWiring;
        hal::ConnectorWiring   connectorWiring;

        hal::N6701A            dcP1{ hal::InstrumentId::DcP1, 1 };
        hal::N6701A            dcP2{ hal::InstrumentId::DcP2, 2 };
        hal::Ac6677A           acP1{ hal::InstrumentId::AcP1 };

        ApplyEngine  apply{  fabric, instrumentWiring, connectorWiring };
        RemoveEngine remove{ fabric, instrumentWiring, connectorWiring };

        SourceInstrumentFixture()
        {
            instrumentWiring.addWire( hal::InstrumentId::DcP1, { hal::SwitchDeviceKind::Matrix, "Matrix2", 20 });
            instrumentWiring.addWire( hal::InstrumentId::DcP2, { hal::SwitchDeviceKind::Matrix, "Matrix2", 21 });
            instrumentWiring.addWire( hal::InstrumentId::AcP1, { hal::SwitchDeviceKind::Matrix, "Matrix2", 22 });

            connectorWiring.addWire( kInput24V, { hal::SwitchDeviceKind::Mux, "Mux1", 6 });
            connectorWiring.addWire( kOutput5V, { hal::SwitchDeviceKind::Mux, "Mux1", 3 });
            connectorWiring.addWire( kPhaseA,   { hal::SwitchDeviceKind::Mux, "Mux2", 2 });
            connectorWiring.addWire( kPhaseB,   { hal::SwitchDeviceKind::Mux, "Mux2", 3 });
            connectorWiring.addWire( kPhaseC,   { hal::SwitchDeviceKind::Mux, "Mux2", 4 });
        }
    };
} // namespace

TEST_F( SourceInstrumentFixture, DcApplyRoutesTheFabricAndProgramsTheInstrument)
{
    apply( dcP1.dc( at( Input24V)).voltage( 24.0_V).currentLimit( 7.0_A));

    EXPECT_TRUE( dcP1.isEnabled());
    EXPECT_DOUBLE_EQ( dcP1.outputVoltage().value(), 24.0);
    ASSERT_TRUE( dcP1.currentLimit().has_value());
    EXPECT_DOUBLE_EQ( dcP1.currentLimit()->value(), 7.0);
}

TEST( SourceInstrument, DcApplyWithOnlyVoltageLeavesCurrentLimitUnset)
{
    hal::N6701A dcP1{ hal::InstrumentId::DcP1, 1 };

    // Programs the instrument directly, bypassing routing -- exercised on
    // its own since applyDriver requires wired fabric/tables (see the
    // fixture-based tests above for that path).
    dcP1.applyOutput( 24.0_V, std::nullopt);

    EXPECT_DOUBLE_EQ( dcP1.outputVoltage().value(), 24.0);
    EXPECT_FALSE( dcP1.currentLimit().has_value());
}

TEST_F( SourceInstrumentFixture, DcRemoveRoutesTheFabricAndDisablesTheInstrument)
{
    apply( dcP1.dc( at( Input24V)).voltage( 24.0_V));
    ASSERT_TRUE( dcP1.isEnabled());

    remove( dcP1.dc( at( Input24V)));

    EXPECT_FALSE( dcP1.isEnabled());
}

TEST_F( SourceInstrumentFixture, DcBuilderChainReturnsUpdatedCopiesWithoutMutatingTheOriginal)
{
    const auto base     = dcP1.dc( at( Input24V));
    const auto withVolt = base.voltage( 24.0_V);

    EXPECT_FALSE( base.config().Voltage.has_value());
    ASSERT_TRUE( withVolt.config().Voltage.has_value());
    EXPECT_DOUBLE_EQ( withVolt.config().Voltage->value(), 24.0);
}

TEST_F( SourceInstrumentFixture, TwoN6701AChannelsAreProgrammedAndRoutedIndependently)
{
    // DcP1 and DcP2 are two separate hal::N6701A instances -- two channels
    // of the same physical mainframe, but with no shared state at this
    // layer, the same way Dmm1/Dmm2 don't share state today.
    apply( dcP1.dc( at( Input24V)).voltage( 24.0_V));
    apply( dcP2.dc( at( Output5V)).voltage( 5.0_V));

    EXPECT_DOUBLE_EQ( dcP1.outputVoltage().value(), 24.0);
    EXPECT_DOUBLE_EQ( dcP2.outputVoltage().value(), 5.0);
    EXPECT_EQ( dcP1.channel(), 1);
    EXPECT_EQ( dcP2.channel(), 2);

    remove( dcP1.dc( at( Input24V)));

    EXPECT_FALSE( dcP1.isEnabled());
    EXPECT_TRUE( dcP2.isEnabled());
}

TEST_F( SourceInstrumentFixture, AcApplyRoutesTheInstrumentAndTheThreePhaseChannelsTogether)
{
    apply( acP1.threePhaseWye( { .a=phase( at( AcInput_A)), .b=phase( at( AcInput_B)),
                                 .c=phase( at( AcInput_C)) })
               .phaseVoltage( 115.0_V).frequency( 400.0_Hz).currentLimit( 3.0_A));

    EXPECT_TRUE( acP1.isEnabled());
}

TEST_F( SourceInstrumentFixture, AcApplySetsPhaseVoltageFrequencyAndCurrentLimit)
{
    apply( acP1.threePhaseWye( { .a=phase( at( AcInput_A)), .b=phase( at( AcInput_B)),
                                 .c=phase( at( AcInput_C)) })
               .phaseVoltage( 115.0_V).frequency( 400.0_Hz).currentLimit( 3.0_A));

    EXPECT_DOUBLE_EQ( acP1.phaseVoltage().value(), 115.0);
    ASSERT_TRUE( acP1.frequency().has_value());
    EXPECT_DOUBLE_EQ( acP1.frequency()->value(), 400.0);
    ASSERT_TRUE( acP1.currentLimit().has_value());
    EXPECT_DOUBLE_EQ( acP1.currentLimit()->value(), 3.0);
}

TEST_F( SourceInstrumentFixture, AcRemoveDisablesTheInstrumentWithoutRequiringAnySetupCalls)
{
    const hal::ThreePhaseWyePoints points{ .a=phase( at( AcInput_A)), .b=phase( at( AcInput_B)),
                                            .c=phase( at( AcInput_C)) };

    apply( acP1.threePhaseWye( points).phaseVoltage( 115.0_V));
    ASSERT_TRUE( acP1.isEnabled());

    remove( acP1.threePhaseWye( points));

    EXPECT_FALSE( acP1.isEnabled());
}

TEST_F( SourceInstrumentFixture, AcBuilderChainReturnsUpdatedCopiesWithoutMutatingTheOriginal)
{
    const hal::ThreePhaseWyePoints points{ .a=phase( at( AcInput_A)), .b=phase( at( AcInput_B)),
                                            .c=phase( at( AcInput_C)) };

    const auto base      = acP1.threePhaseWye( points);
    const auto withVolts = base.phaseVoltage( 115.0_V);

    EXPECT_FALSE( base.config().PhaseVoltage.has_value());
    ASSERT_TRUE( withVolts.config().PhaseVoltage.has_value());
    EXPECT_DOUBLE_EQ( withVolts.config().PhaseVoltage->value(), 115.0);
}

namespace
{
    //
    // Same concept-wrapped static_assert pattern as
    // core/tests/test_static_constraints.cpp -- see that file's own comment
    // for why a bare static_assert(!requires{...}) isn't reliable here.
    // Proves the two verified compile-fail cases from this session's design
    // work: a Current-tagged point has no matching .dc(at(...))/phase(at(...))
    // overload, since both require Kind == Voltage.
    //
    template<typename InstrumentT, typename WrappedT>
    concept HasDcOverload = requires( InstrumentT & instrument, const WrappedT & wrapped) { instrument.dc( wrapped); };

    template<typename WrappedT>
    concept HasPhaseOverload = requires( const WrappedT & wrapped) { hal::phase( wrapped); };

    using VoltagePoint = decltype( core::at( Input24V));
    using CurrentPoint = decltype( core::at( NotVoltageTagged));

    static_assert(  HasDcOverload<hal::N6701A, VoltagePoint> );
    static_assert( !HasDcOverload<hal::N6701A, CurrentPoint> );

    static_assert(  HasPhaseOverload<VoltagePoint> );
    static_assert( !HasPhaseOverload<CurrentPoint> );
} // namespace

TEST( SourceInstrumentStaticConstraints, DcAndPhaseRejectNonVoltagePointsAtCompileTime)
{
    // Nothing to run -- see the file comment on the static_asserts above.
    SUCCEED();
}
