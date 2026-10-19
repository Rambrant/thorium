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

//
// AcConfig + AcBuilder
//
// Part of hal/keysight_ac6834b.hpp -- include that, not this.
//

namespace hal::keysight_ac6834b
{
    //
    // Symmetric with N6701AConfig (see hal/keysight_edu36311a.hpp) -- no points here
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
    struct AcConfig
    {
        AcSource &                                           Instrument;
        PhaseSetting<Symmetry, core::quantities::Voltage>   PhaseVoltage{};
        std::optional<core::quantities::Frequency>          Frequency{};
        PhaseSetting<Symmetry, core::quantities::Current>   CurrentLimit{};

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
        PhaseSetting<Symmetry, core::quantities::Voltage>   Range{};
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
        constexpr auto asPerPhase( const AcConfig<Symmetry> & config) -> AcConfig<PerPhase>
        {
            return AcConfig<PerPhase>{
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
    // hal/keysight_edu36311a.hpp -- with one addition: a per-phase setter returns a
    // builder of a *different* symmetry, so a chain that names phases
    // individually ends up holding an AcConfig<PerPhase> and one that
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
    class AcBuilder : public ConfigBuilder<AcBuilder<Symmetry>, AcConfig<Symmetry>>
    {
        public:
            using Config = AcConfig<Symmetry>;

            explicit AcBuilder( AcSource & instrument) :
                AcBuilder::ConfigBuilder( Config{ instrument })
            {}

            // Used by the per-phase setters below to carry an already-built
            // config across the symmetry change.
            explicit AcBuilder( Config config) :
                AcBuilder::ConfigBuilder( std::move( config))
            {}

            //
            // All three phases alike. Under PerPhase this broadcasts rather
            // than being unavailable -- see detail::broadcast's own comment.
            //
            [[nodiscard]]
            auto phaseVoltage( const core::quantities::Voltage v) const
            {
                return this->with( &Config::PhaseVoltage, settingOf( v));
            }

            //
            // Each phase its own voltage -- and the point of the whole
            // symmetry split. Returns a per-phase builder whatever this one
            // was.
            //
            [[nodiscard]]
            auto phaseVoltage( const PhaseValue<core::quantities::Voltage, Phase::A> a,
                               const PhaseValue<core::quantities::Voltage, Phase::B> b,
                               const PhaseValue<core::quantities::Voltage, Phase::C> c) const -> AcBuilder<PerPhase>
            {
                auto widened = detail::asPerPhase( this->mConfig);

                widened.PhaseVoltage = PerPhaseValues<core::quantities::Voltage>{ a.Value, b.Value, c.Value };

                return AcBuilder<PerPhase>{ widened };
            }

            //
            // One frequency, no per-phase overload -- see AcConfig's own
            // comment for why there is nothing here to overload.
            //
            [[nodiscard]]
            auto frequency( const core::quantities::Frequency f) const
            {
                return this->with( &Config::Frequency, f);
            }

            [[nodiscard]]
            auto currentLimit( const core::quantities::Current c) const
            {
                return this->with( &Config::CurrentLimit, settingOf( c));
            }

            [[nodiscard]]
            auto currentLimit( const PhaseValue<core::quantities::Current, Phase::A> a,
                               const PhaseValue<core::quantities::Current, Phase::B> b,
                               const PhaseValue<core::quantities::Current, Phase::C> c) const -> AcBuilder<PerPhase>
            {
                auto widened = detail::asPerPhase( this->mConfig);

                widened.CurrentLimit = PerPhaseValues<core::quantities::Current>{ a.Value, b.Value, c.Value };

                return AcBuilder<PerPhase>{ widened };
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
            auto range( const core::quantities::Voltage accommodating) const
            {
                return this->with( &Config::Range, settingOf( rangeFor( accommodating)));
            }

            [[nodiscard]]
            auto range( const PhaseValue<core::quantities::Voltage, Phase::A> a,
                        const PhaseValue<core::quantities::Voltage, Phase::B> b,
                        const PhaseValue<core::quantities::Voltage, Phase::C> c) const -> AcBuilder<PerPhase>
            {
                auto widened = detail::asPerPhase( this->mConfig);

                widened.Range = PerPhaseValues<core::quantities::Voltage>{
                    rangeFor( a.Value), rangeFor( b.Value), rangeFor( c.Value) };

                return AcBuilder<PerPhase>{ widened };
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
    };
} // namespace hal::keysight_ac6834b
