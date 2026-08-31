#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "core/meta.hpp"

#include "hal/driver/address.hpp"

namespace hal
{
    //
    // Which kind of switching hardware a device is -- what it does in a signal
    // chain, not which card it is (SwitchDeviceModel below is that, and states
    // this alongside it, so no rig writes the two down separately and gets
    // them to disagree).
    //
    // RfMux is a genuinely separate physical device from Mux, not just a
    // naming distinction: RF signal paths need controlled impedance
    // (typically 50 ohm) end to end, which an LF matrix/mux card's relays
    // aren't built to preserve -- an RF signal routed through an LF mux
    // would see reflections/loss a real RF measurement can't tolerate. So
    // the two never share hardware, and a route through the fabric mixes
    // them only in the sense that hal::Path can hold hops on either kind of
    // device in the same chain -- e.g. an LF mux hop narrowing down to a
    // matrix column is a different case from an RF signal's own, separate
    // RfMux chain; nothing here enforces that an RF point is only ever
    // reached through RfMux hops (see this project's decision not to build a
    // compile-time-checked point-category gate for that kind of DUT-specific
    // judgment call) -- that's left to whoever wires the rig's wiring.inc
    // (rig/wiring.inc in this repo) and writes the test getting it right,
    // the same way it already is for Matrix vs Mux today.
    //
    // Spdt and Spst are the two plain relay cards, and they are here for the
    // same reason RfMux is: a rack has them, and calling one a Mux would be a
    // sentence about hardware that does not exist. Neither routes anything to
    // anything -- there is no common, no fan-in, nothing to select between on
    // the Spst -- they make and break one connection each, which is what an
    // isolation relay in a supply's output lead or a changeover on a console
    // line actually is. A Mux channel connects its input to a common shared
    // with every other channel on that card; an Spst channel connects the two
    // ends of its own pair and nothing else, so two Spst channels closed at
    // once are two unrelated circuits where two Mux channels closed at once
    // are one short. Kind is what says which of those a rig just wrote down.
    //
    // Purely descriptive as far as this library is concerned -- every kind is
    // commanded identically here (open/close a channel) -- and kept so a
    // route's path reads clearly and so error messages can say "Matrix1
    // channel 300" rather than an opaque pair.
    //
    enum class SwitchDeviceKind
    {
        Matrix, // crosspoint array -- rows and columns, a relay at each meeting
        Mux,    // one common, many channels -- closing two shorts them together
        RfMux,  // the same, in controlled impedance, for signals an LF relay would spoil
        Spdt,   // Form C changeover -- one common, a normally-closed and a normally-open contact
        Spst    // Form A make/break -- one connection, made or not
    };

    //
    // Which card a device actually is. Not decoration: a card model is what
    // fixes the channel numbers a rig may legally write (hasChannel below),
    // which is the difference between "channel 300" meaning group 0 row 3
    // column 00 of a matrix and meaning nothing at all on an 80-channel relay
    // card, and it is what a future per-card driver would be constructed
    // from -- see detail::SwitchCardSpec.
    //
    // Model rather than kind is the column a rig writes, and kind comes from
    // the model, because the two can't then disagree: an Agilent E1472A is an
    // RfMux, always, on every rig that owns one, and a rig saying otherwise
    // was stating a fact it doesn't get a vote on. What a rig does get a vote
    // on is which cards it has and where they are.
    //
    // Note what this costs: a rig with a card this list doesn't name has to
    // add it here, in generic hal, which is the one place rig/README.md
    // otherwise promises a new rig never has to touch. That is the same trade
    // hal::SwitchDeviceKind already makes one level coarser, and it is
    // deliberate at this size -- a spec row is four constants and a
    // predicate. The seam if it stops being deliberate is the one
    // instruments/ already draws: when these cards get drivers (a real matrix
    // and a real RF selector do not speak the same commands), each model
    // becomes its own directory with its own header, and this enum becomes
    // the tokens naming them, exactly as rig/instrument.inc names driver
    // types today.
    //
    enum class SwitchDeviceModel
    {
        Racal1260_45,   // LF matrix       -- quad 4 x 16, latching, 25 MHz
        Racal1260_35,   // LF multiplexer  -- 1 x 96 two-wire scanner, 50 MHz
        Racal1260_17,   // LF SPDT         -- 80 Form C relays, 60 MHz
        Racal1260_18,   // LF SPST         -- 152 Form A relays, 2 A
        AgilentE1472A   // HF multiplexer  -- six 1 x 4 banks, 50 ohm, 1.3 GHz
    };

