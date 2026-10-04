#include "../prelude.hpp"

#include "core/journal/journal.hpp"
#include "hal/fabric/switch_device.hpp"
#include "hal/io/scpi.hpp"

#include <string>
#include <vector>

//
// The 34980A switch unit, tested in isolation -- the SwitchUnit group in
// dev/suite/test_catalog.inc. Nothing is wired to it: every check is a
// question the mainframe answers about itself.
//
// This desk's rack, which is also the rack rig/devices.inc records the bench
// migrating onto:
//
//     slots 1-4   34932A (34932T terminal block)   dual 4 x 16 armature matrix
//     slot  5     34941A                            quad 1 x 4 RF multiplexer
//
//   inventory  what is in the eight slots, by SYST:CTYP?
//   matrix     per 34932A: one crosspoint, a list spanning both matrices,
//              closeExclusively, the four Analog Bus relays, openAll -- each
//              checked by asking ROUT:CLOS?, never by trusting what was sent
//   refusal    channels a 34932A does not have are errors, not no-ops
//   RF         the 34941A's banks select 1-of-4, and refuse to be opened
//   cycles     a relay's life count moves when it is driven
//
// What "closed" means here, and what it does not: ROUT:CLOS? reports what the
// mainframe told the relay, and DIAG:REL:CYCL? that the relay's drive changed
// state. Neither is a meter on the contacts. That is the one check this group
// cannot make, and the one that needs something wired.
//
// With nothing live, closing is harmless in every combination below. The one
// cable on the rack -- DcP7 on slot 1's Matrix 2 column 1 -- is off whenever
// these run (nothing here enables it, and safing turns it off), so several
// crosspoints closed together join rows and columns carrying nothing, and
// an Analog Bus relay joins a Matrix 2 row to a backplane nothing else is on
// (or to the internal DMM, if one is fitted and enabled). A rack with a DUT on
// its terminal blocks is a different matter, and not what these are for.
//
// The channel numbers come from hal's own models of the two modules --
// hal::detail::keysight34932ARowColumn and keysight34941ABankChannel, the
// arithmetic the fabric will route with -- so a numbering mistake there shows
// up here, against the hardware, before any rig depends on it.
//
// These call the chassis driver directly -- Swu1.close(), not Connect() --
// because what they test is the driver: that each SCPI command does what it
// says on this rack. A script that wants a route rather than a relay uses the
// fabric (see swu_dmm.cpp's routed reading); these relay moves are not journal
// events, and the verdicts are. Every script puts back what it moved
// before it returns -- matrices all open, RF banks on channel 01 -- and safing
// opens every matrix after the run regardless, in the pass after every source
// is off.
//

using hal::keysight_34980a::AnalogBus;
using hal::keysight_34980a::ChannelAddress;
using hal::keysight_34980a::analogBus;

namespace
{
    constexpr int kRfSlot = 5;

    //
    // A 34932A crosspoint, as the chassis addresses it: slot, then <row><column>.
    //
    constexpr auto crosspoint( const int slot, const unsigned row, const unsigned column) -> ChannelAddress
    {
        return ChannelAddress{ slot, hal::detail::keysight34932ARowColumn( row, column) };
    }

    //
    // A 34941A channel: slot, then <bank><channel>.
    //
    constexpr auto rfChannel( const unsigned bank, const unsigned channel) -> ChannelAddress
    {
        return ChannelAddress{ kRfSlot, hal::detail::keysight34941ABankChannel( bank, channel) };
    }

    //
    // Whether the instrument refused this close -- which is what checked()
    // turns a queued SCPI error into (see Chassis::close()). A close that went
    // through is the failure, and is undone so the rack is left as found.
    //
    auto refusesToClose( const ChannelAddress channel) -> bool
    {
        try
        {
            Swu1.close( channel);
        }
        catch( const hal::io::ScpiFault &)
        {
            return true;
        }

        Swu1.openAll( channel.Slot);

        return false;
    }

