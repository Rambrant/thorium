#include "hal/bus.hpp"

#include <gtest/gtest.h>

TEST( HalBus, ReadUnwrittenRegisterReturnsZero)
{
    const hal::Bus bus;

    EXPECT_EQ(bus.readRegister(0x1000), 0u);
}

TEST( HalBus, WriteThenReadReturnsSameValue)
{
    hal::Bus bus;
    bus.writeRegister( 0x1000, 0xDEADBEEF);

    EXPECT_EQ( bus.readRegister( 0x1000), 0xDEADBEEFu);
}

TEST( HalBus, RegistersAreIndependent)
{
    hal::Bus bus;
    bus.writeRegister( 0x1000, 1);
    bus.writeRegister( 0x2000, 2);

    EXPECT_EQ(bus.readRegister( 0x1000), 1u);
    EXPECT_EQ(bus.readRegister( 0x2000), 2u);
}
