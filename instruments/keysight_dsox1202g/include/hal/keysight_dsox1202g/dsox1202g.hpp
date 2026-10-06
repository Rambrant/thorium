#pragma once

#include <array>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <variant>

#include "core/driver/port.hpp"
#include "core/meta.hpp"
#include "core/quantities/quantity.hpp"
#include "core/quantities/quantity_kind.hpp"
#include "core/quantities/waveform.hpp"

#include "hal/driver/address.hpp"
#include "hal/driver/box_connection.hpp"
#include "hal/driver/instrument.hpp"
#include "hal/io/scpi.hpp"

#include "hal/keysight_dsox1202g/vocabulary.hpp"
#include "hal/keysight_dsox1202g/trigger.hpp"
#include "hal/keysight_dsox1202g/timebase.hpp"
#include "hal/keysight_dsox1202g/acquisition.hpp"
#include "hal/keysight_dsox1202g/channel.hpp"
#include "hal/keysight_dsox1202g/single.hpp"
#include "hal/keysight_dsox1202g/waveform.hpp"

//
// The instrument itself: DSOX1202G, its session, the configure/arm/await/
// fetch methods the verbs end up in, the simulation hooks and the settings
// readback. Everything that goes on the wire is in src/keysight_dsox1202g.cpp.
//
// Part of hal/keysight_dsox1202g.hpp -- include that, not this.
//

namespace hal::keysight_dsox1202g
{
    //
    // Keysight InfiniiVision DSOX1202G: this rig's actual scope -- two
    // channels, 2 GSa/s, 70 MHz as shipped and licence-upgradable to 200 --
    // replacing hal::keysight_dsox1202g::DSOX1202G, which was this bench's scope
    // when the only thing known about it was the legacy ATE script it had to
    // reproduce.
    //
    // That driver is still in the tree and still builds, which is deliberate:
    // it is a working, tested driver for a real instrument, and the day a
    // second bench has an Infiniium on it, it is what that bench uses. What
    // changed is which one rig/instrument.inc names.
    //
    // What it does, in the four groups a script uses:
    //
    //   Setup  -- :TRIGger, :TIMebase, :ACQuire instrument-wide, and
    //             :CHANnel<N> per input. Four builders, one per subsystem;
    //             see the subsystem headers beside this one for why they are four and not
    //             one.
    //   Arm /  -- :SINGle, bracketing whatever event the script causes. See
    //   Await     core/verbs/acquire.hpp for why they are two verbs, and
    //             armSingle/awaitAcquisition below for the exact register
    //             sequence this instrument's guide prescribes.
    //   Measure-- the :MEASure amplitude and timing families, channel-scoped
    //             via channel<N>()/Channel (channel_view.hpp).
    //   Fetch  -- :WAVeform, the whole captured record off one channel rather
    //             than one number measured from it. See core/verbs/trace.hpp.
    //
    // Calling one of the Mode-tagged measurement methods switches the
    // instrument's current measurement mode *and* channel, the same way the
    // front-panel Meas menu switches which measurement is installed before a
    // reading is taken. Same accepted sharp edge hal::keysight_edu34450a::EDU34450A's
    // AC/DC mode has: a port handle read after a later mode or channel switch
    // reads whichever is current at rawMeasure() time, not whichever was
    // active when the handle was obtained -- harmless for
    // Measure( port, at( ...))'s read-immediately-and-discard usage.
    //
    // -- It talks ------------------------------------------------------------
    //
    // Every command below goes out over a real SCPI session when this driver is
    // constructed with a real address (see isSimulated(), and
    // src/keysight_dsox1202g.cpp for everything that is actually sent). USBTMC
    // through whatever VISA the bench has, because that is this instrument's
    // one connector -- hal::io::openTransport routes hal::Usb there (see
    // hal/io/visa_transport.hpp on why VISA rather than libusb).
    //
    // What a session does, once, before the first command that needs it:
    //
    //     SYST:ERR?                 until empty -- whatever the last user of
    //                               this scope left queued is not this run's
    //     *IDN?                     and refused if the model is not this one
    //                               (verifyIdentity)
    //
    // The simulation hooks below are still here and still what a driver test
    // uses, and an attached instrument never reads them: rawMeasure() branches
    // on isSimulated() once, and every other path in this class does the same.
    //
    // rig/instrument.inc still says hal::Simulated{} for this row, and that is
    // now the same statement Dmm1's row makes rather than a different one:
    // this driver reads its address column, so the column may not name a bus
    // address until there is a serial number to put in it. See that file.
    //
    // Also still deliberately deferred:
    //   - Segmented acquisition (:ACQuire:MODE SEGMented) -- a licensed
    //     option, see AcquisitionType.
    //   - The waveform generator is not this class: it is a source, so it is
    //     a second instrument sharing this one's box, hal::keysight_dsox1202g::
    //     WGEN (wgen.hpp), with its own row, session family and safing.
    //   - Duty cycle, overshoot, preshoot -- see Channel's own comment on why
    //     these wait on a core decision, not a driver one.
    //   - The trigger kinds beyond edge (:TRIGger:MODE GLITch / PATTern / TV /
    //     and this model's serial-bus triggers). Edge is what this bench
    //     triggers on; the others are a large surface each with its own
    //     parameter set.
    //
    class DSOX1202G : public InstrumentTag
    {
        public:
            static constexpr unsigned channel_count = 2;

