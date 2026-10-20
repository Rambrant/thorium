#pragma once

#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>

#include "core/driver/describe.hpp"
#include "core/driver/port.hpp"
#include "core/quantities/quantity.hpp"

#include "hal/driver/address.hpp"
#include "hal/driver/box_connection.hpp"
#include "hal/driver/builder.hpp"
#include "hal/driver/describe.hpp"
#include "hal/driver/instrument.hpp"
#include "hal/fabric/switch_fabric.hpp"
#include "hal/io/scpi.hpp"
#include "hal/topology/wiring.hpp"

#include "hal/keysight_edu36311a_dc_source.hpp"

//
// the ADL hooks the verbs and journal find
//
// Part of hal/keysight_edu36311a.hpp -- include that, not this.
//

namespace hal::keysight_edu36311a
{
    //
    // ADL targets for core::ApplyEngine/RemoveEngine -- see
    // core/verbs/source.hpp's own comment on the applyDriver/removeDriver
    // customization points. Programs -- or disables -- the output only; the
    // fabric path is a separate concern, see connectDriver/disconnectDriver
    // below. Defined for both Isolation kinds identically: programming a
    // supply's output does not care whether it has a relay in its path.
    //
    // Found via ADL because DcConfig lives in this namespace, which is also
    // what keeps these out of the pile every driver used to share.
    //
    template<typename Output, typename Isolation>
    auto applyDriver( const DcConfig<Output, Isolation> & config) -> void
    {
        config.Instrument.applyOutput( config.Voltage, config.CurrentLimit, config.OverVoltage);
    }

    template<typename Output, typename Isolation>
    auto removeDriver( const DcConfig<Output, Isolation> & config) -> void
    {
        config.Instrument.removeOutput();
    }

    //
    // What one output was programmed to, as setpoint readbacks (VOLTage?,
    // CURRent?) rather than measurements: measuredVoltage() answers what the
    // source says it is producing, which is the right question for a check and
    // the wrong one for a restore. Re-applying a measured 23.98 V would program
    // the rig to a value nobody asked for, and would do it again, a little
    // further off, on every pass.
    //
    // CurrentLimit is optional because "never programmed" is a state the
    // instrument distinguishes and a restore has to preserve: std::nullopt means
    // leave that setting alone, which is what it means everywhere else in
    // DcConfig. Voltage is not optional, because the driver's own readback is
    // not -- an output voltage is always some value.
    //
    struct DcSnapshot
    {
        core::quantities::Voltage                Voltage{};
        std::optional<core::quantities::Current> CurrentLimit{};
    };

    //
    // ADL targets for hal::SourceSnapshot (hal/verbs/snapshot.hpp), which holds
    // the value and decides *whether* to put it back; these two say what there
    // is to hold and how to program it again, which only code alongside
    // DcConfig can.
    //
    // restoreDriver returns the builder rather than applying it, so the caller
    // can Connect and then Apply the same object -- chained through a named
    // local because a DcConfig holds a reference to its instrument: the builder
    // is copy-constructible, which the ternary needs, and not copy-assignable,
    // which `builder = builder.currentLimit( ...)` would need.
    //
    template<typename Output, typename Isolation>
    auto snapshotDriver( const DcSource<Output, Isolation> & source) -> DcSnapshot
    {
        return DcSnapshot{ source.outputVoltage(), source.currentLimit() };
    }

    template<typename Output, typename Isolation>
    auto portDriver( DcSource<Output, Isolation> & source)
    {
        return source.dc();
    }

    template<typename Output, typename Isolation>
    auto restoreDriver( DcSource<Output, Isolation> & source, const DcSnapshot & snapshot)
    {
        const auto voltage = source.dc().voltage( snapshot.Voltage);

        return snapshot.CurrentLimit
                   ? voltage.currentLimit( *snapshot.CurrentLimit)
                   : voltage;
    }

