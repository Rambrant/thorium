#include "dut/device.hpp"

#include <gtest/gtest.h>

#include "hal/bus.hpp"

TEST( DutDevice, StartsPoweredOff)
{
    hal::Bus          bus;
    const dut::Device device( bus);

    EXPECT_FALSE( device.isPoweredOn());
}

TEST( DutDevice, PowerOnSetsState)
{
    hal::Bus    bus;
    dut::Device device( bus);

    device.powerOn();

    EXPECT_TRUE( device.isPoweredOn());
}

TEST( DutDevice, PowerOffAfterOnClearsState)
{
    hal::Bus    bus;
    dut::Device device( bus);

    device.powerOn();
    device.powerOff();

    EXPECT_FALSE( device.isPoweredOn());
}

TEST( DutDevice, ValueRoundTrips)
{
    hal::Bus    bus;
    dut::Device device( bus);
    device.setValue( 42);

    EXPECT_EQ( device.getValue(), 42u);
}

TEST( DutDevice, ReadsFuseRegisterWrittenDirectlyToBus)
{
    hal::Bus          bus;
    const dut::Device device( bus);
    bus.writeRegister( dut::Device::kFuseRegister, 0xF5);

    EXPECT_EQ( device.readFuseRegister(), 0xF5u);
}

TEST( DutDevice, MeasuresVoltageFromMillivoltRegister)
{
    hal::Bus          bus;
    const dut::Device device( bus);
    bus.writeRegister( dut::Device::kVoltageRegister, 12030); // 12.030 V

    EXPECT_DOUBLE_EQ( device.measureOutputVoltage().value(), 12.03);
}