            //
            // No Vmiddle -- see Channel's comment on the :MEASure:VMIDdle this
            // instrument does not have.
            //
            enum class Mode
            {
                Vpp, Vmax, Vmin, Vrms, Vaverage, Vbase, Vtop, Vamplitude,
                Frequency, Period, RiseTime, FallTime, PositiveWidth, NegativeWidth
            };

            //
            // How long Await polls for the capture to complete, and Arm for
            // the scope to report itself armed, when the script does not say.
            //
            // Both are stated here rather than left implicit because a default
            // timeout is a real decision about how a failing test behaves: too
            // short and a slow DUT reports a transient that was there, too long
            // and a suite hangs on a DUT that is simply dead. The capture
            // default is the more generous of the two for that reason -- it is
            // waiting on the device under test, where arming is waiting only on
            // the instrument.
            //
            static constexpr core::quantities::Time kDefaultCaptureTimeout{ 5.0 };
            static constexpr core::quantities::Time kDefaultArmTimeout{ 1.0 };

            //
            // LAN or USB, and nothing else: no GPIB connector and no serial
            // port, so a row addressing it over either fails to compile.
            //
            // LAN is here because the instrument in hand has it, and that
            // overrides the document this list first followed. The 1000
            // X-Series programmer's guide says of the two-channel models
            // "There is no LAN interface (only USB is supported)", and for a
            // while this list said Usb alone on that authority -- until the
            // DSOX1202G on the dev desk turned out to have a LAN connector on
            // its back panel. A back panel is a fact about hardware, and the
            // hardware is the better witness.
            //
            // What that costs is worth stating: a unit that really has no LAN
            // port -- an early one, if the guide was right about those -- now
            // accepts Lan( ...) at compile time and fails when the socket does
            // not connect, rather than failing to build. The failure is still
            // at startup, before the first script, because preflight opens
            // every row (see hal/verbs/preflight.hpp); it just says "cannot
            // reach" where it used to say "does not compile".
            //
            // Port 5025, the SCPI socket every Keysight instrument in this tree
            // listens on and hal::Lan's default -- nothing to write in a row.
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
            DSOX1202G( const InstrumentId id, const AddressT address) : mId( id), mConnection( id, address) {}

