#include "scripts/scripts.hpp"

#include <gtest/gtest.h>

#include "dut/device.hpp"
#include "hal/bus.hpp"

TEST(ValueScript, PassesOnFreshDevice) {
    hal::Bus bus;
    dut::Device device(bus);
    EXPECT_TRUE(scripts::valueScript(device));
}

TEST(ValueScript, LeavesDeviceValueAtZero) {
    hal::Bus bus;
    dut::Device device(bus);
    ASSERT_TRUE(scripts::valueScript(device));
    EXPECT_EQ(device.getValue(), 0u);
}
