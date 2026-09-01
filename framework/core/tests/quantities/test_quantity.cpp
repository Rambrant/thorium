#include "core/quantities/quantity.hpp"

#include <gtest/gtest.h>

#include <type_traits>

using namespace core::literals;
using namespace core::quantities;

TEST( CoreQuantity, LiteralsProduceExpectedValue)
{
    EXPECT_DOUBLE_EQ( (12.0_V).value(),    12.0);
    EXPECT_DOUBLE_EQ( (1.2_kV).value(),    1200.0);
    EXPECT_DOUBLE_EQ( (500.0_mV).value(),  0.5);
    EXPECT_DOUBLE_EQ( (2.0_A).value(),     2.0);
    EXPECT_DOUBLE_EQ( (250.0_mA).value(),  0.25);
    EXPECT_DOUBLE_EQ( (100.0_Ohm).value(), 100.0);
}

TEST( CoreQuantity, SameUnitComparisonsWork)
{
    EXPECT_TRUE(5.0_V == 5.0_V);
    EXPECT_TRUE(3.0_V < 5.0_V);
    EXPECT_TRUE(5.0_V >= 5.0_V);
    EXPECT_TRUE(7.0_V > 5.0_V);
}

TEST( CoreQuantity, MultiplyingVoltageAndCurrentGivesApparentPower)
{
    const ApparentPower fromVA = 12.0_V * 2.0_A;
    const ApparentPower fromAV = 2.0_A * 12.0_V;

    EXPECT_DOUBLE_EQ( fromVA.value(), 24.0);
    EXPECT_DOUBLE_EQ( fromAV.value(), 24.0);
}

TEST( CoreQuantity, ApparentPowerTimesPowerFactorGivesRealPower)
{
    const ApparentPower apparent{ 100.0};
    const PowerFactor   pf{ 0.8};

    const Power fromSxPF = apparent * pf;
    const Power fromPFxS = pf * apparent;

    EXPECT_DOUBLE_EQ( fromSxPF.value(), 80.0);
    EXPECT_DOUBLE_EQ( fromPFxS.value(), 80.0);
}

TEST( CoreQuantity, RealPowerDividedByApparentPowerGivesPowerFactor)
{
    const Power         real{ 80.0};
    const ApparentPower apparent{ 100.0};

    const PowerFactor pf = real / apparent;

    EXPECT_DOUBLE_EQ( pf.value(), 0.8);
}

TEST( CoreQuantity, RealPowerDividedByPowerFactorGivesApparentPower)
{
    const Power       real{ 80.0};
    const PowerFactor pf{ 0.8};

    const ApparentPower apparent = real / pf;

    EXPECT_DOUBLE_EQ( apparent.value(), 100.0);
}

TEST( CoreQuantity, PowerTriangleReactivePowerFromApparentAndReal)
{
    const ApparentPower apparent{ 100.0};
    const Power         real{ 80.0};

    const ReactivePower reactive = reactivePower( apparent, real);

    EXPECT_DOUBLE_EQ( reactive.value(), 60.0);
}

TEST( CoreQuantity, PowerTriangleRealPowerFromApparentAndReactive)
{
    const ApparentPower apparent{ 100.0};
    const ReactivePower reactive{ 60.0};

    const Power real = realPower( apparent, reactive);

    EXPECT_DOUBLE_EQ( real.value(), 80.0);
}

TEST( CoreQuantity, PowerTriangleApparentPowerFromRealAndReactive)
{
    const Power         real{ 80.0};
    const ReactivePower reactive{ 60.0};

    const ApparentPower apparent = apparentPower( real, reactive);

    EXPECT_DOUBLE_EQ( apparent.value(), 100.0);
}

TEST( CoreQuantity, ReactivePowerLiteralsProduceExpectedValue)
{
    EXPECT_DOUBLE_EQ( (60.0_var).value(),  60.0);
    EXPECT_DOUBLE_EQ( (1.5_kvar).value(),  1500.0);
}

