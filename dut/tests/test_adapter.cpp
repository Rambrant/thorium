#include "hal/adapter.hpp"

#include <gtest/gtest.h>

//
// Exercises the actual DeviceX data (not the generic
// AdapterPointTag mechanism -- see core/tests/test_adapter.cpp for that).
// A plain #include: ADAPTER/POINT (see hal/adapter.hpp) expand fully
// qualified, so nothing else needs to be brought into scope first.
//
#include "dut/adapter.inc"

//
// A point's location is a compile-time value (baked into its type), so the
// natural check is static_assert, not a runtime EXPECT -- a wrong value here
// would already be a compile error before these tests ever ran.
//
// There is no quantity to assert: a point names a pin, and what is measured
// there is chosen by whichever instrument port is pointed at it (see
// core::AdapterPointTag).
//
static_assert( DeviceX::Output5V.LocationValue == hal::VpcLocation{ hal::VpcRack::A, 1, 3 });

static_assert( DeviceX::Output3V3.LocationValue == hal::VpcLocation{ hal::VpcRack::A, 1, 6 });
static_assert( DeviceX::Vout.LocationValue      == hal::VpcLocation{ hal::VpcRack::A, 1, 4 });

TEST( DeviceXProfile, PointsCarryTheNameAndDescriptionFromTheSpec)
{
    EXPECT_EQ( DeviceX::Output5V.Name, "Output5V");
    EXPECT_EQ( DeviceX::Output5V.Description, "5Vdc supply port");
}

TEST( DeviceXProfile, GroupDescriptionMatchesTheAdapterName)
{
    EXPECT_EQ( DeviceX::Description, "Device X on standard adapter");
}
