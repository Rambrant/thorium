#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "hal/driver/address.hpp"
#include "hal/driver/api_version.hpp"
#include "hal/io/scpi.hpp"

//
// Which hal API version this driver was written against -- a literal, never
// THORIUM_HAL_API_VERSION itself, which would only assert that this hal matches
// this hal. See hal/driver/api_version.hpp for what the number means and when
// it moves, and instruments/README.md for why a driver package has to say this
// at all (it travels separately from the hal it compiles against).
//
// Three, for hal/io/. Note what this driver does NOT include and therefore does
// not need: hal/driver/instrument.hpp. See this file's own comment on why.
//
THORIUM_REQUIRE_HAL_API( 3);

//
// This driver's own namespace, nested inside hal -- see instruments/README.md
// for the rule and the collision that produced it. The name carries the
// manufacturer as well as the model, and it follows the company as it is today
// rather than the badge on the front panel: this mainframe shipped as an
// Agilent product, its programmer's reference still says Agilent on every page,
// and a rack may well hold one of each badge. Same treatment the DSO8064A,
// L4411A and N6701A already get.
//
namespace hal::keysight_34980a
{
    //
    // ===================================================================
    // The first switch-device driver in this tree, and a Janus
    // ===================================================================
    //
    // Everything under instruments/ so far has been an *instrument* -- a thing
    // a script names, that measures or sources, carrying an hal::InstrumentId.
    // This is not one, and the difference is the whole reason this file needs a
    // preamble rather than just a class.
    //
    // A Keysight 34980A is a switch/measure mainframe: eight slots of plug-in
    // switching modules, and an optional internal 6.5-digit DMM. So it is
    // exactly the box hal/fabric/switch_device.hpp's own comment predicted
    // would arrive one day, and it predicted this directory too:
    //
    //     "The seam if it stops being deliberate is the one instruments/
    //      already draws: when these cards get drivers (a real matrix and a
    //      real RF selector do not speak the same commands), each model
    //      becomes its own directory with its own header, and this enum
    //      becomes the tokens naming them, exactly as rig/instrument.inc names
    //      driver types today."
    //
    // -- Why the *mainframe* is the driver, and a module is not --------------
    //
    // Because that is how the box divides the work, and the division is the
    // single most useful thing to understand about it.
    //
    // Every switching command is a *mainframe* command. "ROUT:CLOS (@3010)" is
    // sent to the chassis, not to the module in slot 3, and the chassis speaks
    // the same three commands whatever is plugged into it. What a module
    // contributes is which "ccc" are legal, and what they mean -- a 34932A's
    // 2304 is row 3, column 4 of a matrix; a 34937A's is a relay number.
    //
    // Which maps onto the split this codebase already has, exactly:
    //
    //     the mainframe   is the thing that talks: one address, one session,
    //                     ROUT:CLOS/ROUT:OPEN. That is this file.
    //
    //     a module        is a channel space: hal::SwitchDeviceModel plus a
    //                     hal::detail::SwitchCardSpec, and a slot to sit in.
    //                     That is generic hal, and it is deliberately NOT here
    //                     yet -- see "What is deliberately not here" below.
    //
    // And it is the same shape the Racal rack already has, which is worth
    // noticing before assuming this box is a new kind of thing: four 1260 cards
    // sit behind one Option 01T controller, the PC opens the *controller's* one
    // GPIB address, and the card number rides inside the command
    // ("CLOSE 3.0115"). One talker, several channel spaces. A 34980A is that
    // with the card number renamed to a slot and the protocol modernised.
    //
    // -- The structural gap this exposes, stated rather than papered over ----
    //
    // There is no id for a chassis. hal::SwitchDeviceId's enumerators come from
    // a rig's devices.inc, one per *card*, and a mainframe is not a card: it
    // has no channels of its own. So this class carries no id at all -- no
    // hal::InstrumentId (it measures nothing, see below) and no
    // hal::SwitchDeviceId (it switches nothing by itself).
    //
    // That is not an oversight in this driver, it is the next question for the
    // fabric, and this repo has already predicted the answer -- in
    // rig/instrument.inc, about the equivalent case on the instrument side: "a
    // named constant above this table per chassis, not a slot field on the
    // address." A rig with a 34980A writes one address constant, and every
    // module row in that chassis repeats it -- precisely as DcP1..DcP4 repeat
    // their mainframe's address today, and DcP5..DcP7 repeat theirs. See
    // rig/devices.inc, which now records that as this rack's destination.
    //
    // What still has to be built before any of that switches a real relay:
    // hal::SwitchFabric has no transport seam. Its close()/open() increment a
    // use count, and its own comment says so ("On real hardware close()/open()
    // would be GPIB/VXI writes to the relevant card"). This class is what those
    // writes will go through; nothing calls it from the fabric yet, and making
    // that call is a change to generic hal rather than to this directory.
    //
    // -- Not an InstrumentTag, and that is deliberate ------------------------
    //
    // hal::InstrumentTag is what hal::safeRig() reflects over, and inheriting
    // it is how a driver opts into being safed. This class does not, for the
    // reason hal/fabric/switch_device.hpp gives at length: a switching device
    // is plumbing, and hal::InstrumentId is what a *reading* is identified by.
    // Adding the tag to get safing would make every script-facing Measure and
    // Apply overload accept a chassis.
    //
    // Which leaves a real gap, and it is worth naming precisely because it is
    // the one thing a bench engineer would ask about. hal::safeRig() ends by
    // calling hal::fabric.openAll(), and that is bookkeeping -- so on a rig
    // whose switching is a 34980A, a failed run leaves the actual relays where
    // it left them. openAll() below is the command that fixes it, and the day
    // the fabric talks is the day safing reaches the switching. See that
    // member.
    //
    // -- The Janus half that is not here yet ---------------------------------
    //
    // The internal DMM is an ordinary 6.5-digit meter sharing this chassis's
    // address, and when it arrives it will be an hal::InstrumentId row in
    // rig/instrument.inc pointing at a type in *this* package -- one directory
    // holding a switch-device driver and an instrument driver, because one box
    // is both.
    //
    // The one thing that must not happen when it does: a second session. Two
    // objects opening two connections to one mainframe is two error queues,
    // two identity checks, and a *RST from one arriving in the middle of the
    // other's switching. So the seam is already drawn -- this class owns the
    // session and hands out a reference (see session()), and the DMM face will
    // be constructed from a Chassis & rather than from an hal::Address. What is
    // here now for it: internalDmmInstalled(), internalDmmEnabled() and
    // setInternalDmm(), which are mainframe commands and belong on this side
    // regardless.
    //
    // -- What is deliberately not here ---------------------------------------
    //
    // module channel spaces   The point of the exercise, deferred on purpose:
    //                         which modules this rack has is a bench fact
    //                         nobody has yet, and each one is a channel space
    //                         in generic hal (a hal::SwitchDeviceModel
    //                         enumerator and a spec row). Until then a channel
    //                         is checked for shape and not for existence --
    //                         see channelList() and the note on validate()
    //                         about which half of the check is missing and
    //                         what catches it instead.
    //
    // scanning                ROUT:SCAN, INIT, FETCh? -- the mainframe's own
    //                         sequencer, which walks a list of multiplexer
    //                         channels taking a DMM reading at each. It is the
    //                         box's headline feature and it belongs to the DMM
    //                         face, not this one: a scan is a series of
    //                         readings, and this class has nothing to record
    //                         them as. Note also that a scan takes over whole
    //                         banks (see the programmer's reference on what
    //                         may and may not be switched during one), so it
    //                         is not something to bolt on beside close()
    //                         without deciding what happens to a Connect held
    //                         across it.
    //
    // the 34945A driver,      Microwave switch driver, digital I/O, DAC,
    // digital and analog      totaliser. All real modules, none of them
    // modules                 switching in the sense hal::SwitchFabric means,
    //                         and each one a subsystem of its own.
    //
    // paired channels         ROUT:CLOS:PAIR, for modules wired in 4-wire
    //                         pairs. Wanted the day a rig routes a sense path
    //                         through this box (hal::WireRole and
    //                         core::SensePath::Required are the framework half,
    //                         already there); it needs the module set decided
    //                         first, because which channels pair with which is
    //                         a module fact.
    //

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
    // prints these eight answers has learned the rack. It is also what will
    // check a rig's future module rows against the hardware -- a
    // hal::SwitchDeviceModel saying 34932A in slot 1 against a slot 1 that
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

