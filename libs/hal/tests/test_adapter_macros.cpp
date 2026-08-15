#include "hal/adapter.hpp"

#include <gtest/gtest.h>

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
static_assert( std::is_base_of_v<core::AdapterBundleTag, Console>);

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
