#pragma once

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>

#include "core/apply.hpp"
#include "core/meta.hpp"
#include "core/port.hpp"
#include "core/quantity.hpp"

#include "hal/address.hpp"
#include "hal/describe.hpp"
#include "hal/instrument.hpp"
#include "hal/switch_fabric.hpp"
#include "hal/wiring.hpp"

namespace hal
{
    class Ac6834B;

    //
    // Which of the three phases a setting or a reading belongs to. Named
    // A/B/C after the conductors themselves, matching the rig's four
    // WIRE_SOURCE rows for AcP1 and the dut::AcInput bundle's lines (see
    // rig/wiring.inc, dut/adapter.inc) -- the same three wires seen from the
    // instrument side.
    //
    // Numbered 1/2/3 by the instrument itself -- INSTrument:NSELect takes
    // 1|2|3 and the reset phase angles are documented as phase 1 = 0,
    // phase 2 = 240, phase 3 = 120. A/B/C is the conductor naming, which is
    // what the DUT side uses and what a bench technician reads off a cable,
    // so it is what this enum spells; a real driver maps A/B/C onto NSELect
    // 1/2/3, which is indexOf() + 1.
    //
    // The neutral/ground return is deliberately not an enumerator, but the
    // reason is narrower than it first looks. Nothing is *programmed* onto
    // it -- there is no neutral voltage or current-limit setpoint -- so it
    // has no place in any of the per-phase setters, which is what this enum
    // exists for. It is not unmeasurable, though -- the instrument answers
    // MEASure:CURRent:NEUTral? on three-phase models, and that reading has a
    // home of its own: hal::Ac6834B::measuredNeutralCurrent(), which takes no
    // Phase precisely because the neutral is not one.
    //
    enum class Phase
    {
        A,
        B,
        C
    };

    //
    // Reflected over Phase's own enumerators, not a switch -- see
    // core::meta::to_string's own comment in core/meta.hpp, which names the
    // two hand-written switches it replaced. A third one here would have
    // spelled A/B/C a second time for no reason beyond stringifying them.
    //
    // Kept as a hal:: function rather than calling core::meta::to_string at
    // each site, so an unqualified to_string( phase) resolves by ADL the way
    // hal::to_string( InstrumentId) already does (see hal/src/instrument.cpp,
    // the same one-line delegation).
    //
    [[nodiscard]]
    constexpr auto to_string( const Phase phase) -> std::string_view
    {
        return core::meta::to_string( phase);
    }

    //
    // All three, in declaration order -- for the "check each phase" loop,
    // which would otherwise repeat { Phase::A, Phase::B, Phase::C } at every
    // call site. Reflected for the same reason to_string() above is.
    //
    inline constexpr auto phases = core::meta::values<Phase>;

    //
    // One value per phase, in A/B/C order. A fixed-size array rather than a
    // braced list of three: an initializer_list would let a two-element or
    // four-element list compile, and "I gave it the wrong number of phases"
    // is exactly the mistake worth having the compiler catch.
    //
    template<typename QuantityT>
    using PerPhaseValues = std::array<QuantityT, 3>;

    //
    // Where a phase's value sits in a PerPhaseValues. The cast is safe only
    // because Phase leaves its enumerators at their default 0/1/2, which the
    // assertions below pin down -- an explicitly-numbered enumerator would
    // index past the array or alias another phase, and neither shows up as
    // anything but a wrong reading.
    //
    [[nodiscard]]
    constexpr auto indexOf( const Phase phase) -> std::size_t
    {
        return static_cast<std::size_t>( phase);
    }

    static_assert( phases.size() == std::tuple_size_v<PerPhaseValues<int>>,
                   "PerPhaseValues holds one slot per Phase enumerator");
    static_assert( indexOf( Phase::A) == 0 && indexOf( Phase::B) == 1 && indexOf( Phase::C) == 2,
                   "Phase enumerators must keep their default values -- indexOf() indexes "
                   "PerPhaseValues with them directly");