            //
            // Where the PC reaches this scope -- and, since this driver grew a
            // transport, the column that decides whether a reading comes off
            // hardware or out of the simulation hooks below. See isSimulated().
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
            // The one branch in this driver that decides between USBTMC and an
            // mChannels entry, and it is a property of the *address* rather
            // than a mode a caller sets: a rig says what it has once, in its
            // instrument table, and a script capturing a transient is
            // identical either way.
            //
            // An injected transport wins over a Simulated address, deliberately
            // -- see useTransport(). That is how this driver's own tests assert
            // the SCPI it sends without a bench.
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
            // Two callers, and neither is a script: a test hands in a fake and
            // asserts the command strings, and a rig reaching this scope over
            // a bus hal::io::openTransport() does not implement hands in its
            // own hal::io::ITransport. Replaces any session already open --
            // two transports to one instrument is not a state this models --
            // and sends nothing, so handing in a fake cannot throw. The
            // once-per-session exchange belongs to the first use; see
            // session().
            //
            auto useTransport( std::unique_ptr<io::ITransport> transport) -> void
            {
                mConnection.useTransport( std::move( transport));
            }

            //
            // Point this driver at a different instrument -- see
            // hal::keysight_edu34450a::EDU34450A::useAddress() for the whole
            // argument, which is identical here: startup only, validates
            // nothing because hal::ReachableOver and hal::sameKind already
            // have, and drops any open session so that nothing goes on
            // talking to the previous box.
            //
            auto useAddress( const Address & address) -> void
            {
                mConnection.useAddress( address);
            }

            //
            // The live SCPI session, opened on first use.
            //
            // Lazily, which is the only thing that can work here: the rig's
            // instruments are globals constructed before main(), so a
            // constructor that opened a session would make every binary that
            // links the rig -- every unit test, --replay, --skeleton, --help --
            // reach for the bench at static-initialisation time. It is also
            // exactly when a detached run does not need one.
            //
            // Public, because a bring-up session on a desk wants it: a
            // diagnostic can send this scope a command this driver has no
            // accessor for -- :AUToscale, :DISPlay:DATA?, the waveform
            // generator -- without that becoming a reason to widen the driver.
            //
            [[nodiscard]]
            auto session() -> io::ScpiSession &;

            //
            // *IDN? -- "KEYSIGHT TECHNOLOGIES,DSO-X 1202G,CN12345678,01.20..."
            // off the real instrument, opening the session if it is not already
            // open. What a run's traceability header should carry about an
            // instrument is what the instrument says it is, not what the rig
            // table hoped it was.
            //
            [[nodiscard]]
            auto identity() -> std::string;

            //
            // Drop the session. The next command opens a new one.
            //
            // Not called by safe(), and not called at the end of a run: see
            // safe() for why a failed run keeps its session.
            //
            auto closeSession() -> void
            {
                mConnection.close();
            }

            [[nodiscard]]
            auto id() const -> InstrumentId
            {
                return mId;
            }

            //
            // Narrows to one of the instrument's two physical channels --
            // channel<3>() or channel<0>() simply has no valid instantiation
            // (ValidChannel), a hard compile error, not a runtime range check.
            //
            template<unsigned N>
                requires ValidChannel<N>
            [[nodiscard]]
            auto channel() -> Channel<N>
            {
                return Channel<N>{ *this };
            }

            // --- The instrument-wide builders ---

            [[nodiscard]]
            auto trigger() -> TriggerBuilder
            {
                return TriggerBuilder{ *this };
            }

            [[nodiscard]]
            auto timebase() -> TimebaseBuilder
            {
                return TimebaseBuilder{ *this };
            }

            [[nodiscard]]
            auto acquisition() -> AcquisitionBuilder
            {
                return AcquisitionBuilder{ *this };
            }

            //
            // The single-shot capture, for Arm and Await -- named after the
            // instrument's own :SINGle rather than after what the verbs do
            // with it, so that a reader holding the programmer's guide
            // recognises it. Which of the two verbs is being invoked is what
            // says whether this is arming or waiting.
            //
            [[nodiscard]]
            auto single() -> SingleBuilder
            {
                return SingleBuilder{ *this };
            }

