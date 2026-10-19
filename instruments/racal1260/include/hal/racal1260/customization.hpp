#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/quantities/bytes.hpp"
#include "core/driver/describe.hpp"
#include "core/verbs/transfer.hpp"

#include "hal/driver/address.hpp"
#include "hal/driver/builder.hpp"
#include "hal/topology/bundle.hpp"
#include "hal/driver/describe.hpp"
#include "hal/driver/instrument.hpp"
#include "hal/fabric/switch_fabric.hpp"
#include "hal/topology/wiring.hpp"

#include "hal/racal1260/racal1260.hpp"

//
// the ADL hooks the verbs and journal find
//
// Part of hal/racal1260.hpp -- include that, not this.
//

namespace hal::racal1260
{
    //
    // ADL target for core::SetupEngine -- see core/verbs/source.hpp's own comment on
    // the setupDriver customization point, and on why configuring is a verb of
    // its own rather than a flavour of Apply.
    //
    // Note what this driver deliberately does NOT define: applyDriver and
    // removeDriver. A serial port has no output to energise -- there is nothing
    // an Apply( Ser1.rs232()) could mean -- so Apply on this instrument is "no
    // matching function" at compile time, exactly the way Connect( DcP1.dc())
    // is on a supply with no isolation relay (see hal::keysight_edu36311a::SwitchableIsolation).
    // The absence is the design.
    //
    inline auto setupDriver( const Rs232Config & config) -> void
    {
        config.Instrument.configurePort( config);
    }

    //
    // ADL targets for core::WriteEngine/core::ReadEngine -- the byte-oriented
    // verbs, see core/verbs/transfer.hpp.
    //
    inline auto writeDriver( const Rs232Config & config, const core::Bytes & payload) -> void
    {
        config.Instrument.transmit( payload);
    }

    [[nodiscard]]
    inline auto readDriver( const Rs232Config & config) -> core::Bytes
    {
        return config.Instrument.receive( config);
    }

    //
    // ADL target for the run journal -- see core/driver/describe.hpp's own comment on the
    // describeConfig customization point.
    //
    // Framing is reported field by field rather than as the "9600 8N1" shorthand
    // an engineer would say out loud, because the shorthand cannot express an
    // unset field: a Setup that named only the baud rate is a different
    // instruction from one that named all four, and "9600 8N1" would invent the
    // three it was never told. Same reasoning as hal::describeSetting's, which
    // this uses for exactly that.
    //
    inline auto describeConfig( const Rs232Config & config) -> core::SourceDescription
    {
        return core::SourceDescription{
            std::string( to_string( config.Instrument.id())),
            describeSettings( {
                config.BaudRate   ? "baud="       + std::to_string( *config.BaudRate)              : std::string{},
                config.WordLength ? "wordLength=" + std::to_string( *config.WordLength)            : std::string{},
                config.ParityMode ? "parity="     + std::string( to_string( *config.ParityMode))   : std::string{},
                config.Stop       ? "stopBits="   + std::string( to_string( *config.Stop))         : std::string{},
                config.Terminator.empty() ? std::string{} : "terminator=" + core::describeValue( config.Terminator),
                config.Timeout    ? "timeout="    + std::to_string( config.Timeout->count()) + "ms" : std::string{}
            })
        };
    }

    //
    // ADL targets for core::ConnectEngine/DisconnectEngine's bundle overloads.
    //
    // This is the routed connectDriver the other drivers in this repo do not
    // have: hal::keysight_edu36311a::DcSource and hal::keysight_ac6834b::AcSource close their own fixed channels and
    // never consult the connector side at all, because their outputs are cabled
    // to a known pin. Here both halves matter -- the port's own fixed channels
    // and the channels the DUT interface's lines are wired to -- and the route
    // is their concatenation, exactly the composition core::MeasureEngine
    // performs for a routed reading.
    //
    // Every line of the bundle, closed as one path. An RS232 console is not
    // usable a wire at a time (see core::ConnectEngine's bundle overload on why
    // the interface rather than the pin is the unit), and closing the set
    // together is also what hal::InstrumentWiring::findAll already does for
    // hal::keysight_ac6834b::AcSource's three phases and neutral -- the same idea reaching the
    // connector side.
    //
    // The pairing of which instrument channel meets which DUT line is left to
    // the wiring tables and is not asserted here, deliberately. A relay has no
    // notion of transmit and receive; it closes what it is given, and which of
    // those forms the outbound path is a fact about how the bench was cabled --
    // a rig/wiring.inc fact, checked by dut/tests/test_wiring_coverage.cpp, not
    // something a driver can know.
    //
    //
    // The composed route: this port's own fixed channels, plus the channel
    // every line of the destination interface is wired to.
    //
    // findAll rather than find on the instrument side, for the same reason
    // hal::keysight_edu36311a::DcSource's connectDriver uses it: a serial port is at least two wires
    // at the instrument end too, so it has more than one WIRE_INSTRUMENT row
    // (see rig/wiring.inc), and they close and open as a unit.
    //
    // Shared by connect and disconnect rather than written twice, because the
    // two must compose the identical path -- a disconnect that opened a
    // different set of channels from the one the connect closed would leave
    // relays latched with nothing in the code saying so.
    //
    template<typename BundleT>
    [[nodiscard]]
    auto routeTo( const InstrumentWiring &  instrumentWiring,
                  const ConnectorWiring &   connectorWiring,
                  const Rs232Config &   config) -> Path
    {
        auto path = instrumentWiring.findAll( config.Instrument.id());

        for( const auto location : bundleLocations<BundleT>)
        {
            const auto linePath = connectorWiring.find( location);

            path.insert( path.end(), linePath.begin(), linePath.end());
        }

        return path;
    }

    template<typename BundleT>
    auto connectDriver( SwitchFabric &            fabric,
                        const InstrumentWiring &  instrumentWiring,
                        const ConnectorWiring &   connectorWiring,
                        const Rs232Config &   config,
                        const core::AdapterBundle<BundleT> &) -> void
    {
        fabric.connect( routeTo<BundleT>( instrumentWiring, connectorWiring, config));

        config.Instrument.setConnected( true);
    }

    template<typename BundleT>
    auto disconnectDriver( SwitchFabric &            fabric,
                           const InstrumentWiring &  instrumentWiring,
                           const ConnectorWiring &   connectorWiring,
                           const Rs232Config &   config,
                           const core::AdapterBundle<BundleT> &) -> void
    {
        fabric.disconnect( routeTo<BundleT>( instrumentWiring, connectorWiring, config));

        config.Instrument.setConnected( false);
    }
} // namespace hal::racal1260
