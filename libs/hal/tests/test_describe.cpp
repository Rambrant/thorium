#include "hal/ac6834b.hpp"
#include "hal/apply.hpp"
#include "hal/n6701a.hpp"
#include "hal/safing.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "core/journal.hpp"

using namespace core::literals;
using namespace core::quantities;

//
// The hal side of the run journal: each source instrument's describeConfig
// (the ADL customization point core::ApplyEngine and friends log through -- see
// core/apply.hpp), plus the guarantee that the engines actually post what those
// overloads produce.
//
// That second half is the one worth an integration test rather than only a unit
// one. describeConfig being correct is useless if an engine forgets to call it,
// and "did Apply reach the log" is not something either side can assert about
// itself.
//
namespace
{
    class CapturingSink : public core::IJournalSink
    {
        public:
            auto onEvent( const core::JournalEvent & event) -> void override
            {
                Events.push_back( event);
            }

            std::vector<core::JournalEvent> Events;
    };

    struct DescribeFixture : ::testing::Test
    {
        hal::SwitchFabric      fabric;
        hal::InstrumentWiring  instrumentWiring;
        hal::ConnectorWiring   connectorWiring;

        // Same instrument set and wiring as test_source_instruments.cpp's
        // fixture: DcP1 direct-wired (Apply/Remove only), DcP3 relay-isolated
        // (the one Connect/Disconnect can be called on at all).
        hal::N6701ADirect      dcP1{ hal::InstrumentId::DcP1, 1 };
        hal::N6701ARelay       dcP3{ hal::InstrumentId::DcP3, 3 };
        hal::Ac6834B           acP1{ hal::InstrumentId::AcP1 };

        ApplyEngine      apply{};
        RemoveEngine     remove{};
        ConnectEngine    connect{    fabric, instrumentWiring, connectorWiring };
        DisconnectEngine disconnect{ fabric, instrumentWiring, connectorWiring };

        CapturingSink sink;

        DescribeFixture()
        {
            instrumentWiring.addWire( hal::InstrumentId::DcP3, { hal::SwitchDeviceKind::Matrix, "Matrix2", 24 });
            instrumentWiring.addWire( hal::InstrumentId::AcP1, { hal::SwitchDeviceKind::Matrix, "Matrix2", 22 });

            // The journal is process-wide (see core/journal.hpp on why), so
            // each test has to leave it as it found it.
            core::journal().clearSinks();
            core::journal().add( sink);
            core::journal().begin( core::RunInfo{});
        }

        ~DescribeFixture() override
        {
            core::journal().clearSinks();
        }
    };
} // namespace

TEST_F( DescribeFixture, DcConfigNamesTheInstrumentItsSettingsAndItsMainframeSlot)
{
    const auto described = describeConfig( dcP1.dc().voltage( 24.0_V).currentLimit( 7.0_A).config());

    EXPECT_EQ( described.Instrument, "DcP1");

    // The slot is what distinguishes two DcP instances sharing this class.
    EXPECT_EQ( described.Settings, "voltage=24 V, currentLimit=7 A, slot 1");
}

//
// An Apply that left the current limit alone and one that set it to zero are
// very different instructions to a supply, so an unset field must not be
// reported as a value.
//
TEST_F( DescribeFixture, UnsetSettingsAreOmittedRatherThanReportedAsZero)
{
    const auto described = describeConfig( dcP1.dc().voltage( 24.0_V).config());

    EXPECT_EQ( described.Settings, "voltage=24 V, slot 1");
    EXPECT_EQ( described.Settings.find( "currentLimit"), std::string::npos);
}

TEST_F( DescribeFixture, AcConfigStatesItsConnectionModeAndSettings)
{
    const auto described = describeConfig( acP1.ac().phaseVoltage( 230.0_V).frequency( 50.0_Hz).config());

    EXPECT_EQ( described.Instrument, "AcP1");
    EXPECT_EQ( described.Settings,   "3-phase, phaseVoltage=230 V, frequency=50 Hz");
}

