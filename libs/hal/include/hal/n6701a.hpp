#pragma once

#include <optional>

#include "core/quantity.hpp"

#include "hal/instrument.hpp"
#include "hal/switch_fabric.hpp"
#include "hal/wiring.hpp"

namespace hal
{
    template<typename Isolation> class N6701A;

    //
    // Whether a given DC rail has a real isolation relay in its path at
    // all, or is wired straight to its VPC pin with nothing to switch --
    // see hal::N6701A's own comment for the real bench fact this models.
    // Tag types, not an enum: the point isn't a runtime flag N6701A checks
    // before deciding whether to expose Connect/Disconnect -- it's whether
    // connectDriver/disconnectDriver's shared constraint below is
    // satisfied. A DirectWiring supply's config type simply doesn't
    // satisfy SwitchableIsolation -- Connect(DcP1.dc()) is a compile
    // error, "no matching function", not a runtime throw the way a
    // missing hal::InstrumentWiring entry used to be.
    //
    // HasRelay is a property each tag declares about itself, not a type
    // connectDriver/disconnectDriver enumerate by name -- see
    // SwitchableIsolation below. A third tag (say, a future isolation
    // mechanism with its own name) that sets HasRelay = true gets
    // Connect/Disconnect for free, no new overload to write; one that
    // sets it false doesn't, also for free. The alternative -- one
    // connectDriver overload written out per relay-having tag -- would
    // mean every such tag carries an identical copy of the same one-line
    // body, purely because it was named rather than because its
    // implementation differs.
    //
    struct DirectWiring  { static constexpr bool HasRelay = false; };  // no relay -- hard-wired, nothing to Connect/Disconnect
    struct RelayIsolated { static constexpr bool HasRelay = true;  };  // has one -- Connect/Disconnect close/open it

    template<typename Isolation>
    concept SwitchableIsolation = Isolation::HasRelay;

    //
    // What a single Apply(DcP1.dc().voltage(...).currentLimit(...)) call
    // boils down to: which instrument, and whichever of Voltage/
    // CurrentLimit were actually set. A bare Remove(DcP1.dc()) leaves both
    // at nullopt -- removeDriver below only ever reads Instrument.
    //
    // No Loc/AdapterPointTag here -- DcP1's output is hard-wired straight
    // to one specific VPC pin for safety reasons (see hal::N6701A's own
    // comment), not routed through the switching fabric to whichever pin a
    // script names, so there was never a second point for at() to choose
    // between. That fixed pin is purely a hal::wiring.inc fact
    // (InstrumentWiring's one fixed channel, for a RelayIsolated instance
    // -- see connectDriver below), never something a script or this config
    // carries.
    //
    template<typename Isolation>
    struct N6701AConfig
    {
        N6701A<Isolation> &                       Instrument;
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
    template<typename Isolation>
    class N6701ABuilder
    {
        public:
            using Config = N6701AConfig<Isolation>;

