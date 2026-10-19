#pragma once

#include <array>
#include <concepts>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>

#include "core/driver/describe.hpp"
#include "core/quantities/format.hpp"
#include "core/quantities/quantity.hpp"

#include "hal/driver/address.hpp"
#include "hal/driver/box_connection.hpp"
#include "hal/driver/builder.hpp"
#include "hal/driver/describe.hpp"
#include "hal/driver/instrument.hpp"
#include "hal/io/scpi.hpp"

#include "hal/keysight_33522b/vocabulary.hpp"
#include "hal/keysight_33522b/builder.hpp"
#include "hal/keysight_33522b/wire.hpp"

//
// the instrument class
//
// Part of hal/keysight_33522b.hpp -- include that, not this.
//

namespace hal::keysight_33522b
{
    //
    // A Keysight (Agilent) 33522B Trueform waveform generator: two independent
    // 30 MHz outputs, 1 uHz resolution, 16-bit vertical resolution, and 1 mVpp
    // to 10 Vpp into 50 Ohm.
    //
    // One instance models the whole box, both channels, and that is the one
    // structural decision here that goes the other way from
    // hal::keysight_edu36311a -- where three outputs of one chassis are three
    // instruments with three ids. Worth stating why, because the two boxes
    // look alike from a distance:
    //
    //   - an EDU36311A's three outputs are *different* (6 V / 5 A against two
    //     of 30 V / 1 A), each feeds a different DUT rail through its own
    //     lead, and each has its own isolation relay to Connect and
    //     Disconnect. Three endpoints that are separately wired and separately
    //     rated are three instruments.
    //
    //   - a 33522B's two channels are identical, hard-cabled here, and are
    //     routinely used *together* -- a stimulus and its inverse, a clock and
    //     the signal it gates, two phases of one excitation. One id keeps
    //     "safe the generator" a single operation over both connectors, which
    //     is what a failed run needs, and keeps one address in the rig table
    //     rather than the same address written twice.
    //
    // So the channel is chosen at the call site, at compile time, the way the
    // scope's is -- see Channel above.
    //
    // -- What the generator is told, in full ---------------------------------
    //
    // For one Apply of a 3.3 Vpp 10 kHz square with a 25% duty cycle on
    // channel 1, into a high-impedance load:
    //
    //     *IDN?                      once per session -- refused if the model
    //                                is not this one (detail::prepare)
    //     SYST:ERR?                  once per session, until empty: whatever
    //                                the last user left queued is not ours
    //     OUTP1:LOAD INF             the termination first -- see
    //                                detail::program, this is what the volts
    //                                below are quoted against
    //     SYST:ERR?                  did it accept that
    //     SOUR1:FUNC SQU             then the shape, because the frequency and
    //                                amplitude limits are shape-dependent
    //     SYST:ERR?
    //     SOUR1:FREQ 10000           then the frequency
    //     SYST:ERR?
    //     SOUR1:FUNC:SQU:DCYC 25     then the duty cycle, whose own limits
    //                                depend on the frequency just set
    //     SYST:ERR?
    //     SOUR1:VOLT 3.3             then the amplitude
    //     SYST:ERR?
    //     SOUR1:VOLT:OFFS 0          then the offset, which setting the
    //                                amplitude can move
    //     SYST:ERR?
    //     OUTP1 ON                   and only then the output on
    //     SYST:ERR?
    //     *OPC?                      and wait until it has all been processed
    //
    // Which it does -- wire or remembered state -- is decided by the address
    // column of the rig's instrument table and by nothing else. hal::Simulated
    // means the state below is the instrument; a real address means the box at
    // it is. A script cannot tell the difference and no script should have to
    // (see isSimulated()).
    //
    // -- No ports, and that is not an omission ------------------------------
    //
    // This instrument measures nothing. It has no Port-returning members, no
    // rawMeasure, and no simulated-reading hooks, because there is no reading
    // to simulate: a 33522B can be asked what it was told (FREQ?, VOLT?) and
    // that is a readback of this driver's own instruction, not an observation
    // of the world. Putting one in a run journal would record a number no
    // instrument ever measured. What the generator is actually producing is
    // measured by the scope, through the fabric, like any other signal.
    //
    // -- Hard-cabled, and no Connect/Disconnect -----------------------------
    //
    // There is no isolation relay in this rig at all (Thomas, 2026-09-04), so
    // this instrument's outputs are cabled straight through and there is
    // nothing for Connect/Disconnect to move: this header declares no
    // connectDriver/disconnectDriver, and Connect( Wfg1.channel<1>().sine())
    // is "no matching function".
    //
    // Deliberately *not* done with hal::keysight_edu36311a's
    // DirectWiring/RelayIsolated tag pair, even though this is exactly the
    // DirectWiring case. That driver's own comment says a third driver wanting
    // the distinction is the trigger to hoist the tags into
    // hal/driver/instrument.hpp with an API bump -- and hoisting them to model
    // an axis this rig has no hardware for would be adding a template
    // parameter with one legal value. If a relay is ever fitted in this
    // generator's lead, that is the moment for the hoist, and this class grows
    // an Isolation parameter then.
    //
    // -- What is deliberately not modelled ----------------------------------
    //
    // arbitrary waveforms       the B in 33522B is the arb-capable model, and
    //                           DATA:ARBitrary plus the MMEMory subsystem is
    //                           how a point list gets into it. That is a
    //                           waveform *transfer*, not a setting -- it wants
    //                           the counted binary-block read hal::io::
    //                           ITransport still does not have (see
    //                           framework/hal/README.md), and it wants a
    //                           decision about where a rig's arb files live.
    //                           A real addition, and a large one.
    //
    // modulation and sweep      AM/FM/PM/PWM/FSK, SWEep, LIST. Each is a whole
    //                           subsystem with its own carrier-plus-modulator
    //                           vocabulary, and none of it is one more setter
    //                           on this builder.
    //
    // burst                     BURSt:NCYCles and friends -- N cycles on a
    //                           trigger. The nearest thing here to a verb this
    //                           framework already has (it is an Arm/Await
    //                           shape, like the scope's single-shot), and the
    //                           first thing to add if a script needs a
    //                           stimulus that stops.
    //
    // pulse edge and width      FUNC:PULS:WIDTh, :PERiod and the two
    //                           TRANsition times. A pulse here is programmed
    //                           by frequency and duty cycle, which is one of
    //                           the two ways this instrument allows (see
    //                           FUNC:PULS:HOLD); the other is width-based and
    //                           would be a second, mutually exclusive set of
    //                           setters on the pulse shape.
    //
    // VOLT:UNIT, HIGH/LOW       amplitude is Vpp here, always -- see
    //                           detail::program. Vrms and dBm are the same
    //                           signal quoted differently, and quoting an
    //                           amplitude two ways in one codebase is how a
    //                           factor of 2.83 gets into a test report.
    //
    // OUTP:POLarity, :SYNC      inversion and the sync connector. Neither is
    //                           part of the signal a DUT sees on this rig.
    //
    // channel coupling          FREQ:COUPle and VOLT:COUPle tie the two
    //                           channels' settings together. They would make
    //                           an Apply to one channel silently change the
    //                           other, which is precisely the property this
    //                           driver's one-instrument-two-channels shape
    //                           relies on not being true.
    //
    class WaveformGenerator : public InstrumentTag
    {
        public:
            //
            // LAN, USB or GPIB. The first two are standard on every 33500B
            // Series unit; GPIB is the factory- or user-installed option
            // (guide, "USB, GPIB, and LAN remote interfaces (GPIB is optional
            // on some models)"), so it is in the list because the back panel
            // can genuinely have it -- unlike the DSOX1202G's absent GPIB
            // connector, which no option adds and which its driver therefore
            // rejects at compile time. A rig whose unit has no GPIB module
            // finds that out when openTransport fails to reach it, which is
            // the right place for a fact about one particular box.
            //
            // This model's back panel, written once and read twice: the
            // constructor below is constrained by it, and
            // hal::bindAddresses() checks an address supplied at startup
            // against the same list (see hal::BackPanel). A second
            // spelling of these connectors could disagree with this one,
            // and only the bench would ever find out.
            //
            using Buses = BackPanel<Lan, Usb, Gpib>;

