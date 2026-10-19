#pragma once

#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>

#include "core/driver/describe.hpp"
#include "core/driver/port.hpp"
#include "core/quantities/quantity.hpp"

#include "hal/driver/address.hpp"
#include "hal/driver/box_connection.hpp"
#include "hal/driver/builder.hpp"
#include "hal/driver/describe.hpp"
#include "hal/driver/instrument.hpp"
#include "hal/fabric/switch_fabric.hpp"
#include "hal/io/scpi.hpp"
#include "hal/topology/wiring.hpp"

#include "hal/keysight_edu36311a/vocabulary.hpp"
#include "hal/keysight_edu36311a/builder.hpp"
#include "hal/keysight_edu36311a/wire.hpp"

//
// the instrument class and its row aliases
//
// Part of hal/keysight_edu36311a.hpp -- include that, not this.
//

namespace hal::keysight_edu36311a
{
    //
    // One output of a Keysight EDU36311A triple-output DC bench power supply:
    // 90 W in one chassis as 6 V / 5 A, 30 V / 1 A and 30 V / 1 A, each
    // independently programmable and independently switchable.
    //
    // One instance models one *output*, not the box. DcP5/DcP6/DcP7 (see
    // rig/instrument.inc) are three such instances behind one address, exactly
    // the way DcP1..DcP4 are four instances behind one N6701A mainframe
    // address: one box, three endpoints, so there is one address column
    // repeated three times and the endpoint is in the type rather than in the
    // address (see hal/driver/address.hpp on why, and Output1 above on why the
    // endpoint is in the *type* here where the N6701A has it as an argument).
    //
    // -- Smart Bench Essentials, and its sibling on this bench --------------
    //
    // The same product line as hal::keysight_edu34450a::Multimeter: education-
    // market packaging of an instrument whose command set is the professional
    // model's. Its rear panel is the same story too -- gigabit LAN and a USB
    // device port, no GPIB connector and no option that adds one -- so the
    // constructor constraint below is the meter's.
    //
    // What it is *not* is a smaller E36311A, and this is the trap the model
    // number sets. The E36300 series programming guide is the document this
    // driver's SCPI comes from and is the right document; the E36311A it was
    // written for has outputs of 6 V / 5 A, +25 V / 1 A and -25 V / 1 A, one
    // of which is a *negative* rail with its own output identifier (N25V) and
    // its own MIN/MAX inversion. This model has two positive 30 V outputs
    // instead. Every number in that guide's range table is therefore wrong
    // here, and every command in the rest of it is right -- which is exactly
    // the split between Output1..Output3 above (this instrument's ratings,
    // from its own data sheet) and src/keysight_edu36311a.cpp (the series'
    // commands, from the guide).
    //
    // -- This driver talks, and it is the first source that does ------------
    //
    // hal/io/ arrived with a meter and grew a scope (see
    // instruments/keysight_edu34450a and instruments/keysight_dsox1202g). This
    // is the first *sourcing* driver on it, and the thing that makes a source
    // different from both is that its commands complete after they return: a
    // supply told to go to 24 V accepts the command in under 10 ms and reaches
    // the rail some tens of milliseconds later. So Apply ends with the *OPC?
    // that hal::io::ScpiSession::waitForComplete() was written for and that no
    // driver had yet used -- and *OPC? answers when the instrument has
    // finished processing, which is not the same as the rail having settled.
    // See applyOutput() for what that means for the line after an Apply.
    //
    // What the supply is told, in full, for one Apply of a 24 V rail on
    // output 2:
    //
    //     *IDN?                  once per session -- and refused if the model
    //                            is not this one (verifyIdentity)
    //     SYST:ERR?              once per session, until empty: whatever the
    //                            last user left queued is not ours
    //     CURR 0.5, (@2)         the limit first -- see applyOutput()
    //     SYST:ERR?              did it accept that
    //     VOLT:PROT 27, (@2)     then the trip level, before the setpoint it
    //                            has to be above
    //     SYST:ERR?
    //     VOLT 24, (@2)          then the setpoint
    //     SYST:ERR?
    //     OUTP 1, (@2)           and only then the output on
    //     SYST:ERR?
    //     *OPC?                  and wait until it has all been processed
    //
    // and for one reading of what that rail is actually doing:
    //
    //     MEAS:VOLT? (@2)
    //
    // Which one it does -- wire or simulation hooks -- is decided by the
    // address column of the rig's instrument table and by nothing else.
    // hal::Simulated means the hooks below are the instrument; hal::Lan means
    // the box at that hostname is. A script cannot tell the difference and no
    // script should have to (see isSimulated()).
    //
    // -- Isolation, and one real bench consequence -------------------------
    //
    // Isolation (DirectWiring/RelayIsolated above) says whether this rail's
    // lead has an isolation relay in it, as a template parameter rather than a
    // runtime flag, so that Connect/Disconnect exist only where there is
    // something for them to move.
    //
    // On this rig the answer is not the same for all three outputs, and the
    // reason is arithmetic rather than preference. The isolation relays live
    // on Spst1, a Racal 1260-18, whose relays are rated 2 A (see
    // rig/devices.inc). Outputs 2 and 3 are 1 A outputs and fit inside that
    // with room to spare. Output 1 is a 5 A output, and no relay in this rack
    // will carry it -- so its lead is cabled straight through, and its row
    // says DirectOutput1. That is the same distinction DcP1/DcP2 against
    // DcP3/DcP4 already records, arrived at from the current rating instead of
    // from history.
    //
    // -- What is deliberately not here -------------------------------------
    //
    // series/parallel coupling  outputs 2 and 3 can be strapped in series for
    //                           60 V or in parallel for 2 A, and the
    //                           instrument has commands for it
    //                           (OUTP:PAIR). Two of these instances would
    //                           then be one rail, which is not something one
    //                           InstrumentId per output can describe -- and it
    //                           is a cabling fact besides, so a rig that did
    //                           it would want one instance with a different
    //                           Output tag, not a mode on these.
    //
    // output sequencing         OUTP:DEL:RISE/FALL, which stage several
    //                           outputs coming up. Real and useful on a DUT
    //                           with a rail order, and a script already has
    //                           that ordering in it explicitly, one Apply per
    //                           line, where a reader can see it.
    //
    // data logging, front panel  INIT:DLOG, DISP:TEXT and the flash-drive
    //                           store. Nothing a test rig needs from an
    //                           instrument it is driving.
    //
    template<typename Output, typename Isolation>
        requires OutputChannel<Output>
    class DcSource : public InstrumentTag
    {
        public:
            //
            // LAN or USB, and nothing else -- what this model's back panel
            // has. No GPIB connector and no option that adds one, so a rig row
            // addressing it over GPIB fails to compile rather than failing to
            // open (see hal::ReachableOver in hal/driver/address.hpp).
            //
            // Note what this does not constrain: the *front*-panel USB port is
            // for a flash drive holding saved setups, and is not a way for the
            // PC to reach the instrument. hal::Usb here means the rear USBTMC
            // device port.
            //
            // One address for the whole chassis, and no channel argument: the
            // three outputs are three endpoints behind one interface, so all
            // three rows in a rig's instrument.inc carry the same address, and
            // which output a row is is the type column's business (see Output1
            // above).
            //
            // This model's back panel, written once and read twice: the
            // constructor below is constrained by it, and
            // hal::bindAddresses() checks an address supplied at startup
            // against the same list (see hal::BackPanel). A second
            // spelling of these connectors could disagree with this one,
            // and only the bench would ever find out.
            //
            using Buses = BackPanel<Lan, Usb>;