            [[nodiscard]]
            auto mode() const -> Mode
            {
                return mMode;
            }

            [[nodiscard]]
            auto channelNumber() const -> unsigned
            {
                return mChannel;
            }

            //
            // Nothing to do -- a scope is passive, exactly as
            // hal::keysight_edu34450a::EDU34450A is; see that class's safe() for why
            // this is written out as an explicit empty body rather than simply
            // left absent.
            //
            // Note this deliberately does not reset mMode/mChannel, nor any of
            // the settings a Setup left behind. They are instrument state a
            // script set, not anything that can energise the DUT -- and safing
            // runs when a script has already died, so there is nobody left to
            // surprise with a mode change. Resetting them would only discard
            // the last thing the scope was told to look at, which is the one
            // piece of state worth still being able to read afterwards.
            //
            // The armed flag is the one thing that IS cleared, and for a reason
            // the others do not share: it is not a setting but a pending
            // expectation. A scope left armed after a script died is waiting
            // for an event that is no longer coming, and the next script's
            // Await would be answered by it.
            //
            // :STOP goes here for an attached scope, and only for one whose
            // session is already open -- see the definition.
            //
            auto safe() -> void;

            // Switches the instrument's current measurement mode/channel --
            // called by Channel's builder methods, never by a script directly.
            auto setMode( const Mode mode) -> void
            {
                mMode = mode;
            }

            auto setChannel( const unsigned channel) -> void
            {
                mChannel = channel;
            }

            // --- What the ADL customization points in customization.hpp actually call ---

            //
            // Four configure methods, one per subsystem, and each one sends
            // only the fields the Setup named -- an unset field means "leave
            // what is already configured", so a Setup naming only the trigger
            // level must not reset the slope to some default the builder never
            // chose. Each also records what it sent, which is what the settings
            // readback below answers and what a describeConfig renders.
            //
            // Defined in src/keysight_dsox1202g.cpp, with the SCPI. That is
            // where every command this driver puts on the wire lives, in the
            // order the instrument sees it.
            //
            auto configureTrigger( const TriggerConfig & config) -> void;
            auto configureTimebase( const TimebaseConfig & config) -> void;
            auto configureAcquisition( const AcquisitionConfig & config) -> void;
            auto configureChannel( const ChannelConfig & config) -> void;

            //
            // Arm: on real hardware this is the guide's own single-shot
            // sequence, which is worth following exactly because the ordering
            // is what makes it work -- ":STOP", "*OPC?" to let that settle,
            // ":SINGle" to arm, then poll ":AER?" until it answers 1 or the arm
            // timeout runs out. Keysight's example puts the instruction to
            // enable the DUT immediately after that loop, in a comment:
            // "Oscilloscope is armed and ready, enable DUT here."
            //
            // That is precisely the contract core::ArmEngine promises a script
            // -- Arm returns armed, not told-to-arm -- and it is why a script
            // may drop a rail on the very next line.
            //
            // Note :DIGitize is NOT how this is done, and the guide is explicit
            // about why: it blocks the instrument against further commands
            // until the acquisition completes, so a single-shot DUT that has
            // not been triggered yet can never be enabled.
            //
            // Simulated, what survives is the state that ordering produces:
            // armed, with no completed acquisition behind it.
            //
            auto armSingle( const SingleConfig & config) -> void;

