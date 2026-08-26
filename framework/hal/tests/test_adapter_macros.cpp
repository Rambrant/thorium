#include "hal/adapter.hpp"
#include "hal/bundle.hpp"

#include "core/at.hpp"

#include <gtest/gtest.h>

#include <algorithm>

#include <type_traits>

//
// The ADAPTER/POINT/BUNDLE macros themselves, against a local table rather
// than the rig's real dut/adapter.inc -- which dut/tests/test_adapter.cpp
// already covers. Two things make that split worth having:
//
//   - SOURCE_POINT/SOURCE_LINE are exercised here at all. This rig's
//     adapter.inc declares none yet, because which pin each fixed-wired
//     source lands on is an open schematic question (see rig/wiring.inc's
//     SOURCE_WIRING comment) -- so without this file the source half of the
//     macro set would ship untested.
//   - A BUNDLE is checked to be usable on its own, not only nested inside an
//     ADAPTER, since nothing about it depends on being one.
//
// No ADAPTER here despite testing POINT: ADAPTER opens the fixed `dut`
// struct (see hal/adapter.hpp), and defining a second one in a translation
// unit that may be linked alongside the real adapter is a clash waiting to
// happen. POINT/SOURCE_POINT expand to exactly what LINE/SOURCE_LINE do, so
// the bundle below covers both spellings' actual content.
//
namespace
{
    BUNDLE( Console, "RS232 debug console")
        LINE( Tx,  A, 2, 1, "transmit")
        LINE( Rx,  A, 2, 2, "receive")
        LINE( Gnd, A, 2, 5, "signal ground")

        //
        // A line a source is cabled onto -- the in-bundle spelling of
        // SOURCE_POINT. Bus power on a debug connector is the ordinary
        // reason a multi-wire interface has one, and it shows that a bundle
        // can mix the two kinds: most lines are signals, one is a rail.
        //
        SOURCE_LINE( Vbus, A, 2, 9, "5Vdc bus power, cabled from the supply")
    END_BUNDLE
} // namespace

//
// Opted into being a bundle by inheritance, which is what the reflection
// walk in dut/tests/test_wiring_coverage.cpp keys on -- see
// core::AdapterBundleTag. Checked here so a change to what BUNDLE expands to
// fails in hal's own tests rather than only as a silently-shrinking point
// count somewhere else.
//
// `struct Console`, not plain `Console`, and this is the one place in the
// codebase that has to say so: BUNDLE declares a value of that name alongside
// the struct so that at( dut::Console) has something to pass, and a variable
// hides a class name for ordinary lookup. Qualified lookup ignores it -- every
// Console::Tx below is unaffected -- but a type position needs the elaborated
// form. See core::AdapterBundle's own comment for the full arrangement and
// what it bought.
//
static_assert( std::is_base_of_v<core::AdapterBundleTag, struct Console>);

//
// The value half, and the reason for all of the above: a bundle is passable,
// so a routing verb can take the whole interface rather than one wire.
//
static_assert( std::is_same_v<decltype( core::at( Console)),
                              core::At<core::AdapterBundle<struct Console>>>);

static_assert( Console.Name        == "Console");
static_assert( Console.Description == "RS232 debug console");

//
// Both spellings, side by side -- the thing the shared name exists to make
// possible, and the thing most likely to be broken by a well-meaning
// simplification of the BUNDLE macro. The first reaches the variable through
// ordinary lookup, the second reaches the struct through qualified lookup.
//
static_assert( core::at( Console).point.Name == "Console");
static_assert( Console::Tx.LocationValue == hal::VpcLocation{ hal::VpcRack::A, 2, 1 });

static_assert( Console::Tx.LocationValue  == hal::VpcLocation{ hal::VpcRack::A, 2, 1 });
static_assert( Console::Gnd.LocationValue == hal::VpcLocation{ hal::VpcRack::A, 2, 5 });

//
// The distinction the two line macros exist to draw. LINE leaves the kind
// defaulted, so a routable line is the same type it would be if the kind had
// never been added (see core::AdapterPointTag).
//
static_assert( Console::Tx.KindValue   == core::PointKind::Signal);
static_assert( Console::Vbus.KindValue == core::PointKind::Source);

static_assert( std::is_same_v<decltype( Console::Tx),
                              const core::AdapterPointTag<hal::VpcLocation{ hal::VpcRack::A, 2, 1 }>>);

TEST( HalAdapterMacros, BundleLinesCarryTheirOwnNameAndDescription)
{
    EXPECT_EQ( Console::Rx.Name,        "Rx");
    EXPECT_EQ( Console::Rx.Description, "receive");
}

TEST( HalAdapterMacros, ABundleNamesTheInterfaceRatherThanRepeatingItPerLine)
{
    //
    // The readability half of grouping: the interface is named once, and a
    // line is named by what it does within it -- Console::Tx, not
    // Console_Tx repeated four times with a shared prefix nothing checks.
    //
    EXPECT_EQ( Console::Name,        "Console");
    EXPECT_EQ( Console::Description, "RS232 debug console");
}


//
// Every LINE the bundle declares, recovered by reflection rather than from
// anything the macro accumulated -- see hal::bundleLocationList on why that
// distinction is the whole point. The order is declaration order, which is
// what lets a reader check this list against dut/adapter.inc by eye.
//
TEST( HalAdapterMacros, ABundlesLinesAreRecoverableForRouting)
{
    constexpr auto locations = hal::bundleLocations<struct Console>;

    ASSERT_EQ( locations.size(), 4u);

    EXPECT_EQ( locations[ 0], ( hal::VpcLocation{ hal::VpcRack::A, 2, 1 }));   // Tx
    EXPECT_EQ( locations[ 1], ( hal::VpcLocation{ hal::VpcRack::A, 2, 2 }));   // Rx
    EXPECT_EQ( locations[ 2], ( hal::VpcLocation{ hal::VpcRack::A, 2, 5 }));   // Gnd
    EXPECT_EQ( locations[ 3], ( hal::VpcLocation{ hal::VpcRack::A, 2, 9 }));   // Vbus
}

//
// Both kinds of line are returned. Which table a pin is reachable through is
// the router's question, not the walk's -- a bundle whose lines are all
// SOURCE_LINEs (dut::AcInput) still names four pins.
//
TEST( HalAdapterMacros, RoutingSeesSourceLinesAsWellAsSignalLines)
{
    constexpr auto locations = hal::bundleLocations<struct Console>;

    EXPECT_NE( std::ranges::find( locations, hal::VpcLocation{ hal::VpcRack::A, 2, 9 }), locations.end());
}
