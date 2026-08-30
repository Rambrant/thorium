//
// hal::Ac6834B's own tests, split out of rig/tests/test_source_instruments.cpp
// when this driver moved into its own directory -- the N6701A half went to
// instruments/n6701a/tests/ at the same time, and that file's own comment
// explains the shared fixture the two used to hold in common.
//
// Fixture and suite names are unchanged for the same reason given there: TEST_F
// takes its suite name from the fixture, so renaming would rename every test
// here and cost the before/after comparison that makes the move provably
// behaviour-preserving.
//
#include "hal/ac6834b.hpp"
#include "hal/verbs/route.hpp"
#include "hal/verbs/source.hpp"

#include <gtest/gtest.h>

#include <concepts>

//
// This model's back panel, as the constructor constraint actually sees it --
// checked in both directions, since a check that only ever passes proves
// nothing about what it rejects (the same shape rig/tests/test_safing.cpp
// uses for hal::SafeableInstrument, and hal/tests/driver/test_address.cpp for the
// hal::ReachableOver mechanism itself).
//
// GPIB or RS-232 and nothing else on this source -- and note that
// hal::Serial here is the PC's own port, which is why it is the one driver
// in this rig that accepts it.
//
namespace
{
    static_assert(   std::constructible_from< hal::Ac6834B, hal::InstrumentId, hal::Gpib> );
    static_assert(   std::constructible_from< hal::Ac6834B, hal::InstrumentId, hal::Serial> );
    static_assert(   std::constructible_from< hal::Ac6834B, hal::InstrumentId, hal::Simulated> );
    static_assert( ! std::constructible_from< hal::Ac6834B, hal::InstrumentId, hal::Lan> );
    static_assert( ! std::constructible_from< hal::Ac6834B, hal::InstrumentId, hal::Usb> );
    static_assert( ! std::constructible_from< hal::Ac6834B, hal::InstrumentId> );
} // namespace

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

        hal::Ac6834B           acP1{ hal::InstrumentId::AcP1, hal::Simulated{} };

        ApplyEngine      apply{};
        RemoveEngine     remove{};
        ConnectEngine    connect{    fabric, instrumentWiring, connectorWiring };
        DisconnectEngine disconnect{ fabric, instrumentWiring, connectorWiring };

        SourceInstrumentFixture()
        {
            // AcP1: four fixed channels -- phases A/B/C plus the neutral/
            // ground return (see hal::Ac6834B's own comment on why the
            // return is included), all under the same InstrumentId so
            // hal::InstrumentWiring::findAll() returns all four together.
            instrumentWiring.addWire( hal::InstrumentId::AcP1, { hal::SwitchDeviceId::Spst1, 0 });
            instrumentWiring.addWire( hal::InstrumentId::AcP1, { hal::SwitchDeviceId::Spst1, 1 });
            instrumentWiring.addWire( hal::InstrumentId::AcP1, { hal::SwitchDeviceId::Spst1, 2 });
            instrumentWiring.addWire( hal::InstrumentId::AcP1, { hal::SwitchDeviceId::Spst1, 3 });
        }
    };
} // namespace

TEST_F( SourceInstrumentFixture, AcApplyProgramsTheInstrumentWithoutTouchingTheFabric)
{
    apply( acP1.ac().phaseVoltage( 115.0_V).frequency( 400.0_Hz).currentLimit( 3.0_A));

    EXPECT_TRUE( acP1.isEnabled());
    EXPECT_FALSE( fabric.isClosed( { hal::SwitchDeviceId::Spst1, 0 }));
}

TEST_F( SourceInstrumentFixture, AcConnectClosesAllFourFixedChannelsPhasesAndGround)
{
    connect( acP1.ac());

    EXPECT_TRUE( fabric.isClosed( { hal::SwitchDeviceId::Spst1, 0 })); // phase A
    EXPECT_TRUE( fabric.isClosed( { hal::SwitchDeviceId::Spst1, 1 })); // phase B
    EXPECT_TRUE( fabric.isClosed( { hal::SwitchDeviceId::Spst1, 2 })); // phase C
    EXPECT_TRUE( fabric.isClosed( { hal::SwitchDeviceId::Spst1, 3 })); // ground/neutral
}

TEST_F( SourceInstrumentFixture, AcDisconnectOpensAllFourFixedChannelsTogether)
{
    connect( acP1.ac());
    disconnect( acP1.ac());

    EXPECT_FALSE( fabric.isClosed( { hal::SwitchDeviceId::Spst1, 0 }));
    EXPECT_FALSE( fabric.isClosed( { hal::SwitchDeviceId::Spst1, 1 }));
    EXPECT_FALSE( fabric.isClosed( { hal::SwitchDeviceId::Spst1, 2 }));
    EXPECT_FALSE( fabric.isClosed( { hal::SwitchDeviceId::Spst1, 3 }));
}

