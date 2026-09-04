#pragma once

#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <variant>

#include "core/driver/port.hpp"
#include "core/quantities/quantity.hpp"
#include "core/quantities/quantity_kind.hpp"

#include "hal/driver/address.hpp"
#include "hal/driver/api_version.hpp"
#include "hal/driver/instrument.hpp"
#include "hal/io/scpi.hpp"

//
// Which hal API version this driver was written against -- a literal, never
// THORIUM_HAL_API_VERSION itself, which would only assert that this hal matches
// this hal. See hal/driver/api_version.hpp for what the number means and when it
// moves, and instruments/README.md for why a driver package has to say this at
// all (it travels separately from the hal it compiles against).
//
// Three, not one: this is the first driver here that opens a real session, so
// it is the first that needs hal/io/ (see the version log in api_version.hpp).
THORIUM_REQUIRE_HAL_API( 3);

//
// This driver's own namespace, nested inside hal -- see instruments/README.md
// for the rule and the collision that produced it. The name carries the
// manufacturer as well as the model, and here that matters more than usual:
// "34450A" alone would not even distinguish this meter from its own sibling,
// the Keysight 34450A whose command set it shares.
//
namespace hal::keysight_edu34450a
{
    //
    // The meter's measurement functions, as its SCPI CONFigure subsystem names
    // them -- one enumerator per thing the instrument can be switched to.
    //
    // Deliberately not the same set as this class's port accessors, and the
    // two places they differ are the two places the instrument disagrees with
    // the framework. voltage() and acVoltage() return the same
    // core::Port<Voltage> and select two different functions here (the
    // difference is instrument state -- see mMode); resistance() and
    // fourWireResistance() likewise. So a port cannot be what a command is
    // built from, and this enum is what is.
    //
    // Its own scope-level enum rather than a member of EDU34450A, so that the
    // file-local command table in src/keysight_edu34450a.cpp can be a plain
    // constexpr function over it. Nothing outside this driver names it -- the
    // vocabulary a script uses is still the ports.
    //
    enum class Function
    {
        DcVoltage,
        AcVoltage,
        DcCurrent,
        AcCurrent,
        Resistance,
        FourWireResistance,
        Frequency,
        Capacitance
    };

