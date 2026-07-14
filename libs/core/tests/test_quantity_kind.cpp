#include "core/quantity_kind.hpp"

#include <gtest/gtest.h>

using namespace core::literals;
using namespace core::quantities;

TEST( CoreQuantityKind, QuantityKindOfMatchesTheAliasUsed)
{
    EXPECT_EQ( core::quantityKindOf<Voltage>(), core::QuantityKind::Voltage);
    EXPECT_EQ( core::quantityKindOf<Current>(), core::QuantityKind::Current);
    EXPECT_EQ( core::quantityKindOf<Power>(),   core::QuantityKind::Power);
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
