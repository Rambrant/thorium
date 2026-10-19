#pragma once

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>

#include "core/driver/describe.hpp"
#include "core/meta.hpp"
#include "core/driver/port.hpp"
#include "core/quantities/quantity.hpp"

#include "hal/driver/address.hpp"
#include "hal/driver/builder.hpp"
#include "hal/driver/describe.hpp"
#include "hal/driver/instrument.hpp"
#include "hal/fabric/switch_fabric.hpp"
#include "hal/topology/wiring.hpp"

#include "hal/keysight_ac6834b/vocabulary.hpp"
#include "hal/keysight_ac6834b/builder.hpp"

//
// the instrument class
//
// Part of hal/keysight_ac6834b.hpp -- include that, not this.
//

namespace hal::keysight_ac6834b
{
    //
    // The Agilent/Keysight 6834B AC Power Source/Analyzer: 4500 VA total
    // (1500 VA per phase), two output ranges (150 V/30 A and 300 V/15 A),
    // three-phase, GPIB/RS-232 with SCPI. Modeled after the physical
    // instrument deliberately -- see hal::keysight_edu36311a::DcSource's own comment in
    // hal/keysight_edu36311a.hpp for why.
    //
    // Everything below that names a SCPI command is taken from the Agilent
    // "AC Power Solutions" programming guide (part 5962-0889), which covers
    // models 6811B/6812B/6813B/6814B/6834B/6843A -- not from a datasheet
    // summary, and not guessed.
    //
    // ac() is the entry point and its name is a plain description,
    // not an axis: a 6834B is a three-phase model and has no phase-count
    // setting. Single-phase and three-phase are different *models* in this
    // family (the guide says "1-phase models" and "3-phase models"
    // throughout, and its Instrument subsystem exists to program "the
    // three-phase output capability of the Agilent 6834B"). An earlier
    // version of this comment claimed a programmable single/three-phase mode
    // switch and promised a singlePhase() sibling; that came from a product
    // page describing the family, and the programming guide does not support
    // it. There is no such command.
    //
    // Which leaves the name genuinely arbitrary -- nothing varies here for it
    // to distinguish. It has been threePhaseWye(), then wye(), then this; the
    // wye/delta wiring of the output is not something the programming guide
    // states either, so no spelling should assert it.
    //
    // Note phaseVoltage() keeps its prefix regardless: phase voltage
    // (line-to-neutral) and line voltage (line-to-line) differ by a factor of
    // root three, so that word disambiguates rather than repeats.
    //
    // Deliberately NOT modeled, all of them real 6834B capabilities and each
    // an addition rather than a change to what is here:
    //
    //   - VOLTage:OFFSet, a programmable DC offset on the AC output --
    //     another setpoint on this builder, not another builder.
    //   - PHASe, the per-phase angle (reset defaults 0/240/120). The one
    //     genuinely per-phase parameter still missing, and what unbalanced
    //     testing perturbs alongside magnitude. It does NOT fit PhaseSetting
    //     as voltage does: it is per-phase-only and cannot be broadcast --
    //     see Balanced/PerPhase above. It also wants an Angle quantity
    //     core::quantities has not got.
    //   - MEASure:POWer? -- per-phase power, which IS phase selectable. Only
    //     its instrument-wide sibling MEASure:POWer:AC:TOTal? is modeled (see
    //     measuredTotalPower); a per-phase power reading would slot in beside
    //     measuredVoltage/measuredCurrent.
    //   - MEASure:CURRent:NEUTral:AC? and :ACDC?, and the neutral's harmonic
    //     queries. Only the plain MEASure:CURRent:NEUTral? is modeled -- see
    //     measuredNeutralCurrent.
    //   - Waveform digitisation, harmonic analysis to the 50th harmonic, and
    //     THD. Analyser measurements rather than source settings, and not a
    //     small addition: they need new core::Port quantities, one of which
    //     (THD) is a bare ratio rather than anything with a unit.
    //   - Arbitrary and clipped output waveforms.
    //   - The coupling between VOLTage:RANGe and CURRent. The range itself
    //     IS modeled (see AcBuilder::range), but only as a setpoint:
    //     nothing here checks a current limit against it. The guide is
    //     explicit that "the maximum current limit that can be programmed at
    //     a given time depends on the voltage range setting in which the unit
    //     is presently operating", and that the order coupled commands are
    //     sent in matters -- so a real driver must both order the two and
    //     reject the combinations the instrument would. Deliberately left
    //     out for now: the check is a runtime one (it couples two runtime
    //     quantities, so no compile-time form is available), and a rig with
    //     no hardware behind it cannot exercise it.
    //
    // Fixed-wired, same reasoning and same safety motivation as
    // hal::keysight_edu36311a::DcSource (see that class's own comment) -- but with four physical
    // connections rather than one: phases A, B, and C, plus the neutral/
    // ground return. An earlier version of this file treated the neutral
    // return as simply not modeled at all ("hard-wired to ground, outside
    // the switching fabric entirely") on the theory that it's never
    // switched -- true, but that reasoning quietly stopped applying once
    // the three phases *also* became fixed, always-together relays rather
    // than mux-routed ones: if opening this instrument's path is meant to
    // fully isolate it (the same safety motivation for fixed-wiring the
    // phases in the first place), the return conductor is part of that
    // isolation too, not a wire that gets to sit outside the model.
    // ac() below takes no points -- connectDriver/disconnectDriver
    // close/open all four of this instrument's fixed channels together (see
    // hal::InstrumentWiring::findAll()), phases and ground alike, each its
    // own entry in the rig's wiring.inc under the same InstrumentId.
    //
    class AcSource : public InstrumentTag
    {
        public:
            //
            // One phase's output state, and the thing a per-phase reading is
            // actually taken from.
            //
            // It exists as a separate object rather than as three sets of
            // fields on Ac6834B because core::Port binds to a reference and
            // calls rawMeasure() on it (see core/driver/port.hpp): for
            // Measure( AcP1.measuredVoltage( Phase::B)) to read phase B and
            // not "the" voltage, the thing the port holds has to be phase B.
            // Each lives as a member of the instrument, so that reference is
            // as long-lived as the instrument itself.
            //
            // id() returns the whole instrument's id, not a per-phase one --
            // there is one AcP1 on this rig, and three phases of it. What
            // keeps the three readings apart in a recording is the port's
            // qualifier instead (see measuredVoltage() below).
            //
            class PhaseOutput
            {
                public:
                    [[nodiscard]]
                    auto id() const -> InstrumentId
                    {
                        return mId;
                    }

