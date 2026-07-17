#pragma once

#include <optional>
#include <string_view>

#include "core/adapter.hpp"
#include "core/at.hpp"
#include "core/quantity.hpp"
#include "core/quantity_kind.hpp"

#include "hal/instrument.hpp"
#include "hal/switch_fabric.hpp"
#include "hal/vpc_location.hpp"
#include "hal/wiring.hpp"

namespace hal
{
    class Ac6677A;
    class Ac6677ABuilder;

    //
    // A single phase point, its Loc erased to a runtime VpcLocation. This is
    // what makes ThreePhaseWyePoints below a plain, non-templated aggregate:
    // a `template<auto LocA, auto LocB, auto LocC> ThreePhaseWyePoints`
    // version was tried first and abandoned, because constructing one via
    // designated-initializer aggregate init -- `{ .a=phase(at(A)), ... }` --
    // is not a deduction context for a class template's non-type parameters;
    // every one of A/B/C would come back undeduced. Losing Loc here costs
    // nothing: the connector lookup in applyDriver below only ever needs it
    // as a runtime value anyway, same as InstrumentWiring::find(InstrumentId)
    // already does for the instrument side.
    //
    struct ErasedPhasePoint
    {
        VpcLocation      Location;
        std::string_view Name;
    };

    //
    // The one place the compile-time Kind == Voltage check happens: phase()
    // only accepts an At<AdapterPointTag<Loc, Voltage>>, so wrapping a
    // Current-tagged point here (`phase(at(SomeCurrentPoint))`) has no
    // matching overload -- a hard compile error at this exact call site,
    // before the point ever gets the chance to erase down to a bare
    // VpcLocation.
    //
    template<auto Loc>
    [[nodiscard]]
    constexpr auto phase( const core::At<core::AdapterPointTag<Loc, core::QuantityKind::Voltage>> & wrapped) -> ErasedPhasePoint
    {
        return ErasedPhasePoint{ Loc, wrapped.point.Name };
    }

    //
    // The three phase points of a wye connection -- deliberately just A/B/C,
    // no neutral: the rig's neutral is hard-wired to ground rather than
    // routed through the switching fabric, so there's nothing for Apply/
    // Remove to route or for this instrument to source. AcInput_N still
    // exists as an ordinary DUT adapter point (see
    // libs/dut/device_x_profile.inc) for continuity/diagnostic Measure(...)
    // calls -- it's just never part of a ThreePhaseWyePoints.
    //
    struct ThreePhaseWyePoints
    {
        ErasedPhasePoint a;
        ErasedPhasePoint b;
        ErasedPhasePoint c;
    };

    //
    // Symmetric with N6701AConfig (see hal/n6701a.hpp), but not templated:
    // ThreePhaseWyePoints already erased every Loc, so there is nothing left
    // for a template parameter to carry, and applyDriver/removeDriver below
    // need no deduction trick at all.
    //
    struct Ac6677AConfig
    {
        Ac6677A &                                       Instrument;
        ThreePhaseWyePoints                            Points;
        std::optional<core::quantities::Voltage>       PhaseVoltage;
        std::optional<core::quantities::Frequency>     Frequency;
        std::optional<core::quantities::Current>       CurrentLimit;
    };

    //
    // Same fluent-builder shape as N6701ABuilder -- see its own comment in
    // hal/n6701a.hpp.
    //
    class Ac6677ABuilder
    {
        public:
            using Config = Ac6677AConfig;

            Ac6677ABuilder( Ac6677A & instrument, const ThreePhaseWyePoints & points) :
                mConfig{ instrument, points, std::nullopt, std::nullopt, std::nullopt }
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
    // A programmable three-phase AC source. Only .threePhaseWye(...) exists
    // today -- a delta-connected variant would be a second builder method,
    // not a change to this one, if/when it's ever needed. Modeled after the
    // physical instrument deliberately -- see hal::N6701A's own comment in
    // hal/n6701a.hpp for why.
    //
    class Ac6677A
    {
        public:
            explicit Ac6677A( const InstrumentId id) : mId( id) {}

            [[nodiscard]]
            auto id() const -> InstrumentId
            {
                return mId;
            }

            [[nodiscard]]
            auto threePhaseWye( const ThreePhaseWyePoints & points) -> Ac6677ABuilder
            {
                return Ac6677ABuilder{ *this, points };
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
    // ADL targets for core::ConnectEngine/DisconnectEngine -- see
    // hal/n6701a.hpp's own comment on the same mechanism. Closes -- or
    // opens -- the instrument channel plus the three phase channels
    // together as one path (no neutral -- see ThreePhaseWyePoints's own
    // comment), additively (hal::SwitchFabric::connect()/disconnect()) so
    // it doesn't disturb whatever else is currently routed.
    //
    inline auto connectDriver( SwitchFabric & fabric, const InstrumentWiring & instrumentWiring, const ConnectorWiring & connectorWiring, const Ac6677AConfig & config) -> void
    {
        const auto instrumentChannel = instrumentWiring.find( config.Instrument.id());

        fabric.connect( {
            instrumentChannel,
            connectorWiring.find( config.Points.a.Location),
            connectorWiring.find( config.Points.b.Location),
            connectorWiring.find( config.Points.c.Location)
        });
    }

    inline auto disconnectDriver( SwitchFabric & fabric, const InstrumentWiring & instrumentWiring, const ConnectorWiring & connectorWiring, const Ac6677AConfig & config) -> void
    {
        const auto instrumentChannel = instrumentWiring.find( config.Instrument.id());

        fabric.disconnect( {
            instrumentChannel,
            connectorWiring.find( config.Points.a.Location),
            connectorWiring.find( config.Points.b.Location),
            connectorWiring.find( config.Points.c.Location)
        });
    }
} // namespace hal