    //
    // One whole cycle of checks on the 34932A in slot N, from and back to all
    // open.
    //
    template<int N>
    auto matrixOn() -> void
    {
        Swu1.openAll( N);

        //
        // One crosspoint: closed when told, its neighbour in the same row
        // untouched, open when told.
        //
        const auto first     = crosspoint( N, 1, 1);
        const auto neighbour = crosspoint( N, 1, 2);

        Swu1.close( first);
        Verify( DEV_Swu_1::DEV_Swu_Closed, Swu1.isClosed( first));
        Verify( DEV_Swu_1::DEV_Swu_Open,   !Swu1.isClosed( neighbour));

        Swu1.open( first);
        Verify( DEV_Swu_1::DEV_Swu_Open,   !Swu1.isClosed( first));

        //
        // A list in one ROUT:CLOS, at the four corners of both matrices --
        // rows 1 and 4 are Matrix 1, rows 5 and 8 Matrix 2, columns 1 and 16
        // the ends of each. The corners are where a numbering off by one shows:
        // row 0, column 0 and column 17 are none of them crosspoints.
        //
        const std::vector<ChannelAddress> corners{
            crosspoint( N, 1, 1), crosspoint( N, 4, 16), crosspoint( N, 5, 1), crosspoint( N, 8, 16) };

        Swu1.close( corners);

        for( const auto channel : corners)
        {
            Verify( DEV_Swu_1::DEV_Swu_Closed, Swu1.isClosed( channel));
        }

        //
        // closeExclusively opens everything else on the *module* -- both
        // matrices -- and closes this one. So all four corners must be open
        // after it, the Matrix 2 ones included.
        //
        const auto exclusive = crosspoint( N, 2, 8);

        Swu1.closeExclusively( exclusive);

        Verify( DEV_Swu_1::DEV_Swu_Closed, Swu1.isClosed( exclusive));

        for( const auto channel : corners)
        {
            Verify( DEV_Swu_1::DEV_Swu_Open, !Swu1.isClosed( channel));
        }

        //
        // All four Analog Bus relays, which on a matrix are Matrix 2's alone --
        // 921-924, bank 2 -- connecting rows 5-8 to ABus1-4. The bank-1
        // numbers a multiplexer would use do not exist here; the refusal
        // script checks that from the other side.
        //
        const std::vector<ChannelAddress> buses{
            analogBus( N, 2, AnalogBus::One),   analogBus( N, 2, AnalogBus::Two),
            analogBus( N, 2, AnalogBus::Three), analogBus( N, 2, AnalogBus::Four) };

        Swu1.close( buses);

        for( const auto bus : buses)
        {
            Verify( DEV_Swu_1::DEV_Swu_Closed, Swu1.isClosed( bus));
        }

        //
        // And openAll for the slot takes everything with it.
        //
        Swu1.openAll( N);
        Swu1.waitForSwitching( N);

        Verify( DEV_Swu_1::DEV_Swu_Open, !Swu1.isClosed( exclusive));

        for( const auto bus : buses)
        {
            Verify( DEV_Swu_1::DEV_Swu_Open, !Swu1.isClosed( bus));
        }
    }
} // namespace

//
// Slots 1-4 hold 34932As and slot 5 a 34941A. Every slot's answer is also
// posted as a Note -- "Swu1 slot 5" and whatever SYST:CTYP? said -- which
// reaches the machine log and the console's Raw events tab, so a failed check
// says what *was* there.
//
auto swuInventory() -> void
{
    const auto rack = Swu1.modules();

    for( int slot = 1; slot <= static_cast<int>( rack.size()); ++slot)
    {
        const auto & module = rack[ slot - 1];

        core::journal().post( core::JournalRecord{
            .Method  = core::Verb::Note,
            .Subject = "Swu1 slot " + std::to_string( slot),
            .Detail  = module.Empty ? std::string( "empty")
                                    : module.Vendor + " " + module.Model + " serial " + module.Serial
                                      + " firmware " + module.Firmware });
    }

    for( int slot = 1; slot <= 4; ++slot)
    {
        Verify( DEV_Swu_1::DEV_Swu_Module34932A, rack[ slot - 1].Model == "34932A");
    }

    Verify( DEV_Swu_1::DEV_Swu_Module34941A, rack[ kRfSlot - 1].Model == "34941A");
}

