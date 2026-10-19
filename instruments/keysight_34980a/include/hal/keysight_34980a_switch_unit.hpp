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

#include "hal/keysight_34980a/vocabulary.hpp"
#include "hal/keysight_34980a/mainframe.hpp"

//
// The first face of the mainframe: the switching. Relays to close and open,
// the modules in the slots, the safing. The second face is
// keysight_34980a_multimeter.hpp; hal/keysight_34980a.hpp includes both.
//

namespace hal::keysight_34980a
{
    //
    // A Keysight (Agilent) 34980A Multifunction Switch/Measure Unit mainframe:
    // eight slots, one address, one session, and the three switching commands
    // every module in it is driven by.
    //
    // See this file's own preamble for what this is and is not -- in one line:
    // the thing that talks, where a module is the thing that has channels. An
    // instrument row, so preflight, --address and safing all reach it.
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
    class SwitchUnit : public InstrumentTag
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
            // An hal::InstrumentId like every row in instrument.inc -- see this
            // file's preamble on why a chassis became one. Still not an
            // hal::SwitchDeviceId: those are the cards in its slots.
            //
            // This model's back panel, written once and read twice: the
            // constructor below is constrained by it, and
            // hal::bindAddresses() checks an address supplied at startup
            // against the same list (see hal::BackPanel). A second
            // spelling of these connectors could disagree with this one,
            // and only the bench would ever find out.
            //
            using Buses = BackPanel<Gpib, Lan, Usb>;

            template<typename AddressT>
                requires Buses::allows<AddressT>
            SwitchUnit( const InstrumentId id, const AddressT address) : mId( id), mConnection( id, address) {}

            [[nodiscard]]
            auto id() const -> InstrumentId
            {
                return mId;
            }

            //
            // Where the PC reaches this mainframe -- and, since this driver
            // talks, the value that decides whether close() moves a relay or
            // moves a number. See isSimulated().
            //
            [[nodiscard]]
            auto address() const -> const Address &
            {
                return mConnection.address();
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
                return mConnection.isSimulated();
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
                mConnection.useTransport( std::move( transport));
            }

