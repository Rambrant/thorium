#include "hal/verbs/interlock.hpp"

#include THORIUM_ACTIVE_INSTRUMENTS
#include "hal/l4411a.hpp"
#include "hal/verbs/measure.hpp"
#include "hal/n6701a.hpp"
#include "hal/verbs/route.hpp"
#include "hal/verbs/source.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "core/verbs/at.hpp"
#include "core/verbs/interlock.hpp"
#include "core/journal/journal.hpp"

using namespace core::literals;
using namespace core::quantities;

using core::at;

//
// The electrical interlock, end to end against this rig -- see
// core/verbs/interlock.hpp for the argument that shapes it, and README.md §1, which
// used to list both of these hazards as things nothing prevented.
//
// This file belongs here rather than in framework/core/tests or in a driver's own
// directory, and for two different reasons depending on which half is being
// tested.
//
// The refusal half has to be here: hal::energisedSourceAt answers from this
// rig's *global* instrument objects and this rig's own SOURCE_WIRING table (see
// hal/src/verbs/interlock.cpp), because those are the two things that correspond to
// hardware. A local N6701A built in a fixture is not on the bench and the
// interlock is right not to see it, so a test of the refusal has to energise
// the real DcP3 -- which makes the global state this file's responsibility to
// put back, and is what the fixture's destructor is for.
//
// The hot-switch half does not: that one asks the config's own instrument, so a
// local one works, and using a local one is better -- these tests can then be
// about a relay moving on a live path without also being about which rail on
// this rig happens to be live at the time.
//
namespace
{
    //
    // DcP3's landing pin -- WIRE_SOURCE( DcP3, A, 1, 8) in rig/wiring.inc,
    // which dut/adapter.inc declares as SOURCE_POINT( BatterySupply, A, 1, 8).
    // Spelled out here rather than reached through dut::BatterySupply because
    // rig_tests has no dut dependency (see rig/CMakeLists.txt), and because a
    // point declared locally lets the Signal/Source contrast below be written
    // at all -- the DUT would never declare the same pin both ways.
    //
    constexpr hal::VpcLocation kBatterySupply{ hal::VpcRack::A, 1, 8 };

    // A pin nothing is cabled onto: an ordinary routed signal point.
    constexpr hal::VpcLocation kOutput5V{ hal::VpcRack::A, 1, 3 };

    constexpr core::AdapterPointTag<kBatterySupply, core::PointKind::Source>
        BatterySupply{ "BatterySupply", "24Vdc secondary backup supply" };

    //
    // The same pin declared Signal, which this rig's own tables make
    // impossible (dut/tests/test_wiring_coverage.cpp static_asserts that an
    // ordinary POINT is not source-wired) and which is exactly why it is worth
    // constructing by hand here: it is the only way to demonstrate that
    // PointKind is what gates the check, rather than the check happening to
    // pass for an unrelated reason.
    //
    constexpr core::AdapterPointTag<kBatterySupply, core::PointKind::Signal>
        BatterySupplyAsSignal{ "BatterySupplyAsSignal", "the same pin, mis-declared" };

    constexpr core::AdapterPointTag<kOutput5V> Output5V{ "Output5V", "5Vdc supply port" };

    class CapturingSink : public core::IJournalSink
    {
        public:
            auto onEvent( const core::JournalEvent & event) -> void override
            {
                Events.push_back( event);
            }

            std::vector<core::JournalEvent> Events;
    };

    struct InterlockFixture : ::testing::Test
    {
        hal::SwitchFabric      fabric;
        hal::InstrumentWiring  instrumentWiring;
        hal::ConnectorWiring   connectorWiring;

        //
        // Empty: this fixture's rig is the routed one, and the interlock is a
        // property of it. A tapped instrument would take the direct branch in
        // core::MeasureEngine and never reach the path composition these tests
        // are about -- see hal::TapWiring.
        //
        hal::TapWiring         tapWiring;

        // Local, for the hot-switch half -- see this file's own comment.
        hal::N6701ARelay  localDcP3{ hal::InstrumentId::DcP3, hal::Simulated{}, 3 };

        ApplyEngine      apply{};
        RemoveEngine     remove{};
        ConnectEngine    connect{    fabric, instrumentWiring, connectorWiring };
        DisconnectEngine disconnect{ fabric, instrumentWiring, connectorWiring };
        MeasureEngine    measure{    fabric, instrumentWiring, connectorWiring, tapWiring };

        CapturingSink sink;

        InterlockFixture()
        {
            instrumentWiring.addWire( hal::InstrumentId::DcP3, { hal::SwitchDeviceId::Spst1, 4 });
            instrumentWiring.addWire( hal::InstrumentId::Dmm1, { hal::SwitchDeviceId::Matrix1, 0 });

            connectorWiring.addWire( kBatterySupply, { hal::SwitchDeviceId::Mux1, 8 });
            connectorWiring.addWire( kOutput5V,      { hal::SwitchDeviceId::Mux1, 3 });

            core::journal().clearSinks();
            core::journal().add( sink);
            core::journal().begin( core::RunInfo{});
        }