auto swuMatrixSlot1() -> void { matrixOn<1>(); }
auto swuMatrixSlot2() -> void { matrixOn<2>(); }
auto swuMatrixSlot3() -> void { matrixOn<3>(); }
auto swuMatrixSlot4() -> void { matrixOn<4>(); }

//
// Three channels a 34932A does not have, each a well-formed (@sccc) the
// driver's shape check passes -- so it is the instrument that has to refuse,
// and the driver that has to turn the refusal into an error rather than a
// relay that silently did not move:
//
//   117   row 1, column 17 -- one past the last column
//   901   row 9            -- one past the last row
//   911   ABus1 on bank 1  -- a multiplexer's bus relay, which a matrix lacks
//                             (the driver once hard-coded these; see its README)
//
// hal's own model agrees with all three before the box is asked, which is
// asserted at compile time: if the model ever thought one of them was a
// channel, this script would be checking the wrong thing.
//
auto swuRefusesMissingChannels() -> void
{
    static_assert( !hal::detail::keysight34932AHasChannel( 117));
    static_assert( !hal::detail::keysight34932AHasChannel( 901));
    static_assert( !hal::detail::keysight34932AHasChannel( 911));

    for( const int missing : { 117, 901, 911 })
    {
        Verify( DEV_Swu_1::DEV_Swu_Refused, refusesToClose( ChannelAddress{ 1, missing }));
    }
}

//
// The 34941A, which is not a set of relays that open and close: each bank is
// a 1-of-4 selector. Closing a channel opens whichever the bank had closed, in
// a sequence that never joins two inputs, and there is no "all open" -- so
// ROUT:OPEN is refused. All four banks, so a bank-numbering mistake cannot
// hide in the three this script did not look at.
//
// Left with every bank on channel 01, which is this desk's defined idle: an RF
// bank stays wherever it was put, and safing cannot open it (see
// Chassis::safeRelays()).
//
auto swuRfMultiplexer() -> void
{
    for( unsigned bank = 1; bank <= 4; ++bank)
    {
        const auto one  = rfChannel( bank, 1);
        const auto four = rfChannel( bank, 4);

        Swu1.close( one);
        Verify( DEV_Swu_1::DEV_Swu_Closed, Swu1.isClosed( one));

        Swu1.close( four);
        Verify( DEV_Swu_1::DEV_Swu_Closed,    Swu1.isClosed( four));
        Verify( DEV_Swu_1::DEV_Swu_RfSelects, !Swu1.isClosed( one));

        bool refused = false;

        try
        {
            Swu1.open( four);
        }
        catch( const hal::io::ScpiFault &)
        {
            refused = true;
        }

        Verify( DEV_Swu_1::DEV_Swu_RfNoOpen, refused);

        Swu1.close( one);
    }
}

//
// A crosspoint's life count, before and after one close and one open. By one
// or two rather than exactly one, because whether the mainframe counts a cycle
// per operation or per close-and-open pair is a fact about its firmware this
// script should not have to assume -- either is a relay that was driven, and
// zero is one that was not.
//
auto swuRelayCycles() -> void
{
    const auto relay = crosspoint( 1, 1, 1);

    Swu1.openAll( 1);

    const auto before = Swu1.relayCycles( relay);

    Swu1.close( relay);
    Swu1.open( relay);

    const auto moved = Swu1.relayCycles( relay) - before;

    Verify( DEV_Swu_1::DEV_Swu_CycleCounted, moved >= 1 && moved <= 2);
}
