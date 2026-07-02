#include "hal/bus.hpp"

#include <gtest/gtest.h>

TEST(HalBus, ReadUnwrittenRegisterReturnsZero) {
    hal::Bus bus;
    EXPECT_EQ(bus.read_register(0x1000), 0u);
}

TEST(HalBus, WriteThenReadReturnsSameValue) {
    hal::Bus bus;
    bus.write_register(0x1000, 0xDEADBEEF);
    EXPECT_EQ(bus.read_register(0x1000), 0xDEADBEEFu);
}

TEST(HalBus, RegistersAreIndependent) {
    hal::Bus bus;
    bus.write_register(0x1000, 1);
    bus.write_register(0x2000, 2);
    EXPECT_EQ(bus.read_register(0x1000), 1u);
    EXPECT_EQ(bus.read_register(0x2000), 2u);
}
