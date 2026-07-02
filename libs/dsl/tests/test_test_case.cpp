#include "dsl/test_case.hpp"

#include <gtest/gtest.h>

#include "dut/device.hpp"
#include "hal/bus.hpp"

namespace {

struct DslFixture : public ::testing::Test {
    hal::Bus bus;
    dut::Device device{bus};
};

}  // namespace

TEST_F(DslFixture, AllPassingStepsMakeRunReturnTrue) {
    dsl::TestCase test_case("power on works", device);
    test_case.power_on().expect_powered_on();
    EXPECT_TRUE(test_case.run());
}

TEST_F(DslFixture, AFailingStepMakesRunReturnFalse) {
    dsl::TestCase test_case("bad expectation", device);
    // Device starts powered off, so this expectation should fail.
    test_case.expect_powered_on();
    EXPECT_FALSE(test_case.run());
}

TEST_F(DslFixture, StepsAreRecordedInOrder) {
    dsl::TestCase test_case("value round trip", device);
    test_case.set_value(7).expect_value(7);

    const auto& steps = test_case.steps();
    ASSERT_EQ(steps.size(), 2u);
    EXPECT_EQ(steps[0].description, "set_value");
    EXPECT_EQ(steps[1].description, "expect_value");
    EXPECT_TRUE(steps[1].passed);
}
