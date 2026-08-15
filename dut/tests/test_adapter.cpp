#include "hal/adapter.hpp"

#include <gtest/gtest.h>

#include <type_traits>

//
// Exercises the actual dut adapter data (not the generic
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
static_assert( dut::Output5V.LocationValue == hal::VpcLocation{ hal::VpcRack::A, 1, 3 });

static_assert( dut::Output3V3.LocationValue == hal::VpcLocation{ hal::VpcRack::A, 1, 6 });
static_assert( dut::Vout.LocationValue      == hal::VpcLocation{ hal::VpcRack::A, 1, 4 });

TEST( DutProfile, PointsCarryTheNameAndDescriptionFromTheSpec)
{
    EXPECT_EQ( dut::Output5V.Name, "Output5V");
    EXPECT_EQ( dut::Output5V.Description, "5Vdc supply port");
}

TEST( DutProfile, GroupDescriptionComesFromTheAdapterTable)
{
    EXPECT_EQ( dut::Description, "Device X on standard adapter");
}

//
// The one BUNDLE on this adapter -- four pins that are one three-phase input
// (see dut/adapter.inc). Reached with :: exactly as dut:: itself is, so a
// misspelled line is the same "no such member" error a misspelled point is.
//
static_assert( dut::AcInput::A.LocationValue == hal::VpcLocation{ hal::VpcRack::A, 3, 1 });
static_assert( dut::AcInput::B.LocationValue == hal::VpcLocation{ hal::VpcRack::A, 3, 3 });
static_assert( dut::AcInput::C.LocationValue == hal::VpcLocation{ hal::VpcRack::A, 3, 5 });
static_assert( dut::AcInput::N.LocationValue == hal::VpcLocation{ hal::VpcRack::A, 3, 7 });

//
// A line is an ordinary point, not a second kind of thing -- same type, same
// default kind as a top-level POINT (see core::AdapterBundleTag).
//
static_assert( std::is_same_v<decltype( dut::AcInput::A), decltype( dut::Output5V)> == false);  // different pins, so different types
// AcP1 is cabled onto all four AC input lines, so they are SOURCE_LINEs;
// Output5V is a pin the DUT presents, so it stays Signal. Both are equally
// measurable -- the kind records what the pin is, not what may be done with
// it (see core::PointKind).
static_assert( dut::AcInput::A.KindValue == core::PointKind::Source);
static_assert( dut::Output5V.KindValue   == core::PointKind::Signal);
static_assert( dut::BackupSupply.KindValue == core::PointKind::Source);

TEST( DutProfile, BundleLinesCarryTheirOwnNameAndDescription)
{
    EXPECT_EQ( dut::AcInput::A.Name,        "A");
    EXPECT_EQ( dut::AcInput::A.Description, "AC input, phase A");
}

TEST( DutProfile, ABundleNamesTheInterfaceItsLinesBelongTo)
{
    EXPECT_EQ( dut::AcInput::Name,        "AcInput");
    EXPECT_EQ( dut::AcInput::Description, "Three-phase AC input");
}
