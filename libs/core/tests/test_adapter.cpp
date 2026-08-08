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

TEST( CoreAdapterPointTag, CarriesItsLocationAsACompileTimeValue)
{
    constexpr core::AdapterPointTag<MockLocation{ 3 }> point{ "Output5V", "5Vdc supply port" };

    static_assert( point.LocationValue == MockLocation{ 3 });

    EXPECT_EQ( point.Name, "Output5V");
    EXPECT_EQ( point.Description, "5Vdc supply port");
}

TEST( CoreAdapterPointTag, DifferentLocationsAreDifferentTypes)
{
    using PointA = core::AdapterPointTag<MockLocation{ 3 }>;
    using PointB = core::AdapterPointTag<MockLocation{ 4 }>;

    static_assert( !std::is_same_v<PointA, PointB>);
}

//
// A point is a place, so two points at the same location are the same type
// whatever is measured at either -- the quantity lives on the instrument port
// (see core::AdapterPointTag). This used to be the opposite assertion.
//
TEST( CoreAdapterPointTag, SameLocationIsTheSameTypeWhateverIsMeasuredThere)
{
    using PointAsSeenByAVoltmeter = core::AdapterPointTag<MockLocation{ 3 }>;
    using PointAsSeenByAnAmmeter  = core::AdapterPointTag<MockLocation{ 3 }>;

    static_assert( std::is_same_v<PointAsSeenByAVoltmeter, PointAsSeenByAnAmmeter>);
}