    //
    // A Keysight (Agilent) 34980A Multifunction Switch/Measure Unit mainframe:
    // eight slots, one address, one session, and the three switching commands
    // every module in it is driven by.
    //
    // See this file's own preamble for what this is and is not -- in one line:
    // the thing that talks, where a module is the thing that has channels, and
    // neither an instrument nor, yet, anything a rig table has a row for.
    //
    // -- What the chassis is told, in full, to close one crosspoint ----------
    //
    //     *IDN?                    once per session -- and refused if the model
    //                              is not a 34980A (verifyIdentity)
    //     SYST:ERR?                once per session, until empty: whatever the
    //                              last user left queued is not ours
    //     ROUT:CLOS (@1003)        the crosspoint
    //     SYST:ERR?                did it accept that -- which is where a
    //                              channel the module does not have is caught
    //
    // Three commands for the first close and two for each one after it.
    //
    class Chassis
    {
        public:
            //
            // GPIB, LAN or USB. All three are standard on this mainframe --
            // unlike most of the boxes in this tree, whose back panel rules
            // something out (see hal::ReachableOver in hal/driver/address.hpp,
            // and framework/hal/README.md's table of which driver accepts
            // what). So this constraint rejects only hal::Serial, and it is
            // written as a constraint anyway rather than left off, because
            // "this box has no serial port" is a fact worth stating and worth
            // failing to compile on.
            //
            // No id argument, and that is the interesting part of this
            // signature rather than an omission -- see this file's preamble on
            // why a chassis has neither an hal::InstrumentId nor an
            // hal::SwitchDeviceId. What identifies one is its address.
            //
            template<typename AddressT>
                requires ReachableOver<AddressT, Gpib, Lan, Usb>
            explicit Chassis( const AddressT address) : mAddress( address) {}