    //
    // A quantity tagged with the phase it is meant for -- what phaseA()/
    // phaseB()/phaseC() below produce, and what the per-phase setters take.
    //
    // The phase lives in the type, not in a runtime field, so the three
    // arguments of a per-phase setter can only be supplied in A/B/C order:
    // writing .phaseVoltage( phaseB( ...), phaseA( ...), phaseC( ...)) is a
    // compile error rather than a silent transposition.
    //
    // This is the same reasoning core::at() is built on (see core/at.hpp):
    // the alternative spelling, a bare triple like { 115_V, 113_V, 117_V },
    // is shorter but depends on the reader knowing which position is which
    // conductor -- and reads identically whether or not it is right.
    //
    template<typename QuantityT, Phase P>
    struct PhaseValue
    {
        static constexpr Phase Which = P;

        QuantityT Value;
    };

    template<typename QuantityT>
    [[nodiscard]]
    constexpr auto phaseA( const QuantityT value) -> PhaseValue<QuantityT, Phase::A>
    {
        return { value };
    }

    template<typename QuantityT>
    [[nodiscard]]
    constexpr auto phaseB( const QuantityT value) -> PhaseValue<QuantityT, Phase::B>
    {
        return { value };
    }

    template<typename QuantityT>
    [[nodiscard]]
    constexpr auto phaseC( const QuantityT value) -> PhaseValue<QuantityT, Phase::C>
    {
        return { value };
    }

    //
    // Whether a configuration drives all three phases alike or each one
    // separately.
    //
    // Tag types on the builder and its config rather than a runtime flag, the
    // same shape (and for the same reason) as hal::DirectWiring/
    // RelayIsolated in hal/n6701a.hpp: what differs between the two is the
    // *type* of what a config holds -- one voltage or three -- so a driver
    // reading it never has to ask which case it is in.
    //
    // This is the only axis the builder has, and deliberately so: it is a
    // property the chain acquires rather than a second entry-point method
    // alongside ac(). Entry points multiply against anything else
    // that ever becomes selectable, where a builder-carried tag does not --
    // and on this instrument there is nothing else to select anyway (see
    // hal::Ac6834B on why the phase count is not a setting).
    //
    // This split is the instrument's own, not an invention of this header.
    // The 6834B has INSTrument:COUPle <ALL|NONE>: under COUPle ALL, sending a
    // command to any phase sends it to all three, and the commands this
    // affects are exactly those the programming guide marks "Phase
    // Selectable". Balanced is COUPle ALL; PerPhase is COUPle NONE plus
    // INSTrument:NSELect per phase. The guide's *RST value for COUPle is ALL,
    // which is also why ac() starts a Balanced chain rather than a
    // per-phase one.
    //
    // Which settings are phase-selectable is likewise the guide's answer and
    // not a guess: VOLTage and CURRent (the rms current limit) carry the
    // designation, FREQuency does not -- see Ac6834BConfig below, where that
    // asymmetry is the whole reason frequency is a plain scalar.
    //
    // One command does not fit this scheme and will need care when it is
    // added: PHASe, the per-phase angle, is "not influenced by
    // INSTrument:COUPle ALL. It applies only to the current output phase
    // selected by INSTrument:NSELect." So phase angle is per-phase *only* --
    // it can never be broadcast, and a scalar setter for it would be a lie
    // rather than a convenience. That makes three categories, where this
    // header currently models two: scalar-only (frequency), scalar-or-
    // per-phase (voltage, current limit), and per-phase-only (angle).
    //
    struct Balanced {};
    struct PerPhase {};

    //
    // A setting that may differ per phase: three values under PerPhase, one
    // under Balanced. Frequency deliberately never uses this -- see
    // Ac6834BConfig below.
    //
    template<typename Symmetry, typename QuantityT>
    using PhaseSetting = std::conditional_t<std::is_same_v<Symmetry, PerPhase>,
                                            std::optional<PerPhaseValues<QuantityT>>,
                                            std::optional<QuantityT>>;