            explicit N6701ABuilder( N6701A<Isolation> & instrument) :
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
    // not the mainframe as a whole. DcP1..DcP4 (see instrument.inc) are four
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
    // output. dc() below takes no point at all -- if there's a relay in
    // this instrument's path at all (RelayIsolated), it's exactly one --
    // its own matrix channel, see connectDriver -- not the instrument-
    // channel-plus-connector-channel pair a routed instrument like
    // hal::DSO8064 has. Which DUT point that fixed channel corresponds to
    // (e.g. "Output5V") is a fact the DUT adapter documents about itself
    // (see e.g. libs/dut/device_x_profile.inc), never something hal::
    // names -- this header has no idea DcP1 has anything to do with any
    // point called Output5V, only that it may or may not have one fixed
    // relay.
    //
    // Not every DC rail actually has that relay, though -- on the real
    // bench this modeled after, some supplies are wired straight through
    // with no isolation relay at all, and others have one built in. Real
    // consequence, not a detail to paper over: Connect/Disconnect on a
    // relay-less supply have nothing to do -- there is no hardware state
    // for them to change. Isolation (DirectWiring/RelayIsolated above) is
    // what tells them apart, as a template parameter rather than a runtime
    // flag N6701A itself would have to check: connectDriver/
    // disconnectDriver (see the bottom of this file) are constrained by
    // SwitchableIsolation, a concept checking Isolation::HasRelay, so
    // Connect(DcP1.dc()) on a DirectWiring instance is a compile error --
    // the same "misspelled/wrong identifier fails to compile" guarantee
    // this project applies everywhere else -- not a hal::InstrumentWiring
    // lookup that happens to throw at runtime because nobody wired a
    // channel for it.
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
    template<typename Isolation>
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
            auto dc() -> N6701ABuilder<Isolation>
            {
                return N6701ABuilder<Isolation>{ *this };
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
    // hal/instrument.inc names instruments by these aliases, not by
    // N6701A<...> directly -- partly readability, partly mechanical: the
    // INSTRUMENT(type, name, id, ...) macro in hal/active_instruments.hpp
    // splits its arguments on top-level commas, and N6701A<RelayIsolated>
    // would be split at the angle brackets' own comma-free content just
    // fine here, but a bare comma inside a template argument list (as a
    // future Isolation-like parameter might need) would not be. Plain
    // identifiers sidestep the question entirely.
    //
    using N6701ADirect = N6701A<DirectWiring>;
    using N6701ARelay  = N6701A<RelayIsolated>;

    //
    // ADL targets for core::ApplyEngine/RemoveEngine -- see core/apply.hpp's
    // own comment on the applyDriver/removeDriver customization points.
    // Programs -- or disables -- the instrument's simulated output only;
    // the fabric path is a separate concern, see connectDriver/
    // disconnectDriver below. Defined for both Isolation kinds identically
    // -- programming a supply's output doesn't care whether it has a
    // relay in its path. Found via ADL because N6701AConfig lives in
    // namespace hal, the same trick core/measure.hpp's
    // to_string(instrumentId) call relies on.
    //
    template<typename Isolation>
    auto applyDriver( const N6701AConfig<Isolation> & config) -> void
    {
        config.Instrument.applyOutput( config.Voltage.value_or( core::quantities::Voltage{}), config.CurrentLimit);
    }

    template<typename Isolation>
    auto removeDriver( const N6701AConfig<Isolation> & config) -> void
    {
        config.Instrument.removeOutput();
    }

    //
    // ADL targets for core::ConnectEngine/DisconnectEngine -- see
    // core/apply.hpp's own comment on the connectDriver/disconnectDriver
    // customization points. Closes -- or opens -- every fixed path
    // registered for this instrument, together (see
    // hal::InstrumentWiring::findAll() and hal::WireRole's own comment on
    // why force and any sense leads are meant to move as one unit here,
    // unlike a DMM's per-measurement sense choice). For most N6701A
    // instances that's just the one force channel -- findAll() over a
    // single entry behaves identically to find() -- but if this rig ever
    // wires remote-sense leads for a given DcP instance (WIRE_INSTRUMENT_SENSE
    // in hal/wiring.inc), they close and open right along with it, no
    // driver change needed. No connector-side hop: there is no connector
    // path to look up any more (see N6701AConfig's own comment), so
    // connectorWiring is accepted (for signature symmetry with every other
    // instrument's connectDriver/disconnectDriver, all called through the
    // same core::ConnectEngine/DisconnectEngine) but never consulted here.
    //
    // One generic template, constrained by SwitchableIsolation, rather
    // than one overload per relay-having tag -- see that concept's own
    // comment for why. DirectWiring doesn't satisfy the constraint, so
    // there is no version of this function template that instantiates
    // for it at all: Connect(DcP1.dc()) fails overload resolution outright,
    // the same "misspelled/wrong identifier fails to compile" guarantee
    // this project applies everywhere else.
    //
    template<typename Isolation>
        requires SwitchableIsolation<Isolation>
    auto connectDriver( SwitchFabric & fabric, const InstrumentWiring & instrumentWiring, const ConnectorWiring &, const N6701AConfig<Isolation> & config) -> void
    {
        fabric.connect( instrumentWiring.findAll( config.Instrument.id()));
    }

    template<typename Isolation>
        requires SwitchableIsolation<Isolation>
    auto disconnectDriver( SwitchFabric & fabric, const InstrumentWiring & instrumentWiring, const ConnectorWiring &, const N6701AConfig<Isolation> & config) -> void
    {
        fabric.disconnect( instrumentWiring.findAll( config.Instrument.id()));
    }
} // namespace hal
