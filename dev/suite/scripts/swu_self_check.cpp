#include "../prelude.hpp"

#include "core/journal/journal.hpp"
#include "hal/io/scpi.hpp"

#include <string>

//
// The 34980A switch unit, tested in isolation -- the SwitchUnit group in
// dev/suite/test_catalog.inc. Nothing is wired to it: every check is a
// question the mainframe answers about itself.
//
//   inventory  what is in the eight slots, by SYST:CTYP?
//   relays     per 34921A: close, open, a list across both banks,
//              closeExclusively, the Analog Bus relays, openAll -- each checked
//              by asking ROUT:CLOS?, never by trusting what was sent
//   refusal    a channel the module does not have is an error, not a no-op
//   cycles     a relay's life count moves when it is driven
//
// What "closed" means here, and what it does not: ROUT:CLOS? reports what the
// mainframe told the relay, and DIAG:REL:CYCL? that the relay's drive changed
// state. Neither is a meter on the contacts. That is the one check this group
// cannot make, and the one that needs something wired -- a short on a
// channel's terminals and the desk DMM on the other side.
//
// With nothing wired, closing is harmless in every combination below: two
// channels of one bank closed together join two inputs that are connected to
// nothing, and an Analog Bus relay joins a channel to a backplane that nothing
// else is on (or to the internal DMM, if one is fitted and enabled, which
// reads nothing into anything). A rig with a DUT on the terminal blocks is a
// different matter, and not what these scripts are for.
//
// These call the chassis driver directly -- Swu1.close(), not Connect() --
// because the fabric does not drive a 34980A yet (see the driver README): so
// the relay moves are not journal events, and the verdicts are. Every script
// opens what it closed before it returns, and safing opens everything after
// the run regardless, in the pass after every source is off.
//
// Slot 5, the coaxial module, is only inventoried: an RF multiplexer cannot be
// opened, only switched to another channel (see Chassis::open()), and which
// channels it has depends on the model the first run will name.
//

using hal::keysight_34980a::AnalogBus;
using hal::keysight_34980a::ChannelAddress;
using hal::keysight_34980a::analogBus;

namespace
{
    //
    // What a 34921A has: forty channels in two banks of twenty (001-020 and
    // 021-040), four current channels (041-044), and an Analog Bus relay per
    // bus per bank (911-914, 921-924). Written out here rather than asked of
    // the module, because no module's channel space is modelled yet -- see
    // hal::SwitchDeviceModel.
    //
    constexpr int kFirstBankOne  = 1;
    constexpr int kLastBankOne   = 20;
    constexpr int kFirstBankTwo  = 21;
    constexpr int kLastBankTwo   = 40;
    constexpr int kExclusive     = 10;