            //
            // Await: poll ":OPERegister:CONDition?" and watch the RUN bit
            // (bit 3, 0x08) until it clears, or the timeout runs out. A cleared
            // RUN bit means the scope has stopped, which after a :SINGle means
            // the acquisition completed.
            //
            // Not :TER? (the trigger event register), which answers whether a
            // trigger happened and is cleared by being read -- a triggered
            // scope may still be filling its record. The RUN bit is the one
            // that says the capture is over and the record can be measured.
            //
            // Awaiting something that was never armed answers false rather
            // than throwing, and that is a deliberate reading of what the
            // mistake is. A script that measures a transient without having
            // armed a capture has not crashed -- it has measured whatever was
            // left in the acquisition buffer, which is a wrong answer, and the
            // check that this Await gates is exactly where a wrong answer
            // should be caught. Throwing would abandon the rest of the run over
            // a script bug the run itself is capable of reporting.
            //
            [[nodiscard]]
            auto awaitAcquisition( const SingleConfig & config) -> bool;

            //
            // Fetch: on real hardware, :WAVeform:SOURce CHANnel<N>, then
            // :WAVeform:PREamble? for the scaling and :WAVeform:DATA? for the
            // block, with the raw levels turned into volts against seconds
            // here -- (raw - yReference) * yIncrement + yOrigin, and the x pair
            // carried straight into core::Waveform::Timing.
            //
            // The scaling belongs here and not one layer up, which is what
            // core::Waveform stores values in units for: a raw level is a fact
            // about this digitiser at this vertical setting, and this driver is
            // the only thing that knows the encoding. A recording holding raw
            // levels would be unreadable without the instrument that wrote it.
            //
            // Simulated here, so what comes back is whatever setSimulatedTrace()
            // put there. Fetching a channel nothing was put on answers an empty
            // trace rather than throwing, for the reason awaitAcquisition
            // answers false rather than throwing.
            //
            [[nodiscard]]
            auto fetchWaveform( const WaveformConfig & config) -> core::Waveform;

            // --- Test/simulation hooks -- real hardware has no such setters ---
            //
            // Channel is a plain runtime unsigned here (1-2): this is test
            // scaffolding setting up canned data, not the compile-time-checked
            // script-facing surface channel<N>() provides.

            auto setSimulatedVpp( const unsigned channel, const core::quantities::Voltage v) -> void
            {
                atChannel( channel).Vpp = v;
            }

            auto setSimulatedTrace( const unsigned channel, core::Waveform trace) -> void
            {
                atChannel( channel).Trace = std::move( trace);
            }

            auto setSimulatedVmax( const unsigned channel, const core::quantities::Voltage v) -> void
            {
                atChannel( channel).Vmax = v;
            }

            auto setSimulatedVmin( const unsigned channel, const core::quantities::Voltage v) -> void
            {
                atChannel( channel).Vmin = v;
            }

            auto setSimulatedVrms( const unsigned channel, const core::quantities::Voltage v) -> void
            {
                atChannel( channel).Vrms = v;
            }

            auto setSimulatedVaverage( const unsigned channel, const core::quantities::Voltage v) -> void
            {
                atChannel( channel).Vaverage = v;
            }

            auto setSimulatedVbase( const unsigned channel, const core::quantities::Voltage v) -> void
            {
                atChannel( channel).Vbase = v;
            }

            auto setSimulatedVtop( const unsigned channel, const core::quantities::Voltage v) -> void
            {
                atChannel( channel).Vtop = v;
            }

            auto setSimulatedVamplitude( const unsigned channel, const core::quantities::Voltage v) -> void
            {
                atChannel( channel).Vamplitude = v;
            }

            auto setSimulatedFrequency( const unsigned channel, const core::quantities::Frequency f) -> void
            {
                atChannel( channel).Freq = f;
            }

            auto setSimulatedPeriod( const unsigned channel, const core::quantities::Time t) -> void
            {
                atChannel( channel).Period = t;
            }

            auto setSimulatedRiseTime( const unsigned channel, const core::quantities::Time t) -> void
            {
                atChannel( channel).RiseTime = t;
            }

            auto setSimulatedFallTime( const unsigned channel, const core::quantities::Time t) -> void
            {
                atChannel( channel).FallTime = t;
            }

            auto setSimulatedPositiveWidth( const unsigned channel, const core::quantities::Time t) -> void
            {
                atChannel( channel).PositiveWidth = t;
            }