            //
            // Point this driver at a different mainframe -- what preflight does
            // with an --address, THORIUM_ADDRESS_<box> or a site row. Startup
            // only, validates nothing (hal::bindAddresses() already checked the
            // address against Buses), and drops any open session so that
            // nothing goes on switching the previous box.
            //
            // Closing that session opens no relay, for the reason
            // closeSession() gives: a latching module keeps its state.
            //
            auto useAddress( const Address & address) -> void
            {
                mConnection.useAddress( address);
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
            // paper over -- see openAll() for the command that does, and
            // safeRelays() for where a failed run gets it.
            //
            auto closeSession() -> void
            {
                mConnection.close();
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
            // The command on the end of a failed run, as ROUT:OPEN:ALL ALL --
            // safeRelays() sends it in hal::safeRig()'s second pass, once every
            // source is off. hal::fabric.openAll() after it is bookkeeping:
            // the hardware has been opened by then, and safing may not talk
            // through the fabric's driver.
            //
            // Deliberately not called from a destructor, and this is the same
            // argument hal::keysight_dsox1202g::Oscilloscope's safe() makes about
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

            // --- Driven by the fabric ---

            //
            // One relay on the card in this slot, as hal::fabric moves it --
            // see hal::CardSwitchingInstrument. A card row on this chassis's
            // box (SWITCH_DEVICE( Swu1, Keysight34932A, Matrix1, ..., Card( 1)))
            // gives the slot; the fabric gives the channel; this is one
            // ROUT:CLOS or ROUT:OPEN of the two, through close()/open() and so
            // through everything they already do: the slot checked, the
            // command checked(), and nothing sent on a detached bench.
            //
            auto closeOnCard( const int card, const std::uint16_t channel) -> void
            {
                close( ChannelAddress{ card, channel });
            }

            auto openOnCard( const int card, const std::uint16_t channel) -> void
            {
                open( ChannelAddress{ card, channel });
            }

            //
            // What the card in this slot reports itself as ("34932A"), or ""
            // for an empty slot -- see hal::CardIdentifyingInstrument, and
            // preflight, which compares it against the card row's model.
            //
            [[nodiscard]]
            auto cardModel( const int card) -> std::string
            {
                return moduleIn( card).Model;
            }

            // --- Relay health ---

            //
            // DIAG:REL:CYCL? -- how many times this relay has operated in its
            // life, as the mainframe counts it.
            //
            // Worth having beside isClosed(), because the two answer different
            // questions. ROUT:CLOS? reports what the mainframe *told* the relay
            // to do; the cycle count goes up when the relay's drive actually
            // changed state. A close/open pair that moves the count by one is
            // a relay that was driven -- which is as far as a check can go
            // without a meter on the contacts. And the count is the wear
            // figure a relay is replaced on.
            //
            // A simulated chassis counts one cycle per close of an open channel.
            //
            [[nodiscard]]
            auto relayCycles( ChannelAddress channel) -> long;

            // --- Safing ---

            //
            // Nothing -- deliberately, and unlike every other driver's safe().
            // This is a switching device, and its relays have to move after
            // every source in the rig is off; hal::safeRig() calls safe() in
            // declaration order, which cannot promise that. The relays are
            // safeRelays()'s, in the pass after this one. See
            // hal::RelayHoldingInstrument.
            //
            auto safe() -> void
            {
            }

            //
            // ROUT:OPEN:ALL ALL -- every relay in every slot, called by
            // hal::safeRig() once every output is off.
            //
            // safe()'s contract, carried over whole: only down a session that
            // is already open, never opening one, and never throwing. Sent with
            // write() rather than checked(), so a mainframe that has stopped
            // answering does not abandon the rest of the safing pass -- and an
            // RF module's refusal to open (see open()) does not either. A
            // simulated chassis simply forgets what it had closed.
            //
            // Not a latching-relay cure-all: an RF bank has no open state, and
            // stays on whichever channel it was left on. A rig wires that
            // bank's idle channel to somewhere harmless (see open()).
            //
            auto safeRelays() -> void;

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
            // Whether a call answers from the simulation rather than the wire:
            // a simulated chassis, or a detached bench.
            //
            // The second is what keeps a --skeleton, --replay or --inject run
            // off the hardware. Every verb already asks core::bench() before
            // it instructs anything; a script calling this driver directly
            // (Swu1.close(), until the fabric drives this box) asks nothing, so
            // the driver asks for it. Detached, a close is bookkeeping and an
            // isClosed() answers from it -- which is exactly what a run whose
            // readings are fiction should do with a relay.
            //
            [[nodiscard]]
            auto simulating() const -> bool;

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
            auto verifyIdentity( io::ScpiSession & opened) -> std::string;

            // --- The simulated half ---

            auto simulatedClose( const std::vector<ChannelAddress> & channels) -> void;
            auto simulatedOpen( const std::vector<ChannelAddress> & channels) -> void;

            InstrumentId                      mId;
            //
            // Where this instrument is and the session to it -- shared with any
            // other face of the same box (see hal::BoxConnection). Its being
            // non-copyable is what makes this class non-copyable, and that is
            // correct rather than incidental: a copy would be a second object
            // claiming the same instrument.
            //
            BoxConnection                 mConnection;

            //
            // Kept sorted and unique, so that a test comparing the whole set
            // does not depend on the order the closes happened in. Ordered
            // rather than hashed because eight slots of a few hundred channels
            // is small and a sorted vector prints readably in a failure.
            //
            std::vector<ChannelAddress>       mSimClosed;

            std::map<int, ModuleIdentity>     mSimModules;
            std::map<ChannelAddress, long>    mSimCycles;
            bool                              mSimDmmInstalled{ false };
            bool                              mSimDmmEnabled{ false };
    };

} // namespace hal::keysight_34980a