    //
    // One whole-cycle of checks on the 34921A in slot N, from and back to all
    // open.
    //
    template<int N>
    auto relaysOn() -> void
    {
        const ChannelAddress first{ N, kFirstBankOne };
        const ChannelAddress second{ N, kFirstBankOne + 1 };

        Swu1.openAll( N);

        //
        // One relay: closed when told, its neighbour untouched, open when told.
        //
        Swu1.close( first);
        Verify( DEV_Swu_1::DEV_Swu_Closed, Swu1.isClosed( first));
        Verify( DEV_Swu_1::DEV_Swu_Open,   !Swu1.isClosed( second));

        Swu1.open( first);
        Verify( DEV_Swu_1::DEV_Swu_Open,   !Swu1.isClosed( first));

        //
        // A list, spanning both banks and both ends of each -- one ROUT:CLOS
        // with four channels in it, which is the form a range is deliberately
        // never used in place of (see the driver README, "Lists, never
        // ranges").
        //
        const std::vector<ChannelAddress> spread{
            { N, kFirstBankOne }, { N, kLastBankOne }, { N, kFirstBankTwo }, { N, kLastBankTwo } };

        Swu1.close( spread);

        for( const auto channel : spread)
        {
            Verify( DEV_Swu_1::DEV_Swu_Closed, Swu1.isClosed( channel));
        }

        //
        // closeExclusively opens everything else on the *module* -- both banks
        // -- and closes this one. So all four of the spread must be open after
        // it, not just the ones in its bank.
        //
        const ChannelAddress exclusive{ N, kExclusive };

        Swu1.closeExclusively( exclusive);

        Verify( DEV_Swu_1::DEV_Swu_Closed, Swu1.isClosed( exclusive));

        for( const auto channel : spread)
        {
            Verify( DEV_Swu_1::DEV_Swu_Open, !Swu1.isClosed( channel));
        }

        //
        // The Analog Bus relays, one per bank -- 911 and 921, ABus1 from each.
        // A 34921A has both, where a 2-wire matrix has only the second; this
        // is the check that the driver's bank arithmetic reaches both.
        //
        const auto busBankOne = analogBus( N, 1, AnalogBus::One);
        const auto busBankTwo = analogBus( N, 2, AnalogBus::One);

        Swu1.close( { busBankOne, busBankTwo });

        Verify( DEV_Swu_1::DEV_Swu_Closed, Swu1.isClosed( busBankOne));
        Verify( DEV_Swu_1::DEV_Swu_Closed, Swu1.isClosed( busBankTwo));

        //
        // And openAll for the slot takes everything with it -- a channel and
        // both bus relays.
        //
        Swu1.openAll( N);
        Swu1.waitForSwitching( N);

        Verify( DEV_Swu_1::DEV_Swu_Open, !Swu1.isClosed( exclusive));
        Verify( DEV_Swu_1::DEV_Swu_Open, !Swu1.isClosed( busBankOne));
        Verify( DEV_Swu_1::DEV_Swu_Open, !Swu1.isClosed( busBankTwo));
    }
} // namespace

//
// Slots 1-4 hold 34921As and slot 5 holds something. The model in slot 5 is
// not checked -- finding it out is what this run is for -- so every slot's
// answer is posted as a Note: "Swu1 slot 5" and whatever SYST:CTYP? said. A
// Note reaches the machine log and the console's Raw events tab, not the
// human report, which is the right place for an inventory nobody verifies.
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
        Verify( DEV_Swu_1::DEV_Swu_Module34921A, rack[ slot - 1].Model == "34921A");
    }

    Verify( DEV_Swu_1::DEV_Swu_SlotFitted, !rack[ 4].Empty);
}

auto swuRelaysSlot1() -> void { relaysOn<1>(); }
auto swuRelaysSlot2() -> void { relaysOn<2>(); }
auto swuRelaysSlot3() -> void { relaysOn<3>(); }
auto swuRelaysSlot4() -> void { relaysOn<4>(); }

//
// Channel 050 of slot 1: a well-formed (@1050), which the driver's shape check
// passes, and not a channel a 34921A has -- so it is the instrument that has
// to refuse it, and the driver that has to turn that refusal into an error
// rather than a relay that silently did not move. That second half is the
// whole reason every command here goes through checked() (see
// Chassis::close()), and this is the check that it does, on the real box.
//
auto swuRefusesAMissingChannel() -> void
{
    bool refused = false;

    try
    {
        Swu1.close( ChannelAddress{ 1, 50 });
    }
    catch( const hal::io::ScpiFault &)
    {
        refused = true;
    }

    Verify( DEV_Swu_1::DEV_Swu_Refused, refused);

    Swu1.openAll( 1);
}

//
// A relay's life count, before and after one close and one open. By one or
// two rather than exactly one, because whether the mainframe counts a cycle
// per operation or per close-and-open pair is a fact about its firmware this
// script should not have to assume -- either is a relay that was driven, and
// zero is one that was not.
//
auto swuRelayCycles() -> void
{
    const ChannelAddress relay{ 1, kFirstBankOne };

    Swu1.openAll( 1);

    const auto before = Swu1.relayCycles( relay);

    Swu1.close( relay);
    Swu1.open( relay);

    const auto moved = Swu1.relayCycles( relay) - before;

    Verify( DEV_Swu_1::DEV_Swu_CycleCounted, moved >= 1 && moved <= 2);
}
