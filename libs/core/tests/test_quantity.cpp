#include "core/quantity.hpp"

#include <gtest/gtest.h>

using namespace core::literals;

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
    const core::ApparentPower fromVA = 12.0_V * 2.0_A;
    const core::ApparentPower fromAV = 2.0_A * 12.0_V;

    EXPECT_DOUBLE_EQ( fromVA.value(), 24.0);
    EXPECT_DOUBLE_EQ( fromAV.value(), 24.0);
}

TEST( CoreQuantity, ApparentPowerTimesPowerFactorGivesRealPower)
{
    const core::ApparentPower apparent{ 100.0};
    const core::PowerFactor   pf{ 0.8};

    const core::Power fromSxPF = apparent * pf;
    const core::Power fromPFxS = pf * apparent;

    EXPECT_DOUBLE_EQ( fromSxPF.value(), 80.0);
    EXPECT_DOUBLE_EQ( fromPFxS.value(), 80.0);
}

TEST( CoreQuantity, RealPowerDividedByApparentPowerGivesPowerFactor)
{
    const core::Power         real{ 80.0};
    const core::ApparentPower apparent{ 100.0};

    const core::PowerFactor pf = real / apparent;

    EXPECT_DOUBLE_EQ( pf.value(), 0.8);
}

TEST( CoreQuantity, RealPowerDividedByPowerFactorGivesApparentPower)
{
    const core::Power       real{ 80.0};
    const core::PowerFactor pf{ 0.8};

    const core::ApparentPower apparent = real / pf;

    EXPECT_DOUBLE_EQ( apparent.value(), 100.0);
}

TEST( CoreQuantity, SameUnitAdditionAndSubtractionStayInUnit)
{
    const core::Power sum  = 2.0_W + 3.0_W;
    const core::Power diff = 5.0_W - 3.0_W;
    const core::Power neg  = -2.0_W;

    EXPECT_DOUBLE_EQ( sum.value(),  5.0);
    EXPECT_DOUBLE_EQ( diff.value(), 2.0);
    EXPECT_DOUBLE_EQ( neg.value(), -2.0);
}

TEST( CoreQuantity, ScalarMultiplicationScalesInUnit)
{
    const core::Power doubled     = 2.0_W * 2.0;
    const core::Power alsoDoubled = 2.0 * 2.0_W;

    EXPECT_DOUBLE_EQ( doubled.value(),     4.0);
    EXPECT_DOUBLE_EQ( alsoDoubled.value(), 4.0);
}

TEST( CoreQuantity, ScalarDivisionScalesInUnit)
{
    const core::Power halved = 6.0_W / 2.0;

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
//     const auto illegal = 80.0_W + core::PowerFactor{ 0.8};  // compile error: different units
// }
//