    //
    // Keysight EDU34450A: the 5.5-digit dual-display bench DMM from the Smart
    // Bench Essentials line. It measures eleven input signals -- DC and true
    // RMS AC volts, DC and true RMS AC amps, two- and four-wire ohms,
    // frequency, continuity, diode test, temperature and capacitance -- of
    // which this class exposes the eight that this framework has a quantity
    // for (see "What is deliberately not here" below).
    //
    // It shares its SCPI dialect with the Keysight 34450A, whose programmer's
    // reference is the document the comments here cite; the EDU is that
    // instrument's education-market packaging, not a different command set.
    //
    // -- Why this is not simply another L4411A --------------------------------
    //
    // Dmm1 and Dmm2 used to be two L4411A instances, and
    // Dmm2 still is (see rig/instrument.inc). The two meters are close enough
    // that a script measuring a rail cannot tell them apart, and far enough
    // apart that one C++ type would have had to lie about one of them:
    //
    //   digits       5.5 here, 6.5 on the L4411A -- a criterion tightened to
    //                the L4411A's resolution is not a criterion this meter can
    //                answer.
    //
    //   integration  the L4411A integrates over power-line cycles and takes an
    //                NPLC. This family has no NPLC command at all: resolution
    //                is one of exactly three discrete values, and that is what
    //                Resolution below names. See setResolution().
    //
    //   functions    this meter adds capacitance, temperature and a secondary
    //                display; the L4411A has none of those and reaches further
    //                in range and accuracy instead.
    //
    // Which is the argument this codebase already made when it retired the
    // generic hal::Dmm placeholder: once the real model is known, naming the
    // class after it documents the non-portability of its measurement-function
    // set rather than pretending a DMM driver is interchangeable across models.
    // Two meters on one bench being two different C++ types is that argument
    // arriving where it was always heading.
    //
    // -- This is the driver that talks -----------------------------------------
    //
    // Every driver in this tree answered its readings out of its own
    // mSimVoltage and friends, and this is the first one that opens a real
    // session instead: SCPI over a raw socket to the meter's port 5025, using
    // hal/io/ (see hal/io/transport.hpp for the layer and
    // framework/hal/README.md for what it does and does not implement).
    //
    // Which one it does is decided by the address column of the rig's own
    // instrument table and by nothing else. hal::Simulated means the hooks
    // below are the instrument; hal::Lan means the meter at that hostname is.
    // A script cannot tell the difference and no script should have to -- see
    // isSimulated(), and rig/instrument.inc for what that now means for a rig
    // whose rack does not exist yet.
    //
    // The simulation hooks stay, and are not a legacy: they are what every
    // test of every script in this repository reads through, they are what
    // --replay and --inject fall back on when nothing is attached, and they are
    // the only version of this meter that exists in CI. What they are not is a
    // *fallback*: an instrument this driver was told to reach and could not
    // raises hal::io::TransportError and fails the run, rather than quietly
    // answering 0 V. A rig that reads zero volts off a meter that was never
    // connected is the failure mode this whole framework is written against.
    //
    // What the meter is told, in full, for one reading of a routed rail:
    //
    //     *IDN?                     once per session -- and refused if the
    //                               model is not this one (verifyIdentity)
    //     SYST:ERR?                 once per session, until empty: whatever
    //                               the last user left queued is not ours
    //     CONF:VOLT:DC 10,1.5E-6    the function, the range and the resolution
    //     SYST:ERR?                 did it accept that
    //     READ?                     trigger, and hand back the reading
    //
    // Five commands for the first reading and three for each one after it,
    // which is the shape a bench engineer would recognise from typing the same
    // thing into a terminal. Nothing clever, and deliberately no state cached
    // between readings: this driver reconfigures the function before every
    // reading rather than remembering that it already did. That costs one
    // round trip -- about a millisecond against the 20 ms a 5.5-digit reading
    // takes -- and buys the thing worth having, which is that a reading does
    // not depend on what the run did before it, or on whether somebody pressed
    // a front-panel key in between.
    //
    // -- The one sharp edge, unchanged from the L4411A ------------------------
    //
    // AC/DC mode and 2-wire/4-wire resistance mode are stored on the
    // instrument, not the port -- calling .acVoltage()/.acCurrent() switches
    // this meter into AC, .voltage()/.current() switch it back to DC, and
    // .resistance()/.fourWireResistance() switch between 2-wire and 4-wire
    // ohms, exactly the way the front-panel function keys work and exactly
    // what SCPI's FUNC "VOLT:AC" does. So a port handle obtained before a mode
    // switch reads whichever mode is current when rawMeasure() is eventually
    // called, not the mode active when the handle was created. That never
    // matters for real usage, since Measure(port, at(...)) reads a port
    // immediately and discards it, and it is tested rather than designed away.
    //
    // -- What is deliberately not here ----------------------------------------
    //
    // temperature   core does have a Temperature, but this function is a
    //               2-wire measurement of a 5 kOhm thermistor plugged into the
    //               meter's own front terminals, auto-ranging only. It is not a
    //               reading that can travel through the matrix to a DUT pin, so
    //               a port for it would be one nothing on this rig could
    //               legitimately route (see rig/wiring.inc, and TAP_WIRING for
    //               the shape a directly-bolted lead would take if one ever
    //               appeared).
    //
    // continuity /  both are threshold answers rather than measurements -- a
    // diode test    fixed 10 Ohm continuity threshold with a beeper behind it,
    //               and a forward-voltage check on a fixed 1 V range, both
    //               available in fast mode only. What a script wants from
    //               either is a verdict, and this framework already spells a
    //               verdict as a criterion over a real reading (see
    //               core/criteria/criterion.hpp): resistance() with a
    //               LT( 10_Ohm) says the same thing and says it in the
    //               vocabulary the report is written in.
    //
    // secondary     SCPI reaches the second display through a whole parallel
    // display       command tree (SEC:FUNC, CONF:SEC:..., MEAS:SEC:...?) and it
    //               is a genuinely useful thing -- one acquisition answering
    //               "DC volts and the AC ripple on them" at once. It is also a
    //               second reading per port, which core::Port has no shape for:
    //               a port is one quantity. Exposing it means a second port
    //               accessor pair and a decision about how two readings key
    //               into one session slot, which is a design question and not a
    //               driver detail.
    //
    class EDU34450A : public InstrumentTag
    {
        public:
            enum class Mode           { Dc, Ac };
            enum class ResistanceMode { TwoWire, FourWire };