            //
            // Where the PC reaches this mainframe -- and, since this driver
            // talks, the value that decides whether close() moves a relay or
            // moves a number. See isSimulated().
            //
            [[nodiscard]]
            auto address() const -> const Address &
            {
                return mAddress;
            }

            //
            // Whether there is nothing at the other end -- a hal::Simulated
            // address and no transport handed in.
            //
            // On this class that branch buys something the instrument drivers
            // do not have an equivalent of: a simulated chassis keeps its own
            // closed-channel set, so close(), open(), isClosed() and openAll()
            // all behave, and a test of routing logic needs no bench. Which is
            // the same service hal::SwitchFabric performs today for the rig as
            // a whole -- see this class's own comment on how the two relate.
            //
            [[nodiscard]]
            auto isSimulated() const -> bool
            {
                return !mSession && std::holds_alternative<Simulated>( mAddress);
            }

            //
            // Hand this driver a transport to talk through, instead of one
            // opened from its address. A test hands in a fake and asserts the
            // command strings; a rig with a VISA installation, or one on a bus
            // hal::io::openTransport() does not implement, hands in its own.
            //
            // Replaces any session already open, closing it, and sends nothing:
            // the identity check and the error-queue drain belong to the first
            // *use* (see session()).
            //
            auto useTransport( std::unique_ptr<io::ITransport> transport) -> void
            {
                mSession  = std::make_unique<io::ScpiSession>( std::move( transport));
                mPrepared = false;
            }

            //
            // The live SCPI session, opened on first use.
            //
            // Lazily, for the reason every driver here opens lazily: a rig's
            // hardware is reachable from globals constructed before main(), so
            // a constructor that opened a socket would make every unit test and
            // every detached run reach for the bench at static-initialisation
            // time, and would throw from a constructor with nowhere to catch
            // it.
            //
            // Public for two reasons rather than one. The first is every other
            // driver's: a bring-up session on a desk wants to send a command
            // this driver has no accessor for -- a scan, a DAC output, the
            // 34945A's drive settings -- without that becoming a reason to
            // widen the driver. The second is this box specifically: it is the
            // seam the internal DMM face will be built on, because one box must
            // have one session (see this file's preamble on the Janus).
            //
            // Throws hal::io::TransportError if the mainframe cannot be
            // reached, and hal::io::ScpiFault if it answers *IDN? with
            // something that is not a 34980A.
            //
            [[nodiscard]]
            auto session() -> io::ScpiSession &;

            //
            // *IDN? -- "Agilent Technologies,34980A,MY12345678,2.43-2.42-1.19"
            // off the real mainframe, opening the session if it is not already
            // open.
            //
            // What a run's traceability header should carry about this box is
            // this *and* the eight answers to moduleIn(), which is a
            // distinction worth making: on an ordinary instrument the identity
            // is the whole inventory, and on a mainframe it is the chassis only
            // -- the rack is what is in the slots.
            //
            [[nodiscard]]
            auto identity() -> std::string;