            template<typename AddressT>
                requires Buses::allows<AddressT>
            WaveformGenerator( const InstrumentId id, const AddressT address) : mId( id), mConnection( id, address) {}

            //
            // Where the PC reaches this generator -- and, since this driver has
            // a transport, the column that decides whether an Apply reaches
            // hardware or the remembered state below. See isSimulated().
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

            //
            // One of the two outputs, chosen at compile time. channel<0>() and
            // channel<3>() have no valid instantiation at all (ValidChannel),
            // which is a hard compile error rather than a runtime range check.
            //
            template<unsigned N>
                requires ValidChannel<N>
            [[nodiscard]]
            auto channel() -> Channel<N>
            {
                return Channel<N>{ *this };
            }

            //
            // Whether there is nothing at the other end -- a hal::Simulated
            // address and no transport handed in. A property of the *address*
            // rather than a mode a caller sets: a rig says what it has once,
            // in its instrument table. An injected transport wins over a
            // Simulated address, deliberately -- see useTransport().
            //
            [[nodiscard]]
            auto isSimulated() const -> bool
            {
                return mConnection.isSimulated();
            }

            //
            // Hand this driver a transport to talk through, instead of one
            // opened from its address. Two callers, and neither is a script: a
            // test hands in a fake and asserts the command strings, and a rig
            // reaching this box over a bus hal::io::openTransport() does not
            // implement hands in its own hal::io::ITransport.
            //
            // Replaces any session already open, closing it, and sends
            // nothing: the identity check and the error-queue drain belong to
            // the first *use* (see session()), so handing in a fake transport
            // is not a thing that can throw.
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
            // throw from a constructor with nowhere to catch it.
            //
            // Public, because a bring-up session on a desk wants it: a
            // deployment test can send this generator a command this driver
            // has no accessor for -- a burst, a sweep, DISP:TEXT -- without
            // that becoming a reason to widen the driver. Throws
            // hal::io::TransportError if the box cannot be reached, and
            // hal::io::ScpiFault if it answers *IDN? with a model this driver
            // is not for.
            //
            [[nodiscard]]
            auto session() -> io::ScpiSession &
            {
                //
                // The box's session (see hal::BoxConnection), opened on first
                // use and prepared once for this driver, here rather than
                // where it is created because a session arrives two ways --
                // opened from the address, or handed in by useTransport() --
                // and preparing it where it is *used* is what makes both go
                // through it.
                //
                // Not marked prepared until it has succeeded, so a generator
                // that failed its identity check is asked again on the next
                // command rather than being treated as verified.
                //
                return mConnection.session( "keysight_33522b", []( io::ScpiSession & opened)
                {
                    detail::prepare( opened);
                });
            }