            template<typename AddressT>
                requires Buses::allows<AddressT>
            DcSource( const InstrumentId id, const AddressT address) : mId( id), mConnection( id, address) {}

            //
            // Non-copyable, and correct rather than incidental: a copy of a
            // driver would be a second object claiming the same output. It used
            // to follow from holding its session in a unique_ptr; now that the
            // session belongs to the box and is shared, it has to be said.
            // Nothing copies one -- a rig's instruments are globals and
            // core::Port holds a reference -- so the restriction costs nothing.
            //
            DcSource( const DcSource &)                    = delete;
            auto operator=( const DcSource &) -> DcSource & = delete;

            //
            // Where the PC reaches this supply -- and, since this driver has a
            // transport, the column that decides whether an Apply reaches
            // hardware or the simulation state below. See isSimulated().
            //
            [[nodiscard]]
            auto address() const -> const Address &
            {
                return mConnection.address();
            }

            [[nodiscard]]
            auto id() const -> InstrumentId
            {
                return mId;
            }

            // --- What this output is, as constants a caller can read ---

            [[nodiscard]]
            static constexpr auto channel() -> int
            {
                return Output::Channel;
            }

            [[nodiscard]]
            static constexpr auto maxVoltage() -> core::quantities::Voltage
            {
                return core::quantities::Voltage{ Output::MaxVolts };
            }

