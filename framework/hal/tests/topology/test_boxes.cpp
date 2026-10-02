#include "hal/topology/boxes.hpp"

#include <array>
#include <string_view>

#include <gtest/gtest.h>

#include "core/meta.hpp"

//
// The two rules hal/topology/boxes.hpp holds every rig's tables to -- one box,
// one address; one address, one box -- checked in both directions on rows made
// up here, since the rig's own rows can only ever show the passing direction
// (a conflict there is a build failure, which is the point, and not something
// a test can observe).
//
// detail::boxConflict() is consteval, so these are static_asserts: the rule is
// evaluated by the same compiler pass that evaluates it on a real table.
//
namespace
{
    using hal::BoxRow;
    using hal::Lan;
    using hal::Simulated;
    using hal::Usb;
    using hal::detail::boxConflict;

    //
    // Three faces of one supply, all saying its address: fine.
    //
    constexpr std::array<BoxRow, 3> kOneBoxThreeFaces{ {
        { "Psu1", "DcP5", Usb{ "CN65100272" } },
        { "Psu1", "DcP6", Usb{ "CN65100272" } },
        { "Psu1", "DcP7", Usb{ "CN65100272" } } } };

    static_assert( boxConflict( kOneBoxThreeFaces).empty());

    //
    // The same three with the last row copied and half-edited: one box, two
    // addresses.
    //
    constexpr std::array<BoxRow, 3> kOneBoxTwoAddresses{ {
        { "Psu1", "DcP5", Usb{ "CN65100272" } },
        { "Psu1", "DcP6", Usb{ "CN65100272" } },
        { "Psu1", "DcP7", Usb{ "CN65100999" } } } };

    static_assert( !boxConflict( kOneBoxTwoAddresses).empty());

    //
    // A typo in a box name -- PSU1 for Psu1 -- leaves one unit under two
    // names, which is what the second rule exists to catch.
    //
    constexpr std::array<BoxRow, 2> kTwoNamesOneAddress{ {
        { "Psu1", "DcP5", Usb{ "CN65100272" } },
        { "PSU1", "DcP6", Usb{ "CN65100272" } } } };

    static_assert( !boxConflict( kTwoNamesOneAddress).empty());

    //
    // Different boxes on different addresses: fine, and the ordinary case.
    //
    constexpr std::array<BoxRow, 2> kTwoBoxes{ {
        { "DeskDmm", "Dmm1", Usb{ "CN65510018" } },
        { "Scope1",  "Osc1", Lan{ "dev-scope" } } } };

    static_assert( boxConflict( kTwoBoxes).empty());

    //
    // Simulated names no unit, so a bench whose instruments are mostly not
    // plugged in may say it on every row without its boxes colliding.
    //
    constexpr std::array<BoxRow, 3> kSimulatedEverywhere{ {
        { "BenchDmm",   "Dmm1", Simulated{} },
        { "BenchScope", "Osc1", Simulated{} },
        { "BenchPsu",   "DcP5", Simulated{} } } };

    static_assert( boxConflict( kSimulatedEverywhere).empty());

    //
    // A face of a real box that is not there -- the 34980A's internal DMM,
    // say -- is Simulated on its own row while the chassis and its cards say
    // the box's address: fine. It opens nothing, so it cannot reach another
    // unit, which is all the first rule is there to stop.
    //
    constexpr std::array<BoxRow, 3> kOneFaceSimulated{ {
        { "Swu1", "Swu1",    Usb{ "MY53154781" } },
        { "Swu1", "Dmm2",    Simulated{} },
        { "Swu1", "Matrix1", Usb{ "MY53154781" }, true } } };

    static_assert( boxConflict( kOneFaceSimulated).empty());

    //
    // Either way round: the simulated face first in the table, too.
    //
    constexpr std::array<BoxRow, 2> kSimulatedFaceFirst{ {
        { "Psu1", "DcP5", Simulated{} },
        { "Psu1", "DcP6", Usb{ "CN65100272" } } } };

    static_assert( boxConflict( kSimulatedFaceFirst).empty());

    //
    // A simulated face does not excuse the others: the box's real rows still
    // have to agree with each other.
    //
    constexpr std::array<BoxRow, 3> kSimulatedFaceAndTwoAddresses{ {
        { "Psu1", "DcP5", Simulated{} },
        { "Psu1", "DcP6", Usb{ "CN65100272" } },
        { "Psu1", "DcP7", Usb{ "CN65100999" } } } };

    static_assert( !boxConflict( kSimulatedFaceAndTwoAddresses).empty());

    //
    // And a device row is not a face anything opens, so Simulated on one
    // while the box is real is a claim about a card the box still drives.
    //
    constexpr std::array<BoxRow, 2> kSimulatedDeviceOnARealBox{ {
        { "Swu1", "Swu1",    Usb{ "MY53154781" } },
        { "Swu1", "Matrix1", Simulated{}, true } } };

    static_assert( !boxConflict( kSimulatedDeviceOnARealBox).empty());

    //
    // And this deployment's own tables, which the header already asserts --
    // repeated here so the test binary says so by name.
    //
    static_assert( hal::detail::rigBoxConflict().empty());
} // namespace

//
// Every instrument has a box, and the box lists it back.
//
TEST( Boxes, EveryInstrumentIsAFaceOfTheBoxItNames)
{
    for( const auto id : core::meta::values<hal::InstrumentId>)
    {
        const auto box   = hal::boxOf( id);
        const auto faces = hal::instrumentsIn( box);

        EXPECT_FALSE( box.empty()) << to_string( id);
        EXPECT_NE( std::ranges::find( faces, id), faces.end()) << to_string( id) << " is not listed by " << box;
        EXPECT_TRUE( hal::hasInstrumentsIn( box)) << box;
    }
}

//
// The box names, each once and in table order -- what --address and
// THORIUM_ADDRESS_<box> are matched against.
//
TEST( Boxes, BoxNamesAreListedOnceEachInTableOrder)
{
    const auto names = hal::instrumentBoxNames();

    ASSERT_FALSE( names.empty());
    EXPECT_EQ( names.front(), hal::boxOf( core::meta::values<hal::InstrumentId>[ 0]));

    for( std::size_t first = 0; first < names.size(); ++first)
    {
        for( std::size_t second = first + 1; second < names.size(); ++second)
        {
            EXPECT_NE( names[ first], names[ second]) << "listed twice";
        }
    }
}

TEST( Boxes, ABoxNoRowNamesHasNoInstruments)
{
    EXPECT_FALSE( hal::hasInstrumentsIn( "NoSuchBox"));
    EXPECT_TRUE( hal::instrumentsIn( "NoSuchBox").empty());
}