            auto setSimulatedNegativeWidth( const unsigned channel, const core::quantities::Time t) -> void
            {
                atChannel( channel).NegativeWidth = t;
            }

            //
            // Makes one measurement on one channel report itself unmeasurable
            // -- what this scope does when the trace does not support the
            // question being asked of it.
            //
            // No fault argument, unlike the Infiniium driver's hook of the same
            // name, and the missing parameter is the point: this instrument
            // answers +9.9E+37 and nothing else, so a hook that let a test
            // choose *which* reason came back would let tests be written
            // against an instrument that does not exist. See kUnmeasurable.
            //
            // Still per (channel, measurement) rather than per channel, because
            // that is how the instrument behaves: a clipped trace still has a
            // perfectly good period, and a flat trace with no edge on it has a
            // vmax and no rise time. A single "this channel is broken" flag
            // could not express either case.
            //
            auto setSimulatedUnmeasurable( const unsigned channel, const Mode mode) -> void
            {
                atChannel( channel).Unmeasurable.at( static_cast<std::size_t>( mode)) = true;
            }

            auto clearSimulatedUnmeasurable( const unsigned channel, const Mode mode) -> void
            {
                atChannel( channel).Unmeasurable.at( static_cast<std::size_t>( mode)) = false;
            }

            //
            // Whether the next armed capture completes. Defaults to true -- the
            // happy path, so a test that does not care about capture failure
            // says nothing about it -- and set false to exercise the timeout
            // branch.
            //
            auto setSimulatedCaptureCompletes( const bool completes) -> void
            {
                mCaptureCompletes = completes;
            }

            [[nodiscard]]
            auto isArmed() const -> bool
            {
                return mArmed;
            }

            [[nodiscard]]
            auto lastAcquisitionCompleted() const -> bool
            {
                return mCompleted;
            }

            // --- Settings readback, for tests and for describeConfig ---

            [[nodiscard]] auto triggerSource() const     -> std::optional<unsigned>          { return mTriggerSource;   }
            [[nodiscard]] auto triggerSlope() const      -> std::optional<TriggerSlope>      { return mTriggerSlope;    }
            [[nodiscard]] auto triggerLevel() const      -> std::optional<core::quantities::Voltage> { return mTriggerLevel; }
            [[nodiscard]] auto triggerSweep() const      -> std::optional<TriggerSweep>      { return mTriggerSweep;    }
            [[nodiscard]] auto triggerCoupling() const   -> std::optional<TriggerCoupling>   { return mTriggerCoupling; }
            [[nodiscard]] auto triggerReject() const     -> std::optional<TriggerReject>     { return mTriggerReject;   }
            [[nodiscard]] auto triggerHoldoff() const    -> std::optional<core::quantities::Time> { return mTriggerHoldoff; }

            [[nodiscard]] auto timePerDivision() const   -> std::optional<core::quantities::Time> { return mTimePerDivision; }
            [[nodiscard]] auto timebasePosition() const  -> std::optional<core::quantities::Time> { return mTimebasePosition; }
            [[nodiscard]] auto timebaseReference() const -> std::optional<TimebaseReference> { return mTimebaseReference; }

            [[nodiscard]] auto acquisitionType() const   -> std::optional<AcquisitionType>   { return mAcquisitionType; }
            [[nodiscard]] auto averageCount() const      -> std::optional<unsigned>          { return mAverageCount;    }

            [[nodiscard]] auto channelCoupling( const unsigned channel) -> std::optional<Coupling>
            {
                return atChannel( channel).InputCoupling;
            }

            [[nodiscard]] auto voltsPerDivision( const unsigned channel) -> std::optional<core::quantities::Voltage>
            {
                return atChannel( channel).VoltsPerDivision;
            }

            [[nodiscard]] auto verticalOffset( const unsigned channel) -> std::optional<core::quantities::Voltage>
            {
                return atChannel( channel).VerticalOffset;
            }