    //
    // The range an arbitrary requested voltage actually selects. The
    // instrument resolves the parameter rather than storing it: "sending a
    // parameter greater than 150 selects the 300 volt range, otherwise the
    // 150 volt range is selected", and VOLTage:RANGe? reads back the range,
    // not what was sent. Modelling that here means range( 115_V) and
    // range( 150_V) are the same instruction, which is what the hardware
    // does, and means a readback can never report a value the instrument
    // would not.
    //
    // Not a validation step -- nothing here rejects anything. The current
    // limit that this range constrains is deliberately left unchecked; see
    // hal::Ac6834B's list of what is not modeled.
    //
    inline constexpr auto LowVoltageRange  = core::quantities::Voltage{ 150.0 };
    inline constexpr auto HighVoltageRange = core::quantities::Voltage{ 300.0 };

    [[nodiscard]]
    constexpr auto rangeFor( const core::quantities::Voltage requested) -> core::quantities::Voltage
    {
        return requested.value() > LowVoltageRange.value() ? HighVoltageRange : LowVoltageRange;
    }

    //
    // Symmetric with N6701AConfig (see hal/n6701a.hpp) -- no points here
    // either, and for the same reason: AcP1's three phases (and its
    // ground/neutral return -- see this header's own comment on Ac6834B)
    // are each hard-cabled to one fixed VPC pin, not routed through a mux
    // to whichever pins a script names, so there's nothing left for a
    // point argument to carry.
    //
    // Frequency is a plain scalar under both symmetries, and that is the
    // instrument's own answer rather than a simplification: in the 6834B's
    // programming guide, VOLTage and CURRent carry the "Phase Selectable"
    // designation and FREQuency does not. Physically that is what one would
    // expect -- three phases of one source share a fixed relationship, so
    // "phase A at 400 Hz, phase B at 50 Hz" is not an unbalanced
    // configuration this type declines to express, it is not a configuration
    // at all -- but the guide settles it rather than the reasoning.
    //
    template<typename Symmetry>
    struct Ac6834BConfig
    {
        Ac6834B &                                           Instrument;
        PhaseSetting<Symmetry, core::quantities::Voltage>   PhaseVoltage;
        std::optional<core::quantities::Frequency>          Frequency;
        PhaseSetting<Symmetry, core::quantities::Current>   CurrentLimit;

        //
        // VOLTage:RANGe -- which of the instrument's two output ranges is in
        // force. Phase Selectable, like PhaseVoltage and CurrentLimit, so it
        // gets the same treatment.
        //
        // Held as a Voltage rather than a two-valued enum because that is what
        // the command takes: "sending a parameter greater than 150 selects the
        // 300 volt range, otherwise the 150 volt range is selected". An enum
        // would read as though the caller picks a range directly, when what
        // they actually supply is a voltage the instrument then resolves --
        // see hal::rangeFor().
        //
        PhaseSetting<Symmetry, core::quantities::Voltage>   Range;
    };

    namespace detail
    {
        //
        // One value repeated across all three phases. What a scalar setter
        // means once a configuration has become per-phase: "the same on each"
        // is a perfectly ordinary thing to say about an otherwise unbalanced
        // config (different voltages, one shared current limit), so it stays
        // expressible rather than forcing the caller to write the same
        // quantity out three times.
        //
        template<typename QuantityT>
        [[nodiscard]]
        constexpr auto broadcast( const std::optional<QuantityT> & value) -> std::optional<PerPhaseValues<QuantityT>>
        {
            if( ! value.has_value())
            {
                return std::nullopt;
            }

            return PerPhaseValues<QuantityT>{ *value, *value, *value };
        }

        // Already per-phase -- the identity case, so asPerPhase() below can be
        // written once for both symmetries.
        template<typename QuantityT>
        [[nodiscard]]
        constexpr auto broadcast( const std::optional<PerPhaseValues<QuantityT>> & values) -> std::optional<PerPhaseValues<QuantityT>>
        {
            return values;
        }