                    template<core::quantities::QuantityType QuantityT>
                    [[nodiscard]]
                    auto rawMeasure( const core::MeasureSetup<QuantityT> &) -> QuantityT
                    {
                        if constexpr( std::is_same_v<QuantityT, core::quantities::Voltage>)
                        {
                            return mEnabled ? mVoltage : core::quantities::Voltage{};
                        }
                        else if constexpr( std::is_same_v<QuantityT, core::quantities::Current>)
                        {
                            return mEnabled ? mSimOutputCurrent : core::quantities::Current{};
                        }
                        else
                        {
                            static_assert( !sizeof( QuantityT), "Ac6834B reports only its output voltage and current");
                        }
                    }

                    [[nodiscard]] auto voltage() const      -> core::quantities::Voltage                   { return mVoltage; }
                    [[nodiscard]] auto currentLimit() const -> std::optional<core::quantities::Current>    { return mCurrentLimit; }
                    [[nodiscard]] auto range() const        -> std::optional<core::quantities::Voltage>    { return mRange; }

                private:
                    friend class AcSource;

                    InstrumentId                              mId{};
                    core::quantities::Voltage                 mVoltage{};
                    core::quantities::Current                 mSimOutputCurrent{};
                    std::optional<core::quantities::Current>  mCurrentLimit;
                    std::optional<core::quantities::Voltage>  mRange;
                    bool                                       mEnabled{ false };
            };

            //
            // The neutral conductor's own readback -- MEASure:CURRent:NEUTral?
            // and nothing else. Deliberately not a Phase (see hal::keysight_ac6834b::Phase):
            // nothing is programmed onto the neutral, so it has no setpoints
            // and no business in the per-phase setters; it only answers a
            // current.
            //
            // Its own object rather than a case inside AcSource::rawMeasure for
            // the same reason PhaseOutput is one -- see
            // measuredNeutralCurrent().
            //
            class NeutralOutput
            {
                public:
                    [[nodiscard]]
                    auto id() const -> InstrumentId
                    {
                        return mId;
                    }

