#include "hal/adapter.hpp"

#include <gtest/gtest.h>

namespace
{
    auto makeAdapter() -> hal::Adapter
    {
        return hal::Adapter{ "DeviceX_StdAdapter", "Device X on standard adapter",
            {
                hal::AdapterPoint{ "5VOutput", hal::VpcLocation{ hal::VpcRack::A, 1, 3 }, core::QuantityKind::Voltage, "5Vdc supply port" },
                hal::AdapterPoint{ "3V3Output", hal::VpcLocation{ hal::VpcRack::A, 1, 6 }, core::QuantityKind::Voltage, "3.3Vdc supply port" },
            }};
    }
} // namespace

TEST( HalAdapter, FindReturnsAKnownPoint)
{
    const auto adapter = makeAdapter();

    const auto point = adapter.find( "5VOutput");

    ASSERT_TRUE( point.has_value());
    EXPECT_EQ( point->location, (hal::VpcLocation{ hal::VpcRack::A, 1, 3 }));
    EXPECT_EQ( point->kind, core::QuantityKind::Voltage);
}

TEST( HalAdapter, FindReturnsNulloptForAnUnknownPoint)
{
    const auto adapter = makeAdapter();

    EXPECT_FALSE( adapter.find( "NoSuchPoint").has_value());
}
