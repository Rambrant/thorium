#include "scripts/scripts.hpp"

#include <gtest/gtest.h>

#include "dut/device.hpp"
#include "hal/bus.hpp"

TEST(PowerCycleScript, PassesOnFreshDevice) {
    hal::Bus bus;
    dut::Device device(bus);
    EXPECT_TRUE(scripts::power_cycle_script(device));
}

TEST(PowerCycleScript, FailsIfDeviceStartsPoweredOn) {
    hal::Bus bus;
    dut::Device device(bus);
    device.power_on();  // violates the script's first expectation
    EXPECT_FALSE(scripts::power_cycle_script(device));
}