TEST_F( SourceInstrumentFixture, AcApplySetsPhaseVoltageFrequencyAndCurrentLimit)
{
    apply( acP1.ac().phaseVoltage( 115.0_V).frequency( 400.0_Hz).currentLimit( 3.0_A));

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
    apply( acP1.ac().phaseVoltage( 115.0_V));
    ASSERT_TRUE( acP1.isEnabled());

    remove( acP1.ac());

    EXPECT_FALSE( acP1.isEnabled());
}

TEST_F( SourceInstrumentFixture, AcBuilderChainReturnsUpdatedCopiesWithoutMutatingTheOriginal)
{
    const auto base      = acP1.ac();
    const auto withVolts = base.phaseVoltage( 115.0_V);

    EXPECT_FALSE( base.config().PhaseVoltage.has_value());
    ASSERT_TRUE( withVolts.config().PhaseVoltage.has_value());
    EXPECT_DOUBLE_EQ( withVolts.config().PhaseVoltage->value(), 115.0);
}

//
// ---------------------------------------------------------------------------
// Per-phase configuration
// ---------------------------------------------------------------------------
// The unbalanced half of hal::Ac6834BBuilder -- see hal::Balanced/hal::PerPhase
// for why this is a symmetry the builder acquires rather than a second entry
// point alongside ac().
//
TEST_F( SourceInstrumentFixture, AcPerPhaseVoltagesReachTheirOwnPhases)
{
    apply( acP1.ac()
                .phaseVoltage( hal::phaseA( 115.0_V), hal::phaseB( 113.0_V), hal::phaseC( 117.0_V))
                .frequency( 400.0_Hz));

    EXPECT_DOUBLE_EQ( acP1.phaseVoltage( hal::Phase::A).value(), 115.0);
    EXPECT_DOUBLE_EQ( acP1.phaseVoltage( hal::Phase::B).value(), 113.0);
    EXPECT_DOUBLE_EQ( acP1.phaseVoltage( hal::Phase::C).value(), 117.0);
}

TEST_F( SourceInstrumentFixture, AcAPerPhaseSetterSwitchesTheConfigToPerPhase)
{
    const auto balanced = acP1.ac().phaseVoltage( 115.0_V);
    const auto perPhase = acP1.ac().phaseVoltage( hal::phaseA( 115.0_V), hal::phaseB( 113.0_V), hal::phaseC( 117.0_V));

    static_assert( std::is_same_v<decltype( balanced)::Config, hal::Ac6834BConfig<hal::Balanced>>);
    static_assert( std::is_same_v<decltype( perPhase)::Config, hal::Ac6834BConfig<hal::PerPhase>>);
}

