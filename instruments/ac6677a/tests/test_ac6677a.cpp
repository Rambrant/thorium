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

#include <type_traits>

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
    apply( acP1.wye().phaseVoltage( 115.0_V).frequency( 400.0_Hz).currentLimit( 3.0_A));

    EXPECT_TRUE( acP1.isEnabled());
    EXPECT_FALSE( fabric.isClosed( { hal::SwitchDeviceKind::Matrix, "Matrix2", 22 }));
}

TEST_F( SourceInstrumentFixture, AcConnectClosesAllFourFixedChannelsPhasesAndGround)
{
    connect( acP1.wye());

    EXPECT_TRUE( fabric.isClosed( { hal::SwitchDeviceKind::Matrix, "Matrix2", 22 })); // phase A
    EXPECT_TRUE( fabric.isClosed( { hal::SwitchDeviceKind::Matrix, "Matrix2", 23 })); // phase B
    EXPECT_TRUE( fabric.isClosed( { hal::SwitchDeviceKind::Matrix, "Matrix2", 26 })); // phase C
    EXPECT_TRUE( fabric.isClosed( { hal::SwitchDeviceKind::Matrix, "Matrix2", 27 })); // ground/neutral
}

TEST_F( SourceInstrumentFixture, AcDisconnectOpensAllFourFixedChannelsTogether)
{
    connect( acP1.wye());
    disconnect( acP1.wye());

    EXPECT_FALSE( fabric.isClosed( { hal::SwitchDeviceKind::Matrix, "Matrix2", 22 }));
    EXPECT_FALSE( fabric.isClosed( { hal::SwitchDeviceKind::Matrix, "Matrix2", 23 }));
    EXPECT_FALSE( fabric.isClosed( { hal::SwitchDeviceKind::Matrix, "Matrix2", 26 }));
    EXPECT_FALSE( fabric.isClosed( { hal::SwitchDeviceKind::Matrix, "Matrix2", 27 }));
}

TEST_F( SourceInstrumentFixture, AcApplySetsPhaseVoltageFrequencyAndCurrentLimit)
{
    apply( acP1.wye().phaseVoltage( 115.0_V).frequency( 400.0_Hz).currentLimit( 3.0_A));

    // Balanced: one setpoint reaches every phase.
    for( const auto phase : hal::phases)
    {
        EXPECT_DOUBLE_EQ( acP1.phaseVoltage( phase).value(), 115.0);
        ASSERT_TRUE( acP1.currentLimit( phase).has_value());
        EXPECT_DOUBLE_EQ( acP1.currentLimit( phase)->value(), 3.0);
    }

    ASSERT_TRUE( acP1.frequency().has_value());
    EXPECT_DOUBLE_EQ( acP1.frequency()->value(), 400.0);
}

TEST_F( SourceInstrumentFixture, AcRemoveDisablesTheInstrumentWithoutRequiringAnySetupCalls)
{
    apply( acP1.wye().phaseVoltage( 115.0_V));
    ASSERT_TRUE( acP1.isEnabled());

    remove( acP1.wye());

    EXPECT_FALSE( acP1.isEnabled());
}

TEST_F( SourceInstrumentFixture, AcBuilderChainReturnsUpdatedCopiesWithoutMutatingTheOriginal)
{
    const auto base      = acP1.wye();
    const auto withVolts = base.phaseVoltage( 115.0_V);

    EXPECT_FALSE( base.config().PhaseVoltage.has_value());
    ASSERT_TRUE( withVolts.config().PhaseVoltage.has_value());
    EXPECT_DOUBLE_EQ( withVolts.config().PhaseVoltage->value(), 115.0);
}

//
// ---------------------------------------------------------------------------
// Per-phase configuration
// ---------------------------------------------------------------------------
// The unbalanced half of hal::Ac6677ABuilder -- see hal::Balanced/hal::PerPhase
// for why this is a symmetry the builder acquires rather than a second entry
// point alongside wye().
//
TEST_F( SourceInstrumentFixture, AcPerPhaseVoltagesReachTheirOwnPhases)
{
    apply( acP1.wye()
                .phaseVoltage( hal::phaseA( 115.0_V), hal::phaseB( 113.0_V), hal::phaseC( 117.0_V))
                .frequency( 400.0_Hz));

    EXPECT_DOUBLE_EQ( acP1.phaseVoltage( hal::Phase::A).value(), 115.0);
    EXPECT_DOUBLE_EQ( acP1.phaseVoltage( hal::Phase::B).value(), 113.0);
    EXPECT_DOUBLE_EQ( acP1.phaseVoltage( hal::Phase::C).value(), 117.0);
}