            //
            // Drop the session, closing the socket. The next command opens a
            // new one.
            //
            // Note what this does NOT do: open any relay. Closing a connection
            // to a latching switch matrix leaves every crosspoint exactly where
            // it was, which is a property of the hardware and not something to
            // paper over -- see openAll() for the command that does, and this
            // file's preamble on why nothing calls it automatically yet.
            //
            auto closeSession() -> void
            {
                mSession.reset();
                mPrepared = false;
            }

            // --- The switching face ---

            //
            // ROUT:CLOS -- close one channel, or several in one command.
            //
            // The list form is not just a convenience on this box: a
            // multi-channel close is executed break-before-make within a bank
            // on the FET multiplexer, is measurably faster than the same
            // channels sent one at a time on the high-density matrix, and is
            // one error-queue check instead of N. See channelList().
            //
            // Through hal::io::ScpiSession::checked(), never write(). A SCPI
            // instrument does not answer a bad command -- it queues an error
            // and carries on -- so a refused ROUT:CLOS is a relay that did not
            // move while every subsequent reading is taken as though it had.
            // On a switching device that is worse than the reading case
            // hal::io::ScpiSession::checked's own comment argues: the
            // measurement is not merely mis-ranged, it is of a different node.
            //
            // It is also where the missing half of the channel check lands, and
            // deliberately so. This driver knows the chassis has eight slots
            // and validates that; whether the module in slot 3 has a channel
            // 010 is a module fact, no module is modelled yet, and the
            // instrument itself answers -222 for one that does not exist. So a
            // wrong channel is an hal::io::ScpiFault naming the command rather
            // than a compile error naming the card -- which is exactly the
            // trade hal::hasChannel exists to remove, and getting it back is
            // what the module models are for.
            //
            auto close( ChannelAddress channel) -> void;
            auto close( const std::vector<ChannelAddress> & channels) -> void;

            //
            // ROUT:OPEN -- open one channel, or several.
            //
            // Not universal, and the exception is a hardware fact worth knowing
            // before wiring an RF path through this box: the RF multiplexer
            // modules (34941A, 34942A) and the microwave switch modules
            // (34946A, 34947A) *do not respond to ROUT:OPEN at all* -- the
            // command generates an error. They are tree-switched 1-of-4s with
            // no "all open" state; to leave a channel, you close a different
            // channel in the same bank.
            //
            // Which is the same fact rig/wiring.inc already records about this
            // rig's current RF card, from the other end: an E1472A's bank
            // powers up into a 50 ohm park on channel 00, and channel 00 is
            // what a route "opens" to. hal::SwitchFabric does not model this --
            // it will happily hold two channels of one bank closed, and the
            // card will not -- so it is a rig's business to wire an RF bank so
            // that its idle channel is a defined one.
            //
            // This driver does not refuse an open on an RF module, because it
            // cannot know what is in the slot without asking, and a driver that
            // queried SYST:CTYP? before every open would pay a round trip per
            // relay to re-learn something a rig's table will state. What it does
            // instead is send the command and let the instrument's own error
            // reach the caller -- and offer closeExclusively() below, which is
            // the operation an RF bank actually wants.
            //
            auto open( ChannelAddress channel) -> void;
            auto open( const std::vector<ChannelAddress> & channels) -> void;

            //
            // ROUT:CLOS:EXCL -- open every channel on the module, then close
            // these.
            //
            // One command, and the atomicity is the point: it is how you select
            // a channel on a 1-of-N without a window in which two are closed
            // or none is. That makes it the right operation for an RF
            // multiplexer bank (which cannot be opened at all -- see open()
            // above) and for any mux where two closed channels would short two
            // DUT pins together, which on a shared-common multiplexer is all of
            // them.
            //
            // "Exclusive" is per *module*, not per bank, and that is the sharp
            // edge: on a two-bank module this opens the other bank's channels
            // too. A rig using one module for two independent routes cannot use
            // this to change one of them.
            //
            // Note it does not disturb the channels it is asked to close if they
            // were already closed -- though the reference is explicit that their
            // state *during* execution is not guaranteed, which matters if
            // something is measuring through one at the time.
            //
            auto closeExclusively( ChannelAddress channel) -> void;
            auto closeExclusively( const std::vector<ChannelAddress> & channels) -> void;