    //
    // Which card in a chassis, for the chassis kinds that address their cards
    // *inside* the protocol rather than on the bus. A Racal 1260's switching
    // cards sit behind one Option 01/01T smart controller in the leftmost
    // slot: the PC opens one GPIB address -- the controller's -- and says
    // `CLOSE 3.0115`, where the leading 3 is the card's own address, set on
    // SW1 on that card's PCB. So four cards is one Address and four card
    // numbers, not four addresses.
    //
    // Which is exactly the shape rig/instrument.inc's DcP1..DcP4 already
    // have, and it is recorded the same way for the same reason: their
    // mainframe slot is a separate column rather than a field on hal::Gpib,
    // because it is not part of how the PC reaches the box (see
    // hal/driver/address.hpp, and hal::keysight_n6701a::N6701A's own comment on its slot argument).
    // Writing a 1260 card number into hal::Gpib::secondary would read as a
    // 488 secondary address, which is a different thing the bus itself
    // understands, and would be wrong on the wire the day a driver believed
    // it.
    //
    // NoCard is the honest answer for a device whose address already reaches
    // it on its own -- an ordinary VXI servant behind a command module, say,
    // whose logical address the command module has already mapped to a real
    // GPIB secondary. Not "card 0", which is a card.
    //
    using CardAddress = std::optional<int>;

    inline constexpr CardAddress NoCard{};

    [[nodiscard]]
    constexpr auto Card( const int address) -> CardAddress
    {
        return address;
    }

    //
    // The fixed, static list of switching devices a rig actually has -- the
    // matrix cards, LF muxes, relay cards and RF selectors its signal chain
    // is built from. Generated from THORIUM_DEVICE_TABLE, the linking rig's
    // own devices.inc (see rig/devices.inc for this repo's), exactly the way
    // hal::InstrumentId comes from THORIUM_INSTRUMENT_TABLE -- this header is
    // generic and shared by every rig, so the enumerators (Matrix1, Mux1,
    // ...) are not listed here at all.
    //
    // These are deliberately NOT hal::InstrumentId enumerators, and that is
    // the whole reason for a second enum rather than a bigger first one. A
    // switching device is plumbing: it has no quantity type and nothing to
    // read, so the type system can never let a script try to Measure a mux
    // channel (see hal::SwitchElementId's own comment in
    // hal/fabric/switch_fabric.hpp, which has said as much since long before this
    // enum existed). InstrumentId is what a recorded sample and a wiring
    // entry identify a *reading* by; a mux channel produces no readings.
    // Folding the two lists together would hand every script-facing
    // Measure/Apply overload a set of ids that cannot answer them.
    //
    // The same split is already visible in how the globals are declared:
    // rig/active_instruments.hpp declares Dmm1 unqualified because a test
    // script writes that name itself, and hal::fabric qualified because
    // nothing but the measure/apply assembly ever names it.
    //
    // What replaced what: SwitchElementId used to carry a bare
    // std::string_view device name, so wiring.inc named cards in strings that
    // nothing validated. HOP( Matrix, "Matrix22", 14) -- a typo -- was
    // accepted, and the fabric would create that element, close it, open it,
    // and route nothing, because the fabric had no idea which cards existed.
    // Worse, kind rode on every hop rather than on the card, so
    // HOP( Matrix, "Matrix2", 14) and HOP( Mux, "Matrix2", 14) were two
    // distinct SwitchElementIds for one physical crosspoint, each with its own
    // use count in hal::SwitchFabric -- connect through one and disconnect
    // through the other and the relay never opened, with nothing complaining.
    // Declaring each device once, here, makes both unrepresentable rather than
    // merely unlikely.
    //
#pragma push_macro( "SWITCH_DEVICES")
#pragma push_macro( "SWITCH_DEVICE")
#pragma push_macro( "END_SWITCH_DEVICES")
#undef SWITCH_DEVICES
#undef SWITCH_DEVICE
#undef END_SWITCH_DEVICES

#define SWITCH_DEVICES
#define SWITCH_DEVICE( model, id, address, card) id,
#define END_SWITCH_DEVICES

