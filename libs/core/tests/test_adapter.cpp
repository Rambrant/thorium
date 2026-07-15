#include "core/adapter.hpp"

#include <gtest/gtest.h>

namespace
{
    //
    // A minimal location type -- just something comparable -- standing in
    // for hal::VpcLocation, demonstrating core::Adapter has no idea what a
    // real physical coordinate looks like.
    //
    struct MockLocation
    {
        int value;
        friend constexpr auto operator==( MockLocation, MockLocation) -> bool = default;
    };

    auto makeAdapter() -> core::Adapter<MockLocation>
    {
        return core::Adapter<MockLocation>{ "MockAdapter", "A mock DUT profile",
            {
                core::AdapterPoint<MockLocation>{ "5VOutput", MockLocation{ 3 }, core::QuantityKind::Voltage, "5Vdc supply port" },
                core::AdapterPoint<MockLocation>{ "3V3Output", MockLocation{ 6 }, core::QuantityKind::Voltage, "3.3Vdc supply port" },
            }};
    }
} // namespace

TEST( CoreAdapter, FindReturnsAKnownPoint)
{
    const auto adapter = makeAdapter();

    const auto point = adapter.find( "5VOutput");

    ASSERT_TRUE( point.has_value());
    EXPECT_EQ( point->location, MockLocation{ 3 });
    EXPECT_EQ( point->kind, core::QuantityKind::Voltage);
}

TEST( CoreAdapter, FindReturnsNulloptForAnUnknownPoint)
{
    const auto adapter = makeAdapter();

    EXPECT_FALSE( adapter.find( "NoSuchPoint").has_value());
}

TEST( CoreAdapter, NameAndDescriptionAreAccessible)
{
    const auto adapter = makeAdapter();

    EXPECT_EQ( adapter.name(), "MockAdapter");
    EXPECT_EQ( adapter.description(), "A mock DUT profile");
}
