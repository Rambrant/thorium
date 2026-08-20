#include "hal/address.hpp"

#include <gtest/gtest.h>

namespace
{
    //
    // The compile-time half of the transport contract, checked in both
    // directions -- the same shape hal/tests/test_safing.cpp uses for
    // hal::SafeableInstrument, and for the same reason: a concept that only
    // ever gets asserted where it holds demonstrates nothing about what it
    // rejects.
    //
    // Each driver spells its own supported set in its own constructor (see
    // hal::L4411A and friends, and each driver's own test for the per-model
    // assertion); what is checked here is the mechanism those all go through.
    //
    static_assert(   hal::ReachableOver< hal::Gpib,   hal::Gpib, hal::Lan> );
    static_assert(   hal::ReachableOver< hal::Lan,    hal::Gpib, hal::Lan> );
    static_assert( ! hal::ReachableOver< hal::Serial, hal::Gpib, hal::Lan> );
    static_assert( ! hal::ReachableOver< hal::Usb,    hal::Gpib, hal::Lan> );

    //
    // Simulated is in every driver's set without any driver listing it -- see
    // hal::ReachableOver's own comment on why that hole is deliberate. Worth
    // its own assertion rather than being left implicit in the fact that the
    // driver tests all construct with it: if this ever stopped holding, every
    // one of those tests would fail at once and none of them would say why.
    //
    static_assert(   hal::ReachableOver< hal::Simulated, hal::Gpib> );
    static_assert(   hal::ReachableOver< hal::Simulated, hal::Lan, hal::Usb> );

    //
    // A rig may hoist a shared box's address into a named constant above its
    // instrument table (see rig/active_instruments.hpp's own comment on the
    // four DcP rows) -- which only works because every alternative stays a
    // literal type. Nothing else in this codebase would notice if a future
    // field broke that, so it is asserted here rather than discovered by the
    // first rig that tries.
    //
    constexpr auto mainframe = hal::Gpib( 0, 14);

    static_assert( mainframe == hal::Gpib( 0, 14));
    static_assert( mainframe != hal::Gpib( 0, 15));
} // namespace

TEST( HalAddress, GpibRendersBoardAndPrimaryAddress)
{
    EXPECT_EQ( to_string( hal::Address{ hal::Gpib( 0, 14) }), "Gpib 0::14");
    EXPECT_EQ( to_string( hal::Address{ hal::Gpib( 1, 9) }),  "Gpib 1::9");
}

//
// Secondary addressing is optional, and an unset optional must not render as
// a value -- the same distinction hal::describeSetting draws for an unset
// builder field (see hal/describe.hpp): "0::14" and "0::14::0" are different
// instruments on the same bus.
//
TEST( HalAddress, GpibRendersSecondaryAddressOnlyWhenThereIsOne)
{
    EXPECT_EQ( to_string( hal::Address{ hal::Gpib( 0, 14) }),    "Gpib 0::14");
    EXPECT_EQ( to_string( hal::Address{ hal::Gpib( 0, 14, 3) }), "Gpib 0::14::3");
    EXPECT_EQ( to_string( hal::Address{ hal::Gpib( 0, 14, 0) }), "Gpib 0::14::0");
}

TEST( HalAddress, LanRendersHostAndPortIncludingTheDefaultOne)
{
    EXPECT_EQ( to_string( hal::Address{ hal::Lan( "bench-dmm1") }),        "Lan bench-dmm1:5025");
    EXPECT_EQ( to_string( hal::Address{ hal::Lan( "10.0.0.11", 5023) }),   "Lan 10.0.0.11:5023");
}

TEST( HalAddress, SerialAndUsbRenderTheOneFieldThatIdentifiesThem)
{
    EXPECT_EQ( to_string( hal::Address{ hal::Serial( "/dev/ttyUSB0") }), "Serial /dev/ttyUSB0");
    EXPECT_EQ( to_string( hal::Address{ hal::Usb( "MY53001234") }),      "Usb MY53001234");
}

//
// The one alternative with nothing to render after its own name -- see
// hal::Simulated's own comment. A trailing space here would be the kind of
// thing nobody notices until it is in a log file.
//
TEST( HalAddress, SimulatedRendersItsKindAndNothingElse)
{
    EXPECT_EQ( to_string( hal::Address{ hal::Simulated{} }), "Simulated");
}