TEST_F( SourceInstrumentFixture, AcAPerPhaseSetterSwitchesTheConfigToPerPhase)
{
    const auto balanced = acP1.wye().phaseVoltage( 115.0_V);
    const auto perPhase = acP1.wye().phaseVoltage( hal::phaseA( 115.0_V), hal::phaseB( 113.0_V), hal::phaseC( 117.0_V));

    static_assert( std::is_same_v<decltype( balanced)::Config, hal::Ac6677AConfig<hal::Balanced>>);
    static_assert( std::is_same_v<decltype( perPhase)::Config, hal::Ac6677AConfig<hal::PerPhase>>);
}

TEST_F( SourceInstrumentFixture, AcSettingsMadeBeforeTheSplitAreCarriedAcrossIt)
{
    // frequency() and currentLimit() were set while the chain was still
    // balanced; naming voltages per phase afterwards must not drop them.
    apply( acP1.wye()
                .frequency( 400.0_Hz)
                .currentLimit( 3.0_A)
                .phaseVoltage( hal::phaseA( 115.0_V), hal::phaseB( 113.0_V), hal::phaseC( 117.0_V)));

    ASSERT_TRUE( acP1.frequency().has_value());
    EXPECT_DOUBLE_EQ( acP1.frequency()->value(), 400.0);

    // A scalar current limit broadcasts -- one shared limit across otherwise
    // unbalanced phases is an ordinary thing to want (see hal::detail::broadcast).
    for( const auto phase : hal::phases)
    {
        ASSERT_TRUE( acP1.currentLimit( phase).has_value());
        EXPECT_DOUBLE_EQ( acP1.currentLimit( phase)->value(), 3.0);
    }
}

TEST_F( SourceInstrumentFixture, AcAScalarSetterAfterTheSplitBroadcastsRatherThanReverting)
{
    const auto builder = acP1.wye()
                              .phaseVoltage( hal::phaseA( 115.0_V), hal::phaseB( 113.0_V), hal::phaseC( 117.0_V))
                              .currentLimit( 3.0_A);

    // Still per-phase: the transition is one-way (see hal::Ac6677ABuilder).
    static_assert( std::is_same_v<decltype( builder)::Config, hal::Ac6677AConfig<hal::PerPhase>>);

    ASSERT_TRUE( builder.config().CurrentLimit.has_value());
    EXPECT_DOUBLE_EQ( ( *builder.config().CurrentLimit)[ 0].value(), 3.0);
    EXPECT_DOUBLE_EQ( ( *builder.config().CurrentLimit)[ 2].value(), 3.0);
}

TEST_F( SourceInstrumentFixture, AcPerPhaseCurrentLimitsReachTheirOwnPhases)
{
    apply( acP1.wye()
                .phaseVoltage( 115.0_V)
                .currentLimit( hal::phaseA( 3.0_A), hal::phaseB( 2.0_A), hal::phaseC( 4.0_A)));

    // The voltage was set before the split and broadcasts to all three.
    EXPECT_DOUBLE_EQ( acP1.phaseVoltage( hal::Phase::B).value(), 115.0);

    EXPECT_DOUBLE_EQ( acP1.currentLimit( hal::Phase::A)->value(), 3.0);
    EXPECT_DOUBLE_EQ( acP1.currentLimit( hal::Phase::B)->value(), 2.0);
    EXPECT_DOUBLE_EQ( acP1.currentLimit( hal::Phase::C)->value(), 4.0);
}

//
// Frequency is deliberately scalar-only: three phases of one source share a
// fixed relationship and therefore a frequency, so there is no per-phase
// overload to call (see hal::Ac6677AConfig's own comment). Stated as a
// compile-time check because the guarantee is the *absence* of an overload,
// which nothing else in this file would notice going missing.
//
// Concept-with-bound-parameters rather than a bare requires-expression -- see
// the IMPORTANT note at the top of core/tests/test_static_constraints.cpp.
//
namespace
{
    template<typename BuilderT, typename ValueT>
    concept HasPerPhaseFrequency = requires( BuilderT builder, ValueT v)
    {
        builder.frequency( hal::phaseA( v), hal::phaseB( v), hal::phaseC( v));
    };

