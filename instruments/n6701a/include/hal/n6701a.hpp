#pragma once

#include <optional>
#include <string>
#include <type_traits>

#include "core/describe.hpp"
#include "core/port.hpp"
#include "core/quantity.hpp"

#include "hal/address.hpp"
#include "hal/api_version.hpp"
#include "hal/describe.hpp"
#include "hal/instrument.hpp"
#include "hal/switch_fabric.hpp"
#include "hal/wiring.hpp"

//
// Which hal API version this driver was written against. A driver and the hal
// it is compiled against are distributed separately -- this directory is meant
// to be zipped and dropped into another rig's instruments/ as it stands (see
// instruments/README.md) -- so the two can disagree, and this line is what
// makes that disagreement one readable diagnostic instead of a failure deep
// inside a template instantiation. A literal, never THORIUM_HAL_API_VERSION
// itself, which would only assert that this hal matches this hal. See
// hal/api_version.hpp for what the number means and when it moves.
//
THORIUM_REQUIRE_HAL_API( 1);

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
    // different axis from InstrumentId/hal::InstrumentWiring's fabric
    // channel: that's which relay this module's output lead passes through
    // in the switching fabric; mChannel is which slot this module occupies
    // inside the mainframe. Neither table knows about the other.
    //
    // Fixed-wired, on purpose: a real DC rail is hard-cabled straight to one
    // VPC pin rather than routed through a mux to whichever pin a script
    // picks, for the same safety reason a signal matrix wouldn't be trusted
    // to carry real load current or pick the wrong destination for a supply
    // output. dc() below takes no point at all -- if there's a relay in
    // this instrument's path at all (RelayIsolated), it's exactly one --
    // its own isolation relay, see connectDriver -- not the instrument-
    // channel-plus-connector-channel pair a routed instrument like
    // hal::DSO8064A has. Which DUT point that fixed channel corresponds to
    // (e.g. "Output5V") is a fact the DUT adapter documents about itself
    // (see e.g. dut/adapter.inc), never something hal::
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
    // by hal::L4411A and hal::DSO8064A respectively): those two were generic
    // enough to stand in for roughly any DMM/scope with minor changes, but a
    // real power-supply driver's SCPI dialect and channel-addressing scheme
    // is inherently tied to its exact model, so naming the class after the
    // model documents that non-portability rather than pretending it isn't
    // there.
    //
    template<typename Isolation>
    class N6701A : public InstrumentTag
    {
        public:
            //
            // GPIB, LAN or USB, and one address for the whole mainframe: the
            // four modules DcP1..DcP4 model are four endpoints behind ONE
            // interface, so all four rows in a rig's instrument.inc carry the
            // same address and differ in the slot argument below (see
            // hal::ReachableOver in hal/address.hpp, and rig/
            // active_instruments.hpp for why the slot is a separate argument
            // rather than a field on the address).
            //
            template<typename AddressT>
                requires ReachableOver<AddressT, Gpib, Lan, Usb>
            N6701A( const InstrumentId id, const AddressT address, const int channel) : mId( id), mAddress( address), mChannel( channel) {}

            // Where the PC reaches this mainframe -- see hal/address.hpp.
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

            //
            // What this supply reports about its own output, over its own
            // interface -- not a routed measurement.
            //
            // A real N6701A measures the voltage it is holding and the current
            // it is delivering; that is how rail current is read on this rig,
            // since the signal switching carries signals and a rail at several
            // amps is not a signal (see this class's own comment on why the
            // output is hard-wired). There is no at(...) at the call site and
            // the fabric is never touched -- see core::MeasureEngine's
            // point-free operator().
            //
            //     Apply( DcP1.dc().voltage( 24_V).currentLimit( 7_A));
            //     const auto inrush = Measure( DcP1.measuredCurrent());
            //
            // Named "measured..." rather than voltage()/current() deliberately:
            // this instrument has both a setpoint and a reading for each of
            // those quantities (dc().voltage( 24_V) sets, measuredVoltage()
            // reads), and on a supply that distinction is worth spelling out at
            // every call site rather than leaving to be inferred from which
            // verb it was passed to.
            //
            [[nodiscard]]
            auto measuredVoltage() -> core::Port<core::quantities::Voltage, N6701A>
            {
                return core::Port<core::quantities::Voltage, N6701A>{ *this };
            }

            [[nodiscard]]
            auto measuredCurrent() -> core::Port<core::quantities::Current, N6701A>
            {
                return core::Port<core::quantities::Current, N6701A>{ *this };
            }

            //
            // The read core::Port performs. A disabled output reads zero for
            // both quantities rather than reporting the last setpoint: that is
            // what the instrument actually does, and it is the reading a script
            // checking "is this rail really off" depends on.
            //
            template<core::quantities::QuantityType QuantityT>
            [[nodiscard]]
            auto rawMeasure( const core::MeasureSetup<QuantityT> &) -> QuantityT
            {
                if constexpr( std::is_same_v<QuantityT, core::quantities::Voltage>)
                {
                    return mEnabled ? mOutputVoltage : core::quantities::Voltage{};
                }
                else if constexpr( std::is_same_v<QuantityT, core::quantities::Current>)
                {
                    return mEnabled ? mSimOutputCurrent : core::quantities::Current{};
                }
                else
                {
                    static_assert( !sizeof( QuantityT), "N6701A reports only its output voltage and current");
                }
            }

            //
            // Drop this supply to a known idle state, unconditionally --
            // see hal::safeRig() in hal/safing.hpp for who calls this and
            // why it takes no arguments and reads no state. Not
            // Remove(DcP1.dc()) under another name: Remove is a test-script
            // step, addressed through a config and a builder chain, and
            // reaching it requires knowing which supply a script was in the
            // middle of driving. safe() is the opposite -- it is called
            // when nobody knows what was running, so there is nothing to
            // consult and nothing that can be too late to matter.
            //
            // Zeroes the programmed setpoint as well as disabling the
            // output, deliberately. On the real instrument OUTP OFF leaves
            // the voltage setpoint where a test left it, so a supply safed
            // at 24 V comes back at 24 V the instant anything enables the
            // output again -- a front-panel press, a half-initialised
            // driver, a reconnecting console. Safing is meant to survive
            // exactly that kind of unattended re-enable, so it clears the
            // setpoint too.
            //
            // mCurrentLimit is deliberately left as-is rather than
            // cleared: with the output off and the setpoint at zero it has
            // nothing to limit, and an accidental re-enable is safer
            // finding a stale limit still in place than finding none at
            // all.
            //
            auto safe() -> void
            {
                mEnabled       = false;
                mOutputVoltage = core::quantities::Voltage{};
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

            // What the simulated output is delivering -- the current a real
            // instrument would report back, which no setpoint determines.
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
            Address                                   mAddress;
            int                                        mChannel;
            core::quantities::Voltage                 mOutputVoltage{};
            core::quantities::Current                 mSimOutputCurrent{};
            std::optional<core::quantities::Current>  mCurrentLimit;
            bool                                       mEnabled{ false };
    };

    //
    // A rig's instrument.inc (rig/instrument.inc in this repo) names
    // instruments by these aliases, not by N6701A<...> directly -- partly
    // readability, partly mechanical: the INSTRUMENT(type, id, address, ...)
    // macro in the rig's active_instruments.hpp splits its arguments on
    // top-level commas, and N6701A<RelayIsolated> would be split at the
    // angle brackets' own comma-free content just fine here, but a bare
    // comma inside a template argument list (as a future Isolation-like
    // parameter might need) would not be. Plain identifiers sidestep the
    // question entirely.
    //
    using N6701ADirect = N6701A<DirectWiring>;
    using N6701ARelay  = N6701A<RelayIsolated>;

    //
    // ADL targets for core::ApplyEngine/RemoveEngine -- see core/source.hpp's
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
    // ADL target for the electrical interlock -- see core/interlock.hpp on the
    // isEnergised customization point, and core::ConnectEngine/DisconnectEngine
    // for the two callers. Answers whether this supply's output is on at the
    // moment a relay in its path is about to move, which is the difference
    // between a contact moving cold and one arcing.
    //
    // Required of this config rather than optional, and the requirement is
    // checked: core::detail::energisedNow static_asserts it for any config that
    // has an applyDriver, on the grounds that a config Apply can energise and
    // that cannot say whether it currently is would silently report every
    // relay move as cold. Defined for both Isolation kinds even though only
    // SwitchableIsolation ever reaches Connect/Disconnect -- the question is
    // about the output, which both kinds have, and constraining it would make
    // the interlock's shape depend on whether there is a relay in the path.
    //
    template<typename Isolation>
    auto isEnergised( const N6701AConfig<Isolation> & config) -> bool
    {
        return config.Instrument.isEnabled();
    }

    //
    // ADL target for the run journal -- see core/describe.hpp's own comment on the
    // describeConfig customization point, and hal::describeSetting in
    // hal/describe.hpp for the optional-field helper. Found the same way
    // applyDriver above is, and required for the same reason: only this config's
    // own type knows which fields it has, so only code alongside it can say what
    // an Apply of it actually did.
    //
    // The mainframe slot is included, because it is the difference between two
    // DcP instances that share this class (see this file's own comment on
    // mChannel) and a log naming only "DcP2" tells a reader nothing about which
    // physical module was programmed.
    //
    template<typename Isolation>
    auto describeConfig( const N6701AConfig<Isolation> & config) -> core::SourceDescription
    {
        return core::SourceDescription{
            std::string( to_string( config.Instrument.id())),
            describeSettings( {
                describeSetting( "voltage",      config.Voltage),
                describeSetting( "currentLimit", config.CurrentLimit),
                "slot " + std::to_string( config.Instrument.channel())
            })
        };
    }

    //
    // ADL targets for core::ConnectEngine/DisconnectEngine -- see
    // core/route.hpp's own comment on the connectDriver/disconnectDriver
    // customization points. Closes -- or opens -- every fixed path
    // registered for this instrument, together (see
    // hal::InstrumentWiring::findAll() and hal::WireRole's own comment on
    // why force and any sense leads are meant to move as one unit here,
    // unlike a DMM's per-measurement sense choice). For most N6701A
    // instances that's just the one force channel -- findAll() over a
    // single entry behaves identically to find() -- but if a rig ever
    // wires remote-sense leads for a given DcP instance (WIRE_INSTRUMENT_SENSE
    // in its wiring.inc), they close and open right along with it, no
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