    enum class SwitchDeviceId
    {
        #include THORIUM_DEVICE_TABLE
    };

#pragma pop_macro( "END_SWITCH_DEVICES")
#pragma pop_macro( "SWITCH_DEVICE")
#pragma pop_macro( "SWITCH_DEVICES")

    [[nodiscard]]
    auto to_string( SwitchDeviceId device) -> std::string_view;

    //
    // The enumerator's own spelling ("Racal1260_45"), for a log line that has
    // a model in hand rather than a device. What the datasheet calls the card
    // is partOf() below -- one is an identifier this codebase chose, the
    // other is a part number a purchasing department would recognise, and a
    // journal wants the second.
    //
    [[nodiscard]]
    auto to_string( SwitchDeviceModel model) -> std::string_view;

    namespace detail
    {
        //
        // What a card model is, as far as this library needs to know: what to
        // call it, what it does, and which channel numbers exist on it.
        //
        // The channel numbers are the part that earns this table. Every card
        // here numbers its channels the way its own manual does, and those
        // schemes have nothing in common -- 0 to 151 flat on a 1260-18,
        // <group><row><column> on a 1260-45, <bank><channel> on an E1472A --
        // so "is 300 a channel" is a question only the model can answer, and
        // one nothing could answer at all while a device was a name and a
        // kind. A rig that mistypes one now fails to build; before, it
        // produced a fabric element for a relay that does not exist, closed
        // it, opened it, and routed nothing (see hal::hop in
        // hal/fabric/switch_fabric.hpp for where the check actually fires).
        //
        // Crosspoint/BankChannel are the same fact in the other direction:
        // the composite numbering schemes, written as the parts they are made
        // of. Null on a card that has no such scheme, which is what makes
        // CROSSPOINT( Mux1, 0, 3, 0) a compile error rather than an
        // arithmetic accident. See hal::crosspoint/hal::bank.
        //
        // A struct of constants and function pointers rather than a class
        // hierarchy, because there is still nothing for a per-card *object*
        // to do: hal::SwitchFabric addresses channels, not cards. When a real
        // matrix card and a real RF selector need their own drivers -- they
        // do not speak the same commands -- this is the table those drivers
        // get constructed from.
        //
        struct SwitchCardSpec
        {
            std::string_view Part;
            SwitchDeviceKind Kind;

            //
            // How this card's manual writes a channel, for the message a
            // failed check prints. Prose on purpose: the shapes are too
            // unlike each other for a common structured form to say more
            // than the sentence does.
            //
            std::string_view Channels;

            bool          ( * HasChannel)( std::uint16_t);
            std::uint16_t ( * Crosspoint)( unsigned group, unsigned row, unsigned column);
            std::uint16_t ( * BankChannel)( unsigned bank, unsigned channel);
        };

        //
        // Racal Instruments 1260-45A: four independent 4 x 16 matrices on one
        // C-size card, written <group><row><column> -- group and row a single
        // digit 0-3, column two digits 00-15, so 2312 is group 2, row 3,
        // column 12 and the whole card is 0000 through 3315 with holes.
        //
        constexpr auto racal1260_45HasChannel( const std::uint16_t channel) -> bool
        {
            return  channel                <= 3315
                && ( channel / 1000)       <= 3
                && (( channel / 100) % 10) <= 3
                && ( channel % 100)        <= 15;
        }

        constexpr auto racal1260_45Crosspoint( const unsigned group, const unsigned row, const unsigned column) -> std::uint16_t
        {
            return static_cast<std::uint16_t>( group * 1000 + row * 100 + column);
        }

        //
        // Racal Instruments 1260-35: a 1 x 96 two-wire scanner. 96 is the
        // hi/lo selector that turns it into a 1 x 192 one-wire scanner, and
        // is a channel only in that configuration -- the four-wire
        // configurations stop at 47. This accepts the widest set the card can
        // present, so passing it means "this card could have that channel",
        // not "this card, wired as this rig wired it, does". Which
        // configuration a rig ordered is a rig fact and belongs in the
        // comment beside the card in its devices.inc.
        //
        constexpr auto racal1260_35HasChannel( const std::uint16_t channel) -> bool
        {
            return channel <= 96;
        }

        //
        // Racal Instruments 1260-17: 80 Form C relays, numbered flat.
        //
        constexpr auto racal1260_17HasChannel( const std::uint16_t channel) -> bool
        {
            return channel <= 79;
        }