            //
            // *IDN? -- "Keysight Technologies,33522B,MY12345678,..." off the
            // real instrument, opening the session if it is not already open.
            // What a run's traceability header should carry about an
            // instrument is what the instrument says it is, not what the rig
            // table hoped it was.
            //
            [[nodiscard]]
            auto identity() -> std::string
            {
                return detail::identify( session());
            }

            //
            // Drop the session, closing the socket. The next Apply opens a new
            // one. For a long-lived process that should not hold a bench
            // instrument between runs, and for recovering from a wedged one.
            // Not called by safe(): see there.
            //
            auto closeSession() -> void
            {
                mConnection.close();
            }

            //
            // What an Apply does. Public because applyDriver below is a free
            // function found by ADL, not because a script should call it --
            // scripts go through Apply (see core/verbs/source.hpp).
            //
            // Throws SettingOutOfRange before anything is sent if a setting is
            // outside what this model can produce for this shape.
            //
            // Returns when the instrument has *processed* the commands (the
            // *OPC? that ends detail::program), which for this box is very
            // nearly the same as the output having arrived: unlike a power
            // supply, whose rail takes tens or hundreds of milliseconds to
            // settle, a function generator's new waveform is present within a
            // cycle or two. The one thing that is not instantaneous is the
            // output relay OUTPut switches, and the guide notes the output
            // "may glitch for about a millisecond until signal stabilizes"
            // when it does.
            //
            template<typename Shape>
            auto applyWaveform( const WaveformConfig<Shape> & config) -> void
            {
                auto & output = outputFor( config.Channel);

                //
                // The termination the amplitude and offset below are quoted
                // against: whichever this config named, or -- if it named none
                // -- whichever the output is already in. The limits depend on
                // it, so the check has to resolve it exactly the way the
                // instrument will.
                //
                const auto termination = config.Load.value_or( output.Load);

                checkAgainstModel<Shape>( config, termination);

                //
                // Remembered before the wire, and remembered whether or not
                // there is a wire: these are what a journal line and a failure
                // message are written from, and an Apply that threw on its way
                // to the instrument still happened as far as a report is
                // concerned.
                //
                output.Function  = Shape::Name;
                output.Load      = termination;
                output.Enabled   = true;

                if( config.Frequency) { output.Frequency = config.Frequency; }
                if( config.Amplitude) { output.Amplitude = config.Amplitude; }
                if( config.Offset)    { output.Offset    = config.Offset;    }

                if( isSimulated())
                {
                    return;
                }

                detail::program( session(), flatten<Shape>( config, termination));
            }