            [[nodiscard]]
            static constexpr auto maxCurrent() -> core::quantities::Current
            {
                return core::quantities::Current{ Output::MaxAmps };
            }

            //
            // "6 V / 5 A" -- the badge beside the terminals, for the one place
            // it is genuinely needed: the message of a RatingExceeded, and a
            // journal line that should say which of three outputs was
            // programmed rather than only which InstrumentId it wears.
            //
            [[nodiscard]]
            static constexpr auto rating() -> std::string_view
            {
                return Output::Rating;
            }

            //
            // Whether there is nothing at the other end -- a hal::Simulated
            // address and no transport handed in.
            //
            // The one branch in this driver that decides between a socket and
            // the state below, and it is a property of the *address* rather
            // than a mode a caller sets: a rig says what it has once, in its
            // instrument table. An injected transport wins over a Simulated
            // address, deliberately -- see useTransport().
            //
            [[nodiscard]]
            auto isSimulated() const -> bool
            {
                return mConnection.isSimulated();
            }

            //
            // Hand this driver a transport to talk through, instead of one
            // opened from its address.
            //
            // Two callers, and neither is a script. A test hands in a fake and
            // asserts the command strings (see this driver's tests). A rig
            // with a VISA installation, or one reaching an instrument over a
            // bus hal::io::openTransport() does not implement, hands in its
            // own hal::io::ITransport and gets this driver working over it
            // with no change here.
            //
            // Replaces any session already open, closing it, and sends
            // nothing: the identity check and the error-queue drain belong to
            // the first *use* (see session()), so handing in a fake transport
            // is not a thing that can throw.
            //
            // Installed on the box, not on this output: every output of the
            // same box at this address talks through it from now on, because a
            // transport is a connection to the box (see hal::BoxConnection). A
            // test wanting three outputs on one fake gives them one address and
            // hands it in once. On a hal::Simulated address the connection is
            // this output's alone, which is what every single-output test
            // relies on.
            //
            auto useTransport( std::unique_ptr<io::ITransport> transport) -> void
            {
                mConnection.useTransport( std::move( transport));
            }

            //
            // Point this driver at a different instrument -- see
            // hal::keysight_edu34450a::Multimeter::useAddress() for the whole
            // argument, which is identical here: startup only, validates
            // nothing because hal::ReachableOver and hal::sameKind already
            // have, and drops any open session so that nothing goes on
            // talking to the previous box.
            //
            // Drops only this output's hold on the old box: its other outputs
            // may still be on it, and their session is theirs to close. The
            // new address is looked up on the next use, so an output moved
            // onto a box another output already has open joins that session.
            //
            auto useAddress( const Address & address) -> void
            {
                mConnection.useAddress( address);
            }

            //
            // The live SCPI session, opened on first use.
            //
            // Lazily, and that is not an optimisation -- a rig's instruments
            // are globals constructed before main() (see
            // hal/topology/active_instruments.hpp), so a constructor that
            // opened a socket would make every binary that links the rig try
            // to reach the bench at static-initialisation time, and would
            // throw from a constructor with nowhere to catch it. So the
            // connection happens at the first Apply or reading that needs one,
            // which is also exactly when a detached run (--replay, --inject,
            // --skeleton) does not need one.
            //
            // Public, because a bring-up session on a desk wants it: a
            // deployment test can send this supply a command this driver has
            // no accessor for -- OUTP:PAIR, DISP:TEXT, the data logger --
            // without that becoming a reason to widen the driver. Throws
            // hal::io::TransportError if the supply cannot be reached, and
            // hal::io::ScpiFault if it answers *IDN? with a model this driver
            // is not for.
            //
            // Shared with this box's other outputs -- see hal::BoxConnection. So
            // "opened on first use" means the first use by *any* output of the
            // box, and the exchange below happens once per box, not once per
            // output: the three outputs are three types and one family,
            // "keysight_edu36311a", so the second output to speak finds the
            // session prepared.
            //
            [[nodiscard]]
            auto session() -> io::ScpiSession &
            {
                //
                // Opened from the address the rig table wrote down -- hal::Usb
                // routes to VISA (see hal/io/visa_transport.hpp), so a machine
                // with no VISA installed fails with "no VISA library found"
                // rather than with a timeout, which is the difference between a
                // missing dependency and a missing instrument.
                //
                // Prepared where it is *used* rather than where it is created,
                // because a session arrives two ways -- opened from the
                // address, or handed in by useTransport() -- and both have to
                // go through it. Not marked prepared until it has succeeded, so
                // a supply that failed its identity check is asked again on the
                // next command rather than treated as verified: the instrument
                // that was off when the run started may be on now.
                //
                return mConnection.session( "keysight_edu36311a", []( io::ScpiSession & opened)
                {
                    detail::prepare( opened);
                });
            }

