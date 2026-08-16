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

#include "hal/describe.hpp"
#include "hal/instrument.hpp"
#include "hal/switch_fabric.hpp"
#include "hal/wiring.hpp"

namespace hal
{
    class Ac6677A;

    //
    // Which of the three phases a setting or a reading belongs to. Named
    // A/B/C after the conductors themselves, matching the rig's four
    // WIRE_SOURCE rows for AcP1 and the dut::AcInput bundle's lines (see
    // rig/wiring.inc, dut/adapter.inc) -- the same three wires seen from the
    // instrument side.
    //
    // The neutral/ground return is deliberately not an enumerator: it is part
    // of this instrument's fixed wiring (see hal::Ac6677A's own comment on
    // why it is switched along with the phases) but it is not something a
    // voltage is programmed onto or read from, so giving it a name here would
    // put a fourth case into every switch that has only three real answers.
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
    // Note what this axis is NOT: the connection topology. Wye and delta are
    // a different question again, and one this instrument's entry-point
    // method already answers (see hal::Ac6677A::wye()). Keeping them separate
    // is why balanced-vs-per-phase is a property the builder acquires rather
    // than a second entry point -- entry points multiply across the two axes,
    // where this does not.
    //
    struct Balanced {};
    struct PerPhase {};

    //
    // A setting that may differ per phase: three values under PerPhase, one
    // under Balanced. Frequency deliberately never uses this -- see
    // Ac6677AConfig below.
    //
    template<typename Symmetry, typename QuantityT>
    using PhaseSetting = std::conditional_t<std::is_same_v<Symmetry, PerPhase>,
                                            std::optional<PerPhaseValues<QuantityT>>,
                                            std::optional<QuantityT>>;

    //
    // Symmetric with N6701AConfig (see hal/n6701a.hpp) -- no points here
    // either, and for the same reason: AcP1's three phases (and its
    // ground/neutral return -- see this header's own comment on Ac6677A)
    // are each hard-cabled to one fixed VPC pin, not routed through a mux
    // to whichever pins a script names, so there's nothing left for a
    // point argument to carry.
    //
    // Frequency is a plain scalar under both symmetries, and that is a
    // statement about the instrument rather than a simplification: the three
    // phases of one source share a fixed phase relationship, so they share a
    // frequency by construction. "Phase A at 400 Hz, phase B at 50 Hz" is not
    // an unbalanced configuration this type declines to express -- it is not
    // a configuration at all, and a field that could hold it would be a field
    // no driver could ever honour.
    //
    template<typename Symmetry>
    struct Ac6677AConfig
    {
        Ac6677A &                                           Instrument;
        PhaseSetting<Symmetry, core::quantities::Voltage>   PhaseVoltage;
        std::optional<core::quantities::Frequency>          Frequency;
        PhaseSetting<Symmetry, core::quantities::Current>   CurrentLimit;
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
        constexpr auto asPerPhase( const Ac6677AConfig<Symmetry> & config) -> Ac6677AConfig<PerPhase>
        {
            return Ac6677AConfig<PerPhase>{
                config.Instrument,
                broadcast( config.PhaseVoltage),
                config.Frequency,
                broadcast( config.CurrentLimit)
            };
        }
    } // namespace detail

    //
    // Same fluent-builder shape as N6701ABuilder -- see its own comment in
    // hal/n6701a.hpp -- with one addition: a per-phase setter returns a
    // builder of a *different* symmetry, so a chain that names phases
    // individually ends up holding an Ac6677AConfig<PerPhase> and one that
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
    class Ac6677ABuilder
    {
        public:
            using Config = Ac6677AConfig<Symmetry>;

            explicit Ac6677ABuilder( Ac6677A & instrument) :
                mConfig{ instrument, std::nullopt, std::nullopt, std::nullopt }
            {}

            // Used by the per-phase setters below to carry an already-built
            // config across the symmetry change.
            explicit Ac6677ABuilder( Config config) : mConfig( std::move( config)) {}

