#include "core/measure.hpp"

#include <filesystem>
#include <fstream>
#include <optional>
#include <vector>

#include <gtest/gtest.h>

using namespace core::literals;
using namespace core::quantities;

//
// A minimal Fabric/RouteTable/Adapter/Instrument stand-in, satisfying
// exactly what core::MeasureEngine needs and nothing more -- see
// core/measure.hpp's own comment for the exact shape each is expected to
// have. hal::SwitchFabric/hal::RouteTable/dut::Adapter (see
// dut/measure.cpp) are just one concrete set of types happening to
// satisfy this same shape; core::MeasureEngine never sees them.
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

    using SwitchPath = std::vector<int>;

    class Fabric
    {
        public:
            auto route( const SwitchPath & path) -> void { mLastRouted = path; }

            [[nodiscard]] auto lastRouted() const -> const SwitchPath & { return mLastRouted; }

        private:
            SwitchPath mLastRouted;
    };

    struct RouteEntry
    {
        Location            location;
        InstrumentId        instrument;
        core::QuantityKind  kind;
        SwitchPath          path;
    };

    class RouteTable
    {
        public:
            auto addRoute( Location location, InstrumentId instrument, core::QuantityKind kind, SwitchPath path) -> void
            {
                mEntries.push_back( RouteEntry{ location, instrument, kind, std::move( path) });
            }

            [[nodiscard]] auto find( Location location, InstrumentId instrument, core::QuantityKind kind) const -> const SwitchPath &
            {
                for( const auto & entry : mEntries)
                {
                    if( entry.location == location && entry.instrument == instrument && entry.kind == kind)
                    {
                        return entry.path;
                    }
                }

                throw std::runtime_error( "mock::RouteTable: no route wired that way");
            }

        private:
            std::vector<RouteEntry> mEntries;
    };

    struct AdapterPoint
    {
        std::string_view    name;
        Location             location;
        core::QuantityKind  kind;
    };

    class Adapter
    {
        public:
            Adapter( std::string_view name, std::vector<AdapterPoint> points) : mName( name), mPoints( std::move( points)) {}

            [[nodiscard]] auto name() const -> std::string_view { return mName; }

            [[nodiscard]] auto find( std::string_view pointName) const -> std::optional<AdapterPoint>
            {
                for( const auto & point : mPoints)
                {
                    if( point.name == pointName)
                    {
                        return point;
                    }
                }

                return std::nullopt;
            }

        private:
            std::string_view          mName;
            std::vector<AdapterPoint> mPoints;
    };

    class Instrument
    {
        public:
            explicit Instrument( InstrumentId id) : mId( id) {}

            [[nodiscard]] auto id() const -> InstrumentId { return mId; }

            [[nodiscard]] auto voltage() -> core::Port<Voltage, Instrument> { return core::Port<Voltage, Instrument>{ *this }; }

            auto setSimulatedVoltage( Voltage v) -> void { mVoltage = v; }

            template<core::quantities::QuantityType Q>
            [[nodiscard]] auto rawMeasure() -> Q { return mVoltage; }

        private:
            InstrumentId mId;
            Voltage      mVoltage{};
    };
} // namespace mock

namespace
{
    const mock::Location   kLoc{ 3 };
    const mock::SwitchPath kPath{ 14, 3 };

    auto makeAdapter() -> mock::Adapter
    {
        return mock::Adapter{ "MockAdapter", { mock::AdapterPoint{ "5VOutput", kLoc, core::QuantityKind::Voltage } } };
    }

    auto makeRoutes() -> mock::RouteTable
    {
        mock::RouteTable table;
        table.addRoute( kLoc, mock::InstrumentId::Dmm1, core::QuantityKind::Voltage, kPath);
        return table;
    }

    struct MeasureEngineFixture : ::testing::Test
    {
        mock::Adapter    adapter = makeAdapter();
        mock::RouteTable routes  = makeRoutes();
        mock::Fabric     fabric;
        mock::Instrument dmm1{ mock::InstrumentId::Dmm1 };

        core::MeasureEngine<mock::Fabric, mock::RouteTable, mock::Adapter> Measure{ fabric, routes, adapter };
    };
} // namespace

TEST_F( MeasureEngineFixture, LiveByDefaultRoutesTheFabricAndReturnsTheInstrumentReading)
{
    dmm1.setSimulatedVoltage( 5.02_V);

    const auto value = Measure( dmm1.voltage(), "5VOutput");

    EXPECT_DOUBLE_EQ( value.value(), 5.02);
    EXPECT_EQ( fabric.lastRouted(), kPath);
}

TEST_F( MeasureEngineFixture, InjectBypassesTheFabricEntirely)
{
    Measure.inject( "5VOutput", Voltage{ 5.02 });
    dmm1.setSimulatedVoltage( 999.0_V); // would fail the test if this were ever read

    const auto value = Measure( dmm1.voltage(), "5VOutput");

    EXPECT_DOUBLE_EQ( value.value(), 5.02);
    EXPECT_TRUE( fabric.lastRouted().empty());
}

TEST_F( MeasureEngineFixture, UseLiveRestoresRealRoutingAfterAnInject)
{
    Measure.inject( "5VOutput", Voltage{ 5.02 });
    Measure.useLive();
    dmm1.setSimulatedVoltage( 6.0_V);

    const auto value = Measure( dmm1.voltage(), "5VOutput");

    EXPECT_DOUBLE_EQ( value.value(), 6.0);
}

TEST_F( MeasureEngineFixture, ThrowsWhenThePointNameIsUnknown)
{
    EXPECT_THROW( (void)Measure( dmm1.voltage(), "NoSuchPoint"), std::runtime_error);
}

TEST_F( MeasureEngineFixture, ThrowsWhenTheRequestedInstrumentIsNotWiredToThatPin)
{
    mock::Instrument dmm2{ mock::InstrumentId::Dmm2 }; // no route added for Dmm2

    EXPECT_THROW( (void)Measure( dmm2.voltage(), "5VOutput"), std::runtime_error);
}

TEST_F( MeasureEngineFixture, RecordingCapturesEachFetchThenLoadReplaysItInOrder)
{
    dmm1.setSimulatedVoltage( 4.98_V);
    Measure.startRecording();
    (void)Measure( dmm1.voltage(), "5VOutput");

    dmm1.setSimulatedVoltage( 5.02_V);
    (void)Measure( dmm1.voltage(), "5VOutput");
    Measure.stopRecording();

    const auto path = std::filesystem::temp_directory_path() / "thorium_core_measure_test_recording.tsv";

    {
        std::ofstream out( path);
        Measure.dump( out);
    }

    core::MeasureEngine<mock::Fabric, mock::RouteTable, mock::Adapter> playback{ fabric, routes, adapter };
    playback.load( path.string());

    const auto first  = playback( dmm1.voltage(), "5VOutput");
    const auto second = playback( dmm1.voltage(), "5VOutput");

    EXPECT_DOUBLE_EQ( first.value(),  4.98);
    EXPECT_DOUBLE_EQ( second.value(), 5.02);

    EXPECT_THROW( (void)playback( dmm1.voltage(), "5VOutput"), std::runtime_error);

    std::remove( path.string().c_str());
}