//
// Five scales, which is more than any other unit here has, for the reason
// F_Type declares the widest prefix span -- see its own comment. Every one is
// checked in both spellings, because the integer overload delegating to the
// long double one is exactly where a copied line gets a factor of a thousand
// wrong (see the literals section's own comment on that).
//
TEST( CoreQuantity, CapacitanceLiteralsProduceExpectedValue)
{
    EXPECT_DOUBLE_EQ( (1.0_F).value(),      1.0);
    EXPECT_DOUBLE_EQ( (1_F).value(),        1.0);
    EXPECT_DOUBLE_EQ( (10.0_mF).value(),    10.0e-3);
    EXPECT_DOUBLE_EQ( (10_mF).value(),      10.0e-3);
    EXPECT_DOUBLE_EQ( (470.0_uF).value(),   470.0e-6);
    EXPECT_DOUBLE_EQ( (470_uF).value(),     470.0e-6);
    EXPECT_DOUBLE_EQ( (100.0_nF).value(),   100.0e-9);
    EXPECT_DOUBLE_EQ( (100_nF).value(),     100.0e-9);
    EXPECT_DOUBLE_EQ( (4700.0_pF).value(),  4700.0e-12);
    EXPECT_DOUBLE_EQ( (4700_pF).value(),    4700.0e-12);

    // And the scales agree with each other, which the individual factors above
    // could each be wrong about consistently.
    EXPECT_DOUBLE_EQ( (1000.0_nF).value(),  (1.0_uF).value());
    EXPECT_DOUBLE_EQ( (1000.0_uF).value(),  (1.0_mF).value());
}

//
// One scale, and negatives written as an operator applied to the literal --
// which is the only way C++ has, and works because Quantity has a unary minus.
//
TEST( CoreQuantity, TemperatureLiteralsProduceExpectedValue)
{
    EXPECT_DOUBLE_EQ( (85.0_degC).value(),  85.0);
    EXPECT_DOUBLE_EQ( (85_degC).value(),    85.0);
    EXPECT_DOUBLE_EQ( (-40.0_degC).value(), -40.0);
    EXPECT_DOUBLE_EQ( (20.0_K).value(),     20.0);
    EXPECT_DOUBLE_EQ( (20_K).value(),       20.0);
}

//
// The affine half of the temperature arithmetic: the gap between two points on
// a scale with no real zero is not itself a point, so it comes back in the
// difference unit. Every other unit's difference is still its own unit -- that
// is the same rule, read for a unit that declares no DifferenceType.
//
TEST( CoreQuantity, TemperatureDifferenceIsADelta)
{
    const auto ambient = 21.5_degC;
    const auto hotspot = 61.5_degC;

    const TemperatureDelta rise = hotspot - ambient;

    EXPECT_DOUBLE_EQ( rise.value(), 40.0);

    static_assert( std::is_same_v<decltype( hotspot - ambient), TemperatureDelta>);
    static_assert( std::is_same_v<decltype( 5.0_V - 3.3_V),     Voltage>);
}

TEST( CoreQuantity, ATemperatureOffsetByADeltaIsATemperature)
{
    const auto ambient = 21.5_degC;

    const Temperature ceiling = ambient + 40.0_K;
    const Temperature floorT  = ambient - 40.0_K;
    const Temperature either  = 40.0_K + ambient;

    EXPECT_DOUBLE_EQ( ceiling.value(),  61.5);
    EXPECT_DOUBLE_EQ( floorT.value(),  -18.5);
    EXPECT_DOUBLE_EQ( either.value(),   61.5);
}

//
// A difference is an ordinary magnitude with a real zero, so it keeps all the
// arithmetic an affine point gives up -- which is what makes the split worth
// having rather than merely correct.
//
TEST( CoreQuantity, TemperatureDeltasAreOrdinaryMagnitudes)
{
    const TemperatureDelta total  = 12.0_K + 8.0_K;
    const TemperatureDelta halved = 20.0_K / 2.0;
    const TemperatureDelta scaled = 10.0_K * 2.0;
    const TemperatureDelta gap    = abs( 8.0_K - 12.0_K);

    EXPECT_DOUBLE_EQ( total.value(),  20.0);
    EXPECT_DOUBLE_EQ( halved.value(), 10.0);
    EXPECT_DOUBLE_EQ( scaled.value(), 20.0);
    EXPECT_DOUBLE_EQ( gap.value(),     4.0);
}

TEST( CoreQuantity, SameUnitAdditionAndSubtractionStayInUnit)
{
    const Power sum  = 2.0_W + 3.0_W;
    const Power diff = 5.0_W - 3.0_W;
    const Power neg  = -2.0_W;

    EXPECT_DOUBLE_EQ( sum.value(),  5.0);
    EXPECT_DOUBLE_EQ( diff.value(), 2.0);
    EXPECT_DOUBLE_EQ( neg.value(), -2.0);
}