        ~InterlockFixture() override
        {
            //
            // The global DcP3 is shared with every other test in this binary,
            // and a test that left it energised would make the next one's
            // interlock fire for reasons that have nothing to do with it. Done
            // unconditionally, including for the tests that never touched it.
            //
            remove( DcP3.dc());

            core::journal().clearSinks();
        }

        [[nodiscard]]
        auto eventsOf( const core::Verb method) const -> std::vector<core::JournalEvent>
        {
            std::vector<core::JournalEvent> found;

            for( const auto & event : sink.Events)
            {
                if( event.Method == method)
                {
                    found.push_back( event);
                }
            }

            return found;
        }
    };
} // namespace

// ---------------------------------------------------------------------------
// hal::energisedSourceAt -- the rig-side lookup
// ---------------------------------------------------------------------------

TEST_F( InterlockFixture, NoSourceLandsOnAnOrdinarySignalPinSoNothingIsEverLiveThere)
{
    apply( DcP3.dc().voltage( 24.0_V));

    // A live rail elsewhere on the rig does not make an unrelated pin live.
    EXPECT_TRUE( hal::energisedSourceAt( kOutput5V).empty());
}

TEST_F( InterlockFixture, ALandingPinWhoseSourceIsOffReportsNothingLive)
{
    ASSERT_FALSE( DcP3.isEnabled());

    EXPECT_TRUE( hal::energisedSourceAt( kBatterySupply).empty());
}

TEST_F( InterlockFixture, ALandingPinNamesItsSourceWhileThatSourceIsEnergised)
{
    apply( DcP3.dc().voltage( 24.0_V).currentLimit( 2.0_A));

    EXPECT_EQ( hal::energisedSourceAt( kBatterySupply), "DcP3");
}

TEST_F( InterlockFixture, RemovingTheSourceMakesItsLandingPinSafeAgain)
{
    apply( DcP3.dc().voltage( 24.0_V));
    ASSERT_EQ( hal::energisedSourceAt( kBatterySupply), "DcP3");

    remove( DcP3.dc());

    EXPECT_TRUE( hal::energisedSourceAt( kBatterySupply).empty());
}

// ---------------------------------------------------------------------------
// The refusal: a low-impedance reading routed onto a live rail
// ---------------------------------------------------------------------------

TEST_F( InterlockFixture, ACurrentMeasurementAtALiveRailIsRefused)
{
    apply( DcP3.dc().voltage( 24.0_V).currentLimit( 2.0_A));

    EXPECT_THROW( (void)measure( Dmm1.current(), at( BatterySupply)), core::InterlockViolation);
}

TEST_F( InterlockFixture, TheRefusalNamesThePinTheRailAndTheWayOut)
{
    apply( DcP3.dc().voltage( 24.0_V));

    try
    {
        (void)measure( Dmm1.current(), at( BatterySupply));
        FAIL() << "expected the interlock to refuse this reading";
    }
    catch( const core::InterlockViolation & refused)
    {
        const auto message = std::string( refused.what());

        EXPECT_NE( message.find( "BatterySupply"), std::string::npos) << message;
        EXPECT_NE( message.find( "DcP3"),          std::string::npos) << message;
        EXPECT_NE( message.find( "Dmm1"),          std::string::npos) << message;

        // The remedy, on the same line as the fault -- see core::liveTapMessage.
        EXPECT_NE( message.find( "Remove("), std::string::npos) << message;
    }
}

TEST_F( InterlockFixture, ARefusedReadingClosesNoRelayAndPostsNoMeasurement)
{
    apply( DcP3.dc().voltage( 24.0_V));

    EXPECT_THROW( (void)measure( Dmm1.current(), at( BatterySupply)), core::InterlockViolation);

    //
    // The whole point of checking before the path is composed: a refusal that
    // had already closed the ammeter onto the rail would have done the damage
    // it exists to prevent, and one that logged a Measure would put a reading
    // in the report that was never taken.
    //
    EXPECT_FALSE( fabric.isClosed( { hal::SwitchDeviceId::Matrix1, 0 }));
    EXPECT_FALSE( fabric.isClosed( { hal::SwitchDeviceId::Mux1, 8 }));
    EXPECT_TRUE( eventsOf( core::Verb::Measure).empty());
}

TEST_F( InterlockFixture, AResistanceMeasurementAtALiveRailIsRefusedToo)
{
    apply( DcP3.dc().voltage( 24.0_V));

    // Not a short, but an ohmmeter's own test current fighting a 24 V rail --
    // see core::requiresDeadNode on why both kinds are in the same predicate.
    EXPECT_THROW( (void)measure( Dmm1.resistance(), at( BatterySupply)), core::InterlockViolation);
}

