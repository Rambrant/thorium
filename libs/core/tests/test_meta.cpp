#include "core/meta.hpp"

#include <gtest/gtest.h>

namespace
{
    //
    // A throwaway enum purely for exercising the generic mechanism itself,
    // independent of any real project enum -- QuantityKind/InstrumentId/
    // SwitchDeviceKind's own tests (core/tests/test_quantity_kind.cpp,
    // hal/tests/test_instrument.cpp, hal/tests/test_switch_fabric.cpp)
    // cover this same code applied to the real ones.
    //
    enum class Color
    {
        Red,
        Green,
        Blue
    };
} // namespace

static_assert( core::meta::to_string( Color::Red)   == "Red");
static_assert( core::meta::to_string( Color::Green) == "Green");
static_assert( core::meta::to_string( Color::Blue)  == "Blue");

static_assert( core::meta::fromString<Color>( "Green") == Color::Green);
static_assert( ! core::meta::fromString<Color>( "Purple").has_value());

static_assert( core::meta::to_string_upper( Color::Red)   == "RED");
static_assert( core::meta::to_string_upper( Color::Green) == "GREEN");
static_assert( core::meta::to_string_upper( Color::Blue)  == "BLUE");

TEST( CoreMeta, ToStringNamesEveryEnumerator)
{
    EXPECT_EQ( core::meta::to_string( Color::Red),   "Red");
    EXPECT_EQ( core::meta::to_string( Color::Green), "Green");
    EXPECT_EQ( core::meta::to_string( Color::Blue),  "Blue");
}

TEST( CoreMeta, ToStringOfAnOutOfRangeValueIsUnknown)
{
    EXPECT_EQ( core::meta::to_string( static_cast<Color>( 99)), "Unknown");
}

TEST( CoreMeta, FromStringRoundTripsThroughToString)
{
    for( auto color : { Color::Red, Color::Green, Color::Blue })
    {
        EXPECT_EQ( core::meta::fromString<Color>( core::meta::to_string( color)), color);
    }
}

TEST( CoreMeta, FromStringOfAnUnknownNameIsNullopt)
{
    EXPECT_FALSE( core::meta::fromString<Color>( "Purple").has_value());
}

TEST( CoreMeta, ToStringUpperUppercasesEveryEnumerator)
{
    EXPECT_EQ( core::meta::to_string_upper( Color::Red),   "RED");
    EXPECT_EQ( core::meta::to_string_upper( Color::Green), "GREEN");
    EXPECT_EQ( core::meta::to_string_upper( Color::Blue),  "BLUE");
}

TEST( CoreMeta, ToStringUpperOfAnOutOfRangeValueIsUnknown)
{
    EXPECT_EQ( core::meta::to_string_upper( static_cast<Color>( 99)), "UNKNOWN");
}
