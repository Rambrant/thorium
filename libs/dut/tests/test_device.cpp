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

TEST(DutDevice, ReadsFuseRegisterWrittenDirectlyToBus) {
    hal::Bus bus;
    dut::Device device(bus);
    bus.write_register(dut::Device::kFuseRegister, 0xF5);
    EXPECT_EQ(device.read_fuse_register(), 0xF5u);
}

TEST(DutDevice, MeasuresVoltageFromMillivoltRegister) {
    hal::Bus bus;
    dut::Device device(bus);
    bus.write_register(dut::Device::kVoltageRegister, 12030);  // 12.030 V
    EXPECT_DOUBLE_EQ(device.measure_output_voltage().value(), 12.03);
}
