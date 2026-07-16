#pragma once

#include <optional>

#include "core/adapter.hpp"
#include "core/at.hpp"
#include "core/quantity.hpp"
#include "core/quantity_kind.hpp"

#include "hal/instrument.hpp"
#include "hal/switch_fabric.hpp"
#include "hal/wiring.hpp"

namespace hal
{
    class N6701A;

    //
    // What a single Apply(DcP1.dc(at(...)).voltage(...).currentLimit(...))
    // call boils down to: which instrument, which DUT point (Loc, exactly
    // like MeasureEngine's operator() in core/measure.hpp), and whichever of
    // Voltage/CurrentLimit were actually set. A bare
    // Remove(DcP1.dc(at(...))) leaves both at nullopt -- removeDriver below
    // only ever reads Instrument/Point.
    //
    // Templated directly on Loc (not nested inside N6701ABuilder<Loc>) so
    // that applyDriver/removeDriver below can deduce Loc from an ordinary
    // function-template argument: `typename N6701ABuilder<Loc>::Config`
    // would put Loc in a non-deduced context (a nested name after `::`),
    // exactly the same deduction trap ac6677a.hpp's ErasedPhasePoint
    // sidesteps a different way, because its call site rules out templating
    // at all.
    //
    template<auto Loc>
    struct N6701AConfig
    {
        N6701A &                                                  Instrument;
        core::AdapterPointTag<Loc, core::QuantityKind::Voltage>   Point;
        std::optional<core::quantities::Voltage>                  Voltage;
        std::optional<core::quantities::Current>                  CurrentLimit;
    };

    //
    // The fluent chain a script builds up before handing it to Apply/Remove
    // -- exactly the same "return *this by value, updated" shape as
    // core::Port's range()/nplc()/frequency() builders in core/port.hpp, for
    // the same reason: a bare `DcP1.dc(at(Input24V))` with no further calls
    // is still a valid (if underspecified) config.
    //
    template<auto Loc>
    class N6701ABuilder
    {
        public:
            using Config = N6701AConfig<Loc>;

            N6701ABuilder( N6701A & instrument, const core::AdapterPointTag<Loc, core::QuantityKind::Voltage> & point) :
                mConfig{ instrument, point, std::nullopt, std::nullopt }
            {}

            [[nodiscard]]
            auto voltage( const core::quantities::Voltage v) const -> N6701ABuilder
            {
                auto copy = *this;
                copy.mConfig.Voltage = v;
                return copy;
            }

            [[nodiscard]]
            auto currentLimit( const core::quantities::Current c) const -> N6701ABuilder
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
    // One channel of an Agilent/Keysight N6701A modular DC power system: the
    // mainframe takes up to 4 independent plug-in power modules, each its
    // own output, so one hal::N6701A instance models one module/channel --
    // not the mainframe as a whole. DcP1..DcP4 (see instruments.hpp) are four
    // such instances, exactly the way Dmm1/Dmm2 are two instances of
    // hal::Dmm: two distinct wiring facts sharing one C++ type.
    //
    // mChannel is the module's slot number within the shared mainframe (1-4)
    // -- a fact a real driver will need to build the right SCPI channel list
    // (e.g. "VOLT 24,(@2)"), kept here now even though nothing reads it yet,
    // so the pattern for "one shared box, several independently-addressed
    // channels" exists before the first real driver needs it. This is a
    // different axis from InstrumentId/hal::InstrumentWiring's matrix
    // channel: that's which crosspoint this module's output leads land on
    // in the switching fabric; mChannel is which slot this module occupies
    // inside the mainframe. Neither table knows about the other.
    //
    // Modeled after the physical instrument deliberately, unlike
    // hal::Dmm/hal::Oscilloscope: those two are generic enough to stand in
    // for roughly any DMM/scope with minor changes, but a real power-supply
    // driver's SCPI dialect and channel-addressing scheme is inherently tied
    // to its exact model, so naming the class after the model documents that
    // non-portability rather than pretending it isn't there.
    //
    class N6701A
    {
        public:
            N6701A( const InstrumentId id, const int channel) : mId( id), mChannel( channel) {}

            [[nodiscard]]
            auto id() const -> InstrumentId
            {
                return mId;
            }

            [[nodiscard]]
            auto channel() const -> int
            {
                return mChannel;
            }

            //
            // point's Loc is a compile-time template parameter, and the
            // parameter type only accepts a Voltage-kind AdapterPointTag --
            // wrapping a Current-tagged point in at() and passing it here
            // simply has no matching overload, the same compile-time
            // protection Measure() gets from core::MeasureEngine::operator().
            //
            template<auto Loc>
            [[nodiscard]]
            auto dc( const core::At<core::AdapterPointTag<Loc, core::QuantityKind::Voltage>> & wrapped) -> N6701ABuilder<Loc>
            {
                return N6701ABuilder<Loc>{ *this, wrapped.point };
            }

            // Test/simulation hooks -- real hardware has no such setters.
            auto applyOutput( const core::quantities::Voltage v, const std::optional<core::quantities::Current> currentLimit) -> void
            {
                mOutputVoltage = v;
                mCurrentLimit  = currentLimit;
                mEnabled       = true;
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
            auto outputVoltage() const -> core::quantities::Voltage
            {
                return mOutputVoltage;
            }

            [[nodiscard]]
            auto currentLimit() const -> std::optional<core::quantities::Current>
            {
                return mCurrentLimit;
            }

        private:
            InstrumentId                              mId;
            int                                        mChannel;
            core::quantities::Voltage                 mOutputVoltage{};
            std::optional<core::quantities::Current>  mCurrentLimit;
            bool                                       mEnabled{ false };
    };

    //
    // ADL targets for core::ApplyEngine/RemoveEngine -- see core/apply.hpp's
    // own comment on the applyDriver/removeDriver customization points.
    // Routes the fabric exactly like MeasureEngine does (see
    // core/measure.hpp), then programs -- or disables -- the instrument's
    // simulated output. Found via ADL because N6701AConfig<Loc> lives in
    // namespace hal, the same trick core/measure.hpp's to_string(instrumentId)
    // call relies on.
    //
    template<auto Loc>
    auto applyDriver( SwitchFabric & fabric, const InstrumentWiring & instrumentWiring, const ConnectorWiring & connectorWiring, const N6701AConfig<Loc> & config) -> void
    {
        const auto instrumentChannel = instrumentWiring.find( config.Instrument.id());
        const auto connectorChannel  = connectorWiring.find( Loc);

        fabric.route( { instrumentChannel, connectorChannel });

        config.Instrument.applyOutput( config.Voltage.value_or( core::quantities::Voltage{}), config.CurrentLimit);
    }

    template<auto Loc>
    auto removeDriver( SwitchFabric & fabric, const InstrumentWiring & instrumentWiring, const ConnectorWiring & connectorWiring, const N6701AConfig<Loc> & config) -> void
    {
        const auto instrumentChannel = instrumentWiring.find( config.Instrument.id());
        const auto connectorChannel  = connectorWiring.find( Loc);

        fabric.route( { instrumentChannel, connectorChannel });

        config.Instrument.removeOutput();
    }
} // namespace hal
