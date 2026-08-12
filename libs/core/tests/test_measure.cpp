#include "core/measure.hpp"

#include <filesystem>
#include <fstream>
#include <generator>
#include <vector>

#include <gtest/gtest.h>

#include "core/at.hpp"

using namespace core::literals;
using namespace core::quantities;

using core::at;

//
// A minimal Fabric/InstrumentWiring/ConnectorWiring/Instrument stand-in,
// satisfying exactly what core::MeasureEngine needs and nothing more -- see
// core/measure.hpp's own comment for the exact shape each is expected to
// have. hal::SwitchFabric/hal::InstrumentWiring/hal::ConnectorWiring (see
// hal/measure.cpp) are just one concrete set of types happening to satisfy
// this same shape; core::MeasureEngine never sees them.
//
namespace mock
{
    enum class InstrumentId { Dmm1, Dmm2 };

    [[nodiscard]] auto to_string( InstrumentId id) -> std::string_view
    {
        return id == InstrumentId::Dmm1 ? "Dmm1" : "Dmm2";
    }

    struct Location
    {
        int value;
        friend constexpr auto operator==( Location, Location) -> bool = default;
    };

    using Channel = int;
    using Path    = std::vector<Channel>;

    class Fabric
    {
        public:
            auto connect( const std::vector<Channel> & path) -> void { mLastConnected = path; }
            auto disconnect( const std::vector<Channel> & path) -> void { mLastDisconnected = path; }

            [[nodiscard]] auto lastConnected() const -> const std::vector<Channel> & { return mLastConnected; }
            [[nodiscard]] auto lastDisconnected() const -> const std::vector<Channel> & { return mLastDisconnected; }

        private:
            std::vector<Channel> mLastConnected;
            std::vector<Channel> mLastDisconnected;
    };

    class InstrumentWiring
    {
        public:
            auto addWire( InstrumentId instrument, Channel channel) -> void { mEntries.push_back( { instrument, channel }); }

            //
            // Returns a Path (see hal::Path's own comment in
            // hal/switch_fabric.hpp for why this is a vector, not a
            // scalar): core::MeasureEngine composes this with the
            // connector's own Path by concatenation, so even this
            // single-channel mock needs to hand back something
            // vector-like, not the bare channel.
            //
            [[nodiscard]] auto find( InstrumentId instrument) const -> Path
            {
                for( const auto & [ id, channel] : mEntries)
                {
                    if( id == instrument) return Path{ channel };
                }
                throw std::runtime_error( "mock::InstrumentWiring: not wired");
            }

            //
            // No sense-wired instrument in this mock's tests -- always
            // throws, exactly like the real hal::InstrumentWiring::
            // findSense() would for an instrument nobody wired sense
            // leads for. Exists purely to satisfy core::MeasureEngine's
            // structural requirement (see that class's own comment on
            // core::SensePath::Required) -- this file has no 4-wire-style test.
            //
            [[nodiscard]] auto findSense( InstrumentId ) const -> Path
            {
                throw std::runtime_error( "mock::InstrumentWiring: no sense wiring in this mock");
            }

        private:
            std::vector<std::pair<InstrumentId, Channel>> mEntries;
    };

    class ConnectorWiring
    {
        public:
            auto addWire( Location location, Channel channel) -> void { mEntries.push_back( { location, channel }); }

            [[nodiscard]] auto find( Location location) const -> Path
            {
                for( const auto & [ loc, channel] : mEntries)
                {
                    if( loc == location) return Path{ channel };
                }
                throw std::runtime_error( "mock::ConnectorWiring: not wired");
            }

            // See mock::InstrumentWiring::findSense()'s own comment.
            [[nodiscard]] auto findSense( Location ) const -> Path
            {
                throw std::runtime_error( "mock::ConnectorWiring: no sense wiring in this mock");
            }

        private:
            std::vector<std::pair<Location, Channel>> mEntries;
    };

    class Instrument
    {
        public:
            explicit Instrument( InstrumentId id) : mId( id) {}

            [[nodiscard]] auto id() const -> InstrumentId { return mId; }

            [[nodiscard]] auto voltage() -> core::Port<Voltage, Instrument> { return core::Port<Voltage, Instrument>{ *this }; }

            auto setSimulatedVoltage( Voltage v) -> void { mVoltage = v; }