        //
        // Any config, widened to the per-phase form. The one place the two
        // symmetries are reconciled: every driver below works in per-phase
        // terms and calls this first, so "balanced" costs exactly one
        // conversion at the boundary rather than a second copy of each
        // driver body.
        //
        template<typename Symmetry>
        [[nodiscard]]
        constexpr auto asPerPhase( const Ac6834BConfig<Symmetry> & config) -> Ac6834BConfig<PerPhase>
        {
            return Ac6834BConfig<PerPhase>{
                config.Instrument,
                broadcast( config.PhaseVoltage),
                config.Frequency,
                broadcast( config.CurrentLimit),
                broadcast( config.Range)
            };
        }
    } // namespace detail

    //
    // Same fluent-builder shape as N6701ABuilder -- see its own comment in
    // hal/n6701a.hpp -- with one addition: a per-phase setter returns a
    // builder of a *different* symmetry, so a chain that names phases
    // individually ends up holding an Ac6834BConfig<PerPhase> and one that
    // does not keeps the balanced config it started with.
    //
    // That is what makes balanced-vs-per-phase a type-level fact without
    // spending a second entry-point method on it (see Balanced/PerPhase
    // above). A script that never mentions an individual phase cannot
    // accidentally produce an unbalanced config, and describeConfig can say
    // which it was without guessing from whether three stored values happen
    // to be equal.
    //
    // The transition is one-way. Going back would mean deciding which of
    // three values the balanced one is, and there is no answer to that which
    // isn't a guess.
    //
    template<typename Symmetry>
    class Ac6834BBuilder
    {
        public:
            using Config = Ac6834BConfig<Symmetry>;

            explicit Ac6834BBuilder( Ac6834B & instrument) :
                mConfig{ instrument, std::nullopt, std::nullopt, std::nullopt, std::nullopt }
            {}

            // Used by the per-phase setters below to carry an already-built
            // config across the symmetry change.
            explicit Ac6834BBuilder( Config config) : mConfig( std::move( config)) {}

            //
            // All three phases alike. Under PerPhase this broadcasts rather
            // than being unavailable -- see detail::broadcast's own comment.
            //
            [[nodiscard]]
            auto phaseVoltage( const core::quantities::Voltage v) const -> Ac6834BBuilder
            {
                auto copy = *this;
                copy.mConfig.PhaseVoltage = settingOf( v);
                return copy;
            }

            //
            // Each phase its own voltage -- and the point of the whole
            // symmetry split. Returns a per-phase builder whatever this one
            // was.
            //
            [[nodiscard]]
            auto phaseVoltage( const PhaseValue<core::quantities::Voltage, Phase::A> a,
                               const PhaseValue<core::quantities::Voltage, Phase::B> b,
                               const PhaseValue<core::quantities::Voltage, Phase::C> c) const -> Ac6834BBuilder<PerPhase>
            {
                auto widened = detail::asPerPhase( mConfig);

                widened.PhaseVoltage = PerPhaseValues<core::quantities::Voltage>{ a.Value, b.Value, c.Value };

                return Ac6834BBuilder<PerPhase>{ widened };
            }

            //
            // One frequency, no per-phase overload -- see Ac6834BConfig's own
            // comment for why there is nothing here to overload.
            //
            [[nodiscard]]
            auto frequency( const core::quantities::Frequency f) const -> Ac6834BBuilder
            {
                auto copy = *this;
                copy.mConfig.Frequency = f;
                return copy;
            }

            [[nodiscard]]
            auto currentLimit( const core::quantities::Current c) const -> Ac6834BBuilder
            {
                auto copy = *this;
                copy.mConfig.CurrentLimit = settingOf( c);
                return copy;
            }

            [[nodiscard]]
            auto currentLimit( const PhaseValue<core::quantities::Current, Phase::A> a,
                               const PhaseValue<core::quantities::Current, Phase::B> b,
                               const PhaseValue<core::quantities::Current, Phase::C> c) const -> Ac6834BBuilder<PerPhase>
            {
                auto widened = detail::asPerPhase( mConfig);

                widened.CurrentLimit = PerPhaseValues<core::quantities::Current>{ a.Value, b.Value, c.Value };

                return Ac6834BBuilder<PerPhase>{ widened };
            }

