#include "scripts/scripts.hpp"

#include <gtest/gtest.h>

#include "dut/device.hpp"
#include "hal/bus.hpp"

TEST(ValueScript, PassesOnFreshDevice) {
    hal::Bus bus;
    dut::Device device(bus);
    EXPECT_TRUE(scripts::value_script(device));
}

TEST(ValueScript, LeavesDeviceValueAtZero) {
    hal::Bus bus;
    dut::Device device(bus);
    ASSERT_TRUE(scripts::value_script(device));
    EXPECT_EQ(device.getValue(), 0u);
}
