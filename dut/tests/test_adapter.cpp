#include "hal/adapter.hpp"

#include <gtest/gtest.h>

//
// Exercises the actual DeviceX_StdAdapter data (not the generic
// AdapterPointTag mechanism -- see core/tests/test_adapter.cpp for that).
// A plain #include: ADAPTER/POINT (see hal/adapter.hpp) expand fully
// qualified, so nothing else needs to be brought into scope first.
//
#include "dut/adapter.inc"

//
// Location and quantity are compile-time values now (baked into each
// point's type), so the natural check is static_assert, not a runtime
// EXPECT -- a wrong value here would already be a compile error before
// these tests ever ran.
//
static_assert( DeviceX_StdAdapter::Output5V.LocationValue == hal::VpcLocation{ hal::VpcRack::A, 1, 3 });
static_assert( DeviceX_StdAdapter::Output5V.KindValue     == core::QuantityKind::Voltage);

static_assert( DeviceX_StdAdapter::Output3V3.LocationValue == hal::VpcLocation{ hal::VpcRack::A, 1, 6 });
static_assert( DeviceX_StdAdapter::Vout.LocationValue      == hal::VpcLocation{ hal::VpcRack::A, 1, 4 });

TEST( DeviceXProfile, PointsCarryTheNameAndDescriptionFromTheSpec)
{
    EXPECT_EQ( DeviceX_StdAdapter::Output5V.Name, "Output5V");
    EXPECT_EQ( DeviceX_StdAdapter::Output5V.Description, "5Vdc supply port");
}

TEST( DeviceXProfile, GroupDescriptionMatchesTheAdapterName)
{
    EXPECT_EQ( DeviceX_StdAdapter::Description, "Device X on standard adapter");
}