    //
    // ADL target for the electrical interlock -- see core/verbs/interlock.hpp
    // on the isEnergised customization point, and
    // core::ConnectEngine/DisconnectEngine for the two callers. Answers
    // whether this output is on at the moment a relay in its path is about to
    // move.
    //
    // Through outputIsOn() rather than isEnabled(), which asks the instrument
    // rather than this driver's memory -- see those two members' shared
    // comment for the whole argument. Note this config is const and its
    // Instrument member is a reference, so a non-const query through it is
    // available here: the constness that stops hal::energisedSourceAt() doing
    // the same is the constness of the *instrument*, not of the config.
    //
    // Required of this config rather than optional, and the requirement is
    // checked: core::detail::energisedNow static_asserts it for any config
    // that has an applyDriver, on the grounds that a config Apply can energise
    // and that cannot say whether it currently is would silently report every
    // relay move as cold. Defined for both Isolation kinds even though only
    // SwitchableIsolation ever reaches Connect/Disconnect -- the question is
    // about the output, which both kinds have.
    //
    template<typename Output, typename Isolation>
    auto isEnergised( const DcConfig<Output, Isolation> & config) -> bool
    {
        return config.Instrument.outputIsOn();
    }

    //
    // ADL target for the run journal -- see core/driver/describe.hpp's own
    // comment on the describeConfig customization point, and
    // hal::describeSetting in hal/driver/describe.hpp for the optional-field
    // helper. Only this config's own type knows which fields it has, so only
    // code alongside it can say what an Apply of it actually did.
    //
    // The output's identity is included, and it has to be: three instances of
    // this driver sit behind one address on this rig, and a log line naming
    // only "DcP6" tells a reader nothing about which of the box's three
    // outputs was programmed -- nor, more usefully, what that output's limits
    // were. "output 2 (30 V / 1 A)" says both.
    //
    template<typename Output, typename Isolation>
    auto describeConfig( const DcConfig<Output, Isolation> & config) -> core::SourceDescription
    {
        using Instrument = DcSource<Output, Isolation>;

        return core::SourceDescription{
            std::string( to_string( config.Instrument.id())),
            describeSettings( {
                describeSetting( "voltage",      config.Voltage),
                describeSetting( "currentLimit", config.CurrentLimit),
                describeSetting( "ovp",          config.OverVoltage),
                "output " + std::to_string( Instrument::channel())
                    + " (" + std::string( Instrument::rating()) + ")"
            })
        };
    }

    //
    // ADL targets for core::ConnectEngine/DisconnectEngine -- see
    // core/verbs/route.hpp's own comment on the connectDriver/disconnectDriver
    // customization points. Closes -- or opens -- every fixed path registered
    // for this instrument, together (see hal::InstrumentWiring::findAll() and
    // hal::WireRole on why force and any sense leads are meant to move as one
    // unit here). No connector-side hop: there is no connector path to look up
    // (see DcConfig's own comment), so connectorWiring is accepted for
    // signature symmetry with every other instrument's connectDriver and never
    // consulted.
    //
    // Constrained by SwitchableIsolation, so there is no version of these that
    // instantiates for a DirectWiring output at all: Connect( DcP5.dc()) fails
    // overload resolution outright, which is the same
    // "misspelled-or-impossible fails to compile" guarantee this project
    // applies everywhere else. One generic template rather than one overload
    // per relay-having tag, so that a future isolation tag setting HasRelay
    // gets these for free.
    //
    template<typename Output, typename Isolation>
        requires SwitchableIsolation<Isolation>
    auto connectDriver( SwitchFabric & fabric, const InstrumentWiring & instrumentWiring, const ConnectorWiring &, const DcConfig<Output, Isolation> & config) -> void
    {
        fabric.connect( instrumentWiring.findAll( config.Instrument.id()));
    }

    template<typename Output, typename Isolation>
        requires SwitchableIsolation<Isolation>
    auto disconnectDriver( SwitchFabric & fabric, const InstrumentWiring & instrumentWiring, const ConnectorWiring &, const DcConfig<Output, Isolation> & config) -> void
    {
        fabric.disconnect( instrumentWiring.findAll( config.Instrument.id()));
    }
} // namespace hal::keysight_edu36311a