                    template<core::quantities::QuantityType QuantityT>
                    [[nodiscard]]
                    auto rawMeasure( const core::MeasureSetup<QuantityT> &) -> QuantityT
                    {
                        if constexpr( std::is_same_v<QuantityT, core::quantities::Current>)
                        {
                            return mEnabled ? mSimNeutralCurrent : core::quantities::Current{};
                        }
                        else
                        {
                            static_assert( !sizeof( QuantityT), "the neutral conductor reports only a current");
                        }
                    }

                private:
                    friend class AcSource;

                    InstrumentId               mId{};
                    core::quantities::Current  mSimNeutralCurrent{};
                    bool                        mEnabled{ false };
            };

            //
            // GPIB or RS-232, which is the whole of this model's remote
            // interface (see this class's own comment) -- no LAN and no USB,
            // so a rig row addressing it over either fails to compile rather
            // than at open time; see hal::ReachableOver in hal/driver/address.hpp.
            // hal::Serial here means the PC's own serial port, the cable this
            // source is commanded down -- not anything the DUT sees, and
            // nothing to do with hal::racal1260::Racal1260's RS232 framing.
            //
            // This model's back panel, written once and read twice: the
            // constructor below is constrained by it, and
            // hal::bindAddresses() checks an address supplied at startup
            // against the same list (see hal::BackPanel). A second
            // spelling of these connectors could disagree with this one,
            // and only the bench would ever find out.
            //
            using Buses = BackPanel<Gpib, Serial>;

            template<typename AddressT>
                requires Buses::allows<AddressT>
            AcSource( const InstrumentId id, const AddressT address) : mId( id), mAddress( address)
            {
                for( auto & phase : mPhases)
                {
                    phase.mId = id;
                }

                mNeutral.mId = id;
            }

            // Where the PC reaches this source -- see hal/driver/address.hpp.
            [[nodiscard]]
            auto address() const -> const Address &
            {
                return mAddress;
            }

            //
            // Point this driver at a different instrument -- see
            // hal::keysight_edu34450a::Multimeter::useAddress() for the
            // argument, and note the one line of it that is missing here.
            //
            // There is no session to drop. This driver opens none: it answers
            // from its own simulation hooks, which is why it has no
            // identity() and why hal::ContactableInstrument does not match it
            // (see hal/verbs/preflight.hpp on why that is branched on rather
            // than asserted). So rebinding it is a store and nothing else --
            // and it still has to exist, because the resolver refuses to
            // silently skip a row it cannot rebind.
            //
            auto useAddress( const Address & address) -> void
            {
                mAddress = address;
            }

            [[nodiscard]]
            auto id() const -> InstrumentId
            {
                return mId;
            }

            [[nodiscard]]
            auto ac() -> AcBuilder<Balanced>
            {
                return AcBuilder<Balanced>{ *this };
            }

            //
            // What this source reports about its own output, over its own
            // interface -- see hal::keysight_edu36311a::DcSource::measuredVoltage() for the reasoning,
            // which is the same here and matters more: 115 V at 5 A is not a
            // signal, so there is no version of this reading that travels
            // through the signal switching.
            //
            // Per phase, and the phase is named at the call site rather than
            // defaulted. It used to be neither -- one measuredVoltage() with
            // no argument, which was honest only while every phase was
            // guaranteed equal. Once phases can be driven independently, a
            // reading with no phase named has no answer, so there is
            // deliberately no no-argument overload to fall back to.
            //
            // A three-phase total would be a different quantity with a
            // different name, not this one under-specified.
            //
            // qualifiedBy() is what keeps the three apart in a recording: a
            // session keys an instrument readback by name, and all three
            // phases share one InstrumentId, so without it phase A and phase
            // C would be the same slot -- the same collision the key already
            // avoids between an instrument's voltage and its current (see
            // core::MeasureEngine's point-free overload).
            //
            [[nodiscard]]
            auto measuredVoltage( const Phase phase) -> core::Port<core::quantities::Voltage, PhaseOutput>
            {
                return core::Port<core::quantities::Voltage, PhaseOutput>{ mPhases[ indexOf( phase)] }
                           .qualifiedBy( to_string( phase));
            }

            [[nodiscard]]
            auto measuredCurrent( const Phase phase) -> core::Port<core::quantities::Current, PhaseOutput>
            {
                return core::Port<core::quantities::Current, PhaseOutput>{ mPhases[ indexOf( phase)] }
                           .qualifiedBy( to_string( phase));
            }