            //
            // The whole of this meter's speed/precision axis, and the reason
            // core::MeasureSetup::Nplc means nothing here.
            //
            // An L4411A is told how many power-line cycles to integrate over,
            // and any number is a number it can be told. This family instead
            // accepts exactly three resolutions per function -- SCPI
            // VOLT:DC:RES takes 1.50E-6, 2.00E-5 or 3.00E-5 and nothing else --
            // which the front panel and the data sheet both name Slow, Medium
            // and Fast:
            //
            //   Slow    5.5 digits, ~1.3 readings/s on DCV, 60 dB of normal-
            //           mode rejection at the line frequency. What the
            //           instrument resets to, and what its accuracy
            //           specifications are quoted at.
            //   Medium  4.5 digits, ~49 readings/s, still 60 dB rejection.
            //   Fast    4.5 digits, up to 110 readings/s, and no normal-mode
            //           rejection at all (0 dB) -- line hum lands in the
            //           reading. Right for sequencing through many pins, wrong
            //           for a rail tolerance check.
            //
            // Named for what the instrument calls them rather than for their
            // digit counts, because Medium and Fast are both 4.5 digits and a
            // pair of enumerators that differed only in a suffix would be worse
            // at saying which is which.
            //
            enum class Resolution     { Slow, Medium, Fast };

            //
            // LAN or USB, and nothing else: gigabit LAN and a USB device port
            // are what this model's back panel has. No GPIB connector and no
            // option that adds one -- the 34450A whose command set this shares
            // does offer GPIB (its SCPI even has SYST:COMM:GPIB:ADDR), and
            // this is the box that does not. So a rig row addressing it over
            // GPIB fails to compile rather than failing to open; see
            // hal::ReachableOver in hal/driver/address.hpp,
            // which is also where the reasoning lives for why the bus *kind* is
            // checked here and the address itself is just a value this driver
            // carries.
            //
            // Note what this does NOT constrain: the front-panel USB host port
            // is for a flash drive holding saved setups, and is not a way for
            // the PC to reach the instrument. hal::Usb here means the rear
            // USBTMC device port.
            //
            template<typename AddressT>
                requires ReachableOver<AddressT, Lan, Usb>
            EDU34450A( const InstrumentId id, const AddressT address) : mId( id), mAddress( address) {}

            //
            // Where the PC reaches this meter -- and, since this driver grew a
            // transport, the column that decides whether a reading comes off
            // hardware or out of the simulation hooks below. See isSimulated().
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
            // The one branch in this driver that decides between a socket and
            // an mSimVoltage, and it is a property of the *address* rather than
            // a mode a caller sets. Which is the whole point: a rig says what
            // it has once, in its instrument table, and a script measuring a
            // rail is identical either way (see rig/instrument.inc, and
            // hal/driver/address.hpp on what hal::Simulated means).
            //
            // An injected transport wins over a Simulated address, deliberately
            // -- see useTransport(). That is how this driver's own tests assert
            // the SCPI it sends without a bench: Simulated{} for the address,
            // because there is genuinely no bench, plus a fake transport,
            // because the question under test is what would have been sent to
            // one.
            //
            [[nodiscard]]
            auto isSimulated() const -> bool
            {
                return !mSession && std::holds_alternative<Simulated>( mAddress);
            }

            //
            // Hand this driver a transport to talk through, instead of one
            // opened from its address.
            //
            // Two callers, and neither is a script. A test hands in a fake and
            // asserts the command strings (see this driver's tests). A rig with
            // a VISA installation, or one reaching an instrument over a bus
            // hal::io::openTransport() does not implement, hands in its own
            // hal::io::ITransport and gets this driver working over it with no
            // change here -- which is the reason the transport is an interface
            // at all.
            //
            // Replaces any session already open, closing it: two transports to
            // one instrument is not a state this models, and silently keeping
            // the first would make the call look like it worked.
            //
            // Sends nothing: the identity check and the error-queue drain that
            // every session gets belong to the first *use*, not to this call
            // (see session()). A setter that did I/O would make handing in a
            // fake transport a thing that could throw.
            auto useTransport( std::unique_ptr<io::ITransport> transport) -> void
            {
                mSession  = std::make_unique<io::ScpiSession>( std::move( transport));
                mPrepared = false;
            }