TEST_F( InterlockFixture, TheSameCurrentMeasurementIsAllowedOnceTheRailIsOff)
{
    apply(  DcP3.dc().voltage( 24.0_V));
    remove( DcP3.dc());

    EXPECT_NO_THROW( (void)measure( Dmm1.current(), at( BatterySupply)));
}

TEST_F( InterlockFixture, AVoltageTapOnALiveRailStaysPermitted)
{
    apply( DcP3.dc().voltage( 24.0_V));

    //
    // The measurement a landing pin is worth declaring for in the first place:
    // what actually arrives at the DUT's pin, cable drop and bad contacts
    // included (see core::PointKind and suite/scripts/rig_power_on.cpp). If
    // this ever starts throwing, the interlock has become the over-constraint
    // core::PointKind's own comment describes talking itself out of.
    //
    EXPECT_NO_THROW( (void)measure( Dmm1.voltage(), at( BatterySupply)));
}

TEST_F( InterlockFixture, PointKindIsWhatGatesTheCheckNotTheLocation)
{
    apply( DcP3.dc().voltage( 24.0_V));

    //
    // The identical pin, identical live rail, identical port -- declared
    // Signal instead of Source, and therefore not checked at all. This is what
    // makes the guard on the routed overload a compile-time one worth having:
    // an ordinary POINT pays nothing for it, and cannot, because
    // dut/tests/test_wiring_coverage.cpp makes this declaration impossible in
    // a real adapter.
    //
    EXPECT_NO_THROW( (void)measure( Dmm1.current(), at( BatterySupplyAsSignal)));
}

// ---------------------------------------------------------------------------
// The record: hot switching, which is reported and never refused
// ---------------------------------------------------------------------------

TEST_F( InterlockFixture, AColdConnectSaysNothingAboutHotSwitching)
{
    connect( localDcP3.dc());

    const auto connects = eventsOf( core::Verb::Connect);

    ASSERT_EQ( connects.size(), 1u);
    EXPECT_EQ( connects.front().Detail, "");
}

TEST_F( InterlockFixture, ConnectingOntoAnEnergisedOutputIsRecordedAndStillHappens)
{
    apply(   localDcP3.dc().voltage( 24.0_V));
    connect( localDcP3.dc());

    const auto connects = eventsOf( core::Verb::Connect);

    ASSERT_EQ( connects.size(), 1u);
    EXPECT_NE( connects.front().Detail.find( "hot switching"), std::string::npos)
        << connects.front().Detail;
    EXPECT_NE( connects.front().Detail.find( "closed"), std::string::npos)
        << connects.front().Detail;

    //
    // Recorded, not refused -- see core/verbs/source.hpp, which has always said this
    // is a wear argument and the sequence author's call. The relay closed.
    //
    EXPECT_TRUE( fabric.isClosed( { hal::SwitchDeviceId::Spst1, 4 }));
}

TEST_F( InterlockFixture, DisconnectingUnderLoadIsRecordedAndStillHappens)
{
    connect( localDcP3.dc());
    apply(   localDcP3.dc().voltage( 24.0_V));

    disconnect( localDcP3.dc());

    const auto disconnects = eventsOf( core::Verb::Disconnect);

    ASSERT_EQ( disconnects.size(), 1u);
    EXPECT_NE( disconnects.front().Detail.find( "hot switching"), std::string::npos)
        << disconnects.front().Detail;
    EXPECT_NE( disconnects.front().Detail.find( "opened"), std::string::npos)
        << disconnects.front().Detail;

    EXPECT_FALSE( fabric.isClosed( { hal::SwitchDeviceId::Spst1, 4 }));
}

TEST_F( InterlockFixture, TheNestedSequenceAScriptShouldWriteRecordsNoHotSwitchAtEitherEnd)
{
    //
    // Connect, Apply ... Remove, Disconnect -- the sequence core/verbs/source.hpp
    // documents and suite/scripts/rig_power_on.cpp writes out. Both contacts
    // move cold, so neither event carries a note.
    //
    connect(    localDcP3.dc());
    apply(      localDcP3.dc().voltage( 24.0_V));
    remove(     localDcP3.dc());
    disconnect( localDcP3.dc());

    for( const auto & event : sink.Events)
    {
        EXPECT_EQ( event.Detail.find( "hot switching"), std::string::npos)
            << core::to_string( event.Method) << ": " << event.Detail;
    }
}

TEST_F( InterlockFixture, HotSwitchingNamesTheInstrumentThatWasLive)
{
    apply(      localDcP3.dc().voltage( 24.0_V));
    disconnect( localDcP3.dc());

    const auto disconnects = eventsOf( core::Verb::Disconnect);

    ASSERT_EQ( disconnects.size(), 1u);
    EXPECT_NE( disconnects.front().Detail.find( "DcP3"), std::string::npos)
        << disconnects.front().Detail;
}