            //
            // The three readings that genuinely belong to the whole source
            // rather than to one phase, and therefore take no Phase argument.
            //
            // Which readings those are is the instrument's answer, not a
            // judgement call: in the programming guide, MEASure:VOLTage and
            // MEASure:CURRent are marked "Phase Selectable", while
            // MEASure:FREQuency?, MEASure:POWer:AC:TOTal? and
            // MEASure:CURRent:NEUTral? are not. So there is a no-argument
            // frequency, a no-argument total power and a no-argument neutral
            // current -- and deliberately still no no-argument voltage, because
            // the instrument has nothing to answer such a query with. That is
            // the same distinction argued for before the guide was available
            // (a three-phase total is a different quantity, not this one
            // under-specified), now settled by the guide rather than by
            // reasoning.
            //
            // MEASure:FREQuency? -- one output frequency, matching a setter
            // that is likewise scalar-only (see AcConfig).
            //
            [[nodiscard]]
            auto measuredFrequency() -> core::Port<core::quantities::Frequency, AcSource>
            {
                return core::Port<core::quantities::Frequency, AcSource>{ *this };
            }

            //
            // MEASure:POWer:AC:TOTal? -- real total power across all three
            // phases. Qualified "Total" rather than left bare because
            // MEASure:POWer? is itself Phase Selectable: per-phase power is a
            // real future addition, and an unqualified key here would collide
            // with it in a recording (see core::Port::qualifiedBy).
            //
            [[nodiscard]]
            auto measuredTotalPower() -> core::Port<core::quantities::Power, AcSource>
            {
                return core::Port<core::quantities::Power, AcSource>{ *this }
                           .qualifiedBy( "Total");
            }

            //
            // MEASure:CURRent:NEUTral? -- the current returning through the
            // neutral conductor, which on a balanced load is near zero and on
            // an unbalanced one is the thing worth watching.
            //
            // Bound to its own object rather than to the instrument, for the
            // same reason each phase is (see PhaseOutput): core::Port calls
            // rawMeasure() on whatever it holds, and the neutral's current is
            // a different reading from any phase's. That also keeps
            // AcSource::rawMeasure free of a Current case that would otherwise
            // have to mean "neutral" by convention.
            //
            [[nodiscard]]
            auto measuredNeutralCurrent() -> core::Port<core::quantities::Current, NeutralOutput>
            {
                return core::Port<core::quantities::Current, NeutralOutput>{ mNeutral }
                           .qualifiedBy( "N");
            }

            //
            // The instrument-wide readings' backing. Only the two quantities
            // the whole source can answer for; a Current here would be
            // ambiguous (whose?), which is why the neutral has its own object
            // above and each phase has one of its own.
            //
            template<core::quantities::QuantityType QuantityT>
            [[nodiscard]]
            auto rawMeasure( const core::MeasureSetup<QuantityT> &) -> QuantityT
            {
                if constexpr( std::is_same_v<QuantityT, core::quantities::Frequency>)
                {
                    return mEnabled ? mFrequency.value_or( core::quantities::Frequency{}) : core::quantities::Frequency{};
                }
                else if constexpr( std::is_same_v<QuantityT, core::quantities::Power>)
                {
                    return mEnabled ? mSimTotalPower : core::quantities::Power{};
                }
                else
                {
                    static_assert( !sizeof( QuantityT),
                                   "Ac6834B answers only frequency and total power for the source as a whole -- "
                                   "voltage and current are per phase (see measuredVoltage/measuredCurrent), and "
                                   "neutral current is measuredNeutralCurrent()");
                }
            }

            //
            // Drop this source to a known idle state, unconditionally --
            // same contract, and the same reasoning for zeroing the
            // setpoint rather than only disabling the output, as
            // hal::keysight_edu36311a::DcSource::safe(); see that function's own comment.
            // mFrequency and the per-phase current limits are left as-is for
            // the same reason its mCurrentLimit is.
            //
            auto safe() -> void
            {
                mEnabled          = false;
                mNeutral.mEnabled = false;

                for( auto & phase : mPhases)
                {
                    phase.mEnabled = false;
                    phase.mVoltage = core::quantities::Voltage{};
                }
            }