            template<core::quantities::QuantityType Q>
            [[nodiscard]] auto rawMeasure( const core::MeasureSetup<Q> & ) -> Q { return mVoltage; }

        private:
            InstrumentId mId;
            Voltage      mVoltage{};
    };
} // namespace mock

namespace
{
    constexpr mock::Location kLoc{ 3 };
    constexpr mock::Location kLoc2{ 4 };

    constexpr core::AdapterPointTag<kLoc>  Output5V{ "Output5V", "5Vdc supply port" };

    // A second point, so a test can say something about one point's programming
    // without saying it about every point at once -- see UseLiveDiscards... below.
    constexpr core::AdapterPointTag<kLoc2> Output3V3{ "Output3V3", "3.3Vdc supply port" };

    struct MeasureEngineFixture : ::testing::Test
    {
        mock::Fabric            fabric;
        mock::InstrumentWiring  instrumentWiring;
        mock::ConnectorWiring   connectorWiring;
        mock::Instrument        dmm1{ mock::InstrumentId::Dmm1 };

        core::MeasureEngine<mock::Fabric, mock::InstrumentWiring, mock::ConnectorWiring> Measure{ fabric, instrumentWiring, connectorWiring };

        MeasureEngineFixture()
        {
            instrumentWiring.addWire( mock::InstrumentId::Dmm1, 14);
            connectorWiring.addWire( kLoc, 3);
            connectorWiring.addWire( kLoc2, 4);
        }
    };
} // namespace

TEST_F( MeasureEngineFixture, LiveByDefaultConnectsTakesTheReadingThenDisconnects)
{
    dmm1.setSimulatedVoltage( 5.02_V);

    const auto value = Measure( dmm1.voltage(), at( Output5V));

    EXPECT_DOUBLE_EQ( value.value(), 5.02);
    EXPECT_EQ( fabric.lastConnected(),    (std::vector<mock::Channel>{ 14, 3 }));
    EXPECT_EQ( fabric.lastDisconnected(), (std::vector<mock::Channel>{ 14, 3 }));
}

TEST_F( MeasureEngineFixture, InjectBypassesTheFabricEntirely)
{
    Measure.inject( "Output5V", Voltage{ 5.02 });
    dmm1.setSimulatedVoltage( 999.0_V); // would fail the test if this were ever read

    const auto value = Measure( dmm1.voltage(), at( Output5V));

    EXPECT_DOUBLE_EQ( value.value(), 5.02);
    EXPECT_TRUE( fabric.lastConnected().empty());
    EXPECT_TRUE( fabric.lastDisconnected().empty());
}

//
// The sequence overloads. What matters across these three is that they are one
// mechanism: a braced list, an owned container and a coroutine all arrive as a
// core::ValueSource, so a test can pick whichever way of describing the values
// it wants without the engine offering a different behaviour for each.
//
TEST_F( MeasureEngineFixture, InjectServesABracedListOneValuePerMeasurement)
{
    Measure.inject( "Output5V", { 5.02_V, 5.03_V, 4.90_V });

    EXPECT_DOUBLE_EQ( Measure( dmm1.voltage(), at( Output5V)).value(), 5.02);
    EXPECT_DOUBLE_EQ( Measure( dmm1.voltage(), at( Output5V)).value(), 5.03);
    EXPECT_DOUBLE_EQ( Measure( dmm1.voltage(), at( Output5V)).value(), 4.90);
}

TEST_F( MeasureEngineFixture, InjectedSequenceThrowsRatherThanRepeatingItsLastValue)
{
    Measure.inject( "Output5V", std::vector{ 5.02_V });

    EXPECT_DOUBLE_EQ( Measure( dmm1.voltage(), at( Output5V)).value(), 5.02);
    EXPECT_THROW( (void)Measure( dmm1.voltage(), at( Output5V)), std::runtime_error);
}

//
// The case the whole seam exists for: the caller supplies the algorithm. This
// one ramps until it walks out of tolerance, which is what a "repeat until it
// fails" run needs to be testable without hardware.
//
namespace
{
    auto rampingRail( Voltage from, Voltage step) -> std::generator<Voltage>
    {
        for( auto value = from; ; value = Voltage{ value.value() + step.value() })
        {
            co_yield value;
        }
    }
} // namespace

