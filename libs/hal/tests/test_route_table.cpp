#include "hal/route_table.hpp"

#include <gtest/gtest.h>

namespace
{
    const hal::VpcLocation      kA1Pin3{ hal::VpcRack::A, 1, 3 };
    const hal::SwitchElementId  kMatrix2Ch14{ hal::SwitchDeviceKind::Matrix, "Matrix2", 14 };
    const hal::SwitchElementId  kMux1Ch3{ hal::SwitchDeviceKind::Mux, "Mux1", 3 };
    const hal::SwitchElementId  kMux3Ch1{ hal::SwitchDeviceKind::Mux, "Mux3", 1 };
} // namespace

TEST( HalRouteTable, FindReturnsThePathForAWiredRoute)
{
    hal::RouteTable table;
    table.addRoute( kA1Pin3, hal::InstrumentId::Dmm1, core::QuantityKind::Voltage, { kMatrix2Ch14, kMux1Ch3 });

    const auto & path = table.find( kA1Pin3, hal::InstrumentId::Dmm1, core::QuantityKind::Voltage);

    ASSERT_EQ( path.size(), 2u);
    EXPECT_EQ( path[0], kMatrix2Ch14);
    EXPECT_EQ( path[1], kMux1Ch3);
}

TEST( HalRouteTable, TwoInstrumentsCanReachTheSamePinByDifferentPaths)
{
    hal::RouteTable table;
    table.addRoute( kA1Pin3, hal::InstrumentId::Dmm1, core::QuantityKind::Voltage, { kMatrix2Ch14, kMux1Ch3 });
    table.addRoute( kA1Pin3, hal::InstrumentId::Osc1, core::QuantityKind::Voltage, { kMatrix2Ch14, kMux3Ch1 });

    const auto & dmmPath = table.find( kA1Pin3, hal::InstrumentId::Dmm1, core::QuantityKind::Voltage);
    const auto & oscPath = table.find( kA1Pin3, hal::InstrumentId::Osc1, core::QuantityKind::Voltage);

    EXPECT_NE( dmmPath, oscPath);
}

TEST( HalRouteTable, FindThrowsWhenNothingIsWiredThatWay)
{
    hal::RouteTable table;
    table.addRoute( kA1Pin3, hal::InstrumentId::Dmm1, core::QuantityKind::Voltage, { kMatrix2Ch14 });

    EXPECT_THROW(
        (void)table.find( kA1Pin3, hal::InstrumentId::Dmm2, core::QuantityKind::Voltage),
        std::runtime_error);
}