            //
            // *IDN? -- "Keysight Technologies,EDU36311A,CN12345678,..." off the
            // real instrument, opening the session if it is not already open.
            //
            // Its own accessor rather than leaving callers to spell
            // session().identify(): what a run's traceability header should
            // carry about an instrument is what the instrument says it is, not
            // what the rig table hoped it was.
            //
            [[nodiscard]]
            auto identity() -> std::string
            {
                //
                // session() has already asked and already checked the answer,
                // so this asks again rather than caching it. One extra round
                // trip on a call nothing makes per reading, against a cached
                // string that would be a second thing to keep true if the
                // socket were ever reopened onto a different box.
                //
                return detail::identify( session());
            }

            //
            // Drop the session, closing the socket. The next Apply or reading
            // opens a new one. For a long-lived process that should not hold a
            // bench instrument between runs, and for recovering from a wedged
            // one -- on a raw socket, closing and reopening is the whole of
            // the recovery vocabulary. Not called by safe(): see there.
            //
            // Closes the box's session, not this output's -- there is only the
            // one (see hal::BoxConnection) -- so the box's other outputs reopen it
            // on their next command too. Which is what recovering a wedged box
            // needs: a close that left two more sockets open would recover
            // nothing.
            //
            // Found the way safe() finds it, so an output that has not spoken
            // yet still closes the box a sibling opened -- and, like safe(),
            // never creates one to close.
            //
            auto closeSession() -> void
            {
                mConnection.close();
            }

            //
            // No point argument -- this output is cabled to one pin, and which
            // pin is a rig fact (SOURCE_WIRING in rig/wiring.inc), never
            // something a script or this config carries. See this class's own
            // comment.
            //
            [[nodiscard]]
            auto dc() -> DcBuilder<Output, Isolation>
            {
                return DcBuilder<Output, Isolation>{ *this };
            }

            //
            // What this supply reports about its own output, over its own
            // interface -- not a routed measurement. MEAS:VOLT? and MEAS:CURR?
            // on the real instrument, which is how rail current is read on this
            // rig: the signal switching carries signals, and a rail at several
            // amps is not a signal.
            //
            // There is no at(...) at the call site and the fabric is never
            // touched -- see core::MeasureEngine's point-free operator().
            //
            //     Apply( DcP6.dc().voltage( 24_V).currentLimit( 0.5_A));
            //     const auto drawn = Measure( DcP6.measuredCurrent());
            //
            // Named "measured..." rather than voltage()/current()
            // deliberately: this instrument has both a setpoint and a reading
            // for each of those quantities (dc().voltage( 24_V) sets,
            // measuredVoltage() reads), and on a supply that distinction is
            // worth spelling out at every call site rather than leaving it to
            // be inferred from which verb it was passed to.
            //
            // Reading the rail at the *DUT* is a stricter check than either of
            // these, and the two are not interchangeable: a supply's own
            // readback cannot see cable drop or a bad contact. See
            // suite/scripts/rig_power_on.cpp, which spells that out.
            //
            [[nodiscard]]
            auto measuredVoltage() -> core::Port<core::quantities::Voltage, DcSource>
            {
                return core::Port<core::quantities::Voltage, DcSource>{ *this };
            }

            [[nodiscard]]
            auto measuredCurrent() -> core::Port<core::quantities::Current, DcSource>
            {
                return core::Port<core::quantities::Current, DcSource>{ *this };
            }

