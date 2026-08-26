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

//
// values<Enum> -- the enumerator list itself, reflected rather than repeated
// at each call site. See core::meta::values' own comment for why a braced
// list is a worse duplicate than the switch to_string() replaced.
//
namespace
{
    enum class Conductor
    {
        A,
        B,
        C
    };

    // Non-default values, and out of numeric order: values<> must report what
    // the enum declares, in declaration order, not a 0..N-1 range.
    enum class Baud
    {
        Fast   = 115200,
        Medium = 9600,
        Slow   = 300
    };
} // namespace

TEST( CoreMetaValues, ReportsEveryEnumeratorInDeclarationOrder)
{
    static_assert( core::meta::values<Conductor>.size() == 3);

    EXPECT_EQ( core::meta::values<Conductor>[ 0], Conductor::A);
    EXPECT_EQ( core::meta::values<Conductor>[ 1], Conductor::B);
    EXPECT_EQ( core::meta::values<Conductor>[ 2], Conductor::C);
}

TEST( CoreMetaValues, CarriesTheDeclaredValuesNotAnIndexRange)
{
    ASSERT_EQ( core::meta::values<Baud>.size(), 3u);

    EXPECT_EQ( core::meta::values<Baud>[ 0], Baud::Fast);
    EXPECT_EQ( static_cast<int>( core::meta::values<Baud>[ 0]), 115200);
    EXPECT_EQ( static_cast<int>( core::meta::values<Baud>[ 2]),    300);
}

TEST( CoreMetaValues, RoundTripsThroughToStringForEveryValue)
{
    // The two halves of core/meta.hpp against each other: every value the
    // enum declares stringifies to a name that maps back to it.
    for( const auto value : core::meta::values<Baud>)
    {
        EXPECT_EQ( core::meta::fromString<Baud>( core::meta::to_string( value)), value);
    }
}