TEST_F( SourceInstrumentFixture, AcSettingsMadeBeforeTheSplitAreCarriedAcrossIt)
{
    // frequency() and currentLimit() were set while the chain was still
    // balanced; naming voltages per phase afterwards must not drop them.
    apply( acP1.ac()
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
    const auto builder = acP1.ac()
                              .phaseVoltage( hal::phaseA( 115.0_V), hal::phaseB( 113.0_V), hal::phaseC( 117.0_V))
                              .currentLimit( 3.0_A);

    // Still per-phase: the transition is one-way (see hal::Ac6834BBuilder).
    static_assert( std::is_same_v<decltype( builder)::Config, hal::Ac6834BConfig<hal::PerPhase>>);

    ASSERT_TRUE( builder.config().CurrentLimit.has_value());
    EXPECT_DOUBLE_EQ( ( *builder.config().CurrentLimit)[ 0].value(), 3.0);
    EXPECT_DOUBLE_EQ( ( *builder.config().CurrentLimit)[ 2].value(), 3.0);
}

TEST_F( SourceInstrumentFixture, AcPerPhaseCurrentLimitsReachTheirOwnPhases)
{
    apply( acP1.ac()
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
// overload to call (see hal::Ac6834BConfig's own comment). Stated as a
// compile-time check because the guarantee is the *absence* of an overload,
// which nothing else in this file would notice going missing.
//
// Concept-with-bound-parameters rather than a bare requires-expression -- see
// the IMPORTANT note at the top of core/tests/criteria/test_static_constraints.cpp.
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
    using BuilderT = hal::Ac6834BBuilder<hal::Balanced>;

    // The control: voltage does have one, so what follows is about frequency
    // rather than about the phaseA/B/C spelling.
    static_assert( HasPerPhaseVoltage<BuilderT, Voltage>);

    static_assert( ! HasPerPhaseFrequency<BuilderT, Frequency>,
                   "frequency must not be settable per phase -- the three phases of one source "
                   "share a frequency by construction (see hal::Ac6834BConfig)");
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
    static_assert( ! AcceptsTransposedPhases<hal::Ac6834BBuilder<hal::Balanced>, Voltage>,
                   "phase arguments must be A, B, C in order -- the phase is part of each "
                   "argument's type precisely so a transposition cannot compile");
}

//
// Readback follows the setpoints per phase, and each phase keys its own
// recording slot -- see core::Port::qualifiedBy.
//
TEST_F( SourceInstrumentFixture, AcEachPhaseReadsBackItsOwnVoltage)
{
    apply( acP1.ac().phaseVoltage( hal::phaseA( 115.0_V), hal::phaseB( 113.0_V), hal::phaseC( 117.0_V)));

    EXPECT_EQ( acP1.measuredVoltage( hal::Phase::A).qualifier(), "A");
    EXPECT_EQ( acP1.measuredVoltage( hal::Phase::C).qualifier(), "C");

    EXPECT_DOUBLE_EQ( acP1.measuredVoltage( hal::Phase::A).rawMeasure().value(), 115.0);
    EXPECT_DOUBLE_EQ( acP1.measuredVoltage( hal::Phase::B).rawMeasure().value(), 113.0);
    EXPECT_DOUBLE_EQ( acP1.measuredVoltage( hal::Phase::C).rawMeasure().value(), 117.0);
}

TEST_F( SourceInstrumentFixture, AcADisabledOutputReadsZeroOnEveryPhase)
{
    apply( acP1.ac().phaseVoltage( hal::phaseA( 115.0_V), hal::phaseB( 113.0_V), hal::phaseC( 117.0_V)));
    remove( acP1.ac());

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

//
// ---------------------------------------------------------------------------
// Instrument-wide readings
// ---------------------------------------------------------------------------
// The three the 6834B answers for the source as a whole rather than per phase
// -- MEASure:FREQuency?, MEASure:POWer:AC:TOTal? and MEASure:CURRent:NEUTral?,
// none of which the programming guide marks "Phase Selectable". See
// hal::Ac6834B::measuredFrequency and its neighbours.
//
TEST_F( SourceInstrumentFixture, AcFrequencyReadsBackWithoutNamingAPhase)
{
    apply( acP1.ac().phaseVoltage( 115.0_V).frequency( 400.0_Hz));

    EXPECT_DOUBLE_EQ( acP1.measuredFrequency().rawMeasure().value(), 400.0);
    EXPECT_EQ( acP1.measuredFrequency().qualifier(), "");
}

TEST_F( SourceInstrumentFixture, AcTotalPowerIsQualifiedSoItCannotCollideWithPerPhasePower)
{
    // MEASure:POWer? is itself phase selectable, so per-phase power is a real
    // future addition; "Total" keeps this reading's recording slot distinct.
    acP1.setSimulatedTotalPower( 1400.0_W);
    apply( acP1.ac().phaseVoltage( 115.0_V));

    EXPECT_EQ( acP1.measuredTotalPower().qualifier(), "Total");
    EXPECT_DOUBLE_EQ( acP1.measuredTotalPower().rawMeasure().value(), 1400.0);
}

TEST_F( SourceInstrumentFixture, AcNeutralCurrentIsItsOwnReadingNotAFourthPhase)
{
    acP1.setSimulatedNeutralCurrent( 0.4_A);
    apply( acP1.ac().phaseVoltage( hal::phaseA( 115.0_V), hal::phaseB( 113.0_V), hal::phaseC( 117.0_V)));

    EXPECT_EQ( acP1.measuredNeutralCurrent().qualifier(), "N");
    EXPECT_DOUBLE_EQ( acP1.measuredNeutralCurrent().rawMeasure().value(), 0.4);

    // Distinct from any phase's current -- the neutral carries the imbalance,
    // not one of the conductors' load.
    acP1.setSimulatedOutputCurrent( 2.0_A);
    EXPECT_DOUBLE_EQ( acP1.measuredCurrent( hal::Phase::B).rawMeasure().value(), 2.0);
    EXPECT_DOUBLE_EQ( acP1.measuredNeutralCurrent().rawMeasure().value(),        0.4);
}

TEST_F( SourceInstrumentFixture, AcInstrumentWideReadingsGoDeadWithTheOutput)
{
    acP1.setSimulatedTotalPower( 1400.0_W);
    acP1.setSimulatedNeutralCurrent( 0.4_A);
    apply( acP1.ac().phaseVoltage( 115.0_V).frequency( 400.0_Hz));

    remove( acP1.ac());

    EXPECT_DOUBLE_EQ( acP1.measuredFrequency().rawMeasure().value(),      0.0);
    EXPECT_DOUBLE_EQ( acP1.measuredTotalPower().rawMeasure().value(),     0.0);
    EXPECT_DOUBLE_EQ( acP1.measuredNeutralCurrent().rawMeasure().value(), 0.0);
}

//
// There is still no no-argument voltage, and that is the instrument's answer
// rather than a modelling preference: MEASure:VOLTage is marked "Phase
// Selectable", and the guide states outright that "there is no way to query
// more than one phase with a single command". A no-argument measuredVoltage()
// would have nothing to send.
//
// Asserted so the absence is deliberate and stays that way -- the neighbouring
// no-argument readings above make it look like an oversight otherwise.
//
namespace
{
    template<typename SourceT>
    concept HasUnqualifiedVoltage = requires( SourceT source) { source.measuredVoltage(); };

    template<typename SourceT>
    concept HasUnqualifiedFrequency = requires( SourceT source) { source.measuredFrequency(); };
} // namespace

TEST_F( SourceInstrumentFixture, AcHasNoUnqualifiedVoltageReading)
{
    // The control: no-argument readings do exist where the instrument has one.
    static_assert( HasUnqualifiedFrequency<hal::Ac6834B>);

    static_assert( ! HasUnqualifiedVoltage<hal::Ac6834B>,
                   "measuredVoltage() must require a phase -- MEASure:VOLTage is Phase Selectable "
                   "and the instrument cannot answer an unqualified voltage query");
}

//
// ---------------------------------------------------------------------------
// Output range
// ---------------------------------------------------------------------------
// VOLTage:RANGe -- Phase Selectable like voltage and current limit, and taking
// the voltage the range must accommodate rather than a range identifier. See
// hal::rangeFor.
//
TEST_F( SourceInstrumentFixture, AcRangeResolvesToWhatTheInstrumentWouldSelect)
{
    // "Sending a parameter greater than 150 selects the 300 volt range,
    // otherwise the 150 volt range is selected" -- so a request is resolved,
    // not stored verbatim.
    static_assert( hal::rangeFor( 115.0_V) == hal::LowVoltageRange);
    static_assert( hal::rangeFor( 150.0_V) == hal::LowVoltageRange);   // boundary: 150 is not "greater than 150"
    static_assert( hal::rangeFor( 150.1_V) == hal::HighVoltageRange);
    static_assert( hal::rangeFor( 230.0_V) == hal::HighVoltageRange);

    apply( acP1.ac().phaseVoltage( 115.0_V).range( 115.0_V));

    for( const auto phase : hal::phases)
    {
        ASSERT_TRUE( acP1.range( phase).has_value());
        EXPECT_DOUBLE_EQ( acP1.range( phase)->value(), 150.0);
    }
}

TEST_F( SourceInstrumentFixture, AcRangeCanDifferPerPhaseLikeAnyOtherPhaseSelectableSetting)
{
    apply( acP1.ac()
                .phaseVoltage( hal::phaseA( 115.0_V), hal::phaseB( 230.0_V), hal::phaseC( 115.0_V))
                .range(        hal::phaseA( 115.0_V), hal::phaseB( 230.0_V), hal::phaseC( 115.0_V)));

    EXPECT_DOUBLE_EQ( acP1.range( hal::Phase::A)->value(), 150.0);
    EXPECT_DOUBLE_EQ( acP1.range( hal::Phase::B)->value(), 300.0);
    EXPECT_DOUBLE_EQ( acP1.range( hal::Phase::C)->value(), 150.0);
}

TEST_F( SourceInstrumentFixture, AcAnApplyThatSaysNothingAboutTheRangeLeavesItAlone)
{
    // std::nullopt means "use whatever is already configured" here as
    // everywhere else -- an Apply without a range must not silently reset it.
    apply( acP1.ac().phaseVoltage( 230.0_V).range( 230.0_V));
    ASSERT_DOUBLE_EQ( acP1.range( hal::Phase::A)->value(), 300.0);

    apply( acP1.ac().phaseVoltage( 115.0_V));

    EXPECT_DOUBLE_EQ( acP1.range( hal::Phase::A)->value(), 300.0);
}

TEST_F( SourceInstrumentFixture, AcRangeIsUnsetUntilSomethingSetsIt)
{
    apply( acP1.ac().phaseVoltage( 115.0_V));

    EXPECT_FALSE( acP1.range( hal::Phase::A).has_value());
}

TEST_F( SourceInstrumentFixture, AcRangeAppearsInTheJournalDescription)
{
    const auto described = describeConfig( acP1.ac().phaseVoltage( 115.0_V).range( 115.0_V).config());

    EXPECT_NE( described.Settings.find( "range=150"), std::string::npos);
}
