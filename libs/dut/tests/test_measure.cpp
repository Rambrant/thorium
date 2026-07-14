#include "dut/measure.hpp"

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

    struct MeasureFixture : ::testing::Test
    {
        dut::Adapter      adapter = makeAdapter();
        hal::RouteTable   routes  = makeRoutes();
        hal::SwitchFabric fabric;
        hal::Dmm          dmm1{ hal::InstrumentId::Dmm1 };
    };
} // namespace

TEST_F( MeasureFixture, LiveSessionRoutesTheFabricAndReturnsTheInstrumentReading)
{
    core::LiveSession session;
    dut::Bench        bench{ fabric, routes, adapter, session };

    dmm1.setSimulatedVoltage( 5.02_V);

    const auto value = dut::Measure( bench, dmm1.voltage(), "5VOutput");

    EXPECT_DOUBLE_EQ( value.value(), 5.02);
    EXPECT_TRUE( fabric.isClosed( kMatrix2Ch14));
    EXPECT_TRUE( fabric.isClosed( kMux1Ch3));
}

TEST_F( MeasureFixture, ScriptedSessionNeverTouchesTheFabric)
{
    core::ScriptedSession session;
    session.program( "5VOutput", Voltage{ 5.02 });
    dut::Bench bench{ fabric, routes, adapter, session };

    dmm1.setSimulatedVoltage( 999.0_V); // would fail the test if this were ever read

    const auto value = dut::Measure( bench, dmm1.voltage(), "5VOutput");

    EXPECT_DOUBLE_EQ( value.value(), 5.02);
    EXPECT_FALSE( fabric.isClosed( kMatrix2Ch14));
    EXPECT_FALSE( fabric.isClosed( kMux1Ch3));
}

TEST_F( MeasureFixture, ThrowsWhenThePointNameIsUnknown)
{
    core::LiveSession session;
    dut::Bench        bench{ fabric, routes, adapter, session };

    EXPECT_THROW(
        (void)dut::Measure( bench, dmm1.voltage(), "NoSuchPoint"),
        std::runtime_error);
}

TEST_F( MeasureFixture, ThrowsWhenTheQuantityDoesNotMatchTheAdaptersDeclaredKind)
{
    core::LiveSession session;
    dut::Bench        bench{ fabric, routes, adapter, session };

    // 5VOutput is declared Voltage; asking for Current at that pin is a wiring
    // mismatch even though Dmm has a Current port.
    EXPECT_THROW(
        (void)dut::Measure( bench, dmm1.current(), "5VOutput"),
        std::runtime_error);
}

TEST_F( MeasureFixture, ThrowsWhenTheRequestedInstrumentIsNotWiredToThatPin)
{
    core::LiveSession session;
    dut::Bench        bench{ fabric, routes, adapter, session };

    hal::Dmm dmm2{ hal::InstrumentId::Dmm2 }; // no route added for Dmm2

    EXPECT_THROW(
        (void)dut::Measure( bench, dmm2.voltage(), "5VOutput"),
        std::runtime_error);
}

TEST_F( MeasureFixture, RecordingSessionCapturesTheNameInstrumentAndValue)
{
    core::LiveSession       live;
    core::RecordingSession  recording( live);
    dut::Bench              bench{ fabric, routes, adapter, recording };

    dmm1.setSimulatedVoltage( 5.02_V);

    (void)dut::Measure( bench, dmm1.voltage(), "5VOutput");

    ASSERT_EQ( recording.samples().size(), 1u);
    EXPECT_EQ( recording.samples()[0].mPointName, "5VOutput");
    EXPECT_EQ( recording.samples()[0].mInstrumentId, "Dmm1");
    EXPECT_DOUBLE_EQ( core::rawValue( recording.samples()[0].mValue), 5.02);
}