TEST( CoreQuantity, ScalarMultiplicationScalesInUnit)
{
    const Power doubled     = 2.0_W * 2.0;
    const Power alsoDoubled = 2.0 * 2.0_W;

    EXPECT_DOUBLE_EQ( doubled.value(),     4.0);
    EXPECT_DOUBLE_EQ( alsoDoubled.value(), 4.0);
}

TEST( CoreQuantity, ScalarDivisionScalesInUnit)
{
    const Power halved = 6.0_W / 2.0;

    EXPECT_DOUBLE_EQ( halved.value(), 3.0);
}

//
// NOTE: the following intentionally do NOT compile if uncommented --
// that's the point of using distinct unit tags. Left here as documentation.
// TEST(CoreQuantity, CannotCompareDifferentUnits) {
//     EXPECT_TRUE(5.0_V == 5.0_A);  // compile error: no operator== for Voltage/Current
// }
//
// TEST(CoreQuantity, CannotMultiplyTwoQuantitiesOfTheSameUnit) {
//     const auto illegal = 2.0_W * 3.0_W;  // compile error: no operator* for Power * Power
// }
//
// TEST(CoreQuantity, CannotAddPowerAndPowerFactor) {
//     const auto illegal = 80.0_W + PowerFactor{ 0.8};  // compile error: different units
// }
//
// TEST(CoreQuantity, CannotAddRealAndReactivePower) {
//     const auto illegal = 80.0_W + 60.0_var;  // compile error: different units, and
//                                               // wrong anyway -- see reactivePower()/
//                                               // realPower()/apparentPower() instead
// }
//

//
// Ohm's law, in units. The shunt case is the reason it exists: a current too
// large to route through a switching matrix is measured as a voltage across a
// known resistance, and the conversion has to stay in the type system or the
// unit is lost exactly where it matters most.
//
TEST( CoreQuantity, OhmsLawConvertsBetweenUnitsWithoutLeavingTheTypeSystem)
{
    constexpr auto shunt = 10.0_mOhm;
    constexpr auto drop  = 50.0_mV;

    constexpr auto load = drop / shunt;

    static_assert( std::is_same_v<decltype( load), const Current>);
    EXPECT_DOUBLE_EQ( load.value(), 5.0);

    // and back, both ways round
    static_assert( std::is_same_v<decltype( load * shunt), Voltage>);
    static_assert( std::is_same_v<decltype( shunt * load), Voltage>);
    static_assert( std::is_same_v<decltype( drop / load),  Resistance>);

    EXPECT_DOUBLE_EQ( ( load * shunt).value(), drop.value());
    EXPECT_DOUBLE_EQ( ( drop / load).value(),  shunt.value());
}


//
// Integer literals, for the round setpoints a bench actually runs at: 28_V is
// the same value as 28.0_V, not a different overload with its own conversion to
// get wrong. Checked as a static_assert rather than an EXPECT so the claim is
// "these are the same constant", not "they compare equal at runtime".
//
TEST( CoreQuantity, IntegerLiteralsMeanTheSameAsTheirDecimalSpelling)
{
    static_assert( 28_V   == 28.0_V);
    static_assert( 7_A    == 7.0_A);
    static_assert( 400_Hz == 400.0_Hz);
    static_assert( 50_ms  == 50.0_ms);

    static_assert( std::is_same_v<decltype( 28_V), Voltage>);
    static_assert( std::is_same_v<decltype( 7_A),  Current>);

    SUCCEED();
}

//
// The scaled suffixes are where an integer overload could plausibly have gone
// wrong -- each one delegates to its long double sibling precisely so the
// factor is not written twice and cannot drift between the two spellings.
//
TEST( CoreQuantity, IntegerLiteralsCarryTheSameScaleFactorAsTheirDecimalSpelling)
{
    static_assert( 2_kV   == 2000.0_V);
    static_assert( 50_mV  == 0.05_V);
    static_assert( 2_kOhm == 2000.0_Ohm);
    static_assert( 10_mOhm == 0.01_Ohm);
    static_assert( 3_kHz  == 3000.0_Hz);
    static_assert( 250_mA == 0.25_A);

    SUCCEED();
}