        //
        // Racal Instruments 1260-18: 152 Form A relays, numbered flat.
        //
        constexpr auto racal1260_18HasChannel( const std::uint16_t channel) -> bool
        {
            return channel <= 151;
        }

        //
        // Agilent E1472A: six independent 1 x 4 banks, written
        // <bank><channel> -- bank 0-5, channel 0-3, so 00-03, 10-13, ... 50-53
        // and nothing else. Only one channel per bank reaches its common at a
        // time, which is a fact about the card that this predicate cannot
        // state and hal::SwitchFabric does not model (see hal::SwitchFabric's
        // own comment): the fabric will happily hold two channels of one bank
        // closed, and the card will not.
        //
        constexpr auto agilentE1472AHasChannel( const std::uint16_t channel) -> bool
        {
            return ( channel / 10) <= 5 && ( channel % 10) <= 3;
        }

        constexpr auto agilentE1472ABankChannel( const unsigned bank, const unsigned channel) -> std::uint16_t
        {
            return static_cast<std::uint16_t>( bank * 10 + channel);
        }

        //
        // Indexed by SwitchDeviceModel's own value, the same way
        // switchDevices below is indexed by SwitchDeviceId's -- so the order
        // here is the order there, and the static_assert under it says so.
        //
        inline constexpr SwitchCardSpec cardSpecs[] =
        {
            { "Racal Instruments 1260-45A", SwitchDeviceKind::Matrix,
              "0000-3315, written <group><row><column> (group 0-3, row 0-3, column 00-15)",
              racal1260_45HasChannel, racal1260_45Crosspoint, nullptr },

            { "Racal Instruments 1260-35",  SwitchDeviceKind::Mux,
              "0-95 two-wire (0-96 one-wire, 0-47 four-wire)",
              racal1260_35HasChannel, nullptr, nullptr },

            { "Racal Instruments 1260-17",  SwitchDeviceKind::Spdt,
              "0-79",
              racal1260_17HasChannel, nullptr, nullptr },

            { "Racal Instruments 1260-18",  SwitchDeviceKind::Spst,
              "0-151",
              racal1260_18HasChannel, nullptr, nullptr },

            { "Agilent E1472A",             SwitchDeviceKind::RfMux,
              "00-03, 10-13, 20-23, 30-33, 40-43, 50-53, written <bank><channel>",
              agilentE1472AHasChannel, nullptr, agilentE1472ABankChannel }
        };

        static_assert( std::size( cardSpecs) == core::meta::values<SwitchDeviceModel>.size(),
                       "every hal::SwitchDeviceModel enumerator needs a spec row, in the same order");

        //
        // The three facts each declared device carries besides its own
        // identity: which card it is, where the PC reaches it, and -- for a
        // chassis that addresses its cards inside the protocol -- which card
        // in that chassis it is. See hal::CardAddress above for why the last
        // is its own column rather than a field on the address.
        //
        struct SwitchDeviceInfo
        {
            SwitchDeviceModel Model;
            Address           Where;
            CardAddress       Card;
        };

        //
        // devices.inc read a second time, with SWITCH_DEVICE redefined to
        // build one row each. Two reads of one file rather than one, and
        // unlike the reads reflection replaced elsewhere in this codebase
        // (see core::meta::all() in core/criteria/criterion.hpp, hal::safeRig()) this
        // second one cannot go the same way: reflection over SwitchDeviceId
        // can recover an enumerator's *name*, which is all safeRig() needed,
        // but a device's model, address and card number are data the table
        // states and nothing about the enum implies. There is nothing to
        // reflect them out of.
        //
        // Both reads see the same rows in the same order, which is what makes
        // indexing this array by the enumerator's own value correct. The
        // static_assert below is the guard on that -- it fires if the two
        // expansions ever disagree on how many rows there were.
        //
#pragma push_macro( "SWITCH_DEVICES")
#pragma push_macro( "SWITCH_DEVICE")
#pragma push_macro( "END_SWITCH_DEVICES")
#undef SWITCH_DEVICES
#undef SWITCH_DEVICE
#undef END_SWITCH_DEVICES

#define SWITCH_DEVICES
#define SWITCH_DEVICE( model, id, address, card) \
    SwitchDeviceInfo{ SwitchDeviceModel::model, hal::address, hal::card },
#define END_SWITCH_DEVICES

