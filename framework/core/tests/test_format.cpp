#include "core/format.hpp"

#include <gtest/gtest.h>

#include "core/quantity.hpp"

// Explicit: core/format.hpp no longer includes this. Reporting a value is a
// compile-time job; the type-erased side is a separate concern.
#include "core/quantity_kind.hpp"

using namespace core::literals;
using namespace core::quantities;

TEST( CoreFormat, NumberIsTrimmedToSixSignificantDigits)
{
    //
    // The reason formatNumber exists at all: a rail read as 5.021 must not
    // appear in a report as 5.0209999999999999.
    //
    EXPECT_EQ( core::formatNumber( 5.021),        "5.021");
    EXPECT_EQ( core::formatNumber( 5.0),          "5");
    EXPECT_EQ( core::formatNumber( 3.2999999999), "3.3");
    EXPECT_EQ( core::formatNumber( -0.05),        "-0.05");
}

//
// Every unit states its own symbol, on its unit tag -- these are compile-time
// constants, not a table this file has to be kept in step with.
//
TEST( CoreFormat, EveryUnitCarriesItsOwnSymbol)
{
    static_assert( Voltage::symbol()       == "V");
    static_assert( Current::symbol()       == "A");
    static_assert( Power::symbol()         == "W");
    static_assert( ApparentPower::symbol() == "VA");
    static_assert( Resistance::symbol()    == "Ohm");
    static_assert( Time::symbol()          == "s");
    static_assert( Decibel::symbol()       == "dB");
    static_assert( Frequency::symbol()     == "Hz");
    static_assert( ReactivePower::symbol() == "var");
}

//
// PowerFactor is dimensionless, so it has no symbol -- and that has to stay a
// deliberate empty string rather than drifting into a placeholder, because
// describeValue below relies on it to decide whether to append a unit at all.
//
TEST( CoreFormat, PowerFactorHasNoUnitSymbol)
{
    static_assert( PowerFactor::symbol().empty());
    EXPECT_EQ( core::describeValue( PowerFactor{ 0.95}), "0.95");
}

TEST( CoreFormat, QuantityIsDescribedWithItsUnit)
{
    EXPECT_EQ( core::describeValue( 5.021_V), "5.021 V");
    EXPECT_EQ( core::describeValue( 7.0_A),   "7 A");
    EXPECT_EQ( core::describeValue( 50.0_Hz), "50 Hz");
}

//
// An integer register readback is authored in hex in the criteria file (see
// dut/criteria_production.inc) but reads as decimal in any default formatting,
// so the log gives both -- otherwise checking a logged value against its
// criterion means converting by hand.
//
TEST( CoreFormat, IntegralIsDescribedInBothBases)
{
    EXPECT_EQ( core::describeValue( 0xF5u), "245 (0xF5)");
    EXPECT_EQ( core::describeValue( 0),     "0 (0x0)");
}

TEST( CoreFormat, NegativeIntegralHexIsItsTwosComplementPattern)
{
    // Not an implementation-defined conversion of a negative to uint64.
    EXPECT_EQ( core::describeValue( static_cast<std::int8_t>( -1)), "-1 (0xFF)");
}

TEST( CoreFormat, NumericAndUnitAreReportedSeparatelyFromText)
{
    //
    // The machine log compares numbers against limits, so it needs the value
    // apart from its formatting -- see JournalRecord::Numeric.
    //
    EXPECT_EQ( core::numericOf( 5.021_V).value(), 5.021);
    EXPECT_EQ( core::unitOf<Voltage>(),            "V");

    EXPECT_EQ( core::numericOf( 0xF5u).value(), 245.0);
    EXPECT_TRUE( core::unitOf<unsigned>().empty());
}

//
// bool has no meaningful number: "true" is not 1.0 in a test report, and a
// consumer trending numericValue must not be handed one.
//
TEST( CoreFormat, BoolHasTextButNoNumber)
{
    EXPECT_EQ( core::describeValue( true), "true");
    EXPECT_FALSE( core::numericOf( true).has_value());
}

//
// A value that arrives type-erased (out of a session, or a recording) is
// unwrapped to its concrete type before anything formats it -- see
// core::MeasureEngine. So this is the same one code path, reached from the
// runtime side.
//
TEST( CoreFormat, AnUnwrappedVariantFormatsTheSameWay)
{
    const core::QuantityVariant erased = 1.5_mA;

    EXPECT_EQ( core::describeValue( core::asQuantity<Current>( erased)), "0.0015 A");
}