            //
            // What a Remove does: the named output off, and nothing else.
            // Every setting is deliberately left where the script put it --
            // unlike safe(), a Remove is a step in a script that knows what it
            // is doing and may well re-enable the same output two lines later.
            //
            auto removeOutput( const unsigned channel) -> void
            {
                outputFor( channel).Enabled = false;

                if( isSimulated())
                {
                    return;
                }

                detail::disableOutput( session(), static_cast<int>( channel));
            }

            //
            // Both outputs off. This is the spelling the electrical interlock
            // detects an instrument-with-an-output by -- hal::energisedSourceAt
            // requires a no-argument removeOutput() and, having found one, a
            // no-argument const isEnabled() (see
            // framework/hal/src/verbs/interlock.cpp, where the pairing is a
            // static_assert with a message). A driver that offered only the
            // per-channel overloads would be skipped by that walk silently,
            // and a live generator would be reported as cold.
            //
            // hal::keysight_ac6834b::AcSource answers the same question the
            // same way across its three phases, for the same reason.
            //
            auto removeOutput() -> void
            {
                for( unsigned channel = 1; channel <= mOutputs.size(); ++channel)
                {
                    removeOutput( channel);
                }
            }

            //
            // -- Two answers to "is this output live", and why there are two --
            //
            // isEnabled() is what hal::energisedSourceAt() reads before a
            // low-impedance reading is routed to a pin this generator is
            // cabled onto. Its concept takes a *const* instrument, so this
            // reports what this driver last commanded and performs no I/O.
            // With no argument it means "either output", which is the right
            // reading of the question on a box whose two connectors share one
            // InstrumentId.
            //
            // outputIsOn() asks the instrument, and is what isEnergised()
            // below uses. One round trip, in exchange for being right about
            // the cases this process did not cause: an output left on by a
            // previous run, or switched on from the front panel. The whole
            // argument is written out in hal::keysight_edu36311a::DcSource's
            // shared comment on its own pair and is not repeated here.
            //
            [[nodiscard]]
            auto isEnabled() const -> bool
            {
                for( const auto & output : mOutputs)
                {
                    if( output.Enabled)
                    {
                        return true;
                    }
                }

                return false;
            }

            [[nodiscard]]
            auto isEnabled( const unsigned channel) const -> bool
            {
                return outputFor( channel).Enabled;
            }

            [[nodiscard]]
            auto outputIsOn( const unsigned channel) -> bool
            {
                if( isSimulated())
                {
                    return isEnabled( channel);
                }

                return detail::outputIsOn( session(), static_cast<int>( channel));
            }