TEST_F( DescribeFixture, ApplyPostsWhatWasAppliedToTheJournal)
{
    apply( dcP1.dc().voltage( 24.0_V).currentLimit( 7.0_A));

    ASSERT_EQ( sink.Events.size(), 1u);

    const auto & event = sink.Events[ 0];

    EXPECT_EQ( event.Method,     core::Verb::Apply);
    EXPECT_EQ( event.Subject,    "DcP1");
    EXPECT_EQ( event.Instrument, "DcP1");
    EXPECT_EQ( event.Value,      "voltage=24 V, currentLimit=7 A, slot 1");

    // Not a check -- an Apply has no pass/fail notion, and a sink that
    // rendered one would show it as a passing result in a report.
    EXPECT_FALSE( event.Passed.has_value());
}

//
// Remove/Connect/Disconnect name the instrument and nothing more: there are no
// settings to report (removeDriver only ever reads Instrument), and a log
// claiming otherwise would be describing the config a script happened to build
// rather than what the rig was told.
//
TEST_F( DescribeFixture, RoutingAndRemovalPostTheInstrumentWithoutSettings)
{
    remove(     dcP1.dc().voltage( 24.0_V));
    connect(    dcP3.dc());
    disconnect( dcP3.dc());

    ASSERT_EQ( sink.Events.size(), 3u);

    EXPECT_EQ( sink.Events[ 0].Method, core::Verb::Remove);
    EXPECT_EQ( sink.Events[ 1].Method, core::Verb::Connect);
    EXPECT_EQ( sink.Events[ 2].Method, core::Verb::Disconnect);

    for( const auto & event : sink.Events)
    {
        EXPECT_FALSE( event.Subject.empty());
        EXPECT_TRUE( event.Value.empty());
    }
}

//
// Safing is part of what a run did to the rig, so it has to reach the machine
// log -- and it is posted before the work rather than after, since the process
// may not survive to the next statement (see hal/src/safing.cpp).
//
TEST_F( DescribeFixture, SafingPostsToTheJournal)
{
    hal::safeRig();

    ASSERT_FALSE( sink.Events.empty());
    EXPECT_EQ( sink.Events[ 0].Method,  core::Verb::Safe);
    EXPECT_EQ( sink.Events[ 0].Subject, "rig");
}

//
// A per-phase Apply must not be logged as though it were balanced: a journal
// showing one "phaseVoltage=115V" for a deliberately unbalanced run describes
// a test that was never performed. See hal::describeConfig's own comment.
//
TEST_F( DescribeFixture, AnUnbalancedAcConfigLogsAllThreePhasesAndSaysItIsPerPhase)
{
    const auto described = describeConfig( acP1.ac()
                                                .phaseVoltage( hal::phaseA( 115.0_V), hal::phaseB( 113.0_V), hal::phaseC( 117.0_V))
                                                .frequency( 400.0_Hz)
                                                .config());

    EXPECT_EQ( described.Instrument, "AcP1");

    EXPECT_NE( described.Settings.find( "per-phase"), std::string::npos);
    EXPECT_NE( described.Settings.find( "A 115"),     std::string::npos);
    EXPECT_NE( described.Settings.find( "B 113"),     std::string::npos);
    EXPECT_NE( described.Settings.find( "C 117"),     std::string::npos);

    // One frequency, rendered once -- it has no per-phase form to render.
    EXPECT_NE( described.Settings.find( "frequency="), std::string::npos);
}

TEST_F( DescribeFixture, ABalancedAcConfigStillLogsOneVoltage)
{
    const auto described = describeConfig( acP1.ac().phaseVoltage( 115.0_V).config());

    EXPECT_EQ( described.Settings.find( "per-phase"), std::string::npos);
    EXPECT_NE( described.Settings.find( "phaseVoltage=115"), std::string::npos);
}