            //
            // ROUT:CLOS? -- is this channel closed, according to the
            // instrument?
            //
            // Asked of the hardware rather than remembered, which on a
            // switching device is worth more than on a supply: relay state
            // survives a power cycle on a latching module, a front-panel
            // operator can change it, and a scan opens whole banks on its own
            // initiative. A driver reporting its own last command would be
            // confidently wrong in all three cases.
            //
            [[nodiscard]]
            auto isClosed( ChannelAddress channel) -> bool;

            //
            // ROUT:OPEN:ALL -- open every channel relay and every Analog Bus
            // relay, on one module or on all of them.
            //
            // This is the command that ought to be on the end of a failed run,
            // and today nothing puts it there. hal::safeRig() finishes by
            // calling hal::fabric.openAll(), which is bookkeeping -- so on a
            // rig whose switching is a 34980A, a script that died with a rail
            // routed to a DUT pin leaves that relay closed. See this file's
            // preamble; the fix is a transport seam in hal::SwitchFabric, and
            // this member is what it will call.
            //
            // Deliberately not called from a destructor, and this is the same
            // argument hal::keysight_dsox1202g::DSOX1202G's safe() makes about
            // opening sessions during cleanup, one step further: a chassis
            // going out of scope is not evidence that the rig should be torn
            // down, and a driver that opened every relay on the way out would
            // make a copy, a container reallocation or an early return into a
            // silent disconnection of the whole DUT.
            //
            // Refuses nothing and waits for nothing. Note the instrument's own
            // rule: with a scan running, this is an error for any module whose
            // banks the scan owns, and no relay moves.
            //
            auto openAll() -> void;
            auto openAll( int slot) -> void;

            //
            // ROUT:MOD:WAIT -- block until the module in this slot, or every
            // module, has finished switching.
            //
            // Not *OPC? and not *WAI: this waits for *switching* only, where
            // *WAI would also wait for a scan or a DMM reading to finish. On a
            // mainframe that can be measuring and switching at once, those are
            // different questions, and the one a route wants is this one.
            //
            // Ordinarily unnecessary, because ROUT:CLOS does not return until
            // the relay has moved -- it earns its place in overlapped switching
            // mode (ROUT:OPER:OVER), where the command returns immediately and
            // the relay follows. This driver never enables that mode, so this
            // is here for the caller who does, and for the one who wants to be
            // sure before energising something.
            //
            auto waitForSwitching() -> void;
            auto waitForSwitching( int slot) -> void;

            // --- What is in the rack ---

            //
            // SYST:CTYP? -- what is plugged into this slot. See ModuleIdentity,
            // which is where the argument for having this at all lives: what a
            // 34980A *is* depends on what is in it.
            //
            [[nodiscard]]
            auto moduleIn( int slot) -> ModuleIdentity;

            //
            // All eight slots, in order, empty ones included.
            //
            // Eight round trips, which is a lot for one call and exactly right
            // for the one thing it is for: a bring-up run that prints the rack.
            // Nothing on a measurement path should call it.
            //
            [[nodiscard]]
            auto modules() -> std::vector<ModuleIdentity>;

            // --- The internal DMM, from the switch side ---

            //
            // INST:DMM:INST? -- is the optional internal DMM assembly fitted?
            //
            // A property of the chassis, not of a module, and the first thing
            // the future DMM face will have to ask: a rig whose instrument
            // table names an internal DMM in a mainframe that does not have one
            // is a table describing a different box, and that should fail on
            // the first reading with a sentence rather than with -113 on a
            // CONFigure.
            //
            [[nodiscard]]
            auto internalDmmInstalled() -> bool;

            //
            // INST:DMM? -- is it enabled?
            //
            // Distinct from installed, and the distinction is electrical rather
            // than administrative: disabling the internal DMM "has the same
            // effect as physically removing the DMM assembly, including
            // removing the connections to the Analog Buses". So a disabled DMM
            // is not a DMM that refuses to read -- it is a DMM that is no longer
            // on the backplane the multiplexers reach it through, which is a
            // fact about the *switching* topology and the reason this pair of
            // accessors is on this face at all.
            //
            [[nodiscard]]
            auto internalDmmEnabled() -> bool;

