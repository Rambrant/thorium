#include "core/quantity.hpp"

#include <gtest/gtest.h>

using namespace core::literals;

TEST(CoreQuantity, LiteralsProduceExpectedValue) {
    EXPECT_DOUBLE_EQ((12.0_V).value(), 12.0);
    EXPECT_DOUBLE_EQ((1.2_kV).value(), 1200.0);
    EXPECT_DOUBLE_EQ((500.0_mV).value(), 0.5);
    EXPECT_DOUBLE_EQ((2.0_A).value(), 2.0);
    EXPECT_DOUBLE_EQ((250.0_mA).value(), 0.25);
    EXPECT_DOUBLE_EQ((100.0_Ohm).value(), 100.0);
}

TEST(CoreQuantity, SameUnitComparisonsWork) {
    EXPECT_TRUE(5.0_V == 5.0_V);
    EXPECT_TRUE(3.0_V < 5.0_V);
    EXPECT_TRUE(5.0_V >= 5.0_V);
    EXPECT_TRUE(7.0_V > 5.0_V);
}

TEST(CoreQuantity, MultiplyingVoltageAndCurrentGivesApparentPower) {
    const core::ApparentPower power = 12.0_V * 2.0_A;
    EXPECT_DOUBLE_EQ(power.value(), 24.0);
}

// NOTE: the following intentionally do NOT compile if uncommented --
// that's the point of using distinct unit tags. Left here as documentation.
// TEST(CoreQuantity, CannotCompareDifferentUnits) {
//     EXPECT_TRUE(5.0_V == 5.0_A);  // compile error: no operator== for Voltage/Current
// }
