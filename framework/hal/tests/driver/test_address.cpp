#include "hal/driver/address.hpp"

#include <gtest/gtest.h>

namespace
{
    //
    // The compile-time half of the transport contract, checked in both
    // directions -- the same shape rig/tests/test_safing.cpp uses for
    // hal::SafeableInstrument, and for the same reason: a concept that only
    // ever gets asserted where it holds demonstrates nothing about what it
    // rejects.
    //
    // Each driver spells its own supported set in its own constructor (see
    // hal::keysight_edu34450a::EDU34450A and friends, and each driver's own test for the per-model
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
    // instrument table (see rig/instrument.inc's own comment on the
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
// builder field (see hal/driver/describe.hpp): "0::14" and "0::14::0" are different
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

//
// hal::BackPanel is the same list of connectors as the concept above, in a
// form something other than an overload resolution can ask about -- see
// hal/driver/address.hpp on why a resolver needs one.
//
// The compile-time half is asserted in both directions, exactly as
// hal::ReachableOver is at the top of this file, and for the same reason: a
// check only ever exercised where it holds demonstrates nothing about what it
// rejects.
//
using TwoConnectors = hal::BackPanel<hal::Lan, hal::Usb>;

static_assert(   TwoConnectors::allows<hal::Lan> );
static_assert(   TwoConnectors::allows<hal::Usb> );
static_assert( ! TwoConnectors::allows<hal::Gpib> );
static_assert( ! TwoConnectors::allows<hal::Serial> );

//
// Simulated is in every panel without any driver listing it -- the same
// deliberate hole ReachableOver has, and the thing that makes detaching one
// instrument of a live rig possible without editing its table.
//
static_assert( TwoConnectors::allows<hal::Simulated> );

//
// And the runtime half agrees with the compile-time half, which is the
// property the whole arrangement rests on: these are two readings of one
// list, not two lists that happen to match today.
//
TEST( HalBackPanel, TheRuntimeCheckAgreesWithTheCompileTimeOne)
{
    EXPECT_TRUE(  TwoConnectors::accepts( hal::Address{ hal::Lan{ "host" } }));
    EXPECT_TRUE(  TwoConnectors::accepts( hal::Address{ hal::Usb{ "CN0001" } }));
    EXPECT_TRUE(  TwoConnectors::accepts( hal::Address{ hal::Simulated{} }));
    EXPECT_FALSE( TwoConnectors::accepts( hal::Address{ hal::Gpib{ 0, 5 } }));
    EXPECT_FALSE( TwoConnectors::accepts( hal::Address{ hal::Serial{ "/dev/ttyUSB0" } }));
}

//
// The connectors named for a diagnostic, taken off each alternative's own
// type name the way to_string() takes a kind's -- so a driver cannot offer a
// reader a connector its constraint does not have.
//
// Simulated is absent from the text on purpose. It is not a connector, and
// offering it to somebody whose USB serial was just refused would be
// suggesting they unplug the instrument they are trying to reach.
//
TEST( HalBackPanel, ThePanelNamesItsOwnConnectors)
{
    EXPECT_EQ( TwoConnectors::Kinds,                        "Lan, Usb");
    EXPECT_EQ( ( hal::BackPanel<hal::Usb>::Kinds),          "Usb");
    EXPECT_EQ( ( hal::BackPanel<hal::Gpib, hal::Serial>::Kinds), "Gpib, Serial");
}

//
// A panel nobody declared accepts nothing, which is the safe direction for a
// default: a caller carrying an unset hal::BackPanelInfo refuses every
// address rather than waving all of them through.
//
TEST( HalBackPanel, AnUndeclaredPanelAcceptsNothing)
{
    const hal::BackPanelInfo none;

    EXPECT_FALSE( none.allows( hal::Address{ hal::Lan{ "host" } }));
    EXPECT_FALSE( none.allows( hal::Address{ hal::Simulated{} }));
}

//
// kindOf() is to_string() without the detail -- for a message about the bus
// rather than about the address, which is what a refusal against a panel is.
//
TEST( HalAddress, KindOfNamesTheBusAndNothingElse)
{
    EXPECT_EQ( hal::kindOf( hal::Address{ hal::Lan( "bench-dmm1") }),   "Lan");
    EXPECT_EQ( hal::kindOf( hal::Address{ hal::Gpib( 0, 14) }),         "Gpib");
    EXPECT_EQ( hal::kindOf( hal::Address{ hal::Simulated{} }),          "Simulated");
}