            //
            // The live SCPI session, opened on first use.
            //
            // Lazily, and that is not an optimisation -- it is the only thing
            // that can work. The rig's instruments are globals constructed
            // before main() (see hal/topology/active_instruments.hpp), so a
            // constructor that opened a socket would make every binary that
            // links the rig -- every unit test, --replay, --skeleton, --help --
            // try to reach the bench at static-initialisation time, and would
            // throw from a constructor with nowhere to catch it.
            //
            // So the connection happens at the first reading that needs one,
            // which is also exactly when a detached run (--replay, --inject,
            // --skeleton) does not need one: a scripted session never invokes
            // the live read, so nothing here is ever opened. See
            // core/session/bench.hpp.
            //
            // Public, because a bring-up session on a desk wants it: a
            // deployment test or a diagnostic can send this meter a command
            // this driver has no accessor for -- CALC:NULL, DISP:TEXT, the
            // secondary display -- without that becoming a reason to widen the
            // driver. Throws hal::io::TransportError if the meter cannot be
            // reached, and hal::io::ScpiFault if it answers *IDN? with a model
            // this driver is not for (see verifyIdentity()).
            //
            [[nodiscard]]
            auto session() -> io::ScpiSession &;

            //
            // *IDN? -- "Keysight Technologies,EDU34450A,MY12345678,..." off the
            // real instrument, opening the session if it is not already open.
            //
            // Worth having as its own accessor rather than leaving callers to
            // spell session().identify(): what a run's traceability header
            // should carry about an instrument is what the instrument says it
            // is, not what the rig table hoped it was, and this is the one
            // sentence that says so.
            //
            [[nodiscard]]
            auto identity() -> std::string;

            //
            // Drop the session, closing the socket. The next reading opens a
            // new one.
            //
            // For a long-lived process that should not hold a bench instrument
            // between runs, and for recovering from a wedged instrument -- on a
            // raw socket, closing and reopening is the whole of the recovery
            // vocabulary (see hal/io/socket_transport.hpp). Not called by
            // safe(): see there.
            //
            auto closeSession() -> void
            {
                mSession.reset();
                mPrepared = false;
            }

            [[nodiscard]]
            auto id() const -> InstrumentId
            {
                return mId;
            }

            [[nodiscard]]
            auto voltage() -> core::Port<core::quantities::Voltage, EDU34450A>
            {
                mMode = Mode::Dc;
                return core::Port<core::quantities::Voltage, EDU34450A>{ *this };
            }

            [[nodiscard]]
            auto current() -> core::Port<core::quantities::Current, EDU34450A>
            {
                mMode = Mode::Dc;
                return core::Port<core::quantities::Current, EDU34450A>{ *this };
            }

            //
            // True RMS, and worth saying rather than assuming: this reads the
            // AC component of whatever is on the terminals regardless of
            // waveshape, AC-coupled with up to 400 V of DC bias riding under
            // it. The data sheet's accuracies assume a sine and carry a crest-
            // factor adder for anything else.
            //
            [[nodiscard]]
            auto acVoltage() -> core::Port<core::quantities::Voltage, EDU34450A>
            {
                mMode = Mode::Ac;
                return core::Port<core::quantities::Voltage, EDU34450A>{ *this };
            }

            [[nodiscard]]
            auto acCurrent() -> core::Port<core::quantities::Current, EDU34450A>
            {
                mMode = Mode::Ac;
                return core::Port<core::quantities::Current, EDU34450A>{ *this };
            }

            //
            // 2-wire ohms: the meter's own lead resistance is included in the
            // reading, same as pressing the front-panel 2W key. The data
            // sheet's resistance accuracies are quoted with the NULL function
            // on; without it, add 0.2 Ohm.
            //
            [[nodiscard]]
            auto resistance() -> core::Port<core::quantities::Resistance, EDU34450A>
            {
                mResistanceMode = ResistanceMode::TwoWire;
                return core::Port<core::quantities::Resistance, EDU34450A>{ *this };
            }

