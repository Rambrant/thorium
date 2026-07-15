#include "dut/measure_engine.hpp"

#include <filesystem>
#include <fstream>

#include <gtest/gtest.h>

using namespace core::literals;
using namespace core::quantities;

namespace
{
    const hal::VpcLocation     kA1Pin3{ hal::VpcRack::A, 1, 3 };
    const hal::SwitchElementId kMatrix2Ch14{ hal::SwitchDeviceKind::Matrix, "Matrix2", 14 };
    const hal::SwitchElementId kMux1Ch3{ hal::SwitchDeviceKind::Mux, "Mux1", 3 };

    auto makeAdapter() -> dut::Adapter
    {
        return dut::Adapter{ "DeviceX_StdAdapter", "Device X on standard adapter",
            {
                dut::AdapterPoint{ "5VOutput", kA1Pin3, core::QuantityKind::Voltage, "5Vdc supply port" },
            }};
    }

    auto makeRoutes() -> hal::RouteTable
    {
        hal::RouteTable table;
        table.addRoute( kA1Pin3, hal::InstrumentId::Dmm1, core::QuantityKind::Voltage, { kMatrix2Ch14, kMux1Ch3 });
        return table;
    }

    struct MeasureEngineFixture : ::testing::Test
    {
        dut::Adapter      adapter = makeAdapter();
        hal::RouteTable   routes  = makeRoutes();
        hal::SwitchFabric fabric;
        hal::Dmm          dmm1{ hal::InstrumentId::Dmm1 };
        dut::MeasureEngine Measure{ fabric, routes, adapter };
    };
} // namespace

TEST_F( MeasureEngineFixture, LiveByDefaultRoutesTheFabricAndReturnsTheInstrumentReading)
{
    dmm1.setSimulatedVoltage( 5.02_V);

    const auto value = Measure( dmm1.voltage(), "5VOutput");

    EXPECT_DOUBLE_EQ( value.value(), 5.02);
    EXPECT_TRUE( fabric.isClosed( kMatrix2Ch14));
    EXPECT_TRUE( fabric.isClosed( kMux1Ch3));
}

TEST_F( MeasureEngineFixture, InjectBypassesTheFabricEntirely)
{
    Measure.inject( "5VOutput", Voltage{ 5.02 });
    dmm1.setSimulatedVoltage( 999.0_V); // would fail the test if this were ever read

    const auto value = Measure( dmm1.voltage(), "5VOutput");

    EXPECT_DOUBLE_EQ( value.value(), 5.02);
    EXPECT_FALSE( fabric.isClosed( kMatrix2Ch14));
    EXPECT_FALSE( fabric.isClosed( kMux1Ch3));
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

TEST_F( MeasureEngineFixture, ThrowsWhenTheQuantityDoesNotMatchTheAdaptersDeclaredKind)
{
    // 5VOutput is declared Voltage; asking for Current at that pin is a
    // wiring mismatch even though Dmm has a Current port.
    EXPECT_THROW( (void)Measure( dmm1.current(), "5VOutput"), std::runtime_error);
}

TEST_F( MeasureEngineFixture, ThrowsWhenTheRequestedInstrumentIsNotWiredToThatPin)
{
    hal::Dmm dmm2{ hal::InstrumentId::Dmm2 }; // no route added for Dmm2

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

    const auto path = std::filesystem::temp_directory_path() / "thorium_measure_engine_test_recording.tsv";

    {
        std::ofstream out( path);
        Measure.dump( out);
    }

    dut::MeasureEngine playback{ fabric, routes, adapter };
    playback.load( path.string());

    const auto first  = playback( dmm1.voltage(), "5VOutput");
    const auto second = playback( dmm1.voltage(), "5VOutput");

    EXPECT_DOUBLE_EQ( first.value(),  4.98);
    EXPECT_DOUBLE_EQ( second.value(), 5.02);

    EXPECT_THROW( (void)playback( dmm1.voltage(), "5VOutput"), std::runtime_error);

    std::remove( path.string().c_str());
}