            //
            // The read a core::Port performs.
            //
            // core::MeasureSetup's fields are all ignored here, and for once
            // that needs no argument per field: a supply's readback is not
            // configurable. There is no range to pick (MEAS:VOLT? reads the
            // output on the range the output has), no integration time, no
            // trigger, and no threshold. What the meter's driver spends a
            // paragraph each on -- see
            // hal::keysight_edu34450a::Multimeter::rawMeasure -- this
            // instrument simply does not have.
            //
            // A disabled output reads zero for both quantities rather than
            // reporting the last setpoint. On the wire that is just what the
            // instrument answers; simulated, it is arranged to be, because it
            // is the reading a script checking "is this rail really off"
            // depends on.
            //
            template<core::quantities::QuantityType QuantityT>
            [[nodiscard]]
            auto rawMeasure( const core::MeasureSetup<QuantityT> &) -> QuantityT
            {
                if constexpr( std::is_same_v<QuantityT, core::quantities::Voltage>)
                {
                    if( isSimulated())
                    {
                        return mEnabled ? mOutputVoltage : core::quantities::Voltage{};
                    }

                    return core::quantities::Voltage{ detail::measure( session(), Output::Channel, detail::Reading::Voltage) };
                }
                else if constexpr( std::is_same_v<QuantityT, core::quantities::Current>)
                {
                    if( isSimulated())
                    {
                        return mEnabled ? mSimOutputCurrent : core::quantities::Current{};
                    }

                    return core::quantities::Current{ detail::measure( session(), Output::Channel, detail::Reading::Current) };
                }
                else
                {
                    static_assert( !sizeof( QuantityT), "EDU36311A reports only its output voltage and current");
                }
            }

            //
            // Drop this output to a known idle state, unconditionally -- see
            // hal::safeRig() in hal/verbs/safing.hpp for who calls this and
            // why it takes no arguments and reads no state. Not Remove(
            // DcP6.dc()) under another name: Remove is a test-script step,
            // addressed through a config and a builder chain, and reaching it
            // requires knowing which supply a script was in the middle of
            // driving. safe() is the opposite -- it is called when nobody
            // knows what was running.
            //
            // Zeroes the programmed setpoint as well as disabling the output,
            // deliberately. On the real instrument OUTP OFF leaves the voltage
            // setpoint where a test left it, so a supply safed at 24 V comes
            // back at 24 V the instant anything enables the output again -- a
            // front-panel press, a half-initialised driver, a reconnecting
            // console. Safing is meant to survive exactly that kind of
            // unattended re-enable.
            //
            // On an attached supply it says both of those on the wire, and
            // only down a session that is already open -- see the body below,
            // and instruments/README.md's rule, of which this instrument is
            // the most important case: a rail left on by a failed run is the
            // one thing on a bench that safing genuinely has to reach.
            //
            auto safe() -> void
            {
                mEnabled       = false;
                mOutputVoltage = core::quantities::Voltage{};

                //
                // mCurrentLimit and mOverVoltage are deliberately left as they
                // are rather than cleared: with the output off and the
                // setpoint at zero neither has anything to act on, and an
                // accidental re-enable is safer finding a stale limit and a
                // stale trip level still in place than finding none at all.
                //
                // On the wire, only down a session that is *already* open, and
                // never opening one. Safing runs when a script has already
                // failed, quite possibly because this supply is unreachable;
                // opening a session at that moment would replace the run's
                // real failure with a transport error from the cleanup path,
                // and hal::safeRig() does not catch (see
                // hal/src/verbs/safing.cpp), so it would also abandon the
                // safing of every instrument after this one.
                //
                // The session is deliberately not closed either: its error
                // queue is the best evidence of what went wrong, and dropping
                // it is closeSession()'s job and the caller's call.
                //
                // Open means open to the box, by any of its outputs: a
                // supply whose output 1 was used this run has a session this
                // output can safe its own channel down, even if this output
                // never spoke. That is a rail that gets turned off where it
                // used to be skipped, and it opens nothing new.
                //
                if( auto * const open = mConnection.openSession())
                {
                    detail::sendSafe( *open, Output::Channel);
                }
            }