            [[nodiscard]] auto channelBandwidth( const unsigned channel) -> std::optional<Bandwidth>
            {
                return atChannel( channel).BandwidthLimit;
            }

            [[nodiscard]] auto probeAttenuation( const unsigned channel) -> std::optional<double>
            {
                return atChannel( channel).ProbeAttenuation;
            }

            [[nodiscard]] auto channelDisplay( const unsigned channel) -> std::optional<ChannelDisplay>
            {
                return atChannel( channel).Display;
            }

            //
            // The one rawMeasure() Port<Q, DSOX1202G> actually calls --
            // mChannel/mMode are already instrument state by the time this runs
            // (set by whichever Channel<N> builder method produced the Port), so
            // this needs no channel argument of its own.
            //
            // Throws core::UnmeasurableReading when the instrument would have
            // answered +9.9E+37 -- see kUnmeasurable for why the message is
            // always the same one, and core::MeasureEngine for what catches it.
            // Throwing rather than returning a sentinel is what keeps every
            // driver's rawMeasure() returning the quantity it says it returns:
            // a sentinel would be a magic number every caller had to know to
            // test for, which is exactly the ISINVALID() arrangement this
            // replaces.
            //
            // The live path is one line, and everything about it that is
            // instrument-specific -- which :MEASure command, which thresholds,
            // what +9.9E+37 means -- is in readMeasurement() in the .cpp. What
            // stays here is the part that genuinely depends on QuantityT: which
            // of this class's simulated fields to answer from, and what type to
            // wrap the number in.
            //
            template<core::quantities::QuantityType QuantityT>
            [[nodiscard]]
            auto rawMeasure( const core::MeasureSetup<QuantityT> & setup) -> QuantityT
            {
                if( !isSimulated())
                {
                    return QuantityT{ readMeasurement( mMode, mChannel, setup.LowThreshold, setup.HighThreshold) };
                }

                const auto & data = atChannel( mChannel);

                if( data.Unmeasurable.at( static_cast<std::size_t>( mMode)))
                {
                    throw core::UnmeasurableReading( std::string( kUnmeasurable));
                }

                if constexpr( std::is_same_v<QuantityT, core::quantities::Voltage>)
                {
                    switch( mMode)
                    {
                        case Mode::Vpp:        return data.Vpp;
                        case Mode::Vmax:       return data.Vmax;
                        case Mode::Vmin:       return data.Vmin;
                        case Mode::Vrms:       return data.Vrms;
                        case Mode::Vaverage:   return data.Vaverage;
                        case Mode::Vbase:      return data.Vbase;
                        case Mode::Vtop:       return data.Vtop;
                        case Mode::Vamplitude: return data.Vamplitude;
                        default:               return data.Vpp;
                    }
                }
                else if constexpr( std::is_same_v<QuantityT, core::quantities::Frequency>)
                {
                    return data.Freq;
                }
                else if constexpr( std::is_same_v<QuantityT, core::quantities::Time>)
                {
                    switch( mMode)
                    {
                        case Mode::Period:        return data.Period;
                        case Mode::RiseTime:      return data.RiseTime;
                        case Mode::FallTime:      return data.FallTime;
                        case Mode::PositiveWidth: return data.PositiveWidth;
                        case Mode::NegativeWidth: return data.NegativeWidth;
                        default:                  return data.Period;
                    }
                }
                else
                {
                    static_assert( !sizeof( QuantityT), "DSOX1202G has no port for this quantity");
                }
            }

        private:
            //
            // The one reading path that talks, and a plain function over Mode
            // rather than a template: which :MEASure command to send, and what
            // to make of the answer, depend on the measurement and not on the
            // C++ type the number is about to be wrapped in. Same split
            // hal::keysight_edu34450a::EDU34450A::read() draws, and for the
            // same reason -- it puts everything that goes on the wire in one
            // file a bench engineer can check against the manual.
            //
            [[nodiscard]]
            auto readMeasurement( Mode mode, unsigned channel,
                                  std::optional<double> lowThreshold,
                                  std::optional<double> highThreshold) -> double;

