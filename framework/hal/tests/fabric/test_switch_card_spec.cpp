//
// hal::SwitchDeviceModel's card table, tested without any declared device.
//
// The split rig/tests/test_switch_device.cpp predicted and left for later:
//
//     "What is genuinely generic here is the *model* table -- that a 1260-45
//      is a matrix, that its channels are <group><row><column> -- and those
//      facts are reachable through hal::SwitchDeviceModel without a declared
//      device at all. Splitting the file along that line is a real improvement
//      and a separate change; every assertion below currently goes through an
//      id, which is why the whole file moved rather than half of it."
//
// It is not optional any more, which is what brought this file into being. The
// two 34980A plug-in modules below are declared in generic hal and named by no
// rig's devices.inc -- this bench is migrating onto them and has not yet (see
// rig/devices.inc, which records the mapping and why the rows have not moved).
// So there is no hal::SwitchDeviceId to ask about them through, and the only
// place their channel spaces can be checked is here, against the model.
//
// Which is also the right place on the merits. A card's channel numbering is a
// datasheet fact, identical on every rig that owns one; whether *this* bench
// owns one is a different claim, and rig/tests/ is where that belongs. This
// file lives in the generic library's test directory and links plain hal, so a
// test here that reached for a rig's table would fail to link -- the check that
// could not see the old arrangement (a SwitchDeviceId enumerator is neither an
// instrument global nor an Apply) does see this one, because there are no
// enumerators in it.
//
#include "hal/fabric/switch_device.hpp"

#include "core/meta.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <string_view>

namespace
{
    //
    // The predicate for one model, reached through the spec rather than through
    // hal::hasChannel -- which takes a device, and a device is what these two
    // models do not have.
    //
    [[nodiscard]]
    constexpr auto hasChannel( const hal::SwitchDeviceModel model, const std::uint16_t channel) -> bool
    {
        return hal::specOf( model).HasChannel( channel);
    }
} // namespace

// ===========================================================================
// Keysight 34932A -- dual 4 x 16 armature matrix
// ===========================================================================
//
// Channels are <row><column>: row one digit, column two, so 315 is row 3
// column 15 and the whole module is 101 through 816. The module's own manual
// suggests remembering the full address as "srcc".
//
// The two matrices share the row axis rather than being selected separately --
// Matrix 1 is rows 1-4, Matrix 2 is rows 5-8 -- which is the fact most likely
// to be got wrong by someone porting a row across from the quad-matrix 1260-45A
// and reaching for a group digit.
//

TEST( HalSwitchCardSpec, The34932AIsAMatrixWithRowColumnNumbering)
{
    EXPECT_EQ( hal::specOf( hal::SwitchDeviceModel::Keysight34932A).Kind,
               hal::SwitchDeviceKind::Matrix);

    EXPECT_EQ( hal::specOf( hal::SwitchDeviceModel::Keysight34932A).Part, "Keysight 34932A");

    // The scheme it carries, and the two it does not.
    EXPECT_NE( hal::specOf( hal::SwitchDeviceModel::Keysight34932A).RowColumn,   nullptr);
    EXPECT_EQ( hal::specOf( hal::SwitchDeviceModel::Keysight34932A).Crosspoint,  nullptr);
    EXPECT_EQ( hal::specOf( hal::SwitchDeviceModel::Keysight34932A).BankChannel, nullptr);
}

TEST( HalSwitchCardSpec, The34932ARowsRunOneToEightAcrossBothMatrices)
{
    // Matrix 1.
    EXPECT_TRUE( hasChannel( hal::SwitchDeviceModel::Keysight34932A, 101));
    EXPECT_TRUE( hasChannel( hal::SwitchDeviceModel::Keysight34932A, 416));

    // Matrix 2, which is rows 5-8 of the same axis rather than a second
    // coordinate.
    EXPECT_TRUE( hasChannel( hal::SwitchDeviceModel::Keysight34932A, 501));
    EXPECT_TRUE( hasChannel( hal::SwitchDeviceModel::Keysight34932A, 816));

    // And one row past the end.
    EXPECT_FALSE( hasChannel( hal::SwitchDeviceModel::Keysight34932A, 901));
}