            //
            // VOLTage:RANGe -- which output range to program in. Takes the
            // voltage the range must accommodate, not a range identifier, and
            // stores what the instrument would actually select (see
            // hal::rangeFor): .range( 115_V) and .range( 150_V) are one
            // instruction.
            //
            // Named range() rather than voltageRange() to match
            // core::Port::range() on the measurement side, which means the
            // same thing -- the full-scale value in force, not a span. There
            // is only one range on this instrument, so nothing is ambiguous.
            //
            // Left unset, nothing is programmed and whatever range the
            // instrument is already in stays -- the same meaning std::nullopt
            // carries for every other setpoint here. Note that is NOT the
            // instrument's own reset state, which is VOLTage:RANGe MAXimum,
            // i.e. the 300 V range.
            //
            [[nodiscard]]
            auto range( const core::quantities::Voltage accommodating) const -> Ac6834BBuilder
            {
                auto copy = *this;
                copy.mConfig.Range = settingOf( rangeFor( accommodating));
                return copy;
            }

            [[nodiscard]]
            auto range( const PhaseValue<core::quantities::Voltage, Phase::A> a,
                        const PhaseValue<core::quantities::Voltage, Phase::B> b,
                        const PhaseValue<core::quantities::Voltage, Phase::C> c) const -> Ac6834BBuilder<PerPhase>
            {
                auto widened = detail::asPerPhase( mConfig);

                widened.Range = PerPhaseValues<core::quantities::Voltage>{
                    rangeFor( a.Value), rangeFor( b.Value), rangeFor( c.Value) };

                return Ac6834BBuilder<PerPhase>{ widened };
            }

            [[nodiscard]]
            auto config() const -> const Config &
            {
                return mConfig;
            }

        private:
            //
            // A scalar, in whichever form this symmetry's config stores it.
            //
            template<typename QuantityT>
            [[nodiscard]]
            static constexpr auto settingOf( const QuantityT value) -> PhaseSetting<Symmetry, QuantityT>
            {
                if constexpr( std::is_same_v<Symmetry, PerPhase>)
                {
                    return PerPhaseValues<QuantityT>{ value, value, value };
                }
                else
                {
                    return value;
                }
            }

            Config mConfig;
    };

    //
    // The Agilent/Keysight 6834B AC Power Source/Analyzer: 4500 VA total
    // (1500 VA per phase), two output ranges (150 V/30 A and 300 V/15 A),
    // three-phase, GPIB/RS-232 with SCPI. Modeled after the physical
    // instrument deliberately -- see hal::N6701A's own comment in
    // hal/n6701a.hpp for why.
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
    //     IS modeled (see Ac6834BBuilder::range), but only as a setpoint:
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
    // hal::N6701A (see that class's own comment) -- but with four physical
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
    class Ac6834B : public InstrumentTag
    {
        public:
            //
            // One phase's output state, and the thing a per-phase reading is
            // actually taken from.
            //
            // It exists as a separate object rather than as three sets of
            // fields on Ac6834B because core::Port binds to a reference and
            // calls rawMeasure() on it (see core/port.hpp): for
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
                    friend class Ac6834B;

                    InstrumentId                              mId{};
                    core::quantities::Voltage                 mVoltage{};
                    core::quantities::Current                 mSimOutputCurrent{};
                    std::optional<core::quantities::Current>  mCurrentLimit;
                    std::optional<core::quantities::Voltage>  mRange;
                    bool                                       mEnabled{ false };
            };

            //
            // The neutral conductor's own readback -- MEASure:CURRent:NEUTral?
            // and nothing else. Deliberately not a Phase (see hal::Phase):
            // nothing is programmed onto the neutral, so it has no setpoints
            // and no business in the per-phase setters; it only answers a
            // current.
            //
            // Its own object rather than a case inside Ac6834B::rawMeasure for
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
                    friend class Ac6834B;