            //
            // All three phases alike. Under PerPhase this broadcasts rather
            // than being unavailable -- see detail::broadcast's own comment.
            //
            [[nodiscard]]
            auto phaseVoltage( const core::quantities::Voltage v) const -> Ac6677ABuilder
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
                               const PhaseValue<core::quantities::Voltage, Phase::C> c) const -> Ac6677ABuilder<PerPhase>
            {
                auto widened = detail::asPerPhase( mConfig);

                widened.PhaseVoltage = PerPhaseValues<core::quantities::Voltage>{ a.Value, b.Value, c.Value };

                return Ac6677ABuilder<PerPhase>{ widened };
            }

            //
            // One frequency, no per-phase overload -- see Ac6677AConfig's own
            // comment for why there is nothing here to overload.
            //
            [[nodiscard]]
            auto frequency( const core::quantities::Frequency f) const -> Ac6677ABuilder
            {
                auto copy = *this;
                copy.mConfig.Frequency = f;
                return copy;
            }

            [[nodiscard]]
            auto currentLimit( const core::quantities::Current c) const -> Ac6677ABuilder
            {
                auto copy = *this;
                copy.mConfig.CurrentLimit = settingOf( c);
                return copy;
            }

            [[nodiscard]]
            auto currentLimit( const PhaseValue<core::quantities::Current, Phase::A> a,
                               const PhaseValue<core::quantities::Current, Phase::B> b,
                               const PhaseValue<core::quantities::Current, Phase::C> c) const -> Ac6677ABuilder<PerPhase>
            {
                auto widened = detail::asPerPhase( mConfig);

                widened.CurrentLimit = PerPhaseValues<core::quantities::Current>{ a.Value, b.Value, c.Value };

                return Ac6677ABuilder<PerPhase>{ widened };
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
    // A programmable three-phase AC source. Modeled after the physical
    // instrument deliberately -- see hal::N6701A's own comment in
    // hal/n6701a.hpp for why.
    //
    // wye() is the entry point, named for the connection topology alone: this
    // instrument is three-phase and nothing else, so the "threePhase" this
    // method used to carry said only what the class already says. A
    // delta-connected variant would be a second method alongside it, not a
    // change to this one. Whether the three phases are driven alike is a
    // separate axis and is NOT a second entry point here -- see Balanced/
    // PerPhase above for why crossing the two would multiply methods.
    //
    // Note that phaseVoltage() keeps its prefix where wye() lost one: phase
    // voltage (line-to-neutral) and line voltage (line-to-line) differ by a
    // factor of root three on a wye source, so that word is disambiguating
    // rather than redundant.
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
    // wye() below takes no points -- connectDriver/disconnectDriver
    // close/open all four of this instrument's fixed channels together (see
    // hal::InstrumentWiring::findAll()), phases and ground alike, each its
    // own entry in the rig's wiring.inc under the same InstrumentId.
    //
    class Ac6677A : public InstrumentTag
    {
        public:
            //
            // One phase's output state, and the thing a per-phase reading is
            // actually taken from.
            //
            // It exists as a separate object rather than as three sets of
            // fields on Ac6677A because core::Port binds to a reference and
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
                            static_assert( !sizeof( QuantityT), "Ac6677A reports only its output voltage and current");
                        }
                    }

                    [[nodiscard]] auto voltage() const      -> core::quantities::Voltage                   { return mVoltage; }
                    [[nodiscard]] auto currentLimit() const -> std::optional<core::quantities::Current>    { return mCurrentLimit; }

                private:
                    friend class Ac6677A;