            //
            // 4-wire (Kelvin) ohms: separate sense leads cancel out lead
            // resistance, same as pressing the front-panel 4W key --
            // requiresSensePath() (see core/driver/port.hpp) is what actually
            // tells core::MeasureEngine to route this instrument's sense
            // channel and the DUT point's sense channel alongside the normal
            // force path, only for this one reading. A plain resistance() call
            // above never sets it, so a 2-wire reading on this same instrument
            // never touches sense at all.
            //
            // Much slower than 2-wire here, at every resolution: about 6
            // readings/s against 110 in fast mode, and about 1 against 1.3 in
            // slow. That is a hardware fact rather than anything this driver
            // does, and it is the reason a scan of many pins is usually 2-wire
            // with a NULL rather than 4-wire.
            //
            [[nodiscard]]
            auto fourWireResistance() -> core::Port<core::quantities::Resistance, EDU34450A, core::SensePath::Required>
            {
                mResistanceMode = ResistanceMode::FourWire;
                return core::Port<core::quantities::Resistance, EDU34450A>{ *this }.requiresSensePath();
            }

            //
            // The FREQ function -- SCPI FUNC "FREQ", a counter on whatever is
            // already at the terminals, specified 20 Hz to 300 kHz off the
            // voltage input and 20 Hz to 10 kHz off the current one.
            //
            // Not to be confused with core::Port::frequency(), which is a
            // *setting* -- "the AC signal you are about to measure is at 50 Hz"
            // -- for meters that need telling. This one is a reading. The two
            // spellings sitting a dot apart is unfortunate and the names are
            // both right: Dmm1.frequency() asks the meter how fast the signal
            // is, Dmm1.acVoltage().frequency( 50_Hz) tells it. This model needs
            // no telling and ignores the setting (see rawMeasure below).
            //
            // Its own function rather than a mode of acVoltage(), because that
            // is what the instrument does: FUNC "FREQ" replaces the voltage
            // function outright rather than adding to it. Reading both at once
            // is what the secondary display is for, and that is not exposed --
            // see this class's own comment.
            //
            [[nodiscard]]
            auto frequency() -> core::Port<core::quantities::Frequency, EDU34450A>
            {
                return core::Port<core::quantities::Frequency, EDU34450A>{ *this };
            }

            //
            // The CAP function -- SCPI FUNC "CAP", 1 nF to 10 mF over eight
            // ranges. The one function on this meter that neither the L4411A
            // nor anything else on this bench has, and the reason core grew a
            // farad (see core::quantities::Capacitance).
            //
            // Measured the way a DMM measures a capacitance and not the way an
            // LCR bridge does: a known current into the node -- 100 nA on the
            // 1 nF range, 1 mA on the 10 mF one -- and the slope of the ramp
            // that current produces. Two consequences a script author should
            // know before trusting a reading:
            //
            //   it is a *sourcing* measurement, so the node has to be dead.
            //   core::requiresDeadNode names Capacitance for exactly this
            //   (core/verbs/interlock.hpp), which means a routed reading at a
            //   pin an energised supply is cabled onto is refused before
            //   anything closes rather than attempted. Remove the source
            //   first, or read the meter's own terminals with the point-free
            //   Measure overload.
            //
            //   it is a two-terminal DC measurement, so it reads the whole
            //   node: anything in parallel with the part is in the answer, and
            //   a leaky capacitor reads high because the leakage adds to the
            //   charging current. The data sheet's accuracies assume a NULL of
            //   the open test leads first -- on the 1 nF range the leads
            //   themselves are a meaningful fraction of full scale.
            //
            // No 4-wire variant, and no sense path: the instrument has one
            // capacitance function and it is 2-wire. Kelvin sensing answers a
            // question about lead resistance, which is not what limits this
            // measurement.
            //
            [[nodiscard]]
            auto capacitance() -> core::Port<core::quantities::Capacitance, EDU34450A>
            {
                return core::Port<core::quantities::Capacitance, EDU34450A>{ *this };
            }

            [[nodiscard]]
            auto mode() const -> Mode
            {
                return mMode;
            }

            [[nodiscard]]
            auto resistanceMode() const -> ResistanceMode
            {
                return mResistanceMode;
            }