            // Test/simulation hooks -- real hardware has no such setters.
            auto applyOutput( const PerPhaseValues<core::quantities::Voltage> &            phaseVoltage,
                              const std::optional<core::quantities::Frequency>             frequency,
                              const std::optional<PerPhaseValues<core::quantities::Current>> & currentLimit,
                              const std::optional<PerPhaseValues<core::quantities::Voltage>> & range = std::nullopt) -> void
            {
                mFrequency       = frequency;
                mEnabled         = true;
                mNeutral.mEnabled = true;

                for( std::size_t index = 0; index < mPhases.size(); ++index)
                {
                    mPhases[ index].mVoltage      = phaseVoltage[ index];
                    mPhases[ index].mCurrentLimit = currentLimit ? std::optional{ ( *currentLimit)[ index] } : std::nullopt;
                    mPhases[ index].mEnabled      = true;

                    //
                    // Only overwritten when the config actually carried a
                    // range: an Apply that says nothing about the range leaves
                    // the instrument in whichever one it was already in, which
                    // is what nullopt means everywhere else here too.
                    //
                    if( range)
                    {
                        mPhases[ index].mRange = ( *range)[ index];
                    }
                }
            }

            //
            // The balanced spelling of the same hook -- one value on every
            // phase. Kept because a test that does not care about the
            // per-phase split should not have to write the same quantity out
            // three times to say so.
            //
            auto applyOutput( const core::quantities::Voltage                  phaseVoltage,
                              const std::optional<core::quantities::Frequency> frequency,
                              const std::optional<core::quantities::Current>   currentLimit) -> void
            {
                applyOutput( PerPhaseValues<core::quantities::Voltage>{ phaseVoltage, phaseVoltage, phaseVoltage },
                             frequency,
                             detail::broadcast( currentLimit));
            }

            auto removeOutput() -> void
            {
                mEnabled          = false;
                mNeutral.mEnabled = false;

                for( auto & phase : mPhases)
                {
                    phase.mEnabled = false;
                }
            }

            // Per-phase current the simulated source is delivering -- what a
            // real instrument reports back, which no setpoint determines.
            auto setSimulatedOutputCurrent( const Phase phase, const core::quantities::Current c) -> void
            {
                mPhases[ indexOf( phase)].mSimOutputCurrent = c;
            }

            // The same on every phase -- the balanced convenience, as above.
            auto setSimulatedOutputCurrent( const core::quantities::Current c) -> void
            {
                for( auto & phase : mPhases)
                {
                    phase.mSimOutputCurrent = c;
                }
            }

            // What the instrument-wide readbacks report -- neither is
            // determined by any setpoint, so like the per-phase current they
            // are simulation inputs rather than derived values.
            auto setSimulatedNeutralCurrent( const core::quantities::Current c) -> void
            {
                mNeutral.mSimNeutralCurrent = c;
            }

            auto setSimulatedTotalPower( const core::quantities::Power p) -> void
            {
                mSimTotalPower = p;
            }

            [[nodiscard]]
            auto isEnabled() const -> bool
            {
                return mEnabled;
            }

            [[nodiscard]]
            auto phaseVoltage( const Phase phase) const -> core::quantities::Voltage
            {
                return mPhases[ indexOf( phase)].voltage();
            }

            [[nodiscard]]
            auto currentLimit( const Phase phase) const -> std::optional<core::quantities::Current>
            {
                return mPhases[ indexOf( phase)].currentLimit();
            }

            //
            // The range this phase is programmed into -- what VOLTage:RANGe?
            // would return, i.e. 150 V or 300 V, never the raw value a caller
            // passed to range() (see hal::rangeFor).
            //
            [[nodiscard]]
            auto range( const Phase phase) const -> std::optional<core::quantities::Voltage>
            {
                return mPhases[ indexOf( phase)].range();
            }

            [[nodiscard]]
            auto frequency() const -> std::optional<core::quantities::Frequency>
            {
                return mFrequency;
            }

        private:
            InstrumentId                                 mId;
            Address                                      mAddress;
            std::array<PhaseOutput, 3>                   mPhases;
            NeutralOutput                                mNeutral;
            std::optional<core::quantities::Frequency>   mFrequency;
            core::quantities::Power                      mSimTotalPower{};
            bool                                          mEnabled{ false };
    };
} // namespace hal::keysight_ac6834b
