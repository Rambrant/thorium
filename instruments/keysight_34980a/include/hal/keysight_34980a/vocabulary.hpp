#pragma once

#include <cstdint>
#include <functional>
#include <type_traits>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "core/driver/port.hpp"
#include "core/quantities/quantity.hpp"

#include "hal/driver/address.hpp"
#include "hal/driver/box_connection.hpp"
#include "hal/driver/instrument.hpp"
#include "hal/io/scpi.hpp"

//
// The mainframe's vocabulary: slots, channel addresses, the Analog Bus, what SYST:CTYP? says is in a slot. Shared by both faces.
//
// Part of hal/keysight_34980a.hpp -- include that, not this.
//

namespace hal::keysight_34980a
{
    //
    // How many slots the mainframe has. Eight, and it is a fact about the
    // chassis rather than a configuration -- an empty slot is still a slot, and
    // answers SYST:CTYP? with "Agilent Technologies,0,0,0" (see moduleIn()).
    //
    inline constexpr int kSlots = 8;

    //
    // Is this a slot the mainframe has, and is this a channel number it can
    // address at all?
    //
    // Predicates rather than only the static_asserts inside channel<>() below,
    // and the reason is the one hal::hasChannel already demonstrates: a
    // static_assert in a function body is a diagnostic, not a constraint, so
    // nothing can detect it -- a test cannot assert that channel<9, 3>() fails
    // without failing the build, and a concept wrapped around it reports
    // "satisfied" whether the assertion would fire or not. What is testable in
    // both directions is a predicate, so that is what the checks are written
    // in terms of and what a test asserts against (see this driver's tests,
    // which check both, and rig/tests/test_switch_device.cpp, which does the
    // same for hal::hasChannel).
    //
    // isAddressableChannel is a question about the *channel list format* and
    // nothing more: "(@sccc)" leaves three digits for a channel, so 1000 cannot
    // be written and 999 can. Whether the module in a given slot actually has
    // channel 999 is a different question, and one no module is modelled to
    // answer yet -- see this file's preamble.
    //
    [[nodiscard]]
    constexpr auto isSlot( const int slot) -> bool
    {
        return slot >= 1 && slot <= kSlots;
    }

    [[nodiscard]]
    constexpr auto isAddressableChannel( const int channel) -> bool
    {
        return channel >= 0 && channel <= 999;
    }

    //
    // A slot and a channel on the module in it -- the two halves of the
    // "(@sccc)" channel list every switching command takes.
    //
    // An aggregate rather than an opaque handle, because the runtime path is
    // the one that matters: when hal::SwitchFabric grows a transport it will
    // arrive here holding a hal::SwitchElementId, whose channel is a
    // std::uint16_t decided by a rig's wiring table. So the values have to be
    // constructible at runtime, and validate() below is what checks them. The
    // compile-time spelling is channel<Slot, Number>(), for the call sites
    // where both are literals.
    //
    struct ChannelAddress
    {
        int Slot{};
        int Number{};

        friend constexpr auto operator==( ChannelAddress, ChannelAddress) -> bool = default;
        friend constexpr auto operator<=>( ChannelAddress, ChannelAddress) = default;
    };

    //
    // "(@3010)" -- one channel, written the way the mainframe reads it.
    //
    // Its own function rather than a member of ChannelAddress, so that the
    // format lives beside the commands that use it (see the .cpp) and so a
    // caller can build a multi-channel list without this type growing a
    // container.
    //
    [[nodiscard]]
    auto channelList( ChannelAddress channel) -> std::string;

    //
    // "(@1005,1013,2015)" -- several channels in one command.
    //
    // Worth having as its own spelling, because on this box it is not merely a
    // convenience. The programmer's reference is explicit that a multi-channel
    // ROUT:CLOS on the 34925A FET multiplexer is executed break-before-make
    // within each bank, and that a 34934A matrix switches measurably faster
    // given a list than given the same channels one command at a time. One
    // command is also one error-queue check rather than N.
    //
    // The order is preserved, deliberately: it is the order the instrument
    // operates in, which on a break-before-make module is the difference
    // between which channel is left closed.
    //
    [[nodiscard]]
    auto channelList( const std::vector<ChannelAddress> & channels) -> std::string;

    //
    // The compile-time spelling, for a call site where both numbers are
    // literals -- the same shape hal::hop<Device, Channel>() has in
    // hal/fabric/switch_fabric.hpp, and for the same reason: a template
    // argument is the only place a value is a constant, and a constant is the
    // only thing anything can be checked about.
    //
    // Note what is and is not checked, because the asymmetry is the honest
    // statement of what this driver knows. The slot is checked against the
    // chassis, which this driver does know. The channel is checked for *shape*
    // -- three digits, so that it composes into a well-formed "sccc" -- and not
    // for existence, because whether the module in slot 3 has a channel 010 is
    // a module fact and no module is modelled yet. See validate().
    //
    template<int SlotNumber, int ChannelNumber>
    [[nodiscard]]
    constexpr auto channel() -> ChannelAddress
    {
        static_assert( isSlot( SlotNumber),
                       "not a slot of a 34980A -- the mainframe has slots 1 through 8");
        static_assert( isAddressableChannel( ChannelNumber),
                       "not a channel number a 34980A can address -- a channel list is "
                       "(@sccc), so the channel is at most three digits");

        return ChannelAddress{ SlotNumber, ChannelNumber };
    }