    template<typename BuilderT, typename ValueT>
    concept HasPerPhaseVoltage = requires( BuilderT builder, ValueT v)
    {
        builder.phaseVoltage( hal::phaseA( v), hal::phaseB( v), hal::phaseC( v));
    };
} // namespace

TEST_F( SourceInstrumentFixture, AcFrequencyHasNoPerPhaseForm)
{
    using BuilderT = hal::Ac6677ABuilder<hal::Balanced>;

    // The control: voltage does have one, so what follows is about frequency
    // rather than about the phaseA/B/C spelling.
    static_assert( HasPerPhaseVoltage<BuilderT, Voltage>);

    static_assert( ! HasPerPhaseFrequency<BuilderT, Frequency>,
                   "frequency must not be settable per phase -- the three phases of one source "
                   "share a frequency by construction (see hal::Ac6677AConfig)");
}

//
// The phases are tagged by type, so they can only be given in A/B/C order --
// a transposition is a compile error rather than a silent swap (see
// hal::PhaseValue).
//
namespace
{
    template<typename BuilderT, typename ValueT>
    concept AcceptsTransposedPhases = requires( BuilderT builder, ValueT v)
    {
        builder.phaseVoltage( hal::phaseB( v), hal::phaseA( v), hal::phaseC( v));
    };
} // namespace

TEST_F( SourceInstrumentFixture, AcPhasesCannotBeGivenOutOfOrder)
{
    static_assert( ! AcceptsTransposedPhases<hal::Ac6677ABuilder<hal::Balanced>, Voltage>,
                   "phase arguments must be A, B, C in order -- the phase is part of each "
                   "argument's type precisely so a transposition cannot compile");
}

//
// Readback follows the setpoints per phase, and each phase keys its own
// recording slot -- see core::Port::qualifiedBy.
//
TEST_F( SourceInstrumentFixture, AcEachPhaseReadsBackItsOwnVoltage)
{
    apply( acP1.wye().phaseVoltage( hal::phaseA( 115.0_V), hal::phaseB( 113.0_V), hal::phaseC( 117.0_V)));

    EXPECT_EQ( acP1.measuredVoltage( hal::Phase::A).qualifier(), "A");
    EXPECT_EQ( acP1.measuredVoltage( hal::Phase::C).qualifier(), "C");

    EXPECT_DOUBLE_EQ( acP1.measuredVoltage( hal::Phase::A).rawMeasure().value(), 115.0);
    EXPECT_DOUBLE_EQ( acP1.measuredVoltage( hal::Phase::B).rawMeasure().value(), 113.0);
    EXPECT_DOUBLE_EQ( acP1.measuredVoltage( hal::Phase::C).rawMeasure().value(), 117.0);
}

TEST_F( SourceInstrumentFixture, AcADisabledOutputReadsZeroOnEveryPhase)
{
    apply( acP1.wye().phaseVoltage( hal::phaseA( 115.0_V), hal::phaseB( 113.0_V), hal::phaseC( 117.0_V)));
    remove( acP1.wye());

    for( const auto phase : hal::phases)
    {
        EXPECT_DOUBLE_EQ( acP1.measuredVoltage( phase).rawMeasure().value(), 0.0);
    }
}

//
// to_string reflects Phase's own enumerators rather than repeating them (see
// hal::to_string( Phase)). Asserted because the failure mode of getting this
// wrong is silent: a mislabelled phase in a journal or a recording key reads
// as a real reading of the wrong conductor.
//
TEST_F( SourceInstrumentFixture, AcPhaseNamesComeFromTheEnumeratorsThemselves)
{
    EXPECT_EQ( hal::to_string( hal::Phase::A), "A");
    EXPECT_EQ( hal::to_string( hal::Phase::B), "B");
    EXPECT_EQ( hal::to_string( hal::Phase::C), "C");

    // And the list used by every per-phase loop is the same three, in order.
    static_assert( hal::phases.size() == 3);
    EXPECT_EQ( hal::phases[ 1], hal::Phase::B);
}
