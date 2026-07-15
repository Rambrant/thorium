#include "hal/adapter.hpp"

#include <gtest/gtest.h>

//
// Exercises the actual DeviceX_StdAdapter data (not the generic hal::Adapter
// mechanism -- see hal/tests/test_adapter.cpp for that), the same way
// suite/tests/test_criteria_variants_compile.cpp exercises the criteria
// data. A plain #include: ADAPTER/POINT (see hal/adapter.hpp) expand fully
// qualified, so nothing else needs to be brought into scope first.
//
#include "libs/dut/device_x_profile.inc"

TEST( DeviceXProfile, FiveVoltOutputIsWiredWhereExpected)
{
    const auto point = DeviceX_StdAdapter.find( "5VOutput");

    ASSERT_TRUE( point.has_value());
    EXPECT_EQ( point->location, (hal::VpcLocation{ hal::VpcRack::A, 1, 3 }));
    EXPECT_EQ( point->kind, core::QuantityKind::Voltage);
}

TEST( DeviceXProfile, VoutIsWiredWhereExpected)
{
    const auto point = DeviceX_StdAdapter.find( "Vout");

    ASSERT_TRUE( point.has_value());
    EXPECT_EQ( point->location, (hal::VpcLocation{ hal::VpcRack::A, 1, 4 }));
    EXPECT_EQ( point->kind, core::QuantityKind::Voltage);
}

TEST( DeviceXProfile, UnknownPointIsNotFound)
{
    EXPECT_FALSE( DeviceX_StdAdapter.find( "NoSuchPoint").has_value());
}