    //
    // Which of the four Analog Buses.
    //
    // These are the mainframe's own backplane: two-wire buses running behind
    // all eight slots, and how a multiplexer or matrix channel reaches the
    // internal DMM without a cable.
    //
    // Numbered rather than named, because the number is what goes on the wire
    // -- see analogBus() below for how it composes.
    //
    // Two rules about them are easy to trip over and neither is visible in a
    // channel number:
    //
    //   they are ignored inside a *range*. "(@1030:2005)" silently skips every
    //   Analog Bus relay it spans, and naming one as the first or last channel
    //   of a range is an error outright. This driver never writes ranges (see
    //   channelList above, which writes explicit lists), which sidesteps the
    //   whole rule -- and that is a reason to keep writing lists.
    //
    //   ABus1 and ABus2 are commandeered by a scan. Initiating one opens every
    //   ABus1 relay in every slot, and every ABus2 relay too if any scanned
    //   channel is 4-wire, and refuses manual use of them until the scan ends.
    //   ABus3 and ABus4 stay live -- which the reference warns is a way to join
    //   a stray signal onto a measurement in progress.
    //
    enum class AnalogBus : int
    {
        One   = 1,
        Two   = 2,
        Three = 3,
        Four  = 4
    };

    //
    // The Analog Bus relay for one bus, on one *bank*, in one slot -- channel
    // 9<bank><bus>, so bank 1's four are 911-914 and bank 2's are 921-924.
    //
    // The bank argument is the whole point of this function, and getting it
    // wrong is a mistake worth describing because this driver made it first: an
    // earlier version hard-coded 911-914 and so could only ever name bank 1's
    // relays. A module has one Analog Bus relay per bus *per bank*, and which
    // bank matters:
    //
    //   on a multiplexer module both banks reach the buses, so both sets exist
    //   and a rig picks the one its channel is in.
    //
    //   on a *matrix* module in two-wire mode -- which is what a 34931A/34932A
    //   is, and what this rig is migrating onto (see hal::SwitchDeviceModel's
    //   Keysight34932A) -- **only Matrix 2 connects to the Analog Buses**. So
    //   the only relays that exist are 921-924, and 911 is not a channel of
    //   that module at all. Asking for bank 1 there produces a command the
    //   instrument refuses, which is the honest outcome: this driver models the
    //   chassis, and which banks a module exposes is the module's business (see
    //   hal::hasChannel, and this file's preamble on the missing half of the
    //   channel check).
    //
    //   on an RF multiplexer (34941A/34942A) there are none at any number: the
    //   RF modules do not connect to the Analog Buses, and every signal
    //   connection is an external cable. So an RF path cannot reach the
    //   internal DMM, however the buses are switched.
    //
    // On a 34932A the four buses land on rows 5 through 8 -- ABus1 on row 5,
    // ABus2 on row 6, and so on -- which is Matrix 2 again, seen from the row
    // axis rather than the bank one.
    //
    [[nodiscard]]
    constexpr auto analogBus( const int slot, const int bank, const AnalogBus bus) -> ChannelAddress
    {
        return ChannelAddress{ slot, 900 + bank * 10 + static_cast<int>( bus) };
    }

    //
    // A slot that is not one of the chassis's eight.
    //
    // Thrown by the runtime path, where a bad slot arrives as a value rather
    // than as a template argument -- from a wiring table, or from a fabric that
    // has computed one. The compile-time path (channel<Slot, Number>() above)
    // never reaches this: it fails to build instead.
    //
    // A logic_error, because it is one: nothing about the bench changed, a
    // caller asked for a slot the mainframe does not have.
    //
    class NoSuchSlot : public std::out_of_range
    {
        public:
            explicit NoSuchSlot( const int slot) :
                std::out_of_range(
                    "34980A: slot " + std::to_string( slot)
                    + " does not exist -- the mainframe has slots 1 through " + std::to_string( kSlots))
            {}
    };

    //
    // What SYST:CTYP? says is in a slot.
    //
    // Four fields because that is what the instrument returns -- vendor, model,
    // serial, firmware revision -- and Empty because the instrument's way of
    // saying "nothing here" is to answer with zeros in the last three fields
    // rather than with an error. A caller checking Model against a part number
    // would otherwise have to know that "0" means empty.
    //
    // This is the closest thing the box has to self-description, and it is
    // worth more here than an *IDN? is on an instrument: what a 34980A *is*
    // depends entirely on what is plugged into it, so a bring-up run that
    // prints these eight answers has learned the rack. It is also what checks
    // a rig's card rows against the hardware, at preflight (see cardModel()) --
    // a hal::SwitchDeviceModel saying 34932A in slot 1 against a slot 1 that
    // answers 34921A is a table describing a different rack, which is exactly
    // the mistake an instrument driver's identity check exists to catch.
    //
    struct ModuleIdentity
    {
        std::string Vendor;
        std::string Model;
        std::string Serial;
        std::string Firmware;
        bool        Empty{ false };

        friend auto operator==( const ModuleIdentity &, const ModuleIdentity &) -> bool = default;
    };

} // namespace hal::keysight_34980a