            //
            // Instrument state rather than a per-reading port setting, and
            // deliberately so. core::MeasureSetup is the per-reading channel,
            // and its precision field is Nplc -- a count of power-line cycles
            // this instrument has no command for. Squeezing Slow/Medium/Fast
            // into an integer NPLC would be a translation this driver invented,
            // wrong in both directions: a script asking for .nplc( 10) would
            // silently get a resolution the meter chose, and a reader of that
            // script would believe the rig had integrated for ten cycles.
            //
            // So a setter, in the shape hal::keysight_dsox1202g::DSOX1202G's
            // setMode() already uses for the same reason -- one front-panel
            // knob, set before a reading and left where it was put. A script
            // that wants a rail measured properly sets Slow once and reads;
            // one sequencing a hundred continuity checks sets Fast once.
            //
            // The instrument keeps a resolution per function and restores it on
            // returning to that function; one value covers the whole meter
            // here, and that difference is now invisible rather than
            // unmodelled -- every reading configures its own function before
            // taking it (see read()), so whatever the instrument remembered is
            // overwritten with this value each time. What the difference would
            // cost is a script that set the resolution from the front panel
            // between readings and expected it to stick.
            //
            auto setResolution( const Resolution resolution) -> void
            {
                mResolution = resolution;
            }

            [[nodiscard]]
            auto resolution() const -> Resolution
            {
                return mResolution;
            }

            //
            // Nothing to do, and that is a hardware fact rather than an
            // unfinished implementation: a DMM is a passive instrument -- it
            // sources nothing into the DUT, so there is no output for
            // hal::safeRig() (see hal/verbs/safing.hpp) to drop. Its leads are
            // taken off the DUT by the fabric's own openAll(), which safeRig()
            // does after this, not by anything this instrument is asked to do.
            //
            // Written out as an explicit empty body rather than left off and
            // detected as absent, on purpose -- see
            // hal::racal1260::Racal1260::safe(), the other passive driver on this
            // bench, for the same deliberately-empty body -- and
            // hal::SafeableInstrument in hal/driver/instrument.hpp.
            //
            // Still empty now that there is a socket to close, and that is a
            // decision worth stating. Safing answers "leave the DUT in a state
            // nobody can be hurt by", which is about outputs and relays; a
            // dangling TCP connection endangers nothing, and dropping it here
            // would mean the safing pass on a failed run also destroyed the
            // session whose error queue is the best evidence of what went
            // wrong. Closing is closeSession(), and it is the caller's call.
            //
            auto safe() -> void
            {
            }

            //
            // Test/simulation hooks -- real hardware has no such setters, and
            // an instrument with a session open never reads them (see
            // rawMeasure). Setting one on an attached instrument is not an
            // error and does nothing, which is the same shape every other
            // driver in this tree has.
            //
            auto setSimulatedVoltage( const core::quantities::Voltage v) -> void
            {
                mSimVoltage = v;
            }

            auto setSimulatedCurrent( const core::quantities::Current c) -> void
            {
                mSimCurrent = c;
            }

            auto setSimulatedAcVoltage( const core::quantities::Voltage v) -> void
            {
                mSimAcVoltage = v;
            }

            auto setSimulatedAcCurrent( const core::quantities::Current c) -> void
            {
                mSimAcCurrent = c;
            }

            auto setSimulatedResistance( const core::quantities::Resistance r) -> void
            {
                mSimResistance = r;
            }

            auto setSimulatedFourWireResistance( const core::quantities::Resistance r) -> void
            {
                mSimFourWireResistance = r;
            }

            auto setSimulatedFrequency( const core::quantities::Frequency f) -> void
            {
                mSimFrequency = f;
            }

            auto setSimulatedCapacitance( const core::quantities::Capacitance c) -> void
            {
                mSimCapacitance = c;
            }