            //
            // Drop this generator to a known idle state, unconditionally --
            // see hal::safeRig() in hal/verbs/safing.hpp for who calls this
            // and why it takes no arguments and reads no state. Not Remove()
            // under another name: Remove is a test-script step, addressed
            // through a config and a builder chain, and reaching it requires
            // knowing what a script was in the middle of driving. safe() is
            // the opposite -- it is called when nobody knows what was running.
            //
            // Both channels, and the amplitude and offset zeroed as well as
            // the outputs disabled, for the reason
            // hal::keysight_edu36311a::DcSource::safe() zeroes its setpoint:
            // OUTPut OFF leaves the programmed signal where a test left it, so
            // a generator safed at 10 Vpp comes back at 10 Vpp the instant
            // anything enables the output again -- a front-panel press, a
            // half-initialised driver, a reconnecting console.
            //
            // On the wire, only down a session that is *already* open, and
            // never opening one. Safing runs when a script has already failed,
            // quite possibly because this box is unreachable; opening a
            // session at that moment would replace the run's real failure with
            // a transport error from the cleanup path, and hal::safeRig() does
            // not catch, so it would also abandon the safing of every
            // instrument after this one.
            //
            // The session is deliberately not closed either: its error queue
            // is the best evidence of what went wrong.
            //
            auto safe() -> void
            {
                for( auto & output : mOutputs)
                {
                    output.Enabled   = false;
                    output.Amplitude = std::nullopt;
                    output.Offset    = std::nullopt;
                }

                auto * const open = mConnection.openSession();

                if( !open)
                {
                    return;
                }

                for( unsigned channel = 1; channel <= mOutputs.size(); ++channel)
                {
                    detail::sendSafe( *open, static_cast<int>( channel));
                }
            }

            // --- The last thing this driver was told, per output ---

            //
            // Kept on an attached generator as well as a simulated one -- it is
            // what describeConfig's journal line and a failure message are
            // written from, and re-querying the instrument to render a log
            // entry would be a round trip to learn what this process just said.
            //
            // Which makes these the *setting*, never a measurement: see this
            // class's own comment on why there are no ports here.
            //
            [[nodiscard]]
            auto function( const unsigned channel) const -> std::string_view
            {
                return outputFor( channel).Function;
            }

            [[nodiscard]]
            auto frequency( const unsigned channel) const -> std::optional<core::quantities::Frequency>
            {
                return outputFor( channel).Frequency;
            }

            [[nodiscard]]
            auto amplitude( const unsigned channel) const -> std::optional<core::quantities::Voltage>
            {
                return outputFor( channel).Amplitude;
            }

            [[nodiscard]]
            auto offset( const unsigned channel) const -> std::optional<core::quantities::Voltage>
            {
                return outputFor( channel).Offset;
            }

            //
            // The termination this output's numbers are quoted against. Never
            // nullopt: the instrument is always in one, and 50 Ohm is the one
            // it powers up in, so that is what this driver believes until an
            // Apply says otherwise.
            //
            [[nodiscard]]
            auto termination( const unsigned channel) const -> Termination
            {
                return outputFor( channel).Load;
            }

            // --- What this model can produce, as constants a caller can read ---

            //
            // Peak-to-peak, and it depends on the termination: 10 Vpp into
            // 50 Ohm, 20 Vpp into an open circuit. Not two different signals --
            // the same signal, quoted against two different assumed loads. See
            // Termination.
            //
            [[nodiscard]]
            static constexpr auto maxAmplitude( const Termination termination) -> core::quantities::Voltage
            {
                return core::quantities::Voltage{ termination == Termination::HighImpedance ? 20.0 : 10.0 };
            }

            //
            // The peak voltage either polarity may reach, which is what bounds
            // the offset and couples it to the amplitude:
            // |offset| <= Vmax - Vpp/2.
            //
            [[nodiscard]]
            static constexpr auto maxPeak( const Termination termination) -> core::quantities::Voltage
            {
                return core::quantities::Voltage{ termination == Termination::HighImpedance ? 10.0 : 5.0 };
            }

            [[nodiscard]]
            static constexpr auto minAmplitude() -> core::quantities::Voltage
            {
                return core::quantities::Voltage{ 0.001 };
            }

            [[nodiscard]]
            static constexpr auto minFrequency() -> core::quantities::Frequency
            {
                return core::quantities::Frequency{ 1.0e-6 };
            }

        private:
            //
            // One output's remembered state. Not a simulation of the
            // instrument -- it is this driver's record of what it last said,
            // which is the same thing on a simulated box and a strictly
            // smaller thing on an attached one.
            //
            struct Output
            {
                std::string_view                            Function{};
                std::optional<core::quantities::Frequency>  Frequency{};
                std::optional<core::quantities::Voltage>    Amplitude{};
                std::optional<core::quantities::Voltage>    Offset{};