                    InstrumentId               mId{};
                    core::quantities::Current  mSimNeutralCurrent{};
                    bool                        mEnabled{ false };
            };

            //
            // GPIB or RS-232, which is the whole of this model's remote
            // interface (see this class's own comment) -- no LAN and no USB,
            // so a rig row addressing it over either fails to compile rather
            // than at open time; see hal::ReachableOver in hal/address.hpp.
            // hal::Serial here means the PC's own serial port, the cable this
            // source is commanded down -- not anything the DUT sees, and
            // nothing to do with hal::Racal1260's RS232 framing.
            //
            template<typename AddressT>
                requires ReachableOver<AddressT, Gpib, Serial>
            Ac6834B( const InstrumentId id, const AddressT address) : mId( id), mAddress( address)
            {
                for( auto & phase : mPhases)
                {
                    phase.mId = id;
                }

                mNeutral.mId = id;
            }

            // Where the PC reaches this source -- see hal/address.hpp.
            [[nodiscard]]
            auto address() const -> const Address &
            {
                return mAddress;
            }

            [[nodiscard]]
            auto id() const -> InstrumentId
            {
                return mId;
            }

            [[nodiscard]]
            auto ac() -> Ac6834BBuilder<Balanced>
            {
                return Ac6834BBuilder<Balanced>{ *this };
            }

            //
            // What this source reports about its own output, over its own
            // interface -- see hal::N6701A::measuredVoltage() for the reasoning,
            // which is the same here and matters more: 115 V at 5 A is not a
            // signal, so there is no version of this reading that travels
            // through the switching matrix.
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
            // that is likewise scalar-only (see Ac6834BConfig).
            //
            [[nodiscard]]
            auto measuredFrequency() -> core::Port<core::quantities::Frequency, Ac6834B>
            {
                return core::Port<core::quantities::Frequency, Ac6834B>{ *this };
            }

            //
            // MEASure:POWer:AC:TOTal? -- real total power across all three
            // phases. Qualified "Total" rather than left bare because
            // MEASure:POWer? is itself Phase Selectable: per-phase power is a
            // real future addition, and an unqualified key here would collide
            // with it in a recording (see core::Port::qualifiedBy).
            //
            [[nodiscard]]
            auto measuredTotalPower() -> core::Port<core::quantities::Power, Ac6834B>
            {
                return core::Port<core::quantities::Power, Ac6834B>{ *this }
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
            // Ac6834B::rawMeasure free of a Current case that would otherwise
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
            // hal::N6701A::safe(); see that function's own comment.
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

    //
    // ADL targets for core::ApplyEngine/RemoveEngine -- see hal/n6701a.hpp's
    // own comment on the same mechanism. Programs -- or disables -- the
    // instrument's simulated output only; see connectDriver/disconnectDriver
    // below for the fabric routing this used to also do.
    //
    // One template over both symmetries rather than an overload apiece: the
    // body is the same once detail::asPerPhase() has run, and hal::N6701A's
    // SwitchableIsolation comment gives the reason for preferring that shape
    // -- an overload per named tag means every tag carries an identical copy
    // of the same body, purely because it was named.
    //
    template<typename Symmetry>
    auto applyDriver( const Ac6834BConfig<Symmetry> & config) -> void
    {
        const auto perPhase = detail::asPerPhase( config);

        config.Instrument.applyOutput(
            perPhase.PhaseVoltage.value_or( PerPhaseValues<core::quantities::Voltage>{}),
            perPhase.Frequency,
            perPhase.CurrentLimit,
            perPhase.Range);
    }

    template<typename Symmetry>
    auto removeDriver( const Ac6834BConfig<Symmetry> & config) -> void
    {
        config.Instrument.removeOutput();
    }

    namespace detail
    {
        //
        // The per-phase counterpart of hal::describeSetting (hal/describe.hpp)
        // -- "phaseVoltage=[A 115V, B 113V, C 117V]". Local to this header
        // rather than alongside the shared helpers because "has three values,
        // one per phase" is a shape only this instrument has.
        //
        template<typename QuantityT>
        [[nodiscard]]
        auto describePhaseSetting( const std::string_view name, const std::optional<PerPhaseValues<QuantityT>> & values) -> std::string
        {
            if( ! values.has_value())
            {
                return {};
            }

            std::string rendered;

            for( const auto phase : phases)
            {
                if( ! rendered.empty())
                {
                    rendered += ", ";
                }

                rendered += std::string( to_string( phase)) + " " + core::describeValue( ( *values)[ indexOf( phase)]);
            }

            return std::string( name) + "=[" + rendered + "]";
        }
    } // namespace detail

    //
    // ADL target for the run journal -- see hal/n6701a.hpp's describeConfig for
    // the same mechanism and hal/describe.hpp for the helpers. "3-phase" is
    // stated rather than implied so a reader of the log knows what was
    // driven without going to look up the model -- not because it could have
    // been anything else on this instrument (it could not; see this class's
    // own comment). It stops there and does not claim wye or delta, because
    // the programming guide does not say which this instrument's three-phase
    // output is.
    //
    // The same argument now applies to the symmetry, which is why a per-phase
    // config says so and renders all three values. A log that showed only
    // "phaseVoltage=115V" for a deliberately unbalanced run would describe a
    // test that was never performed.
    //
    template<typename Symmetry>
    auto describeConfig( const Ac6834BConfig<Symmetry> & config) -> core::SourceDescription
    {
        auto settings = [&]() -> std::vector<std::string>
        {
            if constexpr( std::is_same_v<Symmetry, PerPhase>)
            {
                return {
                    "3-phase, per-phase",
                    detail::describePhaseSetting( "phaseVoltage", config.PhaseVoltage),
                    describeSetting( "frequency", config.Frequency),
                    detail::describePhaseSetting( "currentLimit", config.CurrentLimit),
                    detail::describePhaseSetting( "range", config.Range)
                };
            }
            else
            {
                return {
                    "3-phase",
                    describeSetting( "phaseVoltage", config.PhaseVoltage),
                    describeSetting( "frequency",    config.Frequency),
                    describeSetting( "currentLimit", config.CurrentLimit),
                    describeSetting( "range",        config.Range)
                };
            }
        }();

        return core::SourceDescription{
            std::string( to_string( config.Instrument.id())),
            describeSettings( settings)
        };
    }

    //
    // ADL targets for core::ConnectEngine/DisconnectEngine -- see
    // hal/n6701a.hpp's own comment on the same mechanism. Closes -- or
    // opens -- all four of this instrument's fixed channels together (see
    // hal::InstrumentWiring::findAll() and this class's own comment on why
    // ground/neutral is included), additively (hal::SwitchFabric::
    // connect()/disconnect()) so it doesn't disturb whatever else is
    // currently routed. connectorWiring is accepted (signature symmetry
    // with every other instrument's connectDriver/disconnectDriver) but
    // never consulted -- see N6701A's own connectDriver for the same
    // reasoning.
    //
    // Templated over the symmetry and otherwise untouched by it: which
    // relays this instrument owns has nothing to do with whether its phases
    // are driven alike.
    //
    template<typename Symmetry>
    auto connectDriver( SwitchFabric & fabric, const InstrumentWiring & instrumentWiring, const ConnectorWiring &, const Ac6834BConfig<Symmetry> & config) -> void
    {
        fabric.connect( instrumentWiring.findAll( config.Instrument.id()));
    }

    template<typename Symmetry>
    auto disconnectDriver( SwitchFabric & fabric, const InstrumentWiring & instrumentWiring, const ConnectorWiring &, const Ac6834BConfig<Symmetry> & config) -> void
    {
        fabric.disconnect( instrumentWiring.findAll( config.Instrument.id()));
    }
} // namespace hal