            //
            // One reading, off the instrument if there is one and out of the
            // simulation hooks if there is not.
            //
            // What became of core::MeasureSetup's five fields, now that they
            // reach real hardware:
            //
            //   Range      is sent, as the CONFigure command's <range>
            //              argument, in the port's own unit -- .range( 10_V)
            //              becomes "CONF:VOLT:DC 10". Not calling it leaves the
            //              meter autoranging, which is the instrument's own
            //              reset default. A value between two ranges is not
            //              rounded up by this driver: the instrument accepts
            //              only its own eight-or-so discrete range values and
            //              answers -222 "Data out of range" for anything else,
            //              which arrives as an hal::io::ScpiFault naming the
            //              command. Rounding here would be this driver
            //              inventing a range the script did not ask for.
            //
            //   Nplc       is still ignored, and now for a reason a reader can
            //              check against the wire rather than a promise: there
            //              is no NPLC command in this model's SCPI at all. Its
            //              speed/precision axis is setResolution(), which is
            //              instrument state rather than a per-reading setting
            //              -- see Resolution above for the whole argument.
            //
            //   Frequency  is ignored: this meter's AC path needs no hint about
            //              the signal it is looking at. (Not to be confused
            //              with a *frequency reading*, which is its own
            //              function -- see frequency().)
            //
            //   Low/High   are ignored, and are meaningless here: they are edge
            //   Threshold  timing thresholds for a scope's rise-time
            //              measurement.
            //
            // The reading itself comes back through READ?, which is CONFigure's
            // documented partner: configure, then trigger-and-return. An
            // overload -- the input beyond the range it was told to use --
            // arrives as core::UnmeasurableReading carrying the instrument's
            // own condition, which core::MeasureEngine catches and records as
            // NaN with the reason beside it, so the run carries on to the next
            // check (see core/driver/port.hpp).
            //
            template<core::quantities::QuantityType QuantityT>
            [[nodiscard]]
            auto rawMeasure( const core::MeasureSetup<QuantityT> & setup) -> QuantityT
            {
                if( isSimulated())
                {
                    return simulatedReading<QuantityT>();
                }

                //
                // The range goes down the wire as a bare number in the port's
                // own unit, which is what .value() already is: a Voltage holds
                // volts, a Capacitance holds farads, and the meter's
                // <range> argument is in exactly those units. No conversion,
                // and nowhere for one to be wrong.
                //
                const std::optional<double> range =
                    setup.Range ? std::optional<double>{ setup.Range->value() } : std::nullopt;

                return QuantityT{ read( functionFor<QuantityT>(), range) };
            }

        private:
            //
            // Which of the instrument's functions answers a given port -- the
            // whole mapping from this framework's vocabulary onto the meter's,
            // and the one place the DC/AC and 2-wire/4-wire instrument state is
            // read.
            //
            // A template over the quantity rather than a runtime switch,
            // because the quantity is what the *call site* fixed:
            // Measure( Dmm1.voltage(), ...) is a Voltage at compile time, so a
            // quantity this meter has no function for is a compile error naming
            // the driver -- which is the static_assert at the bottom, and is
            // the same guarantee the simulated version of this always gave.
            //
            template<core::quantities::QuantityType QuantityT>
            [[nodiscard]]
            auto functionFor() const -> Function
            {
                if constexpr( std::is_same_v<QuantityT, core::quantities::Voltage>)
                {
                    return mMode == Mode::Ac ? Function::AcVoltage : Function::DcVoltage;
                }
                else if constexpr( std::is_same_v<QuantityT, core::quantities::Current>)
                {
                    return mMode == Mode::Ac ? Function::AcCurrent : Function::DcCurrent;
                }
                else if constexpr( std::is_same_v<QuantityT, core::quantities::Resistance>)
                {
                    return mResistanceMode == ResistanceMode::FourWire
                        ? Function::FourWireResistance
                        : Function::Resistance;
                }
                else if constexpr( std::is_same_v<QuantityT, core::quantities::Frequency>)
                {
                    //
                    // Not switched on mMode: FREQ is its own function on this
                    // meter, not the AC half of the voltage one, so what the
                    // DC/AC keys were last set to says nothing about it.
                    //
                    return Function::Frequency;
                }
                else if constexpr( std::is_same_v<QuantityT, core::quantities::Capacitance>)
                {
                    // Its own function too, and likewise unaffected by mMode.
                    return Function::Capacitance;
                }
                else
                {
                    static_assert( !sizeof( QuantityT), "EDU34450A has no port for this quantity");
                }
            }