        //
        // std::array sized from the enum, not a plain `SwitchDeviceInfo[]`,
        // and the reason is a rig with no switching hardware at all: a
        // dev bench that is a PC and one instrument declares the table with
        // no rows, and `SwitchDeviceInfo switchDevices[]` is then a
        // zero-length array -- a GCC extension rather than C++, which
        // std::size() below has no overload for. The deduced-bound form
        // could not express "this rig has no cards" at all, which is a thing
        // a rig is entitled to say (see dev/rig/devices.inc).
        //
        // Taking the bound from SwitchDeviceId also moves half the count
        // check from the static_assert into the declaration: too many rows in
        // the second read is now an excess-initializers error at this line.
        // The assert stays for the other half and for its message, which
        // names what actually went wrong where "too many initializers"
        // does not.
        //
        inline constexpr std::array<SwitchDeviceInfo, core::meta::values<SwitchDeviceId>.size()> switchDevices =
        {
            #include THORIUM_DEVICE_TABLE
        };

#pragma pop_macro( "END_SWITCH_DEVICES")
#pragma pop_macro( "SWITCH_DEVICE")
#pragma pop_macro( "SWITCH_DEVICES")

        static_assert( switchDevices.size() == core::meta::values<SwitchDeviceId>.size(),
                       "THORIUM_DEVICE_TABLE's two expansions disagree on how many devices this rig has");
    } // namespace detail

    //
    // Which card a declared device is, and everything that follows from it.
    //
    // kindOf() is why SwitchElementId no longer carries a kind of its own,
    // and why the rig's table no longer carries one either: the kind is a
    // property of the card model, so neither a hop nor a rig can contradict
    // the datasheet about it.
    //
    // addressOf() is the fabric's half of what hal/driver/address.hpp does for
    // instruments, and closes the gap that header's own comment named:
    // closing a relay on real hardware is a GPIB/VXI write to a card, which
    // has to be addressed exactly as an instrument does. cardOf() is the
    // second half of that for a chassis whose cards share one address -- see
    // hal::CardAddress. Nothing reads either yet, for the same reason nothing
    // reads an instrument's address yet -- hal::SwitchFabric only tracks
    // relay state -- but a rig can now state the fact, which it could not
    // before.
    //
    [[nodiscard]]
    constexpr auto modelOf( const SwitchDeviceId device) -> SwitchDeviceModel
    {
        return detail::switchDevices[ static_cast<std::size_t>( device)].Model;
    }

    [[nodiscard]]
    constexpr auto specOf( const SwitchDeviceModel model) -> const detail::SwitchCardSpec &
    {
        return detail::cardSpecs[ static_cast<std::size_t>( model)];
    }

    [[nodiscard]]
    constexpr auto kindOf( const SwitchDeviceId device) -> SwitchDeviceKind
    {
        return specOf( modelOf( device)).Kind;
    }

    [[nodiscard]]
    constexpr auto addressOf( const SwitchDeviceId device) -> const Address &
    {
        return detail::switchDevices[ static_cast<std::size_t>( device)].Where;
    }

    [[nodiscard]]
    constexpr auto cardOf( const SwitchDeviceId device) -> CardAddress
    {
        return detail::switchDevices[ static_cast<std::size_t>( device)].Card;
    }

    //
    // The card's part number ("Racal Instruments 1260-45A") and the sentence
    // describing how it numbers its channels -- both for the humans reading a
    // failed check or a journal, and both stated once per model rather than
    // once per rig.
    //
    [[nodiscard]]
    constexpr auto partOf( const SwitchDeviceId device) -> std::string_view
    {
        return specOf( modelOf( device)).Part;
    }

    [[nodiscard]]
    constexpr auto channelsOf( const SwitchDeviceId device) -> std::string_view
    {
        return specOf( modelOf( device)).Channels;
    }

    //
    // Is this a channel the card actually has? The question rig/wiring.inc
    // could not ask before there was a model column, and the reason it is
    // worth asking: an invented channel is not a route that fails, it is a
    // route that closes something else, or nothing, and reports success
    // either way.
    //
    // Necessary, not sufficient -- see racal1260_35HasChannel's own comment.
    // A card ordered in one of its narrower configurations has fewer channels
    // than its widest, and which one a rig ordered is not a fact this table
    // holds.
    //
    [[nodiscard]]
    constexpr auto hasChannel( const SwitchDeviceId device, const std::uint16_t channel) -> bool
    {
        return specOf( modelOf( device)).HasChannel( channel);
    }
} // namespace hal
