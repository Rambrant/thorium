#include "core/quantities/quantity_kind.hpp"
#include "core/quantities/format.hpp"

#include <gtest/gtest.h>

#include <meta>
#include <utility>
#include <variant>

using namespace core::literals;
using namespace core::quantities;

TEST( CoreQuantityKind, QuantityKindOfMatchesTheAliasUsed)
{
    EXPECT_EQ( core::quantityKindOf<Voltage>(), core::QuantityKind::Voltage);
    EXPECT_EQ( core::quantityKindOf<Current>(), core::QuantityKind::Current);
    EXPECT_EQ( core::quantityKindOf<Power>(),   core::QuantityKind::Power);
}

TEST( CoreQuantityKind, QuantityForIsTheInverseOfQuantityKindOf)
{
    static_assert( std::is_same_v<core::QuantityFor<core::QuantityKind::Voltage>, Voltage>);
    static_assert( std::is_same_v<core::QuantityFor<core::QuantityKind::Current>, Current>);
    static_assert( std::is_same_v<core::QuantityFor<core::quantityKindOf<Power>()>, Power>);
}

TEST( CoreQuantityKind, ToStringRoundTripsThroughFromString)
{
    for( auto kind : { core::QuantityKind::Voltage, core::QuantityKind::Current, core::QuantityKind::Power,
                       core::QuantityKind::ApparentPower, core::QuantityKind::Resistance, core::QuantityKind::Time,
                       core::QuantityKind::Decibel, core::QuantityKind::Frequency, core::QuantityKind::PowerFactor,
                       core::QuantityKind::ReactivePower})
    {
        EXPECT_EQ( core::quantityKindFromString( core::to_string( kind)), kind);
    }
}

TEST( CoreQuantityKind, AsQuantityUnwrapsAMatchingVariant)
{
    core::QuantityVariant value = 12.0_V;

    EXPECT_DOUBLE_EQ( core::asQuantity<Voltage>( value).value(), 12.0);
}

TEST( CoreQuantityKind, AsQuantityThrowsOnAMismatchedVariant)
{
    core::QuantityVariant value = 12.0_V;

    EXPECT_THROW( (void)core::asQuantity<Current>( value), std::runtime_error);
}

TEST( CoreQuantityKind, RawValueAndFromKindRoundTrip)
{
    core::QuantityVariant value = 3.3_V;

    const auto rebuilt = core::quantityVariantFromKind( core::QuantityKind::Voltage, core::rawValue( value));

    EXPECT_DOUBLE_EQ( core::asQuantity<Voltage>( rebuilt).value(), 3.3);
}

//
// QuantityVariant is generated from QuantityKind's enumerators (see
// core/quantities/quantity_kind.hpp), so "the variant has one alternative per kind, in the
// same order" is true by construction rather than asserted ten times. These
// pin the property down anyway -- not to catch a forgotten list, which can no
// longer exist, but because every kind-to-type conversion in the framework
// (QuantityFor, asQuantity, quantityVariantFromKind, unitSymbol) is built on
// index() == the enumerator's value, and a change to how the variant is
// generated must not be able to break that quietly.
//
TEST( CoreQuantityKind, EveryKindHasExactlyOneVariantAlternativeInOrder)
{
    static_assert( std::variant_size_v<core::QuantityVariant> ==
                   std::meta::enumerators_of( ^^core::QuantityKind).size());

    // Spot-checked at both ends and either side of a pair that is easy to swap
    // by hand (Power is W, ApparentPower is VA).
    static_assert( std::is_same_v<core::QuantityFor<core::QuantityKind::Voltage>,       Voltage>);
    static_assert( std::is_same_v<core::QuantityFor<core::QuantityKind::Power>,         Power>);
    static_assert( std::is_same_v<core::QuantityFor<core::QuantityKind::ApparentPower>, ApparentPower>);
    static_assert( std::is_same_v<core::QuantityFor<core::QuantityKind::ReactivePower>, ReactivePower>);

    // The round trip, for every kind at once.
    [] <std::size_t... I> ( std::index_sequence<I...>)
    {
        ( ( void) [] < std::size_t N> ()
        {
            constexpr auto kind = static_cast<core::QuantityKind>( N);

            static_assert( core::quantityKindOf<core::QuantityFor<kind>>() == kind);
        }.template operator()<I>(), ...);
    }( std::make_index_sequence<std::variant_size_v<core::QuantityVariant>>{});
}

//
// A unit's symbol is declared once, on its unit tag, and is a compile-time
// constant everywhere it is asked for -- there is no runtime lookup left to
// disagree with it. Reporting a value never needs QuantityKind at all; see
// core/quantities/format.hpp.
//
TEST( CoreQuantityKind, UnitSymbolIsACompileTimeConstantOnTheType)
{
    static_assert( Voltage::symbol()    == "V");
    static_assert( Resistance::symbol() == "Ohm");
    static_assert( Time::symbol()       == "s");

    // PowerFactor is dimensionless -- an empty symbol is the correct value.
    static_assert( PowerFactor::symbol().empty());

    // Reachable through the kind-to-type mapping too, since that yields the
    // same concrete type.
    static_assert( core::QuantityFor<core::QuantityKind::Resistance>::symbol() == Resistance::symbol());
    static_assert( core::unitOf<Time>() == Time::symbol());
}
