#include "dut/matrix_map.hpp"

#include <gtest/gtest.h>

// A routing table declared exactly as a real DUT header would.
MATRIX(TestAdapter, "Adapter used for unit-testing the matrix map")
    POINT(Port5Vdc, Instrument::Voltmeter, 3, 7, "5Vdc supply port")
    POINT(ClkProbe, Instrument::Oscilloscope, 1, 2, "Config clock probe")
END_MATRIX

TEST(DutMatrixMap, HasExpectedNumberOfPoints) {
    EXPECT_EQ(TestAdapter.points.size(), 2u);
}

TEST(DutMatrixMap, FindsPointByName) {
    auto point = TestAdapter.find("Port5Vdc");
    ASSERT_TRUE(point.has_value());
    EXPECT_EQ(point->instrument, dut::Instrument::Voltmeter);
    EXPECT_EQ(point->crosspoint, (core::Crosspoint{3, 7}));
    EXPECT_EQ(point->description, "5Vdc supply port");
}

TEST(DutMatrixMap, UnknownPointReturnsNullopt) {
    EXPECT_FALSE(TestAdapter.find("does_not_exist").has_value());
}

// The whole table is a compile-time constant -- prove it.
TEST(DutMatrixMap, IsUsableInConstantExpressions) {
    static_assert(TestAdapter.find("ClkProbe").has_value());
    static_assert(TestAdapter.find("ClkProbe")->crosspoint == core::Crosspoint{1, 2});
    SUCCEED();
}
