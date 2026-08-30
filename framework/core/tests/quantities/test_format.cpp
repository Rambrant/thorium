#include "core/quantities/format.hpp"

#include <gtest/gtest.h>

#include "core/quantities/quantity.hpp"

// Explicit: core/quantities/format.hpp no longer includes this. Reporting a value is a
// compile-time job; the type-erased side is a separate concern.
#include "core/quantities/quantity_kind.hpp"

using namespace core::literals;
using namespace core::quantities;

//
// "Does this unit tag say anything about prefixes" -- the same question
// core::describeValue asks, asked here of a named unit. A template rather than
// a bare requires-expression at each call site because a requires-expression
// over a concrete type is a hard error, not a substitution failure: written
// inline, `! requires { degC_Type::Prefixes; }` fails to compile instead of
// being false.
//
template<typename Unit>
constexpr bool declaresPrefixes = requires { Unit::Prefixes; };

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
    static_assert( Voltage::symbol()          == "V");
    static_assert( Current::symbol()          == "A");
    static_assert( Power::symbol()            == "W");
    static_assert( ApparentPower::symbol()    == "VA");
    static_assert( Resistance::symbol()       == "Ohm");
    static_assert( Time::symbol()             == "s");
    static_assert( Decibel::symbol()          == "dB");
    static_assert( Frequency::symbol()        == "Hz");
    static_assert( ReactivePower::symbol()    == "var");
    static_assert( Temperature::symbol()      == "degC");
    static_assert( TemperatureDelta::symbol() == "K");
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
// The point of the prefix rule: one unit reads legibly across its whole range,
// the way the instrument that measured it does. A fixed number of decimals
// cannot -- two of them is 10 mV of granularity on the 400 V input and nothing
// at all on the shunt drop.
//
TEST( CoreFormat, AQuantityIsScaledIntoAnSiPrefix)
{
    EXPECT_EQ( core::describeValue( 50.21_mV),  "50.21 mV");
    EXPECT_EQ( core::describeValue( 5.021_V),   "5.021 V");
    EXPECT_EQ( core::describeValue( 400.1_V),   "400.1 V");
    EXPECT_EQ( core::describeValue( 1.2_us),    "1.2 us");
    EXPECT_EQ( core::describeValue( 10.0_mOhm), "10 mOhm");
    EXPECT_EQ( core::describeValue( 400.0_Hz),  "400 Hz");
    EXPECT_EQ( core::describeValue( 2.4_kHz),   "2.4 kHz");
}

//
// Four significant digits, not formatNumber's six: the last two are below the
// resolution of every instrument on this rig, and reading them off a report is
// what prompted the prefix rule in the first place. Rounding, not truncation --
// the digit that goes is still accounted for.
//
TEST( CoreFormat, APrefixedQuantityCarriesFourSignificantDigits)
{
    EXPECT_EQ( core::describeValue( 5.0213456_V), "5.021 V");
    EXPECT_EQ( core::describeValue( 5.0216_V),    "5.022 V");
    EXPECT_EQ( core::describeValue( 400.123_V),   "400.1 V");
    EXPECT_EQ( core::describeValue( 50.2149_mV),  "50.21 mV");

    //
    // Trailing zeros still go, so an exact setpoint reads as the number
    // somebody wrote rather than as a false claim of resolution.
    //
    EXPECT_EQ( core::describeValue( 5.0_V),  "5 V");
    EXPECT_EQ( core::describeValue( 200_mV), "200 mV");
}

//
// The prefix binds to the unit, never to the number, and its sign comes from
// the magnitude rather than from the value -- a negative reading is a negative
// mantissa in the same prefix its positive twin would use.
//
TEST( CoreFormat, ANegativeQuantityIsPrefixedByItsMagnitude)
{
    EXPECT_EQ( core::describeValue( -50.21_mV), "-50.21 mV");
    EXPECT_EQ( core::describeValue( -5.021_V),  "-5.021 V");
}

