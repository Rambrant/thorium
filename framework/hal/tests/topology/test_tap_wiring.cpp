#include "hal/topology/wiring.hpp"

#include <gtest/gtest.h>

#include <string>

#include "core/meta.hpp"

//
// hal::TapWiring on its own -- the fourth wiring table, built by hand rather
// than expanded from any rig's TAP_WIRING block.
//
// Here rather than in rig/tests/ for the reason framework/hal/CMakeLists.txt
// gives about what is left in this directory: nothing below names a card, an
// instrument global or a rig, so this is a test of the mechanism and not of a
// deployment. rig/tests/test_wiring_uniqueness.cpp is where the real table is
// checked against itself, and framework/core/tests/verbs/test_measure.cpp is
// where what core::MeasureEngine *does* with these answers is pinned down.
//
// The instrument ids come from core::meta::values<hal::InstrumentId> rather
// than being spelled out, so this file survives a deployment that declares a
// different set -- the mistake dev/README.md records four framework tests
// having made, and which the link line cannot catch because an enumerator is
// neither a global nor a symbol.
//
namespace
{
    constexpr auto ids = core::meta::values<hal::InstrumentId>;

    constexpr hal::VpcLocation kPin{      hal::VpcRack::A, 1, 5 };
    constexpr hal::VpcLocation kOtherPin{ hal::VpcRack::A, 1, 7 };
} // namespace

TEST( HalTapWiring, AnEmptyTableTapsNothing)
{
    //
    // The state every rack rig's table is in, and the one core::MeasureEngine
    // asks about first on every single reading -- so "empty means routed" is
    // worth asserting rather than assuming.
    //
    const hal::TapWiring wiring;

    EXPECT_FALSE( wiring.taps( ids[ 0]));
    EXPECT_FALSE( wiring.isTappedBy( ids[ 0], kPin));
    EXPECT_TRUE(  wiring.findAll( ids[ 0]).empty());
}

TEST( HalTapWiring, ATappedInstrumentIsFoundOnItsOwnPinAndOnNoOther)
{
    hal::TapWiring wiring;
    wiring.addTap( ids[ 0], kPin);

    EXPECT_TRUE(  wiring.taps( ids[ 0]));
    EXPECT_TRUE(  wiring.isTappedBy( ids[ 0], kPin));
    EXPECT_FALSE( wiring.isTappedBy( ids[ 0], kOtherPin));
}

//
// The multichannel case: a meter cabled to several pins is several rows under
// one id, the same shape SourceWiring already uses for a three-phase source's
// four conductors. Note what this cannot distinguish -- which channel is on
// which pin -- and see hal::TapWiring's own comment for why that is out of
// reach and what the remaining check still catches.
//
TEST( HalTapWiring, OneInstrumentMayTapSeveralPins)
{
    hal::TapWiring wiring;
    wiring.addTap( ids[ 0], kPin);
    wiring.addTap( ids[ 0], kOtherPin);

    EXPECT_TRUE( wiring.isTappedBy( ids[ 0], kPin));
    EXPECT_TRUE( wiring.isTappedBy( ids[ 0], kOtherPin));
    EXPECT_EQ(   wiring.findAll( ids[ 0]).size(), 2u);
}

//
// And the converse, which is deliberately legal where the source table's
// equivalent is not: a scope and a meter on one node are two high-impedance
// taps in parallel, not a short. See hal::TapWiring on the asymmetry.
//
TEST( HalTapWiring, TwoInstrumentsMayTapOnePin)
{
    if constexpr( ids.size() < 2)
    {
        GTEST_SKIP() << "this deployment declares one instrument -- no second id to share a pin with";
    }
    else
    {
        hal::TapWiring wiring;
        wiring.addTap( ids[ 0], kPin);
        wiring.addTap( ids[ 1], kPin);

        EXPECT_TRUE( wiring.isTappedBy( ids[ 0], kPin));
        EXPECT_TRUE( wiring.isTappedBy( ids[ 1], kPin));
    }
}

//
// A tap row says nothing about any other instrument, which is what makes
// "routed or tapped" a per-instrument question rather than a per-rig one -- a
// bench may have a meter bolted to the DUT and a scope on a mux.
//
TEST( HalTapWiring, AnUntappedInstrumentIsUnaffectedByAnothersTap)
{
    if constexpr( ids.size() < 2)
    {
        GTEST_SKIP() << "this deployment declares one instrument -- nothing to be unaffected";
    }
    else
    {
        hal::TapWiring wiring;
        wiring.addTap( ids[ 0], kPin);

        EXPECT_FALSE( wiring.taps( ids[ 1]));
        EXPECT_FALSE( wiring.isTappedBy( ids[ 1], kPin));
    }
}

//
// describeTaps() exists for one caller -- core::MeasureEngine's refusal when a
// script points a tapped instrument at a pin it is not on -- so what is
// asserted here is that the message can name the pin, not the exact
// punctuation between two of them.
//
TEST( HalTapWiring, DescribeTapsNamesEveryPinTheInstrumentIsOn)
{
    hal::TapWiring wiring;
    wiring.addTap( ids[ 0], kPin);
    wiring.addTap( ids[ 0], kOtherPin);

    const auto described = wiring.describeTaps( ids[ 0]);

    EXPECT_NE( described.find( to_string( kPin)),      std::string::npos);
    EXPECT_NE( described.find( to_string( kOtherPin)), std::string::npos);
}

//
// "none" rather than an empty string, so the refusal it is spliced into reads
// as a sentence for anyone who asks about an instrument that taps nothing.
// Unreachable through core::MeasureEngine, which only asks after taps() said
// yes -- see this method's own comment.
//
TEST( HalTapWiring, DescribeTapsSaysNoneRatherThanNothingAtAll)
{
    const hal::TapWiring wiring;

    EXPECT_EQ( wiring.describeTaps( ids[ 0]), "none");
}