            //
            // What an Apply does. Public because applyDriver below is a free
            // function found by ADL, not because a script should call it --
            // scripts go through Apply (see core/verbs/source.hpp).
            //
            // Programs the settings that were named, then enables the output;
            // on an attached supply, in that order and for the reason
            // detail::program gives (and see this class's own comment for the
            // full exchange). Throws RatingExceeded before anything is sent if
            // a setpoint is beyond this output's badge.
            //
            // Returns when the instrument has *processed* the commands, not
            // when the rail has arrived. This model's up-programming settling
            // time is about 80 ms to within 1% of the excursion, and its
            // down-programming time into no load is about 500 ms -- so a
            // script that removes a rail and reads it on the very next line
            // can legitimately read a rail still on its way down. That is a
            // property of the hardware rather than of this driver, and the
            // place to deal with it is the script, which is the only thing
            // that knows what it is waiting for.
            //
            auto applyOutput( const std::optional<core::quantities::Voltage> voltage,
                              const std::optional<core::quantities::Current> currentLimit,
                              const std::optional<core::quantities::Voltage> overVoltage) -> void
            {
                checkAgainstRating( voltage, currentLimit, overVoltage);

                //
                // Remembered before the wire, and remembered whether or not
                // there is a wire: these are what a journal line and a failure
                // message are written from (see describeConfig below), and an
                // Apply that threw on its way to the instrument still
                // happened as far as a report is concerned.
                //
                mOutputVoltage = voltage.value_or( core::quantities::Voltage{});
                mCurrentLimit  = currentLimit;
                mOverVoltage   = overVoltage;
                mEnabled       = true;

                if( isSimulated())
                {
                    return;
                }

                detail::program( session(), Output::Channel,
                                 voltage      ? std::optional{ voltage->value()      } : std::nullopt,
                                 currentLimit ? std::optional{ currentLimit->value() } : std::nullopt,
                                 overVoltage  ? std::optional{ overVoltage->value()  } : std::nullopt);
            }

            //
            // What a Remove does: the output off, and nothing else. The
            // setpoint is deliberately left where the script put it -- unlike
            // safe(), a Remove is a step in a script that knows what it is
            // doing and may well enable the same rail again two lines later.
            //
            auto removeOutput() -> void
            {
                mEnabled = false;

                if( isSimulated())
                {
                    return;
                }

                detail::disableOutput( session(), Output::Channel);
            }

            //
            // -- Two answers to "is this output live", and why there are two --
            //
            // Both questions are the electrical interlock's, and they are
            // asked from two different places with two different budgets.
            //
            // isEnabled() is what hal::energisedSourceAt() reads before a
            // low-impedance reading is routed to a pin a supply is cabled onto
            // (see hal/src/verbs/interlock.cpp, which reflects over every
            // instrument global in the program to find the one that lands
            // there). Its concept takes a *const* instrument, deliberately --
            // it is an observer on the measurement path -- so this reports what
            // this driver last commanded and performs no I/O.
            //
            // outputIsOn() asks the instrument, and is what isEnergised()
            // below uses: the Connect/Disconnect path, where a relay in this
            // rail's lead is about to move and the question is the difference
            // between a contact moving cold and one arcing. One round trip, on
            // a path that is about to actuate hardware either way, in exchange
            // for being right about the cases this process did not cause -- an
            // output left on by a previous run, switched on from the front
            // panel, or brought up by a coupled channel. A driver reading back
            // its own memory would call those cold and close a relay into
            // them.
            //
            // So the truthful-but-expensive answer is used exactly where
            // hardware is about to move, and the cheap one where a reading is
            // about to be taken. Making the measurement-side check ask the
            // instrument too is a change to that concept in hal rather than
            // something this driver can decide on its own, and the case for it
            // is weaker: routing a reading closes fabric relays, which the
            // Connect path has already been asked about.
            //
            // outputIsOn() is not const and not exception-free, for the same
            // reason: a supply that cannot be reached fails the Connect
            // instead of letting it proceed on an assumption.
            //
            [[nodiscard]]
            auto isEnabled() const -> bool
            {
                return mEnabled;
            }

            [[nodiscard]]
            auto outputIsOn() -> bool
            {
                if( isSimulated())
                {
                    return mEnabled;
                }

                return detail::outputIsOn( session(), Output::Channel);
            }

            // --- Test/simulation hooks -- real hardware has no such setters ---

            //
            // What the simulated output is delivering -- the current a real
            // instrument would report back, which no setpoint determines. An
            // attached instrument never reads this; setting it on one is not an
            // error and does nothing, the same shape every other driver in
            // this tree has.
            //
            auto setSimulatedOutputCurrent( const core::quantities::Current c) -> void
            {
                mSimOutputCurrent = c;
            }