//
// One-based, unlike every Racal card in this table. A 1260-45A's channels start
// at 0000 and this module's start at 101, so a rig porting a crosspoint across
// cannot keep its zeroes -- which is exactly the mistake a `TRUE` here would
// hide.
//
TEST( HalSwitchCardSpec, The34932AHasNoRowZeroAndNoColumnZero)
{
    EXPECT_FALSE( hasChannel( hal::SwitchDeviceModel::Keysight34932A, 0));
    EXPECT_FALSE( hasChannel( hal::SwitchDeviceModel::Keysight34932A, 1));    // row 0, column 01
    EXPECT_FALSE( hasChannel( hal::SwitchDeviceModel::Keysight34932A, 100));  // row 1, column 00
    EXPECT_FALSE( hasChannel( hal::SwitchDeviceModel::Keysight34932A, 300));  // row 3, column 00
}

//
// Being inside 101-816 is not enough, the same way it was not enough on the
// 1260-45A: 317 is column 17 of a module whose columns stop at 16.
//
TEST( HalSwitchCardSpec, The34932AColumnsStopAtSixteen)
{
    EXPECT_TRUE(  hasChannel( hal::SwitchDeviceModel::Keysight34932A, 316));
    EXPECT_FALSE( hasChannel( hal::SwitchDeviceModel::Keysight34932A, 317));
    EXPECT_FALSE( hasChannel( hal::SwitchDeviceModel::Keysight34932A, 399));
}

//
// The Analog Bus relays are channels too, and they are Matrix 2's: on a matrix
// module in two-wire mode only Matrix 2 reaches the mainframe's Analog Buses,
// so they are numbered 921-924 (bank 2) and bank 1's 911-914 do not exist on
// this module at all.
//
// Which is the bug instruments/keysight_34980a shipped with -- it hard-coded
// 911-914 -- and the reason this pair of assertions is worth having rather than
// being obvious.
//
TEST( HalSwitchCardSpec, The34932ACarriesMatrixTwosAnalogBusRelaysAndNotBankOnes)
{
    EXPECT_TRUE( hasChannel( hal::SwitchDeviceModel::Keysight34932A, 921));
    EXPECT_TRUE( hasChannel( hal::SwitchDeviceModel::Keysight34932A, 924));

    EXPECT_FALSE( hasChannel( hal::SwitchDeviceModel::Keysight34932A, 911));
    EXPECT_FALSE( hasChannel( hal::SwitchDeviceModel::Keysight34932A, 914));
    EXPECT_FALSE( hasChannel( hal::SwitchDeviceModel::Keysight34932A, 925));
}

TEST( HalSwitchCardSpec, The34932ARowColumnPacksTheTwoDigitColumn)
{
    const auto pack = hal::specOf( hal::SwitchDeviceModel::Keysight34932A).RowColumn;

    ASSERT_NE( pack, nullptr);

    EXPECT_EQ( pack( 3, 15), 315);
    EXPECT_EQ( pack( 3, 4),  304);   // the manual's own "srcc" example, minus the slot
    EXPECT_EQ( pack( 8, 16), 816);
}

// ===========================================================================
// Keysight 34941A -- quad 1 x 4 50 ohm RF multiplexer
// ===========================================================================
//
// Channels are <bank><channel>: bank one digit 1-4, channel two digits 01-04,
// so the whole module is 101-104, 201-204, 301-304, 401-404.
//
// The same shape as the E1472A and a decade apart in the arithmetic -- that
// card packs bank and channel into two digits, this one into three -- which is
// why the packing is a per-model function rather than something the macro
// knows.
//

TEST( HalSwitchCardSpec, The34941AIsAnRfMuxWithBankChannelNumbering)
{
    EXPECT_EQ( hal::specOf( hal::SwitchDeviceModel::Keysight34941A).Kind,
               hal::SwitchDeviceKind::RfMux);

    EXPECT_EQ( hal::specOf( hal::SwitchDeviceModel::Keysight34941A).Part, "Keysight 34941A");

    EXPECT_NE( hal::specOf( hal::SwitchDeviceModel::Keysight34941A).BankChannel, nullptr);
    EXPECT_EQ( hal::specOf( hal::SwitchDeviceModel::Keysight34941A).RowColumn,   nullptr);
    EXPECT_EQ( hal::specOf( hal::SwitchDeviceModel::Keysight34941A).Crosspoint,  nullptr);
}