            //
            // INST:DMM ON|OFF -- enable or disable it.
            //
            // With one consequence that has to be shouted rather than
            // mentioned: changing this state makes the instrument issue a
            // Factory Reset. A *RST on this box aborts any measurement, clears
            // the scan list, and returns every measurement parameter to its
            // factory setting -- so calling this in the middle of a run
            // silently discards what the run had set up.
            //
            // It does not open relays (a *RST does not, on latching modules),
            // which is the one mercy in that list and also the reason the
            // damage is easy to miss: the routing survives and the
            // configuration does not.
            //
            // So this is a bring-up and rig-configuration operation, not
            // something a script calls. It is exposed because "is the DMM on
            // the Analog Buses" is a real question about a rig's switching
            // topology, and because a bench with a fitted-but-disabled DMM is
            // otherwise a mystery nobody can diagnose from the PC.
            //
            auto setInternalDmm( bool enabled) -> void;

            // --- Test/simulation hooks ---

            //
            // What a simulated chassis has closed. Real hardware has no such
            // accessor -- isClosed() is the question you ask a real one -- and
            // this is here so a test can assert the whole state in one
            // comparison rather than one query per channel.
            //
            // A set rather than a use count, which is the difference between
            // this class and hal::SwitchFabric and is worth being clear about:
            // the fabric counts because two callers can legitimately need one
            // relay closed at once and whoever releases first must not open it
            // (see its own comment). A relay is not a counter, though -- it is
            // open or closed -- so this models the hardware, and the arbitration
            // stays where it already is, one layer up.
            //
            [[nodiscard]]
            auto simulatedClosedChannels() const -> const std::vector<ChannelAddress> &
            {
                return mSimClosed;
            }

            //
            // What a simulated chassis reports for a slot. Defaults to empty
            // for every slot, which is the honest simulation of a mainframe
            // nobody has told about any modules.
            //
            auto setSimulatedModule( int slot, ModuleIdentity module) -> void;

            //
            // Whether a simulated chassis claims a fitted internal DMM. False
            // by default: the DMM is an option, and a simulation that claimed
            // one would let the future DMM face's own installed-check pass
            // against a box that has nothing.
            //
            auto setSimulatedInternalDmm( const bool installed, const bool enabled) -> void
            {
                mSimDmmInstalled = installed;
                mSimDmmEnabled   = installed && enabled;
            }

        private:
            //
            // Refuse a slot the chassis does not have, on the runtime path.
            // Called by everything that takes a slot or a channel, on a
            // simulated chassis as well as an attached one -- which is the
            // point: a bad slot must not be a thing that only fails on the
            // bench (see hal::keysight_edu36311a::RatingExceeded, which makes
            // the same argument about a supply's rating).
            //
            static auto validate( int slot) -> void;

            static auto validate( const std::vector<ChannelAddress> & channels) -> void;

            //
            // Ask the mainframe what it is and refuse anything that is not a
            // 34980A. Called once, when the session is opened.
            //
            [[nodiscard]]
            auto verifyIdentity() -> std::string;

            // --- The simulated half ---

            auto simulatedClose( const std::vector<ChannelAddress> & channels) -> void;
            auto simulatedOpen( const std::vector<ChannelAddress> & channels) -> void;

            Address                           mAddress;

            //
            // Null until the first command that needs hardware, and null
            // forever on a simulated chassis -- see session() on why it cannot
            // be opened in the constructor.
            //
            // A unique_ptr, which makes this class non-copyable, and that is
            // correct rather than incidental: a copy would be a second object
            // claiming one mainframe, and one of the two would hold the socket.
            // It is also the guarantee the Janus depends on -- see this file's
            // preamble on why one box must have one session.
            //
            std::unique_ptr<io::ScpiSession>  mSession;

            //
            // Whether mSession has had the once-per-session exchange: the
            // error-queue drain and the identity check. A flag beside the
            // pointer rather than something done where the session is created,
            // because a session arrives two ways -- opened from the address, or
            // handed in by useTransport() -- and both have to be prepared.
            //
            bool                              mPrepared{ false };

            //
            // Kept sorted and unique, so that a test comparing the whole set
            // does not depend on the order the closes happened in. Ordered
            // rather than hashed because eight slots of a few hundred channels
            // is small and a sorted vector prints readably in a failure.
            //
            std::vector<ChannelAddress>       mSimClosed;

            std::map<int, ModuleIdentity>     mSimModules;
            bool                              mSimDmmInstalled{ false };
            bool                              mSimDmmEnabled{ false };
    };
} // namespace hal::keysight_34980a
