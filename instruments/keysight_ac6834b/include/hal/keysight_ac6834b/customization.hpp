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

#include "hal/keysight_ac6834b_ac_source.hpp"

//
// the ADL hooks the verbs and journal find
//
// Part of hal/keysight_ac6834b.hpp -- include that, not this.
//

namespace hal::keysight_ac6834b
{
    //
    // ADL targets for core::ApplyEngine/RemoveEngine -- see hal/keysight_edu36311a.hpp's
    // own comment on the same mechanism. Programs -- or disables -- the
    // instrument's simulated output only; see connectDriver/disconnectDriver
    // below for the fabric routing this used to also do.
    //
    // One template over both symmetries rather than an overload apiece: the
    // body is the same once detail::asPerPhase() has run, and hal::keysight_edu36311a::DcSource's
    // SwitchableIsolation comment gives the reason for preferring that shape
    // -- an overload per named tag means every tag carries an identical copy
    // of the same body, purely because it was named.
    //
    template<typename Symmetry>
    auto applyDriver( const AcConfig<Symmetry> & config) -> void
    {
        const auto perPhase = detail::asPerPhase( config);

        config.Instrument.applyOutput(
            perPhase.PhaseVoltage.value_or( PerPhaseValues<core::quantities::Voltage>{}),
            perPhase.Frequency,
            perPhase.CurrentLimit,
            perPhase.Range);
    }

    template<typename Symmetry>
    auto removeDriver( const AcConfig<Symmetry> & config) -> void
    {
        config.Instrument.removeOutput();
    }

    //
    // What the source was programmed to, as setpoint readbacks (VOLTage?,
    // FREQuency?, CURRent?, VOLTage:RANGe?) rather than measurements --
    // measuredVoltage() is the right question for a check and the wrong one for
    // a restore: re-applying a measured 114.7 V would program the rig to a value
    // nobody asked for, and do it again, a little further off, on every pass.
    //
    // Optionals throughout, because "never programmed" is a state the
    // instrument distinguishes and a restore has to preserve: std::nullopt means
    // leave that setting alone, which is what it means everywhere else in
    // AcConfig. Range is normally exactly this case.
    //
    struct AcSnapshot
    {
        PerPhaseValues<core::quantities::Voltage>                PhaseVoltage{};
        std::optional<core::quantities::Frequency>               Frequency{};
        std::optional<PerPhaseValues<core::quantities::Current>> CurrentLimit{};
        std::optional<PerPhaseValues<core::quantities::Voltage>> Range{};
    };

    namespace detail
    {
        //
        // One optional readback per phase, folded into one optional triple:
        // present only if all three phases are. The builder's per-phase setters
        // take all three or none, so a setting programmed on some phases and not
        // others is not something a restore could put back by halves -- and it
        // is not a state this instrument can be left in anyway (INSTrument:COUPle
        // broadcasts, see AcBuilder).
        //
        template<typename QuantityT, typename ReadbackT>
        [[nodiscard]]
        auto foldPerPhase( ReadbackT && readback) -> std::optional<PerPhaseValues<QuantityT>>
        {
            PerPhaseValues<QuantityT> values{};

            for( const auto phase : phases)
            {
                const auto value = readback( phase);

                if( ! value.has_value())
                {
                    return std::nullopt;
                }

                values[ indexOf( phase)] = *value;
            }

            return values;
        }
    } // namespace detail

    //
    // ADL targets for hal::SourceSnapshot (hal/verbs/snapshot.hpp), which holds
    // the value and decides *whether* to put it back; these two say what there
    // is to hold and how to program it again. See hal::keysight_edu36311a's pair
    // for why restoreDriver returns the builder instead of applying it.
    //
    inline auto snapshotDriver( const AcSource & source) -> AcSnapshot
    {
        using namespace core::quantities;

        AcSnapshot snapshot{};

        for( const auto phase : phases)
        {
            snapshot.PhaseVoltage[ indexOf( phase)] = source.phaseVoltage( phase);
        }

        snapshot.Frequency    = source.frequency();
        snapshot.CurrentLimit = detail::foldPerPhase<Current>(
                                    [ & ]( const Phase phase) { return source.currentLimit( phase); });
        snapshot.Range        = detail::foldPerPhase<Voltage>(
                                    [ & ]( const Phase phase) { return source.range( phase); });

        return snapshot;
    }

