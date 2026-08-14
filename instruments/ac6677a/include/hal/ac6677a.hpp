#pragma once

#include <optional>
#include <string>
#include <type_traits>

#include "core/apply.hpp"
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
    // Symmetric with N6701AConfig (see hal/n6701a.hpp) -- no points here
    // either, and for the same reason: AcP1's three phases (and its
    // ground/neutral return -- see this header's own comment on Ac6677A)
    // are each hard-cabled to one fixed VPC pin, not routed through a mux
    // to whichever pins a script names, so there's nothing left for a
    // point argument to carry.
    //
    struct Ac6677AConfig
    {
        Ac6677A &                                    Instrument;
        std::optional<core::quantities::Voltage>     PhaseVoltage;
        std::optional<core::quantities::Frequency>   Frequency;
        std::optional<core::quantities::Current>     CurrentLimit;
    };

    //
    // Same fluent-builder shape as N6701ABuilder -- see its own comment in
    // hal/n6701a.hpp.
    //
    class Ac6677ABuilder
    {
        public:
            using Config = Ac6677AConfig;

            explicit Ac6677ABuilder( Ac6677A & instrument) :
                mConfig{ instrument, std::nullopt, std::nullopt, std::nullopt }
            {}

            [[nodiscard]]
            auto phaseVoltage( const core::quantities::Voltage v) const -> Ac6677ABuilder
            {
                auto copy = *this;
                copy.mConfig.PhaseVoltage = v;
                return copy;
            }

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
                copy.mConfig.CurrentLimit = c;
                return copy;
            }

            [[nodiscard]]
            auto config() const -> const Config &
            {
                return mConfig;
            }

        private:
            Config mConfig;
    };

    //
    // A programmable three-phase AC source. Only .threePhaseWye() exists
    // today -- a delta-connected variant would be a second builder method,
    // not a change to this one, if/when it's ever needed. Modeled after the
    // physical instrument deliberately -- see hal::N6701A's own comment in
    // hal/n6701a.hpp for why.
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
    // threePhaseWye() below takes no points -- connectDriver/
    // disconnectDriver close/open all four of this instrument's fixed
    // channels together (see hal::InstrumentWiring::findAll()), phases and
    // ground alike, each its own entry in the rig's wiring.inc under the
    // same InstrumentId.
    //
    class Ac6677A : public InstrumentTag
    {
        public:
            explicit Ac6677A( const InstrumentId id) : mId( id) {}

            [[nodiscard]]
            auto id() const -> InstrumentId
            {
                return mId;
            }

            [[nodiscard]]
            auto threePhaseWye() -> Ac6677ABuilder
            {
                return Ac6677ABuilder{ *this };
            }

            //
            // What this source reports about its own output, over its own
            // interface -- see hal::N6701A::measuredVoltage() for the reasoning,
            // which is the same here and matters more: 115 V at 5 A is not a
            // signal, so there is no version of this reading that travels
            // through the switching matrix.
            //
            // Per phase, not summed: measuredVoltage() is the phase voltage
            // (matching the phaseVoltage() setpoint), and measuredCurrent() the
            // per-phase current. A three-phase total would be a different
            // quantity with a different name, not this one under-specified.
            //
            [[nodiscard]]
            auto measuredVoltage() -> core::Port<core::quantities::Voltage, Ac6677A>
            {
                return core::Port<core::quantities::Voltage, Ac6677A>{ *this };
            }

            [[nodiscard]]
            auto measuredCurrent() -> core::Port<core::quantities::Current, Ac6677A>
            {
                return core::Port<core::quantities::Current, Ac6677A>{ *this };
            }

            template<core::quantities::QuantityType QuantityT>
            [[nodiscard]]
            auto rawMeasure( const core::MeasureSetup<QuantityT> &) -> QuantityT
            {
                if constexpr( std::is_same_v<QuantityT, core::quantities::Voltage>)
                {
                    return mEnabled ? mPhaseVoltage : core::quantities::Voltage{};
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

            //
            // Drop this source to a known idle state, unconditionally --
            // same contract, and the same reasoning for zeroing the
            // setpoint rather than only disabling the output, as
            // hal::N6701A::safe(); see that function's own comment.
            // mFrequency/mCurrentLimit are left as-is for the same reason
            // its mCurrentLimit is.
            //
            auto safe() -> void
            {
                mEnabled      = false;
                mPhaseVoltage = core::quantities::Voltage{};
            }

            // Test/simulation hooks -- real hardware has no such setters.
            auto applyOutput( const core::quantities::Voltage phaseVoltage, const std::optional<core::quantities::Frequency> frequency, const std::optional<core::quantities::Current> currentLimit) -> void
            {
                mPhaseVoltage = phaseVoltage;
                mFrequency    = frequency;
                mCurrentLimit = currentLimit;
                mEnabled      = true;
            }

            auto removeOutput() -> void
            {
                mEnabled = false;
            }

            // Per-phase current the simulated source is delivering -- what a
            // real instrument reports back, which no setpoint determines.
            auto setSimulatedOutputCurrent( const core::quantities::Current c) -> void
            {
                mSimOutputCurrent = c;
            }

            [[nodiscard]]
            auto isEnabled() const -> bool
            {
                return mEnabled;
            }

            [[nodiscard]]
            auto phaseVoltage() const -> core::quantities::Voltage
            {
                return mPhaseVoltage;
            }

            [[nodiscard]]
            auto frequency() const -> std::optional<core::quantities::Frequency>
            {
                return mFrequency;
            }

            [[nodiscard]]
            auto currentLimit() const -> std::optional<core::quantities::Current>
            {
                return mCurrentLimit;
            }

        private:
            InstrumentId                                 mId;
            core::quantities::Voltage                    mPhaseVoltage{};
            core::quantities::Current                    mSimOutputCurrent{};
            std::optional<core::quantities::Frequency>   mFrequency;
            std::optional<core::quantities::Current>     mCurrentLimit;
            bool                                          mEnabled{ false };
    };

    //
    // ADL targets for core::ApplyEngine/RemoveEngine -- see hal/n6701a.hpp's
    // own comment on the same mechanism. Programs -- or disables -- the
    // instrument's simulated output only; see connectDriver/disconnectDriver
    // below for the fabric routing this used to also do.
    //
    inline auto applyDriver( const Ac6677AConfig & config) -> void
    {
        config.Instrument.applyOutput( config.PhaseVoltage.value_or( core::quantities::Voltage{}), config.Frequency, config.CurrentLimit);
    }

    inline auto removeDriver( const Ac6677AConfig & config) -> void
    {
        config.Instrument.removeOutput();
    }

    //
    // ADL target for the run journal -- see hal/n6701a.hpp's describeConfig for
    // the same mechanism and hal/describe.hpp for the helpers. "3-phase wye" is
    // stated rather than implied: this instrument has only one connection mode
    // today (see this class's own comment on why a delta variant would be a
    // second builder method), and a log that says which one was used stays
    // correct rather than becoming ambiguous the moment a second one exists.
    //
    inline auto describeConfig( const Ac6677AConfig & config) -> core::SourceDescription
    {
        return core::SourceDescription{
            std::string( to_string( config.Instrument.id())),
            describeSettings( {
                "3-phase wye",
                describeSetting( "phaseVoltage", config.PhaseVoltage),
                describeSetting( "frequency",    config.Frequency),
                describeSetting( "currentLimit", config.CurrentLimit)
            })
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
    inline auto connectDriver( SwitchFabric & fabric, const InstrumentWiring & instrumentWiring, const ConnectorWiring &, const Ac6677AConfig & config) -> void
    {
        fabric.connect( instrumentWiring.findAll( config.Instrument.id()));
    }

    inline auto disconnectDriver( SwitchFabric & fabric, const InstrumentWiring & instrumentWiring, const ConnectorWiring &, const Ac6677AConfig & config) -> void
    {
        fabric.disconnect( instrumentWiring.findAll( config.Instrument.id()));
    }
} // namespace hal