                    InstrumentId                              mId{};
                    core::quantities::Voltage                 mVoltage{};
                    core::quantities::Current                 mSimOutputCurrent{};
                    std::optional<core::quantities::Current>  mCurrentLimit;
                    bool                                       mEnabled{ false };
            };

            explicit Ac6677A( const InstrumentId id) : mId( id)
            {
                for( auto & phase : mPhases)
                {
                    phase.mId = id;
                }
            }

            [[nodiscard]]
            auto id() const -> InstrumentId
            {
                return mId;
            }

            [[nodiscard]]
            auto wye() -> Ac6677ABuilder<Balanced>
            {
                return Ac6677ABuilder<Balanced>{ *this };
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
            // Drop this source to a known idle state, unconditionally --
            // same contract, and the same reasoning for zeroing the
            // setpoint rather than only disabling the output, as
            // hal::N6701A::safe(); see that function's own comment.
            // mFrequency and the per-phase current limits are left as-is for
            // the same reason its mCurrentLimit is.
            //
            auto safe() -> void
            {
                mEnabled = false;

                for( auto & phase : mPhases)
                {
                    phase.mEnabled = false;
                    phase.mVoltage = core::quantities::Voltage{};
                }
            }

            // Test/simulation hooks -- real hardware has no such setters.
            auto applyOutput( const PerPhaseValues<core::quantities::Voltage> &            phaseVoltage,
                              const std::optional<core::quantities::Frequency>             frequency,
                              const std::optional<PerPhaseValues<core::quantities::Current>> & currentLimit) -> void
            {
                mFrequency = frequency;
                mEnabled   = true;

                for( std::size_t index = 0; index < mPhases.size(); ++index)
                {
                    mPhases[ index].mVoltage      = phaseVoltage[ index];
                    mPhases[ index].mCurrentLimit = currentLimit ? std::optional{ ( *currentLimit)[ index] } : std::nullopt;
                    mPhases[ index].mEnabled      = true;
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
                mEnabled = false;

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

            [[nodiscard]]
            auto frequency() const -> std::optional<core::quantities::Frequency>
            {
                return mFrequency;
            }

        private:
            InstrumentId                                 mId;
            std::array<PhaseOutput, 3>                   mPhases;
            std::optional<core::quantities::Frequency>   mFrequency;
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
    auto applyDriver( const Ac6677AConfig<Symmetry> & config) -> void
    {
        const auto perPhase = detail::asPerPhase( config);

        config.Instrument.applyOutput(
            perPhase.PhaseVoltage.value_or( PerPhaseValues<core::quantities::Voltage>{}),
            perPhase.Frequency,
            perPhase.CurrentLimit);
    }

    template<typename Symmetry>
    auto removeDriver( const Ac6677AConfig<Symmetry> & config) -> void
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
    // the same mechanism and hal/describe.hpp for the helpers. "3-phase wye" is
    // stated rather than implied: this instrument has only one connection mode
    // today (see this class's own comment on why a delta variant would be a
    // second builder method), and a log that says which one was used stays
    // correct rather than becoming ambiguous the moment a second one exists.
    //
    // The same argument now applies to the symmetry, which is why a per-phase
    // config says so and renders all three values. A log that showed only
    // "phaseVoltage=115V" for a deliberately unbalanced run would describe a
    // test that was never performed.
    //
    template<typename Symmetry>
    auto describeConfig( const Ac6677AConfig<Symmetry> & config) -> core::SourceDescription
    {
        auto settings = [&]() -> std::vector<std::string>
        {
            if constexpr( std::is_same_v<Symmetry, PerPhase>)
            {
                return {
                    "3-phase wye, per-phase",
                    detail::describePhaseSetting( "phaseVoltage", config.PhaseVoltage),
                    describeSetting( "frequency", config.Frequency),
                    detail::describePhaseSetting( "currentLimit", config.CurrentLimit)
                };
            }
            else
            {
                return {
                    "3-phase wye",
                    describeSetting( "phaseVoltage", config.PhaseVoltage),
                    describeSetting( "frequency",    config.Frequency),
                    describeSetting( "currentLimit", config.CurrentLimit)
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
    auto connectDriver( SwitchFabric & fabric, const InstrumentWiring & instrumentWiring, const ConnectorWiring &, const Ac6677AConfig<Symmetry> & config) -> void
    {
        fabric.connect( instrumentWiring.findAll( config.Instrument.id()));
    }

    template<typename Symmetry>
    auto disconnectDriver( SwitchFabric & fabric, const InstrumentWiring & instrumentWiring, const ConnectorWiring &, const Ac6677AConfig<Symmetry> & config) -> void
    {
        fabric.disconnect( instrumentWiring.findAll( config.Instrument.id()));
    }
} // namespace hal