TEST_F( MeasureEngineFixture, InjectServesAGeneratorAndAnInfiniteOneNeverRunsOut)
{
    Measure.inject( "Output5V", rampingRail( 5.00_V, 0.01_V));

    EXPECT_DOUBLE_EQ( Measure( dmm1.voltage(), at( Output5V)).value(), 5.00);
    EXPECT_DOUBLE_EQ( Measure( dmm1.voltage(), at( Output5V)).value(), 5.01);
    EXPECT_DOUBLE_EQ( Measure( dmm1.voltage(), at( Output5V)).value(), 5.02);

    // Far past where any finite list would have ended.
    for( int i = 0; i < 500; ++i)
    {
        EXPECT_NO_THROW( (void)Measure( dmm1.voltage(), at( Output5V)));
    }
}

TEST_F( MeasureEngineFixture, UseLiveRestoresRealRoutingAfterAnInject)
{
    Measure.inject( "Output5V", Voltage{ 5.02 });
    Measure.useLive();
    dmm1.setSimulatedVoltage( 6.0_V);

    const auto value = Measure( dmm1.voltage(), at( Output5V));

    EXPECT_DOUBLE_EQ( value.value(), 6.0);
}

//
// useLive() is the full inverse of inject(), not just the routing half of it.
//
// This is the shape of a real bug rather than a hypothetical: every script test
// fixture calls useLive() in TearDown to undo its injections, and while it only
// switched the session back, each test inherited every point the tests before it
// had programmed. suite/tests/test_supply_rail_script.cpp's
// "ThrowsWhenAPointIsMissing" passed only because gtest_discover_tests gives each
// test its own process -- run in one process it found "Output3V3" still armed
// from an earlier test and quietly measured it instead of throwing.
//
// The last line is the assertion that matters. A point this caller never
// programmed must produce ScriptedSession's "nothing programmed for point" error,
// because a canned value surviving from an earlier caller is indistinguishable
// from a real reading once it reaches a criterion.
//
TEST_F( MeasureEngineFixture, UseLiveDiscardsProgrammingSoALaterInjectStartsClean)
{
    Measure.inject( "Output5V",  Voltage{ 5.02 });
    Measure.inject( "Output3V3", Voltage{ 3.29 });

    Measure.useLive();

    // A fresh injection for one point only -- the other must not still be armed.
    Measure.inject( "Output5V", Voltage{ 5.01 });

    EXPECT_DOUBLE_EQ( Measure( dmm1.voltage(), at( Output5V)).value(), 5.01);

    EXPECT_THROW( (void)Measure( dmm1.voltage(), at( Output3V3)), std::runtime_error);
}

TEST_F( MeasureEngineFixture, ThrowsWhenTheRequestedInstrumentIsNotWiredToThatPin)
{
    mock::Instrument dmm2{ mock::InstrumentId::Dmm2 }; // no wire added for Dmm2

    EXPECT_THROW( (void)Measure( dmm2.voltage(), at( Output5V)), std::runtime_error);
}

TEST_F( MeasureEngineFixture, RecordingCapturesEachFetchThenLoadReplaysItInOrder)
{
    dmm1.setSimulatedVoltage( 4.98_V);
    Measure.startRecording();
    (void)Measure( dmm1.voltage(), at( Output5V));

    dmm1.setSimulatedVoltage( 5.02_V);
    (void)Measure( dmm1.voltage(), at( Output5V));
    Measure.stopRecording();

    const auto path = std::filesystem::temp_directory_path() / "thorium_core_measure_test_recording.tsv";

    {
        std::ofstream out( path);
        Measure.dump( out);
    }

    core::MeasureEngine<mock::Fabric, mock::InstrumentWiring, mock::ConnectorWiring> playback{ fabric, instrumentWiring, connectorWiring };
    playback.load( path.string());

    const auto first  = playback( dmm1.voltage(), at( Output5V));
    const auto second = playback( dmm1.voltage(), at( Output5V));

    EXPECT_DOUBLE_EQ( first.value(),  4.98);
    EXPECT_DOUBLE_EQ( second.value(), 5.02);

    EXPECT_THROW( (void)playback( dmm1.voltage(), at( Output5V)), std::runtime_error);

    std::remove( path.string().c_str());
}