            //
            // The same mapping again, onto the simulation hooks -- what a
            // reading is when there is no instrument.
            //
            // Written out a second time rather than derived from
            // functionFor(): a switch over Function returning one of eight
            // members would have to return one *type*, and these are eight
            // different Quantity types. The duplication is between two
            // if-constexpr chains that a compile error keeps in step -- add a
            // port and neither compiles until both know about it.
            //
            template<core::quantities::QuantityType QuantityT>
            [[nodiscard]]
            auto simulatedReading() const -> QuantityT
            {
                if constexpr( std::is_same_v<QuantityT, core::quantities::Voltage>)
                {
                    return mMode == Mode::Ac ? mSimAcVoltage : mSimVoltage;
                }
                else if constexpr( std::is_same_v<QuantityT, core::quantities::Current>)
                {
                    return mMode == Mode::Ac ? mSimAcCurrent : mSimCurrent;
                }
                else if constexpr( std::is_same_v<QuantityT, core::quantities::Resistance>)
                {
                    return mResistanceMode == ResistanceMode::FourWire ? mSimFourWireResistance : mSimResistance;
                }
                else if constexpr( std::is_same_v<QuantityT, core::quantities::Frequency>)
                {
                    return mSimFrequency;
                }
                else if constexpr( std::is_same_v<QuantityT, core::quantities::Capacitance>)
                {
                    return mSimCapacitance;
                }
                else
                {
                    static_assert( !sizeof( QuantityT), "EDU34450A has no port for this quantity");
                }
            }

            //
            // Configure the meter for one function and read it -- the whole of
            // this driver's real I/O, and the only member here that is not
            // inline.
            //
            // Not a template, on purpose: nothing about talking to the
            // instrument depends on which C++ type the answer is going to be
            // wrapped in, so all of it -- the command table, the range
            // formatting, the resolution rules, the overload check -- lives in
            // one ordinary .cpp where it can be read as a unit and where the
            // command strings are not re-instantiated per quantity. See
            // src/keysight_edu34450a.cpp.
            //
            [[nodiscard]]
            auto read( Function function, std::optional<double> range) -> double;

            //
            // Ask the instrument what it is and refuse anything this driver is
            // not for. Called once, when the session is opened.
            //
            [[nodiscard]]
            auto verifyIdentity() -> std::string;

            InstrumentId                  mId;
            Address                       mAddress;

            //
            // Null until the first reading that needs hardware, and null
            // forever on a simulated instrument -- see session() on why it
            // cannot be opened in the constructor, and isSimulated() on what
            // its being null means.
            //
            // A unique_ptr, which makes this class non-copyable, and that is
            // correct rather than incidental: a copy of a driver would be a
            // second object claiming the same instrument, and one of the two
            // would hold the socket. Nothing copies one -- the rig's
            // instruments are globals and core::Port holds a reference (see
            // core/driver/port.hpp) -- so the restriction costs nothing and
            // removes a mistake.
            //
            std::unique_ptr<io::ScpiSession> mSession;

            //
            // Whether mSession has had the once-per-session exchange -- the
            // error-queue drain and the identity check. A flag beside the
            // pointer rather than something done where the session is created,
            // because a session arrives two ways: opened from the address, or
            // handed in by useTransport(). Both have to be prepared, and only
            // one of them goes through openTransport(), so the guarantee has to
            // live at the point of *use*. See session().
            //
            bool                             mPrepared{ false };

            Mode                          mMode{ Mode::Dc };
            ResistanceMode                mResistanceMode{ ResistanceMode::TwoWire };
            //
            // Slow, matching what the instrument resets to and what its
            // accuracy specifications are quoted at -- a default that is wrong
            // in the safe direction, since the failure it cannot cause is a
            // reading that quietly arrived with a digit less than the criterion
            // checking it assumes.
            //
            Resolution                    mResolution{ Resolution::Slow };
            core::quantities::Voltage     mSimVoltage{};
            core::quantities::Current     mSimCurrent{};
            core::quantities::Voltage     mSimAcVoltage{};
            core::quantities::Current     mSimAcCurrent{};
            core::quantities::Resistance  mSimResistance{};
            core::quantities::Resistance  mSimFourWireResistance{};
            core::quantities::Frequency   mSimFrequency{};
            core::quantities::Capacitance mSimCapacitance{};
    };
} // namespace hal::keysight_edu34450a
