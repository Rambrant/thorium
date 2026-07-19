#pragma once

#include <optional>

#include "core/quantity.hpp"

#include "hal/instrument.hpp"
#include "hal/switch_fabric.hpp"
#include "hal/wiring.hpp"

namespace hal
{
    class N6701A;

    //
    // What a single Apply(DcP1.dc().voltage(...).currentLimit(...)) call
    // boils down to: which instrument, and whichever of Voltage/
    // CurrentLimit were actually set. A bare Remove(DcP1.dc()) leaves both
    // at nullopt -- removeDriver below only ever reads Instrument.
    //
    // No Loc/AdapterPointTag here any more -- DcP1's output is hard-wired
    // straight to one specific VPC pin for safety reasons (see this
    // class's own comment), not routed through the switching fabric to
    // whichever pin a script names, so there was never a second point for
    // at() to choose between. That fixed pin is purely a hal::wiring.inc
    // fact (InstrumentWiring's one fixed channel for DcP1 -- see
    // connectDriver below), not something a script or this config carries.
    //
    struct N6701AConfig
    {
        N6701A &                                  Instrument;
        std::optional<core::quantities::Voltage>  Voltage;
        std::optional<core::quantities::Current>  CurrentLimit;
    };

    //
    // The fluent chain a script builds up before handing it to Apply/Remove
    // -- exactly the same "return *this by value, updated" shape as
    // core::Port's range()/nplc()/frequency() builders in core/port.hpp, for
    // the same reason: a bare `DcP1.dc()` with no further calls is still a
    // valid (if underspecified) config.
    //
    class N6701ABuilder
    {
        public:
            using Config = N6701AConfig;

            explicit N6701ABuilder( N6701A & instrument) :
                mConfig{ instrument, std::nullopt, std::nullopt }
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
    // Fixed-wired, on purpose: a real DC rail is hard-cabled straight to one
    // VPC pin rather than routed through a mux to whichever pin a script
    // picks, for the same safety reason a matrix wouldn't be trusted to
    // carry real load current or pick the wrong destination for a supply
    // output. dc() below takes no point at all -- there is exactly one
    // relay in this instrument's whole path (its own matrix channel, see
    // connectDriver), not the instrument-channel-plus-connector-channel
    // pair a routed instrument like hal::DSO8064 has. Which DUT point that
    // fixed channel corresponds to (e.g. "Output5V") is a fact the DUT
    // adapter documents about itself (see e.g. libs/dut/device_x_profile.inc),
    // never something hal:: names -- this header has no idea DcP1 has
    // anything to do with any point called Output5V, only that it has one
    // fixed relay.
    //
    // Modeled after the physical instrument deliberately, unlike the old
    // generic hal::Dmm/hal::Oscilloscope placeholders (both since retired,
    // by hal::L4411A and hal::DSO8064 respectively): those two were generic
    // enough to stand in for roughly any DMM/scope with minor changes, but a
    // real power-supply driver's SCPI dialect and channel-addressing scheme
    // is inherently tied to its exact model, so naming the class after the
    // model documents that non-portability rather than pretending it isn't
    // there.
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
            // No point argument -- see this class's own comment for why.
            //
            [[nodiscard]]
            auto dc() -> N6701ABuilder
            {
                return N6701ABuilder{ *this };
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
    // Programs -- or disables -- the instrument's simulated output only;
    // the fabric path is a separate concern now, see connectDriver/
    // disconnectDriver below. Found via ADL because N6701AConfig lives in
    // namespace hal, the same trick core/measure.hpp's
    // to_string(instrumentId) call relies on.
    //
    inline auto applyDriver( const N6701AConfig & config) -> void
    {
        config.Instrument.applyOutput( config.Voltage.value_or( core::quantities::Voltage{}), config.CurrentLimit);
    }

    inline auto removeDriver( const N6701AConfig & config) -> void
    {
        config.Instrument.removeOutput();
    }

    //
    // ADL targets for core::ConnectEngine/DisconnectEngine -- see
    // core/apply.hpp's own comment on the connectDriver/disconnectDriver
    // customization points. Closes -- or opens -- exactly this instrument's
    // one fixed matrix channel. One hop, not two: there is no connector
    // channel to look up any more (see N6701AConfig's own comment), so
    // connectorWiring is accepted (for signature symmetry with every other
    // instrument's connectDriver/disconnectDriver, all called through the
    // same core::ConnectEngine/DisconnectEngine) but never consulted here.
    //
    inline auto connectDriver( SwitchFabric & fabric, const InstrumentWiring & instrumentWiring, const ConnectorWiring &, const N6701AConfig & config) -> void
    {
        fabric.connect( { instrumentWiring.find( config.Instrument.id()) });
    }

    inline auto disconnectDriver( SwitchFabric & fabric, const InstrumentWiring & instrumentWiring, const ConnectorWiring &, const N6701AConfig & config) -> void
    {
        fabric.disconnect( { instrumentWiring.find( config.Instrument.id()) });
    }
} // namespace hal