TEST( HalSwitchCardSpec, The34941AIsFourBanksOfFour)
{
    EXPECT_TRUE( hasChannel( hal::SwitchDeviceModel::Keysight34941A, 101));
    EXPECT_TRUE( hasChannel( hal::SwitchDeviceModel::Keysight34941A, 104));
    EXPECT_TRUE( hasChannel( hal::SwitchDeviceModel::Keysight34941A, 401));
    EXPECT_TRUE( hasChannel( hal::SwitchDeviceModel::Keysight34941A, 404));

    EXPECT_FALSE( hasChannel( hal::SwitchDeviceModel::Keysight34941A, 105));  // five channels
    EXPECT_FALSE( hasChannel( hal::SwitchDeviceModel::Keysight34941A, 501));  // five banks
    EXPECT_FALSE( hasChannel( hal::SwitchDeviceModel::Keysight34941A, 100));  // channel 00
}

//
// The front panel labels these connectors 11 through 44 -- bank and channel as
// one digit each -- and that is *not* the SCPI channel number. Asserted because
// reading a number off the module and writing it into a rig table is the
// obvious thing to do and would be wrong.
//
TEST( HalSwitchCardSpec, The34941AFrontPanelLabelsAreNotItsChannelNumbers)
{
    EXPECT_FALSE( hasChannel( hal::SwitchDeviceModel::Keysight34941A, 11));
    EXPECT_FALSE( hasChannel( hal::SwitchDeviceModel::Keysight34941A, 33));
    EXPECT_FALSE( hasChannel( hal::SwitchDeviceModel::Keysight34941A, 44));
}

//
// No Analog Bus relays at any number: the RF modules do not connect to the
// Analog Buses at all, so every signal connection is an external cable and an
// RF path cannot reach the mainframe's internal DMM.
//
TEST( HalSwitchCardSpec, The34941AHasNoAnalogBusRelays)
{
    EXPECT_FALSE( hasChannel( hal::SwitchDeviceModel::Keysight34941A, 911));
    EXPECT_FALSE( hasChannel( hal::SwitchDeviceModel::Keysight34941A, 921));
}

TEST( HalSwitchCardSpec, The34941ABankChannelPacksIntoThreeDigitsNotTwo)
{
    const auto packRf     = hal::specOf( hal::SwitchDeviceModel::Keysight34941A).BankChannel;
    const auto packE1472A = hal::specOf( hal::SwitchDeviceModel::AgilentE1472A).BankChannel;

    ASSERT_NE( packRf,     nullptr);
    ASSERT_NE( packE1472A, nullptr);

    EXPECT_EQ( packRf( 1, 3), 103);
    EXPECT_EQ( packRf( 4, 4), 404);

    // The same bank and channel on the card this one replaces.
    EXPECT_EQ( packE1472A( 1, 3), 13);
}

// ===========================================================================
// Every model says how it numbers its channels
// ===========================================================================
//
// Prose, only ever read by a human -- it is what the failed check in hal::hop()
// prints, so that a rejected channel says what the card does have rather than
// only that this was not it. Checked for every model rather than sampled,
// because a spec row added without one produces a diagnostic that says nothing.
//
TEST( HalSwitchCardSpec, EveryModelDescribesItsChannelsAndNamesItsPart)
{
    for( const auto model : core::meta::values<hal::SwitchDeviceModel>)
    {
        EXPECT_FALSE( hal::specOf( model).Part.empty())
            << "a model with no part number: " << core::meta::to_string( model);

        EXPECT_FALSE( hal::specOf( model).Channels.empty())
            << "a model that cannot say what channels it has: " << core::meta::to_string( model);

        EXPECT_NE( hal::specOf( model).HasChannel, nullptr)
            << "a model with no channel predicate: " << core::meta::to_string( model);
    }
}

//
// And the two new ones say something a reader can act on, since they are the
// two whose numbering has no precedent in this table.
//
TEST( HalSwitchCardSpec, TheNewModulesDescribeTheirSchemes)
{
    EXPECT_NE( hal::specOf( hal::SwitchDeviceModel::Keysight34932A).Channels.find( "<row><column>"),
               std::string_view::npos);

    EXPECT_NE( hal::specOf( hal::SwitchDeviceModel::Keysight34932A).Channels.find( "Matrix 2"),
               std::string_view::npos);

    EXPECT_NE( hal::specOf( hal::SwitchDeviceModel::Keysight34941A).Channels.find( "<bank><channel>"),
               std::string_view::npos);
}