            //
            // The last thing this driver was told to do, whether or not there
            // was an instrument to tell. Kept on an attached supply as well --
            // it is what describeConfig's journal line and a failure message
            // are written from, and re-querying the instrument to render a log
            // entry would be a round trip to learn what this process just said.
            //
            // Which makes these the *setpoint*, never the reading. What the
            // rail is actually at is measuredVoltage(), and on an attached
            // supply the two can legitimately differ -- a current-limited
            // output sits below its voltage setpoint, which is the whole point
            // of a limit.
            //
            [[nodiscard]]
            auto outputVoltage() const -> core::quantities::Voltage
            {
                return mOutputVoltage;
            }

            [[nodiscard]]
            auto currentLimit() const -> std::optional<core::quantities::Current>
            {
                return mCurrentLimit;
            }

            [[nodiscard]]
            auto overVoltageProtection() const -> std::optional<core::quantities::Voltage>
            {
                return mOverVoltage;
            }

        private:
            //
            // Refuse a setpoint beyond this output's badge -- see
            // RatingExceeded. Called by applyOutput before anything is sent,
            // on a simulated instrument as well as an attached one, which is
            // the entire point of it existing at all.
            //
            // The overvoltage trip level is checked against the voltage
            // rating too, and that is not the same check as the setpoint's: a
            // trip level above what the output can produce is not a
            // protection, it is a protection that can never fire, and a
            // script that wrote one believes the rail is guarded when it is
            // not.
            //
            auto checkAgainstRating( const std::optional<core::quantities::Voltage> & voltage,
                                     const std::optional<core::quantities::Current> & currentLimit,
                                     const std::optional<core::quantities::Voltage> & overVoltage) const -> void
            {
                const std::string instrument{ to_string( mId) };

                if( voltage && voltage->value() > Output::MaxVolts)
                {
                    throw RatingExceeded( instrument, "voltage", voltage->value(), Output::MaxVolts, "V");
                }

                if( overVoltage && overVoltage->value() > Output::MaxVolts)
                {
                    throw RatingExceeded( instrument, "overvoltage trip level", overVoltage->value(), Output::MaxVolts, "V");
                }

                if( currentLimit && currentLimit->value() > Output::MaxAmps)
                {
                    throw RatingExceeded( instrument, "current limit", currentLimit->value(), Output::MaxAmps, "A");
                }
            }

            InstrumentId                      mId;

            //
            // Where this output's box is, and the session to it -- shared with
            // the box's other outputs (see hal::BoxConnection).
            //
            BoxConnection                     mConnection;

            core::quantities::Voltage                 mOutputVoltage{};
            core::quantities::Current                 mSimOutputCurrent{};
            std::optional<core::quantities::Current>  mCurrentLimit;
            std::optional<core::quantities::Voltage>  mOverVoltage;
            bool                                      mEnabled{ false };
    };

    //
    // A rig's instrument.inc names instruments by these aliases rather than by
    // DcSource<...> directly -- partly readability, partly mechanical: the
    // INSTRUMENT( box, type, id, address, ...) macro in
    // hal/topology/active_instruments.hpp splits its arguments on top-level
    // commas, and DcSource<Output2, RelayIsolated> would arrive there as two
    // arguments. Plain identifiers sidestep the question entirely.
    //
    // Six, because the two axes are independent and a rig may need any
    // combination: three physical outputs, each either cabled straight through
    // or through an isolation relay. Written out rather than generated, so
    // that the list of what a row may say is the list a reader sees.
    //
    // The name says both facts in the order a bench engineer would: whether
    // there is a relay in the lead, then which output of the box it is. So
    //
    //     INSTRUMENT( BenchPsu, keysight_edu36311a::DirectOutput1, DcP5, Simulated{})
    //
    // reads as "the 6 V output, hard-wired", which is the whole of what that
    // row has to say beyond its id and its address.
    //
    using DirectOutput1 = DcSource<Output1, DirectWiring>;
    using DirectOutput2 = DcSource<Output2, DirectWiring>;
    using DirectOutput3 = DcSource<Output3, DirectWiring>;
    using RelayOutput1  = DcSource<Output1, RelayIsolated>;
    using RelayOutput2  = DcSource<Output2, RelayIsolated>;
    using RelayOutput3  = DcSource<Output3, RelayIsolated>;
} // namespace hal::keysight_edu36311a