//
// A zero has no magnitude to take a prefix from, so it takes none: "0 mV"
// would be a claim about a range that was never measured.
//
TEST( CoreFormat, ZeroIsNeverPrefixed)
{
    EXPECT_EQ( core::describeValue( 0.0_V), "0 V");
    EXPECT_EQ( core::describeValue( 0.0_s), "0 s");
}

//
// Rounding at four digits can carry a mantissa over the top of its own band.
// 999.97 V is "1000" at that width, and "1 kV" is what a reader should be
// shown -- but only because V_Type's span reaches kilovolts.
//
TEST( CoreFormat, RoundingUpAcrossABandStepsToTheNextPrefix)
{
    EXPECT_EQ( core::describeValue( 999.97_V),  "1 kV");
    EXPECT_EQ( core::describeValue( 999.94_V),  "999.9 V");
}

//
// Each unit states how far its own scale runs (see SiPrefixRange in
// core/quantities/quantity.hpp), and a value past either end falls back to the
// plain rendering rather than being dressed in a prefix that would read worse.
// A timebase has no "ks"; a frequency has no "mHz".
//
TEST( CoreFormat, AValueOutsideItsUnitsSpanIsLeftUnprefixed)
{
    static_assert( time_Type::Prefixes.Largest  ==   0);
    static_assert( Hz_Type::Prefixes.Smallest   ==   0);
    static_assert( V_Type::Prefixes.Smallest    ==  -6);

    EXPECT_EQ( core::describeValue( Time{ 3600.0 }),      "3600 s");
    EXPECT_EQ( core::describeValue( Frequency{ 0.05 }),   "0.05 Hz");

    //
    // Just below the smallest prefix still reads better inside it -- "0.5 uV"
    // over "5e-07 V" -- and only turns over a few decades further down.
    //
    EXPECT_EQ( core::describeValue( Voltage{ 5.0e-7 }),   "0.5 uV");
    EXPECT_EQ( core::describeValue( Voltage{ 1.0e-12 }),  "1e-12 V");
}

//
// The units that say nothing about prefixes render exactly as they did before
// the rule existed. Two of them are dimensionless and two are affine -- a
// "millidegC" is not a temperature anybody quotes, and a rise of "1 mK" is not
// a margin anybody writes.
//
TEST( CoreFormat, UnprefixedUnitsAreUnchanged)
{
    static_assert( ! declaresPrefixes<degC_Type>);
    static_assert( ! declaresPrefixes<K_Type>);
    static_assert( ! declaresPrefixes<dB_Type>);
    static_assert( ! declaresPrefixes<PF_Type>);

    EXPECT_EQ( core::describeValue( 0.05_degC),          "0.05 degC");
    EXPECT_EQ( core::describeValue( 0.002_K),            "0.002 K");
    EXPECT_EQ( core::describeValue( Decibel{ 0.004 }),   "0.004 dB");
    EXPECT_EQ( core::describeValue( PowerFactor{ 0.95}), "0.95");
}

//
// formatNumber itself is untouched by any of this: it is what the machine log's
// numericValue goes through (see core/src/journal/sarif_sink.cpp), and that
// column is deliberately an unprefixed number in the event's stated unit. A
// consumer that had to know which prefix a run happened to pick would be back
// to parsing the text.
//
TEST( CoreFormat, TheBareNumberIsNeverPrefixed)
{
    EXPECT_EQ( core::formatNumber( 0.05),   "0.05");
    EXPECT_EQ( core::formatNumber( 1.2e-6), "1.2e-06");

    EXPECT_EQ( core::describeValue( 200_mV),          "200 mV");
    EXPECT_EQ( core::numericOf( 200_mV).value(),      0.2);
    EXPECT_EQ( core::unitOf<Voltage>(),               "V");
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

    EXPECT_EQ( core::describeValue( core::asQuantity<Current>( erased)), "1.5 mA");
}
