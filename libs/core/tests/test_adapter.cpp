#include "core/adapter.hpp"

#include <gtest/gtest.h>

namespace
{
    //
    // A minimal location type -- just something comparable -- standing in
    // for hal::VpcLocation, demonstrating core::AdapterPointTag has no idea
    // what a real physical coordinate looks like.
    //
    struct MockLocation
    {
        int value;
        friend constexpr auto operator==( MockLocation, MockLocation) -> bool = default;
    };
} // namespace

TEST( CoreAdapterPointTag, CarriesLocationAndKindAsCompileTimeValues)
{
    constexpr core::AdapterPointTag<MockLocation{ 3 }, core::QuantityKind::Voltage> point{ "Output5V", "5Vdc supply port" };

    static_assert( point.LocationValue == MockLocation{ 3 });
    static_assert( point.KindValue == core::QuantityKind::Voltage);

    EXPECT_EQ( point.Name, "Output5V");
    EXPECT_EQ( point.Description, "5Vdc supply port");
}

TEST( CoreAdapterPointTag, DifferentLocationsAreDifferentTypes)
{
    using PointA = core::AdapterPointTag<MockLocation{ 3 }, core::QuantityKind::Voltage>;
    using PointB = core::AdapterPointTag<MockLocation{ 4 }, core::QuantityKind::Voltage>;

    static_assert( !std::is_same_v<PointA, PointB>);
}

TEST( CoreAdapterPointTag, DifferentKindsAreDifferentTypes)
{
    using VoltagePoint = core::AdapterPointTag<MockLocation{ 3 }, core::QuantityKind::Voltage>;
    using CurrentPoint = core::AdapterPointTag<MockLocation{ 3 }, core::QuantityKind::Current>;

    static_assert( !std::is_same_v<VoltagePoint, CurrentPoint>);
}