                //
                // The instrument's own power-on termination, so this driver
                // starts out believing what the box believes.
                //
                Termination                                 Load{ Termination::Ohms50 };
                bool                                        Enabled{ false };
            };

            //
            // Channel numbers are 1-based at every call site and in every SCPI
            // command, and 0-based in this array exactly once, here.
            //
            // .at() rather than operator[], which is the one place in this
            // driver a runtime bound is paid for. Every channel that arrives
            // through a config came from a ValidChannel-constrained Channel<N>
            // and cannot be wrong -- but the per-channel accessors above
            // (isEnabled, function, removeOutput and the rest) take a plain
            // unsigned, because they are called from tests and from journal
            // code that has a number rather than a template argument. Indexing
            // a two-element array with an unchecked one of those is undefined
            // behaviour on a driver whose whole job is refusing things that
            // cannot be. Note channel 0 underflows into a very large unsigned
            // and is caught by the same bound.
            //
            [[nodiscard]]
            auto outputFor( const unsigned channel) -> Output &
            {
                return mOutputs.at( channel - 1);
            }

            [[nodiscard]]
            auto outputFor( const unsigned channel) const -> const Output &
            {
                return mOutputs.at( channel - 1);
            }

            //
            // Refuse a setting this model cannot produce -- see
            // SettingOutOfRange, and note this runs on a simulated instrument
            // as well as an attached one, which is the entire point of it
            // existing at all.
            //
            template<typename Shape>
            auto checkAgainstModel( const WaveformConfig<Shape> & config, const Termination termination) const -> void
            {
                const std::string instrument{ to_string( mId) };

                if constexpr( Periodic<Shape>)
                {
                    if( config.Frequency)
                    {
                        const auto hertz = config.Frequency->value();

                        if( hertz < minFrequency().value() || hertz > Shape::MaxHertz)
                        {
                            throw SettingOutOfRange( instrument, "frequency",
                                "of " + core::describeValue( *config.Frequency) + " is outside a "
                                + std::string( Shape::Name) + "'s range on a 33522B ("
                                + core::describeValue( minFrequency()) + " to "
                                + core::describeValue( core::quantities::Frequency{ Shape::MaxHertz }) + ")");
                        }
                    }
                }

                if constexpr( Amplifiable<Shape>)
                {
                    if( config.Amplitude)
                    {
                        const auto volts = config.Amplitude->value();

                        if( volts < minAmplitude().value() || volts > maxAmplitude( termination).value())
                        {
                            throw SettingOutOfRange( instrument, "amplitude",
                                "of " + core::describeValue( *config.Amplitude) + " peak-to-peak is outside "
                                + core::describeValue( minAmplitude()) + " to "
                                + core::describeValue( maxAmplitude( termination)) + " "
                                + std::string( describeTermination( termination)));
                        }
                    }
                }

                if( config.Offset)
                {
                    const auto offset = config.Offset->value();
                    const auto peak   = maxPeak( termination).value();

                    if( offset < -peak || offset > peak)
                    {
                        throw SettingOutOfRange( instrument, "offset",
                            "of " + core::describeValue( *config.Offset) + " is outside +-"
                            + core::describeValue( maxPeak( termination)) + " "
                            + std::string( describeTermination( termination)));
                    }

                    //
                    // And the coupled limit the guide states as
                    // |Voffset| < Vmax - Vpp/2: an offset and an amplitude that
                    // are each legal on their own can still ask for a peak the
                    // output cannot reach. Checked only when this config names
                    // both, because when it names one the other is whatever the
                    // instrument is already set to -- a number this driver has
                    // not necessarily been told, and would be guessing at.
                    //
                    // What an attached instrument does with the pair is the
                    // reason this is worth catching here: it does not refuse
                    // them. It *adjusts the offset* to the largest value the
                    // amplitude allows, generates "Data out of range", and
                    // carries on generating -- so the DUT sees a signal
                    // centred somewhere the script never asked for.
                    //
                    if constexpr( Amplifiable<Shape>)
                    {
                        if( config.Amplitude)
                        {
                            const auto headroom = peak - config.Amplitude->value() / 2.0;

                            if( offset < -headroom || offset > headroom)
                            {
                                throw SettingOutOfRange( instrument, "offset",
                                    "of " + core::describeValue( *config.Offset) + " leaves no headroom for a "
                                    + core::describeValue( *config.Amplitude) + " peak-to-peak amplitude "
                                    + std::string( describeTermination( termination))
                                    + " -- the instrument allows |offset| <= "
                                    + core::describeValue( maxPeak( termination))
                                    + " - amplitude/2, so at most "
                                    + core::describeValue( core::quantities::Voltage{ headroom }));
                            }
                        }
                    }
                }

                if constexpr( HasDutyCycle<Shape>)
                {
                    //
                    // 0.01 to 99.99 percent, and the *achievable* range is
                    // narrower than that at speed: this model holds a 16 ns
                    // minimum pulse width, so at 1 MHz the duty cycle cannot go
                    // below 1.6% and at 10 MHz not below 16%. That narrowing is
                    // deliberately not modelled -- it is arithmetic on the
                    // frequency this config may not even carry -- so the check
                    // here is the absolute bound and the instrument keeps the
                    // frequency-dependent one.
                    //
                    checkPercent( instrument, "duty cycle", config.DutyCycle, 0.01, 99.99);
                }

                if constexpr( HasSymmetry<Shape>)
                {
                    checkPercent( instrument, "symmetry", config.Symmetry, 0.0, 100.0);
                }
            }

