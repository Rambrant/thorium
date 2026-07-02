#include "dut/device.hpp"

#include <gtest/gtest.h>

#include "hal/bus.hpp"

TEST(DutDevice, StartsPoweredOff) {
    hal::Bus bus;
    dut::Device device(bus);
    EXPECT_FALSE(device.is_powered_on());
}

TEST(DutDevice, PowerOnSetsState) {
    hal::Bus bus;
    dut::Device device(bus);
    device.power_on();
    EXPECT_TRUE(device.is_powered_on());
}

TEST(DutDevice, PowerOffAfterOnClearsState) {
    hal::Bus bus;
    dut::Device device(bus);
    device.power_on();
    device.power_off();
    EXPECT_FALSE(device.is_powered_on());
}

TEST(DutDevice, ValueRoundTrips) {
    hal::Bus bus;
    dut::Device device(bus);
    device.set_value(42);
    EXPECT_EQ(device.get_value(), 42u);
}