    inline auto portDriver( AcSource & source)
    {
        return source.ac();
    }

    //
    // Chained through named locals for the same copy-constructible-but-not-
    // assignable reason as the DC pair. Each stage is skipped where the snapshot
    // found nothing programmed, so a setting nobody touched is left alone
    // rather than given a value invented here.
    //
    // The range readback is already resolved to what the instrument selected
    // (150 V or 300 V, never the raw value someone passed), and rangeFor maps
    // each of those to itself -- so it round-trips through the setter rather
    // than being re-resolved into the other range.
    //
    inline auto restoreDriver( AcSource & source, const AcSnapshot & snapshot)
    {
        const auto voltages = source.ac().phaseVoltage(
                                  phaseA( snapshot.PhaseVoltage[ indexOf( Phase::A)]),
                                  phaseB( snapshot.PhaseVoltage[ indexOf( Phase::B)]),
                                  phaseC( snapshot.PhaseVoltage[ indexOf( Phase::C)]));

        const auto withFrequency = snapshot.Frequency
                                       ? voltages.frequency( *snapshot.Frequency)
                                       : voltages;

        const auto withCurrentLimit = snapshot.CurrentLimit
                                          ? withFrequency.currentLimit(
                                                phaseA( ( *snapshot.CurrentLimit)[ indexOf( Phase::A)]),
                                                phaseB( ( *snapshot.CurrentLimit)[ indexOf( Phase::B)]),
                                                phaseC( ( *snapshot.CurrentLimit)[ indexOf( Phase::C)]))
                                          : withFrequency;

        return snapshot.Range
                   ? withCurrentLimit.range(
                         phaseA( ( *snapshot.Range)[ indexOf( Phase::A)]),
                         phaseB( ( *snapshot.Range)[ indexOf( Phase::B)]),
                         phaseC( ( *snapshot.Range)[ indexOf( Phase::C)]))
                   : withCurrentLimit;
    }

    //
    // ADL target for the electrical interlock -- see hal::keysight_edu36311a::DcSource's own
    // isEnergised for the contract, which is identical, and core/verbs/interlock.hpp
    // for why every config with an applyDriver must have one.
    //
    // The source's own enabled flag, not a per-phase one. removeOutput() drops
    // all three phases and the neutral return together (see this class's own
    // safe()), and Connect/Disconnect close and open all four of its wiring
    // entries as one unit -- so "is a contact about to move on a live path"
    // has one answer here, not four.
    //
    template<typename Symmetry>
    auto isEnergised( const AcConfig<Symmetry> & config) -> bool
    {
        return config.Instrument.isEnabled();
    }

    namespace detail
    {
        //
        // The per-phase counterpart of hal::describeSetting (hal/driver/describe.hpp)
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
    // ADL target for the run journal -- see hal/keysight_edu36311a.hpp's describeConfig for
    // the same mechanism and hal/driver/describe.hpp for the helpers. "3-phase" is
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
    auto describeConfig( const AcConfig<Symmetry> & config) -> core::SourceDescription
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
    // hal/keysight_edu36311a.hpp's own comment on the same mechanism. Closes -- or
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
    auto connectDriver( SwitchFabric & fabric, const InstrumentWiring & instrumentWiring, const ConnectorWiring &, const AcConfig<Symmetry> & config) -> void
    {
        fabric.connect( instrumentWiring.findAll( config.Instrument.id()));
    }

    template<typename Symmetry>
    auto disconnectDriver( SwitchFabric & fabric, const InstrumentWiring & instrumentWiring, const ConnectorWiring &, const AcConfig<Symmetry> & config) -> void
    {
        fabric.disconnect( instrumentWiring.findAll( config.Instrument.id()));
    }
} // namespace hal::keysight_ac6834b