            //
            // *IDN?, and a refusal if the answer is not one of this model's
            // spellings -- see the definition for which are accepted and why a
            // hostname-shaped mistake is the failure this catches.
            //
            auto verifyIdentity( io::ScpiSession & opened) -> std::string;


            //
            // How many measurements a channel can be made to report as
            // unmeasurable -- derived from Mode's own enumerators by reflection
            // rather than written down beside them (see core/meta.hpp). Adding
            // a measurement to Mode therefore cannot leave this array a size
            // too short, because there is nothing to forget to update.
            //
            static constexpr std::size_t kModeCount = core::meta::values<Mode>.size();

            struct ChannelData
            {
                core::quantities::Voltage    Vpp{};
                core::quantities::Voltage    Vmax{};
                core::quantities::Voltage    Vmin{};
                core::quantities::Voltage    Vrms{};
                core::quantities::Voltage    Vaverage{};
                core::quantities::Voltage    Vbase{};
                core::quantities::Voltage    Vtop{};
                core::quantities::Voltage    Vamplitude{};
                core::quantities::Frequency  Freq{};
                core::quantities::Time       Period{};
                core::quantities::Time       RiseTime{};
                core::quantities::Time       FallTime{};
                core::quantities::Time       PositiveWidth{};
                core::quantities::Time       NegativeWidth{};

                // What a Setup left on this input, and which of its
                // measurements the instrument would refuse to answer.
                std::optional<Coupling>                   InputCoupling;
                std::optional<core::quantities::Voltage>  VoltsPerDivision;
                std::optional<core::quantities::Voltage>  VerticalOffset;
                std::optional<Bandwidth>                  BandwidthLimit;
                std::optional<double>                     ProbeAttenuation;
                std::optional<ChannelDisplay>             Display;

                //
                // A flag per measurement rather than an optional fault code:
                // there is one reason, so the only question is whether this
                // measurement answers. See kUnmeasurable.
                //
                std::array<bool, kModeCount> Unmeasurable{};

                // What :WAVeform:DATA? would hand back for this input.
                core::Waveform Trace{};
            };

            [[nodiscard]]
            auto atChannel( const unsigned channel) -> ChannelData &
            {
                return mChannels.at( channel - 1);
            }

            InstrumentId               mId;

            //
            // Where this scope is and the session to it -- shared with any other
            // face of the same box, and prepared once (see hal::BoxConnection
            // and session()). Not marked prepared until the preparation has
            // succeeded, so a scope that failed its identity check is asked
            // again on the next command rather than treated as verified.
            //
            BoxConnection              mConnection;
            Mode                       mMode{ Mode::Vpp};
            unsigned                   mChannel{ 1};
            std::array<ChannelData, channel_count> mChannels;

            std::optional<unsigned>                     mTriggerSource;
            std::optional<TriggerSlope>                 mTriggerSlope;
            std::optional<core::quantities::Voltage>    mTriggerLevel;
            std::optional<TriggerSweep>                 mTriggerSweep;
            std::optional<TriggerCoupling>              mTriggerCoupling;
            std::optional<TriggerReject>                mTriggerReject;
            std::optional<core::quantities::Time>       mTriggerHoldoff;

            std::optional<core::quantities::Time>       mTimePerDivision;
            std::optional<core::quantities::Time>       mTimebasePosition;
            std::optional<TimebaseReference>            mTimebaseReference;

            std::optional<AcquisitionType>              mAcquisitionType;
            std::optional<unsigned>                     mAverageCount;

            bool                                        mArmed{ false };
            bool                                        mCompleted{ false };
            bool                                        mCaptureCompletes{ true };
    };
} // namespace hal::keysight_dsox1202g