            static auto checkPercent( const std::string & instrument, const std::string_view setting,
                                      const std::optional<double> & value, const double low, const double high) -> void
            {
                if( !value || ( *value >= low && *value <= high))
                {
                    return;
                }

                throw SettingOutOfRange( instrument, setting,
                    "of " + io::ScpiSession::number( *value) + "% is outside "
                    + io::ScpiSession::number( low) + "% to " + io::ScpiSession::number( high) + "%");
            }

            //
            // The config, reduced to the plain data detail::program works in.
            // Every template parameter is spent here and none of it crosses
            // into the .cpp -- see detail's own comment.
            //
            template<typename Shape>
            [[nodiscard]]
            static auto flatten( const WaveformConfig<Shape> & config, const Termination termination) -> detail::Program
            {
                detail::Program flat;

                flat.Channel  = static_cast<int>( config.Channel);
                flat.Function = Shape::Function;

                //
                // Sent only when this config named one. An Apply that says
                // nothing about the termination leaves the instrument in
                // whichever one it is in -- and, since this driver's
                // remembered Load is then already that one, the limits were
                // checked against the same value the instrument will use.
                //
                if( config.Load)
                {
                    flat.Load = termination == Termination::HighImpedance ? "INF" : "50";
                }

                if( config.Frequency) { flat.Hertz       = config.Frequency->value(); }
                if( config.Amplitude) { flat.Volts       = config.Amplitude->value(); }
                if( config.Offset)    { flat.OffsetVolts = config.Offset->value();    }

                if constexpr( HasDutyCycle<Shape>)
                {
                    if( config.DutyCycle)
                    {
                        flat.ShapeCommand = Shape::DutyCycleCommand;
                        flat.ShapePercent = config.DutyCycle;
                    }
                }

                if constexpr( HasSymmetry<Shape>)
                {
                    if( config.Symmetry)
                    {
                        flat.ShapeCommand = Shape::SymmetryCommand;
                        flat.ShapePercent = config.Symmetry;
                    }
                }

                return flat;
            }

            //
            // "into 50 Ohm" / "into a high-impedance load" -- for the sentence
            // a SettingOutOfRange is, where the limit means nothing without the
            // termination it was derived from.
            //
            [[nodiscard]]
            static constexpr auto describeTermination( const Termination termination) -> std::string_view
            {
                return termination == Termination::HighImpedance ? "into a high-impedance load" : "into 50 Ohm";
            }

            InstrumentId  mId;
            BoxConnection mConnection;   // where this generator is, and the session to it -- shared per box, see hal::BoxConnection

            std::array<Output, 2>             mOutputs;
    };
} // namespace hal::keysight_33522b
