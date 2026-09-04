#include "hal/keysight_ac6834b.hpp"
#include "hal/verbs/route.hpp"
#include "hal/verbs/source.hpp"
#include "hal/keysight_edu36311a.hpp"
#include "hal/verbs/safing.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "core/journal/journal.hpp"
#include "core/testing/capturing_sink.hpp"

using namespace core::literals;
using namespace core::quantities;

//
// The hal side of the run journal: each source instrument's describeConfig
// (the ADL customization point core::ApplyEngine and friends log through -- see
// core/driver/describe.hpp), plus the guarantee that the engines actually post what those
// overloads produce.
//
// That second half is the one worth an integration test rather than only a unit
// one. describeConfig being correct is useless if an engine forgets to call it,
// and "did Apply reach the log" is not something either side can assert about
// itself.
//
namespace
{
    struct DescribeFixture : ::testing::Test
    {
        hal::SwitchFabric      fabric;
        hal::InstrumentWiring  instrumentWiring;
        hal::ConnectorWiring   connectorWiring;

        // Same instrument set and wiring as test_source_instruments.cpp's
        // fixture: DcP5 direct-wired (Apply/Remove only), DcP7 relay-isolated
        // (the one Connect/Disconnect can be called on at all).
        hal::keysight_edu36311a::DirectOutput1      dcP5{ hal::InstrumentId::DcP5, hal::Simulated{} };
        hal::keysight_edu36311a::RelayOutput3       dcP7{ hal::InstrumentId::DcP7, hal::Simulated{} };
        hal::keysight_ac6834b::Ac6834B           acP1{ hal::InstrumentId::AcP1, hal::Simulated{} };

        ApplyEngine      apply{};
        RemoveEngine     remove{};
        ConnectEngine    connect{    fabric, instrumentWiring, connectorWiring };
        DisconnectEngine disconnect{ fabric, instrumentWiring, connectorWiring };

        core::CapturingSink sink;

        DescribeFixture()
        {
            instrumentWiring.addWire( hal::InstrumentId::DcP7, { hal::SwitchDeviceId::Spst1, 7 });
            instrumentWiring.addWire( hal::InstrumentId::AcP1, { hal::SwitchDeviceId::Spst1, 0 });

            // The journal is process-wide (see core/journal/journal.hpp on why), so
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

TEST_F( DescribeFixture, DcConfigNamesTheInstrumentItsSettingsAndWhichOutputItIs)
{
    const auto described = describeConfig( dcP5.dc().voltage( 5.0_V).currentLimit( 4.0_A).config());

    EXPECT_EQ( described.Instrument, "DcP5");

    // Which output it is, and what that output can do -- the two things that
    // distinguish three DcP instances sharing one chassis and one address.
    EXPECT_EQ( described.Settings, "voltage=5 V, currentLimit=4 A, output 1 (6 V / 5 A)");
}

//
// An Apply that left the current limit alone and one that set it to zero are
// very different instructions to a supply, so an unset field must not be
// reported as a value.
//
TEST_F( DescribeFixture, UnsetSettingsAreOmittedRatherThanReportedAsZero)
{
    const auto described = describeConfig( dcP5.dc().voltage( 5.0_V).config());

    EXPECT_EQ( described.Settings, "voltage=5 V, output 1 (6 V / 5 A)");
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
    apply( dcP5.dc().voltage( 5.0_V).currentLimit( 4.0_A));

    ASSERT_EQ( sink.Events.size(), 1u);

    const auto & event = sink.Events[ 0];

    EXPECT_EQ( event.Method,     core::Verb::Apply);
    EXPECT_EQ( event.Subject,    "DcP5");
    EXPECT_EQ( event.Instrument, "DcP5");
    EXPECT_EQ( event.Value,      "voltage=5 V, currentLimit=4 A, output 1 (6 V / 5 A)");

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
    remove(     dcP5.dc().voltage( 5.0_V));
    connect(    dcP7.dc());
    disconnect( dcP7.dc());

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
// may not survive to the next statement (see hal/src/verbs/safing.cpp).
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
// a test that was never performed. See describeConfig's own comment.
//
TEST_F( DescribeFixture, AnUnbalancedAcConfigLogsAllThreePhasesAndSaysItIsPerPhase)
{
    const auto described = describeConfig( acP1.ac()
                                                .phaseVoltage( hal::keysight_ac6834b::phaseA( 115.0_V), hal::keysight_ac6834b::phaseB( 113.0_V), hal::keysight_ac6834b::phaseC( 117.0_V))
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
